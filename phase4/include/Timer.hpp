#pragma once
#include <chrono>

class Timer {
public:
    using Clock = std::chrono::high_resolution_clock;

    void start() { t0_ = Clock::now(); }

    long long stop() {
        auto t1 = Clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0_).count();
    }

private:
    Clock::time_point t0_{};
};

// scoped timer that appends to a vector on destruction. handy for the hot loop.
class ScopedNanos {
public:
    explicit ScopedNanos(long long& sink)
        : sink_(sink), t0_(Timer::Clock::now()) {}

    ~ScopedNanos() {
        auto t1 = Timer::Clock::now();
        sink_ = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0_).count();
    }

private:
    long long& sink_;
    Timer::Clock::time_point t0_;
};
