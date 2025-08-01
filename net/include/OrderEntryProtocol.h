#pragma once

// ─────────────────────────────────────────────────────────────────────────
// OrderEntryProtocol — a compact binary wire protocol for order entry.
//
// Framing: every message is an 8-byte header followed by a fixed-size payload.
//
//     ┌────────────────── WireHeader (8 bytes) ──────────────────┐
//     │ type : u8 │ pad : u8[3] │ len : u32  (payload byte count) │
//     └───────────────────────────────────────────────────────────┘
//     ┌─────────────────────── payload (`len` bytes) ────────────┐
//
// Inbound (client → gateway):  NewOrder, Cancel
// Outbound (gateway → client): Exec (one per fill), Ack (one per request)
//
// A NewOrder elicits zero or more Exec messages (in match order) followed by
// exactly one Ack, so the client always knows when a request is complete.
//
// Endianness: this demo assumes a homogeneous little-endian deployment (x86-64
// / ARM64 on both ends) and copies POD structs on the wire. A production
// protocol would byte-order-normalise the header length and numeric fields
// (e.g. htole64); that is deliberately out of scope here and noted in the README.
// ─────────────────────────────────────────────────────────────────────────

#include "Order.h"          // micro_exchange::core types (Price, Quantity, ...)

#include <cstdint>
#include <cstring>
#include <unistd.h>

namespace micro_exchange::net {

using core::Price;
using core::Quantity;
using core::OrderId;

enum class MsgType : uint8_t {
    NewOrder = 1,
    Cancel   = 2,
    Ack      = 10,
    Exec     = 11,
    Reject   = 12,
};

enum class AckStatus : uint8_t {
