#include "../../../include/core_engine/GifWriter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace ilmeee {
namespace {
using Color = std::array<uint8_t, 3>;

// Median cut over a pixel sample: split the box with the widest channel at
// its median until there are 256 boxes; each box's mean is a palette entry.
std::vector<Color> BuildPalette(const std::vector<std::vector<unsigned char>> &frames,
                                size_t pixelsPerFrame) {
  std::vector<Color> samples;
  const size_t total = pixelsPerFrame * frames.size();
  const size_t step = std::max<size_t>(1, total / 200000);
  for (size_t i = 0; i < total; i += step) {
    const unsigned char *p = &frames[i / pixelsPerFrame][(i % pixelsPerFrame) * 4];
    samples.push_back({p[0], p[1], p[2]});
  }
  struct Box { size_t begin, end; };
  std::vector<Box> boxes{{0, samples.size()}};
  const auto widest = [&](const Box &b, int &channel) {
    int best = -1;
    for (int c = 0; c < 3; ++c) {
      uint8_t lo = 255, hi = 0;
      for (size_t i = b.begin; i < b.end; ++i) {
        lo = std::min(lo, samples[i][c]);
        hi = std::max(hi, samples[i][c]);
      }
      if (hi - lo > best) { best = hi - lo; channel = c; }
    }
    return best;
  };
  while (boxes.size() < 256) {
    int pick = -1, channel = 0, range = 0;
    for (size_t b = 0; b < boxes.size(); ++b) {
      if (boxes[b].end - boxes[b].begin < 2) continue;
      int c = 0;
      const int r = widest(boxes[b], c);
      if (r > range) { range = r; pick = int(b); channel = c; }
    }
    if (pick < 0) break; // every box is a single colour
    Box box = boxes[pick];
    const size_t mid = (box.begin + box.end) / 2;
    std::nth_element(samples.begin() + box.begin, samples.begin() + mid,
                     samples.begin() + box.end,
                     [&](const Color &a, const Color &b) { return a[channel] < b[channel]; });
    boxes[pick] = {box.begin, mid};
    boxes.push_back({mid, box.end});
  }
  std::vector<Color> palette;
  for (const auto &b : boxes) {
    if (b.begin == b.end) continue;
    uint64_t sum[3] = {0, 0, 0};
    for (size_t i = b.begin; i < b.end; ++i)
      for (int c = 0; c < 3; ++c) sum[c] += samples[i][c];
    const uint64_t n = b.end - b.begin;
    palette.push_back({uint8_t(sum[0] / n), uint8_t(sum[1] / n), uint8_t(sum[2] / n)});
  }
  if (palette.empty()) palette.push_back({0, 0, 0});
  palette.resize(256, palette.back());
  return palette;
}

class BitWriter {
public:
  explicit BitWriter(std::vector<unsigned char> &out) : out_(out) {}
  void Write(uint32_t code, int bits) {
    buffer_ |= code << count_;
    count_ += bits;
    while (count_ >= 8) {
      Byte(uint8_t(buffer_ & 0xff));
      buffer_ >>= 8;
      count_ -= 8;
    }
  }
  void Finish() {
    if (count_ > 0) Byte(uint8_t(buffer_ & 0xff));
    buffer_ = 0; count_ = 0;
    FlushBlock();
    out_.push_back(0); // block terminator
  }
private:
  void Byte(uint8_t b) {
    block_[blockSize_++] = b;
    if (blockSize_ == 255) FlushBlock();
  }
  void FlushBlock() {
    if (!blockSize_) return;
    out_.push_back(uint8_t(blockSize_));
    out_.insert(out_.end(), block_, block_ + blockSize_);
    blockSize_ = 0;
  }
  std::vector<unsigned char> &out_;
  uint32_t buffer_ = 0;
  int count_ = 0;
  uint8_t block_[255];
  int blockSize_ = 0;
};

// LZW with a code tree (same scheme as the public-domain gif.h).
void EncodeLzw(const std::vector<uint8_t> &indices, std::vector<unsigned char> &out) {
  constexpr int kMinCodeSize = 8;
  constexpr uint32_t kClear = 1u << kMinCodeSize, kEnd = kClear + 1;
  out.push_back(kMinCodeSize);
  std::vector<std::array<uint16_t, 256>> tree(4096);
  const auto reset = [&] { for (auto &n : tree) n.fill(0); };
  reset();
  BitWriter bits(out);
  int codeSize = kMinCodeSize + 1;
  uint32_t maxCode = kEnd;
  bits.Write(kClear, codeSize);
  int32_t current = -1;
  for (uint8_t value : indices) {
    if (current < 0) { current = value; continue; }
    if (tree[current][value]) { current = tree[current][value]; continue; }
    bits.Write(uint32_t(current), codeSize);
    tree[current][value] = uint16_t(++maxCode);
    if (maxCode >= (1u << codeSize)) ++codeSize;
    if (maxCode == 4095) {
      bits.Write(kClear, codeSize);
      reset();
      codeSize = kMinCodeSize + 1;
      maxCode = kEnd;
    }
    current = value;
  }
  if (current >= 0) bits.Write(uint32_t(current), codeSize);
  bits.Write(kEnd, codeSize);
  bits.Finish();
}

void U16(std::vector<unsigned char> &out, int v) {
  out.push_back(uint8_t(v & 0xff));
  out.push_back(uint8_t((v >> 8) & 0xff));
}
} // namespace

