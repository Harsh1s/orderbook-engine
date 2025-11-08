#pragma once

#include "Order.h"
#include "PriceLevel.h"
#include "ArenaAllocator.h"

#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include <string>
#include <cassert>

namespace micro_exchange::core {

/**
 * OrderBook — Central Limit Order Book (CLOB) with price-time priority.
 *
 * Design rationale:
 * ─────────────────
 * The book is organized as two sorted maps of PriceLevels (bids descending,
 * asks ascending). Within each level, orders are queued in FIFO order via
 * PriceLevel's intrusive linked list.
 *
 * Data structure choice — std::map vs alternatives:
 *
 *   • std::map<Price, PriceLevel>: O(log N) lookup by price. For typical
 *     equity books with 20-50 active levels, log₂(50) ≈ 6 comparisons.
 *     The real cost is cache misses from tree traversal.
 *
 *   • Alternative: contiguous array indexed by (price - min_price) / tick_size.
 *     O(1) lookup, perfect cache locality for BBO scan. Used in production
 *     exchanges (e.g., LMAX). We use std::map for clarity; the array
 *     optimization is documented as a design note.
 *
 *   • We additionally maintain a hash map from OrderId → Order* for O(1)
 *     cancel/amend operations.
 *
 * Matching algorithm:
 *   1. Incoming order scans the opposite side from best price inward
 *   2. At each level, match against FIFO queue from front
 *   3. Generate Trade for each fill
 *   4. Remove filled orders, update partial fills
 *   5. If incoming order has remaining quantity and is a limit order, rest it
 *
 * Invariants (verified by property-based tests):
 *   • No crossed book: best_bid < best_ask after every match cycle
 *   • FIFO: within a price level, earlier orders fill first
 *   • Determinism: given identical input sequence, output is identical
 *   • Conservation: total filled quantity on both sides of every trade is equal
 */
class OrderBook {
public:
    // Trade callback: invoked for each execution
    using TradeCallback = std::function<void(const Trade&)>;

    // Order update callback: invoked for status changes
    using OrderCallback = std::function<void(const Order&)>;

    explicit OrderBook(const std::string& symbol = "")
        : symbol_(symbol)
        , order_arena_(65536)
    {}

    // ───────────────────────────────────────────
    // Multi-subscriber dispatch
    //
    // The legacy set_*_callback API replaces a single slot, which made it
    // impossible to attach the matching engine and a feed publisher to the
    // same book without one clobbering the other. add_*_listener appends
    // to a fan-out list and is what new code should use.
    // ───────────────────────────────────────────
    void add_trade_listener(TradeCallback cb) {
        trade_listeners_.push_back(std::move(cb));
    }
    void add_order_listener(OrderCallback cb) {
        order_listeners_.push_back(std::move(cb));
    }
    void clear_listeners() {
        trade_listeners_.clear();
        order_listeners_.clear();
    }

    // ═══════════════════════════════════════════
    // Order Operations
    // ═══════════════════════════════════════════

    /**
     * Submit a new order. Attempts matching, then rests remainder if limit.
     * Returns the order pointer (owned by arena).
     */
    Order* add_order(const NewOrderRequest& req) {
        // Allocate from arena (zero malloc)
        ts_ = now();                 // one clock read per event; reused for every fill
        Order* order = order_arena_.allocate();
        new (order) Order{};

        order->id        = req.id;
        order->sequence  = next_sequence_++;
        order->side      = req.side;
        order->type      = req.type;
        order->tif       = req.tif;
        order->price     = req.price;
        order->stop_price = req.stop_price;
        order->quantity  = req.quantity;
        order->leaves_qty = req.quantity;
        order->filled_qty = 0;
        order->entry_time = ts_;
        order->last_update = ts_;
        order->status    = OrderStatus::New;
        std::memcpy(order->symbol, req.symbol, sizeof(order->symbol));

        // Index by ID for O(1) cancel/amend
        order_index_[order->id] = order;

        // Stop / StopLimit orders are parked until last-traded price crosses
        // the trigger. We park them first; if the trigger is already in the
        // money relative to the current best they will be released by the
        // explicit check_stop_triggers() call below.
        if (order->type == OrderType::Stop || order->type == OrderType::StopLimit) {
            park_stop_order(order);
            notify_order(*order);
            check_stop_triggers();
            return order;
        }

        // Attempt matching
        match(order);

        // Handle post-match: rest or cancel based on type
        if (order->leaves_qty > 0) {
            switch (order->type) {
                case OrderType::Limit:
                    rest_order(order);
                    break;
                case OrderType::Market:
                case OrderType::IOC:
                    // Cancel unfilled remainder
                    order->cancel(ts_);
                    order_index_.erase(order->id);
                    notify_order(*order);
                    break;
                case OrderType::FOK:
                    // Should have been fully filled or not at all
                    // (FOK pre-check happens in match())
                    order->cancel(ts_);
                    order_index_.erase(order->id);
                    notify_order(*order);
                    break;
                case OrderType::Stop:
                case OrderType::StopLimit:
                    // Handled above before reaching match() — should not get here.
                    break;
            }
        }

        // Each successful aggressive cycle may move the last-traded price and
        // therefore activate parked stop orders.
        check_stop_triggers();

        return order;
    }

    /**
     * Cancel an existing order. O(1) lookup + O(1) removal from level.
     */
    bool cancel_order(OrderId id) {
        auto it = order_index_.find(id);
        if (it == order_index_.end()) return false;

        Order* order = it->second;
        if (!order->is_active()) return false;

        // Stop orders live in stop_orders_, not in the price levels
        if (order->type == OrderType::Stop || order->type == OrderType::StopLimit) {
            unpark_stop_order(order);
            order->cancel();
            order_index_.erase(it);
            notify_order(*order);
            return true;
        }

        // Remove from price level
        remove_from_book(order);

        order->cancel();
        order_index_.erase(it);

        notify_order(*order);
        return true;
    }

    /**
     * Amend price and/or quantity. Price change = cancel + re-insert (loses priority).
     * Quantity reduction preserves priority.
     */
    bool amend_order(const AmendRequest& req) {
        auto it = order_index_.find(req.order_id);
        if (it == order_index_.end()) return false;

        Order* order = it->second;
        if (!order->is_active()) return false;

        ts_ = now();
        bool price_changed = (req.new_price != 0 && req.new_price != order->price);
        bool qty_increased = (req.new_quantity != 0 && req.new_quantity > order->leaves_qty);

        if (price_changed || qty_increased) {
            // Loses queue priority: remove and re-insert
            remove_from_book(order);

