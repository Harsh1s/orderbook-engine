#pragma once

#include "FeedMessage.h"
#include "SPSCRingBuffer.h"
#include "OrderBook.h"

#include <vector>
#include <functional>
#include <fstream>
#include <string>

namespace micro_exchange::md {

using namespace micro_exchange::core;

/**
 * FeedPublisher — Publishes incremental book updates and snapshots.
 *
 * The publisher sits between the matching engine and downstream consumers
 * (analytics, logging, network dissemination). It transforms engine events
 * into a standardized feed protocol.
 *
 * Architecture:
 *   [MatchingEngine] → callbacks → [FeedPublisher] → SPSC buffer → [consumers]
 *
 * The publisher maintains sequence numbers for gap detection and supports
 * periodic snapshot generation for client recovery.
 */
class FeedPublisher {
public:
    static constexpr size_t BUFFER_SIZE = 1 << 16;  // 65536 messages

    using MessageCallback = std::function<void(const FeedMessage&)>;

    FeedPublisher() = default;

    /**
     * Wire up to an OrderBook's callbacks.
     *
     * Uses the multi-listener fan-out so the engine's own trade routing and
     * any other subscribers continue to receive events alongside us.
     */
    void attach(OrderBook& book) {
        book.add_trade_listener([this, &book](const Trade& trade) {
            publish_trade(trade);
            publish_bbo_update(book);
        });

        book.add_order_listener([this, &book](const Order& order) {
            if (order.status == OrderStatus::New || order.status == OrderStatus::Amended) {
                publish_add(order);
            } else if (order.status == OrderStatus::Cancelled) {
                publish_delete(order);
            }
            publish_bbo_update(book);
        });
    }

    /**
     * Generate a full book snapshot for recovery.
     */
    FeedMessage generate_snapshot(const OrderBook& book) {
        FeedMessage snap{};
        snap.type = FeedMessageType::Snapshot;
        snap.sequence = next_seq_++;

        auto bb = book.best_bid();
        auto ba = book.best_ask();
        snap.best_bid = bb.value_or(0);
