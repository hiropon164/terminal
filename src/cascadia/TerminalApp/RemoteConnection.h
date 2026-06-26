// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Observer-side ITerminalConnection: it has no real PTY. It connects to a shared
// session over a WebSocket (SharingTransportClient + SharingClient) and turns the
// inbound VT stream into TerminalOutput so an ordinary TermControl renders it.
// Modeled on DebugTapConnection (a code-only winrt::implements connection).

#pragma once

#include <winrt/Microsoft.Terminal.TerminalConnection.h>
#include <til/latch.h>

#include "../SharingEngine/Client.h"
#include "SharingTransportClient.h"

#include <atomic>
#include <memory>
#include <string>

namespace winrt::TerminalApp::implementation
{
    class RemoteConnection : public winrt::implements<RemoteConnection, winrt::Microsoft::Terminal::TerminalConnection::ITerminalConnection>
    {
    public:
        RemoteConnection(winrt::hstring uri, winrt::hstring token);

        void Initialize(const winrt::Windows::Foundation::Collections::ValueSet& /*settings*/) {}
        void Start();
        void WriteInput(const winrt::array_view<const char16_t> data);
        void Resize(uint32_t rows, uint32_t columns);
        void Close();

        winrt::guid SessionId() const noexcept { return {}; }
        winrt::Microsoft::Terminal::TerminalConnection::ConnectionState State() const noexcept;

        // Optionally tie a background SSH tunnel process to this connection: it is
        // terminated when the viewer connection closes. `processHandle` is a Win32
        // HANDLE (kept as void* to avoid <windows.h> in this header); ownership is
        // transferred here.
        void SetTunnelProcess(void* processHandle) noexcept { _tunnelProc = processHandle; }

        til::event<winrt::Microsoft::Terminal::TerminalConnection::TerminalOutputHandler> TerminalOutput;
        til::typed_event<winrt::Microsoft::Terminal::TerminalConnection::ITerminalConnection, winrt::Windows::Foundation::IInspectable> StateChanged;

    private:
        winrt::fire_and_forget _Connect();
        void _setState(winrt::Microsoft::Terminal::TerminalConnection::ConnectionState state);
        void _emitUtf8(const std::string& utf8);

        winrt::hstring _uri;
        winrt::hstring _token;
        std::shared_ptr<pane_sharing::SharingClient> _client;
        std::shared_ptr<pane_sharing::SharingTransportClient> _transport;
        std::atomic<winrt::Microsoft::Terminal::TerminalConnection::ConnectionState> _state{ winrt::Microsoft::Terminal::TerminalConnection::ConnectionState::NotConnected };
        std::string _utf8Pending; // incomplete trailing UTF-8 bytes carried to next chunk
        void* _tunnelProc = nullptr; // owned SSH tunnel process HANDLE, or null
    };
}
