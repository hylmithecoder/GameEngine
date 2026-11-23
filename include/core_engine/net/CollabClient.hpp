#pragma once
// Realtime collaboration client: several editors in one "room" editing the
// same scene. Server: server/realtime (Rust), deployed behind the tunnel at
// wss://realtimeprotokol.ilmeee.com/ws. It speaks the same envelope as the
// engine IPC bus, so this is just MessageBus + WsClientTransport with the
// collab messages on top.
//
// Model (see server/realtime/src/room.rs): the room is a last-writer-wins
// store of (key, slot) -> JSON. The server stamps every write with a sequence
// number and echoes it to everyone, the sender included. Everyone applies
// writes in sequence order, so all peers converge.
//
// The only subtle part is our own writes. We apply them locally right away
// (no lag while dragging), so a remote write to the same (key, slot) that the
// server ordered *before* ours must not overwrite our newer local value.
// CollabLww tracks writes still waiting for their echo and drops exactly
// those remote writes. Once our echo arrives, later remote writes apply.
//
// Threading: like MessageBus — call everything, Poll() included, from one
// thread (the editor main loop). Callbacks fire inside Poll().
//
// Wire messages (protocol 1):
//   request room.join {room, name, color} -> {self, room, seq, fresh, peers[], state[]}
//   request room.leave {}
//   event   op {key, slot, data} | {key, del:true}  (server echoes with seq, from)
//   event   presence {...}                          (server relays as {from, data})
//   event   lock.acquire {key} | lock.release {key}
//   server events: peer.joined {id,name,color,presence}, peer.left {id,reason},
//                  notice {level, message},
//                  lock {key, holder, queue[]} (holder 0 = free; also in the
//                  join reply as locks[])
//
// Locks: an edit lock per key with a FIFO queue. Asking for a held key puts
// you at the back of its queue; when the holder releases (or leaves) the next
// one gets it. The server does not reject ops on locked keys — the editor
// enforces locks in its UI.

#include "Message.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ilmeee::net {

class MessageBus;
class WsClientTransport;

inline constexpr const char *kCollabDefaultUrl =
    "wss://realtimeprotokol.ilmeee.com/ws";

struct CollabOptions {
  std::string url = kCollabDefaultUrl;
  std::string token; // server-wide ILMEEE_RT_TOKEN
  std::string room;
  std::string name;
  std::uint32_t color = 0x4FC3F7; // 0xRRGGBB, shown next to the name
};

struct CollabPeer {
  std::uint32_t id = 0;
  std::string name;
  std::uint32_t color = 0;
  json presence; // whatever that peer last sent with SetPresence (or null)
};

struct CollabOp {
  std::uint64_t seq = 0;
  std::uint32_t from = 0;
  std::string key;
  std::string slot; // empty for deletes
  json data;
  bool del = false;

  static CollabOp FromJson(const json &j);
};

struct CollabLock {
  std::uint32_t holder = 0; // 0 = free
  std::vector<std::uint32_t> queue;
};

enum class CollabState {
  Offline,    // not started / stopped
  Connecting, // dialing (or redialing after a drop)
  Joining,    // connected, room.join sent
  Joined,     // in the room; ops flow
  Failed,     // refused (bad token, bad room, ...); see LastError()
};

const char *ToString(CollabState state);

// Decides which remote writes to apply. Pure logic, unit-tested.
class CollabLww {
public:
  enum class Verdict {
    Apply,      // remote write: apply it
    Skip,       // superseded by one of our writes still in flight
    SkipResend, // a remote delete lost against our in-flight write: the key is
                // gone on the server except for what we are still sending, so
                // resend the full object to recreate it everywhere
    OwnEcho,    // our own write coming back: already applied locally
  };

  void Reset() { pending_.clear(); perKey_.clear(); }
  // Call after sending a put (slot non-empty) or a delete (slot empty).
  void NoteSent(const std::string &key, const std::string &slot);
  Verdict Judge(const CollabOp &op, std::uint32_t self);
  bool HasPending(const std::string &key) const;

private:
  static std::string Id(const std::string &key, const std::string &slot) {
    return key + '\x1f' + slot;
  }
  void Release(const std::string &key, const std::string &slot);

