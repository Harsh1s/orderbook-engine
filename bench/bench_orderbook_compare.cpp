/**
 * bench_orderbook_compare.cpp — std::map OrderBook vs tick-indexed ArrayOrderBook.
 *
 * Two questions, answered side by side:
 *
 *   1. CORRECTNESS: do the two books produce the *identical* trade stream on
 *      the same input? (If not, any speed comparison is meaningless.) We run
 *      both, capture every (buy_id, sell_id, price, qty, aggressor) tuple, and
 *      assert the sequences match exactly.
 *
 *   2. PERFORMANCE: how much faster is the contiguous, cache-friendly array
 *      layout than the red-black tree on the matching hot path?
 *
 * Both books reuse the SAME Order, PriceLevel, and ArenaAllocator, so the only
 * variable is the level container (std::map vs flat array).
 *
 * Methodology: orders are pre-generated, so we time only the matching loop, not
 * RNG. Same seed and same workload feed both books.
 */

#include "../core/include/OrderBook.h"
#include "../core/include/ArrayOrderBook.h"
#include "../core/include/Order.h"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <algorithm>
#include <numeric>
#include <cstring>

using namespace micro_exchange::core;

using Clock = std::chrono::high_resolution_clock;
using ns    = std::chrono::nanoseconds;

// Price band for the workload. Limits land in [9900, 10100]; the array book is
// sized a little wider so every resting limit has a slot.
static constexpr Price kMinPrice = 9800;
static constexpr Price kMaxPrice = 10200;

// ─────────────────────────────────────────────
// Workload (same generator the throughput bench uses)
// ─────────────────────────────────────────────
std::vector<NewOrderRequest> generate_orders(size_t count, uint64_t seed = 42) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<Price>    price_dist(9900, 10100);
    std::uniform_int_distribution<Quantity> qty_dist(1, 10);
    std::uniform_int_distribution<int>      side_dist(0, 1);
    std::uniform_real_distribution<double>  type_dist(0.0, 1.0);

    std::vector<NewOrderRequest> orders;
    orders.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        NewOrderRequest req{};
        req.id   = i + 1;
        req.side = side_dist(rng) ? Side::Buy : Side::Sell;
        if (type_dist(rng) < 0.7) {
            req.type  = OrderType::Limit;
            req.tif   = TimeInForce::GTC;
            req.price = price_dist(rng);
