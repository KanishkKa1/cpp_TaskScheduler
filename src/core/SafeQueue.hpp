#pragma once
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
// Notes:
// - empty() and size() are snapshots and may be stale immediately.
template <typename T> class SafeQueue {
  private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;

    std::condition_variable not_empty_; // consumers wait on this when queue is empty
    std::condition_variable not_full_;  // producers wait on this when queue is at capacity

    bool shutdown_ = false;
    const size_t capacity_;

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

    template <typename U> bool push(U &&value) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(lock, [this] { return queue_.size() < capacity_ || shutdown_; });

        if (shutdown_) {
            return false;
        }

        queue_.emplace(std::forward<U>(value));
        lock.unlock();

        not_empty_.notify_one();
        return true;
    }

    template <typename... Args> bool emplace(Args &&...args) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(lock, [this] { return queue_.size() < capacity_ || shutdown_; });

        if (shutdown_) {
            return false;
        }

        queue_.emplace(std::forward<Args>(args)...);
        lock.unlock();

        not_empty_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty() || shutdown_; });

        if (queue_.empty()) {
            return std::nullopt;
        }

        auto value = std::move(queue_.front());
        queue_.pop();

        lock.unlock();
        not_full_.notify_one();

        return value;
    }

    std::optional<T> try_pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            return std::nullopt;
        }

        auto value = std::move(queue_.front());
        queue_.pop();

        lock.unlock();
        not_full_.notify_one();

        return value;
    }

    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutdown_) {
                return;
            }
            shutdown_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    bool is_shutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

    // NOTE: This is a snapshot and may be stale immediately in concurrent use.
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // NOTE: This is a snapshot and may be stale immediately in concurrent use.
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
};