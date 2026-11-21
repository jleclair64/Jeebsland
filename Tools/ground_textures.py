# Paints the ground's pictures (2026-11-12): the grass ground, the rock for steep slopes and foundation stones, and the cobbles
# of Oak Hill's streets (colour and normal map). Pure Python like gen_textures.py (it runs inside the editor's Python, and
# with any other Python for a look at the result: python ground_textures.py <folder> writes them there).
# Each picture is only painted again when its version number changes (cached in Saved/ as <name>_v<version>.png).
#
# The look (CLAUDE.md, art direction): Breath of the Wild ground, painted, soft and clean. Each picture tiles both ways. No
# contour lines or stripes, no photo grit; a little brush-like variation; lighting does the shading (the cobbles' shape is in
# their normal map, not painted on).
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_textures import Noise, fbm, cached, clamp01, to8, write_png   # noqa: E402


def _srgb_to_lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _lin_to_srgb(c):
    c = clamp01(c)
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


class Voronoi:
    """Jittered points on an n x n grid that wraps (so the picture tiles): nearest and second-nearest distance and the cell's id"""

    def __init__(self, seed, n, jitter=0.85):
        r = random.Random(seed)
        self.n = n
        self.pts = [((i + 0.5 + (r.random() - 0.5) * jitter), (j + 0.5 + (r.random() - 0.5) * jitter)) for j in range(n) for i in range(n)]

    def at(self, u, v):
        n = self.n
        x, y = u * n, v * n
        ci, cj = int(math.floor(x)), int(math.floor(y))
        f1 = f2 = 1e9
        best = 0
        bx = by = 0.0
        for dj in (-1, 0, 1):
            for di in (-1, 0, 1):
                i, j = ci + di, cj + dj
                k = (j % n) * n + (i % n)
                px, py = self.pts[k]
                px += i - (i % n)
                py += j - (j % n)
                d = (px - x) ** 2 + (py - y) ** 2
                if d < f1:
                    f2 = f1
                    f1 = d
                    best = k
                    bx, by = px - x, py - y
                elif d < f2:
                    f2 = d
        return math.sqrt(f1), math.sqrt(f2), best, bx, by


# ------------------------------------------------------------------ grass ground: painted blades over a mottled green
def paint_grass_ground():
    N = 512
    rnd = random.Random(8811)
    mottle = [Noise(2201 + i, 4 << i) for i in range(4)]
    # the base: soft mottled green, close to the meadow tufts seen from afar (so the land past the grass reads the same)
    lin = [0.0] * (N * N * 3)
    for j in range(N):
        for i in range(N):
            u, v = i / N, j / N
            m = fbm(mottle, u, v)
            s = 0.0   # (no painted bare bits: they repeated every 3 m as a grid of brown dots; worn ground comes from the masks)
            l = 0.84 + 0.36 * (m - 0.5)
            r = (0.235 + 0.05 * m) * (1 - s) + 0.30 * s
            g = (0.375 + 0.04 * m) * (1 - s) + 0.24 * s
            b = 0.175 * (1 - s) + 0.15 * s
            o = (j * N + i) * 3
            lin[o], lin[o + 1], lin[o + 2] = _srgb_to_lin(r * l), _srgb_to_lin(g * l), _srgb_to_lin(b * l)
    # painted blades: short tapered strokes in many directions, lighter and darker greens (brush strokes, not noise)
    tones = [(0.30, 0.42, 0.17), (0.34, 0.46, 0.19), (0.22, 0.33, 0.14), (0.38, 0.47, 0.20), (0.26, 0.38, 0.16), (0.42, 0.50, 0.23)]
    for _ in range(14000):
        cx, cy = rnd.uniform(0, N), rnd.uniform(0, N)
        ang = rnd.uniform(0, math.pi)
        length = rnd.uniform(5.0, 14.0)
        width = rnd.uniform(0.9, 2.1)
        tr, tg, tb = rnd.choice(tones)
        k = rnd.uniform(0.85, 1.12)
        col = (_srgb_to_lin(tr * k), _srgb_to_lin(tg * k), _srgb_to_lin(tb * k))
        alpha = rnd.uniform(0.35, 0.7)
        dx, dy = math.cos(ang), math.sin(ang)
        steps = int(length * 1.5) + 1
        for s in range(steps):
            t = s / (steps - 1) if steps > 1 else 0.0
            w = width * (1.0 - 0.75 * t)                                       # tapers to a tip
            px, py = cx + dx * length * t, cy + dy * length * t
            r0 = int(math.ceil(w))
            for oy in range(-r0, r0 + 1):
                for ox in range(-r0, r0 + 1):
                    d = math.sqrt(ox * ox + oy * oy)
                    cover = clamp01(w - d + 0.5) * alpha * 0.55
                    if cover <= 0.0:
                        continue
                    o = ((int(py + oy) % N) * N + (int(px + ox) % N)) * 3
                    lin[o] += (col[0] - lin[o]) * cover
                    lin[o + 1] += (col[1] - lin[o + 1]) * cover
                    lin[o + 2] += (col[2] - lin[o + 2]) * cover
    px = bytearray(N * N * 4)
    for k in range(N * N):
        o = k * 3
        q = k * 4
        px[q], px[q + 1], px[q + 2], px[q + 3] = to8(_lin_to_srgb(lin[o])), to8(_lin_to_srgb(lin[o + 1])), to8(_lin_to_srgb(lin[o + 2])), 255
    return N, N, px


