#pragma once

#include "Order.h"
#include <cstdint>
#include <cstring>

namespace micro_exchange::md {

using namespace micro_exchange::core;

/**
 * Feed message types — modeled after NASDAQ ITCH 5.0 protocol.
 *
 * The wire protocol uses tagged union messages with a fixed header.
 * In production, these would be serialized to a binary format with
 * network byte order. Here we use the in-memory representation directly.
 *
 * Message types:
 *   A — Add order (new resting order)
 *   X — Order executed (trade)
 *   D — Order deleted (cancel or fill)
 *   U — Order replaced (amend)
 *   S — Snapshot (full book state)
 *   T — Trade (execution report)
 *   Q — Quote update (BBO change)
 */

enum class FeedMessageType : uint8_t {
    AddOrder        = 'A',
    ExecuteOrder    = 'X',
    DeleteOrder     = 'D',
    ReplaceOrder    = 'U',
    Snapshot        = 'S',
    Trade           = 'T',
    QuoteUpdate     = 'Q',
    SystemEvent     = 'E',
};

/**
 * Fixed-size feed message for zero-copy transport over the SPSC buffer.
 * 128 bytes — fits two cache lines.
 */
struct alignas(64) FeedMessage {
    // ── Header (common to all message types) ──
    FeedMessageType type     = FeedMessageType::SystemEvent;
    SeqNum          sequence = 0;
    uint64_t        timestamp_ns = 0;  // Nanoseconds since epoch
    char            symbol[16] = {};
