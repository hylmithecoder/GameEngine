#include "../../../../include/core_engine/net/McpServer.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <ixwebsocket/IXHttpServer.h>
#include <sys/socket.h>
#include <utility>

namespace ilmeee::net {
namespace {
constexpr const char *kModern = "2026-07-28";
constexpr const char *kLegacy = "2025-11-25";
// Older revisions a client may still negotiate; our tool results fit them all.
constexpr const char *kLegacyVersions[] = {kLegacy, "2025-06-18",
                                           "2025-03-26"};
constexpr const char *kVersionKey = "io.modelcontextprotocol/protocolVersion";
constexpr const char *kCapabilitiesKey =
    "io.modelcontextprotocol/clientCapabilities";
constexpr const char *kServerInfoKey = "io.modelcontextprotocol/serverInfo";
constexpr size_t kMaxRequestBytes = 64 * 1024;

std::string Header(const ix::HttpRequest &request, const char *key) {
  auto it = request.headers.find(key);
  return it == request.headers.end() ? std::string() : it->second;
}

bool IsLegacyVersion(const std::string &version) {
  for (const char *known : kLegacyVersions)
    if (version == known)
      return true;
  return false;
}

std::string Lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return text;
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
  json tools = json::array(
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
       {{"name", "get_spring_bones"},
        {"description", "Get the spring bone (hair / breast / skirt physics) "
                        "component of a skinned object: chains, colliders and "
                        "how many bones it simulates."},
        {"inputSchema",
         ObjectSchema({{"index", {{"type", "integer"}, {"minimum", 0}}}},
                      {"index"})}},
       {{"name", "set_spring_bones"},
        {"description",
         "Add or edit spring bones on a skinned (PMX) object in the stopped "
         "editor; records Undo. auto=true detects hair, breast, skirt, ribbon "
         "and tail bones by name and adds head/chest/thigh colliders; the "
         "other fields then override. chains/colliders replace the lists. "
         "stiffness is 1/s (hair ~6, breast ~25), drag 0..1, gravity and wind "
         "in bone lengths per second. Simulates in Play mode."},
        {"inputSchema",
         ObjectSchema(
             {{"index", {{"type", "integer"}, {"minimum", 0}}},
              {"auto", {{"type", "boolean"}}},
              {"enabled", {{"type", "boolean"}}},
              {"wind", vec3},
              {"chains",
               {{"type", "array"},
                {"items",
                 ObjectSchema({{"rootBone", {{"type", "string"}}},
                               {"stiffness", {{"type", "number"}}},
                               {"drag", {{"type", "number"}}},
                               {"gravity", {{"type", "number"}}},
                               {"hitRadius", {{"type", "number"}}},
                               {"useColliders", {{"type", "boolean"}}}},
                              {"rootBone"})}}},
              {"colliders",
               {{"type", "array"},
                {"items",
                 ObjectSchema({{"bone", {{"type", "string"}}},
                               {"offset", vec3},
                               {"tailOffset", vec3},
                               {"radius", {{"type", "number"}}}},
                              {"bone", "radius"})}}}},
             {"index"})}},
       {{"name", "set_animation"},
        {"description",
         "Set the Animation component of a skinned (PMX) object in the "
         "stopped editor; records Undo. clip is a project-relative "
         ".ilmeeeanim (or .vmd) path, controller a .ilmeeecontroller path "
         "that takes precedence (\"\" clears). Setting clip clears the old "
         "controller unless controller is also provided. Assigning an asset "
         "enables animation unless enabled is explicitly set. Plays in Play mode; save_scene persists it."},
        {"inputSchema",
         ObjectSchema({{"index", {{"type", "integer"}, {"minimum", 0}}},
                       {"clip", {{"type", "string"}}},
                       {"controller", {{"type", "string"}}},
                       {"enabled", {{"type", "boolean"}}},
                       {"loop", {{"type", "boolean"}}},
                       {"speed", {{"type", "number"}, {"minimum", 0}, {"maximum", 4}}}},
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

  const json index = {{"type", "integer"}, {"minimum", 0}};
  const json path = {{"type", "string"}, {"minLength", 1},
                     {"description", "Path relative to the active project; outputs must be .ilmeeeanim"}};
  const json bone = {{"type", "string"}, {"minLength", 1},
                     {"description", "Exact primary/alternate bone name, or @humanoid_role from get_skeleton"}};
  const json time = {{"type", "number"}, {"minimum", 0}, {"maximum", 600}};
  const json duration = {{"type", "number"}, {"exclusiveMinimum", 0}, {"maximum", 600}};
  const json fps = {{"type", "number"}, {"minimum", 1}, {"maximum", 240}};
  const json ops = {{"type", "array"}, {"maxItems", 2000},
    {"items", {{"oneOf", json::array({
      ObjectSchema({{"op", {{"const", "set_key"}}}, {"bone", bone}, {"time", time},
                    {"rotation", vec3}, {"translation", vec3}, {"scale", vec3},
                    {"ease", {{"enum", {"linear", "smooth", "ease_in", "ease_out"}}}}}, {"op", "bone", "time"}),
      ObjectSchema({{"op", {{"const", "delete_key"}}}, {"bone", bone},
                    {"time", {{"type", "number"}, {"minimum", 0}, {"maximum", 3600}}}}, {"op", "bone", "time"}),
      ObjectSchema({{"op", {{"const", "clear_track"}}}, {"bone", bone}}, {"op", "bone"}),
      ObjectSchema({{"op", {{"const", "add_rotation"}}}, {"bone", bone}, {"rotation", vec3},
                    {"from", {{"type", "number"}, {"minimum", 0}}},
                    {"to", {{"type", "number"}, {"minimum", 0}}}}, {"op", "bone", "rotation"}),
      ObjectSchema({{"op", {{"const", "mirror"}}}, {"bone", bone},
                    {"from", {{"enum", {"left", "right"}}}}}, {"op"}),
      ObjectSchema({{"op", {{"const", "scale_time"}}},
                    {"factor", {{"type", "number"}, {"minimum", 0.01}, {"maximum", 100}}}}, {"op", "factor"}),
      ObjectSchema({{"op", {{"const", "shift"}}}, {"bone", bone}, {"by", {{"type", "number"}}}}, {"op", "by"}),
      ObjectSchema({{"op", {{"const", "set_duration"}}}, {"duration", duration}, {"fps", fps}}, {"op", "duration"})
    })}}}};
  const auto add = [&](const char *name, const char *description, json schema) {
    tools.push_back({{"name", name}, {"description", description}, {"inputSchema", std::move(schema)}});
  };
  const json componentType = {{"type", "string"},
                              {"enum", {"movement", "physics", "collider", "audio_source", "video_player",
                                        "dialog", "animation", "spring_bones", "script"}}};
  add("list_components", "List the components attached to a scene object with their settings (script components include typed exposed fields), plus what can be added: addable types, registered movement scripts and project C++ scripts with compile status.",
      ObjectSchema({{"index", index}}, {"index"}));
  add("add_component", "Attach a component to a scene object in the stopped editor, or update it if already attached (only given properties change); records Undo, save_scene persists. properties per type: "
      "movement {movable, script (movement script name, default \"WASD XZ\"), speed, camera (camera object index, -1 = Game view), cameraStyle (none|third_person|first_person|side_scroller|top_down), distance, height, moveJoystick, lookJoystick (UI joystick names)}; "
      "physics {useGravity, isKinematic, mass, drag, gravityY} (auto-adds a fitted collider if none); "
      "collider {shape (box|sphere|capsule), center [x,y,z], size [x,y,z] (box), radius, height (capsule), isTrigger, friction 0..1, bounciness 0..1, fit (true = size to the mesh; new colliders fit by default)} - collider alone = static, + physics = dynamic/kinematic, + movement = character; "
      "audio_source {clip, volume, loop, playOnStart, spatial, minDistance, maxDistance}; "
      "video_player {clip, loop, playOnStart, volume, unlit, surface}; "
      "script {source (.cpp path or class name, required), enabled, values {field: value}; GameObject fields take an object index, a UI element name or null}; "
      "dialog/animation/spring_bones take the same fields as set_dialog/set_animation/set_spring_bones (animation needs clip or controller; spring_bones defaults to auto setup).",
      ObjectSchema({{"index", index}, {"type", componentType},
                    {"properties", {{"type", "object"}}}}, {"index", "type"}));
  add("remove_component", "Remove a component from a scene object in the stopped editor; records Undo. For type script give script = source path or class name.",
      ObjectSchema({{"index", index}, {"type", componentType}, {"script", {{"type", "string"}}}},
                   {"index", "type"}));
  add("get_skeleton", "Inspect a skinned scene object's bones, humanoid roles, groups and available generated motion types. Use @role in authoring. Bind positions are model-space; keys use local Euler degrees. Paginated bones; roles/groups cover the full rig.",
      ObjectSchema({{"index", index}, {"group", {{"type", "string"}}}, {"contains", {{"type", "string"}}},
                    {"offset", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100000}}},
                    {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}}}, {"index"}));
  add("get_animation", "Read .ilmeeeanim or .vmd metadata and track counts. includeKeys=true returns paginated keys, optionally filtered by bone; rotation is Euler degrees, curves are incoming Bezier control points. Optional index resolves @roles and reports rig compatibility.",
      ObjectSchema({{"path", path}, {"index", index}, {"bone", bone}, {"includeKeys", {{"type", "boolean"}}},
                    {"offset", {{"type", "integer"}, {"minimum", 0}, {"maximum", 100000}}},
                    {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}}}, {"path"}));
  add("sample_animation", "Evaluate clip FK at time in seconds, clamped by default (loop=true wraps). Returns local bone transforms in Euler degrees; optional index adds model-space positions for the full rig, including unkeyed bones. Physics is not simulated. bone optionally selects a name or @role.",
      ObjectSchema({{"path", path}, {"index", index}, {"bone", bone},
                    {"time", {{"type", "number"}, {"minimum", 0}, {"maximum", 3600}}},
                    {"loop", {{"type", "boolean"}}}}, {"path", "time"}));
  add("create_animation", "Create and save an editable .ilmeeeanim clip (default duration 3 s, FPS 30). Optional ops add keys in the same atomic request; index validates bone names and resolves @roles. Existing outputs require overwrite=true. Stop Play first. File edits are outside scene Undo; assign separately with set_animation.",
      ObjectSchema({{"path", path}, {"index", index}, {"name", {{"type", "string"}}},
                    {"duration", duration}, {"fps", fps}, {"ops", ops},
                    {"overwrite", {{"type", "boolean"}}}}, {"path"}));
  add("edit_animation", "Atomically apply ops to a saved clip. Rotation is local Euler degrees; mirror reflects across YZ (default left to right), shift clamps negative key times to zero with last key winning. output optionally saves a copy or converts VMD to .ilmeeeanim; VMD input is preserved. Stop Play first; file edits are outside scene Undo.",
      ObjectSchema({{"path", path}, {"output", path}, {"index", index}, {"name", {{"type", "string"}}},
                    {"ops", ops}, {"overwrite", {{"type", "boolean"}}}}, {"path", "ops"}));
  add("generate_animation", "Generate and save a procedural humanoid clip for a skinned scene object using detected roles. duration defaults per type (0.2..60 s when supplied); intensity 0..3 defaults to 1. Optional ops refine the result before saving. Stop Play first. Assign with set_animation, then save_scene.",
      ObjectSchema({{"path", path}, {"index", index}, {"type", {{"type", "string"},
                    {"enum", {"idle", "breathe", "wave", "nod", "shake_head", "sway", "bow", "talk", "happy_bounce", "look_around"}}}},
                    {"duration", {{"type", "number"}, {"minimum", 0.2}, {"maximum", 60}}},
                    {"intensity", {{"type", "number"}, {"minimum", 0}, {"maximum", 3}}},
                    {"name", {{"type", "string"}}}, {"ops", ops}, {"overwrite", {{"type", "boolean"}}}}, {"path", "index", "type"}));
  return tools;
}

} // namespace

