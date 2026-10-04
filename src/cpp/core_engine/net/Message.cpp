#include "../../../../include/core_engine/net/Message.hpp"

namespace ilmeee::net {

Message Message::Event(std::string type, json payload) {
  Message m;
  m.kind = MessageKind::Event;
  m.type = std::move(type);
  m.payload = std::move(payload);
  return m;
}

Message Message::Request(std::string type, std::uint64_t id, json payload) {
  Message m;
  m.kind = MessageKind::Request;
  m.type = std::move(type);
  m.id = id;
  m.payload = std::move(payload);
  return m;
}

Message Message::Reply(const Message &request, json payload) {
  Message m;
  m.kind = MessageKind::Response;
  m.type = request.type;
  m.id = request.id;
  m.payload = std::move(payload);
  return m;
}

Message Message::Fail(const Message &request, std::string error) {
  Message m = Reply(request);
  m.ok = false;
  m.error = std::move(error);
  return m;
}

std::string Encode(const Message &msg) {
  json j = {{"v", kProtocolVersion}, {"type", msg.type}};
  switch (msg.kind) {
  case MessageKind::Event:
    break;
  case MessageKind::Request:
    j["id"] = msg.id;
    break;
  case MessageKind::Response:
    j["re"] = msg.id;
    j["ok"] = msg.ok;
    if (!msg.ok)
      j["error"] = msg.error;
    break;
  }
  if (!msg.payload.empty())
    j["payload"] = msg.payload;
  // Replace rather than throw on invalid UTF-8 (a stray byte in a file path
  // must not take down the sender); WebSocket text frames require valid UTF-8.
  return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::optional<Message> Decode(std::string_view text, std::string *error) {
  auto fail = [&](std::string why) -> std::optional<Message> {
    if (error)
      *error = std::move(why);
    return std::nullopt;
  };

  json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded())
    return fail("not valid JSON");
  if (!j.is_object())
    return fail("envelope is not an object");

  auto v = j.find("v");
  if (v == j.end() || !v->is_number_integer())
    return fail("missing protocol version");
  if (v->get<int>() != kProtocolVersion)
    return fail("protocol version " + std::to_string(v->get<int>()) +
                ", expected " + std::to_string(kProtocolVersion));

  auto type = j.find("type");
  if (type == j.end() || !type->is_string() ||
      type->get_ref<const std::string &>().empty())
    return fail("missing message type");

  Message m;
  m.type = type->get<std::string>();

  auto id = j.find("id");
  auto re = j.find("re");
  if (id != j.end() && re != j.end())
    return fail("message has both id and re");
  if (id != j.end()) {
    if (!id->is_number_unsigned() || id->get<std::uint64_t>() == 0)
      return fail("request id must be a positive integer");
    m.kind = MessageKind::Request;
    m.id = id->get<std::uint64_t>();
  } else if (re != j.end()) {
    if (!re->is_number_unsigned() || re->get<std::uint64_t>() == 0)
      return fail("response re must be a positive integer");
    m.kind = MessageKind::Response;
    m.id = re->get<std::uint64_t>();
    auto ok = j.find("ok");
    if (ok == j.end() || !ok->is_boolean())
      return fail("response missing ok");
    m.ok = ok->get<bool>();
    if (!m.ok) {
      auto err = j.find("error");
      m.error = (err != j.end() && err->is_string()) ? err->get<std::string>()
                                                     : "unspecified error";
    }
  }

  auto payload = j.find("payload");
  if (payload != j.end()) {
    if (!payload->is_object())
      return fail("payload is not an object");
    m.payload = std::move(*payload);
  }
  return m;
}

} // namespace ilmeee::net
