#include "../../include/core_engine/net/McpServer.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXHttpClient.h>
#include <thread>

using namespace ilmeee::net;
using namespace std::chrono_literals;

static ix::HttpResponsePtr Post(int port, const json &body,
                                const std::string &token, bool modern,
                                const std::string &origin = {}) {
  ix::HttpClient client;
  auto args = client.createRequest();
  args->extraHeaders["Accept"] = "application/json, text/event-stream";
  args->extraHeaders["Content-Type"] = "application/json";
  args->extraHeaders["Authorization"] = "Bearer " + token;
  if (!origin.empty())
    args->extraHeaders["Origin"] = origin;
  if (modern) {
    args->extraHeaders["MCP-Protocol-Version"] = "2026-07-28";
    args->extraHeaders["Mcp-Method"] = body.at("method").get<std::string>();
    if (body.at("method") == "tools/call")
      args->extraHeaders["Mcp-Name"] =
          body.at("params").at("name").get<std::string>();
  }
  return client.post("http://127.0.0.1:" + std::to_string(port) + "/mcp",
                     body.dump(), args);
}

static json Modern(int id, const std::string &method,
                   json params = json::object()) {
  params["_meta"] = {
      {"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
      {"io.modelcontextprotocol/clientCapabilities", json::object()}};
  return {{"jsonrpc", "2.0"},
          {"id", id},
          {"method", method},
          {"params", std::move(params)}};
}

int main() {
  const int port = ix::getFreePort();
  assert(port > 0);
  std::atomic<int> calls{0};
  McpServer server([&](const std::string &name, const json &args) {
    ++calls;
    return json{{"name", name}, {"arguments", args}};
  });
  std::string error;
  assert(server.Start(port, "test-secret", error));
  McpServer collision(
      [](const std::string &, const json &) { return json::object(); });
  std::string collisionError;
  assert(!collision.Start(port, "other-secret", collisionError));
  assert(!collisionError.empty());
  std::atomic<bool> running{true};
  std::thread editor([&] {
    while (running) {
      server.Poll();
      std::this_thread::sleep_for(1ms);
    }
  });

  auto unauthorized = Post(port, Modern(1, "server/discover"), "wrong", true);
  assert(unauthorized->statusCode == 401);
  auto origin = Post(port, Modern(2, "server/discover"), "test-secret", true,
                     "https://untrusted.example");
  assert(origin->statusCode == 403);
  auto discover = Post(port, Modern(3, "server/discover"), "test-secret", true);
  assert(discover->statusCode == 200);
  auto discovery = json::parse(discover->body);
  assert(discovery["result"]["supportedVersions"].size() == 2);
  auto list = Post(port, Modern(4, "tools/list"), "test-secret", true);
  assert(list->statusCode == 200);
  assert(json::parse(list->body)["result"]["tools"].size() == 6);
  auto call =
      Post(port,
           Modern(5, "tools/call",
                  {{"name", "get_object"}, {"arguments", {{"index", 2}}}}),
           "test-secret", true);
  assert(call->statusCode == 200);
  assert(json::parse(
             call->body)["result"]["structuredContent"]["arguments"]["index"] ==
         2);
  assert(calls == 1);
  auto init = Post(port,
                   {{"jsonrpc", "2.0"},
                    {"id", 6},
                    {"method", "initialize"},
                    {"params",
                     {{"protocolVersion", "2025-11-25"},
                      {"capabilities", json::object()},
                      {"clientInfo", {{"name", "test"}, {"version", "1"}}}}}},
                   "test-secret", false);
  assert(init->statusCode == 200);
  assert(json::parse(init->body)["result"]["protocolVersion"] == "2025-11-25");
  running = false;
  editor.join();
  server.Stop();
  std::cout << "MCP HTTP protocol checks passed\n";
}
