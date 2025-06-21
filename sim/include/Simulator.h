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
        engine.add_symbol(config_.symbol);
        auto* book = engine.get_book(config_.symbol);

        FeedPublisher feed;
        feed.attach(*book);

        // Initialize agents
        std::vector<ZIAgent> agents;
        for (size_t i = 0; i < config_.num_agents; ++i) {
            auto params = config_.agent_params;
            params.agent_id = i;
            agents.emplace_back(params, 42 + i);
        }

        // Collect trades
        engine.set_trade_callback([&](const Trade& trade) {
            data.trades.push_back(trade);
        });

        // ── Seed the book ──
        seed_book(engine, config_.symbol, config_.init_price);

        // ── Generate event times ──
        HawkesProcess hawkes(config_.hawkes_params, 12345);
        auto events = hawkes.generate_sided(config_.duration);
        data.event_times.reserve(events.size());

        // ── Process events ──
        OrderId next_id = 10000;

        for (size_t idx = 0; idx < events.size(); ++idx) {
            const auto& event = events[idx];
            data.event_times.push_back(event.timestamp);

            auto mid = book->midprice().value_or(config_.init_price);
            auto sprd = book->spread().value_or(2);

            data.midprices.push_back(mid);
            data.quoted_spreads.push_back(sprd);

            // Select agent
            size_t agent_idx = next_id % config_.num_agents;
            auto& agent = agents[agent_idx];

            // Record pre-trade midpoint for impact analysis
            Price mid_before = mid;

            // Generate and submit order
            auto req = agent.generate_order(mid, sprd, event.is_buy, next_id++,
                                             config_.symbol.c_str());
            size_t trades_before = data.trades.size();
            engine.submit_order(req);

            // If a trade occurred, record analytics
            if (data.trades.size() > trades_before) {
                Price mid_after = book->midprice().value_or(mid_before);
                for (size_t t = trades_before; t < data.trades.size(); ++t) {
                    SimulationData::TradeRecord rec;
                    rec.trade_price = data.trades[t].price;
                    rec.mid_before = mid_before;
                    rec.mid_after_1s = mid_after;  // Approximate
                    rec.mid_after_5s = mid_after;  // Will refine in analytics
                    rec.volume = data.trades[t].quantity;
                    rec.aggressor = data.trades[t].aggressor;
                    data.trade_records.push_back(rec);
                }
            }

            // Periodic cancellation sweep
            if (idx % 50 == 0) {
                data.total_cancels += cancel_stale_orders(engine, agents, book, config_.symbol);
            }
        }

        data.total_orders = events.size();

        // Backfill mid_after_5s using forward-looking midprices
        backfill_future_midprices(data);

        auto wall_end = std::chrono::high_resolution_clock::now();
        data.wall_time_sec = std::chrono::duration<double>(wall_end - wall_start).count();

        return data;
    }

