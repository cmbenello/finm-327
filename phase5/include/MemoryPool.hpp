#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

namespace phase5 {

// fixed-capacity slab + free list. acquire() pops, release() pushes.
// nodes are constructed in-place via placement new on a raw byte buffer
// so we never call new/delete after the initial reserve.
template <typename T>
class MemoryPool {
public:
    explicit MemoryPool(size_t capacity)
        : capacity_(capacity),
          buffer_(static_cast<T*>(std::malloc(capacity * sizeof(T)))),
          alive_(capacity, false) {
        if (!buffer_) throw std::bad_alloc{};
        free_list_.reserve(capacity);
        for (size_t i = capacity; i-- > 0;) free_list_.push_back(i);
    }

    ~MemoryPool() {
        for (size_t i = 0; i < capacity_; ++i) {
            if (alive_[i]) buffer_[i].~T();
        }
        std::free(buffer_);
    }

    MemoryPool(const MemoryPool&)            = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    template <typename... Args>
    T* acquire(Args&&... args) {
        if (free_list_.empty()) return nullptr;
        size_t idx = free_list_.back();
        free_list_.pop_back();
        T* p = new (buffer_ + idx) T{std::forward<Args>(args)...};
        alive_[idx] = true;
        return p;
    }

    void release(T* p) {
        if (!p) return;
        size_t idx = static_cast<size_t>(p - buffer_);
        p->~T();
        alive_[idx] = false;
        free_list_.push_back(idx);
    }

    size_t in_use() const { return capacity_ - free_list_.size(); }
    size_t capacity() const { return capacity_; }

private:
    size_t              capacity_;
    T*                  buffer_;
    std::vector<bool>   alive_;
    std::vector<size_t> free_list_;
};

} // namespace phase5
