#pragma once

namespace Ilmeee {

// Centralized ImGui theme for the editor. Lives outside MainWindow so
// it can be re-applied from any context (settings change, hot-reload)
// and so the styling is in one place.
class EditorTheme {
public:
  enum class Variant { Dark, Light };

  static void Apply(Variant v = Variant::Dark);

  // Convenience wrappers (kept for readability at call sites).
  static void ApplyDark() { Apply(Variant::Dark); }
  static void ApplyLight() { Apply(Variant::Light); }
};

} // namespace Ilmeee
