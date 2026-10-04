#pragma once
// Message layer on top of an ITransport: handshake, event dispatch,
// request/response correlation and timeouts.
//
// Topology: exactly one Server bus (the core process, GameEngineSDL) and any
// number of Client buses (HandlerIlmeeeEngine, Hub, external tools). Clients
// only ever talk to the server.
//
// Handshake: a client's first message is the reserved event
//   hello   {protocol, role, pid, token}
// and the server answers with
//   welcome {protocol, role:"core", pid, connection}
// or, on a bad token / protocol mismatch,
//   bye     {reason, retry:false}  followed by closing the connection.
// A client that gets bye with retry:false stops its bus (IsReady() stays
// false) instead of letting a reconnecting transport dial again.
// Anything else arriving on a connection that has not said hello is dropped
// and the connection is closed.
//
// Threading: transport callbacks only enqueue. All handlers and response
// callbacks run inside Poll(), on whichever thread calls it — call Poll() once
// per frame from the main loop, since handlers touch scene/Vulkan/ImGui state.
// Every other public method must be called from that same thread.

#include "ITransport.hpp"
#include "Message.hpp"
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ilmeee::net {

enum class BusRole { Server, Client };

struct BusConfig {
  BusRole role = BusRole::Client;
  // Shared secret handed from the core to its children (argv/env). The server
  // rejects clients that do not present it; empty disables the check.
  std::string token;
  // Client only: what this process announces itself as ("engine", "hub", ...).
  std::string clientRole;
  std::chrono::milliseconds defaultTimeout{5000};
};

struct PeerInfo {
  ConnectionId connection = kInvalidConnection;
  std::string role;
  long pid = 0;
};

struct Response {
  bool ok = true;
  json payload = json::object();
  std::string error;
};

// What a request handler returns; becomes the reply.
struct RequestResult {
  bool ok = true;
  json payload = json::object();
  std::string error;

  static RequestResult Ok(json payload = json::object()) {
    return {true, std::move(payload), {}};
  }
  static RequestResult Fail(std::string error) {
    return {false, json::object(), std::move(error)};
  }
};

enum class BusLogLevel { Info, Warning, Error };

class MessageBus {
public:
  using EventHandler =
      std::function<void(ConnectionId from, const json &payload)>;
  using RequestHandler =
      std::function<RequestResult(ConnectionId from, const json &payload)>;
  using ResponseHandler = std::function<void(const Response &)>;
  using PeerHandler = std::function<void(const PeerInfo &)>;
  using PeerLeftHandler =
      std::function<void(const PeerInfo &, const std::string &reason)>;
  using BinaryHandler =
      std::function<void(ConnectionId from, std::vector<std::uint8_t> data)>;
  using LogSink = std::function<void(BusLogLevel, const std::string &)>;

  // The transport must outlive the bus.
  MessageBus(ITransport &transport, BusConfig config);
  ~MessageBus();

  MessageBus(const MessageBus &) = delete;
  MessageBus &operator=(const MessageBus &) = delete;

  bool Start();
  // Stops the transport and drops pending requests without invoking their
  // callbacks (they may point at objects already being torn down).
  void Stop();

  // Drain everything the transport delivered since the last call, dispatch it,
  // and expire timed-out requests.
  void Poll();

  // ---- registration ------------------------------------------------------
  void On(const std::string &type, EventHandler handler);
  void OnRequest(const std::string &type, RequestHandler handler);
  void OnPeerJoined(PeerHandler handler) { onPeerJoined_ = std::move(handler); }
  void OnPeerLeft(PeerLeftHandler handler) { onPeerLeft_ = std::move(handler); }
  void OnBinary(BinaryHandler handler) { onBinary_ = std::move(handler); }
  // Set before Start(). Transport errors are logged from the IO thread, so
  // the sink must be safe to call from any thread.
  void SetLogSink(LogSink sink) { log_ = std::move(sink); }

  // ---- sending (server side, or explicit target) -------------------------
  bool Emit(ConnectionId to, const std::string &type,
            json payload = json::object());
  void Broadcast(const std::string &type, json payload = json::object());
  // Returns the request id, or 0 if it could not be sent (the callback is then
  // invoked immediately with ok=false).
  std::uint64_t Request(ConnectionId to, const std::string &type, json payload,
                        ResponseHandler onResponse,
                        std::optional<std::chrono::milliseconds> timeout = {});
  bool SendBinary(ConnectionId to, std::span<const std::uint8_t> data);

  // ---- sending (client side: always to the server) -----------------------
  // Queued until the server has welcomed us, then flushed in order.
  bool Emit(const std::string &type, json payload = json::object());
  std::uint64_t Request(const std::string &type, json payload,
                        ResponseHandler onResponse,
                        std::optional<std::chrono::milliseconds> timeout = {});

  // ---- state -------------------------------------------------------------
  // Client: welcomed by the server. Server: listening.
  bool IsReady() const { return ready_; }
  // Client: connection id of the server once welcomed, else kInvalidConnection.
  ConnectionId Server() const { return serverConnection_; }
  // Server: authenticated clients. Client: the server, once welcomed.
  std::vector<PeerInfo> Peers() const;
  // First authenticated peer that announced `role`, else kInvalidConnection.
  ConnectionId PeerByRole(const std::string &role) const;

private:
  struct InboxItem {
    enum class Kind { Open, Close, Text, Binary } kind;
    ConnectionId connection;
    std::string text; // Text payload, or Close reason.
    std::vector<std::uint8_t> binary;
  };

  struct Peer {
    bool authenticated = false;
    PeerInfo info;
  };

  struct Pending {
    ConnectionId to;
    std::string type;
    ResponseHandler callback;
    std::chrono::steady_clock::time_point deadline;
  };

  void Enqueue(InboxItem item);
  void HandleOpen(ConnectionId c);
  void HandleClose(ConnectionId c, const std::string &reason);
  void HandleText(ConnectionId c, const std::string &text);
  void HandleBinary(ConnectionId c, std::vector<std::uint8_t> data);
  void HandleHello(ConnectionId c, const Message &msg);
  void HandleWelcome(ConnectionId c, const Message &msg);
  void Dispatch(ConnectionId c, const Message &msg);
  void ExpireRequests();
  void FailPendingFor(ConnectionId c, const std::string &error);

  bool SendMessage(ConnectionId to, const Message &msg);
  void SendHello(ConnectionId c);
  void Refuse(ConnectionId c, const std::string &reason);
  std::uint64_t TrackRequest(ConnectionId to, const std::string &type,
                             ResponseHandler cb,
                             std::optional<std::chrono::milliseconds> timeout);
  void Log(BusLogLevel level, const std::string &text) const;

  ITransport &transport_;
  BusConfig config_;
  bool started_ = false;
  bool ready_ = false;
  ConnectionId serverConnection_ = kInvalidConnection;

  std::mutex inboxMutex_;
  std::deque<InboxItem> inbox_;

  std::unordered_map<ConnectionId, Peer> peers_;
  std::unordered_map<std::string, EventHandler> eventHandlers_;
  std::unordered_map<std::string, RequestHandler> requestHandlers_;
  std::unordered_map<std::uint64_t, Pending> pending_;
  std::uint64_t nextRequestId_ = 1;

  // Client only: messages emitted before the welcome arrived.
  std::vector<Message> outbox_;

  PeerHandler onPeerJoined_;
  PeerLeftHandler onPeerLeft_;
  BinaryHandler onBinary_;
  LogSink log_;
};

} // namespace ilmeee::net
