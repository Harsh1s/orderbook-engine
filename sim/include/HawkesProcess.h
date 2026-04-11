#pragma once

#include <cstdint>
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>

namespace micro_exchange::sim {

/**
 * HawkesProcess — Self-exciting point process for order arrival times.
 *
 * Theoretical background:
 * ───────────────────────
 * In real markets, order arrivals are NOT Poisson. They exhibit:
 *   • Clustering: bursts of activity (earnings, news, momentum)
 *   • Self-excitation: each event increases the probability of the next
 *   • Long-memory: the intensity function has slow decay
 *
 * The Hawkes process (Hawkes, 1971) captures this with an intensity:
 *
 *   λ(t) = μ + Σ_{t_i < t} α · exp(-β · (t - t_i))
 *
 * Where:
 *   μ (mu)    = baseline intensity (orders/sec in calm market)
 *   α (alpha) = jump size per event (excitation magnitude)
 *   β (beta)  = decay rate (how quickly excitation fades)
 *
 * The branching ratio n = α/β controls the clustering intensity:
 *   n < 1: stationary (required for stability)
 *   n → 0: approaches Poisson
 *   n → 1: heavy clustering (approaches criticality)
 *
 * Empirical calibration (Bacry et al., 2015):
 *   Equity markets: n ≈ 0.6-0.8
 *   FX: n ≈ 0.5-0.7
 *
 * This generates the realistic auto-correlated event times that produce
 * the stylized facts we verify: volatility clustering, fat tails in returns,
 * and time-varying spread behavior.
 *
 * Simulation algorithm: Ogata's thinning method (Ogata, 1981).
 */
class HawkesProcess {
public:
    struct Parameters {
        double mu    = 10.0;
        double alpha = 6.0;
        double beta  = 8.0;

        [[nodiscard]] double branching_ratio() const { return alpha / beta; }
        [[nodiscard]] bool is_stationary() const { return alpha < beta; }
    };
