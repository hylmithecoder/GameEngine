#pragma once

#include "Message.hpp"
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>

namespace ix {
class HttpServer;
}

namespace ilmeee::net {

// Local MCP endpoint. HTTP workers enqueue scene operations; Poll() runs them
// on the editor thread, where SceneRenderer and the undo history are owned.
class McpServer {
public:
  using ToolHandler = std::function<json(const std::string &, const json &)>;
  explicit McpServer(ToolHandler handler);
  ~McpServer();
  McpServer(const McpServer &) = delete;
  McpServer &operator=(const McpServer &) = delete;

  bool Start(int port, std::string token, std::string &error);
  void Stop();
  void Poll();

private:
  struct Pending {
    std::string name;
    json arguments;
    std::atomic<bool> cancelled{false};
    std::promise<json> reply;
  };
  ToolHandler handler_;
  std::unique_ptr<ix::HttpServer> server_;
  std::string token_;
  std::atomic<bool> stopping_{true};
  std::mutex mutex_;
  std::deque<std::shared_ptr<Pending>> queue_;
};

} // namespace ilmeee::net
