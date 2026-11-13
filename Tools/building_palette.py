# Repaints a building's colour texture to Oak Hill's colour scheme (2026-11-11). Run in Blender:
#   blender -b -P Tools/building_palette.py -- <in.glb> <out.glb> <profile> [debug-prefix]
# <profile> names the source building below (church, well). With a debug prefix it also writes <prefix>_classes.glb,
# the model painted in class colours (stone grey, plaster white, wood blue, tile red, kept green), to check the classes
# on the model itself before judging colours.
#
# How it works:
# - Every texel is given soft class weights by its distance (in Lab colour) to anchor colours. Each profile's anchors are
#   that building's own colour clusters (k-means of its texture), each labelled by hand after looking at where it lands
#   on the model. An anchor's label may be a mix ("stone 0.5, wood 0.5") where a cluster really holds both.
# - Each class is then moved to an explicit target colour, measured from the approved Oak Hill bank (OakHillBank_300k.glb,
#   its warmed stone): lightness is scaled and the a-b colour vector rotated and scaled so the class mean lands on the
#   target. Every texel keeps its own variation (light and dark, sandy and grey blocks), only the class's colour moves.
# - "keep" anchors (lit amber windows, moss and grass, the well's ground) are left exactly as they are.
# The old way (matching class means between the two textures with HSV thresholds) failed: the bank's dark red tiles fell
# into "wood" and the gains came out wrong.
import bpy, sys, numpy as np

