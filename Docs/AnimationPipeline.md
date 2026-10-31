# Animation clips for the knight

The knight is not an Unreal skeletal mesh: he is the Meshy model cut into rigid pieces, each hanging off a joint (a
`USceneComponent` pivot) that code turns every frame. Animation clips are made in Blender on a rig that matches those joints
exactly, exported as joint rotations, and played on the same joints (the clip player is Stage 3, not built yet).

## The joints (source of truth: the C++)
`KnightDesign::PivotPos` (KnightDesign.h) gives every joint's position; the knight's constructor (KnightCharacter.cpp,
`MakePivot`) gives the parenting:

```
BodyRoot (pelvis)
|- HipL - KneeL - AnkleL        |- HipR - KneeR - AnkleR
`- UpperPivot (chest)
   |- HeadPivot - PlumePivot
   |- ArmPivotR (shoulder) - ElbowR - WristR (sword hand, 2026-10-31)
   |- ArmPivotL - ElbowL
   `- CapeUpper - CapeLower   (old cape joints; the cloth cape does not use them)
```
Every joint has zero rotation at rest; its axes are the body's: X forward, Y right, Z up, centimetres (Unreal is left-handed).

## The wrist (2026-10-31)
The right fist used to be part of the forearm piece. `Tools/knight_blender_export.py` now cuts it off just under the
gauntlet cuff (`WRIST_CUT_Z`) onto its own joint, `P_WristR` / `WristR` (child of `ElbowR`, at `KnightDesign::WristPos(1)`).
The held weapon and the right ring hang off it. At rest it does not turn, so the knight looks exactly as before (checked:
the before and after pictures are pixel-identical). The pen knight has the joint too, but never turns it.

## Tools
| Script | What it does |
|---|---|
| `Tools/knight_rig.py` | Builds `Art/Knight/knight_rig.blend`: an armature with one bone per joint (read from the C++), the game's own armour pieces (`Content/Knight/KnightParts.bin`) hung on the bones, and a stand-in sword in the right fist. Renders rest and test-pose pictures to `Saved/KnightRig`. Rebuild it whenever the joints or the model change. |
| `Tools/knight_clip_export.py` | Exports a Blender action on that rig to `Content/Anim/<clip>.json`, and checks the export by rebuilding every joint's position the way the game will (the gap is printed; it should be about 0). |

```
blender -b -P Tools/knight_rig.py -- <project dir>
blender -b Art/Knight/knight_rig.blend -P Tools/knight_clip_export.py -- <project dir> <action> [<clip name>] [--loop]
```
(Blender 5.2: `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`. Add `--test` instead of an action name for a
small test clip, `test_arm_wave`.)

## Coordinates
The rig is the game mirrored in Y: Blender (x, y, z) = game (X, -Y, Z), 1 Blender unit = 1 cm. The knight faces Blender +X;
his right side is Blender -Y. A Blender quaternion becomes a game quaternion as (x, y, z, w) -> (-x, y, -z, w).

## Clip file (version 1)
```
{ "version": 1, "name": "...", "fps": 30, "frames": N, "duration": seconds, "loop": false,
  "joints": ["BodyRoot", ...],              // parents before children
  "markers": { "hit_start": 0.4, ... },     // seconds, from Blender timeline markers (gameplay timing)
  "root": [[x, y, z], ...],                 // BodyRoot's movement from rest, game cm, per frame
  "rot": [[[x, y, z, w], ...joints], ...] } // each joint's rotation relative to its parent, game axes, per frame
```
