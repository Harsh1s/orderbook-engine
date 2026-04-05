/**
 * test_invariants.cpp — Property-based tests for matching engine invariants.
 *
 * Tests verify the three core invariants that define a correct CLOB:
 *
 *   1. No crossed book: After every operation, best_bid < best_ask
 *   2. FIFO preserved: Within a price level, earlier orders fill first
 *   3. Determinism: Identical input → identical output on every run
 *
 * Additionally:
 *   4. Conservation: Trade qty matches on both sides
 *   5. Quantity consistency: filled_qty + leaves_qty == original qty
 *   6. No phantom orders: cancelled orders don't participate in matching
 *
 * Test methodology: property-based testing with random order streams.
 * Each test generates thousands of random events and checks invariants
 * after every single operation — not just at the end.
 */

#include "../include/MatchingEngine.h"
#include "../include/OrderBook.h"
#include "../include/Order.h"

#include <cassert>
#include <iostream>
#include <random>
#include <vector>
#include <string>
#include <algorithm>

using namespace micro_exchange::core;

// ─────────────────────────────────────────────
// Test helpers
// ─────────────────────────────────────────────

class RandomOrderGenerator {
public:
    explicit RandomOrderGenerator(uint64_t seed = 42)
        : rng_(seed), price_dist_(9900, 10100), qty_dist_(100, 1000),
          side_dist_(0, 1), type_dist_(0.0, 1.0)
    {}

    NewOrderRequest generate(OrderId id) {
        NewOrderRequest req{};
        req.id = id;
        req.side = side_dist_(rng_) ? Side::Buy : Side::Sell;
        req.price = price_dist_(rng_);
        req.quantity = (qty_dist_(rng_) / 100) * 100;  // Round to 100
        if (req.quantity == 0) req.quantity = 100;

        double type_roll = type_dist_(rng_);
        if (type_roll < 0.7) {
            req.type = OrderType::Limit;
            req.tif = TimeInForce::GTC;
        } else if (type_roll < 0.85) {
            req.type = OrderType::Market;
            req.tif = TimeInForce::IOC;
            req.price = PRICE_MARKET;
        } else {
            req.type = OrderType::IOC;
            req.tif = TimeInForce::IOC;
        }

        std::memcpy(req.symbol, "TEST", 5);
        return req;
    }

private:
    std::mt19937_64 rng_;
    std::uniform_int_distribution<Price> price_dist_;
    std::uniform_int_distribution<Quantity> qty_dist_;
    std::uniform_int_distribution<int> side_dist_;
    std::uniform_real_distribution<double> type_dist_;
};

// ─────────────────────────────────────────────
// Test 1: No Crossed Book
// ─────────────────────────────────────────────

void test_no_crossed_book() {
    std::cout << "TEST: No crossed book invariant... ";

    OrderBook book("TEST");
    RandomOrderGenerator gen(12345);

    for (OrderId id = 1; id <= 50000; ++id) {
        auto req = gen.generate(id);
        book.add_order(req);

        // Check invariant after EVERY operation
        assert(book.check_no_crossed_book() &&
               "INVARIANT VIOLATED: Book is crossed after add_order!");
    }

    std::cout << "PASSED (50,000 random orders)\n";
}

// ─────────────────────────────────────────────
// Test 2: FIFO Priority
// ─────────────────────────────────────────────

void test_fifo_priority() {
    std::cout << "TEST: FIFO priority invariant... ";

    OrderBook book("TEST");

    // Place multiple orders at the same price
    for (OrderId id = 1; id <= 10; ++id) {
        NewOrderRequest req{};
        req.id = id;
        req.side = Side::Buy;
        req.type = OrderType::Limit;
        req.tif = TimeInForce::GTC;
        req.price = 10000;
        req.quantity = 100;
        std::memcpy(req.symbol, "TEST", 5);
        book.add_order(req);
    }

    // Send a sell market order that partially fills
    std::vector<OrderId> fill_order;
    book.set_trade_callback([&](const Trade& trade) {
        fill_order.push_back(trade.buy_order_id);
    });

    NewOrderRequest sell{};
    sell.id = 100;
    sell.side = Side::Sell;
    sell.type = OrderType::Market;
    sell.tif = TimeInForce::IOC;
    sell.price = PRICE_MARKET;
    sell.quantity = 300;  // Fill first 3 orders
    std::memcpy(sell.symbol, "TEST", 5);
    book.add_order(sell);

    // Verify FIFO: orders 1, 2, 3 should have filled (in that order)
    assert(fill_order.size() == 3);
    assert(fill_order[0] == 1);
    assert(fill_order[1] == 2);
    assert(fill_order[2] == 3);

    // Verify FIFO invariant in remaining book
    assert(book.check_fifo_invariant());

    std::cout << "PASSED\n";
}

// ─────────────────────────────────────────────
// Test 3: Determinism
// ─────────────────────────────────────────────

void test_determinism() {
    std::cout << "TEST: Deterministic matching... ";

    auto run_simulation = [](uint64_t seed) -> std::vector<Trade> {
        OrderBook book("TEST");
        RandomOrderGenerator gen(seed);
        std::vector<Trade> trades;

        book.set_trade_callback([&](const Trade& trade) {
            trades.push_back(trade);
        });

        for (OrderId id = 1; id <= 10000; ++id) {
            auto req = gen.generate(id);
            book.add_order(req);
        }

        return trades;
    };

    // Run twice with same seed
