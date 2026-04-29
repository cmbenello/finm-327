#pragma once
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <utility>

// fixed-capacity slab allocator for one type. acquire() pops from a
// free list, release() pushes back. no per-allocation new/delete --
// we touch one cache line on the hot path.
//
// not thread safe. each engine thread should own its own pool.
template <typename T>
class MemoryPool {
public:
    explicit MemoryPool(std::size_t capacity)
        : cap_(capacity)
    {
        // raw byte buffer; we placement-new T's into slots on demand.
        storage_ = static_cast<unsigned char*>(
            std::aligned_alloc(alignof(T), slot_bytes() * cap_));
        if (!storage_) throw std::bad_alloc{};

        // initial free list: every slot points at the next.
        for (std::size_t i = 0; i < cap_; ++i) {
            slot_at(i)->next = (i + 1 < cap_) ? slot_at(i + 1) : nullptr;
        }
        head_ = slot_at(0);
    }

    ~MemoryPool() {
        // pool doesn't track which slots are live -- caller is expected to
        // release everything before tearing down. if not, we leak the T's
        // but at least don't blow up.
        std::free(storage_);
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    template <typename... Args>
    T* acquire(Args&&... args) {
        if (!head_) return nullptr;        // pool exhausted; caller falls back
        Slot* s = head_;
        head_ = s->next;
        ++live_;
        return ::new (s) T(std::forward<Args>(args)...);
    }

    void release(T* p) {
        if (!p) return;
        p->~T();
        Slot* s = reinterpret_cast<Slot*>(p);
        s->next = head_;
        head_ = s;
        --live_;
    }

    std::size_t capacity() const { return cap_; }
    std::size_t live()     const { return live_; }

private:
    // each slot must be big enough for a T or a free-list pointer.
    union Slot {
        T value;
        Slot* next;
        Slot() {}
        ~Slot() {}
    };

    static constexpr std::size_t slot_bytes() { return sizeof(Slot); }
    Slot* slot_at(std::size_t i) {
        return reinterpret_cast<Slot*>(storage_ + i * slot_bytes());
    }

    unsigned char* storage_{nullptr};
    Slot*          head_{nullptr};
    std::size_t    cap_{0};
    std::size_t    live_{0};
};