# Targets in CIE Lab (D65), measured from the bank's texture: weighted means of its stone, tile, wood and plaster clusters.
TARGET = {
    "stone":   (48.8, -0.2, 8.4),    # warm medium grey dressed stone (#7a7468-ish), sandy blocks mixed in by variation
    "tile":    (24.6, 22.1, 19.5),   # red-brown clay, deeper and less orange than terracotta
    "wood":    (20.3, 12.4, 15.5),   # rich deep walnut brown
    "plaster": (89.9, 0.8, 15.9),    # cream
}
# The bank's lightness quartiles (25% and 75%) of the texels that are mostly each class, from "measure" on the bank.
TARGET_LQ = {"stone": (46.0, 53.2), "tile": (21.5, 28.6), "wood": (15.2, 23.8), "plaster": (90.3, 90.7)}
# The bank's colour (Lab a, b) of each class's lit quarter (texels above its upper lightness quartile): the faces you see.
# Hue and chroma are matched on these, not on the means: the well's tiles were far more saturated on their faces than in
# their grooves, so matching the mean left the faces 40% too saturated.
TARGET_LIT = {"stone": (-0.7, 8.9), "tile": (22.8, 19.5), "wood": (11.4, 17.2), "plaster": (0.7, 15.6)}
# Per profile: anchors (Lab of the building's own colour clusters) -> class weights; "keep" means leave alone.
# The trailing comment of each anchor is that cluster's sRGB colour and what it covers.
PROFILES = {
    "church": {
        "anchors": [
            ((50.0, -4.1, 19.0), {"stone": 1}),      # #7c7756 olive grey stone
            ((57.2, -4.1, 18.7), {"stone": 1}),      # #8f8a68 lighter stone
            ((41.9, -2.0, 20.0), {"stone": 1}),      # #6b6241 dark olive stone / spire slate
            ((35.9, 11.9, 23.7), {"tile": 1}),       # #704c2e tiles
            ((32.8, 1.1, 19.3), {"stone": 1}),       # #594b2e dark stone shading
            ((23.9, 2.3, 15.1), {"wood": 1}),        # #433622 timber
            ((46.4, -1.5, 10.8), {"stone": 1}),      # #726e5b grey stone
            ((43.2, 12.5, 28.6), {"tile": 1}),       # #865d36 lit tiles
            ((36.7, 1.0, 11.3), {"stone": 1}),       # #5e5544 grey-brown stone
            ((25.7, 11.1, 19.7), {"tile": 1}),       # #54361f dark tiles
            ((56.7, -2.1, 7.9), {"stone": 1}),       # #8a887a pale grey stone
            ((12.2, 5.4, 10.0), {"wood": 1}),        # #2b1c11 darkest gaps
            ((71.2, -1.9, 20.8), {"plaster": 1}),    # #b9ae88 plaster panels
            ((42.9, -11.2, 31.4), {"keep": 1}),      # #63692e moss
            ((64.3, 4.5, 60.2), {"keep": 1}),        # #bd9623 lit amber windows
        ],
    },
    "bank": {   # the reference: run with "measure" to print the targets above
        "anchors": [
            ((51.4, -0.6, 8.8), {"stone": 1}),       # #7f796b
            ((21.9, 23.9, 20.6), {"tile": 1}),       # #592316
            ((44.3, 0.7, 8.3), {"stone": 1}),        # #6f675b
            ((19.6, 13.2, 19.4), {"wood": 1}),       # #472713
            ((25.5, 22.4, 19.9), {"tile": 1}),       # #612d1f
            ((30.5, 23.8, 20.4), {"tile": 1}),       # #703729
            ((57.8, -0.9, 10.0), {"stone": 1}),      # #908a79
            ((13.4, 11.3, 11.7), {"wood": 1}),       # #351b11
            ((28.2, 0.5, 4.8), {"stone": 1}),        # #46423b
            ((25.7, 11.8, 16.1), {"wood": 1}),       # #543524
            ((18.0, 20.1, 16.6), {"tile": 1}),       # #4a1e14
            ((11.5, 2.1, 0.9), {"keep": 1}),         # #211d1c window glass and iron
            ((36.3, 22.0, 19.1), {"tile": 1}),       # #7d4637
            ((35.1, 10.5, 16.2), {"wood": 1}),       # #6a4b39
            ((89.9, 0.7, 16.1), {"plaster": 1}),     # #efe0c3
        ],
    },
    # The four Meshy-made town buildings (2026-11-11): tiles, wood and plaster are already near the bank's; their stone is a
    # cold blue-grey, moved by "shift". Lit windows and the forge's glow are "keep" anchors sampled from their strongest colours.
    "potion": {
        "shift": ("stone",),
        "anchors": [
            ((27.8, 18.7, 19.5), {"tile": 1}),       # #633524
            ((21.2, 12.8, 16.8), {"wood": 1}),       # #4a2a1a
            ((48.7, -0.4, 0.8), {"stone": 1}),       # #737372 cold grey stone
            ((14.5, 13.5, 13.1), {"tile": 0.5, "wood": 0.5}),  # #3a1c12 dark gaps
            ((54.0, -0.3, 3.3), {"stone": 1}),       # #83807b
            ((83.3, 0.6, 14.6), {"plaster": 1}),     # #dcceb4
            ((31.0, 10.5, 15.1), {"wood": 1}),       # #5f4131
            ((7.4, 8.3, 5.5), {"wood": 1}),          # #22100d
            ((59.7, -0.8, 2.6), {"stone": 1}),       # #908f8a
            ((40.5, 14.8, 17.0), {"tile": 0.5, "wood": 0.5}),  # #7e5543 lit tile or wood
            ((41.3, 0.5, 2.8), {"stone": 1}),        # #63605c
            ((31.0, 0.1, 2.1), {"stone": 1}),        # #4a4845
            ((19.6, 2.6, 4.1), {"stone": 1}),        # #352d29 mortar
            ((66.7, 2.4, 14.1), {"stone": 0.5, "keep": 0.5}),  # #b09f89 sandy blocks
            ((92.1, -2.0, 27.2), {"keep": 1}),       # lit windows
            ((83.4, 0.0, 26.2), {"keep": 1}),
            ((74.9, 0.9, 26.1), {"keep": 1}),
        ],
    },
    "townhall": {
        "shift": ("stone",),
        "anchors": [
            ((28.6, 19.0, 20.4), {"tile": 1}),       # #653724
            ((23.6, 20.1, 21.6), {"tile": 1}),       # #5a2a18
            ((21.6, 10.7, 18.0), {"wood": 1}),       # #492d19
            ((53.0, 0.2, 4.0), {"stone": 1}),        # #817e77
            ((46.1, 0.4, 1.5), {"stone": 1}),        # #6e6c6a
            ((17.4, 19.3, 18.1), {"tile": 1}),       # #481d10
            ((23.6, 17.1, 17.3), {"tile": 1}),       # #552d1f
            ((30.5, 9.1, 14.8), {"wood": 1}),        # #5c4231
            ((12.9, 15.4, 13.0), {"tile": 0.5, "wood": 0.5}),  # #38170e
            ((80.5, 1.1, 14.1), {"plaster": 1}),     # #d4c6ad
            ((18.7, 8.5, 11.7), {"wood": 1}),        # #3e281c
            ((8.0, 6.8, 5.5), {"wood": 1}),          # #22130e
            ((38.5, 15.3, 18.1), {"tile": 0.5, "wood": 0.5}),  # #7a503e
            ((32.8, 1.1, 3.5), {"stone": 1}),        # #514c47
            ((91.9, -5.3, 38.4), {"keep": 1}),       # lit windows
            ((79.0, 2.1, 31.5), {"keep": 1}),
            ((71.6, 7.0, 37.5), {"keep": 1}),
            ((84.1, 2.1, 40.3), {"keep": 1}),
            ((56.9, 10.7, 35.9), {"keep": 1}),
        ],
    },
    "smith": {
        "shift": ("stone",),
        "anchors": [
            ((47.3, -0.4, 1.8), {"stone": 1}),       # #70706d
            ((22.6, 10.0, 15.7), {"wood": 1}),       # #4a2f1f
            ((27.2, 17.2, 18.9), {"tile": 1}),       # #5f3524
            ((22.6, 17.6, 19.4), {"tile": 1}),       # #542a19
            ((17.2, 9.8, 14.8), {"wood": 1}),        # #3d2415
            ((53.7, -0.5, 1.7), {"stone": 1}),       # #80807d
            ((29.9, 8.1, 13.7), {"wood": 1}),        # #594131
            ((81.7, 0.9, 16.0), {"plaster": 1}),     # #d8c9ac
            ((38.8, 5.9, 11.8), {"wood": 0.5, "stone": 0.5}),  # #6b5748
            ((14.9, 16.8, 14.7), {"tile": 0.5, "wood": 0.5}),  # #3f1a10
            ((36.6, 15.9, 17.2), {"tile": 0.5, "wood": 0.5}),  # #764c3b
            ((31.9, 0.1, 1.7), {"stone": 1}),        # #4c4a48
            ((10.2, 7.5, 6.4), {"wood": 1}),         # #281711
            ((62.9, 3.7, 16.5), {"stone": 0.5, "keep": 0.5}),  # #a8947a sandy blocks
            ((55.4, 38.4, 49.0), {"keep": 1}),       # forge glow
            ((53.3, 44.0, 51.6), {"keep": 1}),
            ((65.7, 17.1, 41.3), {"keep": 1}),
            ((32.3, 27.4, 30.3), {"keep": 1}),       # glow's dark rim
            ((90.9, -4.4, 34.6), {"keep": 1}),       # lit windows
            ((86.6, -0.6, 41.6), {"keep": 1}),
            ((76.9, 3.4, 32.3), {"keep": 1}),
        ],
    },
    "corner": {
        "shift": ("stone",),
        "anchors": [
            ((26.9, 22.3, 22.8), {"tile": 1}),       # #65301d
            ((20.0, 9.5, 17.3), {"wood": 1}),        # #442a17
            ((23.6, 7.8, 14.9), {"wood": 1}),        # #4a3322
            ((24.8, 18.8, 18.1), {"tile": 1}),       # #5b2e20
            ((84.1, 1.4, 18.2), {"plaster": 1}),     # #e1cfaf
            ((20.5, 22.1, 19.8), {"tile": 1}),       # #542215
            ((50.3, -0.5, 3.0), {"stone": 1}),       # #797772
            ((43.0, 0.1, 3.3), {"stone": 1}),        # #686560
            ((15.1, 8.0, 11.4), {"wood": 1}),        # #352115
            ((14.9, 19.3, 14.4), {"tile": 1}),       # #411810
            ((31.6, 7.4, 15.0), {"wood": 1}),        # #5d4533
            ((33.6, 19.8, 19.1), {"tile": 1}),       # #734131
            ((7.0, 6.5, 5.0), {"wood": 1}),          # #20100c
            ((29.5, 0.2, 3.2), {"stone": 1}),        # #474440
            ((61.6, 4.9, 19.0), {"stone": 0.5, "keep": 0.5}),  # #a99073 sandy blocks
            ((79.9, 3.5, 32.5), {"keep": 1}),        # lit windows
            ((60.3, 8.9, 34.8), {"keep": 1}),
            ((87.2, 1.4, 42.5), {"keep": 1}),
        ],
    },
    "store": {   # Timbered Market House, from Meshy's library: dark grey-brown wood, an awning, barrels and crates
        "anchors": [
            ((20.4, 2.8, 12.3), {"wood": 1}),        # #3b2f1f
            ((27.7, 2.9, 11.7), {"wood": 1}),        # #4c3f2f
            ((33.6, 1.4, 10.4), {"stone": 1}),       # #574d3e stone of the ground floor walls
            ((19.1, 13.8, 17.3), {"tile": 1}),       # #462515
            ((24.1, 3.8, 16.0), {"wood": 1}),        # #463621
            ((26.7, 16.0, 20.4), {"tile": 1}),       # #5d3420
            ((41.9, -0.8, 9.2), {"stone": 1}),       # #676253
            ((30.7, 3.8, 16.8), {"wood": 1}),        # #56452e
            ((13.8, 3.2, 9.9), {"wood": 1}),         # #2c2115
            ((45.0, -13.7, 29.5), {"keep": 1}),      # #646f37 plants
            ((54.4, 1.1, 15.8), {"keep": 1}),        # #8e8067 awning canvas?
            ((22.4, 0.9, 6.9), {"stone": 1}),        # #3a342b stone shading and mortar
            ((40.9, 1.3, 18.8), {"wood": 1}),        # #6d5e41
            ((63.9, 0.6, 12.2), {"stone": 1}),       # #a49985
            ((46.1, 19.0, 50.5), {"keep": 1}),       # lit windows
            ((64.5, 17.2, 65.8), {"keep": 1}),
            ((33.7, 16.6, 32.2), {"keep": 1}),
        ],
    },
    "bankstall": {   # (tile: shift, its slate is near grey) Bank_A (owner's pick, 2026-11-12): blue-grey slate roof to the town's clay tile, its grey stone warmed; brass, gold and the vault kept
        "shift": ("tile",),
        "anchors": [
            ((20.3, -2.6, -8.8), {"tile": 1}),       # blue slate roof
            ((13.7, -2.1, -8.7), {"tile": 1}),       # dark slate, roof shading
            ((20.0, -1.0, -2.1), {"tile": 0.5, "stone": 0.5}),   # neutral grey: roof edges and stone shade
            ((20.2, 2.3, 6.0), {"stone": 1}),        # grey stone
            ((29.2, 2.5, 7.4), {"stone": 1}),
            ((24.0, 5.2, 12.5), {"stone": 1}),
            ((33.2, 5.7, 16.6), {"stone": 1}),       # beige stone
            ((48.1, 5.4, 17.8), {"stone": 1}),       # pale stone
            ((59.1, 4.6, 18.1), {"plaster": 1}),     # cream
            ((12.2, 8.4, 11.6), {"wood": 1}),        # pillars and counter
            ((17.4, 6.7, 12.0), {"wood": 1}),
            ((18.6, 10.5, 18.5), {"wood": 1}),
            ((36.8, 8.1, 26.3), {"keep": 1}),        # brass
            ((46.1, 7.2, 32.8), {"keep": 1}),
            ((57.0, 6.5, 44.7), {"keep": 1}),        # gold
            ((69.2, 5.4, 50.7), {"keep": 1}),
        ],
    },
    "well": {
        "anchors": [
            ((65.8, -0.5, 9.4), {"stone": 1}),       # #a69f8f pale stone
            ((29.1, 3.7, 11.2), {"wood": 1}),        # #504233 wood
            ((35.9, 2.9, 12.8), {"wood": 1}),        # #605240 wood
            ((43.7, 1.5, 14.9), {"wood": 1}),        # #72654e light wood
            ((21.6, 4.6, 10.0), {"wood": 1}),        # #3f3125 dark wood
            ((32.7, 16.2, 18.5), {"tile": 1}),       # #6c4230 tile shading
            ((63.6, 29.8, 35.4), {"tile": 1}),       # #db835c lit terracotta
            ((54.3, -0.2, 10.8), {"stone": 1}),      # #88816f stone shading
            ((22.7, 13.4, 15.2), {"tile": 1}),       # #4e2e20 dark tile gaps
            ((53.4, 26.0, 29.3), {"tile": 1}),       # #b56c4e terracotta
            ((41.5, 21.9, 24.4), {"tile": 1}),       # #8d533b terracotta shade
            ((47.5, -4.9, 21.1), {"keep": 1}),       # #75724d grass and ground
            ((74.4, 0.1, 11.2), {"stone": 1}),       # #bfb6a2 brightest stone
            ((11.8, 6.8, 9.2), {"wood": 0.5, "tile": 0.5}),  # #2b1a11 darkest gaps
            ((59.8, -1.5, 18.7), {"keep": 1}),       # #9a8f6f ground
        ],
    },
}
SIGMA = 5.0          # softness of the class edges, in Lab units
L_WEIGHT = 0.5       # lightness counts half as much as colour when finding a texel's class (shading is not material)
CLASS_COL = {"stone": (0.5, 0.5, 0.5), "plaster": (1, 1, 1), "wood": (0.1, 0.3, 1), "tile": (1, 0.1, 0.1), "keep": (0.1, 0.9, 0.1)}

