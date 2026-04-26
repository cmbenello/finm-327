#pragma once

template<typename InputIt, typename Pred>
InputIt find_if(InputIt first, InputIt last, Pred pred) {
    for (; first != last; ++first) {
        if (pred(*first)) return first;
    }
    return last;
}

// returns count of elements matching pred - useful for "how many orders > $100"
template<typename InputIt, typename Pred>
std::size_t count_if(InputIt first, InputIt last, Pred pred) {
    std::size_t n = 0;
    for (; first != last; ++first) if (pred(*first)) ++n;
    return n;
}
