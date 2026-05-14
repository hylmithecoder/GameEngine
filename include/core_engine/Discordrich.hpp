#include <chrono>
#include <cstddef>
#include <curl/curl.h>
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
  void updateDiscordStatus(const std::string &token, const std::string &text);

private:
  std::thread updateThread;
  bool running = false;

  std::string activityTitle;
  std::string activityDetails;
  std::string activityState;
  std::string activityAssetsLargeImage;
  std::string activityAssetsLargeText;
  std::string activityAssetsSmallImage;
  std::string activityAssetsSmallText;
  std::string activityPartyId;
  std::string activityPartySize;
  std::string activityPartyMax;
  std::string activityPartyType;
  std::string activitySecretsJoin;
  std::string activitySecretsSpectate;
  std::string activitySecretsMatch;
  std::string activityTimestampsStart;
  std::string activityTimestampsEnd;
  std::string activityType;
  std::string activityInstance;
};