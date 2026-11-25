# Android performance and responsive Canvas

The screenshot's roughly 128 ms update time is CPU-side work. It includes
movement, rigid-body collisions, scripts, animation, spring bones, skinning
and media, so the original number alone cannot identify the slow component.

The player now sends `movementMs`, `physicsMs`, `scriptsMs`, and `animationMs`
alongside its existing telemetry. **Device Monitor > Update detail** shows
these values. `animationMs` includes pose evaluation, spring simulation, CPU
skinning and vertex upload (including any wait for the previous scene pass).
`physicsMs` measures the Physics Body/Collider system. Details are unavailable
for older players, rather than incorrectly reported as zero.

Skinning now blends bone matrices once for each vertex's position and normal.
Normals use normalized cofactors, preserving inverse-transpose behavior for
nonuniform scales, reflections, and singular fallbacks. The vertex buffer on
the CPU is reused. Spring simulation shares local pose frames across substeps,
resolves collider bone names during setup, and exits collision passes when no
projection occurs. Fixed-step rates and simulation limits are unchanged.

A local synthetic CPU benchmark with both versions compiled at `-O2` measured
100k-vertex skinning at 13.1 -> 9.5 ms and 300-joint/four-step spring simulation
at 0.92 -> 0.54 ms. These are kernel measurements on the development computer,
not Android frame-rate results. Skinning still runs on the CPU; dense character
scenes can remain expensive and should be measured with the new telemetry.

## Existing project UI

1. Reopen the rebuilt editor. Select the character with the **Dialog** component.
2. Click **Make Dialog UI Responsive**, then save the scene. This changes the
   linked Box/Text to **Stretch Bottom** and the speaker name to **Bottom Left**,
   preserving their positions at the Canvas reference resolution.
3. Set **Canvas > Reference size** to your design resolution, for example
   `1280 x 720`. Coordinates and font sizes use these reference pixels.
4. Keep joysticks at **Bottom Left** / **Bottom Right**, and fixed-size controls
   at a corner or center anchor. Stretch panels, backgrounds and dialog text.

New **Create Dialog UI** panels/text use Stretch Bottom automatically. Existing
UI keeps its original anchors until edited or converted with the button above.

| Anchor | Behavior |
| --- | --- |
| Top/Bottom Left/Right | Scaled distance from that corner |
| Center | Centered, with signed X/Y offsets |
| Top/Bottom Center | Horizontal center and distance from top/bottom |
| Center Left/Right | Vertical center and distance from the side |
| Stretch Top/Bottom/Center | Adds extra viewport width to the reference width |
| Stretch Full | Adds extra viewport width and height |

Stretch keeps **Width/Height as reference sizes**, rather than treating them as
right/bottom offsets. For example, a Bottom Stretch panel at X=40, Width=1200
in a 1280-wide Canvas retains 40-reference-pixel side margins on wider screens.
The font and joystick shape keep a uniform scale. Aspect ratios necessarily
show different amounts of the world; Canvas anchors control UI placement.

The standalone player uses SDL's window safe area for both UI layout and hit
testing, keeping controls away from reported notches and system UI. The 3D
scene still fills the window. Text is clipped to its element rectangle.

## Rebuild

Scene format **1.19** adds the anchor meanings without changing the binary
entity layout. Existing scenes remain readable. Older Android runtimes must
be rebuilt before exporting new scenes; the exporter checks ABI metadata.
Standalone UI JSON uses version 2 when it contains the new anchors and still
loads existing version 1 documents.

```sh
nix-shell --run 'TMPDIR=/tmp cmake --build build --target GameEngineSDL IlmeeePlayer -j2 --verbose'
nix-shell --run 'TMPDIR=/tmp BUILD_TYPE=Debug scripts/android/build-player.sh'
```

The Android Debug player retains debug symbols and applies `-O2` to CPU
animation/physics hot paths. Release keeps its normal optimization settings.
Build the game's APK again through **Build > Android**, and compare the same
scene/camera on the same device in Device Monitor. Updating engine libraries
does not update an already installed APK.
