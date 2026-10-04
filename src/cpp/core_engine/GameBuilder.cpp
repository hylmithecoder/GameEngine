#include "../../../include/core_engine/GameBuilder.hpp"
#include "../../../include/core_engine/IlmeeeScene.hpp"
#include "../../../vendor/nlohmann/json.hpp"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sys/wait.h>

namespace ilmeee {

namespace fs = std::filesystem;

namespace {

// An external model is copied together with its folder (PMX/OBJ keep their
// textures next to them). Past this size that folder is clearly not "the
// model's folder" — Downloads, $HOME — and only the file itself is copied.
constexpr size_t kMaxModelDirFiles = 2000;
constexpr std::uintmax_t kMaxModelDirBytes = 2ull << 30; // 2 GiB

// True if `p` is `dir` or below it. Both must already be canonical.
bool IsWithin(const fs::path &p, const fs::path &dir) {
  fs::path rel = p.lexically_relative(dir);
  return !rel.empty() && *rel.begin() != "..";
}

fs::path Canonical(const fs::path &p) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  return ec ? fs::absolute(p).lexically_normal() : c;
}

std::string HumanBytes(std::uintmax_t b) {
  char buf[32];
  if (b >= (1ull << 30))
    std::snprintf(buf, sizeof buf, "%.1f GiB", b / double(1ull << 30));
  else if (b >= (1ull << 20))
    std::snprintf(buf, sizeof buf, "%.1f MiB", b / double(1ull << 20));
  else
    std::snprintf(buf, sizeof buf, "%.1f KiB", b / 1024.0);
  return buf;
}

std::string ShellQuote(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  return out + "'";
}

class Builder {
public:
  Builder(const GameBuildOptions &o, const BuildLog &log, GameBuildResult &r)
      : o_(o), log_(log), r_(r) {}

  void Say(const std::string &s) const {
    if (log_)
      log_(s);
  }

  void CopyFile(const fs::path &src, const fs::path &dst) {
    fs::create_directories(dst.parent_path());
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    ++r_.filesCopied;
    std::error_code ec;
    r_.bytesCopied += fs::file_size(dst, ec);
    if (r_.filesCopied % 500 == 0)
      Say("  ... " + std::to_string(r_.filesCopied) + " files (" +
          HumanBytes(r_.bytesCopied) + ")");
  }

