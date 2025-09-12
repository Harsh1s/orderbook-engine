/*
 * bench_latency.cpp - per-operation latency histogram for the matching engine.
 *
 * Measures the wall-clock cost of submit_order() across a long stream of
 * synthetic add/cancel/match operations and reports p50/p90/p95/p99/p999
 * along with min/max. The intent is to give a quick "is anything regressing"
 * signal that can be wired into CI or run by hand before tagging a release.
 *
 * Usage:
 *   ./bench_latency                 # 1M operations against a 10x5 seeded book
 *   ./bench_latency --ops 5000000   # 5M operations
 *   ./bench_latency --warmup 200000 # warmup count before timing starts
 */

#include "MatchingEngine.h"
#include "OrderBook.h"
#include "Order.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace micro_exchange::core;

namespace {

struct CliArgs {
    size_t ops    = 1'000'000;
    size_t warmup =   100'000;
};

CliArgs parse(int argc, char** argv) {
    CliArgs a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--ops" && i + 1 < argc)    a.ops    = std::stoull(argv[++i]);
        else if (s == "--warmup" && i + 1 < argc) a.warmup = std::stoull(argv[++i]);
        else if (s == "--help") {
            std::cout << "usage: bench_latency [--ops N] [--warmup N]\n";
            std::exit(0);
        }
    }
    return a;
}

void seed_book(MatchingEngine& engine, const char* sym, Price mid) {
