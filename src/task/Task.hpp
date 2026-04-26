#pragma once

#include <new>
#include <type_traits>
#include <utility>

// Type-erased, move-only task with small buffer optimization
class Task {
  private:
    static constexpr size_t INLINE_SIZE = 64;

    alignas(std::max_align_t) unsigned char buffer_[INLINE_SIZE];

    void (*invoke_)(void *) = nullptr;
    void (*destroy_)(void *) = nullptr;
    void (*move_)(void *, void *) = nullptr;

    bool is_heap_ = false;

    void reset() noexcept {
        if (destroy_) {
            destroy_(buffer_);
        }
        invoke_ = nullptr;
        destroy_ = nullptr;
        move_ = nullptr;
        is_heap_ = false;
    }

  public:
    Task() = default;

    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;

    // =======================
    // Move Constructor
    // =======================
    Task(Task &&other) noexcept {
        if (other.invoke_) {
            other.move_(other.buffer_, buffer_);

            invoke_ = other.invoke_;
            destroy_ = other.destroy_;
            move_ = other.move_;
            is_heap_ = other.is_heap_;

            other.reset();
        }
    }

    // =======================
    // Move Assignment
    // =======================
    Task &operator=(Task &&other) noexcept {
        if (this != &other) {
            reset();

            if (other.invoke_) {
                other.move_(other.buffer_, buffer_);

                invoke_ = other.invoke_;
                destroy_ = other.destroy_;
                move_ = other.move_;
                is_heap_ = other.is_heap_;

                other.reset();
            }
        }
        return *this;
    }

    // =======================
    // Constructor
    // =======================
    template <typename F>
        requires(!std::is_same_v<std::decay_t<F>, Task>)
    explicit Task(F &&f) {
        using T = std::decay_t<F>;

        if constexpr (sizeof(T) <= INLINE_SIZE) {
            // Inline storage
            new (buffer_) T(std::forward<F>(f));

            invoke_ = [](void *ptr) { (*reinterpret_cast<T *>(ptr))(); };

            destroy_ = [](void *ptr) { reinterpret_cast<T *>(ptr)->~T(); };

            move_ = [](void *src, void *dst) {
                T *src_obj = reinterpret_cast<T *>(src);
                new (dst) T(std::move(*src_obj));
                src_obj->~T();
            };

            is_heap_ = false;
        } else {
            // Heap storage
            T *heap_obj = new T(std::forward<F>(f));
            *reinterpret_cast<T **>(buffer_) = heap_obj;

            invoke_ = [](void *ptr) {
                T *obj = *reinterpret_cast<T **>(ptr);
                (*obj)();
            };

            destroy_ = [](void *ptr) {
                T *obj = *reinterpret_cast<T **>(ptr);
                delete obj;
            };

            move_ = [](void *src, void *dst) {
                T *&src_ptr = *reinterpret_cast<T **>(src);
                *reinterpret_cast<T **>(dst) = src_ptr;
                src_ptr = nullptr;
            };

            is_heap_ = true;
        }
    }

    // =======================
    // Invoke
    // =======================
    void operator()() {
        if (invoke_) {
            invoke_(buffer_);
        }
    }

    // =======================
    // Destructor
    // =======================
    ~Task() noexcept {
        reset();
    }

    explicit operator bool() const {
        return invoke_ != nullptr;
    }
};