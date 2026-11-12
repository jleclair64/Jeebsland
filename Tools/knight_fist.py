# The knight's fists, as shape keys (owner 2026-11-11: weapons floated by open fingers, then the first fists stayed open on one side). The rig has no finger
# bones, so each hand gets two shapes in the mesh, made in the bind pose: a relaxed hand (the basis) and a fist round the grip line (shape keys FistR and FistL,
# Unreal morph targets of the same names, set per hand by FKnightAnimator: closed round a weapon, a shield's strap or the bow, relaxed when the hand is empty).
# Used by knight_bare_export.py (the chain mail hand) and knight_suit_export.py (the gauntlets), so both close round the same line, which the game holds every
# weapon on (FClipLib::SolveGear puts the grip there: keep GRIP_ALONG / GRIP_PALM in step with FistAlong / FistPalm in KnightAnimator.cpp).
#
# The frame, per hand (bind pose, metres): H the hand bone's head, A along the hand (forearm to hand), D the way a held blade leaves the fist (the game's
# BladeDir carried onto this arm exactly as SolveGear does), N toward the palm (-A x D on the right, A x D on the left). The grip line K runs along D through
# H + A * (0.07 + GRIP_ALONG) + N * GRIP_PALM. The bend works in the plane square to the line: a along the fingers' way (Af), n toward the palm; z along the line.
#
# How a finger closes: each point is found on the hand's bend line as it is in the open hand (an arc, "bind"), by how far along it lies and how far to its palm
# side; then put at the same place on the fist's line: the knuckle turns (radius r1), the finger runs straight to the grip line and round it at radius Rm (its
# middle, just outside the handle's surface, so the fingertips lie on the grip rather than being cut by it), and past s_tip the tip folds in at radius r3. Length is kept, so nothing
# stretches (the first fists turned by s / r at the radius the point already had, and the back of the fingers stretched about 2.3 times and tips crossed the line).
# The palm side of a tight bend is squeezed (tanh), never pushed through the joint's own centre. Past the web each bare finger bends along its own middle line
# (they lie at different heights in the open hand) and moves beside the next, so they lie side by side as in a fist. Everything is a function of position only, so
# points the model split along its texture seams stay together.
import bmesh, math
from mathutils import Vector, Quaternion

GRIP_ALONG = 0.055   # (m) the handle's middle, along the hand from the old grip point (H + A * 0.07): at the base of the fingers, under the knuckles (was 0.030: the fingers stopped above the handle)
GRIP_PALM = 0.060    # (m) and toward the palm from it

