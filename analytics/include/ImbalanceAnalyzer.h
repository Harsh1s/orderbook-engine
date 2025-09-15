#pragma once

#include "Order.h"
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>

namespace micro_exchange::analytics {

using namespace micro_exchange::core;

/**
 * ImbalanceAnalyzer — Order Flow Imbalance (OFI) and return prediction.
 *
 * Theory:
 * ───────
 * Order flow imbalance measures the directional pressure in the order book.
 * Cont, Kukanov & Stoikov (2014) showed that OFI is a strong predictor
 * of short-horizon price changes, explaining 50-65% of variance at
 * 10-second horizons.
 *
 * Definition (event-level OFI):
 *   OFI_t = Σ (buy_volume_t - sell_volume_t) at best bid/ask
 *
 * More precisely, OFI captures changes in the best bid/ask:
 *   ΔB_t = bid_size_t - bid_size_{t-1}  (if bid price unchanged)
 *   ΔA_t = ask_size_t - ask_size_{t-1}
 *   OFI_t = ΔB_t - ΔA_t
 *
 * Predictive regression:
 *   r_{t+1} = α + β · OFI_t + ε
 *
 * Where r_{t+1} is the return over the next interval.
 * β > 0 means buy pressure predicts positive returns.
 *
 * Additional metrics:
 *   • Volume imbalance: (buy_volume - sell_volume) / total_volume
 *   • Depth imbalance: (bid_depth - ask_depth) / (bid_depth + ask_depth)
 *   • Trade imbalance: running count of buy vs sell-initiated trades
 */
class ImbalanceAnalyzer {
public:
    struct BBOSnapshot {
        double   timestamp;
        Price    bid_price;
        Quantity bid_size;
        Price    ask_price;
        Quantity ask_size;
    };

    struct TradeInput {
        double   timestamp;
        Quantity volume;
        Side     aggressor;
    };

    struct ImbalanceMetrics {
        // OFI regression
        double ofi_beta;           // Regression coefficient
        double ofi_r_squared;      // Explanatory power
        double ofi_t_stat;

        // Summary statistics
        double avg_volume_imbalance;
        double avg_depth_imbalance;
        double max_volume_imbalance;

        // By interval
        std::vector<double> ofi_series;
        std::vector<double> return_series;
    };

    /**
     * Compute OFI metrics and return-prediction regression.
     */
    ImbalanceMetrics compute(
        const std::vector<BBOSnapshot>& bbo_snapshots,
        const std::vector<TradeInput>& trades,
        double interval_sec = 10.0) const
