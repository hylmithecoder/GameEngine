#include "../../include/core_engine/Discordrich.hpp"

#include <cstring>
#include <iostream>

namespace {
constexpr DiscordClientId kClientId = 1410567823054475325;
constexpr auto kReconnectInterval = std::chrono::seconds(10);
constexpr auto kPollInterval = std::chrono::milliseconds(250);

static IDiscordCore *core = nullptr;
static IDiscordActivityManager *activities = nullptr;

void LogLine(const std::string &msg) {
  std::cout << "[Discord] " << msg << std::endl;
}
void LogErr(const std::string &msg) {
  std::cerr << "[Discord] " << msg << std::endl;
}
} // namespace

const char *DiscordRichPresence::ResultToString(int r) {
  switch (r) {
  case DiscordResult_Ok:
    return "Ok";
  case DiscordResult_ServiceUnavailable:
    return "ServiceUnavailable (Discord client not running?)";
  case DiscordResult_InvalidVersion:
    return "InvalidVersion";
  case DiscordResult_LockFailed:
    return "LockFailed";
  case DiscordResult_InternalError:
    return "InternalError";
  case DiscordResult_InvalidPayload:
    return "InvalidPayload";
  case DiscordResult_InvalidCommand:
    return "InvalidCommand";
  case DiscordResult_InvalidPermissions:
    return "InvalidPermissions";
  case DiscordResult_NotFetched:
    return "NotFetched";
  case DiscordResult_NotFound:
    return "NotFound";
  case DiscordResult_Conflict:
    return "Conflict";
  case DiscordResult_InvalidSecret:
    return "InvalidSecret";
  case DiscordResult_InvalidJoinSecret:
    return "InvalidJoinSecret";
  case DiscordResult_NoEligibleActivity:
    return "NoEligibleActivity";
  case DiscordResult_InvalidInvite:
    return "InvalidInvite";
  case DiscordResult_NotAuthenticated:
    return "NotAuthenticated";
  case DiscordResult_InvalidAccessToken:
    return "InvalidAccessToken";
  case DiscordResult_ApplicationMismatch:
    return "ApplicationMismatch";
  case DiscordResult_InvalidDataUrl:
    return "InvalidDataUrl";
  case DiscordResult_InvalidBase64:
    return "InvalidBase64";
  case DiscordResult_NotFiltered:
    return "NotFiltered";
  case DiscordResult_LobbyFull:
    return "LobbyFull";
  case DiscordResult_InvalidLobbySecret:
    return "InvalidLobbySecret";
  case DiscordResult_InvalidFilename:
    return "InvalidFilename";
  case DiscordResult_InvalidFileSize:
    return "InvalidFileSize";
  case DiscordResult_InvalidEntitlement:
    return "InvalidEntitlement";
  case DiscordResult_NotInstalled:
    return "NotInstalled";
  case DiscordResult_NotRunning:
    return "NotRunning (Discord client not launched)";
  default:
    return "UnknownResult";
  }
}

void DiscordRichPresence::TryInitSDK() {
  DiscordCreateParamsSetDefault(&params);
  params.client_id = kClientId;
  params.flags = DiscordCreateFlags_NoRequireDiscord;

  EDiscordResult r = DiscordCreate(DISCORD_VERSION, &params, &core);
  lastReconnectAttempt = std::chrono::steady_clock::now();

  if (r == DiscordResult_Ok && core) {
    activities = core->get_activity_manager(core);
    connected = true;
    LogLine(std::string("SDK connected (client_id=") +
            std::to_string(kClientId) + ")");
    {
      std::lock_guard<std::mutex> lock(activityMutex);
      if (hasPendingActivity) {
        ApplyPendingActivity();
      }
    }
  } else {
    activities = nullptr;
    core = nullptr;
    connected = false;
    LogErr(std::string("SDK init failed: ") + ResultToString(r) +
           " — will retry in " + std::to_string(kReconnectInterval.count()) +
           "s");
  }
}

void DiscordRichPresence::Init() {
  LogLine("Init() called");
  startTimestamp = std::chrono::system_clock::now();
  TryInitSDK();
  StartUpdateThread();

  // Default presence while editor is starting up. Caller can override
  // via SetActivity() once UI/project state is known.
  SetActivity("Starting up", "Loading editor...");
}

