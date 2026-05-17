#include "../../include/core_engine/Debugger.hpp"
#include "../../include/core_engine/SceneRenderer2D.hpp"
#include "../../include/core_engine/TextureManager.hpp"
#include "../../include/ui/Application.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

// Global state management
atomic<bool> g_shutdown_requested{false};
using namespace Debug;

// Global application manager
unique_ptr<ApplicationManager> g_app;

// Signal handlers for proper cleanup
void SignalHandler(int signal) {
  const char *signal_name = "UNKNOWN";
  switch (signal) {
  case SIGINT:
    signal_name = "SIGINT (Ctrl+C)";
    break;
  case SIGTERM:
    signal_name = "SIGTERM";
    break;
  case SIGABRT:
    signal_name = "SIGABRT";
    break;
  case SIGQUIT:
    signal_name = "SIGQUIT";
    break;
  case SIGHUP:
    signal_name = "SIGHUP";
    break;
  }

  Log("Received signal: " + string(signal_name) + " (" + to_string(signal) +
      ")");

  // Set shutdown flag
  g_shutdown_requested.store(true);

  if (g_app) {
    g_app->Shutdown();
  }

  // Give some time for cleanup
  this_thread::sleep_for(chrono::milliseconds(100));

  exit(signal);
}

// Setup signal handlers for Linux
void SetupSignalHandlers() {
  struct sigaction sa;
  sa.sa_handler = SignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART; // Restart interrupted system calls

  // Handle common termination signals
  sigaction(SIGINT, &sa, nullptr);  // Ctrl+C
  sigaction(SIGTERM, &sa, nullptr); // Termination request
  sigaction(SIGABRT, &sa, nullptr); // Abort signal
  sigaction(SIGQUIT, &sa, nullptr); // Quit signal (Ctrl+\)
  sigaction(SIGHUP, &sa, nullptr);  // Hang up signal

  // Ignore SIGPIPE to prevent crashes on broken pipes
  signal(SIGPIPE, SIG_IGN);

  Log("Signal handlers configured for Linux");
}

// Check if running in terminal
bool IsRunningInTerminal() {
  return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

// Print startup information
void PrintStartupInfo() {
  Log("=== Ilmee Editor Starting ===");
  Log("Platform: Linux");
  Log("Terminal: " + string(IsRunningInTerminal() ? "Yes" : "No"));

  // Get process ID
  Log("Process ID: " + to_string(getpid()));

  // Get user information
  char *user = getenv("USER");
  if (user) {
    Log("User: " + string(user));
  }

  // Get display information
  char *display = getenv("DISPLAY");
  if (display) {
    Log("Display: " + string(display));
  } else {
    Log("Display: Not set (may be running headless)");
  }

  // Check for Wayland
  char *wayland_display = getenv("WAYLAND_DISPLAY");
  if (wayland_display) {
    Log("Wayland Display: " + string(wayland_display));
  }
}

// Check system requirements
bool CheckSystemRequirements() {
  Log("Checking system requirements...");

  // Check if we have access to display
  if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) {
    Log("Warning: No display environment detected", Debug::LogLevel::WARNING);
    Log("Make sure you're running in a graphical environment or via SSH with "
        "X11 forwarding",
        Debug::LogLevel::WARNING);
  }

  // Check SDL version
  int compiled = SDL_VERSION;
  int linked = SDL_GetVersion();

  Log("SDL Version - Compiled: " + to_string(SDL_VERSIONNUM_MAJOR(compiled)) +
      "." + to_string(SDL_VERSIONNUM_MINOR(compiled)) + "." +
      to_string(SDL_VERSIONNUM_MICRO(compiled)));
  Log("SDL Version - Linked: " + to_string(SDL_VERSIONNUM_MAJOR(linked)) + "." +
      to_string(SDL_VERSIONNUM_MINOR(linked)) + "." +
      to_string(SDL_VERSIONNUM_MICRO(linked)));

  return true;
}

// Main application entry point
int main(int argc, char *argv[]) {
  // Force line buffering on stdout/stderr so Log output streams to a
  // parent's pipe (e.g. IlmeeeHub's console) line-by-line instead of
  // sitting in a 4KB buffer until the process exits. When stdout is a
  // terminal glibc would line-buffer anyway; this only matters when
  // launched under Hub.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setvbuf(stderr, nullptr, _IOLBF, 0);

  // Print startup information
  PrintStartupInfo();

  // Set up signal handlers
  SetupSignalHandlers();

  // Check system requirements
  if (!CheckSystemRequirements()) {
    Log("System requirements check failed", Debug::LogLevel::CRASH);
    return 1;
  }

  // CLI: `--project <abs_path>` (or `--project=<path>`). When provided,
  // the editor will auto-open this project. Otherwise the hardcoded
  // debug scene runs — useful for first-gen UI/render testing without
  // going through IlmeeeHub.
  std::string cliProjectPath;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--project" && i + 1 < argc) {
      cliProjectPath = argv[++i];
    } else if (a.rfind("--project=", 0) == 0) {
      cliProjectPath = a.substr(strlen("--project="));
    }
  }
  if (!cliProjectPath.empty()) {
    Log("CLI project path: " + cliProjectPath, Debug::LogLevel::SUCCESS);
  } else {
    Log("No --project supplied; running in standalone debug mode.");
  }

  try {
    // Create application manager
    Log("Creating application manager...");
    g_app = make_unique<ApplicationManager>();
    if (!cliProjectPath.empty()) {
      g_app->SetProjectPath(cliProjectPath);
    }

    // Launch engine
    Log("Launching engine...");
    if (!g_app->LaunchEngine()) {
      Log("Failed to launch engine", Debug::LogLevel::CRASH);
      return 1;
    }

    // Initialize application
    Log("Initializing application...");
    if (!g_app->Initialize()) {
      Log("Failed to initialize application", Debug::LogLevel::CRASH);
      return 1;
    }

    Log("Application initialized successfully");
    Log("Entering main loop...");

    // Run main loop
    g_app->Run();

    Log("Main loop exited");

    // Explicit shutdown before cleanup
    Log("Shutting down application...");
    g_app->Shutdown();
    g_app.reset();
    g_app = nullptr;

    Log("Application manager cleaned up");
    Log("=== Ilmee Editor Terminated Successfully ===");
    return 0;

  } catch (const exception &e) {
    Log("Unhandled exception: " + string(e.what()), Debug::LogLevel::CRASH);

    if (g_app) {
      try {
        g_app->Shutdown();
        g_app.reset();
        g_app = nullptr;
      } catch (...) {
        Log("Exception during emergency shutdown", Debug::LogLevel::CRASH);
      }
    }

    Log("=== Ilmee Editor Terminated with Error ===");
    return 1;

  } catch (...) {
    Log("Unknown exception caught", Debug::LogLevel::CRASH);

    if (g_app) {
      try {
        g_app->Shutdown();
        g_app.reset();
        g_app = nullptr;
      } catch (...) {
        Log("Exception during emergency shutdown", Debug::LogLevel::CRASH);
        // Ignore exceptions during emergency shutdown
      }
    }

    Log("=== Ilmee Editor Terminated with Unknown Error ===");
    return 1;
  }
}