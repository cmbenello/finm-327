#pragma once

constexpr long long factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

constexpr long long fibonacci(int n) {
    return n < 2 ? n : fibonacci(n - 1) + fibonacci(n - 2);
}

constexpr int square(int x) { return x * x; }

// rounds price down to the nearest 0.05 increment.
// done with integer math to avoid fp drift at compile time.
constexpr double price_bucket(double price) {
    long long ticks = static_cast<long long>(price * 20.0);
    return static_cast<double>(ticks) / 20.0;
}
