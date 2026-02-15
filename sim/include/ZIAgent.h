#pragma once

#include "../../core/include/Order.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <vector>

namespace micro_exchange::sim {

using namespace micro_exchange::core;

/**
 * ZIAgent — Zero-Intelligence trader with strategic cancellations.
 *
 * Theoretical background:
 * ───────────────────────
 * Zero-intelligence (ZI) models (Gode & Sunder, 1993) show that many
 * market properties emerge from the mechanics of the double auction itself,
 * not from trader sophistication. However, pure ZI misses:
 *
 *   • Realistic spread formation (ZI spreads are too wide)
 *   • Volatility clustering (ZI returns are too thin-tailed)
 *   • Strategic cancellation (real traders pull stale quotes)
 *
 * Our ZI-C (ZI with cancels) variant adds:
 *   1. Price placement relative to midpoint (not uniform over all prices)
 *   2. Strategic cancellation: orders far from mid get cancelled faster
 *   3. Size variation: log-normal order sizes (empirical fact)
 *
 * This produces realistic spread behavior and, combined with the Hawkes
 * arrival process, generates the stylized facts we verify.
 *
 * Parameters calibrated to approximate equity LOB dynamics:
 *   • σ_price: spread of limit order placement around mid
 *   • cancel_distance_factor: how far from mid before cancel probability rises
 *   • mean_size: average order size (shares)
 */
class ZIAgent {
public:
    struct Parameters {
        double sigma_price       = 5.0;
        double market_order_prob = 0.15;
        double mean_size    = 100.0;
        double sigma_size   = 0.8;
        double cancel_base_prob     = 0.02;
        double cancel_distance_mult = 0.005;
        uint64_t agent_id = 0;

        Parameters() = default;
    };

    explicit ZIAgent(Parameters params, uint64_t seed = 42)
