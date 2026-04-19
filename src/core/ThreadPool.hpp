#pragma once

#include "core/SafeQueue.hpp"
#include "task/Task.hpp"

#include <cassert>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

// Thread local worker identifier.
// - Set by worker on thread start
extern thread_local int worker_id;

// Represents per -worker state
// Each worker has its own local deque for fast LIFO execution
struct WorkerState {
    std::deque<Task> local_queue;
    std::mutex mutex;
};

class ThreadPool {
  private:
    // Global task queue (bounded,thread-safe)
    SafeQueue<Task> task_queue_;

    // Worker threads
    std::vector<std::jthread> workers_;

    // Metrics
    std::atomic<size_t> active_workers_{0};
    std::atomic<size_t> total_submitted_{0};
    std::atomic<size_t> total_completed_{0};

    // Per-worker local queues for work stealing
    std::vector<WorkerState> worker_states_;

    // Maximum number of tasks to steal in one batch
    static constexpr size_t MAX_STEAL_BATCH = 4;

    // Prevent unbounded growth of local queues
    static constexpr size_t LOCAL_QUEUE_THREASHOLD = 64;
  public:
    explicit ThreadPool(size_t num_threads);

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    ~ThreadPool() noexcept;

    /**
     * Submit a task to the thread pool.
     *
     * Key properties:
     * - Supports arbitrary callable + arguments
     * - Returns std::future for result retrieval
     * - Exceptions are propagated via promise
     *
     * Scheduling strategy:
     * - Worker thread → push to local queue (fast path)
     * - External thread → push to global queue
     * - If local queue overloaded → fallback to global queue
     */
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>> {

        using ReturnType = std::invoke_result_t<F, Args...>;

        auto promise_ptr = std::make_shared<std::promise<ReturnType>>();

        // Wrap user function into a task abstraction
        Task t(
            [f_ = std::forward<F>(f), ... args_ = std::forward<Args>(args), promise_ptr]() mutable {
                try {
                    if constexpr (std::is_void_v<ReturnType>) {
                        std::invoke(f_, std::move(args_)...);
                        promise_ptr->set_value();
                    } else {
                        auto result = std::invoke(f_, std::move(args_)...);
                        promise_ptr->set_value(std::move(result));
                    }
                } catch (...) {
                    promise_ptr->set_exception(std::current_exception());
                }
            });

        // Ensure worker_id validity
        assert(worker_id < (int)worker_states_.size());

        if (worker_id != -1) {
            // Submission for worker thread - try push to local queue
            auto &state = get_worker_state(worker_id);

            bool pushed_to_local = false;

            {
                std::lock_guard<std::mutex> lock(state.mutex);
                // Prevent unbounded loacl queue growth
                if (state.local_queue.size() < LOCAL_QUEUE_THREASHOLD) {
                    state.local_queue.push_back(std::move(t));
                    pushed_to_local = true;
                }
            }

            // Fallback to global queue if local queue is overloaded
            if (!pushed_to_local) {
                if (!task_queue_.push(std::move(t))) {
                    throw std::runtime_error("ThreadPool is shutdown. Cannot submit new tasks.");
                }
            }
        } else {
            // External thread → always use global queue
        if (!task_queue_.push(std::move(t))) {
                throw std::runtime_error("ThreadPool is shutdown. Cannot submit new tasks.");
            }
        }

        total_submitted_.fetch_add(1, std::memory_order_relaxed);
        total_inflight_.fetch_add(1, std::memory_order_relaxed);

        return promise_ptr->get_future();
    }

    void shutdown() noexcept;

    bool is_shutdown() const noexcept {
        return task_queue_.is_shutdown();
    }

    // Metrics hooks
    void on_task_start() noexcept {
        active_workers_.fetch_add(1, std::memory_order_relaxed);
    }

    void on_task_end() noexcept {
        active_workers_.fetch_sub(1, std::memory_order_relaxed);
        total_completed_.fetch_add(1, std::memory_order_relaxed);
    }

    // Observable APIs
    size_t pending_task() const {
        return task_queue_.size();
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

    // Accessor for worker state by thread ID
    WorkerState &get_worker_state(int id) {
        return worker_states_[id];
    }

    // Work stealing APIs
    bool try_steal_task(int thief_id, Task &stolen_task);
    bool try_steal_tasks_batch(int thief_id, std::vector<Task> &stolen_tasks);
};