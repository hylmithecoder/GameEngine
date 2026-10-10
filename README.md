# Ilmeee Engine (In Development)

<div align="center">

<img src="https://count.getloli.com/@hylmithecoder?name=hylmithecoder&theme=gelbooru&padding=7&offset=0&align=top&scale=1&pixelated=1&darkmode=auto"><br><br>
</div>

A Vulkan-based C++ game engine in active development. Built with a modular 3D pipeline supporting modern mesh formats, real-time lighting, interactive asset workflows, and companion-process IPC. The editor (`GameEngineSDL`) ships alongside `IlmeeeHub` (project launcher) and `HandlerIlmeeeEngine` (companion thread-safe engine process).

## Building

The build is pinned to a Nix shell that brings in SDL2/3, GLFW, Vulkan, GTK3, FFmpeg, shaderc, etc.

```bash
# Enter the Nix development environment
nix-shell

# Run CMake and build within the environment
cmake -B build
make -C build -j$(nproc)
```

Shader compilation uses `glslc` which is available within the shell context:

```bash
glslc assets/shaders/vulkan/scene_mesh.vert -o assets/shaders/vulkan/scene_mesh.vert.spv
```

## Running the editor

Two normal entry points:

```bash
# Through the Hub (recommended — wires up the project handshake):
./build/IlmeeeHub

# Standalone with an explicit project:
./build/GameEngineSDL --project /path/to/MyProject
```

### Standalone debug fallback

When no `--project` is supplied, the editor boots a hardcoded debug scene so the renderer always has something to display:

```bash
./build/GameEngineSDL                # 3D: loads OBJ/PMX fallbacks
./build/GameEngineSDL --2d=true      # 2D: loads assets/testimage.png as a sprite
./build/GameEngineSDL --2d           # same as --2d=true
```

The 2D fallback drops a single sprite at world origin (300x300 px) and shows a dynamic editor grid + colored XY axes overlay that follow pan/zoom.

## In-game UI

Open a project, right-click empty space or the Scene root in **Hierarchy**, and
choose **Create → Canvas**. Right-click Canvas to add Text, Image, or Button
children. Select Canvas or one of its children to edit the UI directly over
the player camera in the **Scene** tab: click an element to select it and drag
to reposition it. Drag an image from **Explorer** onto an Image element to
replace its texture, or onto empty Canvas space to create a new Image at the
drop position. You can also drop onto the Image node in Hierarchy or its
**Image path** field in Inspector. The **Inspector** edits its exact properties, anchor and
reference-pixel offsets (default 1280×720). The **Game** tab shows the same
UI without editor outlines. Save with **Ctrl+S**: Canvas and its children are stored in
`scenes/main.ilmeeescene`. Image paths are relative to the project folder,
for example `assets/logo.png`. Older `assets/ui/main.json` files are imported
when the scene does not contain a Canvas yet.

An explicitly created Canvas starts empty. `TogglePause` is the built-in
button action; other action names currently appear in the Console log but
need gameplay code to perform an effect. The top Play button opens the **Game**
tab and runs the camera view and UI there; the **Scene** tab stays editable
after Stop. Pause and Stop events appear in
**Console → Output**. For a separate game window, choose **Build → Build and
Run** (or Ctrl+Shift+B); the running player's stdout/stderr is streamed into
**Console → Build** and **Console → Output**.

## Movement scripts

Select a mesh game object in Hierarchy, enable **Movement Script → Movable**
in Inspector, choose `WASD XZ`, set its speed, and save with **Ctrl+S**. Play
opens Game; click inside it and use WASD. Movement uses fixed 60 Hz steps and
Stop restores editor transforms. The same component runs in a built game.

To add a native movement function, edit
`src/cpp/core_engine/UserMovementScripts.cpp`: implement a function taking
`ilmeee::MovementContext&`, then call `RegisterMovementScript("MyMove", MyMove)`
inside `RegisterUserMovementScripts()`. The context exposes parent-local
position, rotation, scale, speed, delta seconds and input. Rebuild both
`GameEngineSDL` and `IlmeeePlayer`, then pick the registered name in Inspector.
This is a compiled C++ API; project `.cpp` assets are not hot-loaded yet.

**Camera & UI input.** Each movable object can reference a camera and pick a
**POV Style** — Third Person, First Person, Side Scroller (with the
`Platformer` script) or Top Down — plus distance/height. Add on-screen sticks
with Canvas → Create Child → **Joystick** and link them as *Move Joystick* /
*Look Joystick*. Keyboard (WASD, Space, arrows, right-drag look) and the sticks
stay in sync; on a phone both sticks work at once and swiping the open screen
turns the camera.

