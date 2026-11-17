#pragma once

// Adapter exposing boost::lockfree::spsc_queue through the spsc::Queue interface.
// Reference for bench/tests only (deliberately not part of include/).
//
// Notes on the Boost 1.92 implementation (boost/lockfree/spsc_queue.hpp):
//   - memory ordering: acquire/release
//   - storage_, write_index_, read_index_ are each alignas(64) -> no false sharing
//   - no cached index (every push re-reads read_index_)
//   - runtime-sized variant allocates n+1 slots and wraps with `if (i >= cap) i -= cap` (no mask)
//   - T must be default-constructible (documented requirement)

#include "spsc/detail.hpp"

#include <boost/lockfree/spsc_queue.hpp>

#include <cstddef>
#include <optional>
#include <utility>

namespace spsc::compare {

template <typename T>
class BoostQueue {
public:
    // Round up to a power of two so capacity semantics match spsc::Queue.
    // (the bench uses 65536, so this changes nothing there)
    explicit BoostQueue(std::size_t capacity)
        : capacity_(detail::checked_capacity(capacity)), q_(capacity_) {}

    BoostQueue(const BoostQueue&) = delete;
    BoostQueue& operator=(const BoostQueue&) = delete;

    // Boost has no emplace: build a temporary and move it in (one extra move).
    template <typename... Args>
    bool try_emplace(Args&&... args) {
        return q_.push(T(std::forward<Args>(args)...));
    }

    bool try_push(const T& value) { return q_.push(value); }
    bool try_push(T&& value) { return q_.push(std::move(value)); }

    bool try_pop(T& out) { return q_.pop(out); }

    std::optional<T> try_pop() {
        std::optional<T> out;
        q_.consume_one([&](T&& v) { out.emplace(std::move(v)); });
        return out;
    }

    std::size_t capacity() const noexcept { return capacity_; }

private:
    const std::size_t capacity_;
    boost::lockfree::spsc_queue<T> q_;
};

}  // namespace spsc::compare