  void CopyTree(const fs::path &src, const fs::path &dst) {
    fs::create_directories(dst);
    for (auto it = fs::recursive_directory_iterator(
             src, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
      fs::path target = dst / it->path().lexically_relative(src);
      if (it->is_directory())
        fs::create_directories(target);
      else if (it->is_regular_file())
        CopyFile(it->path(), target);
    }
  }

  // May this folder be copied wholesale with a model? Not if it is huge, and
  // never if it holds the build output (a model sitting directly in the
  // project root would otherwise copy the build into itself, forever).
  bool CanCopyWhole(const fs::path &dir) const {
    if (IsWithin(out_, dir) || IsWithin(stage_, dir))
      return false;
    size_t files = 0;
    std::uintmax_t bytes = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec))
        continue;
      bytes += it->file_size(ec);
      if (++files > kMaxModelDirFiles || bytes > kMaxModelDirBytes)
        return false;
    }
    return true;
  }

  std::string UniqueExternalName(const std::string &want) {
    std::string name = want.empty() ? "external" : want;
    std::string candidate = name;
    for (int n = 2; usedExternalNames_.count(candidate); ++n)
      candidate = name + "_" + std::to_string(n);
    usedExternalNames_.insert(candidate);
    return candidate;
  }

  // Makes a scene reference valid inside the built game and returns the path
  // to store in its scene file (relative to the game root).
  std::string Relocate(const std::string &ref, bool isModel) {
    if (ref.empty())
      return ref;
    fs::path p(ref);
    fs::path abs = Canonical(p.is_absolute() ? p : root_ / p);
    std::error_code ec;
    if (!fs::exists(abs, ec)) {
      ++r_.missingReferences;
      Say("  ! missing file referenced by the scene: " + ref);
      return ref;
    }
    if (IsWithin(abs, assets_)) // copied with assets/ already
      return abs.lexically_relative(root_).generic_string();

    ++r_.relocatedReferences;
    if (IsWithin(abs, root_)) {
      // In the project but outside assets/: same relative place in the game.
      fs::path rel = abs.lexically_relative(root_);
      if (isModel && CanCopyWhole(abs.parent_path())) {
        if (copiedDirs_.insert({abs.parent_path(), rel.parent_path()}).second)
          CopyTree(abs.parent_path(), stage_ / rel.parent_path());
      } else {
        CopyFile(abs, stage_ / rel);
      }
      return rel.generic_string();
    }

    // Outside the project: gather under assets/external/.
    if (isModel) {
      fs::path srcDir = abs.parent_path();
      auto found = copiedDirs_.find(srcDir);
      fs::path dstDir;
      if (found != copiedDirs_.end()) {
        dstDir = found->second;
      } else {
        dstDir = fs::path("assets/external") /
                 UniqueExternalName(srcDir.filename().string());
        copiedDirs_[srcDir] = dstDir;
        if (CanCopyWhole(srcDir)) {
          Say("  + external model folder " + srcDir.string());
          CopyTree(srcDir, stage_ / dstDir);
        } else {
          Say("  ! " + srcDir.string() +
              " is too large to copy whole; copying only " +
              abs.filename().string() + " (its textures may be missing)");
        }
      }
      fs::path dst = dstDir / abs.filename();
      std::error_code ec2;
      if (!fs::exists(stage_ / dst, ec2))
        CopyFile(abs, stage_ / dst);
      return dst.generic_string();
    }

    auto found = copiedFiles_.find(abs);
    if (found != copiedFiles_.end())
      return found->second.generic_string();
    fs::path dst = fs::path("assets/external/textures") /
                   UniqueExternalName(abs.filename().string());
    Say("  + external texture " + abs.string());
    CopyFile(abs, stage_ / dst);
    copiedFiles_[abs] = dst;
    return dst.generic_string();
  }

  void CopyScenes() {
    fs::path src = root_ / "scenes";
    for (auto it = fs::recursive_directory_iterator(
             src, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
      if (!it->is_regular_file())
        continue;
      fs::path rel = it->path().lexically_relative(src);
      fs::path dst = stage_ / "scenes" / rel;
      IlmeeeScene scene;
      if (it->path().extension() != ".ilmeeescene" ||
          !LoadScene(it->path().string(), scene)) {
        if (it->path().extension() == ".ilmeeescene")
          Say("  ! could not read " + rel.string() + "; copied unchanged");
        CopyFile(it->path(), dst);
        continue;
      }
      for (SceneEntity &e : scene.entities) {
        if (e.kind == PrimitiveKind::ExternalObj ||
            e.kind == PrimitiveKind::ExternalPmx)
          e.externalPath = Relocate(e.externalPath, true);
        for (SurfaceTexture &t : e.surfaceTextures)
          t.texturePath = Relocate(t.texturePath, false);
      }
      fs::create_directories(dst.parent_path());
      if (!SaveScene(dst.string(), scene))
        throw std::runtime_error("cannot write " + dst.string());
      ++r_.filesCopied;
      Say("  scene " + rel.generic_string() + " (" +
          std::to_string(scene.entities.size()) + " objects)");
    }
  }

  bool Bundle() {
    std::string cmd = "bash " + ShellQuote(o_.bundleScript.string()) + " " +
                      ShellQuote(stage_.string()) + " 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      Say("  ! could not run " + o_.bundleScript.string());
      return false;
    }
    char line[1024];
    while (std::fgets(line, sizeof line, pipe)) {
      std::string s(line);
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
      Say("  " + s);
    }
    int status = pclose(pipe);
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  }

  void Run() {
    std::error_code ec;
    root_ = Canonical(o_.projectRoot);
    assets_ = root_ / "assets";
    const std::string name = o_.gameName.empty()
                                 ? root_.filename().string()
                                 : o_.gameName;
    const std::string safe = SafeGameName(name);
    fs::path out = o_.outputDir.empty()
                       ? root_ / "build" / "linux" / safe
                       : fs::absolute(o_.outputDir);
    out = Canonical(out);
    out_ = out;

    // ---- validate before touching anything ----------------------------
    fs::path mainScene = root_ / "scenes" / "main.ilmeeescene";
    IlmeeeScene probe;
    if (!LoadScene(mainScene.string(), probe))
      return Fail("cannot read " + mainScene.string() +
                  " — save the scene first");
    if (!fs::is_regular_file(o_.playerExecutable, ec))
      return Fail("player runtime not found at " +
                  o_.playerExecutable.string() + " (build IlmeeePlayer)");
    if (!fs::is_directory(o_.engineShaderDir, ec))
      return Fail("engine shaders not found at " + o_.engineShaderDir.string());
    if (out == root_ || IsWithin(root_, out))
      return Fail("the output folder " + out.string() +
                  " contains the project itself");
    if (IsWithin(out, assets_) || IsWithin(out, root_ / "scenes"))
      return Fail("the output folder cannot be inside assets/ or scenes/");
    if (fs::exists(out, ec)) {
      if (!fs::is_directory(out, ec))
        return Fail(out.string() + " exists and is not a folder");
      if (!fs::is_empty(out, ec) && !fs::exists(out / "game.json", ec))
        return Fail("refusing to replace " + out.string() +
                    ": it is not empty and not a previous build");
    }

    stage_ = out.parent_path() / (out.filename().string() + ".building");
    fs::remove_all(stage_);
    fs::create_directories(stage_);
    Say("Building '" + name + "' from " + root_.string());

    // ---- data ---------------------------------------------------------
    if (fs::is_directory(assets_, ec)) {
      Say("Copying assets/ ...");
      CopyTree(assets_, stage_ / "assets");
    }
    Say("Writing scenes/ ...");
    CopyScenes();

    fs::path shaderDst = stage_ / "assets" / "shaders" / "vulkan";
    if (fs::exists(shaderDst, ec))
      Say("  ! the project has its own assets/shaders/vulkan; the engine's "
          "shaders replace it in the build");
    Say("Copying engine shaders ...");
    CopyTree(o_.engineShaderDir, shaderDst);

    // ---- runtime ------------------------------------------------------
    fs::path bin = stage_ / "bin" / safe;
    CopyFile(o_.playerExecutable, bin);
    fs::permissions(bin,
                    fs::perms::owner_exec | fs::perms::group_exec |
                        fs::perms::others_exec,
                    fs::perm_options::add);
    fs::create_symlink(fs::path("bin") / safe, stage_ / safe);

    nlohmann::json manifest = {
        {"name", name},
        {"startScene", "scenes/main.ilmeeescene"},
        {"window",
         {{"width", o_.windowWidth},
          {"height", o_.windowHeight},
          {"fullscreen", o_.fullscreen}}}};
    std::ofstream(stage_ / "game.json") << manifest.dump(2) << "\n";

    if (!o_.bundleScript.empty()) {
      Say("Bundling libraries (portable build) ...");
      r_.librariesBundled = Bundle();
      if (!r_.librariesBundled)
        Say("  ! bundling failed; the game will only run on this machine");
    }

    // ---- swap in ------------------------------------------------------
    fs::remove_all(out);
    fs::rename(stage_, out);
    stage_.clear();

    r_.ok = true;
    r_.outputDir = out;
    r_.launcher = out / safe;
    Say("Done: " + std::to_string(r_.filesCopied) + " files, " +
        HumanBytes(r_.bytesCopied) + " -> " + out.string());
    if (r_.missingReferences)
      Say("Warning: " + std::to_string(r_.missingReferences) +
          " referenced file(s) were missing; those objects will not appear");
  }

  void Fail(const std::string &why) {
    r_.ok = false;
    r_.error = why;
    Say("Build failed: " + why);
  }

  // A failed build leaves no half-written folder behind.
  void DiscardStage() {
    if (stage_.empty())
      return;
    std::error_code ec;
    fs::remove_all(stage_, ec);
  }

private:
  const GameBuildOptions &o_;
  const BuildLog &log_;
  GameBuildResult &r_;
  fs::path root_, assets_, stage_, out_;
  std::map<fs::path, fs::path> copiedDirs_;
  std::map<fs::path, fs::path> copiedFiles_;
  std::set<std::string> usedExternalNames_;
};

} // namespace

std::string SafeGameName(const std::string &name) {
  std::string out;
  for (unsigned char c : name) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.')
      out += (char)c;
    else if (c == ' ' && !out.empty() && out.back() != '_')
      out += '_';
  }
  while (!out.empty() && (out.back() == '_' || out.back() == '.'))
    out.pop_back();
  while (!out.empty() && out.front() == '.')
    out.erase(out.begin());
  return out.empty() ? "Game" : out;
}

GameBuildResult BuildGame(const GameBuildOptions &options,
                          const BuildLog &log) {
  GameBuildResult result;
  Builder b(options, log, result);
  try {
    b.Run();
  } catch (const std::exception &e) {
    b.Fail(e.what());
  }
  if (!result.ok)
    b.DiscardStage();
  return result;
}

} // namespace ilmeee
