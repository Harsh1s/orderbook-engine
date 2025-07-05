#pragma once

#include "Order.h"
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <cassert>

namespace micro_exchange::analytics {

using namespace micro_exchange::core;

/**
 * ImpactAnalyzer — Price impact measurement and Kyle's lambda estimation.
 *
 * Theory:
 * ───────
 * Kyle (1985) established the fundamental model of informed trading:
 *
 *   ΔP = λ · ΔX + ε
 *
 * Where:
 *   ΔP = price change over interval
 *   ΔX = net signed order flow (buy volume - sell volume)
 *   λ  = Kyle's lambda (price impact coefficient)
 *   ε  = noise
 *
 * λ measures the market's "price impact per unit of order flow."
 * Higher λ means:
 *   • Less liquid market
 *   • More information in order flow
 *   • Wider effective spreads
 *
 * Kyle showed that in equilibrium, λ = σ_v / (2 · σ_u)
 * where σ_v = volatility of fundamental value, σ_u = noise trader volume.
 *
 * We estimate λ using OLS on aggregated intervals:
 *   1. Divide trading day into N intervals (e.g., 5-second buckets)
 *   2. Compute ΔP and ΔX for each interval
 *   3. Run regression ΔP = α + λ · ΔX + ε
 *
 * Additional impact metrics:
 *   • Temporary impact: price move that reverts (inventory/bounce)
 *   • Permanent impact: price move that persists (information)
 *   • Impact curve: impact as function of trade size quantile
 */
class ImpactAnalyzer {
public:
    struct TradeInput {
        double   timestamp;      // Seconds since start
        Price    price;
        Quantity volume;
        Side     aggressor;
    };

    struct KyleLambdaResult {
        double lambda;           // Price impact coefficient
        double alpha;            // Intercept
        double r_squared;        // Goodness of fit
        double t_statistic;      // Statistical significance of lambda
        double std_error;        // Standard error of lambda
        size_t num_intervals;    // Number of intervals used
    };

    struct ImpactCurvePoint {
        double volume_quantile;  // 0-100 percentile
        double avg_impact;       // Average absolute price impact
    };

    /**
     * Estimate Kyle's lambda via OLS regression.
     *
     * @param trades      Vector of trade records
     * @param midprices   Midprice time series
     * @param interval_sec  Aggregation interval (seconds)
     */
    KyleLambdaResult estimate_kyle_lambda(
        const std::vector<TradeInput>& trades,
        const std::vector<std::pair<double, Price>>& timed_midprices,
        double interval_sec = 5.0) const
    {
