// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "PaneShareSession.h"

using namespace winrt::Microsoft::Terminal::TerminalConnection;

namespace winrt::TerminalApp::implementation
{
    namespace
    {
        // Stateful UTF-16 -> UTF-8. A high surrogate that arrives at the end of a
        // chunk is held in `pendingHigh` and paired with the low surrogate from the
        // next chunk, so a surrogate pair split across two TerminalOutput callbacks
        // isn't corrupted.
        void utf16ToUtf8(winrt::array_view<const char16_t> in, char16_t& pendingHigh, std::string& out)
        {
            const auto emit = [&out](uint32_t cp) {
                if (cp < 0x80)
                {
                    out.push_back(static_cast<char>(cp));
                }
                else if (cp < 0x800)
                {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else if (cp < 0x10000)
                {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            };

            uint32_t i = 0;
            while (i < in.size())
            {
                const char16_t u = in[i++];
                if (pendingHigh != 0)
                {
                    if (u >= 0xDC00 && u <= 0xDFFF)
                    {
                        const uint32_t cp = 0x10000u + ((static_cast<uint32_t>(pendingHigh) - 0xD800u) << 10) + (static_cast<uint32_t>(u) - 0xDC00u);
                        pendingHigh = 0;
                        emit(cp);
                        continue;
                    }
                    // unpaired high surrogate
                    pendingHigh = 0;
                    emit(0xFFFD);
                    --i; // reprocess u
                    continue;
                }
                if (u >= 0xD800 && u <= 0xDBFF)
                {
                    pendingHigh = u; // wait for the low surrogate (maybe next chunk)
                    continue;
                }
                if (u >= 0xDC00 && u <= 0xDFFF)
                {
                    emit(0xFFFD); // unpaired low surrogate
                    continue;
                }
                emit(u);
            }
        }
    }

    PaneShareSession::PaneShareSession(ITerminalConnection connection,
                                       winrt::Microsoft::Terminal::Control::ControlCore core,
                                       std::string token,
                                       bool allInterfaces) :
        _conn{ std::move(connection) },
        _core{ std::move(core) },
        _token{ std::move(token) },
        _allInterfaces{ allInterfaces }
    {
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> PaneShareSession::StartAsync()
    {
        auto self = shared_from_this();

        pane_sharing::TransportServerOptions opts;
        opts.bindHost = L"localhost"; // M7 (default)
        opts.allInterfaces = _allInterfaces; // opt-in LAN exposure
        opts.port = 0; // ephemeral
        opts.protocol.token = _token;
        opts.protocol.allowWrite = false; // read-only for now (M2/M4 trivially hold)
        opts.protocol.tokenLifetimeMs = 30 * 60 * 1000; // M5: token admits new viewers for 30 min
        opts.protocol.idleTimeoutMs = 10 * 60 * 1000; // M5: auto-stop after 10 min with no viewers

        // makeSnapshot is invoked on the transport's socket thread when a viewer
        // connects; ControlCore::SerializeMainBuffer takes the terminal read lock,
        // so it is safe to call there. Capture the (agile) core by value.
        auto core = _core;
        _server = std::make_shared<pane_sharing::SharingTransportServer>(
            opts,
            [core]() -> pane_sharing::Bytes {
                try
                {
                    if (!core)
                    {
                        return {};
                    }
                    const auto vt = core.SerializeMainBuffer();
                    if (vt.empty())
                    {
                        return {};
                    }
                    const auto first = reinterpret_cast<const char16_t*>(vt.data());
                    std::string out;
                    char16_t pending = 0;
                    utf16ToUtf8(winrt::array_view<const char16_t>{ first, first + vt.size() }, pending, out);
                    return out;
                }
                catch (...)
                {
                    return {};
                }
            },
            [](const pane_sharing::Bytes&) {} // read-only: no remote input
        );

        // M5: when the server auto-stops (idle/lifetime), notify the owner.
        {
            auto weak = weak_from_this();
            _server->onStopped = [weak]() {
                if (auto s = weak.lock(); s && s->OnAutoStopped)
                {
                    s->OnAutoStopped();
                }
            };
        }

        const bool ok = co_await _server->StartAsync();
        if (!ok)
        {
            _server.reset();
            co_return false;
        }

        // Subscribe only after the server is up. A weak ref keeps the handler safe
        // if the session is torn down concurrently.
        auto weak = weak_from_this();
        _revoker = _conn.TerminalOutput(winrt::auto_revoke, [weak](winrt::array_view<const char16_t> data) {
            if (auto s = weak.lock())
            {
                s->_onOutput(data);
            }
        });
        co_return true;
    }

    void PaneShareSession::Stop()
    {
        _revoker.revoke();
        if (_server)
        {
            _server->Stop();
            _server.reset();
        }
    }

    uint16_t PaneShareSession::Port() const noexcept
    {
        return _server ? _server->Port() : 0;
    }

    void PaneShareSession::_onOutput(winrt::array_view<const char16_t> data)
    {
        if (!_server)
        {
            return;
        }
        std::string utf8;
        utf16ToUtf8(data, _pendingHigh, utf8);
        if (!utf8.empty())
        {
            _server->PushOutput(utf8);
        }
    }
}
