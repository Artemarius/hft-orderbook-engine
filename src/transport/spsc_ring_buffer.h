#pragma once

/// @file spsc_ring_buffer.h
/// @brief Lock-free single-producer single-consumer ring buffer.
///
/// Hot-path transport primitive. Zero allocation after construction, lock-free,
/// cache-line padded to prevent false sharing between producer and consumer.
/// Uses monotonically increasing indices with masking for buffer access —
/// unsigned overflow is well-defined and avoids ABA problems.
///
/// Memory ordering: acquire/release on head_ and tail_ — no seq_cst.

#include <atomic>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace hft {

/// Lock-free SPSC ring buffer.
///
/// @tparam T        Element type — must be trivially copyable (POD messages).
/// @tparam Capacity Number of slots — must be a power of two.
template <typename T, size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(Capacity > 0, "Capacity must be greater than zero");
    static_assert(std::is_trivially_copyable_v<T>,
                  "Element type must be trivially copyable");

public:
    SPSCRingBuffer() noexcept = default;

    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer(SPSCRingBuffer&&) = delete;
    SPSCRingBuffer& operator=(SPSCRingBuffer&&) = delete;

    /// Try to push an item into the buffer (producer side).
    /// @return true if the item was pushed, false if the buffer is full.
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);

        if (head - tail >= Capacity) {
            return false;  // full
        }

        std::memcpy(&buffer_[head & kMask], &item, sizeof(T));
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Try to pop an item from the buffer (consumer side).
    /// @return true if an item was popped, false if the buffer is empty.
    [[nodiscard]] bool try_pop(T& item) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);

        if (tail >= head) {
            return false;  // empty
        }

        std::memcpy(&item, &buffer_[tail & kMask], sizeof(T));
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// Number of items currently in the buffer (approximate, racy between threads).
    [[nodiscard]] size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }

    /// Whether the buffer is empty (approximate, racy between threads).
    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    /// Whether the buffer is full (approximate, racy between threads).
    [[nodiscard]] bool full() const noexcept {
        return size() >= Capacity;
    }

    /// Fixed capacity of the buffer.
    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity;
    }

private:
    static constexpr size_t kMask = Capacity - 1;

    // Producer writes head_, consumer reads head_.
    alignas(64) std::atomic<size_t> head_{0};
    char pad_head_[64 - sizeof(std::atomic<size_t>)];

    // Consumer writes tail_, producer reads tail_.
    alignas(64) std::atomic<size_t> tail_{0};
    char pad_tail_[64 - sizeof(std::atomic<size_t>)];

    // Buffer on its own cache-line boundary.
    alignas(64) T buffer_[Capacity];
};

}  // namespace hft