  std::unordered_map<std::string, int> pending_; // key\x1fslot ("" = delete)
  std::unordered_map<std::string, int> perKey_;  // total in flight per key
};

class CollabClient {
public:
  CollabClient();
  ~CollabClient();
  CollabClient(const CollabClient &) = delete;
  CollabClient &operator=(const CollabClient &) = delete;

  // Connects in the background, joins the room once welcomed, and rejoins
  // after every reconnect (onJoined fires again with the server's state).
  bool Start(const CollabOptions &options);
  void Stop();
  void Poll();

  CollabState State() const { return state_; }
  const std::string &LastError() const { return lastError_; }
  const CollabOptions &Options() const { return options_; }
  std::uint32_t Self() const { return self_; }
  std::uint64_t Seq() const { return seq_; }
  const std::vector<CollabPeer> &Peers() const { return peers_; }
  const CollabPeer *FindPeer(std::uint32_t id) const;

  // Only while Joined; false otherwise (nothing is queued).
  bool Put(const std::string &key, const std::string &slot, json data);
  bool Delete(const std::string &key);
  bool SetPresence(json data);
  bool HasPending(const std::string &key) const { return lww_.HasPending(key); }

  // Edit locks. Requests are fire-and-forget; the result arrives as a `lock`
  // event that updates Locks() and fires onLocksChanged. Locks die with the
  // connection, so after a reconnect ask again.
  bool AcquireLock(const std::string &key);
  bool ReleaseLock(const std::string &key);
  const std::unordered_map<std::string, CollabLock> &Locks() const { return locks_; }
  // Holder of `key`, 0 when free.
  std::uint32_t LockHolder(const std::string &key) const;
  // 1-based place in the queue of `key`, 0 when not waiting.
  std::size_t QueuePosition(const std::string &key) const;

  // Joined (again): the room's current values in replay order. `fresh` = the
  // room was empty, so this client should seed it with its own scene.
  std::function<void(const std::vector<CollabOp> &state, bool fresh)> onJoined;
  // A remote write that should be applied (already filtered by CollabLww).
  std::function<void(const CollabOp &op)> onOp;
  // See CollabLww::Verdict::SkipResend.
  std::function<void(const std::string &key)> onResend;
  // Peer list or a peer's presence changed.
  std::function<void()> onPeersChanged;
  // A lock changed holder or queue.
  std::function<void(const std::string &key)> onLockChanged;
  // Connection-level messages for the editor console.
  std::function<void(const std::string &message, bool error)> onLog;

private:
  void SendJoin();
  void HandleOp(const json &payload);
  void HandleLock(const json &payload, bool notify);
  void Log(const std::string &message, bool error);
  void SetState(CollabState s);

  CollabOptions options_;
  std::unique_ptr<WsClientTransport> transport_;
  std::unique_ptr<MessageBus> bus_;
  CollabState state_ = CollabState::Offline;
  std::string lastError_;
  std::uint32_t self_ = 0;
  std::uint64_t seq_ = 0;
  std::uint64_t joinGeneration_ = 0;
  std::vector<CollabPeer> peers_;
  std::unordered_map<std::string, CollabLock> locks_;
  CollabLww lww_;
  // Set from the bus log sink (may run on the IO thread), read in Poll().
  struct Shared;
  std::shared_ptr<Shared> shared_;
};

// ---- room directory (HTTP GET /rooms, used by the Hub) ----------------------

struct CollabRoomInfo {
  std::string name;
  std::vector<std::pair<std::string, std::uint32_t>> peers; // name, color
  std::size_t objects = 0;
  std::uint64_t seq = 0;
  std::uint64_t updatedUnix = 0;
};

struct CollabRoomList {
  bool ok = false;
  int status = 0;    // HTTP status (0 = no answer)
  std::string error; // why !ok
  std::vector<CollabRoomInfo> rooms; // most recently active first
};

// "wss://host/ws" -> "https://host", "ws://h:9230/ws" -> "http://h:9230".
// Empty when the URL is not ws:// or wss://.
std::string CollabHttpBase(const std::string &wsUrl);

// Blocking (up to timeoutSecs); call it off the UI thread.
CollabRoomList FetchCollabRooms(const std::string &wsUrl, const std::string &token,
                                int timeoutSecs = 8);

} // namespace ilmeee::net
