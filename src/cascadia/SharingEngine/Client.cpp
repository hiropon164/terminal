// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "Client.h"

namespace pane_sharing
{
    SharingClient::SharingClient(ClientConfig cfg, ClientCallbacks cb) :
        _cfg{ std::move(cfg) },
        _cb{ std::move(cb) }
    {
    }

    void SharingClient::onConnected()
    {
        if (!_cb.send)
        {
            return;
        }
        HelloMsg h;
        h.protocolVersion = ProtocolVersion;
        h.token = _cfg.token;
        h.readOnly = _cfg.readOnly;
        h.viewportRows = _cfg.viewportRows;
        h.viewportCols = _cfg.viewportCols;
        _cb.send(encodeHello(h));
    }

    void SharingClient::onServerFrame(const Bytes& frameBytes)
    {
        const auto frame = parseFrame(frameBytes);
        if (!frame)
        {
            return;
        }
        switch (frame->op)
        {
        case Opcode::Snapshot:
            if (const auto s = parseSnapshot(frame->payload); s && _cb.onSnapshot)
            {
                _cb.onSnapshot(*s);
            }
            break;
        case Opcode::Output:
            if (_cb.onOutput)
            {
                _cb.onOutput(frame->payload);
            }
            break;
        case Opcode::Resize:
            if (const auto r = parseResize(frame->payload); r && _cb.onResize)
            {
                _cb.onResize(r->rows, r->cols);
            }
            break;
        case Opcode::State:
            if (const auto st = parseState(frame->payload); st && _cb.onState)
            {
                _cb.onState(st->state);
            }
            break;
        default:
            // client->server opcodes echoed back, or anything unexpected: ignore.
            break;
        }
    }

    void SharingClient::sendInput(const Bytes& vt)
    {
        if (_cfg.readOnly || !_cb.send)
        {
            return;
        }
        _cb.send(encodeInput(vt));
    }
}
