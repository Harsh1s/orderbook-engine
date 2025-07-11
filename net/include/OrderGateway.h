#pragma once

// ─────────────────────────────────────────────────────────────────────────
// OrderGateway — a single-threaded TCP order-entry gateway.
//
// This is the network front-end a real exchange puts in front of its matching
// engine: clients open a socket, stream binary order messages in, and receive
// execution reports + acknowledgements back. It deliberately mirrors the
// standard exchange model — one sequential gateway feeding a single-threaded
// matching core, which keeps the hot path lock-free and the event order
// deterministic.
//
// Design notes:
//   • Binds to 127.0.0.1 only (a demo gateway should never be world-reachable).
//   • Port 0 lets the OS pick a free port — handy for tests; read it via port().
//   • Trade prints are pushed to the connected client synchronously from the
//     matching callback, so Exec messages for a NewOrder arrive before its Ack.
//   • SIGPIPE is ignored so a client disconnect mid-write can't kill the server.
//
// Scope: one client at a time, processed sequentially (the common single-
// gateway model). Multiplexing many clients (epoll/kqueue) is a natural
// extension and is noted as future work.
// ─────────────────────────────────────────────────────────────────────────

#include "MatchingEngine.h"
#include "OrderEntryProtocol.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace micro_exchange::net {

using namespace micro_exchange::core;

class OrderGateway {
public:
    OrderGateway(uint16_t port, const std::string& symbol)
        : symbol_(symbol)
    {
        std::signal(SIGPIPE, SIG_IGN);   // a dead client must not kill us

        engine_.add_symbol(symbol_);

        // Stream every trade print to the currently-connected client.
        engine_.set_trade_callback([this](const Trade& t) {
            if (client_fd_ < 0) return;
            WireExec e{};
            e.buy_order_id  = t.buy_order_id;
            e.sell_order_id = t.sell_order_id;
            e.price         = t.price;
            e.quantity      = t.quantity;
            e.aggressor     = static_cast<uint8_t>(t.aggressor);
            send_msg(client_fd_, MsgType::Exec, &e, sizeof(e));
            ++execs_sent_;
        });
