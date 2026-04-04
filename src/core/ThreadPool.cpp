#include "ThreadPool.hpp"

ThreadPool::ThreadPool(size_t num_threads) {
    workers_.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            Worker worker(task_queue_);
            worker();
        });
    }
}

// Destructor triggers shutdown and blocks until all worker threads exit.
// Remaining tasks in the queue are executed before termination.
ThreadPool::~ThreadPool() {
    shutdown();
}

// Signals all workers to stop accepting new tasks and begin shutdown.
void ThreadPool::shutdown() noexcept {
    task_queue_.shutdown();
}