M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]], dtype=np.float32)
WHITE = np.array([0.9505, 1.0, 1.089], dtype=np.float32)

def to_lab(rgb):
    c = np.where(rgb <= 0.04045, rgb / 12.92, ((np.maximum(rgb, 0) + 0.055) / 1.055) ** 2.4)
    xyz = c @ M.T / WHITE
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[:, 1] - 16, 500 * (f[:, 0] - f[:, 1]), 200 * (f[:, 1] - f[:, 2])], 1)

def to_rgb(lab):
    fy = (lab[:, 0] + 16) / 116; fx = fy + lab[:, 1] / 500; fz = fy - lab[:, 2] / 200
    f = np.stack([fx, fy, fz], 1)
    xyz = np.where(f > 0.206893, f ** 3, (f - 16 / 116) / 7.787) * WHITE
    c = xyz @ np.linalg.inv(M).T
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)

def weights(lab, anchors):
    # soft class weights per texel: a softmax over distances to the anchors, then summed per class
    A = np.array([a for a, _ in anchors], dtype=np.float32)
    scale = np.array([L_WEIGHT, 1, 1], dtype=np.float32)
    d2 = (((lab[:, None, :] - A[None]) * scale) ** 2).sum(2)
    e = np.exp(-(d2 - d2.min(1, keepdims=True)) / (2 * SIGMA * SIGMA)); e /= e.sum(1, keepdims=True)
    W = {k: np.zeros(len(lab), dtype=np.float32) for k in CLASS_COL}
    for i, (_, lab_mix) in enumerate(anchors):
        for k, v in lab_mix.items(): W[k] += e[:, i] * v
    return W

