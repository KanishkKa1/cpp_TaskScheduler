#include "ThreadPool.hpp"

#include <exception>
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads) : task_queue_(100) {
    workers_.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            while (auto task = task_queue_.pop()) {
                try {
                    (*task)();
                } catch (const std::exception &e) {
                    // Prevent worker thread termination due to task exceptions.
                    std::cerr << "Task exception: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Task threw an unknown exception." << std::endl;
                }
            }
        });
    }
}

// Destructor triggers shutdown.
// std::jthread ensures all worker threads are joined before destruction.
ThreadPool::~ThreadPool() {
    shutdown();
}

// Signals all workers to stop accepting new tasks and begin shutdown.
void ThreadPool::shutdown() noexcept {
    if (task_queue_.is_shutdown())
        return;

    task_queue_.shutdown();
}