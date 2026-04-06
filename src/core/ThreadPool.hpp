#pragma once

#include "core/SafeQueue.hpp"
#include "core/Worker.hpp"
#include "task/Task.hpp"

#include <functional>
#include <future>
#include <stdexcept>
#include <thread>
#include <type_traits>

class ThreadPool {
  private:
    SafeQueue<Task> task_queue_;
    std::vector<std::jthread> workers_;

  public:
    // constructor
    explicit ThreadPool(size_t num_threads);

    // non-copyable
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    // destructor
    ~ThreadPool();

    // Submits a callable with arguments to the thread pool.
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        std::promise<ReturnType> promise;
        auto future = promise.get_future();

        // Wrap user function into a task
        Task t([f_ = std::forward<F>(f), ... args_ = std::forward<Args>(args),
                p_ = std::move(promise)]() mutable {
            try {
                if constexpr (std::is_void_v<ReturnType>) {
                    std::invoke(f_, std::move(args_)...);
                    p_.set_value();
                } else {
                    auto result = std::invoke(f_, std::move(args_)...);
                    p_.set_value(std::move(result));
                }
            } catch (...) {
                p_.set_exception(std::current_exception());
            }
        });

        // Enqueue the task for execution
        task_queue_.push(std::move(t));

        return future;
    }

    // Stops the thread pool. No new tasks will be accepted, but existing tasks will be completed.
    void shutdown() noexcept;
};