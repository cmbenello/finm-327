#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <new>
#include <utility>

// allocator policies. they all expose allocate(n) -> T* and deallocate(p).

template<typename T>
struct HeapAllocator {
    T* allocate(std::size_t n) {
        return static_cast<T*>(::operator new(sizeof(T) * n));
    }
    void deallocate(T* p) { ::operator delete(p); }
};

// stack-style bump allocator backed by a fixed-size local buffer.
// once you blow past the buffer you fall back to heap so we don't crash
// in unrealistic test sizes. realistic HFT use would size the buffer up front.
template<typename T>
struct StackAllocator {
    static constexpr std::size_t kBytes = 4096;
    alignas(T) unsigned char buf_[kBytes]{};
    std::size_t used_ = 0;

    T* allocate(std::size_t n) {
        std::size_t need = sizeof(T) * n;
        if (used_ + need <= kBytes) {
            T* p = reinterpret_cast<T*>(buf_ + used_);
            used_ += need;
            return p;
        }
        return static_cast<T*>(::operator new(need));
    }

    void deallocate(T* p) {
        auto bp = reinterpret_cast<unsigned char*>(p);
        if (bp >= buf_ && bp < buf_ + kBytes) return; // owned by stack buf
        ::operator delete(p);
    }
};

// bonus: zeroes memory before handing it back
template<typename T>
struct ZeroInitAllocator {
    T* allocate(std::size_t n) {
        T* p = static_cast<T*>(::operator new(sizeof(T) * n));
        std::memset(p, 0, sizeof(T) * n);
        return p;
    }
    void deallocate(T* p) { ::operator delete(p); }
};

struct NoLock {
    void lock() {}
    void unlock() {}
};

struct MutexLock {
    std::mutex m_;
    void lock()   { m_.lock(); }
    void unlock() { m_.unlock(); }
};

// guard helper so we never forget to unlock on early return / throw
template<typename L>
struct lock_guard_t {
    L& l;
    explicit lock_guard_t(L& lock) : l(lock) { l.lock(); }
    ~lock_guard_t() { l.unlock(); }
};

template<typename T,
         template<typename> class AllocatorPolicy = HeapAllocator,
         typename ThreadingPolicy = NoLock>
class OrderBookBuffer {
public:
    explicit OrderBookBuffer(std::size_t capacity)
        : cap_(capacity), size_(0) {
        data_ = alloc_.allocate(cap_);
    }

    ~OrderBookBuffer() {
        for (std::size_t i = 0; i < size_; ++i) data_[i].~T();
        alloc_.deallocate(data_);
    }

    OrderBookBuffer(const OrderBookBuffer&) = delete;
    OrderBookBuffer& operator=(const OrderBookBuffer&) = delete;

    bool add_order(const T& o) {
        lock_guard_t<ThreadingPolicy> g(thr_);
        if (size_ >= cap_) return false;
        new (&data_[size_]) T(o);
        ++size_;
        return true;
    }

    void print_orders() {
        lock_guard_t<ThreadingPolicy> g(thr_);
        for (std::size_t i = 0; i < size_; ++i) {
            std::cout << "  [" << i << "] ";
            print_one(data_[i]);
        }
    }

    std::size_t size() const { return size_; }

private:
    AllocatorPolicy<T> alloc_{};
    ThreadingPolicy thr_{};
    T* data_ = nullptr;
    std::size_t cap_;
    std::size_t size_;

    // detect Order-shaped types so we print nicely; otherwise just dump operator<<
    template<typename U>
    auto print_one(const U& v) -> decltype(v.id, v.price, v.qty, void()) {
        std::cout << "id=" << v.id << " price=" << v.price << " qty=" << v.qty << "\n";
    }
    template<typename U, typename... Ignore>
    void print_one(const U& v, Ignore...) {
        std::cout << v << "\n";
    }
};
