#pragma once

#include "core/SafeQueue.hpp"
#include "task/Task.hpp"

#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

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

  public:
    explicit ThreadPool(size_t num_threads);

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    ~ThreadPool() noexcept;

    // Submits a callable to the thread pool and returns a future.
    // - Executes task asynchronously on worker threads
    // - Propagates exceptions via future
    // - Throws std::runtime_error if pool is shutdown
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>> {

        using ReturnType = std::invoke_result_t<F, Args...>;

        auto promise_ptr = std::make_shared<std::promise<ReturnType>>();

        // Wrap user function into a task
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

        if (!task_queue_.push(std::move(t))) {
            throw std::runtime_error("ThreadPool is shutdown, cannot submit new tasks");
        }

        total_submitted_++;

        auto future = promise_ptr->get_future();
        return future;
    }

    void shutdown() noexcept;

    bool is_shutdown() const noexcept {
        return task_queue_.is_shutdown();
    }

    void on_task_start() noexcept {
        active_workers_++;
    }

    void on_task_end() noexcept {
        active_workers_--;
        total_completed_++;
    }

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

    // Work stealing APIs
    bool try_steal_task(int thief_id, Task &stolen_task);
    bool try_steal_tasks_batch(int thief_id, std::vector<Task> &stolen_tasks);
};