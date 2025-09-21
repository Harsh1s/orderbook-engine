/**
 * test_gateway.cpp — end-to-end loopback test for the TCP order-entry gateway.
 *
 * Spins up an OrderGateway on an ephemeral port in a server thread, connects a
 * client over 127.0.0.1, and streams a deterministic order flow through the
 * wire protocol. It then runs the SAME orders through an in-process
 * MatchingEngine reference and asserts the networked path produced an identical
 * number of executions and identical traded volume — i.e. serialising orders
 * over TCP and parsing them back changes nothing about the matching outcome.
 *
 * Doubles as a usage demo for the protocol and as a CTest gate.
 */

#include "OrderGateway.h"
#include "OrderEntryProtocol.h"
#include "MatchingEngine.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <thread>
#include <vector>
#include <random>
#include <iostream>
#include <cstring>

using namespace micro_exchange;
using namespace micro_exchange::core;
using namespace micro_exchange::net;

static std::vector<NewOrderRequest> make_flow(size_t n, const char* sym) {
    std::mt19937_64 rng(7);
    std::uniform_int_distribution<Price>    price(95, 105);
    std::uniform_int_distribution<Quantity> qty(1, 5);
    std::uniform_int_distribution<int>      side(0, 1);
    std::uniform_real_distribution<double>  type(0.0, 1.0);

    std::vector<NewOrderRequest> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        NewOrderRequest r{};
        r.id   = i + 1;
        r.side = side(rng) ? Side::Buy : Side::Sell;
        if (type(rng) < 0.65) {
            r.type  = OrderType::Limit;
            r.tif   = TimeInForce::GTC;
            r.price = price(rng);
        } else {
            r.type  = OrderType::Market;
            r.tif   = TimeInForce::IOC;
