// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "SharingTransportClient.h"

#include <vector>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Networking::Sockets;
using namespace winrt::Windows::Storage::Streams;

namespace pane_sharing
{
    namespace
    {
        winrt::array_view<uint8_t const> asView(const std::string& s)
        {
            const auto p = reinterpret_cast<uint8_t const*>(s.data());
            return winrt::array_view<uint8_t const>{ p, p + s.size() };
        }
    }

    SharingTransportClient::SharingTransportClient(Callbacks cb) :
        _cb{ std::move(cb) }
    {
    }

    SharingTransportClient::~SharingTransportClient()
    {
        Close();
    }

    IAsyncOperation<bool> SharingTransportClient::ConnectAsync(winrt::hstring uri)
    {
        auto self = shared_from_this();
        try
        {
            _ws = MessageWebSocket{};
            _ws.Control().MessageType(SocketMessageType::Binary);

            std::weak_ptr<SharingTransportClient> weak = self;
            _messageToken = _ws.MessageReceived([weak](MessageWebSocket const&, MessageWebSocketMessageReceivedEventArgs const& args) {
                auto strong = weak.lock();
                if (!strong)
                {
                    return;
                }
                try
                {
                    DataReader reader = args.GetDataReader();
                    const uint32_t n = reader.UnconsumedBufferLength();
                    std::vector<uint8_t> b(n);
                    if (n != 0)
                    {
                        reader.ReadBytes(b);
                    }
                    if (strong->_cb.onFrame)
                    {
                        strong->_cb.onFrame(std::string(reinterpret_cast<char*>(b.data()), b.size()));
                    }
                }
                catch (...)
                {
                }
            });
            _closedToken = _ws.Closed([weak](IWebSocket const&, WebSocketClosedEventArgs const&) {
                if (auto strong = weak.lock(); strong && strong->_cb.onClosed)
                {
                    strong->_cb.onClosed("closed");
                }
            });

            co_await _ws.ConnectAsync(Uri{ uri });
            _writer = DataWriter{ _ws.OutputStream() };
        }
        catch (...)
        {
            co_return false;
        }

        if (_cb.onConnected)
        {
            _cb.onConnected();
        }
        co_return true;
    }

    void SharingTransportClient::Send(const Bytes& frame)
    {
        if (_closed)
        {
            return;
        }
        bool kick = false;
        {
            std::lock_guard guard{ _writeLock };
            _outQueue.push_back(frame);
            if (!_writing)
            {
                _writing = true;
                kick = true;
            }
        }
        if (kick)
        {
            _Drain();
        }
    }

    fire_and_forget SharingTransportClient::_Drain()
    {
        auto self = shared_from_this();
        try
        {
            for (;;)
            {
                std::string frame;
                {
                    std::lock_guard guard{ _writeLock };
                    if (_outQueue.empty())
                    {
                        _writing = false;
                        co_return;
                    }
                    frame = std::move(_outQueue.front());
                    _outQueue.pop_front();
                }
                // One frame == one MessageWebSocket message (StoreAsync flushes a
                // complete message), preserving frame boundaries on the wire.
                _writer.WriteBytes(asView(frame));
                co_await _writer.StoreAsync();
            }
        }
        catch (...)
        {
            std::lock_guard guard{ _writeLock };
            _writing = false;
        }
    }

    void SharingTransportClient::Close()
    {
        if (_closed.exchange(true))
        {
            return;
        }
        try
        {
            if (_ws)
            {
                _ws.MessageReceived(_messageToken);
                _ws.Closed(_closedToken);
                _ws.Close();
                _ws = nullptr;
            }
        }
        catch (...)
        {
        }
    }
}
