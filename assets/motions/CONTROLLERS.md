# Character locomotion

Select a PMX character, then use **Mesh > Locomotion Controller** in Inspector.
Animation has two tools: **Clip Editor** for individual VMD/Ilmeee clips and
**Locomotion Controller** for character behavior. The old empty Main View /
Viewport / Particle Editor tabs have been removed; Scene and Game remain.

1. Choose **Use Starter Motions** to draft Idle/Breathing, Walk and Run.
2. Adjust Walk Speed and Run Speed to the character's movement in world units
   per second. The run boundary is the midpoint between these reference speeds.
   Joystick magnitude, the movement component and C++ scripts determine movement.
3. Use the preview Speed slider to test Idle / Walk / Run and transitions. The
   preview does not change scene transforms. Missing Walk/Run slots fall back to
   the available states; Idle is required.
4. **Save + Assign to Character** writes an `.ilmeeecontroller` asset and its
   imported clips to the project, assigns it, and saves the scene. Enable the
   character's movement component or use a C++ script to move it during Game Play.

Controllers are reusable project assets. Clip references are relative to the
controller's directory. Saving imports external/VMD sources as editable
`.ilmeeeanim` copies, leaving the source files intact. A controller takes
precedence over a character's single clip. **Use Single Clip** in Inspector
returns to direct clip playback. Changes to an assigned controller are reloaded
on its next playback update after saving through the editor.

The runtime measures actual world-space horizontal displacement after movement
and native scripts. A 50 ms sample window avoids flicker between fixed movement
steps. Idle/Walk/Run boundaries have hysteresis. Vertical-only movement and
teleports do not start a walking cycle. NPCs and player characters use the same
runtime. Jump/fall/attack states and root motion driving gameplay are future work.

Walk/Run playback scales with actual speed (bounded to 0.35x..1.75x), and their
cycle phase is shared. Transitions blend each bone from its currently displayed
pose, so interrupting a transition does not snap back to an older clip. Missing
bone tracks blend toward the bind pose. In-place mode strips horizontal
translation from standard MMD Center/Groove/All Parent bones and the configured
additional root bone, while preserving vertical bounce and limb motion.

The default transition is 0.2 seconds. Rig proportions and foot placement can
still need clip edits: FK animation currently has no IK, facial morphs, append
constraints or physics. This controller is locomotion selection and blending,
not full MMD simulation or a foot-locking solver.

Scene format is now v1.16; older scenes continue to load without a controller.
New scenes need the updated editor/player. Controller bindings participate in
Undo/Redo. Export gathers controllers and every referenced motion, rewriting
paths relative to the exported asset so the game can move between machines.
