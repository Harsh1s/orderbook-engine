#pragma once

#include "Order.h"
#include <cstdint>
#include <cassert>

namespace micro_exchange::core {

/**
 * PriceLevel — A single price level in the order book.
 *
 * Design rationale:
 * ─────────────────
 * Each price level maintains a FIFO queue of orders using an intrusive
 * doubly-linked list. This is the standard exchange technique because:
 *
 *   1. O(1) append (new order at tail)
 *   2. O(1) removal (cancel order by pointer — no search needed)
 *   3. O(1) front access (matching always takes from head)
 *   4. Zero heap allocation (Order structs carry their own prev/next)
 *   5. Cache-friendly traversal (though orders may be scattered;
 *      the arena allocator mitigates this)
 *
 * The intrusive approach avoids std::list's per-node heap allocation
 * and std::deque's indirection overhead. In a real exchange, removing
 * a cancelled order from the middle of a queue is a hot operation —
 * doubly-linked list makes this O(1) given the order pointer.
 *
 * Invariants:
 *   • All orders in the level have the same price
 *   • Orders are in arrival order (sequence number ascending)
 *   • total_quantity == sum of leaves_qty for all orders in the queue
 *   • order_count == number of nodes in the linked list
 */
class PriceLevel {
public:
    explicit PriceLevel(Price price = 0) noexcept
        : price_(price)
    {}

    // ── Queue operations ──

    /**
     * Append an order to the back of the FIFO queue.
     * The order must have the same price as this level.
     */
    void push_back(Order* order) noexcept {
        assert(order != nullptr);
        assert(order->price == price_);

        order->prev = tail_;
        order->next = nullptr;

        if (tail_) {
            tail_->next = order;
        } else {