McpServer::McpServer(ToolHandler handler) : handler_(std::move(handler)) {}
McpServer::~McpServer() { Stop(); }

bool McpServer::Start(int port, std::string token, std::string &error,
                      std::string publicHost) {
  if (port < 1 || port > 65535 || token.empty()) {
    error = "invalid MCP port or empty token";
    return false;
  }
  token_ = std::move(token);
  publicHost_ = Lowercase(std::move(publicHost));
  stopping_ = false;
  server_ = std::make_unique<ix::HttpServer>(
      port, "127.0.0.1", 16, 8, AF_INET, 10,
      ix::WebSocketServer::kDefaultHandShakeTimeoutSecs,
      static_cast<int>(kMaxRequestBytes), false);
  server_->setOnConnectionCallback([this, port](
                                       ix::HttpRequestPtr request,
                                       std::shared_ptr<ix::ConnectionState>) {
    if (!request)
      return Response(404);
    // "/" and "/mcp" authenticate with a Bearer header. "/mcp/<token>" carries
    // the token in the path for clients that cannot send custom headers
    // (e.g. claude.ai custom connectors through a tunnel).
    const std::string path = request->uri.substr(0, request->uri.find('?'));
    const bool pathToken = path.rfind("/mcp/", 0) == 0;
    if (path != "/" && path != "/mcp" && !pathToken)
      return Response(404);
    const std::string local = "127.0.0.1:" + std::to_string(port);
    const std::string localhost = "localhost:" + std::to_string(port);
    const std::string host = Lowercase(Header(*request, "Host"));
    const std::string origin = Lowercase(Header(*request, "Origin"));
    const bool publicRequest = !publicHost_.empty() &&
                               (host == publicHost_ ||
                                host == publicHost_ + ":443");
    if ((host != local && host != localhost && !publicRequest) ||
        (!origin.empty() && origin != "http://" + local &&
         origin != "http://" + localhost &&
         !(publicRequest && origin == "https://" + publicHost_)))
      return Response(403);
    if (request->method != "POST")
      return Response(405);
    const bool authorized =
        pathToken ? TokenMatches(path.substr(5), token_)
                  : TokenMatches(Header(*request, "Authorization"),
                                 "Bearer " + token_);
    if (!authorized)
      return Response(pathToken ? 404 : 401);
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
      } else if (!headerVersion.empty() && !IsLegacyVersion(headerVersion)) {
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
        const std::string wanted = params.value("protocolVersion", "");
        result = {
            {"protocolVersion", IsLegacyVersion(wanted) ? wanted : kLegacy},
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
