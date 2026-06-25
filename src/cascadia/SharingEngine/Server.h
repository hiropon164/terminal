// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// SharingServer: the WT-independent broadcast/authorization core. It is driven
// by a transport (WebSocket over TCP, supplied separately) through plain
// callbacks, so it can be unit-tested with a fake transport. It owns the
// security-critical policy:
//   M2 - read-only is enforced here (INPUT from a non-write client is dropped).
//   M3 - nothing (SNAPSHOT/OUTPUT/...) is sent before the HELLO token verifies.
//   M4 - if write is disallowed (e.g. elevated pane) all clients are read-only.
//   M6 - max clients / max frame length are enforced.

#pragma once

#include "Protocol.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace pane_sharing
{
    using ClientId = uint64_t;

    struct ServerConfig
    {
        std::string token; // required shared secret; an empty token rejects everyone
        bool allowWrite = false; // master switch; false => every client is read-only (M4)
        size_t maxClients = 8; // M6
        size_t maxFrameLen = 8 * 1024 * 1024; // M6
    };

    // The host (WT side) wires these up. SharingServer never touches sockets or
    // the terminal directly.
    struct ServerCallbacks
    {
        // Produce the current screen as VT bytes (host serializes its buffer).
        std::function<Bytes()> makeSnapshot;
        // Deliver remote input into the shared pane (only called for write clients).
        std::function<void(const Bytes& vt)> onRemoteInput;
        // Send one already-encoded protocol frame to a specific client.
        std::function<void(ClientId, const Bytes& frame)> send;
        // Drop a client (after a rejection, or on policy violation).
        std::function<void(ClientId)> closeClient;
    };

    class SharingServer
    {
    public:
        SharingServer(ServerConfig cfg, ServerCallbacks cb);

        // --- transport -> server ---
        void onClientConnected(ClientId id);
        void onClientFrame(ClientId id, const Bytes& frame);
        void onClientDisconnected(ClientId id);

        // --- host -> server (fan out to authenticated clients only) ---
        void pushOutput(const Bytes& vt);
        void pushResize(uint32_t rows, uint32_t cols);
        void pushState(const std::string& state);

        size_t clientCount() const { return _clients.size(); }
        size_t authedCount() const;

    private:
        struct Client
        {
            bool authed = false;
            bool canWrite = false;
        };

        void _reject(ClientId id, const std::string& reason);

        ServerConfig _cfg;
        ServerCallbacks _cb;
        std::map<ClientId, Client> _clients;
    };
}
