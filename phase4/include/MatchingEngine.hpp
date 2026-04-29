#pragma once
#include <vector>

#include "MarketData.hpp"
#include "Order.hpp"
#include "OrderBook.hpp"
#include "TradeLogger.hpp"

// crosses an incoming order against the book until either it's filled
// or the book has nothing crossable left. resting remainder gets posted.
//
// templated over BookT so we can swap the std::map book with the flat-array
// book for the bench.
template <typename BookT>
class MatchingEngine {
public:
    using OrderPtr = typename BookT::OrderPtr;

    explicit MatchingEngine(BookT& book, TradeLogger& log)
        : book_(book), log_(log) {}

    // returns number of trades emitted for this order.
    int submit(OrderPtr incoming, std::uint64_t tick_seq, long long t_recv_ns) {
        int trades = 0;
        Side opp = (incoming->side == Side::Buy) ? Side::Sell : Side::Buy;

        while (incoming->remaining() > 0 && !book_.empty(opp)) {
            book_.prune_front(opp);
            if (book_.empty(opp)) break;

            auto resting = book_.best(opp);
            if (!crosses(incoming, resting)) break;

            int qty = std::min(incoming->remaining(), resting->remaining());
            double px = resting->price;     // taker pays maker price

            incoming->filled += qty;
            resting->filled  += qty;

            incoming->state = (incoming->remaining() == 0)
                                ? OrdState::Filled : OrdState::PartiallyFilled;
            resting->state  = (resting->remaining() == 0)
                                ? OrdState::Filled : OrdState::PartiallyFilled;

            // tick-to-trade: now - when the tick was timestamped.
            auto t_now = std::chrono::high_resolution_clock::now();
            long long t_now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     t_now.time_since_epoch()).count();

            Trade tr{
                incoming->side == Side::Buy  ? incoming->id : resting->id,
                incoming->side == Side::Sell ? incoming->id : resting->id,
                incoming->symbol,
                px,
                qty,
                tick_seq,
                t_now_ns - t_recv_ns
            };
            log_.add(tr);
            ++trades;

            if (resting->remaining() == 0) book_.pop_best(opp);
        }

        if (incoming->remaining() > 0) book_.add(incoming);
        return trades;
    }

private:
    static bool crosses(const OrderPtr& taker, const OrderPtr& maker) {
        return taker->side == Side::Buy
            ? taker->price >= maker->price
            : taker->price <= maker->price;
    }

    BookT&       book_;
    TradeLogger& log_;
};
