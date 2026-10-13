#include "../include/core_engine/DeviceMonitor.hpp"
#include <cstdio>

static int checks = 0, failures = 0;
#define CHECK(expr)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(expr)) {                                                             \
      ++failures;                                                              \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);     \
    }                                                                          \
  } while (0)

int main() {
  ilmeee::DeviceMonitor monitor;
  monitor.Reset("Android");
  CHECK(monitor.Running() && monitor.Target() == "Android");

  // adb run script, then logcat -v brief lines from the player.
  CHECK(monitor.Feed("ILMEEE_DEVICE_INFO {\"manufacturer\":\"samsung\","
                     "\"model\":\"SM-A125F\",\"android\":\"12\"}",
                     0.0));
  CHECK(monitor.Feed("I/SDL/APP (12345): ILMEEE_GAME_INFO "
                     "{\"gpu\":\"PowerVR Rogue GE8320\",\"vulkan\":\"1.1.131\"}",
                     0.1));
  CHECK(monitor.Device()["model"] == "SM-A125F");
  CHECK(monitor.Device()["gpu"] == "PowerVR Rogue GE8320");
  CHECK(!monitor.Live(0.2));
  for (int i = 0; i < 30; ++i) {
    const double fps = i < 20 ? 60.0 : 30.0;
    char line[256];
    std::snprintf(line, sizeof line,
                  "I/SDL/APP (12345): ILMEEE_STATS {\"fps\":%.1f,"
                  "\"frameMs\":%.2f,\"worstMs\":%.1f,\"rssMiB\":210.5,"
                  "\"width\":1600,\"height\":720,\"objects\":9}",
                  fps, 1000.0 / fps, i == 25 ? 120.0 : 1000.0 / fps);
    CHECK(monitor.Feed(line, 1.0 + i * 0.5));
  }
  CHECK(monitor.Feed("ILMEEE_BATTERY {\"level\":81,\"tempC\":38.5,"
                     "\"charging\":true,\"thermal\":1}",
                     15.0));
  CHECK(monitor.Samples().size() == 30);
  CHECK(monitor.Live(15.6) && !monitor.Live(20.0));
  CHECK(monitor.Battery()["thermal"] == 1);
  CHECK(monitor.Device()["width"] == 1600);

  // Last 4.5 s: samples 21..29 at 30 fps plus nothing older.
  const auto recent = monitor.Summary(15.5, 4.5);
  CHECK(recent["samples"] == 10);
  CHECK(recent["avgFps"] == 30.0);
  CHECK(recent["worstFrameMs"] == 120.0);
  const auto all = monitor.Summary(15.5, 60);
  CHECK(all["samples"] == 30 && all["minFps"] == 30.0);

  // Ordinary output and malformed telemetry go to the log, not the stats.
  CHECK(!monitor.Feed("E/Adreno  (12345): something failed", 16.0));
  CHECK(!monitor.Feed("I/SDL/APP (12345): ILMEEE_STATS {broken", 16.1));
  CHECK(monitor.Log().size() == 2 && monitor.Samples().size() == 30);

  const auto json = monitor.ToJson(15.5, 5);
  CHECK(json["history"].size() == 5 && json["live"] == true);
  CHECK(json["latest"]["fps"] == 30.0 && json["latest"]["rssMiB"] == 210.5);
  CHECK(json["device"]["android"] == "12");

  monitor.SetEnded();
  CHECK(!monitor.Running());
  monitor.Reset("Linux");
  CHECK(monitor.Samples().empty() && monitor.Log().empty() &&
        monitor.Device().empty());

  // The ring buffers stay bounded.
  for (size_t i = 0; i < ilmeee::DeviceMonitor::kMaxSamples + 10; ++i)
    monitor.Feed("ILMEEE_STATS {\"fps\":60}", (double)i);
  CHECK(monitor.Samples().size() == ilmeee::DeviceMonitor::kMaxSamples);

  std::printf("%d/%d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
