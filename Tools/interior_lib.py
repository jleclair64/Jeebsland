# The walk-in houses' room lists: the placing helpers Tools/interior_rooms.py uses (2026-11-21).
# Everything is placed by its bounds (Tools/interior_bounds.csv), never by its pivot: put() takes where the middle of the piece's
# footprint goes and where its underside sits, and works out the pivot. So nothing floats or sinks whatever its model's origin is.
# The house's frame: X along its long side, +Y out of the door, Z up from the floor, cm. Room inside the walls: X -455..495,
# Y -315..230, 270 high. Door X -49..103 in the front wall (Y 230). Hearth at the west end, middle Y -50.
import math, os

HERE = os.path.dirname(os.path.abspath(__file__))
X0, X1, Y0, Y1, H = -455.0, 495.0, -315.0, 230.0, 270.0
DOOR = (-49.0, 103.0)
FLOOR = 3.0   # the planks' top above the lists' Z 0 (the floor piece is 10 thick, set at -7): everything placed here stands on the planks
T = "/Game/Medieval_Tavern/Meshes/Props/SM_"
TM = "/Game/Medieval_Tavern/Meshes/Modular/SM_"
D = "/Game/TownDecor/SM_"
P = "/Game/Props/SM_"
def I(n): return "/Game/Interior/%s/SM_Int_%s" % (n, n)

BOUNDS = {}
for line in open(os.path.join(HERE, "interior_bounds.csv")):
    if line.startswith("#") or not line.strip(): continue
    c = line.strip().split(",")
    BOUNDS[c[0]] = [float(v) for v in c[1:7]]

def rot(yaw, pitch, roll):
    # Unreal's FRotator as a matrix (rows applied to a column vector): yaw about Z, pitch about Y (nose up), roll about X
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    cr, sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
    return [[cp * cy, sr * sp * cy - cr * sy, -(cr * sp * cy + sr * sy)],
            [cp * sy, sr * sp * sy + cr * cy, cy * sr - cr * sp * sy],
            [sp, -sr * cp, cr * cp]]

class Piece:
    def __init__(s, mesh, pivot, yaw, pitch, roll, sc, lo, hi, solid, shadow, light, radius, mat, note):
        s.mesh, s.pivot, s.yaw, s.pitch, s.roll, s.sc, s.lo, s.hi = mesh, pivot, yaw, pitch, roll, sc, lo, hi
        s.solid, s.shadow, s.light, s.radius, s.mat, s.note = solid, shadow, light, radius, mat, note
        s.kind = "floor"   # floor / on / wall / flat / free
    @property
    def top(s): return s.hi[2]
    @property
    def cx(s): return (s.lo[0] + s.hi[0]) * 0.5
    @property
    def cy(s): return (s.lo[1] + s.hi[1]) * 0.5
    def row(s):
        f = lambda v: ("%.1f" % v).rstrip("0").rstrip(".") if abs(v) > 1e-6 else "0"
        sc = s.sc if isinstance(s.sc, tuple) else (s.sc, s.sc, s.sc)
        c = [s.mesh, f(s.pivot[0]), f(s.pivot[1]), f(s.pivot[2] + FLOOR), f(s.yaw), f(s.pitch), f(s.roll), "%.4g" % sc[0], "%.4g" % sc[1], "%.4g" % sc[2],
             "1" if s.solid else "0", "1" if s.shadow else "0"]
        if s.light or s.mat: c += [f(s.light) if s.light else "", f(s.radius) if s.light else ""]
        if s.mat: c += [s.mat]
        return ",".join(c)

