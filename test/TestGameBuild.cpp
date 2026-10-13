// BuildGame: layout of a built game, relocation of scene references, and the
// safety rules around the output folder. Pure filesystem, no GPU.
//
//   cmake --build build --target TestGameBuild && ./build/bin/TestGameBuild

#include "../include/core_engine/GameBuilder.hpp"
#include "../include/core_engine/IlmeeeScene.hpp"
#include "../include/core_engine/NativeScripts.hpp"
#include "../include/core_engine/ScriptWorkspace.hpp"
#include "../vendor/nlohmann/json.hpp"
#include <cstdio>
#include <cstdlib>
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
  Write(project / "extra/thing.obj", "mtllib thing.mtl\no thing");
  Write(project / "extra/thing.mtl", "newmtl m");
  Write(project / "extra/unused.obj", "o unused");
  Write(outside / "hero/hero.obj", "mtllib hero.mtl\no hero");
  Write(outside / "hero/hero.mtl", "newmtl m\nmap_Kd -s 1 1 1 hero_tex.png");
  Write(outside / "hero/hero_tex.png", "png");
  Write(outside / "hero/not_used.png", "png");
  // Media and a model folder with textures it does not use.
  Write(project / "assets/audio/rain.ogg", "ogg");
  Write(project / "assets/audio/unused.wav", "wav");
  Write(project / "assets/videos/intro.mp4", "mp4");
  Write(project / "assets/videos/huge_unused.mp4", std::string(4096, 'x'));
  Write(project / "assets/models/inside.mtl", "map_Kd tex/inside.png");
  Write(project / "assets/models/tex/inside.png", "png");
  Write(project / "assets/models/tex/other.png", "png");
  Write(project / "assets/models/inside.obj", "mtllib inside.mtl\no inside");
  Write(outside / "skin.png", "png");
  Write(project / "model_at_root.obj", "o root");

  IlmeeeScene scene;
  scene.backgroundColor = {0.1f, 0.2f, 0.3f, 1.0f};
  SceneEntity cube;
  cube.name = "Cube";
  cube.movable = true;
  cube.movementScript = "WASD XZ";
  cube.movementSpeed = 4.5f;
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
  SceneEntity speaker;
  speaker.name = "Speaker";
  speaker.kind = PrimitiveKind::AudioSource;
  speaker.audio.enabled = true;
  speaker.audio.clip = "assets/audio/rain.ogg";
  scene.entities.push_back(speaker);
  SceneEntity tv;
  tv.name = "TV";
  tv.kind = PrimitiveKind::Screen;
  tv.video.enabled = true;
  tv.video.clip = "assets/videos/intro.mp4";
  scene.entities.push_back(tv);
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
  // Only what the scenes use: no readme, no unused clips or textures.
  CHECK(!fs::exists(out / "assets/readme.txt"));
  CHECK(fs::exists(out / "assets/audio/rain.ogg"));
  CHECK(!fs::exists(out / "assets/audio/unused.wav"));
  CHECK(fs::exists(out / "assets/videos/intro.mp4"));
  CHECK(!fs::exists(out / "assets/videos/huge_unused.mp4"));
  CHECK(fs::exists(out / "assets/models/inside.mtl"));
  CHECK(fs::exists(out / "assets/models/tex/inside.png"));
  CHECK(!fs::exists(out / "assets/models/tex/other.png"));
  // readme.txt, unused.wav, huge_unused.mp4, tex/other.png
  CHECK(r.unusedFilesSkipped == 4);
  CHECK(r.unusedBytesSkipped > 4096);
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
  CHECK(built.entities[0].movable);
  CHECK(built.entities[0].movementScript == "WASD XZ");
  CHECK(built.entities[0].movementSpeed == 4.5f);
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
  CHECK(fs::exists(out / "assets/external/hero/hero_tex.png")); // via .mtl
  CHECK(fs::exists(out / "assets/external/hero/hero.mtl"));
  CHECK(!fs::exists(out / "assets/external/hero/not_used.png"));
  CHECK(present(built.entities[2].surfaceTextures[0].texturePath));
  CHECK(built.entities[3].externalPath == "extra/thing.obj");
  CHECK(fs::exists(out / "extra/thing.mtl"));
  CHECK(!fs::exists(out / "extra/unused.obj"));
  CHECK(present(built.entities[5].externalPath));
  CHECK(built.entities[4].externalPath == "assets/models/missing.obj");
  CHECK(r.missingReferences == 1);
  CHECK(r.relocatedReferences == 4); // hero, skin, thing, model_at_root

  // ---- 2. rebuilding replaces the previous build -------------------------
  Write(out / "stale.txt", "old");
  r = BuildGame(o, logger);
  CHECK(r.ok);
  CHECK(!fs::exists(out / "stale.txt"));

  // ---- 2b. "Include unused assets" ships all of assets/ ------------------
  {
    GameBuildOptions all = o;
    all.includeUnusedAssets = true;
    r = BuildGame(all, logger);
    CHECK(r.ok);
    CHECK(fs::exists(out / "assets/readme.txt"));
    CHECK(fs::exists(out / "assets/videos/huge_unused.mp4"));
    CHECK(r.unusedFilesSkipped == 0);
    r = BuildGame(o, logger); // back to the lean build for what follows
    CHECK(r.ok);
  }

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

  // ---- 4b. Android: project prepared, game packed under APK assets -------
  {
    const fs::path player = tmp / "android-player";
    Write(player / "jniLibs/arm64-v8a/libmain.so", "elf");
    Write(player / "jniLibs/arm64-v8a/libSDL3.so", "elf");
    Write(player / "sdl-java/org/libsdl/app/SDLActivity.java", "class");
    const fs::path tmpl = tmp / "android-template";
    Write(tmpl / "app/build.gradle",
          "id=@APPLICATION_ID@ v=@VERSION_NAME@ c=@VERSION_CODE@ a='@ABIS@'");
    Write(tmpl / "app/src/main/res/values/strings.xml", "<s>@APP_NAME@</s>");
    Write(tmpl / "settings.gradle", "include ':app'");

    GameBuildOptions a = o;
    a.target = BuildTarget::Android;
    a.outputDir.clear();
    a.playerExecutable.clear();
    a.androidPlayerDir = player;
    a.androidTemplateDir = tmpl;
    a.androidSdkDir = tmp / "sdk";
    a.gradleCommand.clear(); // prepare only
    r = BuildGame(a, logger);
    CHECK(r.ok);
    const fs::path aout = project / "build/android/My_First_Project";
    CHECK(r.outputDir == fs::weakly_canonical(aout));
    CHECK(r.applicationId == "com.ilmeee.my_first_project");
    const fs::path game = aout / "app/src/main/assets/game";
    CHECK(fs::exists(game / "scenes/main.ilmeeescene"));
    CHECK(fs::exists(game / "assets/shaders/vulkan/scene_mesh.vert.spv"));
    CHECK(fs::exists(aout / "app/src/main/jniLibs/arm64-v8a/libmain.so"));
    CHECK(fs::exists(aout / "app/src/main/java/org/libsdl/app/SDLActivity.java"));
    CHECK(!fs::exists(game / "bin"));
    auto readAll = [](const fs::path &p) {
      std::ifstream in(p);
      return std::string(std::istreambuf_iterator<char>(in), {});
    };
    CHECK(readAll(aout / "app/build.gradle") ==
          "id=com.ilmeee.my_first_project v=1.0 c=1 a='arm64-v8a'");
    CHECK(readAll(aout / "app/src/main/res/values/strings.xml") ==
          "<s>My First Project</s>");
    CHECK(readAll(aout / "local.properties").find("sdk.dir=") == 0);
    const std::string index = readAll(game / "ilmeee_files.txt");
    CHECK(index.rfind("ilmeee-files 1 ", 0) == 0);
    CHECK(index.find("\nscenes/main.ilmeeescene\n") != std::string::npos);
    CHECK(index.find("\ngame.json\n") != std::string::npos);
    CHECK(index.find("ilmeee_files.txt") == std::string::npos);
    nlohmann::json gm = nlohmann::json::parse(readAll(game / "game.json"));
    CHECK(gm["window"]["fullscreen"] == true);

    // Rebuilding replaces the previous Android build.
    CHECK(BuildGame(a, logger).ok);

    // Phone video: an unreadable clip ships unchanged...
    setenv("HOME", (tmp / "home").c_str(), 1); // isolate ~/.ilmeeeengine
    r = BuildGame(a, logger);
    CHECK(r.ok && r.videosTranscoded == 0);
    CHECK(readAll(game / "assets/videos/intro.mp4") == "mp4");
    // ...a real 1080p clip becomes 720p H.264, cached for the next build.
    if (std::system("ffmpeg -version >/dev/null 2>&1") == 0) {
      const fs::path clip = project / "assets/videos/intro.mp4";
      const std::string gen =
          "ffmpeg -nostdin -y -v error -f lavfi -i testsrc=size=1920x1080:"
          "rate=24 -f lavfi -i sine -t 1 -c:v libx264 -pix_fmt yuv420p "
          "-c:a aac '" + clip.string() + "'";
      CHECK(std::system(gen.c_str()) == 0);
      r = BuildGame(a, logger);
      CHECK(r.ok && r.videosTranscoded == 1);
      auto probe = [](const fs::path &f) {
        std::string cmd = "ffprobe -v error -select_streams v:0 -show_entries "
                          "stream=codec_name,height -of csv=p=0 '" +
                          f.string() + "'";
        std::string out;
        if (FILE *pp = popen(cmd.c_str(), "r")) {
          char buf[128];
          while (std::fgets(buf, sizeof buf, pp)) out += buf;
          pclose(pp);
        }
        return out;
      };
      CHECK(probe(game / "assets/videos/intro.mp4") == "h264,720\n");
      CHECK(probe(clip) == "h264,1080\n"); // the project keeps its file
      log.clear();
      r = BuildGame(a, logger);
      CHECK(r.ok && r.videosTranscoded == 1);
      bool fromCache = false;
      for (const auto &l : log)
        fromCache |= l.find("from cache") != std::string::npos;
      CHECK(fromCache);
      // A clip that already suits phones is shipped as-is.
      const std::string gen720 =
          "ffmpeg -nostdin -y -v error -f lavfi -i testsrc=size=1280x720:"
          "rate=24 -t 1 -c:v libx264 -pix_fmt yuv420p '" + clip.string() + "'";
      CHECK(std::system(gen720.c_str()) == 0);
      r = BuildGame(a, logger);
      CHECK(r.ok && r.videosTranscoded == 0);
      CHECK(fs::file_size(game / "assets/videos/intro.mp4") ==
            fs::file_size(clip));
      CHECK(std::system(gen.c_str()) == 0); // back to the 1080p clip
      // "Video original" ships it untouched.
      GameBuildOptions orig = a;
      orig.androidMaxVideoHeight = 0;
      r = BuildGame(orig, logger);
      CHECK(r.ok && r.videosTranscoded == 0);
      CHECK(probe(game / "assets/videos/intro.mp4") == "h264,1080\n");
    } else {
      std::printf("  (ffmpeg not found: skipping the video re-encode check)\n");
    }

    GameBuildOptions badId = a;
    badId.applicationId = "NoDots";
    CHECK(!BuildGame(badId, logger).ok);
    GameBuildOptions noLibs = a;
    noLibs.androidPlayerDir = tmp / "nothing";
    r = BuildGame(noLibs, logger);
    CHECK(!r.ok && r.error.find("build-player.sh") != std::string::npos);
  }
  CHECK(DefaultApplicationId("My First Project") == "com.ilmeee.my_first_project");
  CHECK(DefaultApplicationId("3D Racer!") == "com.ilmeee.g3d_racer");
  CHECK(DefaultApplicationId("ミク") == "com.ilmeee.game");

  // ---- 5. names ----------------------------------------------------------
  CHECK(SafeGameName("My First Project") == "My_First_Project");
  CHECK(SafeGameName("  ../../etc/passwd") == "etcpasswd");
  CHECK(SafeGameName("Ilmeee: VN #1!") == "Ilmeee_VN_1");
  CHECK(SafeGameName("ミク") == "Game");

  // Native bindings ship as modules; the player needs no project compiler.
  {
    const auto scripted = tmp / "Script Project";
    fs::create_directories(scripted / "scenes");
    std::string error;
    CHECK(CreateScriptFiles(scripted / "assets/scripts", "ShipComponent", error));
    IlmeeeScene scene;
    scene.entities.push_back(SceneEntity{});
    scene.entities[0].objectId = 42;
    scene.entities[0].scripts.push_back({"assets/scripts/ShipComponent.cpp", "ShipComponent", true, {}});
    CHECK(SaveScene((scripted / "scenes/main.ilmeeescene").string(), scene));
    auto scriptOptions = o;
    scriptOptions.projectRoot = scripted;
    scriptOptions.outputDir = tmp / "script-game";
    scriptOptions.includeUnusedAssets = false;
    const auto built = BuildGame(scriptOptions, logger);
    if (!built.ok) std::fprintf(stderr, "%s\n", built.error.c_str());
    CHECK(built.ok);
    CHECK(fs::exists(scriptOptions.outputDir / "native-scripts.json"));
    std::ifstream manifestFile(scriptOptions.outputDir / "native-scripts.json");
    const auto modules = nlohmann::json::parse(manifestFile);
    CHECK(modules.size() == 1);
    const auto library = scriptOptions.outputDir / modules[0]["library"].get<std::string>();
    CHECK(fs::exists(library));
    NativeScripts runtime;
    CHECK(runtime.Load({"assets/scripts/ShipComponent.cpp", "ShipComponent", "", library, true}, error));
    CHECK(runtime.Ready("assets/scripts/ShipComponent.cpp"));
    IlmeeeScene reloaded;
    CHECK(LoadScene((scriptOptions.outputDir / "scenes/main.ilmeeescene").string(), reloaded));
    CHECK(reloaded.entities[0].objectId == 42 && reloaded.entities[0].scripts.size() == 1);
    // Android exports cannot pretend an old player supports project modules.
    const auto androidPlayer = tmp / "android-script-player";
    Write(androidPlayer / "jniLibs/arm64-v8a/libmain.so", "elf");
    Write(androidPlayer / "jniLibs/arm64-v8a/libSDL3.so", "elf");
    Write(androidPlayer / "sdl-java/org/libsdl/app/SDLActivity.java", "class");
    const auto androidTemplate = tmp / "android-script-template";
    Write(androidTemplate / "app/build.gradle", "@ABIS@");
    Write(androidTemplate / "app/src/main/res/values/strings.xml", "@APP_NAME@");
    auto androidOptions = scriptOptions;
    androidOptions.target = BuildTarget::Android;
    androidOptions.outputDir = tmp / "android-script-game";
    androidOptions.androidPlayerDir = androidPlayer;
    androidOptions.androidTemplateDir = androidTemplate;
    androidOptions.gradleCommand.clear();
    auto androidBuilt = BuildGame(androidOptions, logger);
    CHECK(!androidBuilt.ok && androidBuilt.error.find("build-player.sh") != std::string::npos);
    CHECK(!fs::exists(tmp / "android-script-game.building"));
    if (const char *player = std::getenv("ILMEEE_TEST_ANDROID_PLAYER")) {
      androidOptions.androidPlayerDir = player;
      androidBuilt = BuildGame(androidOptions, logger);
      if (!androidBuilt.ok) std::fprintf(stderr, "%s\n", androidBuilt.error.c_str());
      CHECK(androidBuilt.ok);
      const auto game = androidOptions.outputDir / "app/src/main/assets/game";
      std::ifstream androidManifest(game / "native-scripts.json");
      const auto androidModules = nlohmann::json::parse(androidManifest);
      CHECK(androidModules.size() == 1);
      CHECK(androidModules[0].at("androidNativeLibrary").get<bool>());
      const auto name = androidModules[0].at("library").get<std::string>();
      CHECK(fs::path(name).filename() == name && name.starts_with("libilmeee_script_"));
      CHECK(!fs::exists(game / name));
      const auto lib = androidOptions.outputDir / "app/src/main/jniLibs/arm64-v8a" / name;
      CHECK(fs::exists(lib));
      unsigned char elf[20]{};
      std::ifstream(lib, std::ios::binary).read(reinterpret_cast<char *>(elf), sizeof elf);
      CHECK(elf[4] == 2 && elf[18] == 183 && elf[19] == 0);
      CHECK(fs::exists(androidOptions.outputDir / "app/src/main/jniLibs/arm64-v8a/libc++_shared.so"));
      std::ifstream index(game / "ilmeee_files.txt");
      const std::string indexText{std::istreambuf_iterator<char>(index), {}};
      CHECK(indexText.find("native-scripts.json") != std::string::npos);
      CHECK(indexText.find(name) == std::string::npos);
      IlmeeeScene androidScene;
      CHECK(LoadScene((game / "scenes/main.ilmeeescene").string(), androidScene));
      CHECK(androidScene.entities[0].scripts[0].source == "assets/scripts/ShipComponent.cpp");
      // A stale SDK is rejected before compilation and cannot replace the APK.
      fs::copy_file(fs::path(player) / "script-toolchain-arm64-v8a.json",
                    androidPlayer / "script-toolchain-arm64-v8a.json");
      fs::create_directories(androidPlayer / "script-sdk");
      fs::copy(fs::path(player) / "script-sdk/arm64-v8a",
               androidPlayer / "script-sdk/arm64-v8a", fs::copy_options::recursive);
      Write(androidPlayer / "script-sdk/arm64-v8a/core_engine/core/Component.hpp",
            "outdated player SDK");
      auto stalePlayer = androidOptions;
      stalePlayer.androidPlayerDir = androidPlayer;
      const auto stale = BuildGame(stalePlayer, logger);
      CHECK(!stale.ok && stale.error.find("outdated") != std::string::npos);
      CHECK(fs::exists(lib));
      Write(scripted / "assets/scripts/ShipComponent.cpp", "this is invalid C++");
      CHECK(!BuildGame(androidOptions, logger).ok);
      CHECK(fs::exists(lib)); // a failed NDK compile preserves the previous build
    }
    // Failed recompiles preserve the previous runnable output.
    Write(scripted / "assets/scripts/ShipComponent.cpp", "this is invalid C++");
    CHECK(!BuildGame(scriptOptions, logger).ok);
    CHECK(fs::exists(library));
  }
  fs::remove_all(tmp);
  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
