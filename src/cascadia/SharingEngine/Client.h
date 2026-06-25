// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// SharingClient: the observer-side core. Transport-agnostic (driven by
// callbacks) so it unit-tests without sockets. It sends HELLO on connect,
// routes inbound server frames to the host, and forwards local input.

#pragma once

#include "Protocol.h"

#include <cstdint>
#include <functional>
#include <string>

namespace pane_sharing
{
    struct ClientConfig
    {
        std::string token;
        bool readOnly = true;
        uint32_t viewportRows = 0;
        uint32_t viewportCols = 0;
    };

    struct ClientCallbacks
    {
        std::function<void(const SnapshotMsg&)> onSnapshot;
        std::function<void(const Bytes& vt)> onOutput;
        std::function<void(uint32_t rows, uint32_t cols)> onResize;
        std::function<void(const std::string& state)> onState;
        // Send one already-encoded protocol frame to the server.
        std::function<void(const Bytes& frame)> send;
    };

    class SharingClient
    {
    public:
        SharingClient(ClientConfig cfg, ClientCallbacks cb);

        // Transport reports the connection is open -> we send HELLO.
        void onConnected();
        // A complete protocol frame arrived from the server.
        void onServerFrame(const Bytes& frame);

        // Host (local TermControl) -> server. No-op when read-only (the server
        // enforces this too; this is just client-side self-restraint).
        void sendInput(const Bytes& vt);

    private:
        ClientConfig _cfg;
        ClientCallbacks _cb;
    };
}
