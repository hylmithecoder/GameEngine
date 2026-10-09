// BuildGame: layout of a built game, relocation of scene references, and the
// safety rules around the output folder. Pure filesystem, no GPU.
//
//   cmake --build build --target TestGameBuild && ./build/bin/TestGameBuild

#include "../include/core_engine/GameBuilder.hpp"
#include "../include/core_engine/IlmeeeScene.hpp"
#include "../vendor/nlohmann/json.hpp"
#include <cstdio>
#include <fstream>

using namespace ilmeee;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                          \
  } while (0)

static void Write(const fs::path &p, const std::string &content) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << content;
}

static SceneEntity Model(const std::string &name, const std::string &path) {
  SceneEntity e;
  e.name = name;
  e.kind = PrimitiveKind::ExternalObj;
  e.externalPath = path;
  return e;
}

int main() {
  const fs::path tmp = fs::temp_directory_path() / "ilmeee-gamebuild-test";
  fs::remove_all(tmp);
  const fs::path project = tmp / "My First Project";
  const fs::path outside = tmp / "elsewhere";

  // ---- a project touching every kind of reference -----------------------
  Write(project / "assets/readme.txt", "hello");
  Write(project / "assets/ui/main.json",
        "{\"version\":1,\"elements\":[{\"id\":\"hp\",\"type\":\"text\","
        "\"text\":\"HP: 100\"}]}");
  Write(project / "assets/models/inside.obj", "o inside");
  Write(project / "assets/logo.png", "png");
  Write(project / "extra/thing.obj", "o thing");
  Write(project / "extra/thing.mtl", "newmtl m");
  Write(outside / "hero/hero.obj", "o hero");
  Write(outside / "hero/hero_tex.png", "png");
  Write(outside / "skin.png", "png");
  Write(project / "model_at_root.obj", "o root");

  IlmeeeScene scene;
  scene.backgroundColor = {0.1f, 0.2f, 0.3f, 1.0f};
  SceneEntity cube;
  cube.name = "Cube";
  scene.entities.push_back(cube);
  scene.entities.push_back(Model("Inside", "assets/models/inside.obj"));
  scene.entities.push_back(Model("Hero", (outside / "hero/hero.obj").string()));
  scene.entities.back().surfaceTextures.push_back(
      {0, (outside / "skin.png").string()});
  scene.entities.push_back(Model("Thing", "extra/thing.obj"));
  scene.entities.push_back(Model("Gone", "assets/models/missing.obj"));
  scene.entities.push_back(Model("AtRoot", "model_at_root.obj"));
  scene.entities.back().parent = 0;
  SceneEntity canvas;
  canvas.name = "Canvas";
  canvas.kind = PrimitiveKind::Canvas;
  scene.entities.push_back(canvas);
  SceneEntity uiImage;
  uiImage.name = "Logo";
  uiImage.kind = PrimitiveKind::UiImage;
  uiImage.parent = 6;
  uiImage.uiImagePath = "assets/logo.png";
  scene.entities.push_back(uiImage);
  fs::create_directories(project / "scenes");
  CHECK(SaveScene((project / "scenes/main.ilmeeescene").string(), scene));

  const fs::path player = tmp / "IlmeeePlayer";
  Write(player, "#!/bin/sh\necho player\n");
  const fs::path shaders = tmp / "engine/assets/shaders/vulkan";
  Write(shaders / "scene_mesh.vert.spv", "spv");

  GameBuildOptions o;
  o.projectRoot = project;
  o.playerExecutable = player;
  o.engineShaderDir = shaders;

  std::vector<std::string> log;
  auto logger = [&](const std::string &s) { log.push_back(s); };

  // ---- 1. a full build ---------------------------------------------------
  GameBuildResult r = BuildGame(o, logger);
  CHECK(r.ok);
  const fs::path out = project / "build/linux/My_First_Project";
  CHECK(r.outputDir == fs::weakly_canonical(out));
  CHECK(r.launcher == r.outputDir / "My_First_Project");
  CHECK(fs::is_symlink(out / "My_First_Project"));
  CHECK(fs::read_symlink(out / "My_First_Project") == "bin/My_First_Project");
  CHECK((fs::status(out / "bin/My_First_Project").permissions() &
         fs::perms::owner_exec) != fs::perms::none);
  CHECK(fs::exists(out / "assets/readme.txt"));
  {
    std::ifstream in(out / "assets/ui/main.json");
    CHECK(in.good());
    if (in) {
      const auto ui = nlohmann::json::parse(in);
      CHECK(ui["elements"][0]["text"] == "HP: 100");
    }
  }
  CHECK(fs::exists(out / "assets/shaders/vulkan/scene_mesh.vert.spv"));
  CHECK(!fs::exists(out / "build")); // the build is not copied into itself
  CHECK(!fs::exists(out.string() + ".building"));

  {
    std::ifstream in(out / "game.json");
    auto j = nlohmann::json::parse(in);
    CHECK(j["name"] == "My First Project");
    CHECK(j["startScene"] == "scenes/main.ilmeeescene");
    CHECK(j["window"]["width"] == 1280);
  }

  IlmeeeScene built;
  CHECK(LoadScene((out / "scenes/main.ilmeeescene").string(), built));
  CHECK(built.entities.size() == scene.entities.size());
  CHECK(built.backgroundColor == scene.backgroundColor);
  CHECK(built.entities[5].parent == 0);
  CHECK(built.entities[6].kind == PrimitiveKind::Canvas);
  CHECK(built.entities[7].kind == PrimitiveKind::UiImage);
  CHECK(built.entities[7].parent == 6);
  CHECK(built.entities[7].uiImagePath == "assets/logo.png");
  // Every model/texture path is now relative and exists inside the game.
  auto present = [&](const std::string &p) {
    return fs::path(p).is_relative() && fs::exists(out / p);
  };
  CHECK(built.entities[1].externalPath == "assets/models/inside.obj");
  CHECK(present(built.entities[1].externalPath));
  CHECK(present(built.entities[2].externalPath));
  CHECK(built.entities[2].externalPath == "assets/external/hero/hero.obj");
  CHECK(fs::exists(out / "assets/external/hero/hero_tex.png")); // its folder
  CHECK(present(built.entities[2].surfaceTextures[0].texturePath));
  CHECK(built.entities[3].externalPath == "extra/thing.obj");
  CHECK(fs::exists(out / "extra/thing.mtl"));
  CHECK(present(built.entities[5].externalPath));
  CHECK(built.entities[4].externalPath == "assets/models/missing.obj");
  CHECK(r.missingReferences == 1);
  CHECK(r.relocatedReferences == 4); // hero, skin, thing, model_at_root

  // ---- 2. rebuilding replaces the previous build -------------------------
  Write(out / "stale.txt", "old");
  r = BuildGame(o, logger);
  CHECK(r.ok);
  CHECK(!fs::exists(out / "stale.txt"));

  // ---- 3. safety: never wipe something that is not a build ---------------
  const fs::path precious = tmp / "precious";
  Write(precious / "thesis.txt", "do not delete");
  GameBuildOptions bad = o;
  bad.outputDir = precious;
  r = BuildGame(bad, logger);
  CHECK(!r.ok);
  CHECK(fs::exists(precious / "thesis.txt"));
  CHECK(!fs::exists(tmp / "precious.building"));

  bad.outputDir = project; // the project itself
  CHECK(!BuildGame(bad, logger).ok);
  bad.outputDir = tmp; // a folder containing the project
  CHECK(!BuildGame(bad, logger).ok);
  bad.outputDir = project / "assets/out";
  CHECK(!BuildGame(bad, logger).ok);
  CHECK(fs::exists(project / "scenes/main.ilmeeescene"));

  // ---- 4. missing inputs fail early, leaving nothing behind --------------
  GameBuildOptions noPlayer = o;
  noPlayer.playerExecutable = tmp / "nope";
  noPlayer.outputDir = tmp / "out-noplayer";
  r = BuildGame(noPlayer, logger);
  CHECK(!r.ok && r.error.find("player") != std::string::npos);
  CHECK(!fs::exists(tmp / "out-noplayer.building"));

  // ---- 5. names ----------------------------------------------------------
  CHECK(SafeGameName("My First Project") == "My_First_Project");
  CHECK(SafeGameName("  ../../etc/passwd") == "etcpasswd");
  CHECK(SafeGameName("Ilmeee: VN #1!") == "Ilmeee_VN_1");
  CHECK(SafeGameName("ミク") == "Game");

  fs::remove_all(tmp);
  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
