#include "../../../../include/core_engine/net/McpServer.hpp"
#include <algorithm>
#include <chrono>
#include <exception>
#include <ixwebsocket/IXHttpServer.h>
#include <sys/socket.h>
#include <utility>

namespace ilmeee::net {
namespace {
constexpr const char *kModern = "2026-07-28";
constexpr const char *kLegacy = "2025-11-25";
constexpr const char *kVersionKey = "io.modelcontextprotocol/protocolVersion";
constexpr const char *kCapabilitiesKey =
    "io.modelcontextprotocol/clientCapabilities";
constexpr const char *kServerInfoKey = "io.modelcontextprotocol/serverInfo";
constexpr size_t kMaxRequestBytes = 64 * 1024;

std::string Header(const ix::HttpRequest &request, const char *key) {
  auto it = request.headers.find(key);
  return it == request.headers.end() ? std::string() : it->second;
}

bool TokenMatches(const std::string &actual, const std::string &expected) {
  // Compare the entire string even when the prefix differs.
  size_t difference = actual.size() ^ expected.size();
  for (size_t i = 0; i < std::max(actual.size(), expected.size()); ++i)
    difference |= (i < actual.size() ? (unsigned char)actual[i] : 0) ^
                  (i < expected.size() ? (unsigned char)expected[i] : 0);
  return difference == 0;
}

ix::HttpResponsePtr Response(int status, const std::string &body = {}) {
  ix::WebSocketHttpHeaders headers;
  headers["Content-Type"] = "application/json; charset=utf-8";
  headers["Cache-Control"] = "no-store";
  return std::make_shared<ix::HttpResponse>(
      status, status == 200 ? "OK" : "Error", ix::HttpErrorCode::Ok, headers,
      body);
}

json RpcError(const json &id, int code, const std::string &message,
              json data = nullptr) {
  json error = {{"code", code}, {"message", message}};
  if (!data.is_null())
    error["data"] = std::move(data);
  return {{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(error)}};
}

json RpcResult(const json &id, json result, bool modern) {
  if (modern) {
    result["resultType"] = "complete";
    result["_meta"][kServerInfoKey] = {{"name", "ilmeee-editor"},
                                       {"version", "1.0.0"}};
  }
  return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

json ObjectSchema(json properties = json::object(),
                  json required = json::array()) {
  return {{"type", "object"},
          {"properties", std::move(properties)},
          {"required", std::move(required)},
          {"additionalProperties", false}};
}

json ToolList() {
  const json vec3 = {{"type", "array"},
                     {"items", {{"type", "number"}}},
                     {"minItems", 3},
                     {"maxItems", 3}};
  const json vec2 = {{"type", "array"},
                     {"items", {{"type", "number"}}},
                     {"minItems", 2},
                     {"maxItems", 2}};
  return json::array(
      {{{"name", "get_project_info"},
        {"description", "Get the open editor project and scene status."},
        {"inputSchema", ObjectSchema()}},
       {{"name", "list_scene_objects"},
        {"description", "List 3D objects in the current scene. Indices may "
                        "change after structural edits."},
        {"inputSchema", ObjectSchema()}},
       {{"name", "get_object"},
        {"description", "Get a 3D scene object by its current index."},
        {"inputSchema",
         ObjectSchema({{"index", {{"type", "integer"}, {"minimum", 0}}}},
                      {"index"})}},
       {{"name", "create_object"},
        {"description",
         "Create a primitive in the stopped editor scene; records Undo."},
        {"inputSchema",
         ObjectSchema(
             {{"kind",
               {{"type", "string"},
                {"enum", {"cube", "sphere", "plane", "light", "camera", "text_3d"}}}},
              {"position", vec3},
              {"parent", {{"type", "integer"}, {"minimum", 0}}}},
             {"kind"})}},
       {{"name", "set_transform"},
        {"description", "Set local position, Euler rotation in degrees, or "
                        "scale; records Undo."},
        {"inputSchema",
         ObjectSchema({{"index", {{"type", "integer"}, {"minimum", 0}}},
                       {"position", vec3},
                       {"rotation", vec3},
                       {"scale", vec3}},
                      {"index"})}},
       {{"name", "save_scene"},
        {"description",
         "Save the current stopped 3D scene to the active project."},
        {"inputSchema", ObjectSchema()}},
       {{"name", "list_ui_elements"},
        {"description", "List the game Canvas elements (Text, Image, Button, "
                        "Joystick) in draw order; later ones are on top. "
                        "Elements are addressed by their unique name."},
        {"inputSchema", ObjectSchema()}},
       {{"name", "set_ui_element"},
        {"description", "Edit a Canvas element in the stopped editor. "
                        "position/size are reference pixels [x, y]; image is "
                        "a project-relative path (Image picture or Button "
                        "sprite, \"\" clears); action \"Interact\" makes a "
                        "button talk to the nearest Dialog."},
        {"inputSchema",
         ObjectSchema({{"name", {{"type", "string"}}},
                       {"text", {{"type", "string"}}},
                       {"action", {{"type", "string"}}},
                       {"image", {{"type", "string"}}},
                       {"visible", {{"type", "boolean"}}},
                       {"position", vec2},
                       {"size", vec2},
                       {"fontSize", {{"type", "number"}, {"minimum", 1}}}},
                      {"name"})}},
       {{"name", "get_dialog"},
        {"description", "Get the Dialog component of a scene object: "
                        "trigger, UI links (element names), lines and parsed "
                        "line count; while playing also whether it is open."},
        {"inputSchema",
         ObjectSchema({{"index", {{"type", "integer"}, {"minimum", 0}}}},
                      {"index"})}},
       {{"name", "set_dialog"},
        {"description",
         "Add or edit the Dialog component of a scene object (stopped "
         "editor; records Undo). lines: one per line, \"Name: text\", "
         "\": narration\", \"# comment\". box/text/speaker/prompt take a "
         "Canvas element name (\"\" clears). createUi adds missing Box/Text/"
         "Name elements and a Talk button. enabled=false removes it."},
        {"inputSchema",
         ObjectSchema(
             {{"index", {{"type", "integer"}, {"minimum", 0}}},
              {"enabled", {{"type", "boolean"}}},
              {"trigger",
               {{"type", "string"},
                {"enum", {"interact", "on_start", "script_only"}}}},
              {"range", {{"type", "number"}, {"minimum", 0}}},
              {"lines", {{"type", "string"}}},
              {"file", {{"type", "string"}}},
              {"typingSpeed",
               {{"type", "number"},
                {"minimum", 0},
                {"description", "Characters per second, 0 = instant"}}},
              {"repeatable", {{"type", "boolean"}}},
              {"freezePlayer", {{"type", "boolean"}}},
              {"box", {{"type", "string"}}},
              {"text", {{"type", "string"}}},
              {"speaker", {{"type", "string"}}},
              {"prompt", {{"type", "string"}}},
              {"createUi", {{"type", "boolean"}}}},
             {"index"})}},
       {{"name", "start_dialog"},
        {"description", "While the editor is playing, open the Dialog of a "
                        "scene object (for testing)."},
        {"inputSchema",
         ObjectSchema({{"index", {{"type", "integer"}, {"minimum", 0}}}},
                      {"index"})}},
       {{"name", "get_device_stats"},
        {"description",
         "Live performance of the game started with Build > Run (desktop, "
         "or an Android phone over adb): device/GPU info, battery and "
         "thermal state, latest frame stats, 10 s / 60 s summaries and the "
         "recent per-0.5 s samples."},
        {"inputSchema",
         ObjectSchema({{"samples",
                        {{"type", "integer"},
                         {"minimum", 0},
                         {"maximum", 600},
                         {"description",
                          "History samples to return (default 20)"}}}})}},
       {{"name", "get_game_log"},
        {"description",
         "Recent output of the running game (Android: its whole logcat, "
         "Info and above, plus crash reports), newest last."},
        {"inputSchema",
         ObjectSchema({{"lines",
                        {{"type", "integer"},
                         {"minimum", 1},
                         {"maximum", 2000},
                         {"description", "Lines to return (default 200)"}}},
                       {"contains",
                        {{"type", "string"},
                         {"description", "Only lines containing this text"}}},
                       {"errorsOnly",
                        {{"type", "boolean"},
                         {"description",
                          "Only logcat Error/Fatal lines and crash output"}}}})}}});
}

} // namespace

McpServer::McpServer(ToolHandler handler) : handler_(std::move(handler)) {}
McpServer::~McpServer() { Stop(); }

bool McpServer::Start(int port, std::string token, std::string &error) {
  if (port < 1 || port > 65535 || token.empty()) {
    error = "invalid MCP port or empty token";
    return false;
  }
  token_ = std::move(token);
  stopping_ = false;
  server_ = std::make_unique<ix::HttpServer>(
      port, "127.0.0.1", 16, 8, AF_INET, 10,
      ix::WebSocketServer::kDefaultHandShakeTimeoutSecs,
      static_cast<int>(kMaxRequestBytes), false);
  server_->setOnConnectionCallback([this, port](
                                       ix::HttpRequestPtr request,
                                       std::shared_ptr<ix::ConnectionState>) {
    if (!request || request->uri != "/mcp")
      return Response(404);
    const std::string local = "127.0.0.1:" + std::to_string(port);
    const std::string localhost = "localhost:" + std::to_string(port);
    const std::string host = Header(*request, "Host");
    const std::string origin = Header(*request, "Origin");
    if ((host != local && host != localhost) ||
        (!origin.empty() && origin != "http://" + local &&
         origin != "http://" + localhost))
      return Response(403);
    if (request->method != "POST")
      return Response(405);
    if (!TokenMatches(Header(*request, "Authorization"), "Bearer " + token_))
      return Response(401);
    if (request->body.size() > kMaxRequestBytes)
      return Response(413);
    if (Header(*request, "Content-Type").find("application/json") != 0)
      return Response(415);
    const std::string accept = Header(*request, "Accept");
    if (accept.find("application/json") == std::string::npos ||
        accept.find("text/event-stream") == std::string::npos)
      return Response(406);

    try {
      json rpc = json::parse(request->body, nullptr, false);
      if (!rpc.is_object() || rpc.value("jsonrpc", "") != "2.0" ||
          !rpc.contains("method") || !rpc["method"].is_string())
        return Response(
            400, RpcError(nullptr, -32600, "Invalid JSON-RPC request").dump());
      const json id = rpc.value("id", json(nullptr));
      const std::string method = rpc["method"].get<std::string>();
      const json params = rpc.value("params", json::object());
      if (!params.is_object())
        return Response(400, RpcError(id, -32602, "Invalid params").dump());
      const json meta = params.value("_meta", json::object());
      const bool modern = meta.is_object() && meta.contains(kVersionKey);
      const std::string headerVersion =
          Header(*request, "MCP-Protocol-Version");
      const std::string requestedVersion =
          modern && meta[kVersionKey].is_string()
              ? meta[kVersionKey].get<std::string>()
              : headerVersion;
      if (modern) {
        if (requestedVersion != kModern) {
          return Response(400,
                          RpcError(id, -32022, "Unsupported protocol version",
                                   {{"supported", {kModern, kLegacy}},
                                    {"requested", requestedVersion}})
                              .dump());
        }
        if (headerVersion != kModern ||
            Header(*request, "Mcp-Method") != method ||
            !meta.contains(kCapabilitiesKey) ||
            !meta[kCapabilitiesKey].is_object() ||
            (method == "tools/call" &&
             (!params.contains("name") || !params["name"].is_string() ||
              Header(*request, "Mcp-Name") !=
                  params["name"].get<std::string>())))
          return Response(
              400, RpcError(id, -32020, "Header or request metadata mismatch")
                       .dump());
      } else if (!headerVersion.empty() && headerVersion != kLegacy) {
        return Response(400,
                        RpcError(id, -32022, "Unsupported protocol version",
                                 {{"supported", {kModern, kLegacy}},
                                  {"requested", headerVersion}})
                            .dump());
      }

      if (method == "notifications/initialized" && id.is_null())
        return Response(202);
      if (id.is_null())
        return Response(400);

      json result;
      if (method == "server/discover" && modern) {
        result = {{"supportedVersions", {kModern, kLegacy}},
                  {"capabilities", {{"tools", json::object()}}},
                  {"instructions", "Control the active Ilmeee Editor scene. "
                                   "Stop play mode before edits."},
                  {"ttlMs", 0},
                  {"cacheScope", "private"}};
      } else if (method == "initialize" && !modern) {
        result = {
            {"protocolVersion", kLegacy},
            {"capabilities", {{"tools", json::object()}}},
            {"serverInfo", {{"name", "ilmeee-editor"}, {"version", "1.0.0"}}}};
        return Response(200, RpcResult(id, std::move(result), false).dump());
      } else if (method == "tools/list") {
        result = {{"tools", ToolList()}};
        if (modern) {
          result["ttlMs"] = 0;
          result["cacheScope"] = "private";
        }
      } else if (method == "tools/call") {
        if (!params.contains("name") || !params["name"].is_string() ||
            !params.value("arguments", json::object()).is_object())
          return Response(
              200, RpcError(id, -32602, "Invalid tool arguments").dump());
        const std::string name = params["name"].get<std::string>();
        bool known = false;
        for (const auto &tool : ToolList())
          if (tool["name"] == name)
            known = true;
        if (!known)
          return Response(200, RpcError(id, -32602, "Unknown tool").dump());
        auto pending = std::make_shared<Pending>();
        pending->name = name;
        pending->arguments = params.value("arguments", json::object());
        auto future = pending->reply.get_future();
        {
          std::lock_guard lock(mutex_);
          if (stopping_)
            return Response(503);
          if (queue_.size() >= 64)
            return Response(503);
          queue_.push_back(pending);
        }
        if (future.wait_for(std::chrono::seconds(8)) !=
            std::future_status::ready) {
          pending->cancelled = true;
          return Response(
              503, RpcError(id, -32000, "Editor did not respond").dump());
        }
        json value = future.get();
        const bool failed = value.contains("error");
        result = {{"content",
                   json::array({{{"type", "text"}, {"text", value.dump()}}})},
                  {"isError", failed}};
        if (!failed)
          result["structuredContent"] = std::move(value);
      } else {
        return Response(404, RpcError(id, -32601, "Method not found").dump());
      }
      return Response(200, RpcResult(id, std::move(result), modern).dump());
    } catch (const json::exception &) {
      return Response(
          400, RpcError(nullptr, -32600, "Invalid JSON-RPC request").dump());
    } catch (const std::exception &) {
      return Response(500,
                      RpcError(nullptr, -32603, "Internal MCP error").dump());
    }
  });
  auto listening = server_->listen();
  if (!listening.first) {
    error = listening.second;
    server_.reset();
    stopping_ = true;
    return false;
  }
  server_->start();
  return true;
}

void McpServer::Stop() {
  stopping_ = true;
  std::deque<std::shared_ptr<Pending>> pending;
  {
    std::lock_guard lock(mutex_);
    pending.swap(queue_);
  }
  for (auto &item : pending)
    item->reply.set_value({{"error", "Editor shutting down"}});
  if (server_) {
    server_->stop();
    server_.reset();
  }
}

void McpServer::Poll() {
  for (int i = 0; i < 16; ++i) {
    std::shared_ptr<Pending> item;
    {
      std::lock_guard lock(mutex_);
      if (queue_.empty())
        break;
      item = queue_.front();
      queue_.pop_front();
    }
    if (item->cancelled) {
      item->reply.set_value({{"error", "Request timed out"}});
      continue;
    }
    try {
      item->reply.set_value(handler_(item->name, item->arguments));
    } catch (const std::exception &e) {
      item->reply.set_value({{"error", e.what()}});
    } catch (...) {
      item->reply.set_value({{"error", "Unknown editor error"}});
    }
  }
}

} // namespace ilmeee::net
