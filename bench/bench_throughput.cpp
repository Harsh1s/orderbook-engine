/**
 * bench_throughput.cpp — Matching engine throughput and latency benchmark.
 *
 * Measures:
 *   • Single-thread matching throughput (orders/sec)
 *   • Per-order latency distribution (p50/p95/p99/p999)
 *   • Arena allocator overhead vs raw new/delete
 *   • Book depth impact on matching performance
 *
 * Methodology:
 *   Pre-generate all orders, then measure only the matching hot path.
 *   This isolates engine performance from random number generation.
 */

#include "../core/include/MatchingEngine.h"
#include "../core/include/OrderBook.h"
#include "../core/include/Order.h"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <algorithm>
#include <numeric>

using namespace micro_exchange::core;

using Clock = std::chrono::high_resolution_clock;
using ns = std::chrono::nanoseconds;

// ─────────────────────────────────────────────
// Pre-generate orders
// ─────────────────────────────────────────────

std::vector<NewOrderRequest> generate_orders(size_t count, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<Price> price_dist(9900, 10100);
    std::uniform_int_distribution<Quantity> qty_dist(1, 10);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_real_distribution<double> type_dist(0.0, 1.0);

    std::vector<NewOrderRequest> orders;
    orders.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        NewOrderRequest req{};
        req.id = i + 1;
        req.side = side_dist(rng) ? Side::Buy : Side::Sell;

        double type_roll = type_dist(rng);
        if (type_roll < 0.7) {
            req.type = OrderType::Limit;
            req.tif = TimeInForce::GTC;
            req.price = price_dist(rng);
        } else {
            req.type = OrderType::Market;
            req.tif = TimeInForce::IOC;
            req.price = PRICE_MARKET;
        }

        req.quantity = qty_dist(rng) * 100;
        std::memcpy(req.symbol, "BENCH", 6);
        orders.push_back(req);
    }

    return orders;
}

// ─────────────────────────────────────────────
// Benchmark: Throughput
// ─────────────────────────────────────────────

void bench_throughput(size_t num_orders) {
    std::cout << "\n── Throughput Benchmark (" << num_orders << " orders) ──\n";

    auto orders = generate_orders(num_orders);
    OrderBook book("BENCH");

    auto start = Clock::now();
    for (const auto& req : orders) {
        book.add_order(req);
    }
    auto end = Clock::now();

    auto elapsed_ns = std::chrono::duration_cast<ns>(end - start).count();
    double elapsed_s = elapsed_ns / 1e9;
    double throughput = num_orders / elapsed_s;

    std::cout << "  Orders processed: " << num_orders << "\n";
    std::cout << "  Trades executed:  " << book.trade_count() << "\n";
    std::cout << "  Wall time:        " << std::fixed << std::setprecision(3)
