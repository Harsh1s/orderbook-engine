#pragma once

#include "Order.h"
#include "OrderBook.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

namespace micro_exchange::core {

/**
 * MatchingEngine — Multi-symbol matching engine facade.
 *
 * Thread safety model:
 * ────────────────────
 * The engine supports two threading models:
 *
 *   1. Single-threaded (default): All operations on one thread.
 *      This is the standard exchange model — events are processed
 *      sequentially from a single gateway queue. Determinism is trivial.
 *
 *   2. Per-symbol sharding: Each OrderBook can be assigned to a
 *      dedicated thread. Cross-symbol operations (rare in equity markets)
 *      require coordination. This is how CME and ICE scale.
 *
 * For the single-threaded hot path, we avoid all locking.
 * The optional mutex is for multi-threaded access patterns only.
 *
 * Sequencing:
 * ───────────
 * Every event (order, cancel, amend, trade) gets a monotonically increasing
 * sequence number. This enables:
 *   • Deterministic replay
 *   • Gap detection in market data feeds
 *   • Consistent ordering across undo/redo
 */
class MatchingEngine {
public:
    struct EngineStats {
        uint64_t total_orders    = 0;
        uint64_t total_cancels   = 0;
        uint64_t total_amends    = 0;
        uint64_t total_trades    = 0;
        uint64_t total_volume    = 0;
        uint64_t total_rejects   = 0;
        uint64_t active_orders   = 0;
        uint64_t symbols_active  = 0;
    };
