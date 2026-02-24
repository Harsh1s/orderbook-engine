# MicroExchange

[![CI](https://github.com/Leotaby/MicroExchange/actions/workflows/ci.yml/badge.svg)](https://github.com/Leotaby/MicroExchange/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)

**Exchange-grade CLOB matching engine + ITCH-style market data replay + microstructure analytics in modern C++20.**

> **[📊 Live Interactive Dashboard](https://Leotaby.github.io/MicroExchange/)** — 3D order book surface, Kyle's lambda landscape, spread decomposition, stylized facts.

A complete market microstructure laboratory: from order entry to trade print, from raw event feeds to empirical spread decomposition, built with the rigor of production exchange systems and the analytical depth of graduate-level financial economics.

### Visualizations

**3D Limit Order Book Surface** - Bid (blue) and ask (red) depth across price levels over time:

![Order Book Surface](docs/images/orderbook_3d.png)

**3D Price Impact Surface** - Kyle's lambda: impact increases with volume (concave, square-root law) and amplifies with directional imbalance:

![Price Impact Surface](docs/images/impact_surface_3d.png)

**Spread Decomposition** - Effective spread split into realized spread (MM revenue) and price impact. *Real large-caps run ~50–70% adverse selection; this zero-intelligence sim produces ≈0 (uninformed flow → no permanent impact) - see [reproducible results](#sample-results).*

![Spread Decomposition](docs/images/spread_decomposition.png)

**Stylized Facts** - Return distribution vs Gaussian and the autocorrelation of |returns|. *Reproduced on 1s bars: volatility clustering AC(\|r\|,1) ≈ 0.24; fat tails are mild (excess kurtosis ≈ 1.2) under zero-intelligence flow - see [reproducible results](#sample-results).*

![Stylized Facts](docs/images/stylized_facts.png)

---

## Architecture

```
  order sources                  matching core                      outputs
  ─────────────                  ─────────────                      ───────

  ┌──────────────┐
  │ TCP Gateway  │─┐   binary order-entry protocol over a socket (net/)
  └──────────────┘ │
  ┌──────────────┐ │   ┌─────────────────────────────┐    ┌──────────────────┐
  │ Simulation   │ ├──▶│      Matching Engine        │──▶ │ Market Data Feed │
  │ (Hawkes/ZI)  │ │   │ price-time priority (FIFO)  │    │ (ITCH-style:     │
  └──────────────┘ │   │ Limit/Market/IOC/FOK/Stop   │    │ incremental +    │
  ┌──────────────┐ │   │                             │    │ snapshots)       │
  │ ITCH Replay  │─┘   │ book backends (same API):   │    └──────────────────┘
  │ (historical) │     │  • OrderBook     (std::map) │    ┌──────────────────┐
  └──────────────┘     │  • ArrayOrderBook (array +  │──▶ │ Analytics        │
                       │     bitmap BBO index)       │    │ spread decomp,   │
                       └──────────────────────-──────┘    │ Kyle's λ, OFI,   │
                                                          │ stylized facts   │
                                                          └──────────────────┘
```

---

## Visualizations

> **[→ Interactive 3D charts (GitHub Pages)](https://Leotaby.github.io/MicroExchange/docs/visualizations.html)**

### 3D Order Book Surface — Depth × Price × Time
Bid side (blue) and ask side (red) form the characteristic valley around the midpoint. Depth clusters at key levels and shifts with the price drift.

![Order Book 3D](docs/images/orderbook_3d.png)

### 3D Price Impact Surface — Kyle's λ Landscape
Price impact as a function of trade volume and order flow imbalance. The concave shape demonstrates the square-root law of impact (Bouchaud et al., 2018) - larger trades have diminishing marginal impact, amplified by directional imbalance.

![Impact 3D](docs/images/impact_3d.png)

### Spread Decomposition — Huang-Stoll (1997)
Effective spread decomposed into realized spread (market maker revenue) and price impact. In liquid equities adverse selection is ~50-70%; under zero-intelligence flow it collapses to ≈0 (no informed trading), consistent with Kyle's λ ≈ 0. Reproducing realistic adverse selection requires informed agents (see Known Issues / future work).

![Spread Decomposition](docs/images/spread_decomposition.png)

### Stylized Facts: Fat Tails & Volatility Clustering
Left: return distribution vs Gaussian - heavy tails from Hawkes-driven clustering. Right: autocorrelation of |returns| showing slow decay characteristic of ARCH effects.

![Stylized Facts](docs/images/stylized_facts.png)

---

## Order Types

| Type | TIF | Behaviour |
|---|---|---|
| **Limit** | GTC / DAY | Rests on the book at `price`. |
| **Market** | IOC | Crosses the book at any price; unfilled remainder cancelled. |
| **IOC** | IOC | Limit semantics; remainder after the first match is cancelled. |
| **FOK** | FOK | Pre-checked for full fill; if not, never enters the book. |
| **Stop** | - | Parked until `last_trade_price` crosses `stop_price`, then released as Market. |
| **StopLimit** | - | Parked until trigger; released as Limit at `price`. |

Stops are stored in dedicated per-side multimaps keyed by trigger price.
Every aggressive cycle that updates the last print runs a guarded
`check_stop_triggers()` pass — releases are themselves matched immediately,
which can cascade into more triggers without recursing on the call stack.

## Microstructure Concepts Implemented

| Domain | Concept | Implementation |
|--------|---------|---------------|
| **Market Structure** | Price-time priority (FIFO) | `core/OrderBook` with deterministic sequencing |
| **Market Structure** | Queue position tracking | Per-level FIFO queues with sequence numbers |
| **Liquidity** | Quoted spread | Real-time BBO tracking in `analytics/SpreadAnalyzer` |
| **Liquidity** | Effective spread | Trade-midpoint deviation analysis |
| **Liquidity** | Depth & resilience | Post-trade book recovery metrics |
| **Price Formation** | Realized spread | 5-second post-trade midpoint reversion |
| **Price Formation** | Price impact (permanent) | Effective − Realized spread decomposition |
| **Information** | Order flow imbalance (OFI) | Signed volume aggregation → return prediction |
| **Information** | Kyle's λ | Regression: ΔP = λ · signed_volume + ε |
| **Adverse Selection** | Glosten-Milgrom intuition | Spread widens with information asymmetry in simulation |
| **Inventory** | Ho-Stoll / Avellaneda-Stoikov | Quote skewing under inventory risk in MM agent |
| **Stylized Facts** | Fat tails, vol clustering | Hawkes arrival process + empirical verification |
| **Stylized Facts** | Spread under stress | Endogenous widening with order imbalance |

---

## What Makes This Different

Most GitHub "matching engines" are toy implementations — a sorted map, a match loop, and a README. This project bridges **three disciplines**:

1. **Systems engineering** - Lock-free queues, arena allocation, cache-aligned structures, deterministic replay, property-based invariant testing
2. **Financial economics** - Spread decomposition, adverse selection models, information-based trading theory (Glosten-Milgrom, Kyle, Ho-Stoll)
3. **Quantitative research** - Reproducible empirical analysis, stylized fact generation, microstructure model calibration

---

## Known Issues & Limitations

- **No informed traders → adverse selection ≈ 0**: Agents are zero-intelligence, so order flow carries no private information. Both the Huang-Stoll decomposition (price impact ≈ 0) and Kyle's λ (R² ≈ 0.01) correctly report this. It is a *modeling* limitation, not a bug - reproducing realistic adverse selection (~50–70% of the spread) requires a Glosten-Milgrom-style informed-trader population. Tracked as future work.

- **Fat tails are mild**: on 1s bars the ZI midprice is contained (a ~15-tick range over the hour), so excess kurtosis is ~1.2 — present but below intraday equities. Deep tails need informed/trending flow or a fundamental-value process.

- **Arena allocator never frees**: Orders accumulate in the arena for the lifetime of the process. Fine for simulation (it exits) but would need periodic cleanup or epoch-based reclamation for production.

- **No proper order tracking per agent**: The cancellation logic in the simulator is approximate — agents don't track their own outstanding orders, so cancel rates are estimates.

- **No iceberg / hidden-quantity orders yet.** Refilling visible slices interacts with FIFO priority in a non-obvious way; tracked in `CHANGELOG.md` as future work.

- **Visualization PNGs predate v1.2.0**: the 3D surface images above are illustrative and were rendered before the analytics fixes below; the authoritative, reproducible numbers live in [`output/report.txt`](output/report.txt). Regenerating the figures from current output is tracked as future work.

### Resolved in v1.2.0
- ~~Volatility clustering is weak (AC\|r\| ≈ 0.02)~~ — was a sampling artifact. Returns are now computed on fixed 1-second bars as log returns instead of per-event on the integer-tick mid; **AC(|r|, lag 1) = 0.24**, inside the empirical 0.15–0.40 range.
- ~~Excess kurtosis is a spurious 78~~ — same root cause (a return series that was ≈99% exact zeros). Time-bar log returns give a realistic **1.16**.
- ~~Spread decomposition doesn't satisfy `effective = realized + impact`~~ — the price-impact term was averaged in absolute value while realized was signed. Now consistently signed, so the identity holds and the adverse-selection % is meaningful.

### Resolved in v1.1.0
- ~~FeedPublisher overwrites OrderBook callbacks~~ — fixed by a multi-subscriber listener fan-out on `OrderBook`. The publisher is now re-enabled in `main.cpp` and reports message counts in the per-run report.
- ~~Kyle's lambda R² is near zero because of event-index bucketing~~ — the regression now uses Hawkes wall-clock timestamps for both trades and midprices. (R² is still low because ZI flow is uninformed — see above — but the bucketing is no longer the bottleneck.)

---

## Build

Requires C++20 and CMake 3.20+.

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Or without CMake:
```bash
g++ -std=c++20 -O2 -I core/include -I md/include -I sim/include -I analytics/include \
    src/main.cpp -o build/micro_exchange
```

### Run Simulation
```bash
# Default 1-hour Hawkes simulation → match → analytics → output/
./bin/micro_exchange

# Custom duration (seconds), symbol, and output directory
./bin/micro_exchange --duration 7200 --symbol AAPL --output output

# Verbose
./bin/micro_exchange -v
```
> Binary feed replay is implemented at the library level (`md/FeedReplayer`,
> via `FeedPublisher::dump_to_file`) but is not yet wired to the CLI — tracked
> as future work alongside real NASDAQ ITCH ingestion.

### Run Tests & Benchmarks
```bash
# Full CTest suite (invariants + fuzz + end-to-end smoke run)
cd build && ctest --output-on-failure

# Or invoke binaries directly
./bin/test_invariants            # Property-based + fuzz + stop-order tests
./bin/bench_throughput           # Single-thread matching throughput
./bin/bench_latency              # Latency histogram (p50/p90/p95/p99/p99.9)
./bin/bench_latency --ops 5000000   # ...with a longer run
```

---

## Sample Results

### Throughput & Latency
```
Single-thread matching throughput: 2.24M orders/sec (1M order run)
Mean latency:     598 ns
P50 latency:      213 ns
P90 latency:      460 ns
P95 latency:      543 ns
P99 latency:      716 ns
P99.9 latency:  1,033 ns
```
*(Throughput/latency are hardware-dependent — numbers above are from the committed `output/benchmark_results.txt`. The analytics below are deterministic and identical on any machine.)*

### Order book: `std::map` vs tick-indexed array

The engine ships **two** order-book implementations behind identical matching
semantics: the default `OrderBook` (two `std::map`s) and `ArrayOrderBook`, a
contiguous tick-indexed array with a **bitmap occupied-index** (hardware
`ctz`/`clz` bit-scan for the best-bid/ask cursors). `bench_orderbook_compare`
runs the same order stream through both, asserts the trade streams are
**byte-identical**, then compares performance (1M orders, ~9900–10100 band):

```
Correctness:  identical trade stream on every run  ✓  (CI-gated equivalence test)

Performance (1M orders; hardware-dependent - example below is Apple M-series)
                    std::map       array (bitmap)
  Throughput        ~7.9 M/s        ~9.6 M/s        → ~1.2x
  Latency  P50      84 ns           83 ns
  Latency  P99      292 ns          250 ns          → lower tail
```

Two takeaways that matter more than a single headline number:

1. **Where the win lands depends on the CPU.** On Apple Silicon the array is
   ~1.2× higher throughput with a lower tail; on the x86 CI sandbox throughput
   is ~even but median latency drops ~33% (84 ns vs 125 ns). Either way the
   array is competitive-or-faster — and the ceiling is set by the per-order
   `OrderId→Order*` hash insert and the `now()` timestamp, *not* the level
   container. **v1.5.0 acts on exactly this:** capturing the timestamp once per
   order instead of once per fill raised both books ~28–30% on x86 (`std::map`
   5.9M→7.6M/s, array 6.7M→8.8M/s); the `OrderId` hash insert is now the
   dominant remaining per-order cost.
2. **A flat array needs an index.** A naive linear best-bid/ask scan is ~25×
   *slower* than `std::map` on a wide/sparse book because it walks empty levels;
   the bitmap occupied-index fixes that and keeps the array ≥1.0× through
   realistic band widths. Extreme sparsity (200k levels) still favours a
   two-level summary bitmap — tracked as future work.

Run it yourself: `./bin/bench_orderbook_compare`.

### Networked order entry (TCP gateway)

The order books are in-process; a real exchange sits behind a network front-end.
`net/OrderGateway` is a single-threaded TCP gateway that accepts a client
connection, decodes a compact **binary order-entry protocol**
(`net/OrderEntryProtocol.h` — length-prefixed framing; `NewOrder`/`Cancel` in,
`Exec`/`Ack` out), feeds the `MatchingEngine`, and streams execution reports
back. It mirrors the standard exchange model: one sequential gateway in front of
a single-threaded, deterministic matching core.

The end-to-end test (`test_gateway`, CI-gated) starts the server on an ephemeral
port, streams 3,000 orders over a real loopback socket, and asserts the
networked path produces **exactly the same trades and volume** as the in-process
engine — serialising orders over TCP changes nothing about the match.

One low-latency networking detail worth calling out: both ends set
**`TCP_NODELAY`**. A lock-step order-entry protocol sends tiny messages, and
leaving Nagle's algorithm on (the default) collides with delayed-ACKs to add
~40 ms per round trip — a real bug this project hit during development and fixed.

Run it yourself: `./bin/test_gateway`.

### Spread Decomposition (1 hr simulated AAPL — deterministic)
```
590,168 orders → 209,905 trades

Metric                  Value (ticks)
─────────────────────────────────────
Quoted spread            1.06
Effective spread         0.77
Realized spread          0.83
Price impact            -0.06
Adverse selection %     -7.5%
```
The decomposition satisfies the Huang-Stoll identity `effective = realized + impact`
(0.77 = 0.83 + (−0.06)). The **negative** price impact is the economically correct
result for this market: agents are zero-intelligence, so order flow carries no
