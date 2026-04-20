#include "feed_parser.h"
#include "market_snapshot.h"
#include "order_manager.h"

#include <iostream>
#include <string>

namespace {

constexpr double kSellThreshold = 100.20;
constexpr double kBuyThreshold = 100.16;
constexpr int kOrderQty = 50;

void run_strategy(MarketSnapshot& snapshot, OrderManager& om) {
    const auto* bb = snapshot.get_best_bid();
    const auto* ba = snapshot.get_best_ask();

    // sell into a strong bid
    if (bb && bb->price >= kSellThreshold && !om.has_active_at(bb->price, Side::Sell)) {
        om.place_order(Side::Sell, bb->price, kOrderQty);
    }

    // buy a cheap offer
    if (ba && ba->price <= kBuyThreshold && !om.has_active_at(ba->price, Side::Buy)) {
        om.place_order(Side::Buy, ba->price, kOrderQty);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string feed_path = (argc > 1) ? argv[1] : "sample_feed.txt";

    auto feed = load_feed(feed_path);
    if (feed.empty()) {
        std::cerr << "No events loaded from " << feed_path << "\n";
        return 1;
    }

    MarketSnapshot snapshot;
    OrderManager om;

    for (const auto& ev : feed) {
        switch (ev.type) {
            case FeedType::BID:
                snapshot.update_bid(ev.price, ev.quantity);
                run_strategy(snapshot, om);
                break;
            case FeedType::ASK:
                snapshot.update_ask(ev.price, ev.quantity);
                run_strategy(snapshot, om);
                break;
            case FeedType::EXECUTION:
                om.handle_fill(ev.order_id, ev.quantity);
                break;
            default:
                break;
        }
    }

    std::cout << "\n[Summary] Final state\n";
    snapshot.print_top_of_book();
    om.print_active_orders();
    return 0;
}
