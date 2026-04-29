#pragma once
#include <cstdint>
#include <string>
#include <type_traits>

enum class Side    : std::uint8_t { Buy, Sell };
enum class OrdState: std::uint8_t { New, PartiallyFilled, Filled, Cancelled };

inline const char* side_name(Side s)     { return s == Side::Buy ? "BUY" : "SELL"; }
inline const char* state_name(OrdState s) {
    switch (s) {
        case OrdState::New:             return "NEW";
        case OrdState::PartiallyFilled: return "PARTIAL";
        case OrdState::Filled:          return "FILLED";
        case OrdState::Cancelled:       return "CANCELLED";
    }
    return "?";
}

// templated so price can be int ticks or double, and order id can be
// int32 / int64 / whatever you want. integral id only -- shared_ptrs in
// a hashmap is no fun if your "id" is a string.
template <typename PriceT, typename OrderIdT>
struct Order {
    static_assert(std::is_integral<OrderIdT>::value,
                  "Order ID must be integral");
    static_assert(std::is_arithmetic<PriceT>::value,
                  "Price must be arithmetic");

    OrderIdT     id{};
    std::string  symbol;
    PriceT       price{};
    int          quantity{0};
    int          filled{0};
    Side         side{Side::Buy};
    OrdState     state{OrdState::New};

    Order() = default;
    Order(OrderIdT i, std::string sym, PriceT p, int q, Side s)
        : id(i), symbol(std::move(sym)), price(p), quantity(q), side(s) {}

    int  remaining() const { return quantity - filled; }
    bool live()      const { return state == OrdState::New || state == OrdState::PartiallyFilled; }
};

// the type we use everywhere. price is double, ids are 64-bit so we can
// run >100M ticks without rolling.
using OrderD = Order<double, std::int64_t>;

// trade record produced by the matching engine.
struct Trade {
    std::int64_t buy_id;
    std::int64_t sell_id;
    std::string  symbol;
    double       price;
    int          quantity;
    std::uint64_t seq;     // tick sequence that produced the trade
    long long    ns;       // tick-to-trade latency for this match
};
