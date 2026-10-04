#include "../../../../include/core_engine/net/WsTransport.hpp"

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

// Callbacks are installed before Start() and only read afterwards, so the IO
// threads read them without a lock (thread creation orders the write first).

namespace ilmeee::net {

namespace {

// Port selection races: getFreePort() releases the port before we bind it.
constexpr int kPortAttempts = 8;

std::string CloseReason(const ix::WebSocketCloseInfo &info) {
  if (!info.reason.empty())
    return info.reason;
  return "closed (code " + std::to_string(info.code) + ")";
}

} // namespace

// ==== server ==============================================================

struct WsServerTransport::Impl {
  WsServerOptions options;
  TransportCallbacks callbacks;
  std::unique_ptr<ix::WebSocketServer> server;
  std::atomic<int> port{0};
  std::atomic<ConnectionId> nextId{1};

  // weak_ptr: the server owns each socket and frees it when the connection
  // thread ends. Senders lock() it, which keeps it alive for the send.
  std::mutex socketsMutex;
  std::unordered_map<ConnectionId, std::weak_ptr<ix::WebSocket>> sockets;

  std::shared_ptr<ix::WebSocket> Find(ConnectionId id) {
    std::lock_guard<std::mutex> lock(socketsMutex);
    auto it = sockets.find(id);
    return it == sockets.end() ? nullptr : it->second.lock();
  }

