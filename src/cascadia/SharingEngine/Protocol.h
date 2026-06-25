// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// SharingEngine wire protocol (see windows-terminal-pane-sharing-spec_2.md ch.4).
//
// This file is deliberately FREE of any Windows Terminal / WinRT / Windows
// dependency: it is plain standard C++ so it can be built and unit-tested
// without the Terminal solution. Bytes are carried in std::string (it may hold
// arbitrary bytes, including embedded NULs); text payloads are UTF-8.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace pane_sharing
{
    // Bumped on incompatible wire changes; exchanged in HELLO.
    inline constexpr uint32_t ProtocolVersion = 1;

    // One opcode byte prefixes every frame (= one WebSocket binary message).
    enum class Opcode : uint8_t
    {
        // server -> client
        Snapshot = 0x01, // uint32 rows + uint32 cols + uint32 len + VT(UTF-8)
        Output = 0x02, // VT(UTF-8)
        Resize = 0x03, // {rows,cols} JSON
        State = 0x04, // {state} JSON
        // client -> server
        Hello = 0x10, // {protocolVersion,token,readOnly,viewportRows,viewportCols} JSON
        Input = 0x11, // VT(UTF-8)
        Ping = 0x12, // empty (keepalive; WS-level ping may substitute)
    };

    using Bytes = std::string;

    struct HelloMsg
    {
        uint32_t protocolVersion = 0;
        std::string token;
        bool readOnly = true;
        uint32_t viewportRows = 0;
        uint32_t viewportCols = 0;
    };

    struct SnapshotMsg
    {
        uint32_t rows = 0;
        uint32_t cols = 0;
        Bytes vt; // UTF-8 VT representation of the current screen
    };

    struct ResizeMsg
    {
        uint32_t rows = 0;
        uint32_t cols = 0;
    };

    struct StateMsg
    {
        std::string state; // e.g. "connected", "closed", or a rejection reason
    };

    // --- Encoders: build the on-the-wire frame (opcode byte + payload) ---
    Bytes encodeSnapshot(const SnapshotMsg& m);
    Bytes encodeOutput(const Bytes& vt);
    Bytes encodeResize(uint32_t rows, uint32_t cols);
    Bytes encodeState(const std::string& state);
    Bytes encodeHello(const HelloMsg& m);
    Bytes encodeInput(const Bytes& vt);
    Bytes encodePing();

    // --- Frame parsing ---
    struct Frame
    {
        Opcode op;
        Bytes payload; // everything after the opcode byte
    };

    // Parse the opcode envelope. Rejects (returns nullopt) an empty frame, an
    // unknown opcode, or a frame larger than maxLen (0 = unlimited). This is the
    // first line of the M6 "frame validation / size limit" defence.
    std::optional<Frame> parseFrame(const Bytes& wire, size_t maxLen = 0);

    // --- Typed payload parsers (call after parseFrame) ---
    std::optional<SnapshotMsg> parseSnapshot(const Bytes& payload);
    std::optional<ResizeMsg> parseResize(const Bytes& payload);
    std::optional<StateMsg> parseState(const Bytes& payload);
    std::optional<HelloMsg> parseHello(const Bytes& payload);
    // Output / Input payloads are the raw VT bytes (= the payload itself).
}
