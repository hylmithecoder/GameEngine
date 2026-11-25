#!/usr/bin/env python3
"""Generate original FK starter loops; no third-party motion data is used.

Run from any directory. Assets are redistributable under the engine's license.
VMD stores CP932 names, 30 FPS, left-handed translations and XYZW rotations.
"""
from pathlib import Path
import math
import struct

DESTINATION = Path(__file__).resolve().parents[1] / "assets" / "motions"


def generate(name, duration, running=False):
    frames = round(duration * 30)
    keys = []
    names = ["上半身", "頭", "センター", "左腕", "右腕"]
    if name != "Breathing":
        names += ["左足", "右足", "左ひざ", "右ひざ", "左足首", "右足首"]
    for bone in names:
        for frame in range(frames + 1):
            phase = 2 * math.pi * frame / frames
            x = z = y = 0.0
            translation = (0, 0, 0)
            if bone == "上半身":
                x = 0.015 * math.sin(phase) if name == "Breathing" else (-0.12 if running else -0.04)
                y = 0 if name == "Breathing" else 0.035 * math.sin(phase)
            elif bone == "頭":
                x = -0.008 * math.sin(phase)
            elif bone == "センター":
                # Standard MMD units (roughly 20 units per character height).
                translation = (0, 0.025 * math.sin(phase) if name == "Breathing" else 0.12 * (1 - math.cos(2 * phase)), 0)
            else:
                left = bone.startswith("左")
                cycle = phase + (0 if left else math.pi)
                if bone.endswith("腕"):
                    # Lower the arms out of the standard PMX T/A rest pose.
                    z = (-1 if left else 1) * 0.72
                    x = 0 if name == "Breathing" else -(0.50 if running else 0.28) * math.sin(cycle)
                elif bone.endswith("ひざ"):
                    x = -(0.95 if running else 0.60) * max(0, -math.sin(cycle))
                elif bone.endswith("足首"):
                    x = 0.12 * math.sin(cycle)
                elif bone.endswith("足"):
                    x = (0.65 if running else 0.40) * math.sin(cycle)
            # Quaternion from XYZ Euler angles.
            cx, sx = math.cos(x / 2), math.sin(x / 2)
            cy, sy = math.cos(y / 2), math.sin(y / 2)
            cz, sz = math.cos(z / 2), math.sin(z / 2)
            rotation = (sx*cy*cz-cx*sy*sz, cx*sy*cz+sx*cy*sz,
                        cx*cy*sz-sx*sy*cz, cx*cy*cz+sx*sy*sz)
            curve = bytes([20]*8 + [107]*8) * 4  # linear XY control points
            keys.append(bone.encode("cp932").ljust(15, b"\0") +
                        struct.pack("<I3f4f", frame, *translation, *rotation) + curve)
    header = b"Vocaloid Motion Data 0002".ljust(30, b"\0")
    data = header + b"Ilmeee FK Starter".ljust(20, b"\0")
    data += struct.pack("<I", len(keys)) + b"".join(keys) + struct.pack("<5I", 0, 0, 0, 0, 0)
    DESTINATION.mkdir(parents=True, exist_ok=True)
    (DESTINATION / f"{name}.vmd").write_bytes(data)
    print(f"{name}: {len(keys)} keys, {duration}s")


if __name__ == "__main__":
    generate("Breathing", 4.0)
    generate("Walk", 1.2)
    generate("Run", 0.8, running=True)