D2R = math.radians
# The settings of each hand, measured on the meshes (m and radians) from the grip line as it first was (GRIP_ALONG 0.030), so a new grip line moves the fist, not the hand.
SETTINGS = {
    "bare": dict(
        bind=(0.030, -0.033, D2R(18), 0.075),         # the open hand's bend line: from (a, n) heading this way from Af toward N, curving toward the palm at this radius
        fist=dict(r1=0.008, Rm=0.029, s_tip=0.09, r3=0.009),   # (Rm was 0.023: the grips are 2.2 to 2.6 cm across their radius, WeaponDesign.h, and cut through the fingertips, which read as torn)
        side=(0.022, 1.45, 0.022, 0.05),              # fingers side by side, touching: spacing across the hand, widened by, from the web (s), fully by (s)
        relaxed=[(0.008, 0.022, D2R(15)), (0.040, 0.020, D2R(24)), (0.062, 0.016, D2R(15))],   # the basis: a soft half curl, joints (s, width, turn)
        side_relaxed=(0.021, 1.3, 0.022, 0.05),       # (the relaxed fingers lie together too, not fanned)
        # The thumb is a low cone (a ring of points at its root, five at its tip): it turns whole about its root, eased in over its first 30%. Its root sits about
        # 8 cm from the handle, too far for it to reach the fingers by turning alone, so it also moves up by shift (the ball of the thumb stretches a little).
        thumb=dict(piv=(-0.052, -0.005, 0.042), tip=(-0.017, 0.041, 0.078), want=(0.06, 0.035, -0.06), share=1.0, fold_at=0.55, fold_deg=30, fold_to=(0.0, -0.5, -1.0), cyl=(0.03, (0.0, 0.3)), shift=(0.027, 0.016, -0.006)),   # across the fingertips where they come round the handle, over the index and middle fingers
        tidy=dict(s_from=0.02, s_to=0.04, iters=8, r0=0.038, give=0.2),   # the closed fingers smoothed (else the tips came out as torn spikes) and nothing standing past 3.8 cm from the handle's line
        fill=(0.02, -0.045, 0.0, -0.012),             # the palm under the handle rises up to 2 cm to meet the fingertips (light came through the left hand along the handle): push, from, to (a from the handle), only points past n
        thumb_relaxed=dict(piv=(-0.052, -0.005, 0.042), tip=(-0.017, 0.041, 0.078), want=(0.85, 0.35, -0.2), share=0.5, cyl=(0.03, (0.0, 0.3))),   # along the index finger
    ),
    # The gauntlet is already a half closed fist (its own shape is the basis): its fist bends the wrist toward the palm and moves the hand onto the line (flex:
    # degrees, along, toward the palm), then the fingers close round it from their own hook (centre about 3.4 cm out). Its fingers are one piece, so no side by side.
    "suit": dict(
        flex=(20.0, -0.008, 0.028),
        bind=(0.049, -0.019, D2R(45), 0.034),
        fist=dict(r1=0.008, Rm=0.028, s_tip=1.0, r3=0.010),
        side=None,
        relaxed=None,
        thumb=dict(piv=(-0.090, -0.040, 0.050), tip=(-0.046, -0.005, 0.097), want=(0.12, 0.07, -0.02), cyl=(0.032, (0.1, 0.5))),
        thumb_relaxed=None,
    ),
}

def g2b(v):
    """The game's frame (X forward, Y right, Z up, cm) to Blender's (the knight looks along -Y)."""
    return Vector((-v.y, -v.x, v.z))

def frame(arm_obj, side):
    """side 'Right' or 'Left'. Returns H, A, D, N (toward the palm) and the grip line's point K, all in world space."""
    sd = 1 if side == "Right" else -1
    b = arm_obj.data.bones
    H = arm_obj.matrix_world @ b["mixamorig:" + side + "Hand"].head_local
    F = arm_obj.matrix_world @ b["mixamorig:" + side + "ForeArm"].head_local
    A = (H - F).normalized()
    new_f = Vector((-A.y, -A.x, A.z))   # (Blender to the game's frame: the same swap)
    old_f = (Vector((-2, 37 * sd, -29)) - Vector((-5, 34.5 * sd, -6))).normalized()   # KnightDesign::WristPos - P_Elbow
    blade = old_f.rotation_difference(new_f) @ Vector((0.97, -sd * 0.06, -0.21)).normalized()   # KnightDesign::BladeDir
    D = g2b(blade).normalized()
    N = A.cross(D).normalized() * -sd   # (toward the palm: his palms face his thighs, so the two sides mirror)
    C = H + A * 0.07
    K = C + A * GRIP_ALONG + N * GRIP_PALM
    return H, A, D, N, K

def sstep(x, a, b):
    if b == a: return 1.0 if x >= a else 0.0
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)

