// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Minimal RFC 6455 WebSocket framing + handshake, in plain standard C++ (no
// Windows / WinRT / Terminal dependency) so it can be unit-tested standalone.
// The socket I/O itself is provided by the caller (e.g. StreamSocketListener on
// the server, MessageWebSocket on the client); this layer only turns bytes into
// messages and back.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pane_sharing::ws
{
    // --- Handshake (server side) ---

    // base64(SHA1(clientKey + RFC6455 magic GUID)).
    std::string computeAcceptKey(const std::string& clientKey);

    // Extract the Sec-WebSocket-Key value from a client HTTP upgrade request.
    // Returns nullopt if it isn't a well-formed WebSocket upgrade.
    std::optional<std::string> handshakeKeyFromRequest(const std::string& httpRequest);

    // Full HTTP 101 response (ending with a blank line) for the given client key.
    std::string buildHandshakeResponse(const std::string& clientKey);

    // --- Handshake (client side, useful for tests / a future native client) ---
    // Returns the request text and fills outKey with the random key used.
    std::string buildHandshakeRequest(const std::string& host, const std::string& path, std::string& outKey);

    // --- Framing ---

    enum class FrameType
    {
        Text,
        Binary,
        Close,
        Ping,
        Pong,
    };

    // Encode one (unfragmented) message. Per RFC 6455, client->server frames MUST
    // be masked and server->client frames MUST NOT be. maskKey is only used when
    // mask==true (pass a non-zero value; tests use a fixed key for determinism).
    std::string encodeMessage(FrameType type, const std::string& payload, bool mask, uint32_t maskKey = 0x01020304u);

    struct Message
    {
        FrameType type;
        std::string payload;
    };

    // Incremental decoder: feed raw bytes, pull complete messages. Handles
    // fragmentation (continuation frames), masking, and the 7/16/64-bit length
    // forms. Control frames (Close/Ping/Pong) are surfaced as their own messages.
    // On any protocol violation or a message exceeding the size cap, error()
    // latches true (M6: a malformed peer can't drive unbounded work).
    class Decoder
    {
    public:
        void setMaxMessage(size_t bytes) { _maxMessage = bytes; }
        void feed(const std::string& bytes) { _buf += bytes; }

        // Returns the next complete message, or nullopt if more bytes are needed
        // (or a protocol error has latched).
        std::optional<Message> next();

        bool error() const { return _error; }

    private:
        std::string _buf;
        // reassembly of a fragmented data message
        bool _inFragment = false;
        FrameType _fragType = FrameType::Binary;
        std::string _fragData;
        size_t _maxMessage = 8 * 1024 * 1024;
        bool _error = false;
    };
}
