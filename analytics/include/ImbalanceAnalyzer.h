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
    {
        ImbalanceMetrics result{};
        if (bbo_snapshots.size() < 2) return result;

        double max_time = bbo_snapshots.back().timestamp;
        size_t num_intervals = static_cast<size_t>(max_time / interval_sec) + 1;

        // ── Compute OFI per interval ──
        std::vector<double> ofi(num_intervals, 0.0);
        std::vector<double> returns(num_intervals, 0.0);
        std::vector<double> vol_imbalance(num_intervals, 0.0);
        std::vector<double> depth_imbalance(num_intervals, 0.0);

        // Aggregate trades into intervals
        std::vector<Quantity> buy_vol(num_intervals, 0);
        std::vector<Quantity> sell_vol(num_intervals, 0);

        for (const auto& t : trades) {
            size_t bucket = static_cast<size_t>(t.timestamp / interval_sec);
            if (bucket >= num_intervals) bucket = num_intervals - 1;

            if (t.aggressor == Side::Buy) {
                buy_vol[bucket] += t.volume;
            } else {
                sell_vol[bucket] += t.volume;
            }
        }

        // Compute OFI from BBO changes
        for (size_t i = 1; i < bbo_snapshots.size(); ++i) {
            const auto& prev = bbo_snapshots[i-1];
            const auto& curr = bbo_snapshots[i];

            size_t bucket = static_cast<size_t>(curr.timestamp / interval_sec);
            if (bucket >= num_intervals) bucket = num_intervals - 1;

            // OFI = change in bid depth - change in ask depth
            double delta_bid = 0, delta_ask = 0;

            if (curr.bid_price == prev.bid_price) {
                delta_bid = static_cast<double>(curr.bid_size) - prev.bid_size;
            } else if (curr.bid_price > prev.bid_price) {
                delta_bid = static_cast<double>(curr.bid_size);
            } else {
                delta_bid = -static_cast<double>(prev.bid_size);
            }

            if (curr.ask_price == prev.ask_price) {
                delta_ask = static_cast<double>(curr.ask_size) - prev.ask_size;
            } else if (curr.ask_price < prev.ask_price) {
                delta_ask = -static_cast<double>(curr.ask_size);
            } else {
                delta_ask = static_cast<double>(prev.ask_size);
            }

            ofi[bucket] += delta_bid - delta_ask;
        }

        // Compute mid-price returns per interval
        for (size_t i = 0; i < num_intervals; ++i) {
            double t_start = i * interval_sec;
            double t_end = (i + 1) * interval_sec;

            Price mid_start = find_mid_at(bbo_snapshots, t_start);
            Price mid_end = find_mid_at(bbo_snapshots, t_end);

            if (mid_start > 0) {
                returns[i] = static_cast<double>(mid_end - mid_start) / mid_start * 10000.0;  // bps
            }

            // Volume imbalance
            double total = buy_vol[i] + sell_vol[i];
            if (total > 0) {
                vol_imbalance[i] = (static_cast<double>(buy_vol[i]) - sell_vol[i]) / total;
            }
        }

        // ── OFI → Return regression ──
        // Use OFI[i] to predict returns[i+1]
        std::vector<double> x, y;