class Path:
    """A line in the bend's plane from (a0, n0) heading h0 (radians from +a toward +n), turning by curvature kf(s), walked in small steps."""
    def __init__(self, a0, n0, h0, kf, length=0.25, step=0.0005):
        self.step = step
        self.pts = []; self.hd = []; self.k = []
        x, y, h = a0, n0, h0
        s = 0.0
        while s <= length + step:
            k = kf(s)
            self.pts.append((x, y)); self.hd.append(h); self.k.append(k)
            h_mid = h + 0.5 * k * step
            x += math.cos(h_mid) * step; y += math.sin(h_mid) * step
            h += k * step; s += step
    def at(self, s):
        """(a, n, heading, curvature) at s along it."""
        i = min(len(self.pts) - 2, max(0, int(s / self.step))); f = s / self.step - i
        f = min(1.0, max(0.0, f)) if s >= 0 else 0.0
        x = self.pts[i][0] + (self.pts[i + 1][0] - self.pts[i][0]) * f
        y = self.pts[i][1] + (self.pts[i + 1][1] - self.pts[i][1]) * f
        h = self.hd[i] + (self.hd[i + 1] - self.hd[i]) * f
        k = self.k[i] + (self.k[i + 1] - self.k[i]) * f
        return x, y, h, k

def joints_path(bind, joints):
    """A hand's line bent at joints: (s at the joint's middle, its width, its turn), each a raised cosine (its integral over the width is the turn)."""
    a0, n0, h0, R0 = bind
    def kf(s):
        k = 0.0
        for sj, wj, tj in joints:
            u = (s - sj) / wj
            if -1.0 < u < 1.0: k += tj / wj * 0.5 * (1 + math.cos(math.pi * u))
        return k
    return Path(a0, n0, h0, kf)

def wrap_path(bind, r1, Rm, s_tip, r3, ka=0.0, kn=0.0, log=print):
    """The fingers' line in a fist: a knuckle of radius r1 from the bind start, straight on to the grip line (at ka, kn), then round it at radius Rm; past s_tip the tip folds in at radius r3."""
    a0, n0, h0, R0 = bind
    cx, cy = a0 - math.sin(h0) * r1, n0 + math.cos(h0) * r1
    vx, vy = ka - cx, kn - cy              # (to K, the grip line)
    d = math.hypot(vx, vy); phi = math.atan2(vy, vx)
    th = phi - math.asin(max(-1.0, min(1.0, (Rm - r1) / d)))
    al = (th - h0) % (2 * math.pi)
    L = vx * math.cos(th) + vy * math.sin(th)
    s1 = r1 * al; s2 = s1 + max(0.0, L)
    log("FIST wrap: knuckle turns %.0f deg over %.1f cm, then %.1f cm straight, then round the line" % (math.degrees(al), s1 * 100, L * 100))
    def kf(s):
        if s >= s_tip: return 1.0 / r3
        if s < s1: return 1.0 / r1
        if s < s2: return 0.0
        return 1.0 / Rm
    return Path(a0, n0, h0, kf)

def bind_s(a, n, bind):
    """How far along the open hand's bend line a point lies (negative: before its start, the palm and the back of the hand, which stay put)."""
    a0, n0, h0, R0 = bind
    ox, oy = a0 - math.sin(h0) * R0, n0 + math.cos(h0) * R0
    ang = math.atan2(n - oy, a - ox) - (h0 - math.pi / 2)
    while ang > math.pi: ang -= 2 * math.pi
    while ang < -math.pi: ang += 2 * math.pi
    return ang * R0

def bend_point(a, n, P, bind):
    """bind: (a0, n0, h0, R0) the open hand's line: from (a0, n0) heading h0, curving toward +n with radius R0. Returns the new (a, n, s), or None before the knuckle."""
    a0, n0, h0, R0 = bind
    ox, oy = a0 - math.sin(h0) * R0, n0 + math.cos(h0) * R0   # (the arc's centre, on the palm side of the start)
    r = math.hypot(a - ox, n - oy)
    s = bind_s(a, n, bind)
    if s <= 0.0: return None
    t = R0 - r            # (toward the inside of the curl: the palm side)
    x, y, h, k = P.at(s)
    if t > 0 and k > 1e-6:   # (the palm side of a tight joint is squeezed, never pushed through the joint's own centre)
        L = 0.8 / k
        t = L * math.tanh(t / L)
    return x - math.sin(h) * t, y + math.cos(h) * t, s

