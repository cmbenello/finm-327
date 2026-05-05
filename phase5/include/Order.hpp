#pragma once
#include <string>

namespace phase5 {

struct Order {
    std::string id;
    double      price;
    int         quantity;
    bool        isBuy;
};

} // namespace phase5
