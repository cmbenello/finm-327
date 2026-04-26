#pragma once
#include <cstddef>
#include <stdexcept>
#include <new>
#include <utility>

// fixed-capacity vector, no heap. uses aligned storage so we can
// hold non-default-constructible types and only construct on push.
template<typename T, std::size_t N>
class StaticVector {
public:
    StaticVector() = default;

    ~StaticVector() {
        for (std::size_t i = 0; i < count_; ++i) {
            data()[i].~T();
        }
    }

    void push_back(const T& v) {
        if (count_ >= N) throw std::overflow_error("StaticVector full");
        new (&data()[count_]) T(v);
        ++count_;
    }

    void push_back(T&& v) {
        if (count_ >= N) throw std::overflow_error("StaticVector full");
        new (&data()[count_]) T(std::move(v));
        ++count_;
    }

    T& operator[](std::size_t i) { return data()[i]; }
    const T& operator[](std::size_t i) const { return data()[i]; }

    std::size_t size() const { return count_; }
    constexpr std::size_t capacity() const { return N; }

    T* begin() { return data(); }
    T* end()   { return data() + count_; }
    const T* begin() const { return data(); }
    const T* end()   const { return data() + count_; }

private:
    alignas(T) unsigned char buf_[sizeof(T) * N]{};
    std::size_t count_ = 0;

    T* data() { return reinterpret_cast<T*>(buf_); }
    const T* data() const { return reinterpret_cast<const T*>(buf_); }
};
