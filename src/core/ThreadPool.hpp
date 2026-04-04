#pragma once

#include "core/SafeQueue.hpp"
#include "core/Worker.hpp"
#include "task/Task.hpp"

#include <stdexcept>
#include <thread>
#include <vector>

class ThreadPool {
  private:
    SafeQueue<Task> task_queue_;
    std::vector<std::jthread> workers_;

  public:
    explicit ThreadPool(size_t num_threads);

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    ~ThreadPool();

    // Submits a task to the thread pool.
    // Throws if the pool is shutting down.
    template <typename F> void submit(F &&f) {
        Task task = std::forward<F>(f);
        if (!task_queue_.push(std::move(task))) {
            throw std::runtime_error("ThreadPool is shutting down, cannot submit new tasks.");
        }
    }

    void shutdown() noexcept;
};