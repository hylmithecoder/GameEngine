#pragma once
// Dialog component: a character (any Mesh3D) you talk to. Plain data like
// the media components; DialogSystem runs it during Play. The UI links are
// objectIds of Canvas elements, so one dialog box can serve every character.
//
// Dialog script, one line per entry:
//   Sakura: Halo!        speaker "Sakura"
//   Apa kabar?           no "Name:" prefix: speaker is the object's name
//   : Angin bertiup.     empty name: narration, the name element is hidden
//   # comment            ignored, like empty lines
// "\n" inside a line is a line break. The prefix only counts as a name when
// it is followed by ": " and is at most 40 bytes, so "Jam 10:30" stays text.

#include <cstdint>
#include <string>
#include <vector>

namespace ilmeee {

enum class DialogTrigger : uint32_t {
  Interact = 0,   // E / Enter / an "Interact" button near the character
  OnStart = 1,    // opens when Play starts
  ScriptOnly = 2, // only DialogGameObject::StartDialog()
};
inline constexpr const char *kDialogTriggerNames =
    "Interact\0On Start\0Script Only\0";

struct DialogComponent {
  bool enabled = false;
  uint64_t box = 0;     // panel shown while talking
  uint64_t text = 0;    // element receiving the line
  uint64_t speaker = 0; // optional element receiving the speaker's name
  uint64_t prompt = 0;  // optional hint shown while the player is in range
  std::string lines;    // inline dialog script
  std::string file;     // project-relative text file; replaces `lines`
  DialogTrigger trigger = DialogTrigger::Interact;
  float range = 2.5f;           // interact distance to the player
  float charsPerSecond = 40.0f; // typewriter speed, 0 = whole line at once
  bool repeatable = true;       // can be talked to again after the end
  bool freezePlayer = true;     // movement input is ignored while talking
};

struct DialogLine {
  std::string speaker;
  std::string text;
};

inline std::vector<DialogLine> ParseDialogScript(const std::string &source,
                                                 const std::string &defaultSpeaker) {
  std::vector<DialogLine> lines;
  size_t start = 0;
  while (start <= source.size()) {
    size_t end = source.find('\n', start);
    if (end == std::string::npos)
      end = source.size();
    std::string row = source.substr(start, end - start);
    start = end + 1;
    if (!row.empty() && row.back() == '\r')
      row.pop_back();
    const size_t first = row.find_first_not_of(" \t");
    if (first == std::string::npos || row[first] == '#')
      continue;
    row.erase(0, first);
    while (!row.empty() && (row.back() == ' ' || row.back() == '\t'))
      row.pop_back();

    DialogLine line{defaultSpeaker, row};
    if (row[0] == ':') {
      line.speaker.clear();
      line.text = row.substr(1);
    } else {
      const size_t colon = row.find(": ");
      if (colon != std::string::npos && colon <= 40) {
        line.speaker = row.substr(0, colon);
        line.text = row.substr(colon + 2);
      }
    }
    const size_t lead = line.text.find_first_not_of(" \t");
    line.text.erase(0, lead == std::string::npos ? line.text.size() : lead);
    for (size_t at = 0; (at = line.text.find("\\n", at)) != std::string::npos;)
      line.text.replace(at, 2, "\n"), ++at;
    if (!line.text.empty())
      lines.push_back(std::move(line));
    if (end == source.size())
      break;
  }
  return lines;
}

// Code points in a UTF-8 string, and its first `count` code points; used by
// the typewriter so multi-byte characters are never cut in half.
inline size_t Utf8Length(const std::string &text) {
  size_t count = 0;
  for (unsigned char c : text)
    count += (c & 0xC0) != 0x80;
  return count;
}
inline std::string Utf8Prefix(const std::string &text, size_t count) {
  size_t i = 0;
  for (; i < text.size(); ++i)
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 && count-- == 0)
      break;
  return text.substr(0, i);
}

} // namespace ilmeee
