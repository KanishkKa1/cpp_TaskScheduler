#pragma once

#include "core/BlockingPriorityQueue.hpp"
#include "core/SafeQueue.hpp"
#include "task/Task.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

// =======================
// Thread-local worker id
// =======================
extern thread_local int worker_id;

// =======================
// Scheduled Task
// =======================
struct ScheduledTask {
    int priority;
    Task task;
};

// =======================
// Worker State
// =======================
struct WorkerState {
    std::deque<ScheduledTask> local_queue;
    mutable std::mutex mutex;
};

// Creates a max-heap by priority and highest priority comes first
struct CompareReady {
    bool operator()(const ScheduledTask &a, const ScheduledTask &b) const {
        return a.priority < b.priority;
    }
};

// =======================
// ThreadPool
// =======================
class ThreadPool {
  private:
    // =======================
    // GLOBAL QUEUE
    // =======================
    // SafeQueue<ScheduledTask> task_queue_;
    BlockingPriorityQueue<ScheduledTask, CompareReady> ready_q_;

    std::vector<std::thread> workers_;
    std::vector<WorkerState> worker_states_;

    // =======================
    // DELAYED TASK SYSTEM
    // =======================
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    struct DelayedTask {
        TimePoint execute_at;
        ScheduledTask task;
    };

    struct CompareDelayed {
        bool operator()(const DelayedTask &a, const DelayedTask &b) const {
            return a.execute_at > b.execute_at;
        }
    };

    using DelayedQueue = std::priority_queue<DelayedTask, std::vector<DelayedTask>, CompareDelayed>;

    DelayedQueue delayed_queue_;
    mutable std::mutex delayed_mutex_;
    std::condition_variable delayed_cv_;
    std::thread timer_thread_;
    std::atomic<bool> stop_timer_{false};

    // =====================
    // Worker signaling
    // ======================
    std::condition_variable worker_cv_;
    std::mutex worker_cv_mutex_;

    // =======================
    // Metrics
    // =======================
    std::atomic<size_t> active_workers_{0};
    std::atomic<size_t> total_submitted_{0};
    std::atomic<size_t> total_completed_{0};
    std::atomic<size_t> total_inflight_{0};

    static constexpr size_t MAX_STEAL_BATCH = 4;
    static constexpr size_t LOCAL_QUEUE_THRESHOLD = 64;

    // ====================
    // shutdown
    std::atomic<bool> shutdown_flag_{false};

  public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool() noexcept;

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    // =======================
    // SUBMIT
    // =======================
    template <typename F, typename... Args> auto submit(F &&f, Args &&...args) {
        return submit_with_priority(0, std::forward<F>(f), std::forward<Args>(args)...);
    }

    // =======================
    // SUBMIT WITH PRIORITY
    // =======================
    template <typename F, typename... Args>
    auto submit_with_priority(int priority, F &&f, Args &&...args)
        -> std::future<std::invoke_result_t<F, Args...>> {

        using ReturnType = std::invoke_result_t<F, Args...>;

        if (shutdown_flag_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("ThreadPool shutdown");
        }

        auto promise_ptr = std::make_shared<std::promise<ReturnType>>();

        ScheduledTask st{priority,
                         Task([f_ = std::forward<F>(f), ... args_ = std::forward<Args>(args),
                               promise_ptr]() mutable {
                             try {
                                 if constexpr (std::is_void_v<ReturnType>) {
                                     std::invoke(f_, std::move(args_)...);
                                     promise_ptr->set_value();
                                 } else {
                                     promise_ptr->set_value(std::invoke(f_, std::move(args_)...));
                                 }
                             } catch (...) {
                                 promise_ptr->set_exception(std::current_exception());
                             }
                         })};

        // Worker thread → local queue (fast path)
        // if (worker_id >= 0 && worker_id < (int)worker_states_.size()) {
        //     auto &state = worker_states_[worker_id];

        //     bool pushed_local = false;

        //     {
        //         std::lock_guard<std::mutex> lock(state.mutex);
        //         if (state.local_queue.size() < LOCAL_QUEUE_THRESHOLD) {
        //             state.local_queue.push_back(std::move(st));
        //             pushed_local = true;
        //         }
        //     }

        //     if (!pushed_local) {
        //         // if (!task_queue_.push(std::move(st))) {
        //         //     throw std::runtime_error("ThreadPool shutdown");
        //         // }
        //         if (!ready_q_.push(std::move(st))) {
        //             throw std::runtime_error("ThreadPool shutdown");
        //         }
        //     }
        // }
        // // External thread → global queue
        // else {
        //     // if (!task_queue_.push(std::move(st))) {
        //     //     throw std::runtime_error("ThreadPool shutdown");
        //     // }
        //     if (!ready_q_.push(std::move(st))) {
        //         throw std::runtime_error("ThreadPool shutdown");
        //     }
        // }

        bool is_worker = (worker_id >= 0 && worker_id < (int)worker_states_.size());

        if (!is_worker || priority > 0) {
            // HIGH priority OR external thread → always global
            if (!ready_q_.push(std::move(st))) {
                throw std::runtime_error("ThreadPool shutdown");
            }
        } else {
            // LOW priority from worker → local queue (fast path)
            auto &state = worker_states_[worker_id];

            bool pushed_local = false;

            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (state.local_queue.size() < LOCAL_QUEUE_THRESHOLD) {
                    state.local_queue.push_back(std::move(st));
                    pushed_local = true;
                }
            }

            if (!pushed_local) {
                ready_q_.push(std::move(st));
            }
        }

