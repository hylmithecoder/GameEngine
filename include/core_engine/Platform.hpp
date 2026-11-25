#pragma once
// The one place engine code includes <windows.h>.
//
// windows.h defines macros that collide with ordinary engine identifiers:
// ERROR (Debug::LogLevel::ERROR), near / far (camera planes), min / max
// (std::min, glm). Include this instead of <windows.h> so they are gone
// everywhere, always in the same way. On other platforms it is empty.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef ERROR
#undef near
#undef far
#undef DELETE
#undef CreateWindow
#endif