  void OnConnection(std::weak_ptr<ix::WebSocket> weak) {
    auto ws = weak.lock();
    if (!ws)
      return;
    ConnectionId id = nextId++;
    ws->setOnMessageCallback([this, id, weak](const ix::WebSocketMessagePtr &msg) {
      switch (msg->type) {
      case ix::WebSocketMessageType::Open: {
        {
          std::lock_guard<std::mutex> lock(socketsMutex);
          sockets[id] = weak;
        }
        if (callbacks.onOpen)
          callbacks.onOpen(id);
        break;
      }
      case ix::WebSocketMessageType::Message:
        if (msg->binary) {
          if (callbacks.onBinary)
            callbacks.onBinary(
                id, std::vector<std::uint8_t>(msg->str.begin(), msg->str.end()));
        } else if (callbacks.onText) {
          callbacks.onText(id, msg->str);
        }
        break;
      case ix::WebSocketMessageType::Close: {
        {
          std::lock_guard<std::mutex> lock(socketsMutex);
          sockets.erase(id);
        }
        if (callbacks.onClose)
          callbacks.onClose(id, CloseReason(msg->closeInfo));
        break;
      }
      case ix::WebSocketMessageType::Error:
        if (callbacks.onError)
          callbacks.onError("connection #" + std::to_string(id) + ": " +
                            msg->errorInfo.reason);
        break;
      default:
        break; // Ping/Pong/Fragment: handled inside IXWebSocket.
      }
    });
  }
};

WsServerTransport::WsServerTransport(WsServerOptions options)
    : impl_(std::make_unique<Impl>()) {
  impl_->options = std::move(options);
}

WsServerTransport::~WsServerTransport() { Stop(); }

void WsServerTransport::SetCallbacks(TransportCallbacks callbacks) {
  impl_->callbacks = std::move(callbacks);
}

bool WsServerTransport::Start() {
  if (impl_->server)
    return true;
  ix::initNetSystem();

  const WsServerOptions &o = impl_->options;
  const int attempts = o.port == 0 ? kPortAttempts : 1;
  std::string lastError;

  for (int i = 0; i < attempts; ++i) {
    int port = o.port != 0 ? o.port : ix::getFreePort();
    auto server = std::make_unique<ix::WebSocketServer>(
        port, o.host, ix::SocketServer::kDefaultTcpBacklog, o.maxConnections);
    // Loopback IPC: compression costs more CPU than it saves bandwidth.
    server->disablePerMessageDeflate();
    server->setOnConnectionCallback(
        [impl = impl_.get()](std::weak_ptr<ix::WebSocket> ws,
                             std::shared_ptr<ix::ConnectionState>) {
          impl->OnConnection(std::move(ws));
        });

    auto [ok, error] = server->listen();
    if (!ok) {
      lastError = error;
      continue;
    }
    server->start();
    impl_->server = std::move(server);
    impl_->port = port;
    return true;
  }

  if (impl_->callbacks.onError)
    impl_->callbacks.onError("listen on " + o.host + " failed: " + lastError);
  return false;
}

void WsServerTransport::Stop() {
  if (!impl_->server)
    return;
  // Closes every client (each reports onClose) and joins the IO threads.
  impl_->server->stop();
  impl_->server.reset();
  impl_->port = 0;
  std::lock_guard<std::mutex> lock(impl_->socketsMutex);
  impl_->sockets.clear();
}

bool WsServerTransport::SendText(ConnectionId to, const std::string &text) {
  auto ws = impl_->Find(to);
  // Encode() guarantees valid UTF-8, so skip IXWebSocket's re-validation.
  return ws && ws->sendUtf8Text(text).success;
}

bool WsServerTransport::SendBinary(ConnectionId to,
                                   std::span<const std::uint8_t> data) {
  auto ws = impl_->Find(to);
  return ws && ws->sendBinary(ix::IXWebSocketSendData(
                                  reinterpret_cast<const char *>(data.data()),
                                  data.size()))
                   .success;
}

void WsServerTransport::Close(ConnectionId who, const std::string &reason) {
  if (auto ws = impl_->Find(who))
    ws->close(ix::WebSocketCloseConstants::kNormalClosureCode, reason);
}

int WsServerTransport::Port() const { return impl_->port; }

std::string WsServerTransport::Url() const {
  // A wildcard bind is not something a client can dial; loopback reaches it.
  const std::string &h = impl_->options.host;
  std::string host = (h == "0.0.0.0" || h.empty()) ? "127.0.0.1" : h;
  return "ws://" + host + ":" + std::to_string(impl_->port.load()) + "/";
}

// ==== client ==============================================================

struct WsClientTransport::Impl {
  WsClientOptions options;
  TransportCallbacks callbacks;
  ix::WebSocket socket;
  bool started = false;
  // Set while Stop() tears the socket down: the "cancelled" error that
  // produces is expected, and it will not be retried.
  std::atomic<bool> stopping{false};
  std::atomic<bool> open{false};
  // While the server is not up yet every retry fails; report the first
  // failure of a streak only, not one line per retry.
  std::atomic<bool> errorReported{false};
};

WsClientTransport::WsClientTransport(WsClientOptions options)
    : impl_(std::make_unique<Impl>()) {
  impl_->options = std::move(options);
}

WsClientTransport::~WsClientTransport() { Stop(); }

void WsClientTransport::SetCallbacks(TransportCallbacks callbacks) {
  impl_->callbacks = std::move(callbacks);
}

bool WsClientTransport::Start() {
  if (impl_->started)
    return true;
  if (impl_->options.url.empty()) {
    if (impl_->callbacks.onError)
      impl_->callbacks.onError("no server URL configured");
    return false;
  }
  ix::initNetSystem();

  Impl *impl = impl_.get();
  impl->stopping = false;
  ix::WebSocket &ws = impl->socket;
  ws.setUrl(impl->options.url);
  ws.setHandshakeTimeout(impl->options.handshakeTimeoutSecs);
  ws.disablePerMessageDeflate();
  ws.enableAutomaticReconnection();
  ws.setMinWaitBetweenReconnectionRetries(impl->options.minRetryMs);
  ws.setMaxWaitBetweenReconnectionRetries(impl->options.maxRetryMs);
  ws.setOnMessageCallback([impl](const ix::WebSocketMessagePtr &msg) {
    const TransportCallbacks &cb = impl->callbacks;
    switch (msg->type) {
    case ix::WebSocketMessageType::Open:
      impl->open = true;
      impl->errorReported = false;
      if (cb.onOpen)
        cb.onOpen(kServerId);
      break;
    case ix::WebSocketMessageType::Message:
      if (msg->binary) {
        if (cb.onBinary)
          cb.onBinary(kServerId, std::vector<std::uint8_t>(msg->str.begin(),
                                                           msg->str.end()));
      } else if (cb.onText) {
        cb.onText(kServerId, msg->str);
      }
      break;
    case ix::WebSocketMessageType::Close:
      if (impl->open.exchange(false) && cb.onClose)
        cb.onClose(kServerId, CloseReason(msg->closeInfo));
      break;
    case ix::WebSocketMessageType::Error:
      if (impl->stopping)
        break;
      if (!impl->errorReported.exchange(true) && cb.onError)
        cb.onError("cannot reach " + impl->options.url + ": " +
                   msg->errorInfo.reason + " (retrying)");
      break;
    default:
      break;
    }
  });
  ws.start();
  impl->started = true;
  return true;
}

void WsClientTransport::Stop() {
  if (!impl_->started)
    return;
  impl_->started = false;
  impl_->stopping = true;
  // Joins the IO thread; a live connection reports onClose on the way out.
  impl_->socket.stop();
}

bool WsClientTransport::SendText(ConnectionId to, const std::string &text) {
  if (to != kServerId || !impl_->open)
    return false;
  return impl_->socket.sendUtf8Text(text).success;
}

bool WsClientTransport::SendBinary(ConnectionId to,
                                   std::span<const std::uint8_t> data) {
  if (to != kServerId || !impl_->open)
    return false;
  return impl_->socket
      .sendBinary(ix::IXWebSocketSendData(
          reinterpret_cast<const char *>(data.data()), data.size()))
      .success;
}

void WsClientTransport::Close(ConnectionId, const std::string &reason) {
  impl_->socket.close(ix::WebSocketCloseConstants::kNormalClosureCode, reason);
}

} // namespace ilmeee::net
