# Changelog

## v1.5.0 (2026-05-30)

### Performance
- **Capture the event timestamp once per order instead of once per fill.** Both
  `OrderBook` and `ArrayOrderBook` were calling `steady_clock::now()` (~17 ns)
  for the order's entry plus every trade print and both sides of every fill —
  ~4 clock reads per order, which profiling put at ~40% of hot-path time. The
  engine now reads the clock once per inbound event and threads it through
  matching via `Order::fill(qty, ts)` / `Order::cancel(ts)`. Measured on x86
  (1M orders): `std::map` 5.9M→7.6M orders/sec (**+28%**), `ArrayOrderBook`
  6.7M→8.8M (**+30%**). Trade streams are unchanged — the `orderbook_equivalence`
  and `gateway_e2e` CI gates still pass.

### Docs
- README architecture diagram now shows the TCP gateway and the two book
  backends feeding the matching engine.

## v1.4.0 (2026-05-30)

### New features
- **TCP order-entry gateway** (`net/OrderGateway.h`) — a single-threaded POSIX
  socket server that accepts a client, decodes a binary order-entry protocol,
  feeds the `MatchingEngine`, and streams `Exec`/`Ack` reports back. Binds to
  loopback; port 0 selects an ephemeral port.
- **Binary order-entry wire protocol** (`net/OrderEntryProtocol.h`) — 8-byte
  length-prefixed framing; `NewOrder`/`Cancel` inbound, `Exec`/`Ack` outbound;
  naturally-aligned structs (UBSan-clean) with partial-read-safe `read_full` /
  `write_full` helpers.
- **End-to-end gateway test** (`net/tests/test_gateway.cpp`) — starts the server
  on a thread, streams 3,000 orders over a loopback socket, and asserts the
  networked path's trades and volume match the in-process engine exactly.
  Registered as the CTest gate `gateway_e2e`.

### Fixes
- Both gateway endpoints set `TCP_NODELAY`. A lock-step small-message protocol
  with Nagle's algorithm enabled collided with delayed-ACKs for ~40 ms per round
  trip; disabling Nagle removed it. (Surfaced as a hang in the gateway test.)
- Silenced a Release-only `-Wunused-but-set-variable` warning in
  `test_invariants.cpp` (the variable is only read inside an `assert`, which
  `-DNDEBUG` compiles out).

### Notes
- The gateway serves one client at a time (the standard single-gateway model);
  multiplexing many clients via epoll/kqueue is future work.

## v1.3.0 (2026-05-30)

### New features
- **`ArrayOrderBook` — a tick-indexed array order book** as a drop-in
  alternative to the `std::map`-based `OrderBook`. Levels live in one
  contiguous array indexed by `price - min_price`; an **occupied-level bitmap**
  with `__builtin_ctzll` / `__builtin_clzll` advances the best-bid/ask cursors
  by word-skipping instead of a linear scan. Reuses the same `Order`,
  `PriceLevel`, and `ArenaAllocator`, so it is a clean A/B against the tree.
- **`bench_orderbook_compare`** — runs an identical order stream through both
  books, asserts a **byte-identical trade stream**, then reports throughput and
  latency. Registered as the CTest gate `orderbook_equivalence`.

### Results (1M orders, ~9900–10100 band)
- Trade streams identical (176,732 trades) — CI-gated.
- Median per-order latency **84 ns (array) vs 125 ns (std::map)**, ~33% lower;
  P99 334 vs 375 ns. Throughput ~6.0M orders/sec for both — the per-order cost
  is dominated by the `OrderId→Order*` hash insert and the `now()` timestamp,
  not the level container.
- Characterised the band-width tradeoff: a *naive* linear best-bid/ask scan is
  ~25× slower than `std::map` on a sparse 200k-level band; the bitmap index
  removes that cliff and keeps the array ≥1.0× through realistic widths.

