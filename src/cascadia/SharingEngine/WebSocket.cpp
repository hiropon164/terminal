// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "WebSocket.h"

#include <array>
#include <cstring>

namespace pane_sharing::ws
{
    namespace
    {
        // --- SHA1 (for the handshake accept key) ---
        struct Sha1
        {
            uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
            uint64_t len = 0;
            uint8_t block[64];
            size_t blockLen = 0;

            static uint32_t rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

            void processBlock()
            {
                uint32_t w[80];
                for (int i = 0; i < 16; ++i)
                {
                    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                           (static_cast<uint32_t>(block[i * 4 + 3]));
                }
                for (int i = 16; i < 80; ++i)
                {
                    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
                }
                uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
                for (int i = 0; i < 80; ++i)
                {
                    uint32_t f, k;
                    if (i < 20)
                    {
                        f = (b & c) | ((~b) & d);
                        k = 0x5A827999u;
                    }
                    else if (i < 40)
                    {
                        f = b ^ c ^ d;
                        k = 0x6ED9EBA1u;
                    }
                    else if (i < 60)
                    {
                        f = (b & c) | (b & d) | (c & d);
                        k = 0x8F1BBCDCu;
                    }
                    else
                    {
                        f = b ^ c ^ d;
                        k = 0xCA62C1D6u;
                    }
                    const uint32_t tmp = rol(a, 5) + f + e + k + w[i];
                    e = d;
                    d = c;
                    c = rol(b, 30);
                    b = a;
                    a = tmp;
                }
                h[0] += a;
                h[1] += b;
                h[2] += c;
                h[3] += d;
                h[4] += e;
            }

            void update(const uint8_t* data, size_t n)
            {
                len += n;
                while (n > 0)
                {
                    const size_t take = std::min<size_t>(n, 64 - blockLen);
                    std::memcpy(block + blockLen, data, take);
                    blockLen += take;
                    data += take;
                    n -= take;
                    if (blockLen == 64)
                    {
                        processBlock();
                        blockLen = 0;
                    }
                }
            }

            void finish(uint8_t out[20])
            {
                const uint64_t bitLen = len * 8;
                uint8_t pad = 0x80;
                update(&pad, 1);
                pad = 0x00;
                while (blockLen != 56)
                {
                    update(&pad, 1);
                }
                uint8_t lenBytes[8];
                for (int i = 0; i < 8; ++i)
                {
                    lenBytes[7 - i] = static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFF);
                }
                update(lenBytes, 8);
                for (int i = 0; i < 5; ++i)
                {
                    out[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
                    out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
                    out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
                    out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
                }
            }
        };

        std::string base64(const uint8_t* data, size_t n)
        {
            static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve(((n + 2) / 3) * 4);
            size_t i = 0;
            for (; i + 3 <= n; i += 3)
            {
                const uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
                out.push_back(tbl[(v >> 18) & 0x3F]);
                out.push_back(tbl[(v >> 12) & 0x3F]);
                out.push_back(tbl[(v >> 6) & 0x3F]);
                out.push_back(tbl[v & 0x3F]);
            }
            if (n - i == 1)
            {
                const uint32_t v = data[i] << 16;
                out.push_back(tbl[(v >> 18) & 0x3F]);
                out.push_back(tbl[(v >> 12) & 0x3F]);
                out += "==";
            }
            else if (n - i == 2)
            {
                const uint32_t v = (data[i] << 16) | (data[i + 1] << 8);
                out.push_back(tbl[(v >> 18) & 0x3F]);
                out.push_back(tbl[(v >> 12) & 0x3F]);
                out.push_back(tbl[(v >> 6) & 0x3F]);
                out.push_back('=');
            }
            return out;
        }

        std::string asciiLower(std::string s)
        {
            for (auto& c : s)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return s;
        }

        std::string trim(const std::string& s)
        {
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            if (a == std::string::npos)
            {
                return {};
            }
            return s.substr(a, b - a + 1);
        }
    } // namespace

