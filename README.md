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
