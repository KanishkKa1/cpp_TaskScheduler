#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

/**
 * @brief Bounded thread-safe blocking queue (MPMC).
 *
 * Guarantees:
 * - Multiple producers and consumers supported
 * - FIFO ordering of elements
 * - Strong exception safety for push/emplace
 *
 * Shutdown semantics:
 * - After shutdown():
 *      * No further pushes are accepted
 *      * All waiting threads are awakened
 *      * Consumers can drain remaining items
 *      * pop() returns std::nullopt when queue becomes empty
 *
 * Concurrency model:
 * - Single mutex protects all shared state
 * - Condition variables coordinate producers/consumers
 *
 * Notes:
 * - size() and empty() are snapshots (non-linearizable)
 * - No fairness guarantee among waiting threads
 */
template <typename T> class SafeQueue {
  private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;

    std::condition_variable not_empty_;
    std::condition_variable not_full_;

    bool shutdown_ = false;
    const size_t capacity_;

  public:
    explicit SafeQueue(size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("Capacity must be > 0");
        }
    }

    SafeQueue(const SafeQueue &) = delete;
    SafeQueue &operator=(const SafeQueue &) = delete;
    SafeQueue(SafeQueue &&) = delete;
    SafeQueue &operator=(SafeQueue &&) = delete;

    // =========================
    // PUSH (blocking)
    // =========================
    template <typename U> bool push(U &&value) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (shutdown_)
            return false;

        not_full_.wait(lock, [this] { return queue_.size() < capacity_ || shutdown_; });

        if (shutdown_)
            return false;

        queue_.emplace(std::forward<U>(value));

        lock.unlock();
        not_empty_.notify_one();

        return true;
    }

    // =========================
    // TRY PUSH
    // =========================
    template <typename U> bool try_push(U &&value) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.size() >= capacity_ || shutdown_) {
            return false;
        }

        queue_.emplace(std::forward<U>(value));
        not_empty_.notify_one();

        return true;
    }

    // =========================
    // POP (blocking)
    // =========================
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_.wait(lock, [this] { return !queue_.empty() || shutdown_; });

        if (queue_.empty()) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();

        lock.unlock();
        not_full_.notify_one();

        return value;
    }

    // =========================
    // TRY POP
    // =========================
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();

        not_full_.notify_one();
        return value;
    }

    // =========================
    // SHUTDOWN
    // =========================
    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }

        not_empty_.notify_all();
        not_full_.notify_all();
    }

    bool is_shutdown() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
};