## Android

1. Once per engine checkout (needs the Android SDK + NDK, default `~/Android/Sdk`):
   ```sh
   nix-shell shell.nix --run scripts/android/build-player.sh
   ```
   This builds IlmeeePlayer as `build-android/jniLibs/arm64-v8a/libmain.so`
   (plus SDL3, same version as the desktop one).
2. In the editor: **Build → Target: Android (APK)**, then **Build Game**.
   The APK lands in `<project>/build/android/<Game>/<Game>.apk`
   (the first build downloads the Android Gradle plugin).
3. Plug in a phone with USB debugging on and press **Run**: the editor
   installs the APK with `adb`, starts it and shows its log in Console → Build.

Everything in `assets/` goes into the APK; on first start the game is
extracted to the app's internal storage. Keep mobile projects' assets small.

## Scene & Viewport Controls

- **3D Viewport Navigation**:
  - `RMB drag` — mouselook (FPS fly-camera)
  - `WASD` — fly camera forward, left, backward, right
  - `Q` / `E` — fly downward / upward
  - `Shift` — fast movement multiplier
- **Object Interaction & Layout**:
  - `LMB click` — **Ray-Triangle Picking**: Casts a ray through the viewport to select the specific 3D model and individual mesh surface under the cursor.
  - `LMB drag` — Move the currently selected mesh in the screen plane.
  - **Drag-and-Drop Spawning**: Drag `.obj`, `.pmx`, or `.fbx` models from the File Explorer and drop them directly onto the viewport to instantiate them at that spatial point.
  - **Drag-and-Drop Texturing**: Drag any texture image (`.png`, `.jpg`, etc.) onto a specific 3D mesh surface to bind it dynamically.
- **Inspector Panel (Surfaces)**:
  - Exposes all auto-detected submesh surfaces and material slots.
  - Allows selecting individual surfaces to inspect properties or **Bind/Clear Custom Textures** on the fly.
- **2D Mode Navigation** (no 3D mesh loaded):
  - `RMB drag` — pan camera
  - `Wheel` — zoom (0.1×–5×)
  - Toolbar `Reset View` resets camera

## Project Structure

```
src/cpp/
  main.cpp                       # editor entry, CLI parsing
  ApplicationManager.cpp         # lifecycle, thread-safe network synchronization, companion startup
  Hub.cpp                        # IlmeeeHub launcher
  core_engine/
    SceneRenderer.cpp            # Vulkan mesh/viewport renderer, OBJ/PMX/FBX loaders, ray picking
    TextureManager.cpp           # Vulkan dynamic texture & descriptor pool manager
  ui/
    MainWindow.cpp               # main shell, docking, and theme controller
    HandlerChildWindow.cpp       # scene viewport, hierarchy, and surface inspector panels
    HandlerProject.cpp           # project file explorer & drag-drop asset import
assets/
  3dmodels/                      # fallback test meshes
  shaders/vulkan/                # vertex, fragment, and grid shader sources
```

## Project File Format

Scenes are serialized to a custom high-performance binary `.ilmeeescene` format. 
* **v1.0**: Stores entity counts, primitive/mesh kinds, transform fields, and path nodes.
* **v1.1**: Appends per-entity light properties (range, spot angle, intensity, type, color, gamma).
* **v1.2 (Current)**: Appends per-entity camera properties (near/far plane, projection, FOV) and **per-surface texture bindings** (resolves relative to project root or loads absolute paths).

User preferences and recents live in the cross-platform `~/.ilmeeeengine/` location via `ilmeee::UserDataRoot()`.

## Thread-Safe Companion Architecture

The editor (`GameEngineSDL`) communicates with the engine runner (`HandlerIlmeeeEngine`) via a localized TCP loopback protocol over ports `27015` and `27016`. 
* All blocking modal GUI actions (such as `Load Scene` native file dialogs) are safely offloaded to the main thread's main loop context using GLib's **`g_idle_add`** dispatching.
* This ensures complete thread-safety across background network readers and the main rendering thread.

## Status

- Vulkan renderer with offscreen target + ImGui presentation: ✅
- 2D sprite pipeline + dynamic grid overlay: ✅
- 3D Primitives (Cube, Sphere, Plane) & Camera entities: ✅
- 3D OBJ, PMX, and FBX loaders (normals, UVs, FNV-1a vertex dedup): ✅
- Surfaces & Per-surface Texture Overrides (v1.2 Scene Graph): ✅
- Ray-Triangle Picking & Viewport Drag-and-Drop: ✅
- Audio engine integration: ⏳ planned
- Android target: ✅ experimental (arm64 APK, Vulkan phones)
