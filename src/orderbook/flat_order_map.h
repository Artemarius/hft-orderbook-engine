#pragma once

/// @file flat_order_map.h
/// @brief Pre-allocated open-addressing hash map: OrderId -> Order*.
///
/// Hot-path component — zero heap allocation after construction.
/// Uses linear probing with backward-shift deletion (no tombstones).
/// Capacity is always a power of 2 for fast modulo via bitmask.
///
/// OrderId 0 is reserved as the empty sentinel and must not be used
/// as a real order ID.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "core/order.h"
#include "core/types.h"

namespace hft {

class FlatOrderMap {
public:
    /// Construct with at least `min_capacity` usable slots.
    /// Actual capacity is the next power of 2 >= 2 * min_capacity
    /// to maintain load factor <= 0.5.
    explicit FlatOrderMap(size_t min_capacity)
        : entries_(nullptr), capacity_(0), capacity_mask_(0), size_(0) {
        size_t desired = (min_capacity < 8) ? 16 : min_capacity * 2;
        capacity_ = next_power_of_2(desired);
        capacity_mask_ = capacity_ - 1;

        entries_ = static_cast<Entry*>(
            std::calloc(capacity_, sizeof(Entry)));
        if (!entries_) {
            std::abort();
        }
    }

    ~FlatOrderMap() { std::free(entries_); }

    FlatOrderMap(const FlatOrderMap&) = delete;
    FlatOrderMap& operator=(const FlatOrderMap&) = delete;
    FlatOrderMap(FlatOrderMap&&) = delete;
    FlatOrderMap& operator=(FlatOrderMap&&) = delete;

    /// Insert a key-value pair. Returns false if key already exists or key is 0.
    bool insert(OrderId key, Order* value) noexcept {
        if (key == EMPTY_KEY) [[unlikely]] {
            return false;
        }

        size_t i = hash(key) & capacity_mask_;
        while (true) {
            if (entries_[i].key == EMPTY_KEY) {
                entries_[i].key = key;
                entries_[i].value = value;
                ++size_;
                return true;
            }
            if (entries_[i].key == key) {
                return false;  // Duplicate
            }
            i = (i + 1) & capacity_mask_;
        }
    }

    /// Erase a key. Returns false if not found.
    /// Uses backward-shift deletion to maintain probe chain integrity.
    bool erase(OrderId key) noexcept {
        if (key == EMPTY_KEY) [[unlikely]] {
            return false;
        }

        size_t i = hash(key) & capacity_mask_;

        // Find the key
        while (true) {
            if (entries_[i].key == EMPTY_KEY) {
                return false;  // Not found
            }
            if (entries_[i].key == key) {
                break;
            }
            i = (i + 1) & capacity_mask_;
        }

        // Found at index i. Backward-shift to fill the gap.
        --size_;
        size_t j = i;
        while (true) {
            j = (j + 1) & capacity_mask_;
            if (entries_[j].key == EMPTY_KEY) {
                break;
            }

            // k = ideal (home) index of entry at j
            size_t k = hash(entries_[j].key) & capacity_mask_;

            // Should we shift entry[j] into position i?
            // Yes if i is in the cyclic range [k, j), meaning the entry
            // at j would need to pass through i to reach j from k.
            size_t dist_ki = (i - k) & capacity_mask_;
            size_t dist_kj = (j - k) & capacity_mask_;
            if (dist_ki < dist_kj) {
                entries_[i] = entries_[j];
                i = j;
            }
        }

        entries_[i].key = EMPTY_KEY;
        entries_[i].value = nullptr;
        return true;
    }

    /// Find an order by ID. Returns nullptr if not found.
    [[nodiscard]] Order* find(OrderId key) const noexcept {
        if (key == EMPTY_KEY) [[unlikely]] {
            return nullptr;
        }

        size_t i = hash(key) & capacity_mask_;
        while (true) {
            if (entries_[i].key == EMPTY_KEY) {
                return nullptr;
            }
            if (entries_[i].key == key) {
                return entries_[i].value;
            }
            i = (i + 1) & capacity_mask_;
        }
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

    void clear() noexcept {
        std::memset(entries_, 0, capacity_ * sizeof(Entry));
        size_ = 0;
    }

private:
    static constexpr OrderId EMPTY_KEY = 0;

    struct Entry {
        OrderId key;    // 0 = empty slot
        Order* value;
    };

    /// Fibonacci hashing for uint64_t keys.
    static size_t hash(OrderId key) noexcept {
        return static_cast<size_t>(key * UINT64_C(11400714819323198485));
    }

    /// Round up to the next power of 2.
    static size_t next_power_of_2(size_t v) noexcept {
        --v;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        return v + 1;
    }

    Entry* entries_;
    size_t capacity_;
    size_t capacity_mask_;
    size_t size_;
};

}  // namespace hft
