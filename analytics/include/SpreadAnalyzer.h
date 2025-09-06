#pragma once

#include "Order.h"
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>

namespace micro_exchange::analytics {

using namespace micro_exchange::core;

/**
 * SpreadAnalyzer — Spread decomposition following Huang & Stoll (1997).
 *
 * Theory:
 * ───────
 * The bid-ask spread compensates market makers for three costs:
 *
 *   1. Order processing costs (fixed costs of operating)
 *   2. Inventory holding costs (risk of holding unbalanced position)
 *   3. Adverse selection costs (trading against informed counterparties)
 *
 * We decompose using:
 *
 *   Quoted Spread:    S_q = Ask - Bid
 *   Effective Spread: S_e = 2 · d · (P_trade - M_t)
 *                     where d = +1 for buys, -1 for sells, M = midpoint
 *   Realized Spread:  S_r = 2 · d · (P_trade - M_{t+Δ})
 *                     captures market maker revenue after price moves
 *   Price Impact:     PI = S_e - S_r = 2 · d · (M_{t+Δ} - M_t)
 *                     permanent information content of the trade
 *
 *   Adverse Selection % = PI / S_e
 *
 * The realized spread is the market maker's actual profit per trade.
 * A high adverse selection ratio (>50%) means the spread mostly
 * compensates for trading against informed flow, not order processing.
 *
 * Typical values (US equities, 2020s):
 *   Effective spread: 1-5 bps for large-cap
 *   Adverse selection: 40-70%
 *   Realized spread: 30-60% of effective
 */
class SpreadAnalyzer {
public:
    struct TradeInput {
        Price    trade_price;
        Price    mid_before;     // Midpoint at trade time
        Price    mid_after;      // Midpoint Δ seconds later (typically 5s)
        Quantity volume;
        Side     aggressor;      // Buy-initiated or sell-initiated
    };
