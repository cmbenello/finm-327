#pragma once
#include <iostream>
#include <type_traits>

template<int N>
struct Factorial {
    static constexpr long long value = N * Factorial<N - 1>::value;
};

template<>
struct Factorial<0> {
    static constexpr long long value = 1;
};

template<int N>
struct Fibonacci {
    static constexpr long long value = Fibonacci<N - 1>::value + Fibonacci<N - 2>::value;
};

template<>
struct Fibonacci<0> { static constexpr long long value = 0; };

template<>
struct Fibonacci<1> { static constexpr long long value = 1; };

// trait that picks out odd integers at compile time
template<typename T, T V>
struct IsOdd : std::integral_constant<bool, (V % 2) != 0> {};

// only enabled when T is integral and the value is odd
// caller passes the value as both runtime arg and template constant so we can
// gate compilation on it. for runtime-only values use the second overload.
template<typename T, T V>
typename std::enable_if<std::is_integral<T>::value && IsOdd<T, V>::value, void>::type
print_if_odd_ct() {
    std::cout << "odd (compile-time): " << V << "\n";
}

// runtime version - SFINAE just restricts to integral types
template<typename T>
typename std::enable_if<std::is_integral<T>::value, void>::type
print_if_odd(T v) {
    if (v % 2 != 0) std::cout << "odd: " << v << "\n";
}

// void_t detection idiom - check if a type has a member named 'price'
template<typename, typename = void>
struct has_price : std::false_type {};

template<typename T>
struct has_price<T, std::void_t<decltype(std::declval<T>().price)>> : std::true_type {};

// variadic print - dumps anything that has operator<<
inline void print_all() { std::cout << "\n"; }

template<typename T, typename... Rest>
void print_all(const T& first, const Rest&... rest) {
    std::cout << first;
    if constexpr (sizeof...(rest) > 0) std::cout << " ";
    print_all(rest...);
}
