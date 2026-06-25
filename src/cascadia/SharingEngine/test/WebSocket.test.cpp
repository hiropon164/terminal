// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
//   g++ -std=c++17 -Wall -Wextra WebSocket.cpp test/WebSocket.test.cpp -o wstest

#include "../WebSocket.h"

#include <cstdio>
#include <string>

using namespace pane_sharing::ws;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        ++g_checks;                                                     \
        if (!(cond))                                                    \
        {                                                               \
            ++g_failures;                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

static void test_accept_key_rfc_vector()
{
    // RFC 6455 section 1.3 canonical example.
    CHECK(computeAcceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

static void test_handshake_parse()
{
    const std::string req =
        "GET /chat HTTP/1.1\r\n"
        "Host: server.example.com\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";
    const auto key = handshakeKeyFromRequest(req);
    CHECK(key.has_value());
    CHECK(*key == "dGhlIHNhbXBsZSBub25jZQ==");

    const auto resp = buildHandshakeResponse(*key);
    CHECK(resp.find("101 Switching Protocols") != std::string::npos);
    CHECK(resp.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);

    // Not an upgrade -> rejected
    CHECK(!handshakeKeyFromRequest("GET / HTTP/1.1\r\nHost: x\r\n\r\n").has_value());
    // Our own client request round-trips through the server parser.
    std::string k;
    const auto creq = buildHandshakeRequest("host", "/share", k);
    const auto pk = handshakeKeyFromRequest(creq);
    CHECK(pk.has_value() && *pk == k);
}

static void test_unmasked_roundtrip()
{
    const std::string payload = std::string("\x01\x02binary\0data", 12);
    const auto wire = encodeMessage(FrameType::Binary, payload, /*mask*/ false);
    // server frames must NOT have the mask bit
    CHECK((static_cast<uint8_t>(wire[1]) & 0x80) == 0);

    Decoder d;
    d.feed(wire);
    const auto m = d.next();
    CHECK(m.has_value());
    CHECK(m->type == FrameType::Binary);
    CHECK(m->payload == payload);
    CHECK(!d.next().has_value());
}

static void test_masked_roundtrip()
{
    const std::string payload = "client->server must be masked";
    const auto wire = encodeMessage(FrameType::Binary, payload, /*mask*/ true, 0xDEADBEEF);
    CHECK((static_cast<uint8_t>(wire[1]) & 0x80) != 0); // mask bit set

    Decoder d;
    d.feed(wire);
    const auto m = d.next();
    CHECK(m.has_value());
    CHECK(m->payload == payload);
}

static void test_byte_by_byte_feed()
{
    const std::string payload(300, 'A'); // forces the 16-bit length form
    const auto wire = encodeMessage(FrameType::Binary, payload, false);
    CHECK(static_cast<uint8_t>(wire[1]) == 126); // extended length marker

    Decoder d;
    for (size_t i = 0; i < wire.size(); ++i)
    {
        d.feed(wire.substr(i, 1));
        if (i + 1 < wire.size())
        {
            CHECK(!d.next().has_value()); // incomplete until the last byte
        }
    }
    const auto m = d.next();
    CHECK(m.has_value());
    CHECK(m->payload == payload);
}

static void test_fragmentation()
{
    // Two data frames: opcode 0x2 FIN=0, then opcode 0x0 FIN=1.
    std::string f1;
    f1.push_back(static_cast<char>(0x02)); // FIN=0, binary
    f1.push_back(static_cast<char>(3));
    f1 += "abc";
    std::string f2;
    f2.push_back(static_cast<char>(0x80)); // FIN=1, continuation (opcode 0)
    f2.push_back(static_cast<char>(3));
    f2 += "def";

    Decoder d;
    d.feed(f1);
    CHECK(!d.next().has_value()); // not complete until FIN
    d.feed(f2);
    const auto m = d.next();
    CHECK(m.has_value());
    CHECK(m->payload == "abcdef");
    CHECK(!d.error());
}

static void test_control_frames()
{
    Decoder d;
    d.feed(encodeMessage(FrameType::Ping, "hi", false));
    d.feed(encodeMessage(FrameType::Close, "", false));
    const auto p = d.next();
    CHECK(p.has_value() && p->type == FrameType::Ping && p->payload == "hi");
    const auto c = d.next();
    CHECK(c.has_value() && c->type == FrameType::Close);
}

static void test_limits_and_errors()
{
    // Oversize message latches error (M6).
    Decoder d;
    d.setMaxMessage(8);
    d.feed(encodeMessage(FrameType::Binary, std::string(100, 'x'), false));
    CHECK(!d.next().has_value());
    CHECK(d.error());

    // Continuation without a started fragment is a protocol error.
    Decoder d2;
    std::string bad;
    bad.push_back(static_cast<char>(0x80)); // FIN=1, opcode 0 (continuation)
    bad.push_back(static_cast<char>(1));
    bad += "x";
    d2.feed(bad);
    CHECK(!d2.next().has_value());
    CHECK(d2.error());
}

int main()
{
    test_accept_key_rfc_vector();
    test_handshake_parse();
    test_unmasked_roundtrip();
    test_masked_roundtrip();
    test_byte_by_byte_feed();
    test_fragmentation();
    test_control_frames();
    test_limits_and_errors();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
