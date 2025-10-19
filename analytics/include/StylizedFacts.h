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
    {
        FactMetrics result{};

        // ── Sample the midprice on fixed clock-time bars, then take LOG returns ──
        //
        // Why not per-event returns? The raw event series samples the midpoint on
        // every order arrival, but ~99% of arrivals don't move the (integer-tick)
        // mid. That yields a return series dominated by exact zeros, which
        // manufactures enormous spurious excess kurtosis (a delta spike at zero)
        // and crushes the autocorrelation of |r|. Stylized facts are defined on
        // returns sampled at a fixed frequency (Cont, 2001), so we bucket the mid
        // into bars of `bar_sec` seconds, take the last mid observed in each bar,
        // and compute log returns r_k = ln(M_k / M_{k-1}).
        std::vector<double> bar_close;
        if (!mid_times.empty() && mid_times.size() == midprices.size() && bar_sec > 0.0) {
            const double t0 = mid_times.front();
            long cur_bar = -1;
            double last_mid = 0.0;
            for (size_t i = 0; i < midprices.size(); ++i) {
                long b = static_cast<long>((mid_times[i] - t0) / bar_sec);
                if (b != cur_bar) {
                    if (cur_bar >= 0) bar_close.push_back(last_mid);
                    cur_bar = b;
                }
                last_mid = static_cast<double>(midprices[i]);
            }
            if (cur_bar >= 0) bar_close.push_back(last_mid);
        } else {
            // Fallback (no timestamps supplied): use the raw mid series as-is.
            bar_close.assign(midprices.begin(), midprices.end());
        }

        std::vector<double> returns;
        returns.reserve(bar_close.size());
        for (size_t i = 1; i < bar_close.size(); ++i) {
            if (bar_close[i-1] > 0.0 && bar_close[i] > 0.0) {
                returns.push_back(std::log(bar_close[i] / bar_close[i-1]));
            }
        }

        if (returns.size() < 20) return result;

        // ── Fat tails ──
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double var = 0, m3 = 0, m4 = 0;
        for (double r : returns) {
            double d = r - mean;
            var += d * d;
            m3 += d * d * d;
            m4 += d * d * d * d;
        }
        var /= returns.size();
        m3 /= returns.size();
        m4 /= returns.size();

        double std_dev = std::sqrt(var);
        if (std_dev > 0) {
            result.return_skewness = m3 / (std_dev * std_dev * std_dev);
            result.return_kurtosis = m4 / (var * var) - 3.0;  // Excess kurtosis
        }

        // Jarque-Bera
        double n = returns.size();
        result.jarque_bera_stat = (n / 6.0) *
            (result.return_skewness * result.return_skewness +
             0.25 * result.return_kurtosis * result.return_kurtosis);

        // ── Volatility clustering ──
        std::vector<double> abs_returns(returns.size());
        std::vector<double> sq_returns(returns.size());
        std::transform(returns.begin(), returns.end(), abs_returns.begin(),
            [](double r) { return std::abs(r); });
        std::transform(returns.begin(), returns.end(), sq_returns.begin(),
            [](double r) { return r * r; });

        result.abs_return_ac_lag1 = autocorrelation(abs_returns, 1);
        result.abs_return_ac_lag5 = autocorrelation(abs_returns, 5);
        result.abs_return_ac_lag10 = autocorrelation(abs_returns, 10);
        result.squared_return_ac_lag1 = autocorrelation(sq_returns, 1);

        // ── Volume-volatility correlation ──
        if (!volumes.empty() && volumes.size() >= returns.size()) {
            std::vector<double> vol_d(volumes.begin(),
                volumes.begin() + std::min(volumes.size(), abs_returns.size()));
            std::vector<double> abs_r(abs_returns.begin(),
                abs_returns.begin() + vol_d.size());
            result.volume_volatility_corr = correlation(
                std::vector<double>(vol_d.begin(), vol_d.end()), abs_r);
        }

