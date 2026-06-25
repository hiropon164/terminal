// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "SharingTransportServer.h"

#include "WebSocket.h"

#include <vector>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Networking;
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

    SharingTransportServer::SharingTransportServer(TransportServerOptions opts,
                                                   std::function<Bytes()> makeSnapshot,
                                                   std::function<void(const Bytes&)> onRemoteInput) :
        _opts{ std::move(opts) },
        _makeSnapshot{ std::move(makeSnapshot) },
        _onRemoteInput{ std::move(onRemoteInput) }
    {
        ServerCallbacks cb;
        cb.makeSnapshot = [this]() -> Bytes { return _makeSnapshot ? _makeSnapshot() : Bytes{}; };
        cb.onRemoteInput = [this](const Bytes& vt) { if (_onRemoteInput) _onRemoteInput(vt); };
        cb.send = [this](ClientId id, const Bytes& frame) { _SendToClient(id, frame); };
        cb.closeClient = [this](ClientId id) {
            const auto it = _conns.find(id);
            if (it != _conns.end())
            {
                try
                {
                    it->second->socket.Close();
                }
                catch (...)
                {
                }
                _conns.erase(it);
            }
        };
        _server = std::make_unique<SharingServer>(_opts.protocol, std::move(cb));
    }

    SharingTransportServer::~SharingTransportServer()
    {
        Stop();
    }

    IAsyncOperation<bool> SharingTransportServer::StartAsync()
    {
        auto self = shared_from_this();
        _listener = StreamSocketListener{};
        _connectedToken = _listener.ConnectionReceived([self](auto&&, StreamSocketListenerConnectionReceivedEventArgs const& args) {
            self->_HandleClient(args.Socket());
        });

        try
        {
            const hstring service = _opts.port == 0 ? hstring{ L"" } : hstring{ std::to_wstring(_opts.port) };
            co_await _listener.BindEndpointAsync(HostName{ _opts.bindHost }, service);
        }
        catch (...)
        {
            co_return false;
        }

        try
        {
            _boundPort = static_cast<uint16_t>(std::stoul(std::wstring{ _listener.Information().LocalPort() }));
        }
        catch (...)
        {
            _boundPort = _opts.port;
        }
        co_return true;
    }

    void SharingTransportServer::Stop()
    {
        if (_stopped.exchange(true))
        {
            return;
        }
        try
        {
            if (_listener)
            {
                _listener.ConnectionReceived(_connectedToken);
                _listener.Close();
                _listener = nullptr;
            }
        }
        catch (...)
        {
        }
        std::lock_guard guard{ _mutex };
        for (auto& [id, conn] : _conns)
        {
            try
            {
                conn->socket.Close();
            }
            catch (...)
            {
            }
        }
        _conns.clear();
    }

    fire_and_forget SharingTransportServer::_HandleClient(StreamSocket socket)
    {
        auto self = shared_from_this();
        const auto maxFrame = _opts.protocol.maxFrameLen;
        ClientId id = 0;
        try
        {
            DataReader reader{ socket.InputStream() };
            reader.InputStreamOptions(InputStreamOptions::Partial);

            // --- read the HTTP upgrade request (bounded) ---
            std::string inbuf;
            size_t hsEnd = std::string::npos;
            while ((hsEnd = inbuf.find("\r\n\r\n")) == std::string::npos)
            {
                const uint32_t n = co_await reader.LoadAsync(4096);
                if (n == 0)
                {
                    co_return; // closed before handshake
                }
                std::vector<uint8_t> b(n);
                reader.ReadBytes(b);
                inbuf.append(reinterpret_cast<char*>(b.data()), b.size());
                if (inbuf.size() > 16 * 1024)
                {
                    co_return; // oversized handshake
                }
            }

            const std::string request = inbuf.substr(0, hsEnd + 4);
            const auto key = ws::handshakeKeyFromRequest(request);
            if (!key)
            {
                co_return;
            }

            auto conn = std::make_shared<ClientConn>();
            conn->socket = socket;
            conn->writer = DataWriter{ socket.OutputStream() };

            // 101 response
            const auto resp = ws::buildHandshakeResponse(*key);
            conn->writer.WriteBytes(asView(resp));
            co_await conn->writer.StoreAsync();

            // Register and announce.
            id = _nextId.fetch_add(1);
            {
                std::lock_guard guard{ _mutex };
                if (_stopped)
                {
                    co_return;
                }
                _conns[id] = conn;
                _server->onClientConnected(id);
            }

            // Decode any bytes that arrived after the handshake, then keep reading.
            ws::Decoder dec;
            dec.setMaxMessage(maxFrame);
            dec.feed(inbuf.substr(hsEnd + 4));

            while (!_stopped)
            {
                bool closing = false;
                for (;;)
                {
                    const auto msg = dec.next();
                    if (!msg)
                    {
                        break;
                    }
                    using FT = ws::FrameType;
                    if (msg->type == FT::Binary || msg->type == FT::Text)
                    {
                        std::lock_guard guard{ _mutex };
                        if (_server)
                        {
                            _server->onClientFrame(id, msg->payload);
                        }
                    }
                    else if (msg->type == FT::Ping)
                    {
                        _EnqueueWs(conn, ws::encodeMessage(FT::Pong, msg->payload, false));
                    }
                    else if (msg->type == FT::Close)
                    {
                        closing = true;
                        break;
                    }
                }
                if (closing || dec.error())
                {
                    break;
                }

                const uint32_t n = co_await reader.LoadAsync(4096);
                if (n == 0)
                {
                    break; // peer closed
                }
                std::vector<uint8_t> b(n);
                reader.ReadBytes(b);
                dec.feed(std::string(reinterpret_cast<char*>(b.data()), b.size()));
            }
        }
        catch (...)
        {
            // socket error / disconnect
        }

        if (id != 0)
        {
            std::lock_guard guard{ _mutex };
            if (_server)
            {
                _server->onClientDisconnected(id);
            }
            _conns.erase(id);
        }
        try
        {
            socket.Close();
        }
        catch (...)
        {
        }
    }

    void SharingTransportServer::_SendToClient(ClientId id, const Bytes& frame)
    {
        // Called under _mutex (from SharingServer). Find the connection and queue
        // a WebSocket binary message (server frames are not masked).
        const auto it = _conns.find(id);
        if (it == _conns.end())
        {
            return;
        }
        _EnqueueWs(it->second, ws::encodeMessage(ws::FrameType::Binary, frame, false));
    }

    void SharingTransportServer::_EnqueueWs(std::shared_ptr<ClientConn> conn, std::string wsBytes)
    {
        bool kick = false;
        {
            std::lock_guard guard{ *conn->writeLock };
            conn->outBuf += wsBytes;
            if (!conn->writing)
            {
                conn->writing = true;
                kick = true;
            }
        }
        if (kick)
        {
            _DrainWrites(conn);
        }
    }

    fire_and_forget SharingTransportServer::_DrainWrites(std::shared_ptr<ClientConn> conn)
    {
        auto self = shared_from_this();
        try
        {
            for (;;)
            {
                std::string chunk;
                {
                    std::lock_guard guard{ *conn->writeLock };
                    if (conn->outBuf.empty())
                    {
                        conn->writing = false;
                        co_return;
                    }
                    chunk.swap(conn->outBuf);
                }
                conn->writer.WriteBytes(asView(chunk));
                co_await conn->writer.StoreAsync();
            }
        }
        catch (...)
        {
            std::lock_guard guard{ *conn->writeLock };
            conn->writing = false;
        }
    }

    void SharingTransportServer::PushOutput(const Bytes& vt)
    {
        std::lock_guard guard{ _mutex };
        if (_server)
        {
            _server->pushOutput(vt);
        }
    }

    void SharingTransportServer::PushResize(uint32_t rows, uint32_t cols)
    {
        std::lock_guard guard{ _mutex };
        if (_server)
        {
            _server->pushResize(rows, cols);
        }
    }

    void SharingTransportServer::PushState(const std::string& state)
    {
        std::lock_guard guard{ _mutex };
        if (_server)
        {
            _server->pushState(state);
        }
    }
}