def finger_lines(me, local_pts, bind, s_web, log=print):
    """Each finger past the web, found by what joins what (points the model split along its texture seams are one point here): {vertex: (finger, s)},
    {finger: its mean place across the hand (z)}, {finger: its own middle line in the bend's plane, and the length along it}."""
    key = {i: (round(a, 5), round(n, 5), round(z, 5)) for i, (a, n, z) in local_pts.items()}
    sv = {i: bind_s(a, n, bind) for i, (a, n, z) in local_pts.items()}
    inset = {k for i, k in key.items() if sv[i] > s_web}
    nb = {k: set() for k in inset}
    for e in me.edges:
        i, j = e.vertices
        if i in key and j in key:
            ki, kj = key[i], key[j]
            if ki in inset and kj in inset and ki != kj: nb[ki].add(kj); nb[kj].add(ki)
    comp = {}
    groups = []
    for k in inset:
        if k in comp: continue
        stack = [k]; comp[k] = len(groups); g = [k]
        while stack:
            c = stack.pop()
            for m in nb[c]:
                if m not in comp: comp[m] = comp[k]; g.append(m); stack.append(m)
        groups.append(g)
    mids, polys = {}, {}
    for gi, g in enumerate(groups):
        if len(g) < 12: mids[gi] = None; continue
        mids[gi] = sum(k[2] for k in g) / len(g)   # (a finger is a tube: the mean of its points is its middle)
        # its own middle line in the bend's plane: the mean of its points every 5 mm along it, smoothed
        bins = {}
        for k in g: bins.setdefault(int(bind_s(k[0], k[1], bind) / 0.005), []).append(k)
        ln = [(sum(k[0] for k in ks) / len(ks), sum(k[1] for k in ks) / len(ks)) for b, ks in sorted(bins.items())]
        for _ in range(2):
            ln = [ln[0]] + [((ln[i - 1][0] + 2 * ln[i][0] + ln[i + 1][0]) / 4, (ln[i - 1][1] + 2 * ln[i][1] + ln[i + 1][1]) / 4) for i in range(1, len(ln) - 1)] + [ln[-1]]
        S = [s_web]
        for p, q in zip(ln, ln[1:]): S.append(S[-1] + math.hypot(q[0] - p[0], q[1] - p[1]))
        polys[gi] = (ln, S)
    owner = {i: (comp[k], sv[i]) for i, k in key.items() if k in comp and mids[comp[k]] is not None}
    log("FIST fingers past the web: %s" % ", ".join("z %+.3f, %.1f cm" % (m, (polys[f][1][-1] - s_web) * 100) for f, m in sorted(mids.items(), key=lambda x: x[1] or 0) if m is not None))
    return owner, mids, polys

def along_poly(poly, a, n):
    """(s, t) of a point against a finger's own middle line: how far along it, and how far to its palm side."""
    ln, S = poly
    best = None
    for j in range(len(ln) - 1):
        (x0, y0), (x1, y1) = ln[j], ln[j + 1]
        dx, dy = x1 - x0, y1 - y0; L2 = dx * dx + dy * dy
        if L2 < 1e-12: continue
        u = ((a - x0) * dx + (n - y0) * dy) / L2
        if j > 0: u = max(u, 0.0)
        if j < len(ln) - 2: u = min(u, 1.0)
        px, py = x0 + dx * u, y0 + dy * u
        d2 = (a - px) ** 2 + (n - py) ** 2
        if best is None or d2 < best[0]:
            L = math.sqrt(L2)
            best = (d2, S[j] + u * L, ((a - px) * -dy + (n - py) * dx) / L)
    return best[1], best[2]