    std::string computeAcceptKey(const std::string& clientKey)
    {
        static const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string s = clientKey + magic;
        Sha1 sha;
        sha.update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
        uint8_t digest[20];
        sha.finish(digest);
        return base64(digest, 20);
    }

    std::optional<std::string> handshakeKeyFromRequest(const std::string& req)
    {
        // Must look like an HTTP GET upgrade with the required headers.
        bool hasUpgrade = false;
        bool hasConnectionUpgrade = false;
        std::string key;

        size_t pos = 0;
        bool firstLine = true;
        while (pos < req.size())
        {
            size_t eol = req.find("\r\n", pos);
            const std::string line = req.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            if (eol == std::string::npos)
            {
                break;
            }
            pos = eol + 2;
            if (firstLine)
            {
                firstLine = false;
                if (line.compare(0, 4, "GET ") != 0)
                {
                    return std::nullopt;
                }
                continue;
            }
            if (line.empty())
            {
                break; // end of headers
            }
            const size_t colon = line.find(':');
            if (colon == std::string::npos)
            {
                continue;
            }
            const std::string name = asciiLower(trim(line.substr(0, colon)));
            const std::string value = trim(line.substr(colon + 1));
            if (name == "upgrade" && asciiLower(value).find("websocket") != std::string::npos)
            {
                hasUpgrade = true;
            }
            else if (name == "connection" && asciiLower(value).find("upgrade") != std::string::npos)
            {
                hasConnectionUpgrade = true;
            }
            else if (name == "sec-websocket-key")
            {
                key = value;
            }
        }
        if (!hasUpgrade || !hasConnectionUpgrade || key.empty())
        {
            return std::nullopt;
        }
        return key;
    }

    std::string buildHandshakeResponse(const std::string& clientKey)
    {
        return "HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Accept: " +
               computeAcceptKey(clientKey) + "\r\n\r\n";
    }

