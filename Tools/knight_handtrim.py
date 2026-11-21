# The Dark Plate gauntlets' back-of-hand blades (2026-11-21, owner: "make sure the new gauntlets don't have 6 fingers"). Each Meshy hand plate ends in a flat
# pointed blade that sticks 6 cm out sideways past the little finger (and in the sword fist it pointed the same way as the fingers), so every hand read as
# four fingers, a thumb and a sixth long point. This pulls the hand-weighted points that lie beyond LIMIT (m, sideways from the body's middle line, in
# the rest pose) back towards it, keeping a little of the shape, before the fists are made (so the fist copy has it too).
# Used by knight_darkplate_export.py after the pieces are loaded; log lines start "HTRIM".
import numpy as np

LIMIT = 0.50     # (m) sideways distance from the middle line past which a hand point is pulled in (his fingers reach 0.49)
KEEP = 0.25      # how much of the distance beyond LIMIT stays

def trim(gaunt, log=print):
    me = gaunt.data
    names = {g.index: g.name.split(":")[-1] for g in gaunt.vertex_groups}
    n = len(me.vertices)
    P = np.empty((n, 3)); me.vertices.foreach_get("co", P.ravel())
    mw = np.array(gaunt.matrix_world); W = P @ mw[:3, :3].T + mw[:3, 3]
    hw = {"Right": np.zeros(n), "Left": np.zeros(n)}
    for v in me.vertices:
        for g in v.groups:
            nm = names.get(g.group, "")
            for sd in hw:
                if nm == sd + "Hand": hw[sd][v.index] = g.weight
    moved = 0
    for sd, sign in (("Right", -1.0), ("Left", 1.0)):
        side = sign * W[:, 0]                       # (distance out from the middle line)
        m = (hw[sd] > 0.5) & (side > LIMIT)
        if not m.any(): continue
        new = LIMIT + (side[m] - LIMIT) * KEEP
        W[m, 0] = sign * new; moved += int(m.sum())
    inv = np.linalg.inv(mw)
    P2 = W @ inv[:3, :3].T + inv[:3, 3]
    me.vertices.foreach_set("co", P2.ravel()); me.update()
    log("HTRIM %d points pulled in" % moved)
