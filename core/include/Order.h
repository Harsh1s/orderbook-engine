#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include <cstring>
#include <limits>

namespace micro_exchange::core {

// ─────────────────────────────────────────────
// Enumerations
// ─────────────────────────────────────────────

enum class Side : uint8_t {
    Buy  = 0,
    Sell = 1
};

enum class OrderType : uint8_t {
    Limit     = 0,
    Market    = 1,
    IOC       = 2,   // Immediate or Cancel
    FOK       = 3,   // Fill or Kill
    Stop      = 4,   // Triggers a Market order when last trade crosses stop_price
    StopLimit = 5    // Triggers a Limit order at `price` when stop_price is crossed
};

enum class TimeInForce : uint8_t {
    GTC = 0,  // Good till cancel
    IOC = 1,  // Immediate or cancel
    FOK = 2,  // Fill or kill
    DAY = 3   // Day order
};

enum class OrderStatus : uint8_t {
    New             = 0,
    PartiallyFilled = 1,
    Filled          = 2,
    Cancelled       = 3,
    Rejected        = 4,
    Amended         = 5
};

// ─────────────────────────────────────────────
// Price representation
// Fixed-point: price in ticks (integer cents or sub-cents)
// Avoids floating-point in the hot path entirely.
// ─────────────────────────────────────────────

using Price    = int64_t;   // Price in ticks (1 tick = 0.01 USD by default)
using Quantity = uint64_t;
using OrderId  = uint64_t;
using SeqNum   = uint64_t;

static constexpr Price PRICE_INVALID = std::numeric_limits<Price>::max();
static constexpr Price PRICE_MARKET  = 0;  // Market orders have no price limit

// ─────────────────────────────────────────────
// Timestamp: nanosecond precision
