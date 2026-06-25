// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "RemoteConnection.h"

using namespace winrt::Microsoft::Terminal::TerminalConnection;

namespace winrt::TerminalApp::implementation
{
    RemoteConnection::RemoteConnection(winrt::hstring uri, winrt::hstring token) :
        _uri{ std::move(uri) },
        _token{ std::move(token) }
    {
        auto weak = get_weak();

        pane_sharing::ClientCallbacks ccb;
        ccb.send = [weak](const pane_sharing::Bytes& f) {
            if (auto s = weak.get(); s && s->_transport)
            {
                s->_transport->Send(f);
            }
        };
        ccb.onSnapshot = [weak](const pane_sharing::SnapshotMsg& m) {
            if (auto s = weak.get())
            {
                s->_emitUtf8(m.vt);
            }
        };
        ccb.onOutput = [weak](const pane_sharing::Bytes& vt) {
            if (auto s = weak.get())
            {
                s->_emitUtf8(vt);
            }
        };
        ccb.onResize = [](uint32_t, uint32_t) {}; // host is authoritative; ignore for now
        ccb.onState = [weak](const std::string& st) {
            if (auto s = weak.get(); s && st == "closed")
            {
                s->_setState(ConnectionState::Closed);
            }
        };

        pane_sharing::ClientConfig cfg;
        cfg.token = winrt::to_string(_token);
        cfg.readOnly = true; // observer is read-only for now
        _client = std::make_shared<pane_sharing::SharingClient>(cfg, std::move(ccb));

        pane_sharing::SharingTransportClient::Callbacks tcb;
        tcb.onConnected = [weak]() {
            if (auto s = weak.get())
            {
                s->_client->onConnected();
                s->_setState(ConnectionState::Connected);
            }
        };
        tcb.onFrame = [weak](const pane_sharing::Bytes& f) {
            if (auto s = weak.get())
            {
                s->_client->onServerFrame(f);
            }
        };
        tcb.onClosed = [weak](const std::string&) {
            if (auto s = weak.get())
            {
                s->_setState(ConnectionState::Closed);
            }
        };
        _transport = std::make_shared<pane_sharing::SharingTransportClient>(std::move(tcb));
    }

    void RemoteConnection::Start()
    {
        _setState(ConnectionState::Connecting);
        _Connect();
    }

    winrt::fire_and_forget RemoteConnection::_Connect()
    {
        auto strong = get_strong();
        const bool ok = co_await _transport->ConnectAsync(_uri);
        if (!ok)
        {
            _setState(ConnectionState::Failed);
        }
        // On success, the transport's onConnected callback sends HELLO and moves
        // the state to Connected.
    }

    void RemoteConnection::WriteInput(const winrt::array_view<const char16_t> /*data*/)
    {
        // Read-only observer: local keystrokes are not forwarded. (Read-write is a
        // later phase; the server would also drop them.)
    }

    void RemoteConnection::Resize(uint32_t /*rows*/, uint32_t /*columns*/)
    {
        // Host-authoritative sizing; the observer just renders what it receives.
    }

    void RemoteConnection::Close()
    {
        _setState(ConnectionState::Closed);
        if (_transport)
        {
            _transport->Close();
        }
        _transport.reset();
        _client.reset();

        // Tear down the SSH tunnel, if this viewer launched one.
        if (_tunnelProc)
        {
            const auto h = static_cast<HANDLE>(_tunnelProc);
            _tunnelProc = nullptr;
            TerminateProcess(h, 0);
            CloseHandle(h);
        }
    }

    ConnectionState RemoteConnection::State() const noexcept
    {
        return _state.load();
    }

    void RemoteConnection::_setState(ConnectionState state)
    {
        _state.store(state);
        StateChanged.raise(*this, nullptr);
    }

    void RemoteConnection::_emitUtf8(const std::string& chunk)
    {
        std::string s = _utf8Pending + chunk;
        _utf8Pending.clear();

        std::u16string out;
        size_t i = 0;
        while (i < s.size())
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            size_t need = 0;
            uint32_t cp = 0;
            if (c < 0x80)
            {
                cp = c;
                need = 1;
            }
            else if ((c >> 5) == 0x6)
            {
                cp = c & 0x1F;
                need = 2;
            }
            else if ((c >> 4) == 0xE)
            {
                cp = c & 0x0F;
                need = 3;
            }
            else if ((c >> 3) == 0x1E)
            {
                cp = c & 0x07;
                need = 4;
            }
            else
            {
                out.push_back(0xFFFD); // invalid lead byte
                ++i;
                continue;
            }

            if (i + need > s.size())
            {
                _utf8Pending = s.substr(i); // incomplete sequence -> carry over
                break;
            }

            bool valid = true;
            for (size_t k = 1; k < need; ++k)
            {
                const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if ((cc >> 6) != 0x2)
                {
                    valid = false;
                    break;
                }
                cp = (cp << 6) | (cc & 0x3F);
            }
            if (!valid)
            {
                out.push_back(0xFFFD);
                ++i;
                continue;
            }
            i += need;

            if (cp < 0x10000)
            {
                out.push_back(static_cast<char16_t>(cp));
            }
            else
            {
                cp -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
            }
        }

        if (!out.empty())
        {
            TerminalOutput.raise(winrt::array_view<const char16_t>{ out.data(), out.data() + out.size() });
        }
    }
}
