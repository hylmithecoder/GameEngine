#include "../../../../include/core_engine/net/LoopbackTransport.hpp"
#include <vector>

// Locks are never held while invoking callbacks: a client's onOpen sends its
// hello straight back through the server, which would self-deadlock.

namespace ilmeee::net {

// ---- server --------------------------------------------------------------

void LoopbackServer::SetCallbacks(TransportCallbacks callbacks) {
  std::lock_guard<std::mutex> lock(mutex_);
  callbacks_ = std::move(callbacks);
}

bool LoopbackServer::Start() {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = true;
  return true;
}

void LoopbackServer::Stop() {
  std::vector<ConnectionId> ids;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    for (auto &[id, _] : clients_)
      ids.push_back(id);
  }
  for (ConnectionId id : ids)
    Detach(id, "server stopped");
}

ConnectionId LoopbackServer::Attach(LoopbackClient *client) {
  ConnectionId id;
  TransportCallbacks cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_)
      return kInvalidConnection;
    id = nextId_++;
    clients_[id] = client;
    cb = callbacks_;
  }
  if (cb.onOpen)
    cb.onOpen(id);
  return id;
}

void LoopbackServer::Detach(ConnectionId id, const std::string &reason) {
  LoopbackClient *client;
  TransportCallbacks cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(id);
    if (it == clients_.end())
      return;
    client = it->second;
    clients_.erase(it);
    cb = callbacks_;
  }
  client->Disconnected(reason);
  if (cb.onClose)
    cb.onClose(id, reason);
}

LoopbackClient *LoopbackServer::Find(ConnectionId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = clients_.find(id);
  return it == clients_.end() ? nullptr : it->second;
}

bool LoopbackServer::SendText(ConnectionId to, const std::string &text) {
  LoopbackClient *client = Find(to);
  if (!client)
    return false;
  TransportCallbacks cb;
  {
    std::lock_guard<std::mutex> lock(client->mutex_);
    cb = client->callbacks_;
  }
  if (cb.onText)
    cb.onText(LoopbackClient::kServerId, text);
  return true;
}

bool LoopbackServer::SendBinary(ConnectionId to,
                                std::span<const std::uint8_t> data) {
  LoopbackClient *client = Find(to);
  if (!client)
    return false;
  TransportCallbacks cb;
  {
    std::lock_guard<std::mutex> lock(client->mutex_);
    cb = client->callbacks_;
  }
  if (cb.onBinary)
    cb.onBinary(LoopbackClient::kServerId,
                std::vector<std::uint8_t>(data.begin(), data.end()));
  return true;
}

void LoopbackServer::Close(ConnectionId who, const std::string &reason) {
  Detach(who, reason);
}

// ---- client --------------------------------------------------------------

void LoopbackClient::SetCallbacks(TransportCallbacks callbacks) {
  std::lock_guard<std::mutex> lock(mutex_);
  callbacks_ = std::move(callbacks);
}

bool LoopbackClient::Start() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (idOnServer_ != kInvalidConnection)
      return true;
  }
  // Attach() fires the server's onOpen first, so the server already knows the
  // connection when our onOpen sends the hello through it. idOnServer_ must be
  // set before that callback, or the hello would have nowhere to go.
  ConnectionId id = server_.Attach(this);
  if (id == kInvalidConnection)
    return false;
  TransportCallbacks cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    idOnServer_ = id;
    cb = callbacks_;
  }
  if (cb.onOpen)
    cb.onOpen(kServerId);
  return true;
}

void LoopbackClient::Stop() {
  ConnectionId id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = idOnServer_;
  }
  if (id != kInvalidConnection)
    server_.Detach(id, "client stopped");
}

void LoopbackClient::Disconnected(const std::string &reason) {
  TransportCallbacks cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    idOnServer_ = kInvalidConnection;
    cb = callbacks_;
  }
  if (cb.onClose)
    cb.onClose(kServerId, reason);
}

bool LoopbackClient::SendText(ConnectionId to, const std::string &text) {
  ConnectionId id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = idOnServer_;
  }
  if (to != kServerId || id == kInvalidConnection)
    return false;
  TransportCallbacks cb;
  {
    std::lock_guard<std::mutex> lock(server_.mutex_);
    cb = server_.callbacks_;
  }
  if (cb.onText)
    cb.onText(id, text);
  return true;
}

bool LoopbackClient::SendBinary(ConnectionId to,
                                std::span<const std::uint8_t> data) {
  ConnectionId id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = idOnServer_;
  }
  if (to != kServerId || id == kInvalidConnection)
    return false;
  TransportCallbacks cb;
  {
    std::lock_guard<std::mutex> lock(server_.mutex_);
    cb = server_.callbacks_;
  }
  if (cb.onBinary)
    cb.onBinary(id, std::vector<std::uint8_t>(data.begin(), data.end()));
  return true;
}

void LoopbackClient::Close(ConnectionId, const std::string &reason) {
  ConnectionId id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = idOnServer_;
  }
  if (id != kInvalidConnection)
    server_.Detach(id, reason);
}

} // namespace ilmeee::net
