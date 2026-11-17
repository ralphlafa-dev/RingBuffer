#pragma once

#include "spsc/detail.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace spsc {

// Lock-free single-producer / single-consumer bounded queue.
//
// Indices are monotonically increasing counters that never wrap; slot = (idx & mask_).
//   empty: tail == head
//   full : tail - head == capacity   (unsigned subtraction stays correct across size_t overflow)
//
// Threading contract: call try_push/try_emplace from exactly one producer thread,
//                     and try_pop from exactly one consumer thread.
template <typename T>
class Queue {
public:
    explicit Queue(std::size_t capacity)
        : capacity_(detail::checked_capacity(capacity)),
          mask_(capacity_ - 1),
          slots_(new Slot[capacity_]) {}

    ~Queue() {
        // Assumes no other thread touches the queue during destruction.
        const std::size_t tail = tail_.load();
        for (std::size_t i = head_.load(); i != tail; ++i) {
            slot_ptr(i)->~T();
        }
    }

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    // ---- producer ----

    template <typename... Args>
    bool try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        const std::size_t tail = tail_.load();
        const std::size_t head = head_.load();
        if (tail - head == capacity_) {
            return false;
        }
        ::new (static_cast<void*>(slots_[tail & mask_].data)) T(std::forward<Args>(args)...);
        // The consumer may see the slot after this store -> construction must finish first.
        tail_.store(tail + 1);
        return true;
    }

    bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(value);
    }

    bool try_push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return try_emplace(std::move(value));
    }

    // ---- consumer ----

    bool try_pop(T& out) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const std::size_t head = head_.load();
        const std::size_t tail = tail_.load();
        if (tail == head) {
            return false;
        }
        T* p = slot_ptr(head);
        out = std::move(*p);
        p->~T();
        // The producer may reuse the slot after this store -> destruction must finish first.
        head_.store(head + 1);
        return true;
    }

    std::optional<T> try_pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        const std::size_t head = head_.load();
        const std::size_t tail = tail_.load();
        if (tail == head) {
            return std::nullopt;
        }
        T* p = slot_ptr(head);
        std::optional<T> out(std::move(*p));
        p->~T();
        head_.store(head + 1);
        return out;
    }

    std::size_t capacity() const noexcept { return capacity_; }

private:
    // Raw storage so T need not be default-constructible.
    struct Slot {
        alignas(T) std::byte data[sizeof(T)];
    };

    T* slot_ptr(std::size_t idx) const noexcept {
        return std::launder(reinterpret_cast<T*>(slots_[idx & mask_].data));
    }

    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<Slot[]> slots_;

    std::atomic<std::size_t> head_{0};  // written by consumer
    std::atomic<std::size_t> tail_{0};  // written by producer
};

}  // namespace spsc
