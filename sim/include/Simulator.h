#pragma once

#include "HawkesProcess.h"
#include "ZIAgent.h"
#include "MatchingEngine.h"
#include "FeedPublisher.h"

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <numeric>
#include <cmath>
#include <algorithm>

namespace micro_exchange::sim {

using namespace micro_exchange::core;
using namespace micro_exchange::md;

// Full pipeline: hawkes events -> ZI agents -> matching -> feed -> analytics
// Took a while to get the cancellation logic right, kept getting stale orders
// clogging the book in early versions. Current approach is "good enough" but
// not how a real exchange handles it (they track per-session order lists).
class Simulator {
public:
    struct Config {
        std::string symbol     = "AAPL";
        double      duration   = 3600.0;
        Price       init_price = 15000;
        size_t      num_agents = 10;

        HawkesProcess::Parameters hawkes_params;
        ZIAgent::Parameters agent_params;

        Config() {
            hawkes_params.mu = 50.0;
            hawkes_params.alpha = 35.0;
            hawkes_params.beta = 50.0;
            agent_params.sigma_price = 8.0;
            agent_params.market_order_prob = 0.12;
            agent_params.mean_size = 200.0;
            agent_params.sigma_size = 0.7;
            agent_params.cancel_base_prob = 0.03;
            agent_params.cancel_distance_mult = 0.004;
        }
    };

    // Collected data for downstream analytics
    struct SimulationData {
        std::vector<Trade> trades;
        std::vector<Price> midprices;         // Time series of midpoints
        std::vector<Price> quoted_spreads;    // Spread at each event
        std::vector<double> event_times;      // Hawkes timestamps

        // Per-trade analytics inputs
        struct TradeRecord {
            Price  trade_price;
            Price  mid_before;
            Price  mid_after_1s;   // Midpoint ~1 second later
            Price  mid_after_5s;
            Quantity volume;
            Side   aggressor;
        };
        std::vector<TradeRecord> trade_records;

        // Engine stats
        uint64_t total_orders  = 0;
        uint64_t total_cancels = 0;
        double   wall_time_sec = 0;
    };

    explicit Simulator(Config config = {}) : config_(config) {}

    /**
     * Run full simulation and return collected data.
     */
    SimulationData run() {
        auto wall_start = std::chrono::high_resolution_clock::now();
        SimulationData data;

        // ── Setup ──
        MatchingEngine engine;
