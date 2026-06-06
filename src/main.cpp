/*
 * main.cpp - MicroExchange CLI
 *
 * Runs the full pipeline: hawkes event generation -> ZI agents ->
 * matching engine -> feed publisher -> analytics.
 *
 * Usage:
 *   ./micro_exchange                      # default 1hr simulation
 *   ./micro_exchange --duration 7200      # 2hr simulation
 *   ./micro_exchange --symbol AAPL        # set the symbol
 *   ./micro_exchange --output results/    # custom output dir
 *   ./micro_exchange -v                   # verbose
 */

#include "MatchingEngine.h"
#include "OrderBook.h"
#include "Order.h"
#include "FeedPublisher.h"
#include "HawkesProcess.h"
#include "ZIAgent.h"
#include "SpreadAnalyzer.h"
#include "ImpactAnalyzer.h"
#include "StylizedFacts.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <string>
#include <vector>
#include <numeric>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>

using namespace micro_exchange::core;
using namespace micro_exchange::md;
using namespace micro_exchange::sim;
using namespace micro_exchange::analytics;

namespace fs = std::filesystem;

// ── Config ──

struct RunConfig {
    std::string symbol    = "AAPL";
    double      duration  = 3600.0;
    Price       init_mid  = 15000;  // $150.00
    size_t      n_agents  = 10;
    std::string out_dir   = "output";
    bool        verbose   = false;
};

RunConfig parse_args(int argc, char* argv[]) {
    RunConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i + 1 < argc) cfg.duration = std::stod(argv[++i]);
        else if (arg == "--symbol" && i + 1 < argc) cfg.symbol = argv[++i];
        else if (arg == "--output" && i + 1 < argc) cfg.out_dir = argv[++i];
        else if (arg == "-v" || arg == "--verbose") cfg.verbose = true;
        else if (arg == "--help") {
            std::cout << "Usage: micro_exchange [--duration SEC] [--symbol SYM] [--output DIR] [-v]\n";
            std::exit(0);
        }
    }
    return cfg;
}

// ── Helpers ──

void seed_book(MatchingEngine& engine, const std::string& symbol, Price mid) {
    // 10 levels each side, 5 orders per level
    // this gives a reasonable starting book so the first few market orders
    // don't just sail through into the void
    OrderId id = 1;
    for (int lvl = 1; lvl <= 10; ++lvl) {
        for (int j = 0; j < 5; ++j) {
            NewOrderRequest bid{};
            bid.id = id++;
            bid.side = Side::Buy;
            bid.type = OrderType::Limit;
            bid.tif = TimeInForce::GTC;
            bid.price = mid - lvl;
            bid.quantity = 100 + (j * 50);
            std::strncpy(bid.symbol, symbol.c_str(), 15);
            engine.submit_order(bid);

            NewOrderRequest ask{};
            ask.id = id++;
            ask.side = Side::Sell;
            ask.type = OrderType::Limit;
            ask.tif = TimeInForce::GTC;
            ask.price = mid + lvl;
            ask.quantity = 100 + (j * 50);
            std::strncpy(ask.symbol, symbol.c_str(), 15);
            engine.submit_order(ask);
        }
    }
}

void write_trades_csv(const std::string& path, const std::vector<Trade>& trades) {
    std::ofstream ofs(path);
    ofs << "seq,buy_id,sell_id,price,qty,aggressor\n";
    for (const auto& t : trades) {
        ofs << t.sequence << ","
            << t.buy_order_id << ","
            << t.sell_order_id << ","
            << t.price << ","
            << t.quantity << ","
            << (t.aggressor == Side::Buy ? "B" : "S") << "\n";
    }
}

void write_midprices_csv(const std::string& path, const std::vector<Price>& mids) {
    std::ofstream ofs(path);
    ofs << "idx,midprice\n";
    for (size_t i = 0; i < mids.size(); ++i) {
        ofs << i << "," << mids[i] << "\n";
    }
}

void write_spreads_csv(const std::string& path, const std::vector<Price>& spreads) {
    std::ofstream ofs(path);
    ofs << "idx,quoted_spread\n";
    for (size_t i = 0; i < spreads.size(); ++i) {
        ofs << i << "," << spreads[i] << "\n";
    }
}

// ── Main ──