    std::string buildHandshakeRequest(const std::string& host, const std::string& path, std::string& outKey)
    {
        // A fixed-but-valid 16-byte key, base64'd. (Randomness is irrelevant to
        // correctness; a real client should randomize it.)
        static const uint8_t keyBytes[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
        outKey = base64(keyBytes, 16);
        return "GET " + path + " HTTP/1.1\r\n"
                                "Host: " +
               host + "\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: " +
               outKey + "\r\n"
                        "Sec-WebSocket-Version: 13\r\n\r\n";
    }

    std::string encodeMessage(FrameType type, const std::string& payload, bool mask, uint32_t maskKey)
    {
        uint8_t opcode = 0x2; // binary
        switch (type)
        {
        case FrameType::Text:
            opcode = 0x1;
            break;
        case FrameType::Binary:
            opcode = 0x2;
            break;
        case FrameType::Close:
            opcode = 0x8;
            break;
        case FrameType::Ping:
            opcode = 0x9;
            break;
        case FrameType::Pong:
            opcode = 0xA;
            break;
        }

        std::string out;
        out.push_back(static_cast<char>(0x80 | opcode)); // FIN + opcode

        const uint64_t n = payload.size();
        const uint8_t maskBit = mask ? 0x80 : 0x00;
        if (n < 126)
        {
            out.push_back(static_cast<char>(maskBit | static_cast<uint8_t>(n)));
        }
        else if (n <= 0xFFFF)
        {
            out.push_back(static_cast<char>(maskBit | 126));
            out.push_back(static_cast<char>((n >> 8) & 0xFF));
            out.push_back(static_cast<char>(n & 0xFF));
        }
        else
        {
            out.push_back(static_cast<char>(maskBit | 127));
            for (int i = 7; i >= 0; --i)
            {
                out.push_back(static_cast<char>((n >> (i * 8)) & 0xFF));
            }
        }

        if (mask)
        {
            uint8_t mk[4] = {
                static_cast<uint8_t>((maskKey >> 24) & 0xFF),
                static_cast<uint8_t>((maskKey >> 16) & 0xFF),
                static_cast<uint8_t>((maskKey >> 8) & 0xFF),
                static_cast<uint8_t>(maskKey & 0xFF),
            };
            out.append(reinterpret_cast<char*>(mk), 4);
            for (size_t i = 0; i < payload.size(); ++i)
            {
                out.push_back(static_cast<char>(static_cast<uint8_t>(payload[i]) ^ mk[i & 3]));
            }
        }
        else
        {
            out += payload;
        }
        return out;
    }

    std::optional<Message> Decoder::next()
    {
        while (!_error)
        {
            // Need at least the 2-byte header.
            if (_buf.size() < 2)
            {
                return std::nullopt;
            }
            const uint8_t b0 = static_cast<uint8_t>(_buf[0]);
            const uint8_t b1 = static_cast<uint8_t>(_buf[1]);
            const bool fin = (b0 & 0x80) != 0;
            const uint8_t rsv = b0 & 0x70;
            const uint8_t opcode = b0 & 0x0F;
            const bool masked = (b1 & 0x80) != 0;
            uint64_t len = b1 & 0x7F;
            size_t headerLen = 2;

            if (rsv != 0)
            {
                _error = true;
                return std::nullopt; // reserved bits must be zero
            }

            if (len == 126)
            {
                if (_buf.size() < 4)
                {
                    return std::nullopt;
                }
                len = (static_cast<uint8_t>(_buf[2]) << 8) | static_cast<uint8_t>(_buf[3]);
                headerLen = 4;
            }
            else if (len == 127)
            {
                if (_buf.size() < 10)
                {
                    return std::nullopt;
                }
                len = 0;
                for (int i = 0; i < 8; ++i)
                {
                    len = (len << 8) | static_cast<uint8_t>(_buf[2 + i]);
                }
                headerLen = 10;
            }

            uint8_t mask[4] = { 0, 0, 0, 0 };
            if (masked)
            {
                if (_buf.size() < headerLen + 4)
                {
                    return std::nullopt;
                }
                for (int i = 0; i < 4; ++i)
                {
                    mask[i] = static_cast<uint8_t>(_buf[headerLen + i]);
                }
                headerLen += 4;
            }

            // Guard against oversized frames before we wait for the whole body.
            if (len > _maxMessage || _fragData.size() + len > _maxMessage)
            {
                _error = true;
                return std::nullopt;
            }

            if (_buf.size() < headerLen + len)
            {
                return std::nullopt; // wait for the rest of the payload
            }

            std::string payload = _buf.substr(headerLen, static_cast<size_t>(len));
            if (masked)
            {
                for (size_t i = 0; i < payload.size(); ++i)
                {
                    payload[i] = static_cast<char>(static_cast<uint8_t>(payload[i]) ^ mask[i & 3]);
                }
            }
            _buf.erase(0, headerLen + static_cast<size_t>(len));

            const bool isControl = (opcode & 0x08) != 0;
            if (isControl)
            {
                // Control frames must be final and <=125 bytes.
                if (!fin || len > 125)
                {
                    _error = true;
                    return std::nullopt;
                }
                switch (opcode)
                {
                case 0x8:
                    return Message{ FrameType::Close, std::move(payload) };
                case 0x9:
                    return Message{ FrameType::Ping, std::move(payload) };
                case 0xA:
                    return Message{ FrameType::Pong, std::move(payload) };
                default:
                    _error = true;
                    return std::nullopt;
                }
            }

            // Data frame (possibly fragmented).
            if (opcode == 0x0)
            {
                // continuation
                if (!_inFragment)
                {
                    _error = true;
                    return std::nullopt;
                }
                _fragData += payload;
            }
            else if (opcode == 0x1 || opcode == 0x2)
            {
                if (_inFragment)
                {
                    _error = true; // new data frame while a fragment is open
                    return std::nullopt;
                }
                _fragType = (opcode == 0x1) ? FrameType::Text : FrameType::Binary;
                _fragData = std::move(payload);
                _inFragment = true;
            }
            else
            {
                _error = true;
                return std::nullopt;
            }

            if (fin)
            {
                _inFragment = false;
                return Message{ _fragType, std::move(_fragData) };
            }
            // else: need more fragments; loop to try to read the next frame
        }
        return std::nullopt;
    }
}
