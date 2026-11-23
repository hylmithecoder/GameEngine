#pragma once
// WebSocket transports (IXWebSocket underneath, kept out of this header).
//
// WsServerTransport is what the core (GameEngineSDL) listens with; everything
// else connects with WsClientTransport. By default the server binds loopback
// only and lets the OS pick the port, so a stale engine process can never make
// startup fail with EADDRINUSE — read Port() after Start() and hand it to the
// children.

#include "ITransport.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace ilmeee::net {

struct WsServerOptions {
  // Loopback by default. Binding anything wider exposes the bus to the LAN;
  // only do it together with a token.
  std::string host = "127.0.0.1";
  // 0 = pick a free port (retried a few times, since the pick can race).
  int port = 0;
  std::size_t maxConnections = 16;
};

class WsServerTransport : public ITransport {
public:
  explicit WsServerTransport(WsServerOptions options = {});
  ~WsServerTransport() override;

  void SetCallbacks(TransportCallbacks callbacks) override;
  bool Start() override;
  void Stop() override;
  bool SendText(ConnectionId to, const std::string &text) override;
  bool SendBinary(ConnectionId to,
                  std::span<const std::uint8_t> data) override;
  void Close(ConnectionId who, const std::string &reason) override;

  // Actual listening port; 0 before Start() or after Stop().
  int Port() const;
  // ws://host:port/ — what a client should connect to.
  std::string Url() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct WsClientOptions {
  std::string url; // e.g. WsServerTransport::Url() handed over via argv/env
  std::uint32_t minRetryMs = 100;
  std::uint32_t maxRetryMs = 2000;
  int handshakeTimeoutSecs = 5;
  // WebSocket ping every N seconds (0 = off). Keeps idle connections through
  // proxies/tunnels alive; the loopback IPC bus does not need it.
  int pingIntervalSecs = 0;
  // wss:// only: PEM bundle of trusted CAs. Empty = $SSL_CERT_FILE, then the
  // usual distro bundle paths, then OpenSSL's built-in default.
  std::string caFile;
};

// True when this build can dial wss:// (IXWebSocket built with OpenSSL).
bool WsTlsAvailable();

// CA bundle for TLS: `configured`, else $SSL_CERT_FILE / $NIX_SSL_CERT_FILE,
// else cacert.pem next to the executable (Windows packages ship one: MSYS2's
// OpenSSL looks for CAs in the MSYS2 tree, which users don't have), else the
// usual distro paths, else "SYSTEM" (OpenSSL's built-in default).
std::string ResolveCaFile(const std::string &configured);

// Connects in the background and keeps reconnecting until Stop(). Close()
// drops the current connection but the client will dial again; call Stop()
// to go away for good.
class WsClientTransport : public ITransport {
public:
  // From the client's side the server is always this connection id.
  static constexpr ConnectionId kServerId = 1;

  explicit WsClientTransport(WsClientOptions options);
  ~WsClientTransport() override;

  void SetCallbacks(TransportCallbacks callbacks) override;
  bool Start() override;
  void Stop() override;
  bool SendText(ConnectionId to, const std::string &text) override;
  bool SendBinary(ConnectionId to,
                  std::span<const std::uint8_t> data) override;
  void Close(ConnectionId who, const std::string &reason) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ilmeee::net
