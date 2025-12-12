#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <new>
#include <array>
#include <optional>

namespace micro_exchange::md {

/**
 * SPSCRingBuffer — Single-Producer Single-Consumer lock-free ring buffer.
 *
 * Design rationale:
 * ─────────────────
 * The market data pipeline has a natural producer-consumer topology:
 *
 *   [Matching Engine Thread] → buffer → [Feed Publisher Thread]
 *
 * An SPSC ring buffer is the optimal primitive here because:
 *
 *   1. No locks: producer and consumer never contend
 *   2. No CAS loops: only relaxed/acquire/release atomics needed
 *   3. Bounded memory: no dynamic allocation after construction
 *   4. Cache-friendly: sequential access pattern
 *   5. Wait-free: both push and pop complete in bounded steps
 *
 * The classic Lamport formulation with two cache-line-separated indices:
 *   • write_pos_: only modified by producer, read by consumer
 *   • read_pos_:  only modified by consumer, read by producer
 *
 * False sharing prevention: positions are on separate cache lines.
 *
 * Capacity must be a power of 2 for efficient modular arithmetic (mask).
 */
template <typename T, size_t Capacity>
class SPSCRingBuffer {
