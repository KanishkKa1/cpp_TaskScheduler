#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

// Thread-safe bounded queue (MPMC) with blocking backpressure.
//
// Semantics:
// - push/emplace: fail after shutdown
// - pop: blocks until item available or shutdown
// - try_pop: non-blocking
// - shutdown:
//     * prevents further pushes
//     * wakes all waiting threads
//     * allows consumers to drain remaining items
//     * pop returns nullopt when empty after shutdown
//
// Invariants:
// - Producers wait on not_full_ when queue is full
// - Consumers wait on not_empty_ when queue is empty
// - shutdown_ wakes all waiting threads
// - No push is allowed after shutdown_
// - pop() returns nullopt when shutdown_ && queue empty
//
// Notes:
// - empty() and size() are snapshots and may be stale immediately.
template <typename T> class SafeQueue {
  private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;

    std::condition_variable not_empty_; // consumers wait on this when queue is empty
    std::condition_variable not_full_;  // producers wait on this when queue is at capacity

    std::atomic<bool> shutdown_{false};

    const size_t capacity_;
    size_t size_ = 0;

  public:
    explicit SafeQueue(size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("Capacity must be greater than 0");
        }
    }

    SafeQueue(const SafeQueue &) = delete;
    SafeQueue &operator=(const SafeQueue &) = delete;

    SafeQueue(SafeQueue &&) = delete;
    SafeQueue &operator=(SafeQueue &&) = delete;

    /**
     * Blocking push with backpressure.
     * Returns false if queue is shutdown.
     */
    template <typename U> bool push(U &&value) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(lock, [this] {
            return size_ < capacity_ || shutdown_.load(std::memory_order_relaxed);
        });

        if (shutdown_) {
            return false;
        }

        queue_.emplace(std::forward<U>(value));
        ++size_;

        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    template <typename... Args> bool emplace(Args &&...args) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(lock, [this] {
            return size_ < capacity_ || shutdown_.load(std::memory_order_relaxed);
        });

        if (shutdown_) {
            return false;
        }

        queue_.emplace(std::forward<Args>(args)...);
        ++size_;

        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    /**
     * Blocking pop.
     * Returns nullopt if shutdown and queue is empty.
     */
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_.wait(lock,
                        [this] { return size_ > 0 || shutdown_.load(std::memory_order_relaxed); });

        if (size_ == 0) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();
        --size_;

        lock.unlock();
        not_full_.notify_one();

        return value;
    }

    /**
     * Non-blocking pop.
     */
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (size_ == 0) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();
        --size_;

        not_full_.notify_one();
        return value;
    }

    /**
     * Shutdown queue:
     * - wakes all waiting threads
     * - prevents further pushes
     */
    void shutdown() noexcept {
        shutdown_.store(true, std::memory_order_relaxed);

        not_empty_.notify_all();
        not_full_.notify_all();
    }

    bool is_shutdown() const noexcept {
        return shutdown_.load(std::memory_order_relaxed);
    }

    // NOTE: This is a snapshot and may be stale immediately in concurrent use.
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    // NOTE: This is a snapshot and may be stale immediately in concurrent use.
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0;
    }
};