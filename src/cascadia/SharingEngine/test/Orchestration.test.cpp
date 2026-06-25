// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
//   g++ -std=c++17 -Wall -Wextra Protocol.cpp Server.cpp Client.cpp test/Orchestration.test.cpp -o otest

#include "../Client.h"
#include "../Protocol.h"
#include "../Server.h"

#include <cstdio>
#include <map>
#include <set>
#include <vector>

using namespace pane_sharing;

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

// A fake server transport that just records what would go on the wire.
struct FakeServerTransport
{
    std::map<ClientId, std::vector<Bytes>> sent;
    std::set<ClientId> closed;
    std::vector<Bytes> remoteInput;
    Bytes snapshotVt = "SNAPSHOT-VT";

    ServerCallbacks callbacks()
    {
        ServerCallbacks cb;
        cb.makeSnapshot = [this]() { return snapshotVt; };
        cb.onRemoteInput = [this](const Bytes& vt) { remoteInput.push_back(vt); };
        cb.send = [this](ClientId id, const Bytes& f) { sent[id].push_back(f); };
        cb.closeClient = [this](ClientId id) { closed.insert(id); };
        return cb;
    }

    // last frame opcode sent to a client (or 0)
    int lastOp(ClientId id) const
    {
        const auto it = sent.find(id);
        if (it == sent.end() || it->second.empty())
        {
            return 0;
        }
        return static_cast<uint8_t>(it->second.back()[0]);
    }
    size_t count(ClientId id) const
    {
        const auto it = sent.find(id);
        return it == sent.end() ? 0 : it->second.size();
    }
};

static Bytes hello(const std::string& token, bool readOnly, uint32_t ver = ProtocolVersion)
{
    HelloMsg h;
    h.protocolVersion = ver;
    h.token = token;
    h.readOnly = readOnly;
    return encodeHello(h);
}

static void test_m3_no_disclosure_before_auth()
{
    FakeServerTransport t;
    SharingServer s({ "secret", true, 8, 1 << 20 }, t.callbacks());

    s.onClientConnected(1);
    CHECK(t.count(1) == 0); // nothing sent before HELLO (M3)

    // A non-HELLO frame before auth is rejected.
    s.onClientFrame(1, encodeInput("rm -rf"));
    CHECK(t.closed.count(1) == 1);
    CHECK(t.remoteInput.empty());
}

static void test_m3_bad_token_rejected_no_snapshot()
{
    FakeServerTransport t;
    SharingServer s({ "secret", true, 8, 1 << 20 }, t.callbacks());
    s.onClientConnected(2);
    s.onClientFrame(2, hello("wrong", true));
    CHECK(t.closed.count(2) == 1);
    CHECK(t.lastOp(2) == static_cast<int>(Opcode::State)); // STATE reason, not SNAPSHOT
    // never a snapshot
    for (const auto& f : t.sent[2])
    {
        CHECK(static_cast<uint8_t>(f[0]) != static_cast<uint8_t>(Opcode::Snapshot));
    }
    CHECK(s.authedCount() == 0);
}

static void test_version_mismatch_rejected()
{
    FakeServerTransport t;
    SharingServer s({ "secret", true, 8, 1 << 20 }, t.callbacks());
    s.onClientConnected(3);
    s.onClientFrame(3, hello("secret", true, ProtocolVersion + 99));
    CHECK(t.closed.count(3) == 1);
    CHECK(s.authedCount() == 0);
}

static void test_auth_then_snapshot_and_output()
{
    FakeServerTransport t;
    SharingServer s({ "secret", true, 8, 1 << 20 }, t.callbacks());
    s.onClientConnected(4);
    s.onClientFrame(4, hello("secret", true));
    CHECK(s.authedCount() == 1);
    CHECK(t.lastOp(4) == static_cast<int>(Opcode::Snapshot));
    const auto snap = parseSnapshot(parseFrame(t.sent[4].back())->payload);
    CHECK(snap && snap->vt == t.snapshotVt);

    // pushOutput reaches the authed client.
    s.pushOutput("hello-world");
    CHECK(t.lastOp(4) == static_cast<int>(Opcode::Output));
    CHECK(parseFrame(t.sent[4].back())->payload == "hello-world");
}

static void test_m2_readonly_input_dropped_writeable_allowed()
{
    FakeServerTransport t;
    SharingServer s({ "secret", true, 8, 1 << 20 }, t.callbacks());

    // read-only client: INPUT must be dropped.
    s.onClientConnected(5);
    s.onClientFrame(5, hello("secret", /*readOnly*/ true));
    s.onClientFrame(5, encodeInput("evil"));
    CHECK(t.remoteInput.empty()); // M2

    // write client (server allows write): INPUT flows through.
    s.onClientConnected(6);
    s.onClientFrame(6, hello("secret", /*readOnly*/ false));
    s.onClientFrame(6, encodeInput("ls\r"));
    CHECK(t.remoteInput.size() == 1);
    CHECK(t.remoteInput[0] == "ls\r");
}

