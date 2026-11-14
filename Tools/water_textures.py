# Paints the water's pictures (2026-11-13) as PNG files in Saved/, each painted again only when its version number changes:
#   T_WaterN     a tiling normal map of small wind ripples: a sum of a few dozen sine waves whose wave vectors are whole
#                numbers of cycles per tile (so it tiles both ways with no seam), amplitudes falling with frequency like real
#                wind waves, mostly running one way with a wide spread. M_Water samples it at two sizes and angles, panning
#                across each other, and once more at a large size, so no repeat shows.
#   T_WaterFoam  a tiling soft noise of blobs and gaps that breaks up the foam line where the water meets the bank.
# Pure Python (runs inside the editor's Python, or the engine's own python.exe).
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_textures import Noise, cached, clamp01, fbm, to8   # noqa: E402  (shared helpers only; gen_textures is not changed)

NORMAL_VERSION = 1
FOAM_VERSION = 1


def paint_water_normal():
    N = 512
    rnd = random.Random(7307)
    waves = []
    seen = set()
    wind = 0.6   # radians: the main way the ripples run (M_Water turns the two samplings apart anyway)
    tries = 0
    while len(waves) < 90 and tries < 5000:
        tries += 1
        f = math.exp(rnd.uniform(math.log(4.0), math.log(48.0)))           # cycles per tile, log-uniform
        ang = wind + rnd.gauss(0.0, 0.85)                                   # a wide spread round the wind
        m = int(round(f * math.cos(ang)))
        n = int(round(f * math.sin(ang)))
        if (m, n) == (0, 0) or (m, n) in seen or (-m, -n) in seen:
            continue
        seen.add((m, n))
        k = math.hypot(m, n)
        amp = k ** -1.35                                                    # height falls with frequency (slopes ~ k^-0.35: ripples at every size)
        phase = rnd.randrange(N)
        waves.append((m, n, amp, phase))

    # tables: one period of sine and cosine over N steps, so every wave is a lookup (the waves are whole cycles per tile)
    two_pi = 2.0 * math.pi
    COS = [math.cos(two_pi * i / N) for i in range(N)]
    # slope of each wave: d/dx [a sin(2 pi (m x + n y) / N)] = a 2 pi m / N cos(...), per pixel
    gx = [0.0] * (N * N)
    gy = [0.0] * (N * N)
    for (m, n, amp, phase) in waves:
        sx = amp * two_pi * m
        sy = amp * two_pi * n
        for y in range(N):
            base = (n * y + phase) % N
            row = y * N
            for x in range(N):
                c = COS[(m * x + base) % N]
                gx[row + x] += sx * c
                gy[row + x] += sy * c
    # scale so the steepest slopes land near 45 degrees (M_Water sets the strength it wants)
    big = sorted(math.hypot(gx[i], gy[i]) for i in range(0, N * N, 37))
    s = 1.0 / max(big[int(len(big) * 0.995)], 1e-6)
    px = bytearray(N * N * 4)
    for i in range(N * N):
        nx, ny, nz = -gx[i] * s, -gy[i] * s, 1.0
        l = math.sqrt(nx * nx + ny * ny + nz * nz)
        o = i * 4
        px[o] = to8(0.5 + 0.5 * nx / l)
        px[o + 1] = to8(0.5 + 0.5 * ny / l)
        px[o + 2] = to8(0.5 + 0.5 * nz / l)
        px[o + 3] = 255
    return N, N, px


def paint_water_foam():
    N = 256
    oct_ = [Noise(2201 + i, 8 << i) for i in range(4)]
    px = bytearray(N * N * 4)
    for j in range(N):
        for i in range(N):
            v = fbm(oct_, i / N, j / N)
            v = clamp01((v - 0.32) * 2.8)       # blobs and gaps, soft edged
            o = (j * N + i) * 4
            px[o] = px[o + 1] = px[o + 2] = to8(v)
            px[o + 3] = 255
    return N, N, px


def water_normal():
    return cached("T_WaterN", NORMAL_VERSION, paint_water_normal)


def water_foam():
    return cached("T_WaterFoam", FOAM_VERSION, paint_water_foam)


if __name__ == "__main__":
    print(water_normal())
    print(water_foam())
