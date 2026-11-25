#pragma once
// Model previews for the asset Explorer (.pmx / .obj / .fbx).
//
// Rendering is a small CPU rasterizer (z-buffer, Lambert light, diffuse
// texture, 2x supersampling) so it needs no Vulkan context and can run on a
// background thread. Results are cached as PNG files under
// ~/.ilmeeeengine/thumbnails, keyed by path + size + mtime, so a model is only
// rendered again after it changes.

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace ilmeee {

bool IsPreviewableModel(const std::string &path);

// Render `modelPath` to a transparent `size` x `size` PNG at `pngPath`.
bool RenderModelThumbnail(const std::string &modelPath,
                          const std::string &pngPath, int size,
                          std::string &error);

class ModelThumbnailCache {
public:
  ModelThumbnailCache() = default;
  ~ModelThumbnailCache();
  ModelThumbnailCache(const ModelThumbnailCache &) = delete;
  ModelThumbnailCache &operator=(const ModelThumbnailCache &) = delete;

  // Returns the cached PNG path once it is ready, "" while it is rendering
  // or when the model cannot be previewed. Never blocks on rendering.
  std::string Request(const std::string &modelPath);

private:
  enum class State { Pending, Ready, Failed };
  struct Entry {
    State state = State::Pending;
    std::string png;
  };
  void Worker();

  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::string> queue_;
  std::unordered_map<std::string, Entry> entries_;
  std::thread worker_;
  bool stopping_ = false;
};

} // namespace ilmeee
