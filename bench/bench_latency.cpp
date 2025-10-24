/*
 * bench_latency.cpp - per-operation latency histogram for the matching engine.
 *
 * Measures the wall-clock cost of submit_order() across a long stream of
 * synthetic add/cancel/match operations and reports p50/p90/p95/p99/p999
 * along with min/max. The intent is to give a quick "is anything regressing"
 * signal that can be wired into CI or run by hand before tagging a release.
 *
 * Usage:
 *   ./bench_latency                 # 1M operations against a 10x5 seeded book
 *   ./bench_latency --ops 5000000   # 5M operations
 *   ./bench_latency --warmup 200000 # warmup count before timing starts
 */

#include "MatchingEngine.h"
#include "OrderBook.h"
#include "Order.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace micro_exchange::core;

namespace {

struct CliArgs {
    size_t ops    = 1'000'000;
    size_t warmup =   100'000;
};

CliArgs parse(int argc, char** argv) {
    CliArgs a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--ops" && i + 1 < argc)    a.ops    = std::stoull(argv[++i]);
        else if (s == "--warmup" && i + 1 < argc) a.warmup = std::stoull(argv[++i]);
        else if (s == "--help") {
            std::cout << "usage: bench_latency [--ops N] [--warmup N]\n";
            std::exit(0);
        }
    }
    return a;
}

void seed_book(MatchingEngine& engine, const char* sym, Price mid) {
    OrderId id = 1;
    for (int lvl = 1; lvl <= 10; ++lvl) {
        for (int j = 0; j < 5; ++j) {
            NewOrderRequest req{};
            req.id = id++;
            req.side = Side::Buy;
            req.type = OrderType::Limit;
            req.tif = TimeInForce::GTC;
            req.price = mid - lvl;
            req.quantity = 100 + j * 50;
            std::strncpy(req.symbol, sym, 15);
            engine.submit_order(req);

            req.id = id++;
            req.side = Side::Sell;
            req.price = mid + lvl;
            engine.submit_order(req);
        }
    }
}

double percentile(std::vector<uint64_t>& v, double p) {
    if (v.empty()) return 0;
    size_t idx = static_cast<size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + idx, v.end());
    return static_cast<double>(v[idx]);
}

} // namespace

int main(int argc, char** argv) {
    CliArgs args = parse(argc, argv);

    std::cout << "\n  MicroExchange — Latency Benchmark\n";
    std::cout << "  ─────────────────────────────────\n";
    std::cout << "  Operations:  " << args.ops << "\n";
    std::cout << "  Warmup:      " << args.warmup << "\n\n";

    const char* sym = "BENCH";
    MatchingEngine engine;
    engine.add_symbol(sym);
    seed_book(engine, sym, 10000);

    std::mt19937_64 rng(0xBEEFCAFE);
    std::uniform_int_distribution<int>      side_dist(0, 1);
    std::uniform_int_distribution<int>      type_dist(0, 9);
    std::uniform_int_distribution<Price>    price_dist(9990, 10010);
    std::uniform_int_distribution<Quantity> qty_dist(1, 5);

    OrderId id = 100'000;
    auto submit_random = [&]() {
        NewOrderRequest req{};
