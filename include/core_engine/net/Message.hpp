#pragma once
// Wire envelope for every text message on the engine IPC channel.
//
//   event    {"v":1, "type":"engine.log", "payload":{...}}
//   request  {"v":1, "type":"scene.load", "id":42, "payload":{...}}
//   response {"v":1, "type":"scene.load", "re":42, "ok":true,  "payload":{...}}
//            {"v":1, "type":"scene.load", "re":42, "ok":false, "error":"..."}
//
// The kind is implied by which of "id"/"re" is present, so a message can
// never be two things at once. Heavy data (textures, scene blobs) does not go
// through here — it travels as binary frames on the transport.

#include "../../../vendor/nlohmann/json.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ilmeee::net {

using json = nlohmann::json;

// Bump when the envelope or the meaning of an existing message type changes
// incompatibly. Peers with a different version are refused at handshake.
inline constexpr int kProtocolVersion = 1;

enum class MessageKind { Event, Request, Response };

struct Message {
  MessageKind kind = MessageKind::Event;
  std::string type;
  std::uint64_t id = 0; // Request: correlation id. Response: id it answers.
  json payload = json::object();
  bool ok = true;     // Response only.
  std::string error;  // Response only, when !ok.

  static Message Event(std::string type, json payload = json::object());
  static Message Request(std::string type, std::uint64_t id,
                         json payload = json::object());
  static Message Reply(const Message &request, json payload = json::object());
  static Message Fail(const Message &request, std::string error);
};

std::string Encode(const Message &msg);

// Returns nullopt and fills `error` for anything that is not a well-formed
// envelope: bad JSON, wrong version, missing/empty type, non-object payload.
std::optional<Message> Decode(std::string_view text, std::string *error);

} // namespace ilmeee::net