void DiscordRichPresence::StartUpdateThread() {
  running = true;
  updateThread = std::thread([this]() {
    LogLine("Update thread started");
    while (running.load()) {
      if (core) {
        EDiscordResult r = core->run_callbacks(core);
        if (r != DiscordResult_Ok) {
          LogErr(std::string("run_callbacks failed: ") + ResultToString(r) +
                 " — dropping connection");
          core->destroy(core);
          core = nullptr;
          activities = nullptr;
          connected = false;
        }
      } else {
        auto now = std::chrono::steady_clock::now();
        if (now - lastReconnectAttempt >= kReconnectInterval) {
          LogLine("Retrying SDK init...");
          TryInitSDK();
        }
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    LogLine("Update thread exiting");
  });
}

void DiscordRichPresence::Update() {
  // Reserved: callable from main loop if caller prefers not using the
  // internal thread. Currently a no-op because the thread handles it.
}

void DiscordRichPresence::Shutdown() {
  LogLine("Shutdown() called");
  running = false;
  if (updateThread.joinable()) {
    updateThread.join();
  }
  if (core) {
    core->destroy(core);
    core = nullptr;
    activities = nullptr;
  }
  connected = false;
  LogLine("Shutdown complete");
}

void DiscordRichPresence::SetAssets(const std::string &largeImage,
                                    const std::string &largeText,
                                    const std::string &smallImage,
                                    const std::string &smallText) {
  std::lock_guard<std::mutex> lock(activityMutex);
  assetsLargeImage = largeImage;
  assetsLargeText = largeText;
  assetsSmallImage = smallImage;
  assetsSmallText = smallText;
  LogLine("Assets set: large='" + largeImage + "' small='" + smallImage + "'");
}

void DiscordRichPresence::ApplyPendingActivity() {
  if (!activities) {
    LogLine("ApplyPendingActivity skipped: no activity manager (will replay "
            "after reconnect)");
    return;
  }

  DiscordActivity activity{};
  std::strncpy(activity.details, pendingDetails.c_str(),
               sizeof(activity.details) - 1);
  std::strncpy(activity.state, pendingState.c_str(),
               sizeof(activity.state) - 1);

  std::strncpy(activity.assets.large_image, assetsLargeImage.c_str(),
               sizeof(activity.assets.large_image) - 1);
  std::strncpy(activity.assets.large_text, assetsLargeText.c_str(),
               sizeof(activity.assets.large_text) - 1);
  if (!assetsSmallImage.empty()) {
    std::strncpy(activity.assets.small_image, assetsSmallImage.c_str(),
                 sizeof(activity.assets.small_image) - 1);
    std::strncpy(activity.assets.small_text, assetsSmallText.c_str(),
                 sizeof(activity.assets.small_text) - 1);
  }

  activity.timestamps.start = std::chrono::duration_cast<std::chrono::seconds>(
                                  startTimestamp.time_since_epoch())
                                  .count();
  activity.type = DiscordActivityType_Playing;

  const std::string logDetails = pendingDetails;
  const std::string logState = pendingState;

  activities->update_activity(
      activities, &activity, nullptr, [](void *data, EDiscordResult result) {
        if (result == DiscordResult_Ok) {
          std::cout << "[Discord] Activity updated successfully" << std::endl;
        } else {
          std::cerr << "[Discord] Activity update failed: "
                    << DiscordRichPresence::ResultToString(result) << std::endl;
        }
      });

  LogLine("Activity dispatched: details='" + logDetails + "' state='" +
          logState + "'");
}

void DiscordRichPresence::SetActivity(const std::string &details,
                                      const std::string &state) {
  std::lock_guard<std::mutex> lock(activityMutex);
  pendingDetails = details;
  pendingState = state;
  hasPendingActivity = true;

  if (activities) {
    ApplyPendingActivity();
  } else {
    LogLine("Activity queued (SDK not connected yet): '" + details + "' | '" +
            state + "'");
  }
}

void DiscordRichPresence::ClearActivity() {
  std::lock_guard<std::mutex> lock(activityMutex);
  hasPendingActivity = false;
  pendingDetails.clear();
  pendingState.clear();
  if (activities) {
    activities->clear_activity(
        activities, nullptr, [](void *data, EDiscordResult r) {
          std::cout << "[Discord] Clear activity: "
                    << DiscordRichPresence::ResultToString(r) << std::endl;
        });
  }
}
