#pragma once
// Live view of a game the editor started (desktop, or Android over adb). It
// is fed the game's output line by line and keeps:
//   ILMEEE_STATS {json}        player, twice a second (fps, frame times, RAM)
//   ILMEEE_GAME_INFO {json}    player, once (GPU, Vulkan version)
//   ILMEEE_DEVICE_INFO {json}  adb run script, once (model, Android, SoC)
//   ILMEEE_BATTERY {json}      adb run script, every few seconds
// plus the last log lines, for the Device Monitor panel and the MCP tools.
// Plain data, no UI: the editor thread owns it.

#include "../../vendor/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>
#include <string>
#include <string_view>

namespace ilmeee {

struct DeviceSample {
  double time = 0; // editor clock (seconds) when received
  double fps = 0, frameMs = 0, worstMs = 0;
  double updateMs = 0, mediaMs = 0, sceneMs = 0;
  double rssMiB = -1;
};

class DeviceMonitor {
public:
  static constexpr size_t kMaxSamples = 600; // 5 minutes at 2 per second
  static constexpr size_t kMaxLog = 4000;

  // A new game run; forgets the previous one.
  void Reset(const std::string &target) {
    *this = DeviceMonitor{};
    target_ = target;
    running_ = true;
  }
  void SetEnded() { running_ = false; }

  // Returns true when the line was telemetry (keep it out of the Console).
  bool Feed(const std::string &line, double now) {
    static constexpr std::string_view kMarkers[] = {
        "ILMEEE_STATS ", "ILMEEE_GAME_INFO ", "ILMEEE_DEVICE_INFO ",
        "ILMEEE_BATTERY "};
    for (size_t kind = 0; kind < std::size(kMarkers); ++kind) {
      const size_t at = line.find(kMarkers[kind]);
      if (at == std::string::npos)
        continue;
      auto value = nlohmann::json::parse(
          line.substr(at + kMarkers[kind].size()), nullptr, false);
      if (!value.is_object())
        break; // malformed: keep it as ordinary output
      if (kind == 0)
        AddSample(value, now);
      else if (kind == 3)
        battery_ = std::move(value);
      else
        device_.update(value);
      return true;
    }
    log_.push_back(line);
    if (log_.size() > kMaxLog)
      log_.pop_front();
    return false;
  }

  const std::string &Target() const { return target_; }
  bool Running() const { return running_; }
  // A sample arrived recently.
  bool Live(double now) const {
    return !samples_.empty() && now - samples_.back().time < 2.0;
  }
  const std::deque<DeviceSample> &Samples() const { return samples_; }
  const nlohmann::json &Device() const { return device_; }
  const nlohmann::json &Battery() const { return battery_; }
  const std::deque<std::string> &Log() const { return log_; }

  // Over the samples of the last `seconds`.
  nlohmann::json Summary(double now, double seconds) const {
    double sumFps = 0, sumFrame = 0, minFps = 1e9, worst = 0;
    size_t n = 0;
    for (auto it = samples_.rbegin();
         it != samples_.rend() && now - it->time <= seconds; ++it, ++n) {
      sumFps += it->fps;
      sumFrame += it->frameMs;
      minFps = std::min(minFps, it->fps);
      worst = std::max(worst, it->worstMs);
    }
    if (!n)
      return {{"samples", 0}};
    return {{"seconds", seconds},
            {"samples", n},
            {"avgFps", Round(sumFps / n)},
            {"minFps", Round(minFps)},
            {"avgFrameMs", Round(sumFrame / n)},
            {"worstFrameMs", Round(worst)}};
  }

  nlohmann::json ToJson(double now, size_t history) const {
    nlohmann::json out = {{"target", target_},
                          {"running", running_},
                          {"live", Live(now)},
                          {"device", device_},
                          {"battery", battery_},
                          {"last10s", Summary(now, 10)},
                          {"last60s", Summary(now, 60)}};
    if (!samples_.empty()) {
      out["secondsSinceLastSample"] = Round(now - samples_.back().time);
      out["latest"] = Json(samples_.back(), now);
    }
    nlohmann::json list = nlohmann::json::array();
    const size_t first =
        samples_.size() - std::min(history, samples_.size());
    for (size_t i = first; i < samples_.size(); ++i)
      list.push_back(Json(samples_[i], now));
    out["history"] = std::move(list);
    return out;
  }

private:
  static double Round(double v) { return std::round(v * 100.0) / 100.0; }
  static double Number(const nlohmann::json &j, const char *key,
                       double fallback = 0) {
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
  }
  static nlohmann::json Json(const DeviceSample &s, double now) {
    return {{"ageSeconds", Round(now - s.time)}, {"fps", s.fps},
            {"frameMs", s.frameMs},             {"worstMs", s.worstMs},
            {"updateMs", s.updateMs},           {"mediaMs", s.mediaMs},
            {"sceneMs", s.sceneMs},             {"rssMiB", s.rssMiB}};
  }
  void AddSample(const nlohmann::json &j, double now) {
    DeviceSample s;
    s.time = now;
    s.fps = Number(j, "fps");
    s.frameMs = Number(j, "frameMs");
    s.worstMs = Number(j, "worstMs");
    s.updateMs = Number(j, "updateMs");
    s.mediaMs = Number(j, "mediaMs");
    s.sceneMs = Number(j, "sceneMs");
    s.rssMiB = Number(j, "rssMiB", -1);
    for (const char *key : {"width", "height", "objects", "paused"})
      if (j.contains(key))
        device_[key] = j[key];
    samples_.push_back(s);
    if (samples_.size() > kMaxSamples)
      samples_.pop_front();
  }

  std::string target_;
  bool running_ = false;
  std::deque<DeviceSample> samples_;
  nlohmann::json device_ = nlohmann::json::object();
  nlohmann::json battery_ = nlohmann::json::object();
  std::deque<std::string> log_;
};

} // namespace ilmeee
