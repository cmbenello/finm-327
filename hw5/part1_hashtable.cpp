// Part 1: Robin Hood hash table for market data, benchmarked vs std::unordered_map.

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// FNV-1a tuned for short stock-symbol strings (<=8 chars covers ~all US tickers).
// Short symbols are packed into a uint64 word and mixed; longer keys fall back to
// byte-wise FNV-1a. The packed path avoids a function call into std::hash and
// keeps everything in registers.
struct SymbolHash {
    static constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
    static constexpr uint64_t FNV_PRIME  = 1099511628211ULL;

    uint64_t operator()(std::string_view s) const noexcept {
        if (s.size() <= 8) {
            uint64_t w = 0;
            std::memcpy(&w, s.data(), s.size());
            w ^= static_cast<uint64_t>(s.size()) << 56;
            // mix (splitmix64 style)
            w ^= w >> 33; w *= 0xff51afd7ed558ccdULL;
            w ^= w >> 33; w *= 0xc4ceb9fe1a85ec53ULL;
            w ^= w >> 33;
            return w;
        }
        uint64_t h = FNV_OFFSET;
        for (unsigned char c : s) { h ^= c; h *= FNV_PRIME; }
        return h;
    }
};

template <typename V>
class RobinHoodMap {
    struct Slot {
        uint32_t dib;     // distance-to-initial-bucket; 0 means empty
        uint32_t keylen;
        uint64_t hash;
        char     key[24]; // SBO for tickers; longer keys spill to heap_key
        std::string heap_key;
        V        value;
    };

    std::vector<Slot> table_;
    size_t mask_ = 0;
    size_t size_ = 0;
    double max_load_ = 0.85;
    SymbolHash hasher_;

