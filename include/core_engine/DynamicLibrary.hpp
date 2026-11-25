#pragma once
// Load a shared library and look up symbols: dlopen/dlsym on POSIX,
// LoadLibraryExW/GetProcAddress on Windows. Paths are UTF-8.
//
//   std::string err;
//   void *lib = ilmeee::OpenLibrary("/path/libX.so", err);
//   auto fn = reinterpret_cast<Fn>(ilmeee::LibrarySymbol(lib, "Name", err));
//   ilmeee::CloseLibrary(lib);

#include <string>

#ifdef _WIN32
#include "Platform.hpp"
#include <filesystem>
#else
#include <dlfcn.h>
#endif

namespace ilmeee {

#ifdef _WIN32
inline std::string Win32ErrorText(unsigned long code) {
  char *buf = nullptr;
  const DWORD n = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<char *>(&buf), 0, nullptr);
  std::string msg = n && buf ? std::string(buf, n) : "error " + std::to_string(code);
  if (buf)
    LocalFree(buf);
  while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' '))
    msg.pop_back();
  return msg;
}
#endif

inline void *OpenLibrary(const std::string &path, std::string &error) {
#ifdef _WIN32
  const std::filesystem::path p(std::u8string(path.begin(), path.end()));
  // Absolute paths: also search the DLL's own folder for its dependencies.
  const DWORD flags = p.is_absolute()
                          ? LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
                          : 0;
  HMODULE h = LoadLibraryExW(p.wstring().c_str(), nullptr, flags);
  if (!h)
    error = Win32ErrorText(GetLastError());
  return reinterpret_cast<void *>(h);
#else
  void *h = dlopen(path.c_str(), RTLD_LAZY);
  if (!h) {
    const char *e = dlerror();
    error = e ? e : "dlopen failed";
  }
  return h;
#endif
}

inline void *LibrarySymbol(void *lib, const char *name, std::string &error) {
  if (!lib) {
    error = "library not loaded";
    return nullptr;
  }
#ifdef _WIN32
  FARPROC f = GetProcAddress(reinterpret_cast<HMODULE>(lib), name);
  if (!f)
    error = Win32ErrorText(GetLastError());
  return reinterpret_cast<void *>(f);
#else
  dlerror();
  void *s = dlsym(lib, name);
  if (const char *e = dlerror()) {
    error = e;
    return nullptr;
  }
  return s;
#endif
}

inline void CloseLibrary(void *lib) {
  if (!lib)
    return;
#ifdef _WIN32
  FreeLibrary(reinterpret_cast<HMODULE>(lib));
#else
  dlclose(lib);
#endif
}

} // namespace ilmeee
