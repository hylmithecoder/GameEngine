#include "../../include/core_engine/Discordrich.hpp"
#include <cstring>
#include <curl/curl.h>
#include <iostream>
#include <thread>

static IDiscordCore *core = nullptr;
static IDiscordActivityManager *activities = nullptr;

void DiscordRichPresence::Init() {
  // Inisialisasi SDK (Opsional, akan gagal jika Client ID tidak valid)
  DiscordCreateParams params{};
  DiscordCreateParamsSetDefault(&params);

  // Gunakan ID Default atau minta user buat sendiri
  // Jika tidak ingin pakai, biarkan saja, kita akan pakai updateDiscordStatus
  params.client_id = 1306305096530133093;
  params.flags = DiscordCreateFlags_Default;

  enum EDiscordResult result = DiscordCreate(DISCORD_VERSION, &params, &core);
  if (result == DiscordResult_Ok) {
    activities = core->get_activity_manager(core);
    std::cout << "[Discord] SDK Initialized successfully." << std::endl;
  } else {
    std::cout << "[Discord] SDK Init skipped/failed (Result: " << result
              << "). Using Custom Status instead." << std::endl;
  }

  running = true;

  // Start update thread
  updateThread = std::thread([this]() {
    while (running) {
      if (core) {
        core->run_callbacks(core);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  });

  SetActivity("Building Ilmeee Engine", "The future of 3D");
}

void DiscordRichPresence::Update() {
  // Update logic if needed
}

void DiscordRichPresence::Shutdown() {
  running = false;
  if (updateThread.joinable()) {
    updateThread.join();
  }
  if (core) {
    core->destroy(core);
    core = nullptr;
  }
}

void DiscordRichPresence::SetActivity(const std::string &details,
                                      const std::string &state) {
  // 1. Update via Official SDK (jika aktif)
  if (activities) {
    DiscordActivity activity{};
    std::strncpy(activity.details, details.c_str(), sizeof(activity.details));
    std::strncpy(activity.state, state.c_str(), sizeof(activity.state));

    std::strncpy(activity.assets.large_image, "logo",
                 sizeof(activity.assets.large_image));
    std::strncpy(activity.assets.large_text, "Ilmeee Engine",
                 sizeof(activity.assets.large_text));

    activities->update_activity(activities, &activity, nullptr,
                                [](void *data, enum EDiscordResult result) {
                                  if (result != DiscordResult_Ok) {
                                    // std::cerr << "[Discord] Failed to update
                                    // Rich Presence: " << result << std::endl;
                                  }
                                });
  }

  // 2. Update via Custom Status (Self-bot approach)
  // PERINGATAN: Simpan token di tempat aman, jangan di hardcode selamanya!
  std::string myToken =
      "MTA0Nzc4MDMyNzc4OTE3NDgyNA.yvbwWLgcjaq4YuxHhU423wcdTF0";
  updateDiscordStatus(myToken, details + " | " + state);
}

void DiscordRichPresence::updateDiscordStatus(const std::string &token,
                                              const std::string &text) {
  CURL *curl = curl_easy_init();
  if (curl) {
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, ("Authorization: " + token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // Escape JSON (sederhana)
    std::string safeText = text;
    size_t pos = 0;
    while ((pos = safeText.find("\"", pos)) != std::string::npos) {
      safeText.replace(pos, 1, "\\\"");
      pos += 2;
    }

    std::string jsonData =
        "{\"custom_status\": {\"text\": \"" + safeText + "\"}}";

    curl_easy_setopt(curl, CURLOPT_URL,
                     "https://discord.com/api/v9/users/@me/settings");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());

    // Timeout agar tidak blocking terlalu lama
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      std::cerr << "[Discord] Status update failed: " << curl_easy_strerror(res)
                << std::endl;
    } else {
      std::cout << "[Discord] Custom status updated: " << text << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
  }
}