# hw4 - HFT Template Programming

Header-only OrderEngine pieces using TMP, constexpr, generic algorithms, and policy-based design.

## Layout

- `include/metaprogramming.hpp` - Factorial, Fibonacci (TMP), IsOdd trait, SFINAE-guarded print_if_odd, variadic print_all, has_price detection idiom
- `include/constexpr_math.hpp` - constexpr factorial, fibonacci, square, price_bucket
- `include/StaticVector.hpp` - fixed-capacity vector, no heap, aligned storage
- `include/generic_algorithms.hpp` - find_if, count_if
- `include/OrderBookBuffer.hpp` - policy-based buffer (HeapAllocator / StackAllocator / ZeroInitAllocator x NoLock / MutexLock)
- `src/main.cpp` - exercises all four parts

## Build

```
mkdir build && cd build
cmake ..
make
./HFTTemplateHomework
```

Or directly:

```
g++ -std=c++17 -O2 -Iinclude src/main.cpp -o hft
./hft
```
