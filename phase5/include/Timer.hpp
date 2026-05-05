#pragma once
#include <chrono>

namespace phase5 {

class Timer {
    using clock = std::chrono::high_resolution_clock;
public:
    Timer() : start_(clock::now()) {}
    void reset() { start_ = clock::now(); }
    double elapsed_seconds() const {
        return std::chrono::duration<double>(clock::now() - start_).count();
    }
    long long elapsed_nanos() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   clock::now() - start_).count();
    }
private:
    clock::time_point start_;
};

} // namespace phase5
