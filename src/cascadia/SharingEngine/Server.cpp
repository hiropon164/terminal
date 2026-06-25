// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "Server.h"

namespace pane_sharing
{
    SharingServer::SharingServer(ServerConfig cfg, ServerCallbacks cb) :
        _cfg{ std::move(cfg) },
        _cb{ std::move(cb) }
    {
    }

    size_t SharingServer::authedCount() const
    {
        size_t n = 0;
        for (const auto& [id, c] : _clients)
        {
            if (c.authed)
            {
                ++n;
            }
        }
        return n;
    }

    void SharingServer::_reject(ClientId id, const std::string& reason)
    {
        // M3: a rejected (unauthenticated) client gets only a STATE reason, never
        // any screen content, and is then dropped.
        if (_cb.send)
        {
            _cb.send(id, encodeState(reason));
        }
        _clients.erase(id);
        if (_cb.closeClient)
        {
            _cb.closeClient(id);
        }
    }

    void SharingServer::onClientConnected(ClientId id)
    {
        // M6: cap concurrent clients. Reject (without disclosing anything) when full.
        if (_clients.size() >= _cfg.maxClients)
        {
            if (_cb.send)
            {
                _cb.send(id, encodeState("server busy"));
            }
            if (_cb.closeClient)
            {
                _cb.closeClient(id);
            }
            return;
        }
        // Registered but NOT authenticated. We send nothing until a valid HELLO.
        _clients[id] = Client{};
    }

    void SharingServer::onClientFrame(ClientId id, const Bytes& frameBytes)
    {
        const auto it = _clients.find(id);
        if (it == _clients.end())
        {
            return; // unknown / already-dropped client
        }
        Client& client = it->second;

        const auto frame = parseFrame(frameBytes, _cfg.maxFrameLen);
        if (!frame)
        {
            _reject(id, "bad frame"); // M6: malformed/oversized => drop
            return;
        }

        if (!client.authed)
        {
            // Before authentication, the ONLY acceptable frame is HELLO (M3).
            if (frame->op != Opcode::Hello)
            {
                _reject(id, "expected hello");
                return;
            }
            const auto hello = parseHello(frame->payload);
            if (!hello)
            {
                _reject(id, "malformed hello");
                return;
            }
            if (hello->protocolVersion != ProtocolVersion)
            {
                _reject(id, "protocol version mismatch");
                return;
            }
            // Token check (M3). Empty configured token rejects everyone.
            if (_cfg.token.empty() || hello->token != _cfg.token)
            {
                _reject(id, "unauthorized");
                return;
            }

            client.authed = true;
            // Write is granted only if the client asked for it AND the server
            // allows writing at all (M4). Otherwise the client is read-only (M2).
            client.canWrite = _cfg.allowWrite && !hello->readOnly;

            // M3: now that auth succeeded, send the initial screen so a late
            // joiner sees the current state immediately.
            if (_cb.send && _cb.makeSnapshot)
            {
                SnapshotMsg snap;
                snap.vt = _cb.makeSnapshot();
                _cb.send(id, encodeSnapshot(snap));
            }
            return;
        }

        // Authenticated client.
        switch (frame->op)
        {
        case Opcode::Input:
            // M2: only clients that hold write permission may inject input. A
            // read-only client's INPUT is silently dropped here on the server,
            // regardless of what the client claims.
            if (client.canWrite && _cb.onRemoteInput)
            {
                _cb.onRemoteInput(frame->payload);
            }
            break;
        case Opcode::Ping:
            // Keepalive; nothing to do at this layer (WS-level pong handled by
            // the transport).
            break;
        default:
            // Server->client opcodes (or anything unexpected) from a client are
            // not allowed.
            _reject(id, "unexpected frame");
            break;
        }
    }

    void SharingServer::onClientDisconnected(ClientId id)
    {
        _clients.erase(id);
    }

    void SharingServer::pushOutput(const Bytes& vt)
    {
        if (!_cb.send)
        {
            return;
        }
        const auto frame = encodeOutput(vt);
        for (const auto& [id, c] : _clients)
        {
            if (c.authed) // M3: never to an unauthenticated client
            {
                _cb.send(id, frame);
            }
        }
    }

    void SharingServer::pushResize(uint32_t rows, uint32_t cols)
    {
        if (!_cb.send)
        {
            return;
        }
        const auto frame = encodeResize(rows, cols);
        for (const auto& [id, c] : _clients)
        {
            if (c.authed)
            {
                _cb.send(id, frame);
            }
        }
    }

    void SharingServer::pushState(const std::string& state)
    {
        if (!_cb.send)
        {
            return;
        }
        const auto frame = encodeState(state);
        for (const auto& [id, c] : _clients)
        {
            if (c.authed)
            {
                _cb.send(id, frame);
            }
        }
    }
}