def curl_fingers(local_pts, P, bind, side_by_side=None):
    """side_by_side: (owner, mids, polys, spacing, widen, s_web, s_full): each finger bends along its own line and moves beside the next, widened, as they lie in a fist."""
    out = {}
    if side_by_side:
        owner, mids, polys, spacing, widen, s_web, s_full = side_by_side
        live = [f for f in mids if mids[f] is not None]
        order = sorted(live, key=lambda f: mids[f])
        middle = sum(mids[f] for f in live) / len(live)
        want = {f: middle + (k - (len(order) - 1) / 2) * spacing for k, f in enumerate(order)}
    for i, (a, n, z) in local_pts.items():
        r = bend_point(a, n, P, bind)
        if r is None: continue
        x, y = r[0], r[1]
        if side_by_side and i in owner:
            f, s = owner[i]
            sf, tf = along_poly(polys[f], a, n)
            px, py, h, k = P.at(sf)
            if tf > 0 and k > 1e-6:
                L = 0.8 / k
                tf = L * math.tanh(tf / L)
            e0 = sstep(s, s_web, s_web + 0.012)
            x += (px - math.sin(h) * tf - x) * e0; y += (py + math.cos(h) * tf - y) * e0
            c = mids[f]
            e = sstep(s, s_web, s_full)
            z = c + (want[f] - c) * e + (z - c) * (1.0 + (widen - 1.0) * e)
        out[i] = (x, y, z)
    return out

def tidy_fingers(me, pts, out, bind, ka, kn, s_from, s_to, iters, r0, give):
    """Smooths the closed fingers (a low-poly tip that only turns comes out as torn spikes): Taubin passes over the fingers past s_from (fully past s_to), the
    points the model split along its texture seams kept as one; then anything standing further than r0 from the grip line is pulled most of the way back in."""
    key = {i: (round(a, 5), round(n, 5), round(z, 5)) for i, (a, n, z) in pts.items()}
    idx = {}
    for i, k in key.items(): idx.setdefault(k, []).append(i)
    P = {k: Vector(out[v[0]]) for k, v in idx.items()}
    W = {k: sstep(bind_s(k[0], k[1], bind), s_from, s_to) for k in idx}
    nb = {k: set() for k in idx}
    for e in me.edges:
        i, j = e.vertices
        if i in key and j in key and key[i] != key[j]: nb[key[i]].add(key[j]); nb[key[j]].add(key[i])
    for _ in range(iters):
        for lam in (0.5, -0.53):   # (Taubin: a smoothing step, then a slightly larger step back, so the fingers don't shrink)
            new = {}
            for k, p in P.items():
                if W[k] <= 0 or not nb[k]: continue
                avg = sum((P[m] for m in nb[k]), Vector()) / len(nb[k])
                new[k] = p + (avg - p) * (lam * W[k])
            P.update(new)
    for k, p in P.items():
        if W[k] <= 0: continue
        r = math.hypot(p.x - ka, p.y - kn)
        if r > r0:
            f = (r0 + (r - r0) * give) / r
            q = Vector((ka + (p.x - ka) * f, kn + (p.y - kn) * f, p.z))
            P[k] = p.lerp(q, W[k])
    for k, v in idx.items():
        for i in v: out[i] = tuple(P[k])

def thumb_weight(a, n, z):
    """The bare thumb: palm side of the hand's middle, at the blade's end of the hand, before the fingers."""
    return sstep(n, -0.012, 0.012) * sstep(z, 0.030, 0.058) * (1 - sstep(a, 0.004, 0.028))

def cyl_weight(piv, tip, radius, ramp):
    """The glove's thumb: inside a cylinder of radius round its line from piv to tip, eased in over ramp (shares of its length)."""
    pv, tp = Vector(piv), Vector(tip)
    ax = tp - pv; L = ax.length; ax.normalize()
    def w(a, n, z):
        d = Vector((a, n, z)) - pv
        u = d.dot(ax)
        r = (d - ax * u).length
        return sstep(u / L, ramp[0], ramp[1]) * (1 - sstep(r, radius * 0.6, radius)) * (1 - sstep(u / L, 1.25, 1.4))
    return w

