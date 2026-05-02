#include "core/ThreadPool.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// multiple producers submitting tasks
const int NUM_PRODUCERS = 4;
const int TASKS_PER_PRODUCER = 100;

int main() {
    std::cout << "=== ThreadPool Test Start ===\n";

    ThreadPool pool(4);
    std::vector<std::future<int>> futures;
    futures.reserve(NUM_PRODUCERS * TASKS_PER_PRODUCER);

    std::mutex future_mutex;

    std::mutex cout_mutex;
    std::atomic<bool> running{true};

    size_t submitted, completed, pending, active;
    {
        // 🔹 Monitor thread (observability)
        std::jthread monitor([&](std::stop_token st) {
            while (!st.stop_requested()) {
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "[Monitor] Active=" << pool.active_workers()
                              << " | Pending(global)=" << pool.pending_task()
                              << " | Submitted=" << pool.total_submitted()
                              << " | Completed=" << pool.total_completed() << "\n";
                }
                std::this_thread::sleep_for(500ms);
            }
        });

        // * Test 1: Basic correctness
        // =======================
        // for (int i = 0; i < 10; i++) {
        //     futures.push_back(pool.submit([i]() {
        //         std::this_thread::sleep_for(10ms);
        //         {
        //             std::lock_guard<std::mutex> lock(cout_mutex);
        //             std::cout << "Task " << i << " is running on thread " <<
        //             std::this_thread::get_id()
        //                       << "\n";
        //         }
        //         return i * i;
        //     }));
        // }

        // * Test 2 - Large number of tasks
        // =======================
        // for (int i = 0; i < 1000; i++) {
        //     futures.push_back(pool.submit([i, &cout_mutex]() {
        //         std::this_thread::sleep_for(10ms);
        //         {
        //             std::lock_guard<std::mutex> lock(cout_mutex);
        //             std::cout << "Task " << i << " is running on thread " <<
        //             std::this_thread::get_id()
        //                       << "\n";
        //         }
        //         return i * i;
        //     }));
        // }

        // * Test 3.A - Parent-child task relationships and waiting
        // =======================
        // Submit tasks for parent child demonstration
        // for (int i = 0; i < 100; i++) {
        //     futures.push_back(pool.submit([i, &pool, &cout_mutex]() {
        //         {
        //             std::lock_guard<std::mutex> lock(cout_mutex);
        //             std::cout << "Parent Task " << i << " is running on thread "
        //                       << std::this_thread::get_id() << "\n";
        //         }
        //         for (int j = 0; j < 10; j++) {
        //             pool.submit([i, j, &cout_mutex]() {
        //                 std::this_thread::sleep_for(std::chrono::milliseconds(10));
        //                 std::lock_guard<std::mutex> lock(cout_mutex);
        //                 std::cout << "  Child Task " << i << "." << j << " is running on thread "
        //                           << std::this_thread::get_id() << "\n";
        //             });
        //         }
        //         return i * i;
        //     }));
        // }

        // *Test 3.B - Parent-child task relationships with parent waiting for children to complete
        // =======================
        // sample test prent wait for child
        // for (int i = 0; i < 10; i++) {
        //     futures.push_back(pool.submit([i, &pool, &cout_mutex]() {
        //         std::vector<std::future<int>> child_futures;
        //         for (int j = 0; j < 10; j++) {
        //             child_futures.push_back(pool.submit([i, j]() {
        //                 std::this_thread::sleep_for(std::chrono::milliseconds(10));
        //                 return i + j;
        //             }));
        //         }
        //         int sum = 0;
        //         for (auto &f : child_futures) {
        //             while (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        //             {
        //                 pool.help_one_task();
        //             }
        //             sum += f.get();
        //         }
        //         {
        //             std::lock_guard<std::mutex> lock(cout_mutex);
        //             std::cout << "Parent Task " << i << " sum = " << sum << "\n";
        //         }
        //         return sum;
        //     }));
        // }

        // * Test 4 -Imbalanced workload (stealing)
        // =======================
        // for (int i = 0; i < 100; i++) {
        //     pool.submit([i, &cout_mutex]() {
        //         if (i == 0) {
        //             {
        //                 std::lock_guard<std::mutex> lock(cout_mutex);
        //                 std::cout << "Heavy Task started on thread " <<
        //                 std::this_thread::get_id()
        //                           << "\n";
        //             }
        //             std::this_thread::sleep_for(2s);
        //             {
        //                 std::lock_guard<std::mutex> lock(cout_mutex);
        //                 std::cout << "Heavy Task finished\n";
        //             }
        //         } else {
        //             std::this_thread::sleep_for(10ms);
        //             {
        //                 std::lock_guard<std::mutex> lock(cout_mutex);
        //                 std::cout << "Light Task " << i << " on thread " <<
        //                 std::this_thread::get_id()
        //                           << "\n";
        //             }
        //         }
        //     });
        // }

        // * Test 5 - Small Lambda tasks to demonstrate inline storage optimization
        // Submit small lambda tasks to demonstrate inline storage
        // for (int i = 0; i < 10; i++) {
        //     futures.push_back(pool.submit([] { return 42; }));
        // }

        // * Test 6 - Large Lambda tasks to demonstrate heap allocation fallback
        // Submit large lambda tasks to demonstrate heap allocation
        // for (int i = 0; i < 10; i++) {
        //     futures.push_back(pool.submit([i, &cout_mutex]() {
        //         std::vector<int> big(10000, i);
        //         int sum = 0;
        //         for (int x : big)
        //             sum += x;
        //         {
        //             std::lock_guard<std::mutex> lock(cout_mutex);
        //             std::cout << "Task " << i << " sum = " << sum << "\n";
        //         }
        //         return sum;
        //     }));
        // }

        // * Test 7 - Move-heavy tasks to demonstrate move semantics in Task and ThreadPool
        // Submit move-heavy tasks to demonstrate move semantics
        // for (int i = 0; i < 20; i++) {
        //     std::vector<int> data(10000, i);
        //     futures.push_back(pool.submit([v = std::move(data), i, &cout_mutex]() mutable {
        //         long long sum = 0;
        //         for (int x : v)
        //             sum += x;
        //         {
        //             std::lock_guard<std::mutex> lock(cout_mutex);
        //             std::cout << "Task " << i << " sum = " << sum << " (size=" << v.size() <<
        //             ")\n";
        //         }
        //         return static_cast<int>(sum);
        //     }));
        // }

        // * Test 8 - Shutdown while tasks Running
        // for (int i = 0; i < 50; i++) {
        //     pool.submit([i, &cout_mutex]() {
        //         std::this_thread::sleep_for(100ms);
        //         {
        //             std::lock_guard<std::mutex> lock(cout_mutex);
        //             std::cout << "Task " << i << " is running on thread "
        //                       << std::this_thread::get_id() << "\n";
        //         }
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "Task " << i << " done\n";
        //     });
        // }
        // std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // * Test 9 - Multiple producers submitting tasks concurrently
        // std::vector<std::thread> producers;
        // for (int p = 0; p < NUM_PRODUCERS; p++) {
        //     producers.emplace_back([p, &pool, &futures, &future_mutex, &cout_mutex]() {
        //         for (int i = 0; i < TASKS_PER_PRODUCER; i++) {
        //             auto future = pool.submit([p, i, &cout_mutex]() {
        //                 std::this_thread::sleep_for(10ms);
        //                 {
        //                     std::lock_guard<std::mutex> lock(cout_mutex);
        //                     std::cout << "Producer " << p << " Task " << i
        //                               << " is running on thread " << std::this_thread::get_id()
        //                               << "\n";
        //                 }
        //                 return p * 1000000 + i;
        //             });
        //             {
        //                 std::lock_guard<std::mutex> lock(future_mutex);
        //                 futures.push_back(std::move(future));
        //             }
        //         }
        //     });
        // }
        // for (auto &t : producers) {
        //     t.join();
        // }

        // * Test 10 - continuous submission
        // std::atomic<bool> stop(false);
        // std::thread producer([&]() {
        //     while (!stop.load(std::memory_order_relaxed)) {
        //         auto future = pool.submit([]() {
        //             std::this_thread::sleep_for(10ms);
        //             return 42;
        //         });
        //         {
        //             std::lock_guard<std::mutex> lock(future_mutex);
        //             futures.push_back(std::move(future));
        //         }
        //     }
        // });
        // // Let system run under continuous load
        // std::this_thread::sleep_for(2s);
        // // Stop producer
        // stop.store(true);
        // producer.join();
        // // Give workers time to drain remaining tasks
        // std::this_thread::sleep_for(1s);

        // * Test 11 — Throwing tasks
        // =======================

        // 1. Normal tasks
        // for (int i = 0; i < 5; i++) {
        //     futures.push_back(pool.submit([i]() { return i * 10; }));
        // }

        // 2. Throwing tasks
        // for (int i = 0; i < 5; i++) {
        //     futures.push_back(
        //         pool.submit([]() -> int { throw std::runtime_error("Intentional failure"); }));
        // }

        //  log results in cli
        // for (size_t i = 0; i < futures.size(); ++i) {
        //     try {
        //         int result = futures[i].get();
        //         {
        //             std::lock_guard<std::mutex> lock(cout_mutex);
        //             std::cout << "Task " << i << " -> Result: " << result << "\n";
        //         }
        //     } catch (const std::exception &e) {
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "Task " << i << " -> Error: " << e.what() << "\n";
        //     }
        // }

        // log results in cli for multiple producers
        // size_t success = 0;
        // for (auto &f : futures) {
        //     try {
        //         f.get();
        //         success++;
        //     } catch (...) {
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "Future exception!\n";
        //     }
        // }
        // {
        //     std::lock_guard<std::mutex> lock(cout_mutex);
        //     std::cout << "Validated: " << success << " / " << futures.size() << "\n";
        // }

        // log the error results in cli for throwing tasks
        // Test 11 — Throwing tasks
        // =======================
        // std::vector<std::future<int>> futures;

        // 1. Normal tasks
        // for (int i = 0; i < 5; i++) {
        //     futures.push_back(pool.submit([i]() { return i * 10; }));
        // }

        // 2. Throwing tasks
        // for (int i = 0; i < 5; i++) {
        //     futures.push_back(
        //         pool.submit([]() -> int { throw std::runtime_error("Intentional failure"); }));
        // }

        // * Test 12 - Priority Test
        // =======================
        // std::vector<std::future<int>> futures;
        // futures.reserve(12);
        // std::cout << "\n=== Priority Test Start ===\n";
        // // 1. LOW priority tasks (block workers)
        // for (int i = 0; i < 4; i++) {
        //     futures.push_back(pool.submit_with_priority(1, [i, &cout_mutex]() {
        //         std::this_thread::sleep_for(500ms);
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "[LOW] Task " << i << " done\n";
        //         return i;
        //     }));
        // }
        // // ensure workers are busy
        // std::this_thread::sleep_for(50ms);
        // // 2. HIGH priority tasks
        // for (int i = 0; i < 4; i++) {
        //     futures.push_back(pool.submit_with_priority(10, [i, &cout_mutex]() {
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "[HIGH] Task " << i << " done\n";
        //         return i;
        //     }));
        // }
        // // 3. More LOW priority tasks
        // for (int i = 4; i < 8; i++) {
        //     futures.push_back(pool.submit_with_priority(1, [i, &cout_mutex]() {
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "[LOW] Task " << i << " done\n";
        //         return i;
        //     }));
        // }

        // * Test 13 - Priority Test with continuous submission
        // std::vector<std::future<int>> futures;
        // std::mutex cout_mutex;
        // std::cout << "\n=== Priority Stress Test (Burst) ===\n";
        // // Large LOW batch
        // for (int i = 0; i < 100; i++) {
        //     futures.push_back(pool.submit_with_priority(1, [i, &cout_mutex]() {
        //         std::this_thread::sleep_for(5ms);
        //         {
        //             std::lock_guard<std::mutex> lock(cout_mutex);
        //             std::cout << "[LOW] " << i << "\n";
        //         }
        //         return i;
        //     }));
        // }
        // Let system fill
        // std::this_thread::sleep_for(50ms);
        // Inject HIGH priority burst
        // for (int i = 0; i < 20; i++) {
        //     futures.push_back(pool.submit_with_priority(100, [i, &cout_mutex]() {
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "[HIGH] " << i << "\n";
        //         return i;
        //     }));
        // }

        // * Test 14 — Mixed Streaming
        // std::vector<std::future<int>> futures;
        // std::mutex cout_mutex;
        // std::cout << "\n=== Mixed Priority Streaming Test ===\n";
        // for (int i = 0; i < 200; i++) {
        //     int pr = (i % 10 == 0) ? 100 : 1;
        //     futures.push_back(pool.submit_with_priority(pr, [i, pr, &cout_mutex]() {
        //         std::this_thread::sleep_for(2ms);
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         if (pr == 100)
        //             std::cout << "[HIGH] Task " << i << "\n";
        //         else
        //             std::cout << "[LOW] Task " << i << "\n";
        //         return i;
        //     }));
        // }

        // * Test 15 — Stealing conflict
        // std::vector<std::future<int>> futures;
        // std::mutex cout_mutex;
        // std::cout << "\n=== Priority vs Stealing Conflict Test ===\n";
        // // One heavy LOW task to create imbalance
        // futures.push_back(pool.submit_with_priority(1, [&cout_mutex]() {
        //     {
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "[LOW-HEAVY] started\n";
        //     }
        //     std::this_thread::sleep_for(1s);
        //     return 0;
        // }));
        // // Many small LOW tasks
        // for (int i = 0; i < 50; i++) {
        //     futures.push_back(pool.submit_with_priority(1, [] {
        //         std::this_thread::sleep_for(10ms);
        //         return 1;
        //     }));
        // }
        // // Inject HIGH tasks mid-way
        // std::this_thread::sleep_for(100ms);
        // for (int i = 0; i < 10; i++) {
        //     futures.push_back(pool.submit_with_priority(100, [i, &cout_mutex]() {
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "[HIGH] " << i << "\n";
        //         return i;
        //     }));
        // }

        // =======================
        // Wait for all tasks
        // =======================
        // for (auto &f : futures) {
        //     try {
        //         f.get();
        //     } catch (const std::exception &e) {
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "Exception: " << e.what() << "\n";
        //     }
        // }
        // std::cout << "=== Priority Test End ===\n";

        // Process results (CRITICAL)
        // =======================
        // for (size_t i = 0; i < futures.size(); ++i) {
        //     try {
        //         int result = futures[i].get();
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "Task " << i << " -> Result: " << result << "\n";
        //     } catch (const std::exception &e) {
        //         std::lock_guard<std::mutex> lock(cout_mutex);
        //         std::cout << "Task " << i << " -> Exception: " << e.what() << "\n";
        //     }
        // }

        // * Test 16 - Delayed Tasks
        // std::cout << "\n=== Delay Basic Test ===\n";
        // std::mutex cout_mutex;
        // auto start = std::chrono::steady_clock::now();
        // pool.submit_after(std::chrono::milliseconds(500), [&]() {
        //     auto now = std::chrono::steady_clock::now();
        //     auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now -
        //     start).count(); std::lock_guard<std::mutex> lock(cout_mutex); std::cout << "[Delay
        //     Test] Executed after " << diff << " ms\n";
        // });
        // for (int d : {100, 300, 500}) {
        //     pool.submit_after(std::chrono::milliseconds(d),
        //                       [d]() { std::cout << "[Delay] " << d << "ms\n"; });
        // }
        // std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // * Test 17 - priority vs delay conflict
        // std::cout << "\n=== Priority vs Delay Conflict ===\n";
        // // Long LOW task
        // pool.submit_with_priority(1, [&]() {
        //     std::this_thread::sleep_for(500ms);
        //     std::cout << "[LOW] finished\n";
        // });
        // // Give it time to start
        // std::this_thread::sleep_for(50ms);
        // // HIGH delayed task
        // pool.submit_after(10ms, [&]() { std::cout << "[HIGH] executed\n"; });

        // * Test 18 - Pure Priority

        std::cout << "\n=== Pure Priority Test ===\n";
        std::atomic<bool> start{false};
        for (int i = 0; i < 5; i++) {
            pool.submit_with_priority(1, [i, &start]() {
                while (!start.load()) {
                }
                std::this_thread::sleep_for(10ms);
                std::cout << "[LOW " << i << "]\n";
            });
        }

        for (int i = 0; i < 3; i++) {
            pool.submit_with_priority(100, [i, &start]() {
                while (!start.load()) {
                }
                std::this_thread::sleep_for(10ms);
                std::cout << "[HIGH " << i << "]\n";
            });
        }

        std::this_thread::sleep_for(50ms);
        start.store(true);

        std::this_thread::sleep_for(500ms);

        pool.shutdown();
        while (!pool.is_idle()) {
            std::this_thread::sleep_for(10ms);
        }

        // stop monitor thread
        // running.store(false, std::memory_order_relaxed);
        // monitor.join();

        // Capture final metrics after shutdown
        submitted = pool.total_submitted();
        completed = pool.total_completed();
        pending = pool.pending_task();
        active = pool.active_workers();

        // =======================
        // Final Metrics
        // =======================
        std::cout << "===Final Metrics====\n";
        std::cout << "Submitted: " << submitted << "\n";
        std::cout << "Completed: " << completed << "\n";
        std::cout << "Pending: " << pending << "\n";
        std::cout << "Active: " << active << "\n";

        std::cout << "=== ThreadPool Test End ===\n";
        return 0;
    }
}