def base_image():
    o = next(x for x in bpy.data.objects if x.type == "MESH")
    pr = next(n for n in o.data.materials[0].node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    return pr.inputs["Base Color"].links[0].from_node.image

if __name__ == "__main__":
    a = sys.argv[sys.argv.index("--") + 1:]
    src, out, prof = a[0], a[1], PROFILES[a[2]]
    debug = a[3] if len(a) > 3 else None
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)
    img = base_image()
    px = np.empty(len(img.pixels), dtype=np.float32); img.pixels.foreach_get(px); px = px.reshape(-1, 4)
    N = len(px); CH = 1 << 20
    # pass 1: class weights and each class's mean Lab in this building
    lab = np.empty((N, 3), dtype=np.float32); W = {k: np.empty(N, dtype=np.float32) for k in CLASS_COL}
    for i in range(0, N, CH):
        lab[i:i + CH] = to_lab(px[i:i + CH, :3])
        w = weights(lab[i:i + CH], prof["anchors"])
        for k in W: W[k][i:i + CH] = w[k]
    def lq(k):   # lightness quartiles of the texels that are mostly this class
        L = lab[W[k] > 0.5, 0]
        return np.percentile(L, [25, 75]) if len(L) > 100 else None
    if out == "measure":
        for k in TARGET:
            s = W[k].sum(); m = (lab * W[k][:, None]).sum(0) / max(s, 1); q = lq(k)
            lit = (W[k] > 0.5) & (lab[:, 0] >= (q[1] if q is not None else 0))
            lm = lab[lit].mean(0) if lit.any() else np.zeros(3)
            print("MEASURE %-7s share %.3f mean Lab %s  L quartiles %s  lit quarter Lab %s (chroma %.1f, hue %.0f)" % (
                k, s / N, np.round(m, 1), None if q is None else np.round(q, 1), np.round(lm, 1), np.hypot(lm[1], lm[2]), np.degrees(np.arctan2(lm[2], lm[1]))))
        sys.exit(0)
    xf = {}
    for k, t in TARGET.items():
        s = W[k].sum()
        if s < N * 0.002: print("PALETTE", k, "too little to move (%.4f)" % (s / N)); continue
        m = (lab * W[k][:, None]).sum(0) / s; t = np.array(t, dtype=np.float32)
        q = lq(k); lit = (W[k] > 0.5) & (lab[:, 0] >= q[1]) if q is not None else None
        if lit is not None and lit.sum() > 100:
            ma = lab[lit, 1:].mean(0); ta = np.array(TARGET_LIT[k], dtype=np.float32)
        else:
            ma = m[1:]; ta = t[1:]
        if k in prof.get("shift", ()):
            # near-grey source (a cold blue-grey stone): scaling its tiny colour up would blow its few sandy blocks up into
            # orange, so the colour is moved by adding the difference instead; every block keeps its own tint on top
            ang, sc = 0.0, 1.0; d = ta - ma
        else:
            ang = np.arctan2(ta[1], ta[0]) - np.arctan2(ma[1], ma[0])
            sc = np.hypot(ta[0], ta[1]) / max(np.hypot(ma[0], ma[1]), 1e-3); d = np.zeros(2, dtype=np.float32)
        # lightness: a straight line taking the upper quartile of this class (the lit faces) onto the bank's, with the spread moved only
        # halfway (square root of the ratio) toward the bank's, so the faces that catch the eye match but the model keeps its
        # own painted variation. Matching only the mean left the well's tiles light: they have much deeper painted grooves.
        tq = TARGET_LQ[k]
        if q is not None and q[1] - q[0] > 1:
            gain = float(np.clip(np.sqrt((tq[1] - tq[0]) / (q[1] - q[0])), 0.45, 1.3))
            off = float(tq[1] - gain * q[1])
        else:
            gain, off = t[0] / max(m[0], 1e-3), 0.0
        xf[k] = (gain, off, sc * np.cos(ang), sc * np.sin(ang), float(d[0]), float(d[1]))
        print("PALETTE %-7s share %.3f  Lab %s -> %s  L quartiles %s -> %s (L' = %.2f L %+.1f)  lit a,b %s -> %s: chroma x%.2f  hue %+.1f deg  shift %s" % (
            k, s / N, np.round(m, 1), np.round(t, 1), None if q is None else np.round(q, 1), tq, gain, off,
            np.round(ma, 1), np.round(ta, 1), sc, np.degrees(ang), np.round(d, 1)))
    print("PALETTE keep    share %.3f" % (W["keep"].sum() / N))
    # pass 2: move each class, blended by its weight; kept texels and the alpha channel are untouched
    for i in range(0, N, CH):
        l = lab[i:i + CH]; o = l.copy() * 0
        wsum = np.zeros(len(l), dtype=np.float32)
        for k, (lg, lo, c, s, da, db) in xf.items():
            w = W[k][i:i + CH]
            moved = np.stack([np.clip(l[:, 0] * lg + lo, 0, 100), c * l[:, 1] - s * l[:, 2] + da, s * l[:, 1] + c * l[:, 2] + db], 1)
            o += moved * w[:, None]; wsum += w
        o += l * (1 - wsum)[:, None]
        px[i:i + CH, :3] = to_rgb(o)
    if debug:
        dbg = np.zeros_like(px); dbg[:, 3] = 1
        for k, col in CLASS_COL.items(): dbg[:, :3] += W[k][:, None] * np.array(col, dtype=np.float32)
        orig = np.empty_like(px); img.pixels.foreach_get(orig.reshape(-1))
        img.pixels.foreach_set(dbg.reshape(-1)); img.update()
        bpy.ops.export_scene.gltf(filepath=debug + "_classes.glb", export_format="GLB", export_image_format="JPEG", export_jpeg_quality=92)
    img.pixels.foreach_set(px.reshape(-1)); img.update()
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_image_format="JPEG", export_jpeg_quality=92)
    print("PALETTE wrote", out)
