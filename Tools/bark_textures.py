# Paints the tree bark pictures (2026-11-13, owner: "find a tree bark skin ... and apply it to the trees") from CC0 scans.
#
# Sources (Poly Haven, CC0, no credit needed; 1k JPG, the colour, height and OpenGL normal maps of each):
#   bark_brown_02  furrowed broadleaf bark (Rob Tuytel)   -> Art/Bark/T_BarkFurrow.png, T_BarkFurrowN.png  (oaks, birches, pines)
#   pine_bark      plated conifer bark                    -> Art/Bark/T_BarkPlate.png,  T_BarkPlateN.png   (redwoods)
#   https://polyhaven.com/a/bark_brown_02, https://polyhaven.com/a/pine_bark
#
# The look (CLAUDE.md art direction: painted, rich, natural, not grimy photo-real): the scans' own colours (moss, lichen,
# grime) are thrown away. The shape of the bark (its height and the scan's lightness, with everything bigger than a few centimetres
# taken out so no blotch or knot repeats up the trunk) is smoothed, then painted as plain values: plates light, fissures
# dark, a soft curve between, grey so M_BarkTint's per-tree Tint colours it. The normal map is softened the same way and
# flattened a little. Everything wraps (the scans tile and every blur here is circular).
#
# Run with Blender's Python (it has numpy) and ffmpeg on PATH:
#   "C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe" -I Tools\bark_textures.py <download folder> Art\Bark
import os
import subprocess
import sys

import numpy as np

N = 1024    # the scans' size
OUT = 512   # the pictures' size: trunks are a few metres off at the closest, 512 is sharp enough and a quarter of the memory


def read(path, gray):
    fmt = "gray" if gray else "rgb24"
    raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", path, "-vf", "scale=%d:%d" % (N, N), "-f", "rawvideo", "-pix_fmt", fmt, "-"],
                         check=True, capture_output=True).stdout
    a = np.frombuffer(raw, np.uint8).astype(np.float64) / 255.0
    return a.reshape(N, N) if gray else a.reshape(N, N, 3)


def write(path, rgb):
    h, w = rgb.shape[:2]
    data = (np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8).tobytes()
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (w, h), "-i", "-",
                    "-vf", "scale=%d:%d:flags=lanczos" % (OUT, OUT), path], input=data, check=True)


def blur(a, sigma):
    """Gaussian blur that wraps round (the pictures tile)"""
    f = np.fft.fftfreq(a.shape[0])
    g = np.exp(-2.0 * (np.pi * sigma) ** 2 * (f[:, None] ** 2 + f[None, :] ** 2))
    return np.real(np.fft.ifft2(np.fft.fft2(a) * g))


def detail(a, big):
    """what is smaller than `big` pixels, in standard deviations"""
    d = a - blur(a, big)
    return d / (d.std() + 1e-6)


def paint(src, name, out_dir, height_share, smooth, dark, light, flatten):
    col = read(os.path.join(src, name + "_diff_1k.jpg"), False)
    hgt = read(os.path.join(src, name + "_disp_1k.jpg"), True)
    nor = read(os.path.join(src, name + "_nor_gl_1k.jpg"), False)
    lum = col @ np.array([0.3, 0.59, 0.11])
    # the bark's shape: mostly its height (the scan's lightness carries moss and lichen stains), big shapes taken out
    d = height_share * detail(hgt, 40.0) + (1.0 - height_share) * detail(lum, 40.0)
    d = blur(d, smooth)                       # no photo grit: soft painted strokes
    d = d / (d.std() + 1e-6)
    # painted values: plates a little lighter than the middle, fissures much darker, a soft curve between
    v = np.where(d > 0.0, 0.5 + light * np.tanh(d * 0.55), 0.5 + dark * np.tanh(d * 0.7))
    v = v * (0.5 / v.mean())                  # averages mid grey, as the old picture, so every tree keeps its colour
    rgb = np.stack([v * 1.02, v, v * 0.96], axis=-1)
    write(os.path.join(out_dir, out_name(name) + ".png"), rgb)
    # the normals: softened like the colour, flattened a little (painted, not deeply carved)
    n = nor * 2.0 - 1.0
    nx, ny = [blur(c - blur(c, 40.0), smooth * 0.8) * flatten for c in (n[..., 0], n[..., 1])]   # (broad bulges such as knots taken out too)
    nz = np.sqrt(np.clip(1.0 - nx * nx - ny * ny, 0.0, 1.0))
    ny = -ny   # Unreal reads DirectX normal maps: green flipped from the OpenGL scan (the trunks' V runs up, their binormal with it)
    write(os.path.join(out_dir, out_name(name) + "N.png"), np.stack([nx, ny, nz], axis=-1) * 0.5 + 0.5)
    print("BARK", name, "->", out_name(name), "value range %.2f-%.2f mean %.2f" % (np.percentile(v, 2), np.percentile(v, 98), v.mean()))


def out_name(name):
    return {"bark_brown_02": "T_BarkFurrow", "pine_bark": "T_BarkPlate"}[name]


if __name__ == "__main__":
    src, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    paint(src, "bark_brown_02", out_dir, height_share=0.75, smooth=2.6, dark=0.34, light=0.13, flatten=0.8)
    paint(src, "pine_bark", out_dir, height_share=0.7, smooth=1.8, dark=0.30, light=0.12, flatten=0.8)