# ------------------------------------------------------------------ cobbles: rounded stones set in packed earth (colour + height)
_COBBLE = {}


def _cobble_fields():
    if _COBBLE:
        return _COBBLE
    N = 512
    vor = Voronoi(5101, 13, 0.75)
    rnd = random.Random(5200)
    stone = [(rnd.uniform(0.40, 0.55), rnd.uniform(-1, 1)) for _ in range(vor.n * vor.n)]
    rnd_pick = random.Random(5250)   # (its own stream, so the stones keep their colours)
    pick = [rnd_pick.uniform(0.04, 1.0) for _ in range(vor.n * vor.n)]   # alpha: each stone its own number, so M_Dirt drops stones whole where the street frays out
    tone = [Noise(5301 + i, 16 << i) for i in range(2)]
    soil = [Noise(5401 + i, 8 << i) for i in range(3)]
    warp = [Noise(5451 + i, 8 << i) for i in range(2)]
    warp2 = [Noise(5461 + i, 8 << i) for i in range(2)]
    h = [0.0] * (N * N)
    col = bytearray(N * N * 4)
    for j in range(N):
        for i in range(N):
            u, v = i / N, j / N
            wu = u + 0.012 * (fbm(warp, u, v) - 0.5)
            wv = v + 0.012 * (fbm(warp2, u, v) - 0.5)
            f1, f2, k, _, _ = vor.at(wu % 1.0, wv % 1.0)
            gap = f2 - f1                                     # cell units: 0 at the joint
            # a stone: away from the joint and not too far from its middle, so the corners come out rounded
            edge = clamp01((gap - 0.05) / 0.13) * clamp01((0.74 - f1) / 0.14)
            dome = math.sin(edge * math.pi * 0.5)             # rounded top
            joint = 1.0 - clamp01(edge * 4.0)
            h[j * N + i] = dome
            lum, warm = stone[k]
            lum *= 0.93 + 0.14 * fbm(tone, u, v)
            lum *= 0.9 + 0.1 * dome                           # a touch darker toward each stone's edge (painted, soft)
            sr, sg, sb = lum * (1.05 + 0.05 * warm), lum * (1.0 + 0.01 * warm), lum * (0.90 - 0.05 * warm)
            e = 0.8 + 0.3 * fbm(soil, u, v)
            er, eg, eb = 0.26 * e, 0.19 * e, 0.13 * e          # packed earth in the joints
            r = sr * (1 - joint) + er * joint
            g = sg * (1 - joint) + eg * joint
            b = sb * (1 - joint) + eb * joint
            o = (j * N + i) * 4
            col[o], col[o + 1], col[o + 2], col[o + 3] = to8(r), to8(g), to8(b), to8(pick[k] * clamp01(edge * 4.0))   # (0 in the joints)
    _COBBLE["N"] = N
    _COBBLE["h"] = h
    _COBBLE["col"] = col
    return _COBBLE


def paint_cobble():
    c = _cobble_fields()
    return c["N"], c["N"], c["col"]


def paint_cobble_normal():
    c = _cobble_fields()
    N, h = c["N"], c["h"]
    strength = 4.0   # how steep the stones' sides look
    px = bytearray(N * N * 4)
    for j in range(N):
        for i in range(N):
            dx = (h[j * N + (i + 1) % N] - h[j * N + (i - 1) % N]) * 0.5 * strength
            dy = (h[((j + 1) % N) * N + i] - h[((j - 1) % N) * N + i]) * 0.5 * strength
            nx, ny, nz = -dx, -dy, 1.0
            l = math.sqrt(nx * nx + ny * ny + nz * nz)
            o = (j * N + i) * 4
            # (Unreal's normal maps are DirectX style: green points down the picture, so Y is flipped)
            px[o], px[o + 1], px[o + 2], px[o + 3] = to8(nx / l * 0.5 + 0.5), to8(-ny / l * 0.5 + 0.5), to8(nz / l * 0.5 + 0.5), 255
    return N, N, px


def grass_ground():
    return cached("T_GrassGround", 2, paint_grass_ground)


def cobble():
    return cached("T_Cobble", 2, paint_cobble)   # (2: the per-stone number in alpha, 2026-11-14)


def cobble_normal():
    return cached("T_CobbleN", 1, paint_cobble_normal)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for name, painter in (("T_GrassGround", paint_grass_ground), ("T_Cobble", paint_cobble), ("T_CobbleN", paint_cobble_normal)):
        w, h, px = painter()
        write_png(os.path.join(out, name + ".png"), w, h, px)
        print("painted", name)
