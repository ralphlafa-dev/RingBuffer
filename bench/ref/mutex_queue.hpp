#pragma once

#include "spsc/detail.hpp"

#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace spsc::compare {

// Mutex-based bounded queue used as a baseline. Same interface as spsc::Queue.
// Reference for bench/tests only (deliberately not part of include/).
template <typename T>
class MutexQueue {
public:
    explicit MutexQueue(std::size_t capacity)
        : capacity_(detail::checked_capacity(capacity)),
          mask_(capacity_ - 1),
          slots_(capacity_) {}

    MutexQueue(const MutexQueue&) = delete;
    MutexQueue& operator=(const MutexQueue&) = delete;

    template <typename... Args>
    bool try_emplace(Args&&... args) {
        std::lock_guard lock(mtx_);
        if (tail_ - head_ == capacity_) {
            return false;
        }
        slots_[tail_ & mask_].emplace(std::forward<Args>(args)...);
        ++tail_;
        return true;
    }

    bool try_push(const T& value) { return try_emplace(value); }
    bool try_push(T&& value) { return try_emplace(std::move(value)); }

    bool try_pop(T& out) {
        std::lock_guard lock(mtx_);
        if (tail_ == head_) {
            return false;
        }
        auto& slot = slots_[head_ & mask_];
        out = std::move(*slot);
        slot.reset();
        ++head_;
        return true;
    }

    std::optional<T> try_pop() {
        std::lock_guard lock(mtx_);
        if (tail_ == head_) {
            return std::nullopt;
        }
        auto& slot = slots_[head_ & mask_];
        std::optional<T> out(std::move(*slot));
        slot.reset();
        ++head_;
        return out;
    }

    std::size_t capacity() const noexcept { return capacity_; }

private:
    std::mutex mtx_;
    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<std::optional<T>> slots_;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
};

}  // namespace spsc::compare
