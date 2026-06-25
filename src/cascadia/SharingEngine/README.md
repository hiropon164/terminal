# panesharing — OS-independent pane-sharing core

The portable core of this fork's read-only pane sharing: the wire **protocol**,
an RFC 6455 **WebSocket** implementation (handshake + framing + SHA-1/Base64),
the broadcast/authorization **Server**, and the observer **Client**.

It is **pure C++17 with no platform dependencies** — only the C++ standard
library, no Windows / WinRT / POSIX headers, and all integers are serialized with
explicit byte order, so it builds unchanged on Linux, macOS and Windows.

## What's here vs. not

| Part | Files | Portable? |
|------|-------|-----------|
| Protocol (frames, minimal JSON) | `Protocol.{h,cpp}` | ✅ |
| WebSocket (RFC 6455) | `WebSocket.{h,cpp}` | ✅ |
| Server (auth/policy M2–M6, M5 expiry/idle via injected clock) | `Server.{h,cpp}` | ✅ |
| Client (observer routing) | `Client.{h,cpp}` | ✅ |
| WinRT transport | `SharingTransportServer/Client.{h,cpp}` | ❌ Windows only — **excluded from this library** |

The core never touches a socket itself: a transport drives it through callbacks
(`ServerCallbacks` / `ClientCallbacks`). On Windows that transport is the WinRT
implementation above; on another OS a host supplies its own (e.g. POSIX sockets,
Asio, libwebsockets). This is what lets the same core be embedded elsewhere.

## Build & test (any platform)

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Produces a static `panesharing` library and runs the three unit-test suites
(`Protocol`, `WebSocket`, `Orchestration` — 41 checks). `-D PANESHARING_BUILD_TESTS=OFF`
builds just the library. `cmake --install build` installs the library and headers
under `include/panesharing/`.

## Embedding

Add this directory via `add_subdirectory(... panesharing)` and link
`panesharing::panesharing`, or `cmake --install` and link the installed library.
A host then:

- runs a WebSocket transport and feeds received frames to `SharingServer::onClientFrame`,
- calls `SharingServer::pushOutput(...)` with the shared session's output (VT bytes),
- supplies `makeSnapshot()` (current screen as a replayable VT stream) for late joiners,
- and, for write access, handles `onRemoteInput`.

> The library has no TLS. Keep plaintext `ws://` on loopback, or terminate TLS in
> the transport / a reverse proxy. Read-only sharing still discloses everything on
> the pane — treat the token as a secret.
