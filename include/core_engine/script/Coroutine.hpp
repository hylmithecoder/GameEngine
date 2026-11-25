#pragma once
// Unity-style coroutines on C++20 co_await. A member function returning
// Coroutine can pause without blocking the game:
//
//   Coroutine Blink() {
//     for (int i = 0; i < 3; ++i) {
//       Log("on");  co_await WaitForSeconds(0.5f);
//       Log("off"); co_await WaitForSeconds(0.5f);
//     }
//   }
//   void Start() override { StartCoroutine(Blink()); }
//
// Coroutines advance once per frame after Update, and are destroyed with
// their component when Play stops. Header-only: everything runs inside the
// script module.
#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <utility>
#include <vector>
#include <cstddef>

namespace ilmeee::script {
class Coroutine {
public:
  struct promise_type {
    float wait = 0;                // seconds left (WaitForSeconds)
    std::function<bool()> until;   // resume once true (WaitUntil/WaitWhile)
    std::coroutine_handle<promise_type> child; // awaited nested Coroutine
    uint64_t frame = 0;            // last frame it ran; at most once per frame
    std::exception_ptr error;
    Coroutine get_return_object() {
      return Coroutine{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { error = std::current_exception(); }
  };
  using Handle = std::coroutine_handle<promise_type>;

  Coroutine() = default;
  explicit Coroutine(Handle handle) : handle_(handle) {}
  Coroutine(Coroutine &&other) noexcept
      : handle_(std::exchange(other.handle_, {})) {}
  Coroutine &operator=(Coroutine &&other) noexcept {
    if (this != &other) {
      Reset();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }
  Coroutine(const Coroutine &) = delete;
  Coroutine &operator=(const Coroutine &) = delete;
  ~Coroutine() { Reset(); }

  // `co_await Other();` runs Other to completion before continuing.
  bool await_ready() const noexcept { return !handle_ || handle_.done(); }
  std::coroutine_handle<> await_suspend(Handle parent) noexcept {
    parent.promise().child = std::exchange(handle_, {});
    return parent.promise().child; // start the child right away
  }
  void await_resume() const noexcept {}

  Handle Release() { return std::exchange(handle_, {}); }

private:
  void Reset() {
    if (handle_)
      handle_.destroy();
    handle_ = {};
  }
  Handle handle_;
};

namespace detail {
// Awaitables only fill in the promise; CoroutineRunner decides when to resume.
struct PromiseAwaiter {
  bool await_ready() const noexcept { return false; }
  void await_resume() const noexcept {}
};
} // namespace detail

// Pause for game seconds (the same delta time Update receives).
struct WaitForSeconds : detail::PromiseAwaiter {
  float seconds;
  explicit WaitForSeconds(float value) : seconds(value) {}
  void await_suspend(Coroutine::Handle handle) const noexcept {
    handle.promise().wait = seconds;
  }
};
// Pause until the next frame.
struct WaitForNextFrame : detail::PromiseAwaiter {
  void await_suspend(Coroutine::Handle) const noexcept {}
};
// Check the condition every frame; continue once it returns true.
struct WaitUntil : detail::PromiseAwaiter {
  std::function<bool()> condition;
  explicit WaitUntil(std::function<bool()> value)
      : condition(std::move(value)) {}
  void await_suspend(Coroutine::Handle handle) {
    handle.promise().until = std::move(condition);
  }
};
// Continue once the condition returns false.
struct WaitWhile : WaitUntil {
  explicit WaitWhile(std::function<bool()> value)
      : WaitUntil([check = std::move(value)] { return !check(); }) {}
};

using CoroutineId = uint64_t;

class CoroutineRunner {
public:
  CoroutineRunner() = default;
  CoroutineRunner(const CoroutineRunner &) = delete;
  CoroutineRunner &operator=(const CoroutineRunner &) = delete;
  ~CoroutineRunner() {
    for (auto &entry : running_)
      Destroy(entry.handle);
  }

  // Runs immediately up to the first co_await; 0 if it finished already.
  CoroutineId Start(Coroutine coroutine) {
    auto handle = coroutine.Release();
    if (!handle)
      return 0;
    bool alive = false;
    try {
      alive = Resume(handle);
    } catch (...) {
      Destroy(handle);
      throw;
    }
    if (!alive) {
      Destroy(handle);
      return 0;
    }
    running_.push_back({++lastId_, handle});
    return lastId_;
  }
  // Safe from inside a coroutine, even for itself: it is destroyed once it
  // next pauses.
  void Stop(CoroutineId id) {
    for (auto &entry : running_)
      if (entry.id == id)
        entry.stopped = true;
    if (!ticking_)
      Sweep();
  }
  void StopAll() {
    for (auto &entry : running_)
      entry.stopped = true;
    if (!ticking_)
      Sweep();
  }
  bool IsRunning(CoroutineId id) const {
    for (const auto &entry : running_)
      if (entry.id == id && !entry.stopped)
        return true;
    return false;
  }
  size_t Count() const {
    size_t count = 0;
    for (const auto &entry : running_)
      count += entry.stopped ? 0 : 1;
    return count;
  }
  // Called once per frame after Update. A coroutine that throws is stopped
  // and the exception propagates (the engine reports the script error).
  void Tick(float deltaTime) {
    ticking_ = true;
    try {
      // Index loop: a coroutine may start new ones while it runs; those
      // wait for the next frame.
      for (size_t i = 0; i < running_.size(); ++i) {
        if (running_[i].stopped)
          continue;
        const auto handle = running_[i].handle;
        bool alive = false;
        try {
          alive = Step(handle, deltaTime);
        } catch (...) {
          running_[i].stopped = true;
          throw;
        }
        if (!alive)
          running_[i].stopped = true;
      }
    } catch (...) {
      ticking_ = false;
      Sweep();
      ++frame_;
      throw;
    }
    ticking_ = false;
    Sweep();
    ++frame_;
  }

private:
  struct Entry {
    CoroutineId id;
    Coroutine::Handle handle;
    bool stopped = false;
  };
  void Sweep() {
    std::erase_if(running_, [](const Entry &entry) {
      if (entry.stopped)
        Destroy(entry.handle);
      return entry.stopped;
    });
  }
  // Returns false once the coroutine has finished.
  bool Step(Coroutine::Handle handle, float deltaTime) {
    auto &promise = handle.promise();
    if (handle.done())
      return false;
    if (promise.frame == frame_)
      return true; // started or resumed this frame already
    if (promise.child) {
      if (Step(promise.child, deltaTime))
        return true;
      auto error = promise.child.promise().error;
      promise.child.destroy();
      promise.child = {};
      if (error)
        std::rethrow_exception(error);
    } else if (promise.wait > 0) {
      promise.wait -= deltaTime;
      if (promise.wait > 1e-4f) // tolerate float drift: 0.2 - 0.1 - 0.1 > 0
        return true;
    } else if (promise.until) {
      if (!promise.until())
        return true;
    }
    promise.wait = 0;
    promise.until = nullptr;
    return Resume(handle);
  }
  bool Resume(Coroutine::Handle handle) {
    handle.resume();
    // Children started by this resume have run this frame too.
    for (auto h = handle; h; h = h.promise().child) {
      h.promise().frame = frame_;
      if (h.promise().error)
        std::rethrow_exception(h.promise().error);
    }
    return !handle.done();
  }
  static void Destroy(Coroutine::Handle handle) {
    if (!handle)
      return;
    Destroy(handle.promise().child);
    handle.destroy();
  }

  std::vector<Entry> running_;
  CoroutineId lastId_ = 0;
  uint64_t frame_ = 0; // completed ticks
  bool ticking_ = false;
};
} // namespace ilmeee::script

#ifdef ILMEEE_SCRIPT_BUILD
using Coroutine = ilmeee::script::Coroutine;
using CoroutineId = ilmeee::script::CoroutineId;
using WaitForSeconds = ilmeee::script::WaitForSeconds;
using WaitForNextFrame = ilmeee::script::WaitForNextFrame;
using WaitUntil = ilmeee::script::WaitUntil;
using WaitWhile = ilmeee::script::WaitWhile;
#endif
