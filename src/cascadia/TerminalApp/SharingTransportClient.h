// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// WinRT transport for the observer (client) side. Uses MessageWebSocket, which
// implements the WebSocket protocol itself, so each received message is already
// one complete SharingEngine protocol frame (no ws:: decoder needed here).

#pragma once

#include "../SharingEngine/Protocol.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>

namespace pane_sharing
{
    class SharingTransportClient : public std::enable_shared_from_this<SharingTransportClient>
    {
    public:
        struct Callbacks
        {
            std::function<void()> onConnected;
            std::function<void(const Bytes& frame)> onFrame;
            std::function<void(const std::string& reason)> onClosed;
        };

        explicit SharingTransportClient(Callbacks cb);
        ~SharingTransportClient();

        winrt::Windows::Foundation::IAsyncOperation<bool> ConnectAsync(winrt::hstring uri);
        void Send(const Bytes& frame); // one protocol frame -> one WS binary message
        void Close();

    private:
        winrt::fire_and_forget _Drain();

        Callbacks _cb;
        winrt::Windows::Networking::Sockets::MessageWebSocket _ws{ nullptr };
        winrt::Windows::Storage::Streams::DataWriter _writer{ nullptr };
        winrt::event_token _messageToken{};
        winrt::event_token _closedToken{};

        std::mutex _writeLock;
        std::deque<std::string> _outQueue; // one entry == one protocol frame == one WS message
        bool _writing = false;
        std::atomic<bool> _closed{ false };
    };
}
