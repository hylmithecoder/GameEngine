#include "../../../../include/core_engine/net/CollabClient.hpp"
#include "../../../../include/core_engine/net/MessageBus.hpp"
#include "../../../../include/core_engine/net/WsTransport.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>

namespace ilmeee::net {

const char *ToString(CollabState state) {
  switch (state) {
  case CollabState::Offline: return "Offline";
  case CollabState::Connecting: return "Connecting";
  case CollabState::Joining: return "Joining";
  case CollabState::Joined: return "Joined";
  case CollabState::Failed: return "Failed";
  }
  return "?";
}

CollabOp CollabOp::FromJson(const json &j) {
  CollabOp op;
  if (!j.is_object())
    return op;
  op.seq = j.value("seq", std::uint64_t{0});
  op.from = j.value("from", std::uint32_t{0});
  op.key = j.value("key", std::string());
  op.slot = j.value("slot", std::string());
  op.del = j.value("del", false);
  if (auto it = j.find("data"); it != j.end())
    op.data = *it;
  return op;
}

// ---- CollabLww ------------------------------------------------------------

void CollabLww::NoteSent(const std::string &key, const std::string &slot) {
  ++pending_[Id(key, slot)];
  ++perKey_[key];
}

void CollabLww::Release(const std::string &key, const std::string &slot) {
  if (auto it = pending_.find(Id(key, slot)); it != pending_.end()) {
    if (--it->second <= 0)
      pending_.erase(it);
    if (auto k = perKey_.find(key); k != perKey_.end() && --k->second <= 0)
      perKey_.erase(k);
  }
}

bool CollabLww::HasPending(const std::string &key) const {
  return perKey_.contains(key);
}

CollabLww::Verdict CollabLww::Judge(const CollabOp &op, std::uint32_t self) {
  const std::string slot = op.del ? std::string() : op.slot;
  if (op.from == self && self != 0) {
    Release(op.key, slot);
    return Verdict::OwnEcho;
  }
  if (op.del) {
    if (!HasPending(op.key))
      return Verdict::Apply;
    // Our writes reach the server after this delete. If we only have a
    // delete in flight the outcome is the same; otherwise our put recreates
    // just its slot, so resend the whole object.
    const bool onlyDeletes =
        pending_.contains(Id(op.key, "")) &&
        perKey_.at(op.key) == pending_.at(Id(op.key, ""));
    return onlyDeletes ? Verdict::Skip : Verdict::SkipResend;
  }
  if (pending_.contains(Id(op.key, slot)) || pending_.contains(Id(op.key, "")))
    return Verdict::Skip;
  return Verdict::Apply;
}

// ---- CollabClient ---------------------------------------------------------

struct CollabClient::Shared {
  std::mutex mutex;
  std::deque<std::pair<std::string, bool>> logs;
  std::string refused;
};

CollabClient::CollabClient() : shared_(std::make_shared<Shared>()) {}

CollabClient::~CollabClient() { Stop(); }

void CollabClient::Log(const std::string &message, bool error) {
  if (onLog)
    onLog(message, error);
}

void CollabClient::SetState(CollabState s) { state_ = s; }

const CollabPeer *CollabClient::FindPeer(std::uint32_t id) const {
  for (const auto &p : peers_)
    if (p.id == id)
      return &p;
  return nullptr;
}

bool CollabClient::Start(const CollabOptions &options) {
  Stop();
  options_ = options;
  lastError_.clear();
  if (options_.room.empty()) {
    lastError_ = "room name is empty";
    SetState(CollabState::Failed);
    return false;
  }
  {
    std::lock_guard lock(shared_->mutex);
    shared_->logs.clear();
    shared_->refused.clear();
  }

  WsClientOptions wo;
  wo.url = options_.url;
  wo.minRetryMs = 500;
  wo.maxRetryMs = 10000;
  wo.handshakeTimeoutSecs = 10;
  wo.pingIntervalSecs = 20; // the tunnel drops idle connections
  transport_ = std::make_unique<WsClientTransport>(wo);

  BusConfig bc;
  bc.role = BusRole::Client;
  bc.token = options_.token;
  bc.clientRole = "collab-editor";
  bc.defaultTimeout = std::chrono::milliseconds(10000);
  bus_ = std::make_unique<MessageBus>(*transport_, bc);

  std::weak_ptr<Shared> weak = shared_;
  bus_->SetLogSink([weak](BusLogLevel level, const std::string &text) {
    auto s = weak.lock();
    if (!s)
      return;
    std::lock_guard lock(s->mutex);
    constexpr const char *kRefused = "refused by core: ";
    if (text.rfind(kRefused, 0) == 0)
      s->refused = text.substr(std::char_traits<char>::length(kRefused));
    if (s->logs.size() < 64)
      s->logs.emplace_back(text, level == BusLogLevel::Error);
  });

  bus_->OnPeerJoined([this](const PeerInfo &) {
    // Welcomed by the server (first connect or after a reconnect).
    SendJoin();
  });
  bus_->OnPeerLeft([this](const PeerInfo &, const std::string &reason) {
    if (state_ == CollabState::Offline || state_ == CollabState::Failed)
      return;
    Log("collab: connection lost (" + reason + "), reconnecting", true);
    peers_.clear();
    self_ = 0;
    lww_.Reset();
    SetState(CollabState::Connecting);
    if (onPeersChanged)
      onPeersChanged();
  });

  bus_->On("op", [this](ConnectionId, const json &p) { HandleOp(p); });
  bus_->On("presence", [this](ConnectionId, const json &p) {
    const auto from = p.value("from", std::uint32_t{0});
    for (auto &peer : peers_)
      if (peer.id == from) {
        peer.presence = p.contains("data") ? p["data"] : json();
        if (onPeersChanged)
          onPeersChanged();
        return;
      }
  });
  bus_->On("peer.joined", [this](ConnectionId, const json &p) {
    CollabPeer peer;
    peer.id = p.value("id", std::uint32_t{0});
    peer.name = p.value("name", std::string());
    peer.color = p.value("color", std::uint32_t{0});
    peer.presence = p.contains("presence") ? p["presence"] : json();
    std::erase_if(peers_, [&](const CollabPeer &x) { return x.id == peer.id; });
    Log("collab: " + peer.name + " joined", false);
    peers_.push_back(std::move(peer));
    if (onPeersChanged)
      onPeersChanged();
  });
  bus_->On("peer.left", [this](ConnectionId, const json &p) {
    const auto id = p.value("id", std::uint32_t{0});
    if (const CollabPeer *peer = FindPeer(id))
      Log("collab: " + peer->name + " left", false);
    std::erase_if(peers_, [&](const CollabPeer &x) { return x.id == id; });
    if (onPeersChanged)
      onPeersChanged();
  });
  bus_->On("notice", [this](ConnectionId, const json &p) {
    Log("collab server: " + p.value("message", std::string()),
        p.value("level", std::string()) == "error");
  });

  SetState(CollabState::Connecting);
  if (!bus_->Start()) {
    lastError_ = "could not start connection to " + options_.url;
    {
      std::lock_guard lock(shared_->mutex);
      for (auto &[text, error] : shared_->logs)
        if (error)
          lastError_ = text;
    }
    bus_.reset();
    transport_.reset();
    SetState(CollabState::Failed);
    return false;
  }
  return true;
}

void CollabClient::Stop() {
  if (bus_) {
    if (state_ == CollabState::Joined)
      bus_->Request("room.leave", json::object(), [](const Response &) {});
    bus_->Stop();
  }
  bus_.reset();
  transport_.reset();
  peers_.clear();
  self_ = 0;
  seq_ = 0;
  lww_.Reset();
  ++joinGeneration_;
  if (state_ != CollabState::Failed)
    SetState(CollabState::Offline);
}

void CollabClient::SendJoin() {
  if (!bus_)
    return;
  SetState(CollabState::Joining);
  const std::uint64_t generation = ++joinGeneration_;
  bus_->Request(
      "room.join",
      {{"room", options_.room}, {"name", options_.name}, {"color", options_.color}},
      [this, generation](const Response &r) {
        if (generation != joinGeneration_ || !bus_)
          return; // reconnected or stopped meanwhile
        if (!r.ok) {
          if (!bus_->IsReady())
            return; // the connection dropped; OnPeerLeft handles it
          lastError_ = "join failed: " + r.error;
          Log("collab: " + lastError_, true);
          bus_->Stop();
          SetState(CollabState::Failed);
          return;
        }
        const json &p = r.payload;
        self_ = p.value("self", std::uint32_t{0});
        seq_ = p.value("seq", std::uint64_t{0});
        peers_.clear();
        if (auto it = p.find("peers"); it != p.end() && it->is_array())
          for (const auto &j : *it) {
            CollabPeer peer;
            peer.id = j.value("id", std::uint32_t{0});
            peer.name = j.value("name", std::string());
            peer.color = j.value("color", std::uint32_t{0});
            peer.presence = j.contains("presence") ? j["presence"] : json();
            if (peer.id != self_)
              peers_.push_back(std::move(peer));
          }
        std::vector<CollabOp> state;
        if (auto it = p.find("state"); it != p.end() && it->is_array())
          for (const auto &j : *it)
            state.push_back(CollabOp::FromJson(j));
        lww_.Reset();
        lastError_.clear();
        SetState(CollabState::Joined);
        Log("collab: joined room '" + options_.room + "' (" +
                std::to_string(peers_.size()) + " other peer(s), " +
                std::to_string(state.size()) + " stored value(s))",
            false);
        if (onJoined)
          onJoined(state, p.value("fresh", state.empty()));
        if (onPeersChanged)
          onPeersChanged();
      },
      std::chrono::milliseconds(15000));
}

void CollabClient::HandleOp(const json &payload) {
  if (state_ != CollabState::Joined)
    return;
  CollabOp op = CollabOp::FromJson(payload);
  if (op.key.empty())
    return;
  seq_ = std::max(seq_, op.seq);
  switch (lww_.Judge(op, self_)) {
  case CollabLww::Verdict::Apply:
    if (onOp)
      onOp(op);
    break;
  case CollabLww::Verdict::SkipResend:
    if (onResend)
      onResend(op.key);
    break;
  case CollabLww::Verdict::Skip:
  case CollabLww::Verdict::OwnEcho:
    break;
  }
}

void CollabClient::Poll() {
  if (!bus_)
    return;
  bus_->Poll();
  std::deque<std::pair<std::string, bool>> logs;
  std::string refused;
  {
    std::lock_guard lock(shared_->mutex);
    logs.swap(shared_->logs);
    refused.swap(shared_->refused);
  }
  for (auto &[text, error] : logs)
    Log("collab: " + text, error);
  if (!refused.empty()) {
    lastError_ = refused;
    if (bus_)
      bus_->Stop();
    peers_.clear();
    SetState(CollabState::Failed);
  }
}

bool CollabClient::Put(const std::string &key, const std::string &slot,
                       json data) {
  if (state_ != CollabState::Joined || !bus_ || key.empty() || slot.empty())
    return false;
  if (!bus_->Emit("op", {{"key", key}, {"slot", slot}, {"data", std::move(data)}}))
    return false;
  lww_.NoteSent(key, slot);
  return true;
}

bool CollabClient::Delete(const std::string &key) {
  if (state_ != CollabState::Joined || !bus_ || key.empty())
    return false;
  if (!bus_->Emit("op", {{"key", key}, {"del", true}}))
    return false;
  lww_.NoteSent(key, "");
  return true;
}

bool CollabClient::SetPresence(json data) {
  if (state_ != CollabState::Joined || !bus_)
    return false;
  return bus_->Emit("presence", std::move(data));
}

} // namespace ilmeee::net
