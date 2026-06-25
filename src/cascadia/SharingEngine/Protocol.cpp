// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "Protocol.h"

#include <array>

namespace pane_sharing
{
    namespace
    {
        void putU32(Bytes& b, uint32_t v)
        {
            b.push_back(static_cast<char>(v & 0xFF));
            b.push_back(static_cast<char>((v >> 8) & 0xFF));
            b.push_back(static_cast<char>((v >> 16) & 0xFF));
            b.push_back(static_cast<char>((v >> 24) & 0xFF));
        }

        bool getU32(const Bytes& b, size_t& i, uint32_t& v)
        {
            if (i + 4 > b.size())
            {
                return false;
            }
            const auto u = [&](size_t k) { return static_cast<uint32_t>(static_cast<unsigned char>(b[i + k])); };
            v = u(0) | (u(1) << 8) | (u(2) << 16) | (u(3) << 24);
            i += 4;
            return true;
        }

        bool isKnownOpcode(uint8_t op)
        {
            switch (static_cast<Opcode>(op))
            {
            case Opcode::Snapshot:
            case Opcode::Output:
            case Opcode::Resize:
            case Opcode::State:
            case Opcode::Hello:
            case Opcode::Input:
            case Opcode::Ping:
                return true;
            default:
                return false;
            }
        }

        // --- minimal JSON (flat objects of scalar values) ---

