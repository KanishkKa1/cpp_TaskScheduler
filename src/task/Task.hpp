#pragma once

#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// Type-erased, move-only task wrapper with small buffer optimization
class Task {
  private:
    static constexpr size_t INLINE_SIZE = 64;

    alignas(std::max_align_t) char inline_buffer_[INLINE_SIZE];

    void (*invoke_)(void *) = nullptr;
    void (*destroy_)(void *) = nullptr;
    void (*move_)(void *, void *) = nullptr;

    bool is_heap_ = false;

  public:
    Task() = default;

    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;

    Task(Task &&other) noexcept {
        if (other.invoke_) {
            other.move_(other.inline_buffer_, inline_buffer_);

            invoke_ = other.invoke_;
            destroy_ = other.destroy_;
            move_ = other.move_;
            is_heap_ = other.is_heap_;

            other.invoke_ = nullptr;
            other.destroy_ = nullptr;
            other.move_ = nullptr;
            other.is_heap_ = false;
        }
    }

    Task &operator=(Task &&other) noexcept {
        if (this != &other) {
            if (destroy_) {
                destroy_(inline_buffer_);
            }

            if (other.invoke_) {
                other.move_(other.inline_buffer_, inline_buffer_);

                invoke_ = other.invoke_;
                destroy_ = other.destroy_;
                move_ = other.move_;
                is_heap_ = other.is_heap_;

                other.invoke_ = nullptr;
                other.destroy_ = nullptr;
                other.move_ = nullptr;
                other.is_heap_ = false;
            }
        }
        return *this;
    }

    template <typename F>
        requires(!std::is_same_v<std::decay_t<F>, Task>)
    explicit Task(F &&f) {
        using Decayed = std::decay_t<F>;

        if constexpr (sizeof(Decayed) <= INLINE_SIZE) {
            // Inline buffer storage
            new (inline_buffer_) Decayed(std::forward<F>(f));

            invoke_ = [](void *ptr) { (*reinterpret_cast<Decayed *>(ptr))(); };

            destroy_ = [](void *ptr) { reinterpret_cast<Decayed *>(ptr)->~Decayed(); };

            move_ = [](void *src, void *dst) {
                new (dst) Decayed(std::move(*reinterpret_cast<Decayed *>(src)));
            };

            is_heap_ = false;
        } else {
            // Heap storage
            Decayed *heap_obj = new Decayed(std::forward<F>(f));
            *reinterpret_cast<Decayed **>(inline_buffer_) = heap_obj;

            invoke_ = [](void *ptr) {
                Decayed *obj = *reinterpret_cast<Decayed **>(ptr);
                (*obj)();
            };

            destroy_ = [](void *ptr) {
                Decayed *obj = *reinterpret_cast<Decayed **>(ptr);
                delete obj;
            };

            move_ = [](void *src, void *dst) {
                *reinterpret_cast<Decayed **>(dst) = *reinterpret_cast<Decayed **>(src);
            };

            is_heap_ = true;
        }
    }

    // Executes the stored callable.
    void operator()() {
        if (invoke_) {
            invoke_(inline_buffer_);
        }
    }

    ~Task() noexcept {
        if (destroy_) {
            destroy_(inline_buffer_);
        }
    }

    explicit operator bool() const {
        return invoke_ != nullptr;
    }
};