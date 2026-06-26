// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.
//
// WinRT transport for the broadcast (host) side: a StreamSocketListener that
// speaks WebSocket (via the ws:: layer) and feeds the WT-independent
// SharingServer. This is the only server-side piece that touches Windows APIs;
// the policy/protocol live in Server.{h,cpp} + Protocol/WebSocket and are tested
// without sockets.
//
// Security posture: defaults to binding localhost only (M7). Plaintext ws:// is
// intended to be exposed (if at all) only via a TLS-terminating reverse proxy
// (M1); this class never binds a non-loopback host unless explicitly told to.

#pragma once

#include "../SharingEngine/Server.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.Threading.h>

namespace pane_sharing
{
    struct TransportServerOptions
    {
        std::wstring bindHost = L"localhost"; // M7: loopback by default
        bool allInterfaces = false; // true => bind every interface (LAN-reachable); opt-in only
        uint16_t port = 0; // 0 => OS-assigned ephemeral port
        ServerConfig protocol; // token, allowWrite, limits
    };

    class SharingTransportServer : public std::enable_shared_from_this<SharingTransportServer>
    {
    public:
        // makeSnapshot: produce the current screen as UTF-8 VT (may return empty).
        // onRemoteInput: deliver remote UTF-8 VT input (only ever called for a
        // write-permitted client; pass a no-op for read-only sharing).
        SharingTransportServer(TransportServerOptions opts,
                               std::function<Bytes()> makeSnapshot,
                               std::function<void(const Bytes&)> onRemoteInput);
        ~SharingTransportServer();

        // Bind + start listening. Returns false if bind failed.
        winrt::Windows::Foundation::IAsyncOperation<bool> StartAsync();
        void Stop();

        // The actual bound port (valid after StartAsync succeeds).
        uint16_t Port() const noexcept { return _boundPort; }

        // Host -> clients.
        void PushOutput(const Bytes& vt);
        void PushResize(uint32_t rows, uint32_t cols);
        void PushState(const std::string& state);

        // M5: invoked once when the server auto-stops itself (idle/lifetime). Set
        // by the owner to clean up (e.g. drop the session, clear the indicator).
        std::function<void()> onStopped;

    private:
        struct ClientConn
        {
            winrt::Windows::Networking::Sockets::StreamSocket socket{ nullptr };
            winrt::Windows::Storage::Streams::DataWriter writer{ nullptr };
            std::string outBuf;
            bool writing = false;
            std::shared_ptr<std::mutex> writeLock = std::make_shared<std::mutex>();
        };

        winrt::fire_and_forget _HandleClient(winrt::Windows::Networking::Sockets::StreamSocket socket);
        void _SendToClient(ClientId id, const Bytes& frame);
        void _EnqueueWs(std::shared_ptr<ClientConn> conn, std::string wsBytes);
        winrt::fire_and_forget _DrainWrites(std::shared_ptr<ClientConn> conn);
        void _OnTick();

        TransportServerOptions _opts;
        std::function<Bytes()> _makeSnapshot;
        std::function<void(const Bytes&)> _onRemoteInput;

        std::recursive_mutex _mutex; // guards _server and _conns
        std::unique_ptr<SharingServer> _server;
        std::map<ClientId, std::shared_ptr<ClientConn>> _conns;
        std::atomic<ClientId> _nextId{ 1 };
        std::atomic<bool> _stopped{ false };
        bool _shouldStop = false; // set by the server's onShouldStop, under _mutex
        uint16_t _boundPort = 0;

        winrt::Windows::Networking::Sockets::StreamSocketListener _listener{ nullptr };
        winrt::event_token _connectedToken{};
        winrt::Windows::System::Threading::ThreadPoolTimer _tickTimer{ nullptr };
    };
}