static void test_m4_server_disallows_write()
{
    FakeServerTransport t;
    SharingServer s({ "secret", /*allowWrite*/ false, 8, 1 << 20 }, t.callbacks());
    s.onClientConnected(7);
    s.onClientFrame(7, hello("secret", /*readOnly*/ false)); // client wants write...
    s.onClientFrame(7, encodeInput("danger"));
    CHECK(t.remoteInput.empty()); // ...but the server forces read-only (M4)
}

static void test_m6_max_clients()
{
    FakeServerTransport t;
    SharingServer s({ "secret", true, /*maxClients*/ 1, 1 << 20 }, t.callbacks());
    s.onClientConnected(8);
    s.onClientConnected(9); // over the cap
    CHECK(t.closed.count(9) == 1);
    CHECK(s.clientCount() == 1);
}

static void test_m6_bad_frame_dropped()
{
    FakeServerTransport t;
    SharingServer s({ "secret", true, 8, /*maxFrameLen*/ 16 }, t.callbacks());
    s.onClientConnected(10);
    s.onClientFrame(10, hello("secret", true));
    // oversized frame after auth
    s.onClientFrame(10, encodeOutput(std::string(1000, 'x')));
    CHECK(t.closed.count(10) == 1);
}

static void test_client_side()
{
    // Client emits HELLO on connect.
    std::vector<Bytes> clientSent;
    ClientCallbacks cb;
    cb.send = [&](const Bytes& f) { clientSent.push_back(f); };
    SnapshotMsg gotSnap;
    bool gotSnapFlag = false;
    Bytes gotOutput;
    uint32_t rrows = 0, rcols = 0;
    std::string gotState;
    cb.onSnapshot = [&](const SnapshotMsg& s) { gotSnap = s; gotSnapFlag = true; };
    cb.onOutput = [&](const Bytes& v) { gotOutput = v; };
    cb.onResize = [&](uint32_t r, uint32_t c) { rrows = r; rcols = c; };
    cb.onState = [&](const std::string& s) { gotState = s; };

    SharingClient c({ "tok", /*readOnly*/ true, 24, 80 }, cb);
    c.onConnected();
    CHECK(clientSent.size() == 1);
    const auto h = parseHello(parseFrame(clientSent[0])->payload);
    CHECK(h && h->token == "tok" && h->readOnly == true);

    // read-only client never sends INPUT.
    c.sendInput("x");
    CHECK(clientSent.size() == 1);

    // server frames are routed to callbacks.
    SnapshotMsg sm;
    sm.rows = 10;
    sm.cols = 20;
    sm.vt = "scr";
    c.onServerFrame(encodeSnapshot(sm));
    CHECK(gotSnapFlag && gotSnap.vt == "scr" && gotSnap.rows == 10);
    c.onServerFrame(encodeOutput("out"));
    CHECK(gotOutput == "out");
    c.onServerFrame(encodeResize(40, 100));
    CHECK(rrows == 40 && rcols == 100);
    c.onServerFrame(encodeState("closed"));
    CHECK(gotState == "closed");
}

static void test_end_to_end()
{
    // Wire a write-capable client straight into the server.
    FakeServerTransport t;
    SharingServer server({ "secret", true, 8, 1 << 20 }, t.callbacks());
    const ClientId cid = 100;
    server.onClientConnected(cid);

    ClientCallbacks cb;
    // client.send -> server.onClientFrame
    cb.send = [&](const Bytes& f) { server.onClientFrame(cid, f); };
    Bytes clientOutput;
    bool snapped = false;
    cb.onSnapshot = [&](const SnapshotMsg&) { snapped = true; };
    cb.onOutput = [&](const Bytes& v) { clientOutput += v; };

    SharingClient client({ "secret", /*readOnly*/ false, 24, 80 }, cb);

    // server.send(cid, ...) -> client.onServerFrame
    // (re-point the fake transport's send for this client to the client object)
    // We do it by polling t.sent after each step instead, to keep it simple:
    auto pump = [&]() {
        for (auto& f : t.sent[cid])
        {
            client.onServerFrame(f);
        }
        t.sent[cid].clear();
    };

    client.onConnected(); // sends HELLO -> server authenticates -> server queues SNAPSHOT
    pump();
    CHECK(snapped);

    server.pushOutput("LINE1\r\n");
    pump();
    CHECK(clientOutput == "LINE1\r\n");

    // write client input reaches the host
    client.sendInput("whoami\r");
    CHECK(t.remoteInput.size() == 1 && t.remoteInput[0] == "whoami\r");
}

int main()
{
    test_m3_no_disclosure_before_auth();
    test_m3_bad_token_rejected_no_snapshot();
    test_version_mismatch_rejected();
    test_auth_then_snapshot_and_output();
    test_m2_readonly_input_dropped_writeable_allowed();
    test_m4_server_disallows_write();
    test_m6_max_clients();
    test_m6_bad_frame_dropped();
    test_client_side();
    test_end_to_end();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
