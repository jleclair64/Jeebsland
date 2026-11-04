# Blender (background): measures where a prepared Meshy knight's joints are, in knight space (cm, feet at Z = -96, as
# knight_blender_export.py places it), so each armour set can be fitted to the shared skeleton (KnightDesign::PivotPos).
# Run: blender -b -P Tools/knight_blender_measure.py -- <work dir> <set>
#   FIST:  the centre of the lowest 8 cm of each arm
#   ARM:   per 2 cm band down the right arm, the arm's front, back and outer edge (the elbow sticks out at the back)
#   LEG:   per 2 cm band down the left leg, its front, back and width (the knee sticks out at the front)
#   TORSO: per 2 cm band the body's half width without the arms
import bpy, sys, os
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
work, SET = argv[0], argv[1]
TAG = "" if SET == "Iron" else "_" + SET
bpy.ops.wm.open_mainfile(filepath=os.path.join(work, "knight_dec%s.blend" % TAG))
me = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0].data
zs = [v.co.z for v in me.vertices]
zmin, zmax = min(zs), max(zs)
S = 162.5 / (zmax - zmin)
P = [Vector((-v.co.y * S, -v.co.x * S, (v.co.z - zmin) * S - 96.0)) for v in me.vertices]

# the arm: everything right of the body's side at that height (the torso's half width found at the chest, below the arms' reach)
def band(lo, hi, pts): return [p for p in pts if lo <= p.z < hi]
right = [p for p in P if p.y > 0]
torso_hw = {}
for z in range(-60, 40, 2):
    b = sorted(abs(p.y) for p in band(z, z + 2, P) if abs(p.x) < 30)
    torso_hw[z] = b[len(b) // 2] if b else 0
print("SET", SET)
arm = [p for p in right if p.y > 21.0 and -60 < p.z < 30]
amin = min(p.z for p in arm)
low = [p for p in arm if p.z < amin + 8.0]
c = sum(low, Vector()) / len(low)
print("FIST right centre x %.1f y %.1f z %.1f (arm bottom %.1f)" % (c.x, c.y, c.z, amin))
for z in range(30, int(amin) - 2, -2):
    b = band(z, z + 2, arm)
    if b: print("ARM z %4d  front %6.1f back %6.1f  out %5.1f in %5.1f" % (z, max(p.x for p in b), min(p.x for p in b), max(p.y for p in b), min(p.y for p in b)))
legL = [p for p in P if p.y < -3.0 and p.z < -20]
for z in range(-20, -96, -2):
    b = band(z, z + 2, legL)
    if b: print("LEG z %4d  front %6.1f back %6.1f  out %5.1f in %5.1f" % (z, max(p.x for p in b), min(p.x for p in b), min(p.y for p in b), max(p.y for p in b)))
mid = [p for p in P if abs(p.y) < 2.0 and -60 < p.z < 0]
# the crotch: going up the middle line, the first band with a lot of surface between the legs
for z in range(-80, 0, 2):
    if len(band(z, z + 2, mid)) > 12: print("CROTCH about z %d" % z); break
# the fist alone (2026-11-03): arm surface outside the thigh's reach, below the elbow; its lowest 9 cm is the fist
for sd, nm in ((1, "right"), (-1, "left")):
    fa = [p for p in P if p.y * sd > 27.0 and -50 < p.z < -10]
    if not fa: continue
    bot = min(p.z for p in fa)
    fist = [p for p in fa if p.z < bot + 9.0]
    c = sum(fist, Vector()) / len(fist)
    hi = [p for p in fa if p.z > -14]
    e = sum(hi, Vector()) / len(hi) if hi else Vector()
    print("FIST2 %s centre x %.1f y %.1f z %.1f bottom %.1f | elbow band centre x %.1f y %.1f" % (nm, c.x, c.y, c.z, bot, e.x, e.y))
