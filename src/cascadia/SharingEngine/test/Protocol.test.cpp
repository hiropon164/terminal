// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Standalone unit test for the SharingEngine protocol layer. Builds WITHOUT the
// Windows Terminal solution (plain C++): e.g.
//   g++ -std=c++17 -Wall -Wextra Protocol.cpp test/Protocol.test.cpp -o ptest

#include "../Protocol.h"

#include <cstdio>
#include <string>

using namespace pane_sharing;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                       \
    do                                                                    \
    {                                                                     \
        ++g_checks;                                                       \
        if (!(cond))                                                      \
        {                                                                 \
            ++g_failures;                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

static void test_output_roundtrip()
{
    // OUTPUT carries arbitrary bytes verbatim, including bytes that look like
    // other opcodes and embedded NULs.
    Bytes vt;
    vt.push_back('\x1b');
    vt += "[31mhi";
    vt.push_back('\0');
    vt.push_back(static_cast<char>(0x10)); // looks like HELLO opcode but is data
    vt += "world";

    const auto wire = encodeOutput(vt);
    const auto f = parseFrame(wire);
    CHECK(f.has_value());
    CHECK(f->op == Opcode::Output);
    CHECK(f->payload == vt);
}

static void test_input_roundtrip()
{
    const Bytes vt = "ls -la\r";
    const auto f = parseFrame(encodeInput(vt));
    CHECK(f.has_value());
    CHECK(f->op == Opcode::Input);
    CHECK(f->payload == vt);
}

static void test_snapshot_roundtrip()
{
    SnapshotMsg m;
    m.rows = 30;
    m.cols = 120;
    m.vt.push_back('\x1b');
    m.vt += "[2J";
    m.vt.push_back('\0'); // ensure binary-safety of the length-prefixed payload
    m.vt += "screen";

    const auto wire = encodeSnapshot(m);
    const auto f = parseFrame(wire);
    CHECK(f.has_value());
    CHECK(f->op == Opcode::Snapshot);
    const auto s = parseSnapshot(f->payload);
    CHECK(s.has_value());
    CHECK(s->rows == 30);
    CHECK(s->cols == 120);
    CHECK(s->vt == m.vt);
}

static void test_resize_roundtrip()
{
    const auto f = parseFrame(encodeResize(40, 100));
    CHECK(f.has_value());
    CHECK(f->op == Opcode::Resize);
    const auto r = parseResize(f->payload);
    CHECK(r.has_value());
    CHECK(r->rows == 40);
    CHECK(r->cols == 100);
}

static void test_state_roundtrip()
{
    const auto f = parseFrame(encodeState("rejected: bad token"));
    CHECK(f.has_value());
    CHECK(f->op == Opcode::State);
    const auto s = parseState(f->payload);
    CHECK(s.has_value());
    CHECK(s->state == "rejected: bad token");
}

static void test_hello_roundtrip()
{
    HelloMsg h;
    h.protocolVersion = ProtocolVersion;
    h.token = "s3cr\"et\\token"; // contains chars that must be JSON-escaped
    h.readOnly = false;
    h.viewportRows = 50;
    h.viewportCols = 200;

    const auto f = parseFrame(encodeHello(h));
    CHECK(f.has_value());
    CHECK(f->op == Opcode::Hello);
    const auto p = parseHello(f->payload);
    CHECK(p.has_value());
    CHECK(p->protocolVersion == ProtocolVersion);
    CHECK(p->token == h.token);
    CHECK(p->readOnly == false);
    CHECK(p->viewportRows == 50);
    CHECK(p->viewportCols == 200);
}

static void test_hello_from_browser_like_json()
{
    // A third-party (e.g. browser/xterm.js) client may send differently-ordered,
    // whitespace-laden JSON. The opcode byte (0x10) + the JSON object.
    Bytes wire;
    wire.push_back(static_cast<char>(Opcode::Hello));
    wire += R"(  { "token" : "abc" , "readOnly": true ,
                   "protocolVersion": 1, "extraIgnored": {"a":[1,2]} } )";
    const auto f = parseFrame(wire);
    CHECK(f.has_value());
    const auto p = parseHello(f->payload);
    CHECK(p.has_value());
    CHECK(p->token == "abc");
    CHECK(p->readOnly == true);
    CHECK(p->protocolVersion == 1);
}

static void test_ping()
{
    const auto f = parseFrame(encodePing());
    CHECK(f.has_value());
    CHECK(f->op == Opcode::Ping);
    CHECK(f->payload.empty());
}

static void test_frame_validation()
{
    // empty frame rejected
    CHECK(!parseFrame(Bytes{}).has_value());

    // unknown opcode rejected (M6)
    Bytes bad;
    bad.push_back('\x7f');
    bad += "junk";
    CHECK(!parseFrame(bad).has_value());

    // oversize frame rejected when a limit is given (M6)
    const auto big = encodeOutput(std::string(100, 'x'));
    CHECK(parseFrame(big, 1000).has_value());
    CHECK(!parseFrame(big, 10).has_value());
}

static void test_malformed_payloads()
{
    // Truncated snapshot (header claims more bytes than present)
    Bytes p;
    // rows, cols, len = 9999 but no body
    for (uint32_t v : { 10u, 10u, 9999u })
    {
        p.push_back(static_cast<char>(v & 0xFF));
        p.push_back(static_cast<char>((v >> 8) & 0xFF));
        p.push_back(static_cast<char>((v >> 16) & 0xFF));
        p.push_back(static_cast<char>((v >> 24) & 0xFF));
    }
    CHECK(!parseSnapshot(p).has_value());

    // Bad JSON in resize
    CHECK(!parseResize("{not json").has_value());
    // Resize missing a field
    CHECK(!parseResize("{\"rows\":10}").has_value());
    // Hello missing the (required) token
    CHECK(!parseHello("{\"protocolVersion\":1}").has_value());
    // Hello with negative protocolVersion (not a valid uint)
    CHECK(!parseHello("{\"protocolVersion\":-1,\"token\":\"x\"}").has_value());
}

int main()
{
    test_output_roundtrip();
    test_input_roundtrip();
    test_snapshot_roundtrip();
    test_resize_roundtrip();
    test_state_roundtrip();
    test_hello_roundtrip();
    test_hello_from_browser_like_json();
    test_ping();
    test_frame_validation();
    test_malformed_payloads();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
