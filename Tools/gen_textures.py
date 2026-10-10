# Paints the procedural textures (moon, Milky Way, path dirt) as PNG files in Saved/.
# Pure Python (runs inside the editor's Python). Each picture is only painted again when its version number changes.
import math
import os
import random
import struct
import zlib

SAVED = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Saved")


def write_png(path, w, h, px):
    """px: bytearray of w*h*4 RGBA bytes"""
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw += px[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


class Noise:
    """Smooth value noise that wraps around every `period` cells (so textures tile)"""

    def __init__(self, seed, period):
        self.p = period
        r = random.Random(seed)
        self.g = [r.random() for _ in range(period * period)]

    def at(self, x, y):
        p = self.p
        xi = math.floor(x)
        yi = math.floor(y)
        fx = x - xi
        fy = y - yi
        x0 = int(xi) % p
        y0 = int(yi) % p
        x1 = (x0 + 1) % p
        y1 = (y0 + 1) % p
        g = self.g
        a = g[y0 * p + x0]
        b = g[y0 * p + x1]
        c = g[y1 * p + x0]
        d = g[y1 * p + x1]
        sx = fx * fx * (3 - 2 * fx)
        sy = fy * fy * (3 - 2 * fy)
        top = a + (b - a) * sx
        bot = c + (d - c) * sx
        return top + (bot - top) * sy


def fbm(octaves, u, v):
    """u, v in 0..1 (wrapping); octaves: list of Noise with growing periods"""
    total = 0.0
    amp = 0.5
    norm = 0.0
    for n in octaves:
        total += amp * n.at(u * n.p, v * n.p)
        norm += amp
        amp *= 0.5
    return total / norm


def cached(name, version, painter):
    path = os.path.join(SAVED, "%s_v%d.png" % (name, version))
    if not os.path.exists(path):
        os.makedirs(SAVED, exist_ok=True)
        w, h, px = painter()
        write_png(path, w, h, px)
    return os.path.abspath(path)


def clamp01(x):
    return 0.0 if x < 0.0 else (1.0 if x > 1.0 else x)


def to8(x):
    return int(clamp01(x) * 255.0 + 0.5)


# ------------------------------------------------------------------ the moon
def paint_moon():
    N = 384
    rnd = random.Random(1969)
    big = [Noise(11 + i, 4 << i) for i in range(4)]
    fine = [Noise(31 + i, 32 << i) for i in range(2)]
    # The dark seas (maria) roughly where they sit on the real moon, north up: (x, y, rx, ry, depth); y grows downward
    maria = [
        (-0.55, 0.02, 0.32, 0.55, 0.50),   # Oceanus Procellarum
        (-0.27, -0.36, 0.27, 0.23, 0.55),  # Imbrium
        (0.12, -0.34, 0.15, 0.14, 0.55),   # Serenitatis
        (0.26, -0.06, 0.20, 0.17, 0.55),   # Tranquillitatis
        (0.62, -0.30, 0.11, 0.09, 0.60),   # Crisium
        (0.50, 0.16, 0.12, 0.16, 0.45),    # Fecunditatis
        (0.33, 0.27, 0.08, 0.08, 0.45),    # Nectaris
        (-0.20, 0.38, 0.18, 0.13, 0.40),   # Nubium
        (-0.52, 0.42, 0.09, 0.08, 0.45),   # Humorum
        (-0.18, -0.64, 0.20, 0.06, 0.32),  # Frigoris (a long, thin chain)
        (0.12, -0.67, 0.18, 0.05, 0.30),
        (-0.02, -0.17, 0.08, 0.06, 0.40),  # Vaporum
    ]
    # Bright young craters that throw rays: (x, y, radius, ray length, number of rays)
    rayed = [(-0.10, 0.70, 0.030, 0.95, 34), (-0.30, -0.12, 0.034, 0.40, 20), (-0.60, -0.08, 0.022, 0.28, 14)]
    ray_angles = [[rnd.uniform(0, 2 * math.pi) for _ in range(k[4])] for k in rayed]

    alb = [0.0] * (N * N)
    for j in range(N):
        y = (j + 0.5) / N * 2 - 1
        for i in range(N):
            x = (i + 0.5) / N * 2 - 1
            if x * x + y * y > 1.0:
                continue
            u, v = (x + 1) * 0.5, (y + 1) * 0.5
            a = 0.70 + 0.18 * (fbm(big, u, v) - 0.5) + 0.08 * (fbm(fine, u, v) - 0.5)
            wob = 0.55 * (fbm(big, u * 1.7 + 0.37, v * 1.7 + 0.11) - 0.5) + 0.07 * (fbm(fine, u + 0.2, v + 0.6) - 0.5)
            dark = 0.0
            for (mx, my, rx, ry, dep) in maria:
                d = math.sqrt(((x - mx) / rx) ** 2 + ((y - my) / ry) ** 2) + wob
                if d < 1.35:
                    t = clamp01((1.35 - d) / 0.7)
                    dark = max(dark, dep * t * t * (3 - 2 * t))
            dark *= 0.85 + 0.3 * (fbm(fine, u + 0.5, v) - 0.5)   # seas are not evenly dark either
            a *= 1.0 - dark
            for k, (cx, cy, cr, rl, nr) in enumerate(rayed):
                dx, dy = x - cx, y - cy
                d = math.sqrt(dx * dx + dy * dy)
                if cr * 1.3 < d < rl:
                    ang = math.atan2(dy, dx)
                    s = 0.0
                    for ra in ray_angles[k]:
                        diff = abs((ang - ra + math.pi) % (2 * math.pi) - math.pi)
                        s = max(s, math.exp(-(diff * d / 0.009) ** 2))
                    a += 0.045 * s * (1 - d / rl) ** 1.5
            alb[j * N + i] = a

    # Craters: a darker floor, a bright rim, a soft dark ring outside. Many small, a few large, more in the southern highlands.
    def crater(cx, cy, r, strength):
        x0 = max(0, int((cx - r * 1.6 + 1) * 0.5 * N))
        x1 = min(N - 1, int((cx + r * 1.6 + 1) * 0.5 * N))
        y0 = max(0, int((cy - r * 1.6 + 1) * 0.5 * N))
        y1 = min(N - 1, int((cy + r * 1.6 + 1) * 0.5 * N))
        for j in range(y0, y1 + 1):
            y = (j + 0.5) / N * 2 - 1
            for i in range(x0, x1 + 1):
                x = (i + 0.5) / N * 2 - 1
                if x * x + y * y > 1.0:
                    continue
                t = math.sqrt((x - cx) ** 2 + (y - cy) ** 2) / r
                if t < 0.9:
                    alb[j * N + i] *= 1 - 0.08 * strength * (1 - t * t)       # a shallow bowl
                elif t < 1.15:
                    alb[j * N + i] *= 1 + 0.12 * strength * (1 - abs(t - 1.02) / 0.13)   # a faint bright rim
                elif t < 1.8:
                    alb[j * N + i] *= 1 + 0.04 * strength * (1.8 - t) / 0.65   # pale ejecta round it

    for _ in range(420):
        r = 0.005 + 0.035 * rnd.random() ** 4
        a = rnd.uniform(0, 2 * math.pi)
        d = math.sqrt(rnd.random()) * 0.97
        cx, cy = d * math.cos(a), d * math.sin(a)
        if cy < 0.1 and rnd.random() < 0.45:
            cy = abs(cy) + 0.1   # push some into the cratered south
        crater(cx, cy, r, rnd.uniform(0.3, 0.8))
    for (cx, cy, cr, _, _) in rayed:
        crater(cx, cy, cr, 1.4)

    px = bytearray(N * N * 4)
    for j in range(N):
        y = (j + 0.5) / N * 2 - 1
        for i in range(N):
            x = (i + 0.5) / N * 2 - 1
            r2 = x * x + y * y
            o = (j * N + i) * 4
            if r2 > 1.0:
                continue
            limb = 0.75 + 0.25 * math.sqrt(max(0.0, 1 - r2))   # a little darker toward the edge
            a = alb[j * N + i] * limb
            edge = clamp01((1.0 - math.sqrt(r2)) * N * 0.5)    # anti-aliased rim
            px[o] = to8(a * 1.00)
            px[o + 1] = to8(a * 0.97)
            px[o + 2] = to8(a * 0.92)
            px[o + 3] = to8(edge)
    return N, N, px


# ------------------------------------------------------------------ the Milky Way band (wraps left to right)
def paint_milky_way():
    W, H = 512, 128
    rnd = random.Random(1610)
    cloud = [Noise(71 + i, 4 << i) for i in range(5)]
    dust = [Noise(91 + i, 8 << i) for i in range(3)]
    lum = [0.0] * (W * H)
    warm = [0.0] * (W * H)
    for i in range(W):
        u = i / W
        core = math.exp(-((((u - 0.30 + 0.5) % 1.0) - 0.5) / 0.12) ** 2)   # the bright galactic centre region
        width = 0.16 + 0.10 * core
        for j in range(H):
            v = j / (H - 1)
            g = math.exp(-((v - 0.5) / width) ** 2)
            c = fbm(cloud, u, v * 0.25)
            b = g * (0.25 + 1.1 * c * c) * (0.6 + 0.8 * core)
            lane = math.exp(-((v - 0.5 - 0.06 * (fbm(dust, u, 0.3) - 0.5)) / 0.085) ** 2)
            b *= 1 - 0.55 * lane * fbm(dust, u + 0.5, v * 0.5) * (0.4 + core)
            lum[j * W + i] = b
            warm[j * W + i] = core * g
    # a dusting of faint stars, crowded into the band
    for _ in range(1500):
        i = rnd.randrange(W)
        j = int(H * 0.5 + rnd.gauss(0, H * 0.16))
        if 0 <= j < H:
            lum[j * W + i] += rnd.random() ** 3 * 0.5
    px = bytearray(W * H * 4)
    for k in range(W * H):
        b = lum[k] * 0.55
        w = warm[k]
        o = k * 4
        px[o] = to8(b * (0.80 + 0.25 * w))
        px[o + 1] = to8(b * (0.84 + 0.08 * w))
        px[o + 2] = to8(b * (1.00 - 0.25 * w))
        px[o + 3] = 255
    return W, H, px


# ------------------------------------------------------------------ packed dirt for the roads (tiles in both directions)
def paint_dirt():
    N = 256
    rnd = random.Random(77)
    mottle = [Noise(201 + i, 4 << i) for i in range(4)]
    grain = [Noise(301 + i, 64 << i) for i in range(2)]
    lum = [0.0] * (N * N)
    hue = [0.0] * (N * N)
    for j in range(N):
        v = j / N
        for i in range(N):
            u = i / N
            m = fbm(mottle, u, v)
            g = fbm(grain, u, v)
            lum[j * N + i] = (0.70 + 0.55 * (m - 0.5)) * (0.86 + 0.28 * g)
            hue[j * N + i] = m
    # small stones and pebbles (wrapping at the edges)
    for _ in range(240):
        cx, cy = rnd.uniform(0, N), rnd.uniform(0, N)
        r = rnd.uniform(0.8, 2.4) if rnd.random() < 0.9 else rnd.uniform(3, 4.5)
        tone = rnd.uniform(1.15, 1.55)
        for dy in range(-int(r) - 2, int(r) + 3):
            for dx in range(-int(r) - 2, int(r) + 3):
                d = math.sqrt(dx * dx + dy * dy)
                if d > r + 1:
                    continue
                k = (int(cy + dy) % N) * N + (int(cx + dx) % N)
                cover = clamp01(r + 0.5 - d)
                shade = 1.0 + 0.25 * (-(dx + dy) / (r * 1.4))     # lit from the top-left
                lum[k] = lum[k] * (1 - cover) + lum[k] * tone * shade * cover
                if r - 1 < d <= r + 1 and dx + dy > 0:               # a dark crescent on the shadow side
                    lum[k] *= 0.82
    px = bytearray(N * N * 4)
    for k in range(N * N):
        l = lum[k]
        h = hue[k]
        o = k * 4
        px[o] = to8(l * (0.40 + 0.04 * h))   # dark brown earth, slightly redder in the darker patches
        px[o + 1] = to8(l * (0.29 + 0.02 * h))
        px[o + 2] = to8(l * 0.19)
        px[o + 3] = 255
    return N, N, px


def moon():
    return cached("T_Moon", 3, paint_moon)


def milky_way():
    return cached("T_MilkyWay", 2, paint_milky_way)


def dirt():
    return cached("T_Dirt", 1, paint_dirt)
