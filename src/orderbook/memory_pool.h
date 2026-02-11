#pragma once

/// @file memory_pool.h
/// @brief Pre-allocated slab allocator for hot-path objects.
///
/// Allocates a contiguous block of memory at construction (the only heap
/// allocation). After construction, allocate() and deallocate() are O(1)
/// with zero system calls — they simply push/pop from an intrusive free list.
///
/// Usage:
///   MemoryPool<Order> pool(1'000'000);  // Pre-allocate 1M slots at startup
///   Order* o = pool.allocate();         // O(1), no heap alloc
///   pool.deallocate(o);                 // O(1), no heap alloc

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>

namespace hft {

template <typename T>
class MemoryPool {
    static_assert(std::is_trivially_destructible_v<T>,
                  "MemoryPool requires trivially destructible types");
    static_assert(sizeof(T) >= sizeof(void*),
                  "T must be at least pointer-sized for free list overlay");

public:
    /// Allocate a contiguous block for `capacity` objects. This is the only
    /// heap allocation — everything after this is O(1) free-list ops.
    explicit MemoryPool(size_t capacity)
        : storage_(nullptr),
          free_list_(nullptr),
          capacity_(capacity),
          allocated_count_(0),
          high_water_mark_(0) {
        // Single contiguous allocation, cache-line aligned
        storage_ = static_cast<char*>(
            std::aligned_alloc(alignof(T), capacity * sizeof(T)));
        if (!storage_) {
            std::abort();  // Startup failure — no recovery
        }

        // Build free list back-to-front so first allocate() returns slot 0
        for (size_t i = capacity; i > 0; --i) {
            auto* node = reinterpret_cast<FreeNode*>(slot_ptr(i - 1));
            node->next = free_list_;
            free_list_ = node;
        }
    }

    ~MemoryPool() { std::free(storage_); }

    // Non-copyable, non-movable — owns the backing memory.
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    /// O(1) allocation from the free list. Returns nullptr if pool exhausted.
    [[nodiscard]] T* allocate() noexcept {
        if (!free_list_) [[unlikely]] {
            return nullptr;
        }
        FreeNode* node = free_list_;
        free_list_ = node->next;
        ++allocated_count_;
        if (allocated_count_ > high_water_mark_) [[unlikely]] {
            high_water_mark_ = allocated_count_;
        }
        return reinterpret_cast<T*>(node);
    }

    /// O(1) deallocation — pushes the slot back onto the free list.
    void deallocate(T* ptr) noexcept {
        auto* node = reinterpret_cast<FreeNode*>(ptr);
        node->next = free_list_;
        free_list_ = node;
        --allocated_count_;
    }

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_t size() const noexcept { return allocated_count_; }
    [[nodiscard]] size_t high_water_mark() const noexcept {
        return high_water_mark_;
    }
    [[nodiscard]] bool full() const noexcept { return free_list_ == nullptr; }
    [[nodiscard]] bool empty() const noexcept {
        return allocated_count_ == 0;
    }

    /// Check if a pointer belongs to this pool's storage region.
    [[nodiscard]] bool owns(const T* ptr) const noexcept {
        auto addr = reinterpret_cast<uintptr_t>(ptr);
        auto start = reinterpret_cast<uintptr_t>(storage_);
        auto end = start + capacity_ * sizeof(T);
        return addr >= start && addr < end;
    }

private:
    /// Overlay on a free slot — reuses the first pointer-sized bytes.
    struct FreeNode {
        FreeNode* next;
    };

    /// Get a pointer to the i-th slot in the storage block.
    char* slot_ptr(size_t index) noexcept {
        return storage_ + index * sizeof(T);
    }

    char* storage_;
    FreeNode* free_list_;
    size_t capacity_;
    size_t allocated_count_;
    size_t high_water_mark_;
};

}  // namespace hft
