// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// Host (broadcast) side glue: tees one live terminal pane's output to a
// SharingTransportServer for read-only sharing. This is the thin WT-side adapter
// over the WT-independent SharingEngine. Read-write (a real SharedConnection
// decorator that merges remote input) is a later phase.

#pragma once

#include <winrt/Microsoft.Terminal.TerminalConnection.h>
#include <winrt/Microsoft.Terminal.Control.h>

#include "../SharingEngine/SharingTransportServer.h"

#include <functional>
#include <memory>
#include <string>

namespace winrt::TerminalApp::implementation
{
    class PaneShareSession : public std::enable_shared_from_this<PaneShareSession>
    {
    public:
        PaneShareSession(winrt::Microsoft::Terminal::TerminalConnection::ITerminalConnection connection,
                         winrt::Microsoft::Terminal::Control::ControlCore core,
                         std::string token);

        // Starts the server and subscribes to the pane output. false on bind failure.
        winrt::Windows::Foundation::IAsyncOperation<bool> StartAsync();
        void Stop();

        uint16_t Port() const noexcept;
        const std::string& Token() const noexcept { return _token; }
        winrt::Microsoft::Terminal::TerminalConnection::ITerminalConnection Connection() const noexcept { return _conn; }

        // M5: invoked (on a background thread) when the share auto-stops itself
        // because it was idle or the token lifetime elapsed. The owner marshals to
        // the UI thread to drop the session and clear the indicator.
        std::function<void()> OnAutoStopped;

    private:
        void _onOutput(winrt::array_view<const char16_t> data);

        winrt::Microsoft::Terminal::TerminalConnection::ITerminalConnection _conn{ nullptr };
        winrt::Microsoft::Terminal::Control::ControlCore _core{ nullptr };
        std::string _token;
        std::shared_ptr<pane_sharing::SharingTransportServer> _server;
        winrt::Microsoft::Terminal::TerminalConnection::ITerminalConnection::TerminalOutput_revoker _revoker;
        char16_t _pendingHigh = 0; // dangling UTF-16 high surrogate carried across chunks
    };
}
