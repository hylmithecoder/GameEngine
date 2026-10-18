# Engine starter motions

For automatic Idle/Walk/Run selection and blending, see [Character locomotion](CONTROLLERS.md).

Breathing.vmd, Walk.vmd and Run.vmd are original procedural FK starter loops
created for Ilmeee Engine, distributed under the repository's LICENSE.
They contain no third-party motion data. Regenerate them with
`python3 scripts/generate-default-motions.py` from the engine checkout.

These files ship in the engine's assets, visible under **Engine Motions** in
Animation and Explorer for every project. Select a PMX character in the scene,
click a motion to preview, then use **Save + Assign to Character** to create
an editable `assets/animations/*.ilmeeeanim` copy inside your project. The
project copy is included in game exports and survives moving the project.
Double-click project VMD files in Explorer, drag them onto Animation's clip
path, or use Browse Motion to preview downloaded motions.

The loops use standard Japanese MMD bone names and 30 FPS. Walk and Run are
in-place FK examples, not production mocap: rig proportions, rest arm angles
and foot placement may need adjustment in the bone editor. Import preserves
VMD translation/rotation Bezier curves and converts MMD handedness to match
PMX rendering. Matching uses exact bone names; unsupported/missing tracks
are reported by the preview's matched-track count.

Current limitations: no IK solving, facial morph playback, camera motion,
grant/append constraints or physics. IK-heavy third-party walking motions can
therefore differ from MMD. VMD sources are never overwritten; editing saves
an Ilmeee clip, not a VMD export.

External compatibility reference (not redistributed here):
https://github.com/takahirox/MMDLoader-app/tree/master/assets/vmd/walk
Its Readme.txt credits the original author and requires following that
author's policy. A public download is not treated as permission to bundle
it in commercial games. Check creator terms before importing external data.
