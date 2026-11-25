#pragma once
// Byte-moving layer under MessageBus. A transport only knows connections and
// frames; it has no idea what a message, a handshake or a request is. That
// split is what lets the WebSocket implementation be swapped (loopback for
// tests and in-process use, something lighter on Android later) without
// touching any message handling.
//
// Threading: callbacks may fire on any thread (usually the transport's IO
// thread). Implementations must make Send*/Close safe to call from any thread.

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace ilmeee::net {

using ConnectionId = std::uint32_t;
inline constexpr ConnectionId kInvalidConnection = 0;

struct TransportCallbacks {
  std::function<void(ConnectionId)> onOpen;
  std::function<void(ConnectionId, const std::string &reason)> onClose;
  std::function<void(ConnectionId, std::string text)> onText;
  std::function<void(ConnectionId, std::vector<std::uint8_t> data)> onBinary;
  // Diagnostics only (failed connect attempt, socket error). Not a close: a
  // connection that actually drops still reports onClose.
  std::function<void(const std::string &what)> onError;
};

class ITransport {
public:
  virtual ~ITransport() = default;

  // Must be called before Start().
  virtual void SetCallbacks(TransportCallbacks callbacks) = 0;

  // Server: begin listening. Client: begin connecting (and reconnecting).
  virtual bool Start() = 0;
  virtual void Stop() = 0;

  virtual bool SendText(ConnectionId to, const std::string &text) = 0;
  virtual bool SendBinary(ConnectionId to,
                          std::span<const std::uint8_t> data) = 0;
  virtual void Close(ConnectionId who, const std::string &reason) = 0;
};

} // namespace ilmeee::net
