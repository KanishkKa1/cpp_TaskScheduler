#include "ThreadPool.hpp"

#include "Worker.hpp"

#include <cstdlib>
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads) : task_queue_(100), worker_states_(num_threads) {
    workers_.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(Worker(task_queue_, this, i));
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

// Initiates shutdown
void ThreadPool::shutdown() noexcept {
    if (task_queue_.is_shutdown())
        return;

    task_queue_.shutdown();
}

/**
 * Steal a single task from other workers.
 *
 * Strategy:
 * - Randomized victim selection to reduce contention
 * - FIFO steal (front) → fairness
 */
bool ThreadPool::try_steal_task(int thief_id, Task &stolen_task) {
    size_t n = worker_states_.size();
    size_t start = rand() % n;

    for (size_t i = 0; i < n; ++i) {
        int victim = (start + i) % n;

        if (victim == thief_id) {
            continue;
        }

        auto &vs = worker_states_[victim];

        std::lock_guard<std::mutex> lock(vs.mutex);

        if (!vs.local_queue.empty()) {
            stolen_task = std::move(vs.local_queue.front());
            vs.local_queue.pop_front();
            return true;
        }
    }
    return false;
}

/**
 * Batch steal:
 * - Reduces frequency of stealing attempts
 * - Limited to avoid long lock holding
 */
bool ThreadPool::try_steal_tasks_batch(int thief_id, std::vector<Task> &stolen_tasks) {
    size_t n = worker_states_.size();

    for (size_t i = 0; i < n; ++i) {
        int victim = (thief_id + i + 1) % n;

        if (victim == thief_id) {
            continue;
        }

        auto &vs = worker_states_[victim];

        std::lock_guard<std::mutex> lock(vs.mutex);

        size_t victim_size_for_steal = vs.local_queue.size();

        if (victim_size_for_steal <= 1) {
            continue;
        }

        size_t steal_count = std::min(victim_size_for_steal / 2, MAX_STEAL_BATCH);

        for (size_t j = 0; j < steal_count; ++j) {
            stolen_tasks.push_back(std::move(vs.local_queue.front()));
            vs.local_queue.pop_front();
        }
        return true;
    }

    return false;
}

/**
 * Cooperative execution helper.
 *
 * Execution priority:
 * 1. Local queue (LIFO)
 * 2. Steal from others
 * 3. Global queue
 * 4. Yield (avoid busy spin)
 */
void ThreadPool::help_one_task() {
    if (worker_id == -1) {
        return;
    }

    auto &state = get_worker_state(worker_id);

    Task task;

    // 1. Try local queue (fast path, LIFO)
    if (state.mutex.try_lock()) {
        if (!state.local_queue.empty()) {
            task = std::move(state.local_queue.back());
            state.local_queue.pop_back();
            state.mutex.unlock();

            on_task_start();
            task();
            on_task_end();

            return;
        }

        state.mutex.unlock();
    }

    // 2. Try batch stealing
    std::vector<Task> stolen;
    if (try_steal_tasks_batch(worker_id, stolen)) {
        for (auto &t : stolen) {
            on_task_start();
            t();
            on_task_end();
        }
        return;
    }

    // 3. Try global queue
    auto opt = task_queue_.try_pop();
    if (opt) {
        on_task_start();
        (*opt)();
        on_task_end();

        return;
    }

    // 4. No work available → yield CPU
    std::this_thread::yield();
}