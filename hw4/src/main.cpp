#include <iostream>
#include <random>
#include "metaprogramming.hpp"
#include "constexpr_math.hpp"
#include "StaticVector.hpp"
#include "generic_algorithms.hpp"
#include "OrderBookBuffer.hpp"

struct Order {
    int id;
    double price;
    int qty;
};

int main() {
    // part 1 - TMP
    static_assert(Factorial<5>::value == 120, "factorial<5> wrong");
    static_assert(Fibonacci<7>::value == 13, "fib<7> wrong");
    static_assert(IsOdd<int, 7>::value, "7 should be odd");
    static_assert(!IsOdd<int, 8>::value, "8 should be even");
    static_assert(has_price<Order>::value, "Order has price");

    std::cout << "part 1: TMP\n";
    std::cout << "  Factorial<5> = " << Factorial<5>::value << "\n";
    std::cout << "  Fibonacci<10> = " << Fibonacci<10>::value << "\n";
    print_if_odd_ct<int, 7>();
    print_if_odd(13);
    print_if_odd(8); // skipped, even
    std::cout << "  variadic: ";
    print_all("order", 42, "price", 99.95, "qty", 100);

    // part 2 - constexpr
    static_assert(square(5) == 25, "square wrong");
    static_assert(factorial(6) == 720, "constexpr factorial wrong");
    static_assert(fibonacci(10) == 55, "constexpr fib wrong");
    static_assert(price_bucket(101.73) == 101.70, "price bucket wrong");
    static_assert(price_bucket(99.999) == 99.95, "price bucket boundary");

    constexpr int Size = square(5);
    int arr[Size]{};
    for (int i = 0; i < Size; ++i) arr[i] = i;

    std::cout << "\npart 2: constexpr\n";
    std::cout << "  square(5) = " << square(5) << "\n";
    std::cout << "  price_bucket(101.73) = " << price_bucket(101.73) << "\n";
    std::cout << "  arr[24] = " << arr[24] << "\n";

    // runtime use of the same functions
    double live_price = 250.07;
    std::cout << "  runtime bucket(" << live_price << ") = " << price_bucket(live_price) << "\n";

    // part 3 - StaticVector + find_if
    std::cout << "\npart 3: StaticVector + find_if\n";
    StaticVector<Order, 16> orders;
    std::mt19937 gen(42);
    std::uniform_real_distribution<> px(80.0, 120.0);
    std::uniform_int_distribution<> qd(1, 50);
    for (int i = 0; i < 12; ++i) {
        orders.push_back(Order{i, px(gen), qd(gen)});
    }

    std::cout << "  all orders:\n";
    for (const auto& o : orders) {
        std::cout << "    id=" << o.id << " price=" << o.price << " qty=" << o.qty << "\n";
    }

    auto over_100 = count_if(orders.begin(), orders.end(),
        [](const Order& o) { return o.price > 100.0; });
    std::cout << "  orders with price > 100: " << over_100 << "\n";

    auto first_div10 = find_if(orders.begin(), orders.end(),
        [](const Order& o) { return o.qty % 10 == 0; });
    if (first_div10 != orders.end()) {
        std::cout << "  first qty divisible by 10: id=" << first_div10->id
                  << " qty=" << first_div10->qty << "\n";
    } else {
        std::cout << "  no order with qty divisible by 10\n";
    }

    // part 4 - policy-based buffers
    std::cout << "\npart 4: policy-based OrderBookBuffer\n";
    OrderBookBuffer<Order, StackAllocator, NoLock> book1(10);
    OrderBookBuffer<Order, HeapAllocator, MutexLock> book2(10);
    OrderBookBuffer<Order, ZeroInitAllocator, NoLock> book3(5);

    for (int i = 0; i < 5; ++i) {
        book1.add_order(Order{i, 100.0 + i, 10 * (i + 1)});
        book2.add_order(Order{i + 100, 200.0 + i, 5 * (i + 1)});
    }
    book3.add_order(Order{999, 50.0, 1});

    std::cout << "book1 (stack/no-lock):\n";
    book1.print_orders();
    std::cout << "book2 (heap/mutex):\n";
    book2.print_orders();
    std::cout << "book3 (zero-init/no-lock):\n";
    book3.print_orders();

    std::cout << "\ndone.\n";
    return 0;
}