        void jsonEscape(std::string& out, const std::string& s)
        {
            out.push_back('"');
            for (const unsigned char c : s)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        static const char* hex = "0123456789abcdef";
                        out += "\\u00";
                        out.push_back(hex[(c >> 4) & 0xF]);
                        out.push_back(hex[c & 0xF]);
                    }
                    else
                    {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
                }
            }
            out.push_back('"');
        }

        struct JVal
        {
            enum class Type
            {
                Str,
                Num,
                Bool,
                Null,
                Raw // nested object/array, captured verbatim
            } type = Type::Null;
            std::string s; // unescaped string, or the literal token
        };

        struct JsonParser
        {
            const std::string& in;
            size_t i = 0;

            explicit JsonParser(const std::string& s) :
                in{ s } {}

            void skipWs()
            {
                while (i < in.size() && (in[i] == ' ' || in[i] == '\t' || in[i] == '\n' || in[i] == '\r'))
                {
                    ++i;
                }
            }

            bool parseString(std::string& out)
            {
                if (i >= in.size() || in[i] != '"')
                {
                    return false;
                }
                ++i;
                out.clear();
                while (i < in.size())
                {
                    const char c = in[i++];
                    if (c == '"')
                    {
                        return true;
                    }
                    if (c == '\\')
                    {
                        if (i >= in.size())
                        {
                            return false;
                        }
                        const char e = in[i++];
                        switch (e)
                        {
                        case '"':
                            out.push_back('"');
                            break;
                        case '\\':
                            out.push_back('\\');
                            break;
                        case '/':
                            out.push_back('/');
                            break;
                        case 'b':
                            out.push_back('\b');
                            break;
                        case 'f':
                            out.push_back('\f');
                            break;
                        case 'n':
                            out.push_back('\n');
                            break;
                        case 'r':
                            out.push_back('\r');
                            break;
                        case 't':
                            out.push_back('\t');
                            break;
                        case 'u':
                        {
                            if (i + 4 > in.size())
                            {
                                return false;
                            }
                            uint32_t cp = 0;
                            for (int k = 0; k < 4; ++k)
                            {
                                const char h = in[i++];
                                cp <<= 4;
                                if (h >= '0' && h <= '9')
                                    cp |= static_cast<uint32_t>(h - '0');
                                else if (h >= 'a' && h <= 'f')
                                    cp |= static_cast<uint32_t>(h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F')
                                    cp |= static_cast<uint32_t>(h - 'A' + 10);
                                else
                                    return false;
                            }
                            // Encode the BMP code point as UTF-8 (surrogate pairs
                            // are not needed for our control messages).
                            if (cp < 0x80)
                            {
                                out.push_back(static_cast<char>(cp));
                            }
                            else if (cp < 0x800)
                            {
                                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                            }
                            else
                            {
                                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                            }
                            break;
                        }
                        default:
                            return false;
                        }
                    }
                    else
                    {
                        out.push_back(c);
                    }
                }
                return false; // unterminated
            }

            // Capture a nested object/array verbatim by bracket matching.
            bool captureNested(std::string& out)
            {
                const char open = in[i];
                const char close = (open == '{') ? '}' : ']';
                int depth = 0;
                const size_t start = i;
                while (i < in.size())
                {
                    const char c = in[i];
                    if (c == '"')
                    {
                        std::string tmp;
                        if (!parseString(tmp))
                        {
                            return false;
                        }
                        continue;
                    }
                    ++i;
                    if (c == open)
                        ++depth;
                    else if (c == close)
                    {
                        if (--depth == 0)
                        {
                            out = in.substr(start, i - start);
                            return true;
                        }
                    }
                }
                return false;
            }

            bool parseValue(JVal& v)
            {
                skipWs();
                if (i >= in.size())
                {
                    return false;
                }
                const char c = in[i];
                if (c == '"')
                {
                    v.type = JVal::Type::Str;
                    return parseString(v.s);
                }
                if (c == '{' || c == '[')
                {
                    v.type = JVal::Type::Raw;
                    return captureNested(v.s);
                }
                if (c == 't' || c == 'f')
                {
                    const bool isTrue = in.compare(i, 4, "true") == 0;
                    const bool isFalse = in.compare(i, 5, "false") == 0;
                    if (!isTrue && !isFalse)
                    {
                        return false;
                    }
                    v.type = JVal::Type::Bool;
                    v.s = isTrue ? "true" : "false";
                    i += isTrue ? 4 : 5;
                    return true;
                }
                if (c == 'n')
                {
                    if (in.compare(i, 4, "null") != 0)
                    {
                        return false;
                    }
                    v.type = JVal::Type::Null;
                    i += 4;
                    return true;
                }
                // number
                const size_t start = i;
                if (c == '-' || c == '+')
                {
                    ++i;
                }
                bool any = false;
                while (i < in.size() && ((in[i] >= '0' && in[i] <= '9') || in[i] == '.' || in[i] == 'e' || in[i] == 'E' || in[i] == '+' || in[i] == '-'))
                {
                    ++i;
                    any = true;
                }
                if (!any)
                {
                    return false;
                }
                v.type = JVal::Type::Num;
                v.s = in.substr(start, i - start);
                return true;
            }

            // Parse a flat top-level object into key -> value.
            bool parseObject(std::map<std::string, JVal>& out)
            {
                skipWs();
                if (i >= in.size() || in[i] != '{')
                {
                    return false;
                }
                ++i;
                skipWs();
                if (i < in.size() && in[i] == '}')
                {
                    ++i;
                    return true;
                }
                while (true)
                {
                    skipWs();
                    std::string key;
                    if (!parseString(key))
                    {
                        return false;
                    }
                    skipWs();
                    if (i >= in.size() || in[i] != ':')
                    {
                        return false;
                    }
                    ++i;
                    JVal v;
                    if (!parseValue(v))
                    {
                        return false;
                    }
                    out[key] = std::move(v);
                    skipWs();
                    if (i >= in.size())
                    {
                        return false;
                    }
                    if (in[i] == ',')
                    {
                        ++i;
                        continue;
                    }
                    if (in[i] == '}')
                    {
                        ++i;
                        return true;
                    }
                    return false;
                }
            }
        };

        std::optional<std::map<std::string, JVal>> parseFlatObject(const std::string& in)
        {
            JsonParser p{ in };
            std::map<std::string, JVal> obj;
            if (!p.parseObject(obj))
            {
                return std::nullopt;
            }
            return obj;
        }

        bool getU32Field(const std::map<std::string, JVal>& o, const char* key, uint32_t& out)
        {
            const auto it = o.find(key);
            if (it == o.end() || it->second.type != JVal::Type::Num)
            {
                return false;
            }
            // non-negative integer only
            unsigned long long v = 0;
            for (const char c : it->second.s)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
                v = v * 10 + static_cast<unsigned long long>(c - '0');
                if (v > 0xFFFFFFFFull)
                {
                    return false;
                }
            }
            if (it->second.s.empty())
            {
                return false;
            }
            out = static_cast<uint32_t>(v);
            return true;
        }
    } // namespace

    Bytes encodeSnapshot(const SnapshotMsg& m)
    {
        Bytes b;
        b.push_back(static_cast<char>(Opcode::Snapshot));
        putU32(b, m.rows);
        putU32(b, m.cols);
        putU32(b, static_cast<uint32_t>(m.vt.size()));
        b += m.vt;
        return b;
    }

    Bytes encodeOutput(const Bytes& vt)
    {
        Bytes b;
        b.push_back(static_cast<char>(Opcode::Output));
        b += vt;
        return b;
    }

    Bytes encodeResize(uint32_t rows, uint32_t cols)
    {
        Bytes b;
        b.push_back(static_cast<char>(Opcode::Resize));
        b += "{\"rows\":" + std::to_string(rows) + ",\"cols\":" + std::to_string(cols) + "}";
        return b;
    }

    Bytes encodeState(const std::string& state)
    {
        Bytes b;
        b.push_back(static_cast<char>(Opcode::State));
        b += "{\"state\":";
        jsonEscape(b, state);
        b += "}";
        return b;
    }

    Bytes encodeHello(const HelloMsg& m)
    {
        Bytes b;
        b.push_back(static_cast<char>(Opcode::Hello));
        b += "{\"protocolVersion\":" + std::to_string(m.protocolVersion);
        b += ",\"token\":";
        jsonEscape(b, m.token);
        b += ",\"readOnly\":";
        b += m.readOnly ? "true" : "false";
        b += ",\"viewportRows\":" + std::to_string(m.viewportRows);
        b += ",\"viewportCols\":" + std::to_string(m.viewportCols);
        b += "}";
        return b;
    }

    Bytes encodeInput(const Bytes& vt)
    {
        Bytes b;
        b.push_back(static_cast<char>(Opcode::Input));
        b += vt;
        return b;
    }

    Bytes encodePing()
    {
        Bytes b;
        b.push_back(static_cast<char>(Opcode::Ping));
        return b;
    }

    std::optional<Frame> parseFrame(const Bytes& wire, size_t maxLen)
    {
        if (wire.empty())
        {
            return std::nullopt;
        }
        if (maxLen != 0 && wire.size() > maxLen)
        {
            return std::nullopt;
        }
        const auto op = static_cast<uint8_t>(wire[0]);
        if (!isKnownOpcode(op))
        {
            return std::nullopt;
        }
        Frame f;
        f.op = static_cast<Opcode>(op);
        f.payload = wire.substr(1);
        return f;
    }

    std::optional<SnapshotMsg> parseSnapshot(const Bytes& payload)
    {
        size_t i = 0;
        SnapshotMsg m;
        uint32_t len = 0;
        if (!getU32(payload, i, m.rows) || !getU32(payload, i, m.cols) || !getU32(payload, i, len))
        {
            return std::nullopt;
        }
        if (payload.size() - i < len)
        {
            return std::nullopt;
        }
        m.vt = payload.substr(i, len);
        return m;
    }

    std::optional<ResizeMsg> parseResize(const Bytes& payload)
    {
        const auto o = parseFlatObject(payload);
        if (!o)
        {
            return std::nullopt;
        }
        ResizeMsg m;
        if (!getU32Field(*o, "rows", m.rows) || !getU32Field(*o, "cols", m.cols))
        {
            return std::nullopt;
        }
        return m;
    }

    std::optional<StateMsg> parseState(const Bytes& payload)
    {
        const auto o = parseFlatObject(payload);
        if (!o)
        {
            return std::nullopt;
        }
        const auto it = o->find("state");
        if (it == o->end() || it->second.type != JVal::Type::Str)
        {
            return std::nullopt;
        }
        StateMsg m;
        m.state = it->second.s;
        return m;
    }

    std::optional<HelloMsg> parseHello(const Bytes& payload)
    {
        const auto o = parseFlatObject(payload);
        if (!o)
        {
            return std::nullopt;
        }
        HelloMsg m;
        if (!getU32Field(*o, "protocolVersion", m.protocolVersion))
        {
            return std::nullopt;
        }
        const auto tok = o->find("token");
        if (tok == o->end() || tok->second.type != JVal::Type::Str)
        {
            return std::nullopt;
        }
        m.token = tok->second.s;
        const auto ro = o->find("readOnly");
        if (ro != o->end() && ro->second.type == JVal::Type::Bool)
        {
            m.readOnly = (ro->second.s == "true");
        }
        // viewport fields are optional
        getU32Field(*o, "viewportRows", m.viewportRows);
        getU32Field(*o, "viewportCols", m.viewportCols);
        return m;
    }
}
