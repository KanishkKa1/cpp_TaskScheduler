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

class ThreadPool {
  private:
    SafeQueue<Task> task_queue_;
    std::vector<std::jthread> workers_;

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
        auto future = promise_ptr->get_future();

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

        return future;
    }

    void shutdown() noexcept;

    bool is_shutdown() const noexcept {
        return task_queue_.is_shutdown();
    }
};