        worker_cv_.notify_one();

        total_submitted_.fetch_add(1, std::memory_order_relaxed);
        total_inflight_.fetch_add(1, std::memory_order_relaxed);

        return promise_ptr->get_future();
    }

    // =======================
    // SUBMIT WITH DELAY
    // =======================
    template <typename F, typename... Args>
    auto submit_after(std::chrono::milliseconds delay, F &&f, Args &&...args)
        -> std::future<std::invoke_result_t<F, Args...>> {

        return submit_after_with_priority(0, delay, std::forward<F>(f),
                                          std::forward<Args>(args)...);
    }

    // =======================
    // SUBMIT WITH DELAY + PRIORITY
    // =======================
    template <typename F, typename... Args>
    auto submit_after_with_priority(int priority, std::chrono::milliseconds delay, F &&f,
                                    Args &&...args)
        -> std::future<std::invoke_result_t<F, Args...>> {

        using ReturnType = std::invoke_result_t<F, Args...>;

        if (shutdown_flag_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("ThreadPool shutdown");
        }

        auto promise_ptr = std::make_shared<std::promise<ReturnType>>();

        ScheduledTask st{priority,
                         Task([f_ = std::forward<F>(f), ... args_ = std::forward<Args>(args),
                               promise_ptr]() mutable {
                             try {
                                 if constexpr (std::is_void_v<ReturnType>) {
                                     std::invoke(f_, std::move(args_)...);
                                     promise_ptr->set_value();
                                 } else {
                                     promise_ptr->set_value(std::invoke(f_, std::move(args_)...));
                                 }
                             } catch (...) {
                                 promise_ptr->set_exception(std::current_exception());
                             }
                         })};

        auto execute_at = Clock::now() + delay;

        {
            std::unique_lock<std::mutex> lock(delayed_mutex_);

            bool notify = delayed_queue_.empty() || execute_at < delayed_queue_.top().execute_at;

            delayed_queue_.push(DelayedTask{execute_at, std::move(st)});

            lock.unlock();

            if (notify) {
                delayed_cv_.notify_one();
            }
        }

        total_submitted_.fetch_add(1, std::memory_order_relaxed);
        total_inflight_.fetch_add(1, std::memory_order_relaxed);

        return promise_ptr->get_future();
    }

    // =======================
    // Lifecycle
    // =======================
    void shutdown() noexcept;

    bool is_shutdown() const noexcept {
        // return task_queue_.is_shutdown();
        return ready_q_.is_shutdown();
    }

    bool should_worker_exit(int id) const;

    // =======================
    // Worker API
    // =======================
    WorkerState &get_worker_state(int id);

    bool try_steal_task(int thief_id, ScheduledTask &stolen);
    bool try_steal_tasks_batch(int thief_id, std::vector<ScheduledTask> &stolen);

    void help_one_task();
    void timer_loop();

    // =======================
    // Metrics hooks
    // =======================
    void on_task_start() noexcept {
        active_workers_.fetch_add(1, std::memory_order_relaxed);
    }

    void on_task_end() noexcept {
        active_workers_.fetch_sub(1, std::memory_order_relaxed);
        total_completed_.fetch_add(1, std::memory_order_relaxed);
        total_inflight_.fetch_sub(1, std::memory_order_relaxed);
    }

    // =======================
    // Observability
    // =======================
    size_t pending_task() const {
        // return task_queue_.size();
        return ready_q_.size();
    }

    size_t active_workers() const {
        return active_workers_;
    }

    size_t total_submitted() const {
        return total_submitted_;
    }

    size_t total_completed() const {
        return total_completed_;
    }

    bool is_idle() const noexcept {
        return total_inflight_.load(std::memory_order_relaxed) == 0;
    }
};