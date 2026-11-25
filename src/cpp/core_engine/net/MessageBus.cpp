#include "../../../../include/core_engine/net/MessageBus.hpp"
#include <iostream>

#ifdef _WIN32
#include <process.h>
#define ILMEEE_GETPID _getpid
#else
#include <unistd.h>
#define ILMEEE_GETPID getpid
#endif

namespace ilmeee::net {

namespace {

constexpr const char *kHello = "hello";
constexpr const char *kWelcome = "welcome";
constexpr const char *kBye = "bye";

bool IsReserved(const std::string &type) {
  return type == kHello || type == kWelcome || type == kBye;
}

// Constant-time so the token cannot be recovered byte by byte from timing.
bool TokensEqual(const std::string &a, const std::string &b) {
  if (a.size() != b.size())
    return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i)
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

} // namespace

MessageBus::MessageBus(ITransport &transport, BusConfig config)
    : transport_(transport), config_(std::move(config)) {
  log_ = [](BusLogLevel level, const std::string &text) {
    const char *tag = level == BusLogLevel::Error     ? "[net][ERROR] "
                      : level == BusLogLevel::Warning ? "[net][WARNING] "
                                                      : "[net] ";
    std::cerr << tag << text << '\n';
  };
}

MessageBus::~MessageBus() { Stop(); }

bool MessageBus::Start() {
  if (started_)
    return true;

  if (config_.role == BusRole::Server && config_.token.empty())
    Log(BusLogLevel::Warning,
        "server started without a token: any local process may connect");

  TransportCallbacks cb;
  cb.onOpen = [this](ConnectionId c) {
    // The hello goes out from the transport thread, not from Poll(), so it is
    // guaranteed to be the first frame on the connection even if the main
    // thread is busy.
    if (config_.role == BusRole::Client)
      SendHello(c);
    Enqueue({InboxItem::Kind::Open, c, {}, {}});
  };
  cb.onClose = [this](ConnectionId c, const std::string &reason) {
    Enqueue({InboxItem::Kind::Close, c, reason, {}});
  };
  cb.onText = [this](ConnectionId c, std::string text) {
    Enqueue({InboxItem::Kind::Text, c, std::move(text), {}});
  };
  cb.onBinary = [this](ConnectionId c, std::vector<std::uint8_t> data) {
    Enqueue({InboxItem::Kind::Binary, c, {}, std::move(data)});
  };
  cb.onError = [this](const std::string &what) {
    // Logged straight from the IO thread, so the sink must tolerate being
    // called off the main thread.
    Log(BusLogLevel::Warning, "transport: " + what);
  };
  transport_.SetCallbacks(std::move(cb));

  if (!transport_.Start()) {
    Log(BusLogLevel::Error, "transport failed to start");
    return false;
  }
  started_ = true;
  ready_ = config_.role == BusRole::Server;
  return true;
}

void MessageBus::Stop() {
  if (!started_)
    return;
  started_ = false;
  transport_.Stop();
  ready_ = false;
  serverConnection_ = kInvalidConnection;
  peers_.clear();
  pending_.clear();
  outbox_.clear();
  std::lock_guard<std::mutex> lock(inboxMutex_);
  inbox_.clear();
}

void MessageBus::Enqueue(InboxItem item) {
  std::lock_guard<std::mutex> lock(inboxMutex_);
  inbox_.push_back(std::move(item));
}

void MessageBus::Poll() {
  std::deque<InboxItem> batch;
  {
    std::lock_guard<std::mutex> lock(inboxMutex_);
    batch.swap(inbox_);
  }

  for (auto &item : batch) {
    // A handler may have called Stop(); don't keep dispatching into a bus
    // that has already forgotten its peers.
    if (!started_)
      return;
    switch (item.kind) {
    case InboxItem::Kind::Open:
      HandleOpen(item.connection);
      break;
    case InboxItem::Kind::Close:
      HandleClose(item.connection, item.text);
      break;
    case InboxItem::Kind::Text:
      HandleText(item.connection, item.text);
      break;
    case InboxItem::Kind::Binary:
      HandleBinary(item.connection, std::move(item.binary));
      break;
    }
  }

  ExpireRequests();
}

// ---- inbound -------------------------------------------------------------

void MessageBus::HandleOpen(ConnectionId c) {
  Peer peer;
  peer.info.connection = c;
  peers_[c] = peer;
}

void MessageBus::HandleClose(ConnectionId c, const std::string &reason) {
  auto it = peers_.find(c);
  if (it == peers_.end())
    return;
  Peer peer = it->second;
  peers_.erase(it);

  FailPendingFor(c, "peer disconnected");

  if (config_.role == BusRole::Client && c == serverConnection_) {
    ready_ = false;
    serverConnection_ = kInvalidConnection;
  }

  if (peer.authenticated && onPeerLeft_)
    onPeerLeft_(peer.info, reason);
}

void MessageBus::HandleText(ConnectionId c, const std::string &text) {
  auto it = peers_.find(c);
  if (it == peers_.end())
    return; // Already closed/refused; late frames are expected.

  std::string error;
  auto msg = Decode(text, &error);
  if (!msg) {
    Log(BusLogLevel::Warning,
        "dropping malformed message from #" + std::to_string(c) + ": " + error);
    if (!it->second.authenticated && config_.role == BusRole::Server)
      Refuse(c, "malformed handshake: " + error);
    return;
  }

  if (msg->type == kBye) {
    std::string reason = msg->payload.value("reason", std::string("no reason"));
    if (config_.role == BusRole::Client && !msg->payload.value("retry", true)) {
      // Refused for something reconnecting cannot fix (bad token, protocol
      // mismatch). Stop, or a reconnecting transport would knock forever.
      Log(BusLogLevel::Error, "refused by core: " + reason);
      Stop();
      return;
    }
    Log(BusLogLevel::Warning,
        "peer #" + std::to_string(c) + " said bye: " + reason);
    return; // The close event follows from the transport.
  }

  if (config_.role == BusRole::Server && !it->second.authenticated) {
    if (msg->type == kHello && msg->kind == MessageKind::Event)
      HandleHello(c, *msg);
    else
      Refuse(c, "handshake required before '" + msg->type + "'");
    return;
  }

  if (config_.role == BusRole::Client && msg->type == kWelcome) {
    HandleWelcome(c, *msg);
    return;
  }

  if (config_.role == BusRole::Client && !ready_) {
    Log(BusLogLevel::Warning,
        "ignoring '" + msg->type + "' received before welcome");
    return;
  }

  Dispatch(c, *msg);
}

void MessageBus::HandleBinary(ConnectionId c, std::vector<std::uint8_t> data) {
  auto it = peers_.find(c);
  if (it == peers_.end())
    return;
  if (config_.role == BusRole::Server && !it->second.authenticated) {
    Refuse(c, "handshake required before binary data");
    return;
  }
  if (onBinary_)
    onBinary_(c, std::move(data));
}

void MessageBus::HandleHello(ConnectionId c, const Message &msg) {
  const json &p = msg.payload;
  int protocol = p.value("protocol", 0);
  if (protocol != kProtocolVersion) {
    Refuse(c, "protocol " + std::to_string(protocol) + " not supported, core speaks " +
                  std::to_string(kProtocolVersion));
    return;
  }
  if (!config_.token.empty() &&
      !TokensEqual(p.value("token", std::string()), config_.token)) {
    Refuse(c, "invalid token");
    return;
  }

  Peer &peer = peers_[c];
  peer.authenticated = true;
  peer.info.role = p.value("role", std::string("unknown"));
  peer.info.pid = p.value("pid", 0L);

  SendMessage(c, Message::Event(kWelcome, {{"protocol", kProtocolVersion},
                                           {"role", "core"},
                                           {"pid", static_cast<long>(ILMEEE_GETPID())},
                                           {"connection", c}}));
  Log(BusLogLevel::Info, "peer #" + std::to_string(c) + " joined as '" +
                             peer.info.role + "' (pid " +
                             std::to_string(peer.info.pid) + ")");
  if (onPeerJoined_)
    onPeerJoined_(peer.info);
}

void MessageBus::HandleWelcome(ConnectionId c, const Message &msg) {
  Peer &peer = peers_[c];
  peer.authenticated = true;
  peer.info.role = msg.payload.value("role", std::string("core"));
  peer.info.pid = msg.payload.value("pid", 0L);

  ready_ = true;
  serverConnection_ = c;

  // Flush what was emitted while we were still connecting. Requests were
  // tracked against "no connection yet"; bind them to the server now so a
  // disconnect fails them correctly.
  std::vector<Message> queued;
  queued.swap(outbox_);
  for (auto &m : queued) {
    if (m.kind == MessageKind::Request) {
      auto p = pending_.find(m.id);
      if (p == pending_.end())
        continue; // Timed out while queued.
      p->second.to = c;
    }
    SendMessage(c, m);
  }

  if (onPeerJoined_)
    onPeerJoined_(peer.info);
}

void MessageBus::Dispatch(ConnectionId c, const Message &msg) {
  switch (msg.kind) {
  case MessageKind::Event: {
    auto h = eventHandlers_.find(msg.type);
    if (h == eventHandlers_.end()) {
      Log(BusLogLevel::Warning, "no handler for event '" + msg.type + "'");
      return;
    }
    try {
      h->second(c, msg.payload);
    } catch (const std::exception &e) {
      Log(BusLogLevel::Error,
          "handler for '" + msg.type + "' threw: " + e.what());
    }
    return;
  }

  case MessageKind::Request: {
    auto h = requestHandlers_.find(msg.type);
    if (h == requestHandlers_.end()) {
      SendMessage(c, Message::Fail(msg, "unknown request '" + msg.type + "'"));
      return;
    }
    RequestResult result;
    try {
      result = h->second(c, msg.payload);
    } catch (const std::exception &e) {
      result = RequestResult::Fail(e.what());
    }
    SendMessage(c, result.ok ? Message::Reply(msg, std::move(result.payload))
                             : Message::Fail(msg, std::move(result.error)));
    return;
  }

  case MessageKind::Response: {
    auto p = pending_.find(msg.id);
    if (p == pending_.end() || p->second.to != c) {
      // Usually a reply that arrived after its timeout.
      Log(BusLogLevel::Warning,
          "dropping unmatched response #" + std::to_string(msg.id) + " ('" +
              msg.type + "')");
      return;
    }
    ResponseHandler cb = std::move(p->second.callback);
    pending_.erase(p);
    if (cb)
      cb(Response{msg.ok, msg.payload, msg.error});
    return;
  }
  }
}

void MessageBus::ExpireRequests() {
  auto now = std::chrono::steady_clock::now();
  std::vector<ResponseHandler> expired;
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (it->second.deadline <= now) {
      Log(BusLogLevel::Warning, "request #" + std::to_string(it->first) +
                                    " ('" + it->second.type + "') timed out");
      expired.push_back(std::move(it->second.callback));
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
  // Invoked after the loop: a callback may issue a new request.
  for (auto &cb : expired)
    if (cb)
      cb(Response{false, json::object(), "timeout"});
}

void MessageBus::FailPendingFor(ConnectionId c, const std::string &error) {
  std::vector<ResponseHandler> failed;
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (it->second.to == c) {
      failed.push_back(std::move(it->second.callback));
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto &cb : failed)
    if (cb)
      cb(Response{false, json::object(), error});
}

// ---- registration --------------------------------------------------------

void MessageBus::On(const std::string &type, EventHandler handler) {
  if (IsReserved(type)) {
    Log(BusLogLevel::Error, "'" + type + "' is reserved for the handshake");
    return;
  }
  eventHandlers_[type] = std::move(handler);
}

void MessageBus::OnRequest(const std::string &type, RequestHandler handler) {
  if (IsReserved(type)) {
    Log(BusLogLevel::Error, "'" + type + "' is reserved for the handshake");
    return;
  }
  requestHandlers_[type] = std::move(handler);
}

// ---- outbound ------------------------------------------------------------

bool MessageBus::SendMessage(ConnectionId to, const Message &msg) {
  return transport_.SendText(to, Encode(msg));
}

void MessageBus::SendHello(ConnectionId c) {
  SendMessage(c, Message::Event(kHello, {{"protocol", kProtocolVersion},
                                         {"role", config_.clientRole},
                                         {"pid", static_cast<long>(ILMEEE_GETPID())},
                                         {"token", config_.token}}));
}

void MessageBus::Refuse(ConnectionId c, const std::string &reason) {
  Log(BusLogLevel::Warning,
      "refusing connection #" + std::to_string(c) + ": " + reason);
  SendMessage(c, Message::Event(kBye, {{"reason", reason}, {"retry", false}}));
  peers_.erase(c);
  transport_.Close(c, reason);
}

std::uint64_t
MessageBus::TrackRequest(ConnectionId to, const std::string &type,
                         ResponseHandler cb,
                         std::optional<std::chrono::milliseconds> timeout) {
  std::uint64_t id = nextRequestId_++;
  pending_[id] = Pending{to, type, std::move(cb),
                         std::chrono::steady_clock::now() +
                             timeout.value_or(config_.defaultTimeout)};
  return id;
}

bool MessageBus::Emit(ConnectionId to, const std::string &type, json payload) {
  if (IsReserved(type))
    return false;
  auto it = peers_.find(to);
  if (it == peers_.end() || !it->second.authenticated)
    return false;
  return SendMessage(to, Message::Event(type, std::move(payload)));
}

void MessageBus::Broadcast(const std::string &type, json payload) {
  if (IsReserved(type))
    return;
  std::string encoded = Encode(Message::Event(type, std::move(payload)));
  for (auto &[c, peer] : peers_)
    if (peer.authenticated)
      transport_.SendText(c, encoded);
}

std::uint64_t
MessageBus::Request(ConnectionId to, const std::string &type, json payload,
                    ResponseHandler onResponse,
                    std::optional<std::chrono::milliseconds> timeout) {
  auto it = peers_.find(to);
  if (IsReserved(type) || it == peers_.end() || !it->second.authenticated) {
    if (onResponse)
      onResponse(Response{false, json::object(), "not connected"});
    return 0;
  }
  std::uint64_t id = TrackRequest(to, type, std::move(onResponse), timeout);
  if (!SendMessage(to, Message::Request(type, id, std::move(payload)))) {
    FailPendingFor(to, "send failed");
    return 0;
  }
  return id;
}

bool MessageBus::SendBinary(ConnectionId to,
                            std::span<const std::uint8_t> data) {
  auto it = peers_.find(to);
  if (it == peers_.end() || !it->second.authenticated)
    return false;
  return transport_.SendBinary(to, data);
}

bool MessageBus::Emit(const std::string &type, json payload) {
  if (config_.role != BusRole::Client) {
    Log(BusLogLevel::Error, "Emit without a target is client-only; use "
                            "Emit(connection, ...) or Broadcast on the server");
    return false;
  }
  if (IsReserved(type) || !started_)
    return false;
  Message msg = Message::Event(type, std::move(payload));
  if (!ready_) {
    outbox_.push_back(std::move(msg));
    return true;
  }
  return SendMessage(serverConnection_, msg);
}

std::uint64_t
MessageBus::Request(const std::string &type, json payload,
                    ResponseHandler onResponse,
                    std::optional<std::chrono::milliseconds> timeout) {
  if (config_.role != BusRole::Client || IsReserved(type) || !started_) {
    if (config_.role != BusRole::Client)
      Log(BusLogLevel::Error, "Request without a target is client-only");
    if (onResponse)
      onResponse(Response{false, json::object(), "invalid request"});
    return 0;
  }
  if (ready_)
    return Request(serverConnection_, type, std::move(payload),
                   std::move(onResponse), timeout);

  std::uint64_t id = TrackRequest(kInvalidConnection, type,
                                  std::move(onResponse), timeout);
  outbox_.push_back(Message::Request(type, id, std::move(payload)));
  return id;
}

std::vector<PeerInfo> MessageBus::Peers() const {
  std::vector<PeerInfo> out;
  for (auto &[c, peer] : peers_)
    if (peer.authenticated)
      out.push_back(peer.info);
  return out;
}

ConnectionId MessageBus::PeerByRole(const std::string &role) const {
  for (auto &[c, peer] : peers_)
    if (peer.authenticated && peer.info.role == role)
      return c;
  return kInvalidConnection;
}

void MessageBus::Log(BusLogLevel level, const std::string &text) const {
  if (log_)
    log_(level, text);
}

} // namespace ilmeee::net
