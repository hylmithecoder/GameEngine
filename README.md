# Ilmeee Engine (In Development)

<div align="center">

<img src="https://count.getloli.com/@hylmithecoder?name=hylmithecoder&theme=gelbooru&padding=7&offset=0&align=top&scale=1&pixelated=1&darkmode=auto"><br><br>
</div>

A Vulkan-based C++ game engine in active development. Targets 2D first
(visual-novel style scenes) with a 3D OBJ pipeline already wired in
for prototyping. The editor (`GameEngineSDL`) ships alongside
`IlmeeeHub` (project launcher) and `HandlerIlmeeeEngine` (companion
process used while the editor is running).

## Building

The build is pinned to a Nix shell that brings in SDL2/3, GLFW,
Vulkan, GTK3, FFmpeg, shaderc, etc.

```bash
mkdir -p build && cd build
nix-shell ../shell.nix --run 'cmake ..'
nix-shell ../shell.nix --run 'make -j$(nproc) GameEngineSDL'
```

Shader compilation uses `glslc` which is not in `shell.nix` directly:

```bash
nix-shell -p shaderc --run 'glslc input.vert -o output.vert.spv'
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

When no `--project` is supplied, the editor boots a hardcoded debug
scene so the renderer always has something to display:

```bash
./build/GameEngineSDL                # 3D: loads assets/3dmodels/yixuan.obj
./build/GameEngineSDL --2d=true      # 2D: loads assets/testimage.png as a sprite
./build/GameEngineSDL --2d           # same as --2d=true
```

The 2D fallback drops a single sprite at world origin (300x300 px) and
shows a dynamic editor grid + colored XY axes overlay that follow
pan/zoom.

## Scene controls

- **3D mode** (mesh loaded):
  - `RMB drag` — mouselook
  - `WASD` — fly
  - `Shift` — fast modifier
  - `LMB drag` — move selected mesh in screen plane
- **2D mode** (no mesh):
  - `RMB drag` — pan camera
  - `Wheel` — zoom (0.1×–5×)
  - Toolbar `Reset View` resets camera

## Project structure

```
src/cpp/
  main.cpp                       # editor entry, CLI parsing
  ApplicationManager.cpp         # lifecycle, network, propagates CLI to MainWindow
  Hub.cpp                        # IlmeeeHub launcher
  core_engine/
    SceneRenderer2D.cpp          # Vulkan renderer (2D sprites + 3D mesh pipeline)
    TextureManager.cpp           # sprite texture loads
    IlmeeeScene.cpp              # binary .ilmeeescene serializer
  ui/
    MainWindow.cpp               # ImGui shell
    HandlerChildWindow.cpp       # scene/hierarchy/inspector panels, debug bootstrap
    HandlerProject.cpp           # project IO
assets/
  3dmodels/                      # OBJ test models
  testimage.png                  # 2D debug-fallback sprite
  shaders/vulkan/*.spv           # compiled shaders
```

## Project file format

Projects use a binary `.ilmeeescene` v1 layout. User data (recents,
preferences) lives at a cross-platform `~/.ilmeeeengine/` location via
`ilmeee::UserDataRoot()`.

## Status

- Vulkan renderer with offscreen target + ImGui presentation: ✅
- 2D sprite pipeline + dynamic grid overlay: ✅
- 3D OBJ loader (positions + normals, flat lighting): ✅
- 3D primitives (cube/sphere/plane): ✅
- OBJ material / texture support: ⏳ planned
- Android target: ⏳ planned
