#include "ThreadPool.hpp"

#include "Worker.hpp"

#include <algorithm>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

// =======================
// Constructor
// =======================
// ThreadPool::ThreadPool(size_t num_threads) : task_queue_(100), worker_states_(num_threads) {
ThreadPool::ThreadPool(size_t num_threads) : worker_states_(num_threads) {

    workers_.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this, i] {
            worker_id = static_cast<int>(i);

            while (true) {
                help_one_task();

                // if (task_queue_.is_shutdown() && is_idle()) {
                //     break;
                // }
                if (ready_q_.is_shutdown() && is_idle()) {
                    break;
                }
            }

            worker_id = -1;
        });
    }

    timer_thread_ = std::thread([this] { timer_loop(); });
}

// =======================
// Destructor
// =======================
ThreadPool::~ThreadPool() noexcept {
    shutdown();

    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }

    for (auto &t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

// =======================
// Shutdown
// =======================
void ThreadPool::shutdown() noexcept {
    bool expected = false;
    if (!shutdown_flag_.compare_exchange_strong(expected, true)) {
        return;
    }

    ready_q_.shutdown();

    // stop timer thread
    stop_timer_.store(true, std::memory_order_relaxed);

    delayed_cv_.notify_all();
    worker_cv_.notify_all();
}

// =======================
// Timer Loop for Delayed Tasks
// =======================
void ThreadPool::timer_loop() {
    std::unique_lock<std::mutex> lock(delayed_mutex_);

    while (true) {
        // Exit condition
        if (stop_timer_.load(std::memory_order_relaxed) && delayed_queue_.empty()) {
            break;
        }

        // Case 1: no delayed tasks → wait
        if (delayed_queue_.empty()) {
            delayed_cv_.wait(lock, [&] {
                return stop_timer_.load(std::memory_order_relaxed) || !delayed_queue_.empty();
            });
            continue;
        }

        auto now = Clock::now();
        auto next_time = delayed_queue_.top().execute_at;

        // Case 2: not ready → timed wait
        if (now < next_time) {
            delayed_cv_.wait_until(lock, next_time, [&] {
                return stop_timer_.load(std::memory_order_relaxed) ||
                       (!delayed_queue_.empty() && delayed_queue_.top().execute_at < next_time);
            });
            continue;
        }

        // Case 3: ready tasks → move out
        std::vector<ScheduledTask> ready;

        while (!delayed_queue_.empty()) {
            auto &top_ref = const_cast<DelayedTask &>(delayed_queue_.top());

            if (top_ref.execute_at > Clock::now())
                break;

            DelayedTask dt = std::move(top_ref);
            delayed_queue_.pop();

            ready.push_back(std::move(dt.task));
        }

        lock.unlock();

        // Push to global queue
        for (auto &task : ready) {
            // task_queue_.push(std::move(task));
            ready_q_.push(std::move(task));
        }

        worker_cv_.notify_all();

        lock.lock();
    }
}
// =======================
// Worker state access
// =======================
WorkerState &ThreadPool::get_worker_state(int id) {
    return worker_states_[id];
}

// =======================
// Single Task Steal
// =======================
bool ThreadPool::try_steal_task(int thief_id, ScheduledTask &stolen) {
    const size_t n = worker_states_.size();

    for (size_t i = 0; i < n; ++i) {
        const int victim = static_cast<int>((thief_id + i + 1) % n);
        if (victim == thief_id)
            continue;

        auto &vs = worker_states_[victim];

        if (vs.mutex.try_lock()) {
            if (!vs.local_queue.empty()) {
                stolen = std::move(vs.local_queue.front());
                vs.local_queue.pop_front();
                vs.mutex.unlock();
                return true;
            }
            vs.mutex.unlock();
        }
    }
    return false;
}

// =======================
// Batch Steal
// =======================
bool ThreadPool::try_steal_tasks_batch(int thief_id, std::vector<ScheduledTask> &stolen) {
    const size_t n = worker_states_.size();

    for (size_t i = 0; i < n; ++i) {
        const int victim = static_cast<int>((thief_id + i + 1) % n);
        if (victim == thief_id)
            continue;

        auto &vs = worker_states_[victim];

        if (vs.mutex.try_lock()) {
            const size_t victim_size = vs.local_queue.size();

            if (victim_size > 1) {
                const size_t steal_count =
                    std::min(victim_size / 2, static_cast<size_t>(MAX_STEAL_BATCH));

                stolen.reserve(steal_count);

                for (size_t j = 0; j < steal_count; ++j) {
                    stolen.push_back(std::move(vs.local_queue.front()));
                    vs.local_queue.pop_front();
                }

                vs.mutex.unlock();
                return true;
            }

            vs.mutex.unlock();
        }
    }

    return false;
}

// =======================
// Core Execution Step
// =======================
void ThreadPool::help_one_task() {
    if (worker_id == -1) {
        return;
    }

    auto &state = get_worker_state(worker_id);

    // -----------------------
    // 1. GLOBAL (PRIORITY)
    // -----------------------
    // if (auto opt = task_queue_.try_pop()) {
    if (auto opt = ready_q_.try_pop()) {
        ScheduledTask st = std::move(*opt);

        on_task_start();
        try {
            st.task();
        } catch (const std::exception &e) {
            std::cerr << "[ThreadPool] Task exception: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "[ThreadPool] Unknown task exception\n";
        }
        on_task_end();

        return;
    }

    // -----------------------
    // 2. GLOBAL AGAIN (close race)
    // -----------------------
    // if (auto opt = task_queue_.try_pop()) {
    if (auto opt = ready_q_.try_pop()) {
        ScheduledTask st = std::move(*opt);

        on_task_start();
        try {
            st.task();
        } catch (const std::exception &e) {
            std::cerr << "[ThreadPool] Task exception: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "[ThreadPool] Unknown task exception\n";
        }
        on_task_end();

        return;
    }

    // -----------------------
    // 3. LOCAL (LIFO)
    // -----------------------
    // if (state.mutex.try_lock()) {
    //     if (!state.local_queue.empty()) {
    //         ScheduledTask st = std::move(state.local_queue.back());
    //         state.local_queue.pop_back();
    //         state.mutex.unlock();

    //         on_task_start();
    //         try {
    //             st.task();
    //         } catch (const std::exception &e) {
    //             std::cerr << "[ThreadPool] Task exception: " << e.what() << '\n';
    //         } catch (...) {
    //             std::cerr << "[ThreadPool] Unknown task exception\n";
    //         }
    //         on_task_end();

    //         return;
    //     }
    //     state.mutex.unlock();
    // }

    // -----------------------
    // LOCAL (priority-aware)
    // -----------------------
    auto global_top = ready_q_.peek();

    if (state.mutex.try_lock()) {
        if (!state.local_queue.empty()) {

            auto &local_task = state.local_queue.back();

            bool should_run_local = false;

            if (!global_top.has_value()) {
                should_run_local = true;
            } else {
                // allow local only if it is >= global priority
                should_run_local = (local_task.priority >= global_top->get().priority);
            }

            if (should_run_local) {
                ScheduledTask st = std::move(local_task);
                state.local_queue.pop_back();
                state.mutex.unlock();

                on_task_start();
                try {
                    st.task();
                } catch (...) {
                }
                on_task_end();

                return;
            }
        }
        state.mutex.unlock();
    }

    // -----------------------
    // 4. STEAL (batch)
    // -----------------------
    // thread_local std::vector<ScheduledTask> stolen;
    // stolen.clear();
    // if (try_steal_tasks_batch(worker_id, stolen) && !stolen.empty()) {
    //     ScheduledTask st = std::move(stolen.back());
    //     stolen.pop_back();
    //     {
    //         std::lock_guard<std::mutex> lock(state.mutex);
    //         for (auto &task : stolen) {
    //             state.local_queue.push_back(std::move(task));
    //         }
    //     }
    //     on_task_start();
    //     try {
    //         st.task();
    //     } catch (const std::exception &e) {
    //         std::cerr << "[ThreadPool] Task exception: " << e.what() << '\n';
    //     } catch (...) {
    //         std::cerr << "[ThreadPool] Unknown task exception\n";
    //     }
    //     on_task_end();
    //     return;
    // }

    // -----------------------
    // 4. STEAL (priority-aware)
    // -----------------------

    auto global_top_steal = ready_q_.peek();
    bool can_steal = false;

    if (!global_top_steal.has_value()) {
        can_steal = true; // nothing in global
    } else {
        // We will only run a stolen task if it is >= global top
        can_steal = true; // we still try to steal, but we validate before executing
    }

    if (can_steal) {
        thread_local std::vector<ScheduledTask> stolen;
        stolen.clear();
        if (try_steal_tasks_batch(worker_id, stolen) && !stolen.empty()) {
            ScheduledTask st = std::move(stolen.back());
            stolen.pop_back();
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                for (auto &t : stolen) {
                    state.local_queue.push_back(std::move(t));
                }
            }
            auto gt = ready_q_.peek();
            bool should_run = false;

            if (!gt.has_value()) {
                should_run = true;
            } else {
                should_run = (st.priority >= gt->get().priority);
            }
            if (should_run) {
                on_task_start();
                try {
                    st.task();
                } catch (...) {
                }
                on_task_end();
                return;
            } else {
                std::lock_guard<std::mutex> lock(state.mutex);
                state.local_queue.push_back(std::move(st));
            }
        }
    }

    // 5. WAIT
    std::unique_lock<std::mutex> lock(worker_cv_mutex_);
    // worker_cv_.wait_for(lock, std::chrono::milliseconds(1));
    worker_cv_.wait(lock);
}

bool ThreadPool::should_worker_exit(int id) const {
    if (!shutdown_flag_.load(std::memory_order_relaxed)) {
        return false;
    }

    // global queue
    if (!ready_q_.empty()) {
        return false;
    }

    // delayed queue
    {
        std::lock_guard<std::mutex> lock(delayed_mutex_);
        if (!delayed_queue_.empty()) {
            return false;
        }
    }

    // local queue
    const auto &state = worker_states_[id];
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.local_queue.empty()) {
            return false;
        }
    }

    // inflight tasks
    if (total_inflight_.load(std::memory_order_relaxed) != 0) {
        return false;
    }

    return true;
}