    static size_t round_pow2(size_t n) {
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    bool key_eq(const Slot& s, std::string_view k) const {
        if (s.keylen != k.size()) return false;
        const char* sp = s.keylen <= sizeof(s.key) ? s.key : s.heap_key.data();
        return std::memcmp(sp, k.data(), s.keylen) == 0;
    }

    void store_key(Slot& s, std::string_view k) {
        s.keylen = static_cast<uint32_t>(k.size());
        if (k.size() <= sizeof(s.key)) {
            std::memcpy(s.key, k.data(), k.size());
        } else {
            s.heap_key.assign(k.data(), k.size());
        }
    }

    void rehash(size_t new_cap) {
        std::vector<Slot> old = std::move(table_);
        table_.assign(new_cap, Slot{});
        mask_ = new_cap - 1;
        size_ = 0;
        for (auto& s : old) {
            if (s.dib != 0) {
                std::string_view kv = s.keylen <= sizeof(s.key)
                    ? std::string_view(s.key, s.keylen)
                    : std::string_view(s.heap_key);
                insert(kv, std::move(s.value));
            }
        }
    }

public:
    explicit RobinHoodMap(size_t cap = 16) {
        cap = round_pow2(std::max<size_t>(cap, 8));
        table_.assign(cap, Slot{});
        mask_ = cap - 1;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return table_.size(); }

    void insert(std::string_view key, V value) {
        if ((size_ + 1) > static_cast<size_t>(max_load_ * table_.size()))
            rehash(table_.size() * 2);

        uint64_t h = hasher_(key);
        size_t idx = h & mask_;
        Slot probe{};
        probe.dib = 1;
        probe.hash = h;
        store_key(probe, key);
        probe.value = std::move(value);

        while (true) {
            Slot& cur = table_[idx];
            if (cur.dib == 0) {                        // empty slot, place
                cur = std::move(probe);
                ++size_;
                return;
            }
            if (cur.hash == probe.hash && key_eq(cur, std::string_view(
                    probe.keylen <= sizeof(probe.key) ? probe.key : probe.heap_key.data(),
                    probe.keylen))) {                  // update existing
                cur.value = std::move(probe.value);
                return;
            }
            if (cur.dib < probe.dib) {                 // robin hood swap: rich gives to poor
                std::swap(cur, probe);
            }
            idx = (idx + 1) & mask_;
            ++probe.dib;
        }
    }

    V* find(std::string_view key) {
        uint64_t h = hasher_(key);
        size_t idx = h & mask_;
        uint32_t dist = 1;
        while (true) {
            Slot& cur = table_[idx];
            if (cur.dib == 0 || cur.dib < dist) return nullptr;
            if (cur.hash == h && key_eq(cur, key)) return &cur.value;
            idx = (idx + 1) & mask_;
            ++dist;
        }
    }

    bool erase(std::string_view key) {
        uint64_t h = hasher_(key);
        size_t idx = h & mask_;
        uint32_t dist = 1;
        while (true) {
            Slot& cur = table_[idx];
            if (cur.dib == 0 || cur.dib < dist) return false;
            if (cur.hash == h && key_eq(cur, key)) break;
            idx = (idx + 1) & mask_;
            ++dist;
        }
        // backshift: pull subsequent rich entries one slot toward home
        while (true) {
            size_t next = (idx + 1) & mask_;
            Slot& nxt = table_[next];
            if (nxt.dib <= 1) {
                table_[idx] = Slot{};
                break;
            }
            table_[idx] = std::move(nxt);
            table_[idx].dib -= 1;
            idx = next;
        }
        --size_;
        return true;
    }
};

struct Quote { double bid, ask; uint64_t ts; };

static std::vector<std::string> make_symbols(size_t n) {
    std::mt19937_64 rng(42);
    std::vector<std::string> out;
    out.reserve(n);
    const char alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (size_t i = 0; i < n; ++i) {
        size_t len = 3 + (rng() % 3);
        std::string s(len, ' ');
        for (auto& c : s) c = alpha[rng() % 26];
        out.push_back(std::move(s));
    }
    return out;
}

template <typename F>
double time_ms(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    constexpr size_t N = 200'000;
    constexpr size_t LOOKUPS = 2'000'000;

    auto syms = make_symbols(N);
    std::mt19937_64 rng(7);
    std::vector<size_t> lookup_idx(LOOKUPS);
    for (auto& x : lookup_idx) x = rng() % N;

    // Robin Hood
    RobinHoodMap<Quote> rh(N * 2);
    double rh_insert = time_ms([&] {
        for (size_t i = 0; i < N; ++i) rh.insert(syms[i], Quote{100.0 + i, 100.1 + i, i});
    });
    volatile double sink = 0;
    double rh_lookup = time_ms([&] {
        for (size_t i = 0; i < LOOKUPS; ++i) {
            auto* q = rh.find(syms[lookup_idx[i]]);
            if (q) sink += q->bid;
        }
    });
    double rh_erase = time_ms([&] {
        for (size_t i = 0; i < N; i += 2) rh.erase(syms[i]);
    });

    // std::unordered_map
    std::unordered_map<std::string, Quote> um;
    um.reserve(N * 2);
    double um_insert = time_ms([&] {
        for (size_t i = 0; i < N; ++i) um.emplace(syms[i], Quote{100.0 + i, 100.1 + i, i});
    });
    double um_lookup = time_ms([&] {
        for (size_t i = 0; i < LOOKUPS; ++i) {
            auto it = um.find(syms[lookup_idx[i]]);
            if (it != um.end()) sink += it->second.bid;
        }
    });
    double um_erase = time_ms([&] {
        for (size_t i = 0; i < N; i += 2) um.erase(syms[i]);
    });

    std::printf("=== Part 1: Hash Table (N=%zu inserts, %zu lookups) ===\n", N, LOOKUPS);
    std::printf("%-22s %12s %12s %12s\n", "structure", "insert(ms)", "lookup(ms)", "erase(ms)");
    std::printf("%-22s %12.2f %12.2f %12.2f\n", "RobinHoodMap", rh_insert, rh_lookup, rh_erase);
    std::printf("%-22s %12.2f %12.2f %12.2f\n", "std::unordered_map", um_insert, um_lookup, um_erase);
    std::printf("speedup (lookup):     %.2fx\n", um_lookup / rh_lookup);
    std::printf("sink=%f\n", (double)sink);
    return 0;
}