std::vector<unsigned char> EncodeGif(const std::vector<std::vector<unsigned char>> &frames,
                                     int width, int height, int delay) {
  std::vector<unsigned char> out;
  if (frames.empty() || width <= 0 || height <= 0 || width > 65535 || height > 65535)
    return out;
  const size_t pixels = size_t(width) * height;
  for (const auto &f : frames)
    if (f.size() < pixels * 4) return out;
  const auto palette = BuildPalette(frames, pixels);
  // Nearest palette entry per 6-bit colour, filled lazily.
  std::vector<int16_t> lookup(1 << 18, -1);
  const auto nearest = [&](const unsigned char *p) {
    const int key = (p[0] >> 2) << 12 | (p[1] >> 2) << 6 | (p[2] >> 2);
    if (lookup[key] >= 0) return uint8_t(lookup[key]);
    int best = 0, bestDistance = 1 << 30;
    for (int i = 0; i < 256; ++i) {
      const int dr = palette[i][0] - p[0], dg = palette[i][1] - p[1], db = palette[i][2] - p[2];
      const int d = 2 * dr * dr + 4 * dg * dg + 3 * db * db; // rough perceptual weights
      if (d < bestDistance) { bestDistance = d; best = i; }
    }
    lookup[key] = int16_t(best);
    return uint8_t(best);
  };

  const char header[] = "GIF89a";
  out.insert(out.end(), header, header + 6);
  U16(out, width); U16(out, height);
  out.push_back(0xF7); // global table, 8-bit colour resolution, 256 entries
  out.push_back(0);    // background index
  out.push_back(0);    // aspect
  for (const auto &c : palette) out.insert(out.end(), c.begin(), c.end());
  // NETSCAPE2.0: loop forever.
  const unsigned char loop[] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E',
                                '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00};
  out.insert(out.end(), loop, loop + sizeof(loop));
  std::vector<uint8_t> indices(pixels);
  for (const auto &frame : frames) {
    out.insert(out.end(), {0x21, 0xF9, 0x04, 0x00});
    U16(out, std::clamp(delay, 2, 65535));
    out.insert(out.end(), {0x00, 0x00});
    out.push_back(0x2C);
    U16(out, 0); U16(out, 0); U16(out, width); U16(out, height);
    out.push_back(0); // no local table, not interlaced
    for (size_t i = 0; i < pixels; ++i) indices[i] = nearest(&frame[i * 4]);
    EncodeLzw(indices, out);
  }
  out.push_back(0x3B);
  return out;
}

bool WriteGif(const std::string &path, const std::vector<std::vector<unsigned char>> &frames,
              int width, int height, int delay, std::string &error) {
  const auto bytes = EncodeGif(frames, width, height, delay);
  if (bytes.empty()) { error = "no frames to encode"; return false; }
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  const std::string temp = path + ".tmp";
  {
    std::ofstream f(temp, std::ios::binary);
    f.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    if (!f) { error = "cannot write " + temp; return false; }
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) { error = ec.message(); return false; }
  return true;
}

} // namespace ilmeee