def aim_thumb(local_pts, weight, piv, tip, want, share=1.0, fold_at=0.6, fold_deg=0.0, fold_to=(0.0, -1.0, 0.0), cyl=None, shift=(0.0, 0.0, 0.0)):
    """The thumb turns about its root (piv) from pointing at tip to pointing along want, by share; its end, past fold_at of its length, first folds by fold_deg toward fold_to;
    then it moves by shift (eased in with the same weight)."""
    out = {}
    pv, tp = Vector(piv), Vector(tip)
    cur = (tp - pv); length = cur.length; cur.normalize()
    q = Quaternion().slerp(cur.rotation_difference(Vector(want).normalized()), share)
    fax = cur.cross(Vector(fold_to)).normalized()
    fpiv = pv + cur * length * fold_at
    for i, (a, n, z) in local_pts.items():
        w = weight(a, n, z)
        if w <= 0.0: continue
        p = Vector((a, n, z))
        if fold_deg:
            wt = sstep((p - fpiv).dot(cur), -0.006, 0.010)
            if wt > 0: p = fpiv + Quaternion(fax, math.radians(fold_deg) * wt) @ (p - fpiv)
        p2 = pv + Quaternion().slerp(q, w) @ (p - pv) + Vector(shift) * w
        out[i] = (p2.x, p2.y, p2.z)
    return out

def hand_positions(obj, arm_obj, side, who, mode, log=print):
    """The hand's new points (world space) as {vertex index: Vector}. mode: 'fist' or 'relaxed'."""
    S = SETTINGS[who]
    H, A, D, N, K = frame(arm_obj, side)
    Af = (A - D * A.dot(D)).normalized()   # (the fingers' way, square to the grip line: Af, N, D are a true frame round the line)
    K = K - A * (GRIP_ALONG - 0.030) - N * (GRIP_PALM - 0.060)   # (the settings' origin; the line itself is at (ka, kn) from it)
    ka = (GRIP_ALONG - 0.030) * A.dot(Af); kn = GRIP_PALM - 0.060
    mw = obj.matrix_world
    names = {g.index: g.name.split(":")[-1] for g in obj.vertex_groups}
    pts, wts = {}, {}
    for v in obj.data.vertices:
        w = sum(g.weight for g in v.groups if names.get(g.group) == side + "Hand")
        if w <= 0.01: continue
        d = mw @ v.co - K
        pts[v.index] = (d.dot(Af), d.dot(N), d.dot(D)); wts[v.index] = min(1.0, w)
    joints = S["fist"] if mode == "fist" else S["relaxed"]
    out = dict(pts)
    if joints:
        P = wrap_path(S["bind"], ka=ka, kn=kn, log=log, **joints) if isinstance(joints, dict) else joints_path(S["bind"], joints)
        sbs = None
        side = S.get("side") if mode == "fist" else S.get("side_relaxed")
        if side:
            spacing, widen, s_web, s_full = side
            owner, mids, polys = finger_lines(obj.data, pts, S["bind"], s_web, log=log)
            sbs = (owner, mids, polys, spacing, widen, s_web, s_full)
        out.update(curl_fingers(pts, P, S["bind"], sbs))
        if mode == "fist" and S.get("tidy"):
            tidy_fingers(obj.data, pts, out, S["bind"], ka, kn, **S["tidy"])
        if mode == "fist" and S.get("fill"):   # (the palm just under the handle rises to meet the fingertips, so no light comes through along the handle)
            push, a_lo, a_hi, n_from = S["fill"]
            for i, (a, n, z) in pts.items():
                if bind_s(a, n, S["bind"]) > 0.0: continue
                x, y, zz = out[i]
                ar = x - ka
                w = sstep(ar, a_lo - 0.02, a_lo) * (1 - sstep(ar, a_hi - 0.012, a_hi)) * sstep(y - kn, n_from - 0.015, n_from)
                if w > 0: out[i] = (x, min(y + push * w, max(y, kn + 0.012)), zz)   # (no further than the fingertips that come round the handle)
    th = S["thumb"] if mode == "fist" else S["thumb_relaxed"]
    if th:
        cyl = th.get("cyl")
        wf = cyl_weight(th["piv"], th["tip"], *cyl) if cyl else thumb_weight
        out.update(aim_thumb({i: out[i] for i in pts}, wf, **th))   # (the thumb turns from where the fingers' bend left it)
    res = {}
    for i, (a, n, z) in out.items():
        p = K + Af * a + N * n + D * z
        res[i] = (mw @ obj.data.vertices[i].co).lerp(p, wts[i])
    return res