class Room:
    def __init__(s, plot, title):
        s.plot, s.title, s.lines, s.pieces, s.problems = plot, title, [], [], []
    def note(s, text): s.lines.append("# " + text)
    def raw(s, text): s.lines.append(text)
    def light(s, x, y, z, lm, radius, fire=False):
        s.lines.append("%s,%g,%g,%g,,,,,,,0,0,%g,%g" % ("FIRE" if fire else "LIGHT", x, y, z + FLOOR, lm, radius))
    def put(s, mesh, x, y, z=0.0, yaw=0.0, sc=1.0, solid=None, pitch=0.0, roll=0.0, light=0.0, radius=300.0, mat="", kind="floor", shadow=False, on=None):
        """The middle of the piece's footprint at x, y and its underside at z (on=<piece>: x, y are offsets from that piece's middle, z its top)."""
        if mesh not in BOUNDS: raise KeyError(mesh)
        if on is not None: x, y, z, kind = on.cx + x, on.cy + y, on.top + z, "on"
        b = BOUNDS[mesh]
        s3 = sc if isinstance(sc, tuple) else (sc, sc, sc)
        m = rot(yaw, pitch, roll)
        lo, hi = [1e9] * 3, [-1e9] * 3
        for i in range(8):
            v = [b[(3 if i & 1 else 0)] * s3[0], b[(4 if i & 2 else 1)] * s3[1], b[(5 if i & 4 else 2)] * s3[2]]
            w = [sum(m[r][k] * v[k] for k in range(3)) for r in range(3)]
            for k in range(3): lo[k] = min(lo[k], w[k]); hi[k] = max(hi[k], w[k])
        piv = [x - (lo[0] + hi[0]) * 0.5, y - (lo[1] + hi[1]) * 0.5, z - lo[2]]
        lo = [lo[k] + piv[k] for k in range(3)]; hi = [hi[k] + piv[k] for k in range(3)]
        if solid is None: solid = kind == "floor" and (hi[2] - lo[2]) > 35 and max(hi[0] - lo[0], hi[1] - lo[1]) > 45
        p = Piece(mesh, piv, yaw, pitch, roll, sc, lo, hi, solid, shadow, light, radius, mat, "")
        p.kind = kind; p.parent = on
        s.pieces.append(p); s.lines.append(p)
        return p
    def wall(s, side, mesh, along, z=0.0, gap=1.0, turn=0.0, **k):
        """Against a wall, its back to it (a model's back is its -Y side; turn adds to the yaw): side N the back wall, S the front
        (door) wall, W the hearth end, E the far end; along = X for N/S, Y for W/E."""
        yaw = {"N": 0.0, "S": 180.0, "W": -90.0, "E": 90.0}[side] + turn
        k.setdefault("kind", "floor" if z < 1 else "wall")
        p = s.put(mesh, 0, 0, z, yaw, **k)
        dx, dy = (p.hi[0] - p.lo[0]) * 0.5, (p.hi[1] - p.lo[1]) * 0.5
        x, y = {"N": (along, Y0 + dy + gap), "S": (along, Y1 - dy - gap), "W": (X0 + dx + gap, along), "E": (X1 - dx - gap, along)}[side]
        s.pieces.remove(p); s.lines.remove(p)
        return s.put(mesh, x, y, z, yaw, **k)
    def check(s):
        """Nothing through a wall, the ceiling or the floor; nothing standing in the doorway's path; floor pieces not through each other;
        pieces set on another piece inside its top."""
        bad = s.problems
        fl = [p for p in s.pieces if p.kind == "floor" and (p.hi[2] - p.lo[2]) > 6]
        for p in s.pieces:
            n = p.mesh.split("/")[-1]
            if p.kind == "shell": continue
            if p.lo[0] < X0 - 0.5 or p.hi[0] > X1 + 0.5 or p.lo[1] < Y0 - 0.5 or p.hi[1] > Y1 + 0.5: bad.append("%s through a wall: %s .. %s" % (n, [round(v) for v in p.lo], [round(v) for v in p.hi]))
            if p.lo[2] < -0.6 or p.hi[2] > H + 0.5: bad.append("%s through the floor or ceiling: z %.0f .. %.0f" % (n, p.lo[2], p.hi[2]))
            if p.kind in ("floor",) and p.hi[2] - p.lo[2] > 6 and p.hi[0] > -45 and p.lo[0] < 99 and p.hi[1] > 95: bad.append("%s stands in the doorway's path" % n)
            if p.kind == "on" and p.parent is not None:
                q = p.parent
                if p.cx < q.lo[0] or p.cx > q.hi[0] or p.cy < q.lo[1] or p.cy > q.hi[1]: bad.append("%s is not over %s" % (n, q.mesh.split("/")[-1]))
        for i, p in enumerate(fl):
            for q in fl[i + 1:]:
                ox = min(p.hi[0], q.hi[0]) - max(p.lo[0], q.lo[0]); oy = min(p.hi[1], q.hi[1]) - max(p.lo[1], q.lo[1])
                if ox > 4 and oy > 4 and not (getattr(p, "ok", False) or getattr(q, "ok", False)):
                    bad.append("%s and %s overlap by %.0f x %.0f" % (p.mesh.split("/")[-1], q.mesh.split("/")[-1], ox, oy))
        return bad
    def write(s, folder):
        out = ["Mesh,X,Y,Z,Yaw,Pitch,Roll,SX,SY,SZ,Solid,Shadow,Light,Radius,Mat",
               "# %s: %s. Written by Tools/interior_rooms.py (edit there and run it again; no build needed). AHouseInterior, HouseInterior.h." % (s.plot, s.title),
               "# X along the house's long side, +Y out of the door, Z from the floor, cm. Room X -455..495, Y -315..230, 270 high; door X -49..103."]
        for l in s.lines: out.append(l.row() if isinstance(l, Piece) else l)
        open(os.path.join(folder, "Interior_%s.csv" % s.plot), "w", newline="\n").write("\n".join(out) + "\n")
