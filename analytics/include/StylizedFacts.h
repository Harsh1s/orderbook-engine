#pragma once

#include "Order.h"
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <string>

namespace micro_exchange::analytics {

using namespace micro_exchange::core;

/**
 * StylizedFacts — Verification of emergent market properties.
 *
 * Background:
 * ───────────
 * "Stylized facts" are statistical regularities observed across virtually
 * all financial markets and time periods (Cont, 2001):
 *
 *   1. Fat tails: Return distributions have excess kurtosis (κ >> 3)
 *      Typical: κ ≈ 5-30 for daily, even higher for intraday
 *
 *   2. Volatility clustering: Large returns beget large returns.
 *      Measured by autocorrelation of |r| or r² at lag 1+.
 *      AC(|r|, lag=1) ≈ 0.15-0.40
 *
 *   3. Asymmetric volatility: Bad news increases vol more than good news
 *      (leverage effect). Correlation(r_t, σ²_{t+1}) < 0.
 *
 *   4. Volume-volatility correlation: High volume episodes have high vol.
 *      Correlation > 0.3 typically.
 *
 *   5. Spread dynamics: Spread widens during high volatility / imbalance
 *      and narrows during calm periods.
 *
 * A simulation that reproduces these facts demonstrates understanding
 * of the mechanisms that generate them (arrival clustering, adverse
 * selection, inventory effects).
 *
 * We compute these metrics from the simulation output and compare
 * against empirical benchmarks.
 */
class StylizedFacts {
public:
    struct FactMetrics {
        // Fat tails
        double return_kurtosis;       // Excess kurtosis (Normal = 0)
        double return_skewness;       // Skewness
        double jarque_bera_stat;      // JB test statistic

        // Volatility clustering
        double abs_return_ac_lag1;    // Autocorrelation of |returns| at lag 1
        double abs_return_ac_lag5;    // At lag 5
        double abs_return_ac_lag10;   // At lag 10
        double squared_return_ac_lag1;

        // Volume-volatility
        double volume_volatility_corr;

        // Spread dynamics
        double spread_vol_corr;       // Correlation(spread, volatility)
        double spread_imbalance_corr; // Correlation(spread, |imbalance|)

        // Summary: which stylized facts are reproduced
        struct FactCheck {
            std::string name;
            bool        reproduced;
            double      value;
            std::string benchmark;
        };
        std::vector<FactCheck> fact_checks;
    };

    /**
     * Compute all stylized fact metrics.
     *
     * @param midprices     Time series of midpoints
     * @param volumes       Volume per interval
     * @param spreads       Spread per interval
     * @param imbalances    Volume imbalance per interval
     */
    FactMetrics compute(
        const std::vector<Price>& midprices,
        const std::vector<double>& mid_times = {},
        double bar_sec = 1.0,
        const std::vector<Quantity>& volumes = {},
        const std::vector<Price>& spreads = {},
        const std::vector<double>& imbalances = {}) const
