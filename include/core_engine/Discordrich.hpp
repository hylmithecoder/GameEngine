#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#define DISCORDAPP_IMPLEMENTATION
#include <discord_game_sdk.h>

class DiscordRichPresence {
public:
  DiscordRichPresence() = default;
  ~DiscordRichPresence() = default;

  DiscordCreateParams params{};

  void Init();
  void Update();
  void Shutdown();

  void SetActivity(const std::string &details, const std::string &state);

  void SetAssets(const std::string &largeImage, const std::string &largeText,
                 const std::string &smallImage = "",
                 const std::string &smallText = "");

  void ClearActivity();

  bool IsConnected() const { return connected.load(); }

private:
  void StartUpdateThread();
  void TryInitSDK();
  void ApplyPendingActivity();
  static const char *ResultToString(int result);

  std::thread updateThread;
  std::atomic<bool> running{false};
  std::atomic<bool> connected{false};

  std::mutex activityMutex;
  std::string pendingDetails;
  std::string pendingState;
  std::string assetsLargeImage = "logo";
  std::string assetsLargeText = "Ilmeee Engine";
  std::string assetsSmallImage;
  std::string assetsSmallText;
  bool hasPendingActivity = false;

  std::chrono::system_clock::time_point startTimestamp;
  std::chrono::steady_clock::time_point lastReconnectAttempt;
};
