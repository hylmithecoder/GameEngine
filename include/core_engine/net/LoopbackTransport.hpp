#pragma once
// In-process transport: a LoopbackServer and any number of LoopbackClients
// wired directly together, no sockets. Used by the protocol tests, and usable
// whenever both ends of the bus end up living in one process.
//
// Delivery is synchronous (Send* invokes the peer's callback before
// returning); MessageBus only enqueues from callbacks, so that is safe.

#include "ITransport.hpp"
#include <mutex>
#include <unordered_map>

namespace ilmeee::net {

class LoopbackClient;

class LoopbackServer : public ITransport {
public:
  void SetCallbacks(TransportCallbacks callbacks) override;
  bool Start() override;
  void Stop() override;
  bool SendText(ConnectionId to, const std::string &text) override;
  bool SendBinary(ConnectionId to,
                  std::span<const std::uint8_t> data) override;
  void Close(ConnectionId who, const std::string &reason) override;

private:
  friend class LoopbackClient;
  ConnectionId Attach(LoopbackClient *client);
  void Detach(ConnectionId id, const std::string &reason);
  LoopbackClient *Find(ConnectionId id);

  std::mutex mutex_;
  TransportCallbacks callbacks_;
  bool running_ = false;
  ConnectionId nextId_ = 1;
  std::unordered_map<ConnectionId, LoopbackClient *> clients_;
};

class LoopbackClient : public ITransport {
public:
  // From the client's side the server is always this connection id.
  static constexpr ConnectionId kServerId = 1;

  explicit LoopbackClient(LoopbackServer &server) : server_(server) {}
  ~LoopbackClient() override { Stop(); }

  void SetCallbacks(TransportCallbacks callbacks) override;
  bool Start() override;
  void Stop() override;
  bool SendText(ConnectionId to, const std::string &text) override;
  bool SendBinary(ConnectionId to,
                  std::span<const std::uint8_t> data) override;
  void Close(ConnectionId who, const std::string &reason) override;

private:
  friend class LoopbackServer;
  // Called by the server when it drops this client.
  void Disconnected(const std::string &reason);

  LoopbackServer &server_;
  std::mutex mutex_;
  TransportCallbacks callbacks_;
  ConnectionId idOnServer_ = kInvalidConnection;
};

} // namespace ilmeee::net