def subdivide_hands(obj, cuts, log=print):
    """Finer fingers so they bend smoothly: every edge of both hands, cut. Done once, before any shape key."""
    me = obj.data
    names = {g.index: g.name.split(":")[-1] for g in obj.vertex_groups}
    bm = bmesh.new(); bm.from_mesh(me)
    dl = bm.verts.layers.deform.verify()
    hand_g = [i for i, n in names.items() if n in ("RightHand", "LeftHand")]
    def hw(v): return sum(v[dl].get(i, 0.0) for i in hand_g)
    edges = [e for e in bm.edges if all(hw(v) > 0.3 for v in e.verts)]
    if edges and cuts > 0: bmesh.ops.subdivide_edges(bm, edges=edges, cuts=cuts, use_grid_fill=True)
    bm.to_mesh(me); bm.free(); me.update()
    log("FIST %s: %d hand edges cut %d times" % (obj.name, len(edges), cuts))

def flex(obj, arm_obj, side, angle, along=0.0, palm=0.0, log=print):
    """The wrist bends the hand toward the palm by angle (radians) about the grip line's direction through the wrist, eased in over the first 5 cm past
    the wrist so the cuff stays put; then the hand moves by along (m, along A) and palm (m, toward the palm), eased the same way."""
    H, A, D, N, K = frame(arm_obj, side)
    mw = obj.matrix_world
    inv = mw.inverted()
    names = {g.index: g.name.split(":")[-1] for g in obj.vertex_groups}
    q_full = Quaternion(D, angle) if A.cross(N).dot(D) > 0 else Quaternion(D, -angle)
    n = 0
    for v in obj.data.vertices:
        w = sum(g.weight for g in v.groups if names.get(g.group) == side + "Hand")
        if w <= 0.01: continue
        p = mw @ v.co
        e = w * sstep((p - H).dot(A), 0.01, 0.06)
        if e <= 0: continue
        q = Quaternion().slerp(q_full, e)
        v.co = inv @ (H + q @ (p - H) + (A * along + N * palm) * e)
        n += 1
    obj.data.update()
    log("FIST %s %s: wrist bent %.1f deg, %d points" % (obj.name, side, math.degrees(angle), n))

def make_fists(obj, arm_obj, bare, log=print):
    """Adds shape keys Basis (the relaxed hand), FistR and FistL (each hand closed round its grip line, the other as the basis).
    bare: the chain mail hand (its basis gets a natural half curl); else the gauntlets (their own half closed shape is the basis)."""
    who = "bare" if bare else "suit"
    subdivide_hands(obj, 2 if bare else 1, log)
    mw = obj.matrix_world; inv = mw.inverted()
    bind = [v.co.copy() for v in obj.data.vertices]
    fists = {}
    for side in ("Right", "Left"):
        if not bare:
            fdeg, falong, fpalm = SETTINGS[who]["flex"]
            flex(obj, arm_obj, side, math.radians(fdeg), falong, fpalm, log=log)
        fists[side] = hand_positions(obj, arm_obj, side, who, "fist", log)
        for i, c in enumerate(bind): obj.data.vertices[i].co = c
    if bare:
        for side in ("Right", "Left"):
            for i, p in hand_positions(obj, arm_obj, side, who, "relaxed", log).items(): obj.data.vertices[i].co = inv @ p
    obj.data.update()
    obj.shape_key_add(name="Basis", from_mix=False)
    for side, key in (("Right", "FistR"), ("Left", "FistL")):
        kb = obj.shape_key_add(name=key, from_mix=False)
        for i, p in fists[side].items(): kb.data[i].co = inv @ p
        kb.value = 0.0
        log("FIST %s %s: %d points" % (obj.name, key, len(fists[side])))
