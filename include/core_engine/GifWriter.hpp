#pragma once
// Minimal animated GIF89a writer: one global 256-colour palette (median cut
// over all frames), LZW, looping forever. Enough for short previews; no
// dithering, no transparency.

#include <string>
#include <vector>

namespace ilmeee {

// `frames` are RGBA8 (alpha ignored), all `width` x `height`.
// delayCentiseconds is per frame (GIF timing unit, 2 = 50 fps max).
bool WriteGif(const std::string &path,
              const std::vector<std::vector<unsigned char>> &frames, int width,
              int height, int delayCentiseconds, std::string &error);

// Same, into memory.
std::vector<unsigned char> EncodeGif(
    const std::vector<std::vector<unsigned char>> &frames, int width, int height,
    int delayCentiseconds);

} // namespace ilmeee
