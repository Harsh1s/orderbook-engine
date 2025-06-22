#pragma once

#include "Order.h"
#include "PriceLevel.h"
#include "ArenaAllocator.h"

#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>
#include <string>
#include <cstring>
#include <cstdint>
#include <algorithm>

namespace micro_exchange::core {

/**
 * ArrayOrderBook — CLOB backed by a contiguous, tick-indexed array of
 * PriceLevels plus a bitmap occupied-index (the layout production matching
 * engines such as LMAX use), as opposed to `OrderBook`'s `std::map`.
 *
 * Why an array AND a bitmap
 * ─────────────────────────
 * A flat array indexed by `(price - min_price)` gives O(1) level lookup,
 * insert, and erase, and lays the levels out sequentially for cache-friendly
 * access. But the best-bid / best-ask cursors still have to *find the next
 * occupied level* when the top of book is consumed. A naive linear scan makes
 * that O(band): on a wide/sparse book it is catastrophic — measured ~25x
 * slower than `std::map`, because the scan walks thousands of empty slots.
 *
 * The fix is a bitmap over levels (one bit = "this level has resting orders")
 * plus hardware bit-scan: `__builtin_ctzll` to find the next occupied level
 * above a cursor, `__builtin_clzll` to find the next one below. That turns the
 * cursor advance into a word-skipping scan (64 levels per instruction) instead
 * of one-level-at-a-time, making the array robust across band widths.
 *
 * This class is a drop-in alternative to `OrderBook` for the matching hot path
 * and reuses the SAME Order, PriceLevel, and ArenaAllocator, so a head-to-head
 * benchmark isolates exactly one variable: the level container. See
 * `bench/bench_orderbook_compare.cpp`, which cross-checks that both books emit
 * an identical trade stream and then compares throughput/latency.
 *
 * Scope: Limit / Market / IOC / FOK + cancel (the types the simulator and
 * benchmarks use). Stop / StopLimit / amend live in the full `OrderBook`.
 *
 * Matching semantics are identical to `OrderBook`: price-time priority, FIFO
 * within a level, trade prints at the resting order's price, Limit remainder
 * rests while Market / IOC / unfilled-FOK remainder cancels.
 */
class ArrayOrderBook {
public:
    using TradeCallback = std::function<void(const Trade&)>;
    using OrderCallback = std::function<void(const Order&)>;

    /**
     * @param symbol     instrument symbol
     * @param min_price  lowest supported limit price (ticks), inclusive
     * @param max_price  highest supported limit price (ticks), inclusive
     */
    ArrayOrderBook(const std::string& symbol, Price min_price, Price max_price)
        : symbol_(symbol)
        , min_price_(min_price)
        , max_price_(max_price)
        , order_arena_(65536)
    {
        const size_t n = static_cast<size_t>(max_price_ - min_price_ + 1);
        levels_.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            levels_.emplace_back(min_price_ + static_cast<Price>(i));
        }
        occ_.assign((n + 63) / 64, 0ULL);     // occupied-level bitmap, all empty
        best_bid_idx_ = -1;                    // no bids
        best_ask_idx_ = static_cast<long>(n);  // no asks (one past the end)
    }

    // ── Multi-subscriber dispatch (mirrors OrderBook's API) ──
    void add_trade_listener(TradeCallback cb) { trade_listeners_.push_back(std::move(cb)); }
    void add_order_listener(OrderCallback cb) { order_listeners_.push_back(std::move(cb)); }
    void set_trade_callback(TradeCallback cb) { add_trade_listener(std::move(cb)); }
    void set_order_callback(OrderCallback cb) { add_order_listener(std::move(cb)); }
    void clear_listeners() { trade_listeners_.clear(); order_listeners_.clear(); }

    // ═══════════════════════════════════════════
    // Order operations
    // ═══════════════════════════════════════════

    Order* add_order(const NewOrderRequest& req) {
        ts_ = now();                 // one clock read per event; reused for every fill
        Order* order = order_arena_.allocate();
        new (order) Order{};

        order->id         = req.id;
        order->sequence   = next_sequence_++;
        order->side       = req.side;
        order->type       = req.type;
        order->tif        = req.tif;
        order->price      = req.price;
        order->stop_price = req.stop_price;
        order->quantity   = req.quantity;
        order->leaves_qty = req.quantity;
        order->filled_qty = 0;
        order->entry_time = ts_;
        order->last_update = ts_;
        order->status     = OrderStatus::New;
        std::memcpy(order->symbol, req.symbol, sizeof(order->symbol));

        order_index_[order->id] = order;

        // FOK: only execute if the whole quantity can be filled right now.
        if (order->type == OrderType::FOK && !can_fill_completely(order)) {
            order->cancel(ts_);
            order_index_.erase(order->id);
            notify_order(*order);
            return order;
        }

        match(order);

        if (order->leaves_qty > 0) {
            if (order->type == OrderType::Limit && in_band(order->price)) {
                rest_order(order);
            } else {
                // Market / IOC / FOK remainder, or out-of-band limit, cancels.
                order->cancel(ts_);
                order_index_.erase(order->id);
                notify_order(*order);
            }
        }
        return order;
    }
