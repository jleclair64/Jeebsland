# Turns Meshy's models in Art/Props into what the game holds (2026-11-05). Run by Tools\BuildProps.ps1 in Blender.
# Each prop gets its grip at the origin and the game's axes, a size, and a mask in its vertex colour (red 1 = takes the tier's or the potion's colour).
# Its pictures are shrunk: base colour 1024, the others 512. Results: Saved/KnightBuild/Props/<Name>.fbx, T_<Name>_Base/Normal/MR.png
#
# The numbers per prop live in PROPS:
#  length: the longest side in cm. axes: where the model's own x, y, z go in the game's frame (signed letters, X Y Z).
#  grip: where the hand holds it, as a share along the long axis in the game's frame (0 = lowest end, 1 = highest end); origin there.
#  bend: straighten the long axis into one gentle arc this deep, along this game axis (the longbow). thick: x its width and depth, about that line.
#  squeeze: (from, to, x): x the width and depth about the long axis, coming in between these shares along it and staying from there on.
#  widen: (axis, x): x its size along this game axis. A single picture leaves the depth a guess.
#  blade: (span, thick, from, root, tip): a head's arms are too flat from the side: past 'from' cm along span, they get this half-thickness along thick, root to tip, in cm.
#  neck: (axis, from cm, stretch cm): everything beyond this, on the low side of the axis, moves that much further out, drawing the faces between into a longer neck.
#  also: (axis, cm): everything beyond this along the axis takes the colour too (a pale patch on the bottle's base).
#  below: (axis, cm) / far: only there does the mask apply (below that value / further than far from the grip along the long axis).
#  wood: (hue degrees, saturation, value, largest hue that counts; all in linear colour; the game's lighting darkens and reddens it, so what reads as brown there is about 25, 0.74, 0.28): evens the wood in the base picture to one rich brown (owner: brown, a little variance, no drastic change, just a
#  touch of grain). Keeps 15% of each pixel's own hue, 25% of the slow changes in brightness and 60% of the fine grain. Hues above the last number (a gold inlay) are left alone.
#  The grip's other two axes come from what is actually at the grip, not the bounding box.
#  mask: "grey" = low-saturation pixels take the tier (metal parts), "colour" = saturated pixels take it (a potion's liquid), "far" = everything beyond 'far' of the
#  grip along the long axis takes it (a stave but not its leather grip).
#  More steel for the tier's colour (owner, 2026-11-11; one model for every tier). Conditions are (axis, lo, hi) on a face's centre in cm, "|Y|" the distance
#  from the middle, "nZ" which way the face looks (-1..1):
#  clean: the mask is decided once per face from inside it (no tinted streaks where a corner sits on the picture's grey seam), and wood faces show only wood.
#  metal: [[conds], ...] whole parts that become steel. bands: [(axis, lo, hi[, [conds]]), ...] slices cut cleanly and raised by 'raise' cm (0.15) as steel bands
#  and plates. plain: [[conds], ...] parts that stay wood whatever the picture painted there. Added steel takes one even colour from a calm steel spot in the picture.
import bpy, bmesh, os, sys, math
from mathutils import Vector, Matrix

a = sys.argv[sys.argv.index("--") + 1:]
src, out = a[0], a[1]
os.makedirs(out, exist_ok=True)

# Frames, from the code that holds them:
#  on the right fist (pickaxe, potion): the working end runs along -Z, grip at the origin (KnightDesign's Xf(GripPos, DownTo(BladeDir)))
#  on the bow's hold (longbow): X along the limbs, Z toward the string, grip at the origin (WeaponDesign K_Bow)
#  on the crossbow's hold: X toward the prod, Z up, the rear grip at the origin
#
# Added 2026-11-12 for the community-library models (Docs/MeshyCommunityLibrary.md lists their pages):
#  reduce: triangles to keep (Blender's collapse decimation, before anything else; the library models come at 3k to 1M).
#  bake: the reduced model is welded, gets fresh UVs and its pictures baked from the full one (a heavy reduction otherwise shatters the picture into shards).
#    ON BY DEFAULT whenever "reduce" cuts triangles (2026-11-21, the owner's "shattered" models): a collapse across the library models' hundreds of UV islands
#    drags each kept corner's UV onto a neighbouring island, so faces sample the wrong part of the picture: dark cracks, white flecks, mirror-metal shards
#    (the crossbow's limbs, the pickaxe head). bake=False only for a model whose picture is one flat colour per part.
#  unfleck: with wood, low-saturation (grey or blue-grey) specks inside the wood, where more than this share of the pixels round them is wood, become
#    wood at the brightness round them (the longbow's painted metal flecks read as white chips in the sun, 2026-11-21).
#  fill: with bake, holes left open by 'cut' are closed once the model is welded (the longbow's tips, where its string was tied on).
#  swatch: with bake, a steel-grey square is painted into an empty corner of the pictures, for added bands to take their colour from (a model with no steel of its own).
#  cull: before a bake, delete faces no ray from them can leave the model by (inner walls). Off unless asked: it ate a crystal face and frayed a medallion.
#  refine: N rounds of splitting the faces the picture's metal/wood edge runs through, so a 'clean' mask follows the edge instead of making teeth (2026-11-13).
#  cut: lambda on a face's centre in the MODEL's own frame (as Blender imports it, metres): those faces are deleted (the crossbow's bolt and string, the bows' strings, the quiver's arrows).
#  marks: {name: (lo, hi)} boxes in the model's own frame: the middle of what is left inside each is printed in the game's frame, in cm ("PROP mark"), for the numbers in JeebProps.h.
#  stretch: (axis, from cm, cm): everything above 'from' along this game axis moves that much further up (a longer grip, the pommel side moved, the blade kept where it was).
#  rg: lambda (game-frame position in cm, (r, g, b) of the picture) -> (red, green) of the mask, instead of 'mask': red takes the tier's colour, green the second colour (an arrow's feathers).
#  mask "none": nothing takes a colour (potions and amulets in their own colours).
# The frames of the new ones:
#  arrows: the tip at the origin, the shaft back along -X (Projectile's arrow; BowBits lay it from the nock).  ArrowBack: only the back of it, for the quiver (nock up along +Z there).
#  quiver: standing along Z, centred on the origin (UpdateQuiver).  potions: as the old one, the cork along -Z, held at the neck.
#  amulets: hung from the top of the bail (the origin), the face looking along +X, down -Z (UpdateAmulet, on the chest).
def _seg_d(p, a_, b_):
    # distance in the model's xy plane from p to the segment a_-b_
    ax_, ay_ = b_[0] - a_[0], b_[1] - a_[1]
    t = max(0.0, min(1.0, ((p[0] - a_[0]) * ax_ + (p[1] - a_[1]) * ay_) / (ax_ * ax_ + ay_ * ay_)))
    return math.hypot(p[0] - a_[0] - t * ax_, p[1] - a_[1] - t * ay_)

def _hue(c):
    r, g_, b = c
    mx, mn = max(c), min(c)
    if mx - mn < 1e-5: return 0.0, 0.0, mx
    if mx == r: h = (g_ - b) / (mx - mn) % 6.0
    elif mx == g_: h = (b - r) / (mx - mn) + 2.0
    else: h = (r - g_) / (mx - mn) + 4.0
    return h * 60.0, (mx - mn) / mx, mx

def _grey(c):
    mx, mn = max(c), min(c)
    return (mx - mn) / max(mx, 1e-4) < 0.30 or mx < 0.06

AMULET = dict(length=6.0, grip=("Z", 1.0), mask="none", reduce=4000, bake=True)
SHIELD = dict(axes=("Y", "-X", "Z"), grip=("Z", 0.5), pivot=(0.0, 0.5, 0.5), mask="none", reduce=6000, bake=True, src_paint=True)   # (the tier mask by pixel: paint, tier)
def _tower_wood(Hd, S, V, Vb):   # (the tower's weathered grey-blue planks: everything else on its face is bronze or iron)
    return (_SB[0] < 0.24) & (Vb < 0.42)   # (by the saturation round each pixel: the bronze is a warm metal, the planks are grey)
_SB = [None]   # (the paint step sets it: the saturation blurred over a few pixels)
SHIELD_H = (66.0, 42.0, 78.0)   # kite, round, tower: the longest side in cm (JeebProps::ShieldSize says the same)
PROPS = {
    "Pickaxe":  dict(file="Pickaxe2.glb", axes=("Y", "X", "-Z"),  length=92.0, grip=("Z", 0.85), mask="grey", below=("Z", -60.0), wood=(25.0, 0.74, 0.30, 60.0), clean=True, reduce=6000),
    "Longbow":  dict(file="Longbow2.glb", axes=("Z", "-Y", "X"),  length=156.0, grip=("X", 0.50), mask="none", far=12.0, widen=("Y", 1.5), wood=(25.0, 0.74, 0.28, 60.0), reduce=5000, bake=True, swatch=True, fill=True, unfleck=0.6,   # (2026-11-16: baked to fresh UVs. Reduced to 3000 without a bake, the stave's picture and normal map shattered into scaly shards with dark pits: the owner's "holes")
                     cut=lambda p: p[0] > 0.136 and -0.925 < p[2] < 0.905,
                     marks={"TipTop": ((0.10, -0.05, 0.86), (0.16, 0.05, 0.97)), "TipLow": ((0.10, -0.05, -0.98), (0.16, 0.05, -0.88))},
                     clean=True, plain=[[("|X|", 15.0, 68.0)]], bands=[("|X|", 9.5, 22.0), ("|X|", 41.0, 45.0), ("|X|", 64.0, 80.0)]),
    "Crossbow": dict(file="Crossbow2.glb", axes=("-X", "-Y", "Z"), length=88.0, grip=("X", 0.36), mask="grey", wood=(25.0, 0.74, 0.28, 60.0), clean=True, reduce=12000,
                     # (the bolt lying on the stock past the nut, and the string from the prod's ends back to the nut, with its loop over the nut: the game draws both)
                     cut=lambda p: (p[0] < -0.15 and abs(p[1]) < 0.042 and ((p[0] > -0.62 and p[2] > 0.085) or (p[0] <= -0.62 and p[2] > 0.118))) or (_seg_d(p, (-0.378, 0.792), (0.065, 0.0)) < 0.03 and (abs(p[1]) > 0.13 or p[2] > 0.135))
                                   or (_seg_d(p, (-0.378, -0.792), (0.065, 0.0)) < 0.03 and (abs(p[1]) > 0.13 or p[2] > 0.135)) or (0.0 < p[0] < 0.13 and abs(p[1]) <= 0.03 and p[2] > 0.15),
                     marks={"TipR": ((-0.46, 0.78, -0.3), (-0.36, 0.92, 0.3)), "TipL": ((-0.46, -0.92, -0.3), (-0.36, -0.78, 0.3)), "Nut": ((0.02, -0.03, 0.0), (0.12, 0.03, 0.3)),
                            "Stirrup": ((-0.96, -0.1, -0.3), (-0.85, 0.1, 0.3))}),
    # Wizard staff and wand (2026-11-18, Meshy meshy-6 from a picture; Art/Props/Originals/*_Tilted.glb, stood upright by Tools/prop_upright.py): the orb
    # (cyan, violet) is saturated so it keeps its own colour; the steel (ferrule, bands, crown, caps) takes the tier. The orb end is game -Z.
    "Staff":    dict(file="Staff.glb", axes=("X", "-Y", "-Z"), length=165.0, grip=("Z", 0.78), girth=1.12, rg=lambda co, c: (1.0 if (_grey(c) and (co - Vector((0.0, 0.0, -117.3))).length > 8.5 and not -21.0 < co.z < 1.0) else 0.0, 0.0), wood=(25.0, 0.74, 0.28, 60.0), clean=True, reduce=7000, bake=True),
    "Wand":     dict(file="Wand.glb", axes=("X", "-Y", "-Z"), length=52.0, grip=("Z", 0.83), girth=1.3, rg=lambda co, c: (1.0 if (_grey(c) and co.z > -38.5 and not -2.2 < co.z < 5.5) else 0.0, 0.0), wood=(25.0, 0.74, 0.28, 60.0), clean=True, reduce=5000, bake=True),
    # The troll's club (2026-11-19, Meshy meshy-t2.5 from a picture; Art/Props/Originals/Club_Meshy.glb): head along game -Z, the troll's fist near the pommel end of the
    # leather wrap; the steel bands, studded cap and pommel cap take the tier, the leather wrap (grey too) is left out of the mask.
    "Club":     dict(file="Club.glb", axes=("X", "-Y", "-Z"), length=125.0, grip=("Z", 0.86), rg=lambda co, c: (1.0 if (_grey(c) and not -24.0 < co.z < 7.0) else 0.0, 0.0), wood=(25.0, 0.74, 0.28, 60.0), clean=True, reduce=6000, bake=True),
    "BoneClub": dict(file="BoneClub.glb", axes=("X", "-Y", "-Z"), length=125.0, grip=("Z", 0.84), rg=lambda co, c: (1.0 if (_grey(c) and max(c) < 0.30) else 0.0, 0.0), clean=True, reduce=6000, bake=True),   # (2026-11-27: the Meshy bone club; held like the Club. Only the two dark horns take the tier; bone and leather keep their colours, no wood evening)
    "Sword":    dict(file="Sword.glb", axes=("Y", "X", "-Z"), length=92.0, grip=("Z", 0.890), mask="grey", clean=True, reduce=8000, stretch=("Z", 1.0, 6.0),
                     marks={"Guard": ((-0.2, -0.06, -0.545), (0.2, 0.06, -0.52)), "Tip": ((-0.02, -0.06, 0.93), (0.02, 0.06, 0.95))}),
    "Axe":      dict(file="Axe.glb", axes=("X", "-Y", "-Z"), length=87.0, grip=("Z", 0.816), mask="grey", below=("Z", -58.0), wood=(25.0, 0.74, 0.28, 60.0), clean=True, reduce=8000, bake=True,
                     marks={"Edge": ((-0.32, -0.1, 0.5), (-0.27, 0.1, 0.95)), "Head": ((-0.3, -0.1, 0.9), (0.3, 0.1, 0.95))}),
    "Bow":      dict(file="Bow.glb", axes=("Z", "-Y", "X"), length=124.0, grip=("X", 0.50), widen=("Z", 0.75), wood=(25.0, 0.74, 0.28, 30.0), clean=True, refine=2, islands=12.0, despeck=0.6,
                     cut=lambda p: p[0] > 0.18 and abs(p[2]) < 0.738,
                     # (the gold fittings take the tier's colour: pale yellow, not the orange wood or the dark leather)
                     rg=lambda co, c: (1.0 if (28.0 <= _hue(c)[0] <= 65.0 and _hue(c)[2] > 0.10 and _hue(c)[1] > 0.2 and not 26.0 < abs(co[0]) < 55.0) else 0.0, 0.0),   # (2026-11-15: the limbs's pale pinstripes passed the gold test and became patches of tier steel, which mirrors the dark surroundings: black holes along the limbs. Fittings only at the collars and tips)
                     marks={"WrapTop": ((0.12, -0.08, 0.72), (0.23, 0.08, 0.84)), "WrapLow": ((0.12, -0.08, -0.84), (0.23, 0.08, -0.72))}),
    "Arrow":    dict(file="Arrow.glb", axes=("Y", "Z", "X"), length=70.0, grip=("X", 1.0), wood=(25.0, 0.74, 0.28, 60.0), reduce=900, bake=True,
                     rg=lambda co, c: (1.0 if co.x > -9.0 else 0.0, 1.0 if (co.x < -52.0 and math.hypot(co.y, co.z) > 0.9) else 0.0)),
    "ArrowBack": dict(file="Arrow.glb", axes=("Y", "Z", "X"), length=29.5, grip=("X", 1.0), wood=(25.0, 0.74, 0.28, 60.0), reduce=900, bake=True,
                     cut=lambda p: p[2] > -0.15,   # (the back 30 cm of it, what shows out of the quiver)
                     rg=lambda co, c: (0.0, 1.0 if (co.x < -11.5 and math.hypot(co.y, co.z) > 0.9) else 0.0)),
    "Quiver":   dict(file="Quiver.glb", axes=("X", "Y", "Z"), length=46.0, grip=("Z", 0.5), mask="none", reduce=8000, bake=True,
                     cut=lambda p: p[2] > 0.47 or (p[2] > 0.30 and (p[0] / 0.175) ** 2 + (p[1] / 0.095) ** 2 < 1.0)),   # (its own arrows: the game puts its own in)
    "PotionRed":    dict(file="PotionRed.glb",    axes=("X", "-Y", "-Z"), length=19.0, grip=("Z", 0.22), mask="none", reduce=5000, bake=True),
    "PotionPurple": dict(file="PotionPurple.glb", axes=("X", "-Y", "-Z"), length=17.0, grip=("Z", 0.22), mask="none", reduce=5000, bake=True),
    "PotionGreen":  dict(file="PotionGreen.glb",  axes=("X", "-Y", "-Z"), length=19.0, grip=("Z", 0.30), reduce=5000,
                     rg=lambda co, c: (1.0 if (c[1] > 1.25 * c[0] and c[1] > 1.1 * c[2] and _hue(c)[1] > 0.25) else 0.0, 0.0)),   # (the liquid takes the potion's colour: swiftness, radar)
    # The wizard's spell book (2026-11-22, Meshy meshy-t2.5 from a picture, 19 credits): held by its spine in his left fist. Spine along game Z, the covers open toward -X, gem cover +Y.
    "SpellBook": dict(file="SpellBook.glb", axes=("Z", "-X", "-Y"), length=24.0, grip=("Z", 0.5), mask="none"),
    # The wizard's off-hand items (2026-11-22, agent wizlook; owner: "change the book to be open and held upright in his left hand", "a glowing crystal ball that floats
    # above his open left hand", "a wizard pipe"). Meshy meshy-t2.5 from pictures (book, pipe); the ball is a sphere with Meshy's picture on it (Saved/WizLook/ball.py
    # in the wizlook copy) and its glow from M_PropGlow (fixlook_assets.py). All in their own frame: the long side along X, the middle at the origin; placed in the
    # hand by AKnightCharacter::UpdateWizardBook.
    "OpenBook":    dict(file="OpenBook.glb", axes=("X", "Y", "Z"), length=30.0, grip=("X", 0.5), mask="none", reduce=5000),
    "Pipe":        dict(file="Pipe.glb", axes=("X", "Y", "Z"), length=28.0, grip=("X", 0.5), mask="none", reduce=5000),
    "CrystalBall": dict(file="CrystalBall.glb", axes=("X", "Y", "Z"), length=11.0, grip=("Z", 0.5), mask="none"),
    # The wizard's flying broom (2026-11-22, agent broom): Meshy meshy-6-lite image-to-3d (15 credits) from the CC0 community "Broomstick" cover picture
    # (meshy.ai/posts/019c98ca-e57f-7240-8dbb-4dbed40d4b35). Handle tip along game +X, bristles -X, up +Z; the origin at the seat (AKnightCharacter::UpdateBroomModel).
    # The hand torch (2026-11-27, agent light; Meshy meshy-t2.5 from a picture, 19 credits; Saved/Torch in the light copy): head up (+Z), the fist round the
    # leather wrap near the foot; placed in the off hand by AKnightCharacter::PlaceLights, the flame on top from code (M_TorchFire).
    "Torch":       dict(file="Torch.glb", axes=("X", "Y", "Z"), length=58.0, grip=("Z", 0.17), mask="none", wood=(25.0, 0.74, 0.28, 60.0), clean=True, reduce=4000, bake=True),
    "Broom":       dict(file="Broom.glb", axes=("X", "Y", "Z"), length=175.0, grip=("X", 0.55), mask="none", reduce=8000, bake=True),
    "AmuletRune":       dict(AMULET, file="AmuletRune.glb",       axes=("-Y", "X", "Z")),
    "AmuletEmberHeart": dict(AMULET, file="AmuletEmberHeart.glb", axes=("-Y", "X", "Z")),
    "AmuletScarab":     dict(AMULET, file="AmuletScarab.glb",     axes=("Y", "-X", "Z")),
    "AmuletCrystal":    dict(AMULET, file="AmuletCrystal.glb",    axes=("Z", "-Y", "X")),
    "AmuletVoidSeer":   dict(AMULET, file="AmuletVoidSeer.glb",   axes=("-Y", "X", "Z")),
    "AmuletEmber":      dict(AMULET, file="AmuletEmber.glb",      axes=("-Y", "X", "Z")),
    # Shields (2026-11-16, the owner's picks from the library; originals git-ignored in Art/Props/Originals, links in Art/Props/Sources.md): the face looks
    # along +X, up +Z, the width along Y; the origin at the back of the box, in the middle (KnightDesign's shield frame: JeebProps::ShieldPlace)
    # Kite (2026-11-21, repainted; owner: "The paint on this shield looks horrible. It looks all smudged and the line where gold stops and black starts isn't
    # defined."): its pictures are no longer sorted out of the library model's grimy one. repaint draws new ones from the model's shape (Tools/prop_repaint.py):
    # one clean charcoal face, the rim, rivets and cross in clean steel that takes the tier's colour, a thin dark line where they meet the face, the cross's
    # sunk panel a darker shade of the tier with its scrollwork lighter. front: the side of the model's own thin axis the face looks along. rows: how fine
    # the shape is measured. tex / normal: the size of the colour and mask pictures / of the normal map (from the shape alone, no painted scratches). calm: how much of the
    # face's dents and cracks is smoothed out of the normal map (0.85).
    # look: any of prop_repaint.design's numbers (field colour, line width, brightness of border, panel, scrollwork and rim).
    "ShieldKite":  dict(SHIELD, file="Originals/ShieldKite_Original.glb",  length=SHIELD_H[0], repaint=dict(front="-Y", rows=2048, tex=2048, normal=1024)),
    # Round: the planks rich brown with a touch of grain, the red ring a clean rich red, the iron rim, studs and boss take the tier's colour
    "ShieldRound": dict(SHIELD, file="Originals/ShieldRound_Original.glb", length=SHIELD_H[1], tier=[1], vote=3, paint=[
                        dict(name="red", test=lambda Hd, S, V, Vb: ((Hd < 12.0) | (Hd > 340.0)) & (S > 0.6) & (V > 0.12), to=(2.0, 0.8, 0.45), fine=0.25, slow=0.3, rough=0.5, metal=0.0, open=1),
                        dict(name="iron", test=lambda Hd, S, V, Vb: (S < 0.25) & (V > 0.05), fine=0.3, sat=0.2, rough=0.4, metal=1.0, open=2),
                        dict(name="wood", test=lambda Hd, S, V, Vb: V >= 0.0, to=(25.0, 0.74, 0.28), fine=0.45, slow=0.25, rough=0.55, metal=0.0)]),
    # Tower (2026-11-16): the library picture's planks were weathered grey and its bronze worn, both full of pale streaks (review rounds 1-2).
    # (2026-11-17) That picture's pale wear and scratches still showed at Silver after two rounds, so the model was retextured by Meshy (10 credits,
    # enable_original_uv: the same mesh and UVs; prompt in Art/Props/Sources.md) as clean painted planks and clean dark steel: ShieldTower_Retex.glb.
    # Colour now tells them apart cleanly (the planks are saturated, the steel grey), so the relief split is no longer needed. All the steel (frame, X
    # bands, boss, edging, side knots) takes the tier's colour.
    "ShieldTower": dict(SHIELD, file="Originals/ShieldTower_Retex.glb", length=SHIELD_H[2], tier=[1], vote=3, paint=[
                        dict(name="wood", test=lambda Hd, S, V, Vb: ((Hd < 50.0) | (Hd > 340.0)) & (S > 0.35) & (V > 0.04), to=(25.0, 0.74, 0.28), fine=0.45, slow=0.25, rough=0.55, metal=0.0, open=2),
                        dict(name="steel", test=lambda Hd, S, V, Vb: V >= 0.0, fine=0.35, sat=0.3, rough=0.35, metal=1.0)]),
}
names = a[2:] or list(PROPS)
IDX = {"X": 0, "Y": 1, "Z": 2}

def axis_matrix(ax):
    # the model's own x, y, z -> game axes
    m = Matrix.Identity(3)
    for col, s in enumerate(ax):
        sign = -1.0 if s[0] == "-" else 1.0
        for row in range(3): m[row][col] = 0.0
        m[IDX[s[-1]]][col] = sign
    return m

def shrink(img, size, path, srgb=True):
    im = img if img.name.startswith("Graded") else img.copy()   # (a copy of a made-up picture comes out black)
    im.scale(size, size)
    im.filepath_raw = path
    im.file_format = "PNG"
    im.save()

def steel_uv(pix, W, H, normal):
    # a calm spot of steel in the model's own picture, as bright as its steel usually is and with a flat normal: metal parts added here all take their colour from it
    import numpy as np
    arr = np.array(pix, dtype=np.float32).reshape(H, W, 4)[..., :3]
    V = arr.max(axis=2); S = (V - arr.min(axis=2)) / np.maximum(V, 1e-4)
    steel = (S < 0.2) & (V > 0.03)   # (not the black around a baked picture's islands)
    if not steel.any(): steel = S < 0.2
    med = float(np.median(V[steel]))
    k = 6
    def box(a_):
        c = np.cumsum(np.cumsum(np.pad(a_, ((1, 0), (1, 0))), 0), 1)
        return (c[2 * k + 1:, 2 * k + 1:] - c[:-2 * k - 1, 2 * k + 1:] - c[2 * k + 1:, :-2 * k - 1] + c[:-2 * k - 1, :-2 * k - 1]) / float((2 * k + 1) ** 2)
    m1 = box(V.astype(np.float64)); m2 = box((V * V).astype(np.float64)); st = box(steel.astype(np.float64))
    var = np.maximum(m2 - m1 * m1, 0.0)
    score = np.sqrt(var) + np.abs(m1 - med) + (st < 0.999) * 10.0
    if normal is not None:
        nw, nh = normal.size
        na = np.array(normal.pixels[:], dtype=np.float32).reshape(nh, nw, 4)
        ys = (np.arange(k, H - k) * nh // H); xs = (np.arange(k, W - k) * nw // W)
        nn = na[ys][:, xs]
        score = score + (np.abs(nn[..., 0] - 0.5) + np.abs(nn[..., 1] - 0.5)) * 2.0
    y, x = np.unravel_index(int(np.argmin(score)), score.shape)
    y += k; x += k
    print("PROP steel spot", x, y, "value %.3f (steel median %.3f)" % (float(V[y, x]), med))
    return ((x + 0.5) / W, (y + 0.5) / H)

def add_metal(me, P, M3, pix, W, H, normal):
    # metal: whole parts that become metal (each a list of (axis, lo, hi) its face's centre must meet; "|Y|" is the distance from the middle along Y).
    # bands: (axis, lo, hi) slices cut cleanly into the model and raised by 'raise' cm as a metal band. Both take one even steel colour from the model's own picture.
    # Returns the faces that take the tier's colour.
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-3)   # (Meshy splits the mesh along its picture's seams: a band must come up as one piece)
    own = bm.loops.layers.float_vector["OwnN"]; uvl = bm.loops.layers.uv.active
    M3T = M3.transposed()
    def fnorm(f):
        n_ = Vector((0.0, 0.0, 0.0))
        for l in f.loops: n_ += (M3 @ Vector(l[own])).normalized()
        return n_.normalized()
    def inside(f, conds):
        c = f.calc_center_median(); n_ = None
        for ax, lo_, hi_ in conds:
            if ax.strip("|").startswith("n"):   # (which way the face looks: "nZ" 0.6..1 = facing up, "|nY|" 0.6..1 = facing either side)
                if n_ is None: n_ = fnorm(f)
                v_ = n_[IDX[ax.strip("|")[1:]]]
                if ax.startswith("|"): v_ = abs(v_)
            else:
                v_ = c[IDX[ax.strip("|")]]
                if ax.startswith("|"): v_ = abs(v_)
            if not (lo_ <= v_ <= hi_): return False
        return True
    def in_band(f, b):
        return inside(f, [b[:3]]) and inside(f, b[3] if len(b) > 3 else [])
    su = steel_uv(pix, W, H, normal)
    bands = P.get("bands", [])
    for b in bands:   # (cut along every edge of the band, so it comes up with straight clean edges; only where the band is, so the rest keeps its triangles)
        bax, blo, bhi = b[:3]; bi = IDX[bax.strip("|")]
        def near(f):
            vs = [abs(v.co[bi]) if bax.startswith("|") else v.co[bi] for v in f.verts]
            return min(vs) <= bhi + 0.01 and max(vs) >= blo - 0.01
        for ax, lo_, hi_ in [b[:3]] + [c_ for c_ in (b[3] if len(b) > 3 else []) if not c_[0].strip("|").startswith("n")]:
            i = IDX[ax.strip("|")]
            ds = [d for d in (lo_, hi_) if (d > 0.0 or not ax.startswith("|")) and abs(d) < 90.0]
            for d in (ds + [-d for d in ds] if ax.startswith("|") else ds):
                fs = [f for f in bm.faces if near(f)]
                es = list({e for f in fs for e in f.edges}); vs_ = list({v for f in fs for v in f.verts})
                co = Vector((0.0, 0.0, 0.0)); co[i] = d; no = Vector((0.0, 0.0, 0.0)); no[i] = 1.0
                bmesh.ops.bisect_plane(bm, geom=vs_ + es + fs, dist=1e-4, plane_co=co, plane_no=no)
    metal = set()
    for conds in P.get("metal", []):
        metal |= {f for f in bm.faces if inside(f, conds)}
    metal -= {f for f in metal if any(in_band(f, b) for b in bands)}
    raised = set()
    if bands:
        band = [f for f in bm.faces if any(in_band(f, b) for b in bands)]
        before = set(bm.faces) - set(band)
        res = bmesh.ops.extrude_face_region(bm, geom=band)
        top = {g for g in res["geom"] if isinstance(g, bmesh.types.BMFace)}
        walls = set(bm.faces) - before - top
        push = {}
        for f in top:
            for l in f.loops:
                push.setdefault(l.vert, Vector((0.0, 0.0, 0.0)))
                push[l.vert] += (M3 @ Vector(l[own])).normalized()
        rz = P.get("raise", 0.15)
        for v, d in push.items(): v.co += d.normalized() * rz
        bm.normal_update()
        for f in walls:
            # (a wall faces away from the band it rims)
            t_ = next((t for e in f.edges for t in e.link_faces if t in top), None)
            if t_ is not None and f.normal.dot(f.calc_center_median() - t_.calc_center_median()) < 0.0:
                f.normal_flip()
            n_ = M3T @ f.normal.normalized()
            for l in f.loops: l[own] = n_
        raised = top | walls
    for f in metal | raised:
        for l in f.loops: l[uvl].uv = su
    bm.faces.index_update()
    forced = {f.index for f in metal | raised}
    plain = {f.index for f in bm.faces if f.index not in forced and any(inside(f, conds) for conds in P.get("plain", []))}   # (parts that stay wood, whatever the picture painted there)
    print("PROP metal faces", len(metal), "band faces", len(raised), "plain faces", len(plain))
    bm.to_mesh(me); bm.free(); me.update()
    return forced, plain


def _tex_node(mat, sock_name):
    pr = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    stack = [pr.inputs[sock_name]]
    while stack:
        s = stack.pop()
        for l in s.links:
            n = l.from_node
            if n.type == "TEX_IMAGE": return n
            stack.extend(n.inputs)
    return None


def cull_hidden(me, name):
    # Faces no ray from them can leave the model by (either side, a spread of 14 directions): the inner walls of a thick shell and anything sealed inside.
    # A heavy collapse otherwise pushes those through the outside as shards.
    from mathutils.bvhtree import BVHTree
    bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table()
    tree = BVHTree.FromBMesh(bm)
    ext = max((max(v.co[i] for v in bm.verts) - min(v.co[i] for v in bm.verts)) for i in range(3))
    eps = ext * 2e-4; far = ext * 3.0
    ring = [(math.cos(k * math.pi / 3.0), math.sin(k * math.pi / 3.0)) for k in range(6)]
    gone = []
    for f in bm.faces:
        n = f.normal
        if n.length < 0.5: continue
        c = f.calc_center_median()
        t1 = n.orthogonal().normalized(); t2 = n.cross(t1)
        seen = False
        for s in (1.0, -1.0):
            nn = n * s
            for d in [nn] + [(nn * 0.64 + (t1 * ca + t2 * sa) * 0.77).normalized() for ca, sa in ring]:
                if tree.ray_cast(c + d * eps, d, far)[0] is None: seen = True; break
            if seen: break
        if not seen: gone.append(f)
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    bm.to_mesh(me); bm.free(); me.update()
    print("PROP", name, "hidden faces removed", len(gone))
def bake_to_new_uvs(o, hi, name, size=2048, swatch=False, extra=None, repaint=None):
    # The decimated model gets its own fresh UVs and its pictures baked from the full model (a heavy collapse across the library models' many UV seams
    # smears the old picture into speckled shards). Base and metal/roughness through emission, the normal map as Cycles bakes it.
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"; sc.cycles.device = "CPU"; sc.cycles.samples = 1
    me = o.data
    old = [l.name for l in me.uv_layers]
    uvb = me.uv_layers.new(name="Bake"); me.uv_layers.active = uvb
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.01)   # (2026-11-21: 0.004 left 4 px between islands at 1024, 0 px by the 256 mip: neighbours bled in as cracks)
    bpy.ops.object.mode_set(mode="OBJECT")
    hmat = hi.data.materials[0]
    tb = _tex_node(hmat, "Base Color"); tn = _tex_node(hmat, "Normal"); tm = _tex_node(hmat, "Metallic") or _tex_node(hmat, "Roughness")
    lmat = bpy.data.materials.new("Baked"); lmat.use_nodes = True
    me.materials.clear(); me.materials.append(lmat)
    ln = lmat.node_tree.nodes; lpr = next(n for n in ln if n.type == "BSDF_PRINCIPLED")
    tgt = ln.new("ShaderNodeTexImage")
    def img(tag, sz, noncolor):
        im = bpy.data.images.new("Graded%s_%s" % (tag, name), sz, sz, alpha=True)   # ("Graded": shrink() scales it in place)
        if noncolor: im.colorspace_settings.name = "Non-Color"
        return im
    pts = [v.co for v in me.vertices]
    diag = (Vector([max(p[i] for p in pts) for i in range(3)]) - Vector([min(p[i] for p in pts) for i in range(3)])).length
    hi.select_set(True); o.select_set(True); bpy.context.view_layer.objects.active = o
    kw = dict(use_selected_to_active=True, cage_extrusion=0.01 * diag, max_ray_distance=0.03 * diag, margin=32, margin_type="EXTEND", use_clear=True)   # (2026-11-21: EXTEND fills round each island with its own edge colour, not black, so mips and texture filtering never pull black or a neighbour's colour into a seam)
    got = {}
    if repaint:
        # (repaint, 2026-11-21, the kite: new pictures drawn from the model's shape, Tools/prop_repaint.py. Measured and drawn before anything is baked)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import prop_repaint
        rp_ai = IDX[repaint["front"][-1]]
        if not repaint["front"].startswith("-"): raise RuntimeError("PROP %s repaint: a front on the high side of its axis is not written yet" % name)
        rp_f, rp_b, rp_fr = prop_repaint.height_fields(hi.data, rp_ai, repaint.get("rows", 2048))
        rp_F, rp_B = prop_repaint.design(rp_f, rp_b, rp_fr, repaint["length"], name, repaint.get("look"))
        # (the normal map from the model's shape alone: its own normal picture is all scratches and pitting, the smudged look in the light)
        for l_ in list(hmat.node_tree.links):
            if l_.to_socket.name == "Normal" and l_.to_node.type == "BSDF_PRINCIPLED": hmat.node_tree.links.remove(l_)
    if tn:
        im = img("Normal", repaint.get("normal", size // 2) if repaint else size // 2, True); tgt.image = im; ln.active = tgt
        bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", **kw); got["Normal"] = im
    out_node = next(n for n in hmat.node_tree.nodes if n.type == "OUTPUT_MATERIAL")
    em = hmat.node_tree.nodes.new("ShaderNodeEmission")
    hmat.node_tree.links.new(em.outputs[0], out_node.inputs["Surface"])
    for tag, tnode, sz, nc in (("Base", tb, size, False), ("MR", tm, size // 2, True)):
        if not tnode: continue
        hmat.node_tree.links.new(tnode.outputs["Color"], em.inputs["Color"])
        im = img(tag, sz, nc); tgt.image = im; ln.active = tgt
        bpy.ops.object.bake(type="EMIT", **kw); got[tag] = im
    for tag, eim in ({} if repaint else (extra or {})).items():   # (more pictures laid on the full model, baked the same way: the shields' tier mask)
        en = hmat.node_tree.nodes.new("ShaderNodeTexImage"); en.image = eim
        hmat.node_tree.links.new(en.outputs["Color"], em.inputs["Color"])
        im = img(tag, size // 2, True); tgt.image = im; ln.active = tgt
        bpy.ops.object.bake(type="EMIT", **kw); got[tag] = im
    if repaint:
        socks = prop_repaint.lay_on(hmat, rp_F, rp_B, rp_f, rp_b, rp_fr, name)
        sc.cycles.samples = 16   # (several rays a pixel: a soft one-pixel edge, not steps)
        for tag, sz, nc in (("Base", repaint.get("tex", size), False), ("MR", size // 2, True), ("Mask", repaint.get("tex", size), True)):
            hmat.node_tree.links.new(socks[tag], em.inputs["Color"])
            im = img(tag, sz, nc); tgt.image = im; ln.active = tgt
            bpy.ops.object.bake(type="EMIT", **kw); got[tag] = im
        sc.cycles.samples = 1
        extra = {"Mask": None}
        if "Normal" in got and repaint.get("calm", 0.85) > 0.0 and got["Normal"].size[0] == got["MR"].size[0]:
            # (the painted face is clean, so its dents, cracks and scratches go from the normal map too, all but a hint: the face is where the new
            # metal picture says no metal, a few pixels in from the steel so the foot of the cross and of the rim keep their shape)
            import numpy as np
            nw = got["Normal"].size[0]
            na = np.array(got["Normal"].pixels[:], dtype=np.float32).reshape(nw, nw, 4)
            ma = np.array(got["MR"].pixels[:], dtype=np.float32).reshape(nw, nw, 4)
            face = (ma[..., 2] < 0.5).astype(np.float32)
            for _i in range(3): face = np.minimum.reduce([face, np.roll(face, 1, 0), np.roll(face, -1, 0), np.roll(face, 1, 1), np.roll(face, -1, 1)])
            for _i in range(2): face = (face + np.roll(face, 1, 0) + np.roll(face, -1, 0) + np.roll(face, 1, 1) + np.roll(face, -1, 1)) / 5.0
            k_ = (face * repaint.get("calm", 0.85))[..., None]
            na[..., :3] = na[..., :3] * (1.0 - k_) + np.array([0.5, 0.5, 1.0], dtype=np.float32) * k_
            got["Normal"].pixels.foreach_set(na.reshape(-1)); got["Normal"].update()
            print("PROP", name, "face calmed in the normal map: %.0f%% of it" % (100.0 * float(face.mean())))
    if swatch and "Base" in got:
        # (a model with no steel of its own, the longbow: added bands need a calm steel spot to take their colour from, and a baked picture is black between its
        # islands. A small steel-grey square goes into an empty corner of every picture, for steel_uv to find)
        import numpy as np
        bw = got["Base"].size[0]; ba = np.array(got["Base"].pixels[:], dtype=np.float32).reshape(bw, bw, 4)
        q = bw // 32; spot = None
        for cy in range(bw // q - 1, -1, -1):
            for cx in range(bw // q - 1, -1, -1):
                if ba[cy * q:(cy + 1) * q, cx * q:(cx + 1) * q, :3].max() < 0.004: spot = (cx, cy); break
            if spot: break
        if spot:
            for tag, col in (("Base", (0.45, 0.46, 0.48, 1.0)), ("MR", (0.0, 0.35, 1.0, 1.0)), ("Normal", (0.5, 0.5, 1.0, 1.0))):
                if tag not in got: continue
                w_ = got[tag].size[0]; q_ = w_ // 32
                a_ = np.array(got[tag].pixels[:], dtype=np.float32).reshape(w_, w_, 4)
                a_[spot[1] * q_:(spot[1] + 1) * q_, spot[0] * q_:(spot[0] + 1) * q_] = col
                got[tag].pixels.foreach_set(a_.reshape(-1)); got[tag].update()
            print("PROP", name, "steel swatch at square", spot)
    ln.remove(tgt)
    def tex(im):
        n = ln.new("ShaderNodeTexImage"); n.image = im; return n
    lk = lmat.node_tree.links
    if "Base" in got: lk.new(tex(got["Base"]).outputs["Color"], lpr.inputs["Base Color"])
    if "Normal" in got:
        nm = ln.new("ShaderNodeNormalMap"); lk.new(tex(got["Normal"]).outputs["Color"], nm.inputs["Color"]); lk.new(nm.outputs[0], lpr.inputs["Normal"])
    if "MR" in got: lk.new(tex(got["MR"]).outputs["Color"], lpr.inputs["Roughness"])
    extra_got = {t: got[t] for t in (extra or {}) if t in got}
    for n_ in old: me.uv_layers.remove(me.uv_layers[n_])
    bpy.data.objects.remove(hi, do_unlink=True)
    bpy.ops.object.select_all(action="DESELECT"); o.select_set(True); bpy.context.view_layer.objects.active = o
    print("PROP", name, "baked", ", ".join(sorted(got)))
    return extra_got
def zone_classes(me, P, name, side):
    # zones (2026-11-16, the kite, review round 2): its face has bright worn chips the same steel grey as the rim and cross, raised like them too, so
    # neither colour nor shape tells them apart; where they are does. Each face of the model is placed on the shield's front (u: -1..1 across, v: 0
    # bottom .. 1 top, rim: how near the outline it is, 0..1 of the half width) and P["zones"](u, v, rim) says whether metal may be there; elsewhere
    # its pixels are forced to class P["zone_else"] (the face). Everything else is decided by colour as before.
    import numpy as np
    fax = P["relief"][2] if "relief" in P else "-Y"
    ai = IDX[fax[-1]]
    ui, vi = [i for i in range(3) if i != ai and i != 2][0], 2
    co = [v.co for v in me.vertices]
    u0, u1 = min(c[ui] for c in co), max(c[ui] for c in co); v0, v1 = min(c[vi] for c in co), max(c[vi] for c in co)
    hw = (u1 - u0) * 0.5; uc = (u0 + u1) * 0.5
    nb = 64
    rowmax = [0.0] * (nb + 1); colmax = [-1e9] * (nb + 1); colmin = [1e9] * (nb + 1)
    for c in co:
        r_ = int((c[vi] - v0) / (v1 - v0) * nb); k_ = int((c[ui] - u0) / (u1 - u0) * nb)
        rowmax[r_] = max(rowmax[r_], abs(c[ui] - uc)); colmax[k_] = max(colmax[k_], c[vi]); colmin[k_] = min(colmin[k_], c[vi])
    uvl = me.uv_layers.active.data
    force = np.full((side, side), -1, dtype=np.int32)
    n_free = 0
    for poly in me.polygons:
        c = poly.center
        u = (c[ui] - uc) / hw; v = (c[vi] - v0) / (v1 - v0)
        r_ = int(v * nb); k_ = int((c[ui] - u0) / (u1 - u0) * nb)
        edge = min(rowmax[r_] - abs(c[ui] - uc), colmax[k_] - c[vi], c[vi] - colmin[k_])
        if P["zones"](u, v, 1.0 - min(1.0, edge / hw)): continue
        n_free += 1
        q = [(uvl[li_].uv[0] * side - 0.5, uvl[li_].uv[1] * side - 0.5) for li_ in poly.loop_indices]
        for k2 in range(1, len(q) - 1):
            (x0, y0), (x1, y1), (x2, y2) = q[0], q[k2], q[k2 + 1]
            den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(den) < 1e-9: continue
            xa = max(0, int(math.floor(min(x0, x1, x2)))); xb = min(side - 1, int(math.ceil(max(x0, x1, x2))))
            ya = max(0, int(math.floor(min(y0, y1, y2)))); yb = min(side - 1, int(math.ceil(max(y0, y1, y2))))
            if xb < xa or yb < ya: continue
            gx, gy = np.meshgrid(np.arange(xa, xb + 1), np.arange(ya, yb + 1))
            l0 = ((y1 - y2) * (gx - x2) + (x2 - x1) * (gy - y2)) / den
            l1 = ((y2 - y0) * (gx - x2) + (x0 - x2) * (gy - y2)) / den
            ok = (l0 >= -0.02) & (l1 >= -0.02) & (1.0 - l0 - l1 >= -0.02)
            force[ya:yb + 1, xa:xb + 1][ok] = P["zone_else"]
    print("PROP", name, "zones: %d of %d faces kept plain" % (n_free, len(me.polygons)))
    return force

def relief_classes(me, P, name, side):
    # relief (2026-11-16, the tower shield, review round 2): its picture can't tell the weathered grey planks from the worn bronze (both pale, streaked,
    # in hundreds of tiny islands), but its shape can: the frame, bosses and studs stand out from the planks. Each vertex's height above the lowest
    # surface within 'reach' (a share of the model's height) of it, on the side it faces; raised or edge-on faces are class 0, the rest class 1,
    # drawn into the model's own UV space. P["relief"] = (reach share, raised share, front axis as "-Y" etc.)
    import numpy as np
    from mathutils import kdtree
    reach_s, raised_s, fax = P["relief"][:3]
    c_up, c_flat = (P["relief"][3], P["relief"][4]) if len(P["relief"]) > 4 else (0, 1)   # (the classes for raised and flat)
    use_mean = len(P["relief"]) > 5 and P["relief"][5] == "mean"
    ai = IDX[fax[-1]]; sg = -1.0 if fax.startswith("-") else 1.0
    co = [v.co.copy() for v in me.vertices]
    span = max(max(c[i] for c in co) - min(c[i] for c in co) for i in range(3))
    reach, raised = reach_s * span, raised_s * span
    kd = kdtree.KDTree(len(co))
    for i, c in enumerate(co):
        q = c.copy(); q[ai] = 0.0; kd.insert(q, i)
    kd.balance()
    vnorm = [v.normal[ai] * sg for v in me.vertices]
    rel = np.zeros(len(co)); side_ = np.zeros(len(co), dtype=bool)
    for i, c in enumerate(co):
        q = c.copy(); q[ai] = 0.0
        facing = 1.0 if vnorm[i] >= 0.0 else -1.0
        ds = [co[j][ai] * sg * facing for (_q, j, _d) in kd.find_range(q, reach) if vnorm[j] * facing > 0.3]
        if len(ds) < 4: continue
        ds.sort()
        rel[i] = c[ai] * sg * facing - (sum(ds) / len(ds) if use_mean else ds[len(ds) // 3])   # (mean: a domed or sloped face averages out; else a third of the way up, the grooves between the planks are not the planks)
        side_[i] = abs(vnorm[i]) < 0.35
    uvl = me.uv_layers.active.data
    force = np.full((side, side), -1, dtype=np.int32)
    n_raised = 0
    for poly in me.polygons:
        vs = list(poly.vertices)
        up = sum(1 for v in vs if rel[v] > raised or side_[v]) * 2 > len(vs)
        n_raised += up
        q = [(uvl[li_].uv[0] * side - 0.5, uvl[li_].uv[1] * side - 0.5) for li_ in poly.loop_indices]
        for k_ in range(1, len(q) - 1):
            (x0, y0), (x1, y1), (x2, y2) = q[0], q[k_], q[k_ + 1]
            den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(den) < 1e-9: continue
            xa = max(0, int(math.floor(min(x0, x1, x2)))); xb = min(side - 1, int(math.ceil(max(x0, x1, x2))))
            ya = max(0, int(math.floor(min(y0, y1, y2)))); yb = min(side - 1, int(math.ceil(max(y0, y1, y2))))
            if xb < xa or yb < ya: continue
            gx, gy = np.meshgrid(np.arange(xa, xb + 1), np.arange(ya, yb + 1))
            l0 = ((y1 - y2) * (gx - x2) + (x2 - x1) * (gy - y2)) / den
            l1 = ((y2 - y0) * (gx - x2) + (x0 - x2) * (gy - y2)) / den
            ok = (l0 >= -0.02) & (l1 >= -0.02) & (1.0 - l0 - l1 >= -0.02)
            force[ya:yb + 1, xa:xb + 1][ok] = c_up if up else c_flat
    print("PROP", name, "relief: %d of %d faces raised (reach %.3f, raised %.4f model units)" % (n_raised, len(me.polygons), reach, raised))
    return force

def paint_pixels(P, name, arr, mr, force=None):
    # (the paint classes on one picture, arr H x W x 4 floats; returns the repainted picture, the metal/roughness picture, the class map and the used pixels)
    import numpy as np
    H, W = arr.shape[0], arr.shape[1]
    rgb = arr[..., :3]
    V = rgb.max(axis=2); Cc = V - rgb.min(axis=2); S = Cc / np.maximum(V, 1e-4)
    Hd = np.zeros_like(V)
    r_, g_c, b_ = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx_ = np.maximum(Cc, 1e-6)
    Hd = np.where(V == r_, ((g_c - b_) / mx_) % 6.0, np.where(V == g_c, (b_ - r_) / mx_ + 2.0, (r_ - g_c) / mx_ + 4.0)) * 60.0
    used = V > 0.012
    def pblur(a_, rad):
        for ax in (0, 1):
            c = np.cumsum(a_, axis=ax, dtype=np.float64)
            n = a_.shape[ax]
            hi_ = np.minimum(np.arange(n) + rad, n - 1); lo_ = np.maximum(np.arange(n) - rad - 1, -1)
            t = np.take(c, hi_, axis=ax); b_v = np.take(c, np.maximum(lo_, 0), axis=ax)
            shape = [1, 1]; shape[ax] = n
            a_ = t - np.where(lo_.reshape(shape) < 0, 0.0, b_v)
        return a_
    near = P.get("near", max(2, W // 160))
    cnt0 = pblur(used.astype(np.float64), near)
    Vb = pblur(np.where(used, V, 0.0), near) / np.maximum(cnt0, 1.0)
    _SB[0] = pblur(np.where(used, S, 0.0), near * 2) / np.maximum(pblur(used.astype(np.float64), near * 2), 1.0)
    cls_map = np.full((H, W), -1, dtype=np.int32)
    if force is not None:   # (classes already known by pixel, from the model's shape: relief)
        cls_map[used & (force >= 0)] = force[used & (force >= 0)]
    for k, cl in enumerate(P["paint"]):
        m = used & (cls_map < 0) & cl["test"](Hd, S, V, Vb)
        cls_map[m] = k
    # vote: each pixel takes the class most of its neighbours within 'vote' pixels have (blotches and specks smaller than that go)
    if P.get("vote"):
        sc_ = np.stack([pblur((cls_map == k).astype(np.float64), P["vote"]) for k in range(len(P["paint"]))], axis=0)
        cls_map = np.where(used, np.argmax(sc_, axis=0).astype(np.int32), -1)
    # open: a class's specks and thin streaks narrower than about 2 x open pixels (a scratch across the face read as steel) go to class 'spill'
    def _grow(a_, r_, op):
        for _i in range(r_):
            a_ = op.reduce([a_, np.roll(a_, 1, 0), np.roll(a_, -1, 0), np.roll(a_, 1, 1), np.roll(a_, -1, 1)])
        return a_
    for k, cl in enumerate(P["paint"]):
        if not cl.get("open"): continue
        m = cls_map == k
        kept = _grow(_grow(m, cl["open"], np.logical_and), cl["open"], np.logical_or) & m
        cls_map[m & ~kept] = cl.get("spill", len(P["paint"]) - 1)
        print("PROP %s paint class %d: %.1f%% of its pixels were specks" % (name, k, 100.0 * (m & ~kept).sum() / max(1, m.sum())))
    outc = rgb.copy()
    rad = max(3, W // 48)
    for k, cl in enumerate(P["paint"]):
        m = cls_map == k
        if not m.any(): continue
        wt_ = pblur(m.astype(np.float64), rad)
        slow = pblur(np.where(m, V, 0.0), rad) / np.maximum(wt_, 1.0)
        fine = np.where(m, V / np.maximum(slow, 0.01), 1.0)
        mean_ = max(float(V[m].mean()), 0.01)
        to = cl.get("to")
        if to is not None:
            h0, s0, v0 = to
            Vn = v0 * np.power(np.maximum(slow, 0.01) / mean_, cl.get("slow", 0.3)) * np.power(fine, cl.get("fine", 0.2))
            dh = ((Hd - h0 + 180.0) % 360.0) - 180.0
            Hn = (h0 + cl.get("hue_keep", 0.1) * np.clip(dh, -40.0, 40.0)) % 360.0
            Sn = np.clip(s0 + 0.15 * (S - s0), 0.0, 1.0) if s0 > 0.02 else np.full_like(S, s0)
        else:
            Vn = slow * np.power(fine, cl.get("fine", 0.2)) * cl.get("gain", 1.0)
            Hn = Hd; Sn = S * cl.get("sat", 1.0)
        Vn = np.clip(Vn, 0.0, 1.0)
        h6 = Hn / 60.0
        Xh = 1.0 - np.abs(h6 % 2.0 - 1.0)
        Cn = Vn * Sn; Xn = Cn * Xh; mn = Vn - Cn
        seg = np.floor(h6).astype(np.int32) % 6
        R = np.choose(seg, [Cn, Xn, 0 * Cn, 0 * Cn, Xn, Cn]); G = np.choose(seg, [Xn, Cn, Cn, Xn, 0 * Cn, 0 * Cn]); B = np.choose(seg, [0 * Cn, 0 * Cn, Xn, Cn, Cn, Xn])
        outc[..., 0] = np.where(m, R + mn, outc[..., 0]); outc[..., 1] = np.where(m, G + mn, outc[..., 1]); outc[..., 2] = np.where(m, B + mn, outc[..., 2])
        print("PROP %s paint class %d (%s): %.1f%% of the picture" % (name, k, cl.get("name", ""), 100.0 * m.sum() / max(1, used.sum())))
    arr[..., :3] = outc
    # the metal/roughness picture: the classes' own roughness and metal (G and B)
    if any("rough" in cl or "metal" in cl for cl in P["paint"]):
        if mr is not None:
            MW, MH = mr.size
            ma = np.array(mr.pixels[:], dtype=np.float32).reshape(MH, MW, 4)
        else:
            MW, MH = W // 2, H // 2
            ma = np.zeros((MH, MW, 4), dtype=np.float32); ma[..., 1] = 0.6; ma[..., 3] = 1.0
        cm = cls_map[np.ix_(np.arange(MH) * H // MH, np.arange(MW) * W // MW)]
        for k, cl in enumerate(P["paint"]):
            if "rough" in cl: ma[..., 1][cm == k] = cl["rough"]
            if "metal" in cl: ma[..., 2][cm == k] = cl["metal"]
        mrc = bpy.data.images.new("GradedMRPaint", MW, MH, alpha=True)
        mrc.colorspace_settings.name = "Non-Color"
        mrc.pixels.foreach_set(ma.reshape(-1)); mrc.update(); mr = mrc
    return arr, mr, cls_map, used

for name in names:
    P = PROPS[name]
    f = os.path.join(src, P["file"])
    if not os.path.exists(f):
        print("PROP missing", f); continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f)
    o = next(x for x in bpy.data.objects if x.type == "MESH")
    bpy.context.view_layer.objects.active = o; o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    me = o.data
    tris0 = sum(len(p.vertices) - 2 for p in me.polygons)
    if "cut" in P:
        bm = bmesh.new(); bm.from_mesh(me)
        gone = [f for f in bm.faces if P["cut"](f.calc_center_median())]
        bmesh.ops.delete(bm, geom=gone, context="FACES")
        bm.to_mesh(me); bm.free(); me.update()
        print("PROP", name, "cut", len(gone), "faces")
    # src_paint (2026-11-16, review round 2): the paint classes run on the model's OWN picture, before the bake. Its islands are whole parts (the face, the
    # frame, a plank), so a class is judged with its neighbours; the baked picture of a 6000-triangle model is hundreds of tiny islands, where a scratch or
    # a worn edge was judged alone and came out as chalky silver blotches on the face and planks. The tier mask is then baked like the colour.
    src_mask = None
    if P.get("src_paint") and "paint" in P and P.get("bake"):
        import numpy as np
        smat = me.materials[0]; spr = next(n for n in smat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
        def _src_node(sock_name):
            stack = [spr.inputs[sock_name]]
            while stack:
                s = stack.pop()
                for l in s.links:
                    if l.from_node.type == "TEX_IMAGE": return l.from_node
                    stack.extend(i for i in l.from_node.inputs)
            return None
        bn = _src_node("Base Color"); mn_ = _src_node("Metallic") or _src_node("Roughness")
        side = min(2048, bn.image.size[0])
        sb = bn.image.copy(); sb.scale(side, side)
        sarr = np.array(sb.pixels[:], dtype=np.float32).reshape(side, side, 4)
        smr = None
        if mn_:
            smr = mn_.image.copy(); smr.scale(side // 2, side // 2)
        force = relief_classes(me, P, name, side) if P.get("relief") else (zone_classes(me, P, name, side) if P.get("zones") else None)
        sarr, smr, scls, sused = paint_pixels(P, name, sarr, smr, force)
        gb = bpy.data.images.new("SrcPaint_%s" % name, side, side, alpha=True)
        gb.pixels.foreach_set(sarr.reshape(-1)); gb.update(); bn.image = gb
        if mn_: mn_.image = smr
        else:   # (no metal/roughness picture of its own: the new one feeds Roughness, where the bake looks for it)
            tn_ = smat.node_tree.nodes.new("ShaderNodeTexImage"); tn_.image = smr
            smat.node_tree.links.new(tn_.outputs["Color"], spr.inputs["Roughness"])
        mk_ = np.isin(scls, list(P.get("tier", []))).astype(np.float32)
        for _d in range(4):   # (into the margin round each island, so the bake's edge samples don't pull a tinted part toward untinted)
            nb_ = np.maximum.reduce([np.roll(mk_, 1, 0), np.roll(mk_, -1, 0), np.roll(mk_, 1, 1), np.roll(mk_, -1, 1)])
            mk_ = np.where(sused, mk_, np.maximum(mk_, nb_))
        ma_ = np.zeros((side, side, 4), dtype=np.float32); ma_[..., 0] = mk_; ma_[..., 1] = mk_; ma_[..., 2] = mk_; ma_[..., 3] = 1.0
        src_mask = bpy.data.images.new("SrcMask_%s" % name, side, side, alpha=True); src_mask.colorspace_settings.name = "Non-Color"
        src_mask.pixels.foreach_set(ma_.reshape(-1)); src_mask.update()
        print("PROP", name, "painted its own %dpx picture; tier mask %.0f%% of it" % (side, 100.0 * mk_[sused].mean()))
    baked_extra = None
    if "reduce" in P:
        tris_now = sum(len(p.vertices) - 2 for p in me.polygons)
        if tris_now > P["reduce"]:
            hi = None
            if P.get("bake", True):
                # (the full model stays for the bake; the copy that is reduced is welded first, since its old UV seams no longer matter and split edges stop the collapse)
                hi = o.copy(); hi.data = o.data.copy(); bpy.context.scene.collection.objects.link(hi)
                if P.get("cull"): cull_hidden(me, name)   # (off by default: welding and baking alone came out cleaner on the library models)
                bm = bmesh.new(); bm.from_mesh(me)
                ext = max((max(v.co[i] for v in bm.verts) - min(v.co[i] for v in bm.verts)) for i in range(3))
                bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=ext * 2e-5)
                if P.get("fill"):
                    # (the cut-away string left an open, ragged crater in each tip where it was tied on: real holes, the inside showing. Welded, the only open
                    # edges left are those rims, so they are closed here)
                    rim = [e for e in bm.edges if e.is_boundary]
                    made = bmesh.ops.holes_fill(bm, edges=rim, sides=0)["faces"] if rim else []
                    if made: bmesh.ops.triangulate(bm, faces=made)
                    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
                    print("PROP", name, "open edges", len(rim), "closed with", len(made), "faces; still open", sum(1 for e in bm.edges if e.is_boundary))
                bm.to_mesh(me); bm.free(); me.update()
                tris_now = sum(len(p.vertices) - 2 for p in me.polygons)
            bpy.ops.object.select_all(action="DESELECT"); bpy.context.view_layer.objects.active = o; o.select_set(True)
            md = o.modifiers.new("Dec", "DECIMATE"); md.decimate_type = "COLLAPSE"; md.ratio = min(1.0, P["reduce"] / float(tris_now)); md.use_collapse_triangulate = True
            bpy.ops.object.modifier_apply(modifier=md.name)
            me = o.data
            if hi:
                try:
                    bpy.ops.mesh.customdata_custom_splitnormals_clear()
                except Exception as e_: print("PROP", name, "custom normals kept:", e_)
                me.shade_smooth(); me.set_sharp_from_angle(angle=math.radians(50.0))
                baked_extra = bake_to_new_uvs(o, hi, name, swatch=bool(P.get("swatch")), extra={"Mask": src_mask} if src_mask else None,
                                              repaint=dict(P["repaint"], length=P["length"]) if P.get("repaint") else None)
                me = o.data
    print("PROP", name, "tris from", tris0, "to", sum(len(p.vertices) - 2 for p in me.polygons))
    # (the model's own normals, by corner: moving the vertices below doesn't turn them, and the game then lights the model from the wrong side. Kept as a corner attribute so cuts and bands carry them along)
    own = me.attributes.new("OwnN", "FLOAT_VECTOR", "CORNER")
    for li_ in range(len(me.loops)): own.data[li_].vector = me.corner_normals[li_].vector.copy()
    # the pictures, by what they feed
    mat = me.materials[0]
    nodes = mat.node_tree.nodes
    pr = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    def tex_of(sock):
        seen = []; stack = [sock]
        while stack:
            s = stack.pop()
            for l in s.links:
                n = l.from_node
                if n.type == "TEX_IMAGE": return n.image
                stack.extend(i for i in n.inputs)
        return None
    base = tex_of(pr.inputs["Base Color"]); normal = tex_of(pr.inputs["Normal"]); mr = tex_of(pr.inputs["Metallic"]) or tex_of(pr.inputs["Roughness"])
    W, H = base.size
    pix = list(base.pixels)
    def colour_at(u, v):
        x = min(W - 1, max(0, int((u % 1.0) * W))); y = min(H - 1, max(0, int((v % 1.0) * H)))
        i = (y * W + x) * 4
        return pix[i], pix[i + 1], pix[i + 2]
    # into the game's frame, at the right size, grip at the origin
    M3 = axis_matrix(P["axes"])
    if abs(M3.determinant() - 1.0) > 1e-3: raise RuntimeError("PROP %s: axes %s mirror the model" % (name, P["axes"]))
    model_co = [v.co.copy() for v in me.vertices]   # (for the marks)
    for v in me.vertices: v.co = M3 @ v.co
    xs = [v.co for v in me.vertices]
    lo = Vector((min(c[i] for c in xs) for i in range(3))); hi = Vector((max(c[i] for c in xs) for i in range(3)))
    S = P["length"] / max(hi - lo)
    long_ax, share = P["grip"]; li = IDX[long_ax]
    g = Vector(((lo[i] + hi[i]) * 0.5 for i in range(3)))
    g[li] = lo[li] + (hi[li] - lo[li]) * share
    band = [c for c in xs if abs(c[li] - g[li]) < 0.03 * (hi[li] - lo[li])]   # (centred on what the hand actually holds there)
    if band:
        for i in range(3):
            if i != li: g[i] = (min(c[i] for c in band) + max(c[i] for c in band)) * 0.5
    for i, sh_ in enumerate(P.get("pivot", (None, None, None))):   # (shields: the origin at these shares of the box, 0 = low side, 1 = high side)
        if sh_ is not None: g[i] = lo[i] + (hi[i] - lo[i]) * sh_
    for v in me.vertices: v.co = (v.co - g) * S
    if "girth" in P:   # (2026-11-18, staff and wand: a little bolder than life; x the width and depth about the long axis through the grip)
        for v in me.vertices:
            for i_ in range(3):
                if i_ != li: v.co[i_] *= P["girth"]
    # one gentle arc along the long axis: take out the stave's own bends, tips toward +axis
    if "bend" in P:
        bi = IDX[P["bend"][0]]; depth = P["bend"][1]
        half = P["length"] * 0.5
        N = 48
        bins = [[] for _ in range(N + 1)]
        for v in me.vertices:
            t = max(0.0, min(1.0, (v.co[li] + half) / (2.0 * half)))
            bins[int(round(t * N))].append(v.co[bi])
        centre = [(sum(b) / len(b)) if b else None for b in bins]
        known = [i for i, c in enumerate(centre) if c is not None]
        for i in range(N + 1):
            if centre[i] is None:
                j = min(known, key=lambda k: abs(k - i)); centre[i] = centre[j]
        # (smoothed, so details on the stave stay)
        smooth = [sum(centre[i + k] for k in (-2, -1, 0, 1, 2) if 0 <= i + k <= N) / sum(1 for k in (-2, -1, 0, 1, 2) if 0 <= i + k <= N) for i in range(N + 1)]
        thick = P.get("thick", 1.0)
        oi = 3 - li - bi
        for v in me.vertices:
            x = v.co[li]; t = max(0.0, min(1.0, (x + half) / (2.0 * half)))
            f_ = t * N; i0 = min(N - 1, int(f_)); c = smooth[i0] + (smooth[i0 + 1] - smooth[i0]) * (f_ - i0)
            v.co[bi] = (v.co[bi] - c) * thick + depth * (x * x) / (half * half)
            v.co[oi] *= thick
    if "squeeze" in P:
        a0, a1, sq = P["squeeze"]
        lo2 = min(v.co[li] for v in me.vertices); hi2 = max(v.co[li] for v in me.vertices)
        for v in me.vertices:
            t = max(0.0, min(1.0, ((v.co[li] - lo2) / (hi2 - lo2) - a0) / (a1 - a0)))
            k = 1.0 + (sq - 1.0) * t * t * (3.0 - 2.0 * t)
            for i in range(3):
                if i != li: v.co[i] *= k
    if "widen" in P:
        wi = IDX[P["widen"][0]]
        for v in me.vertices: v.co[wi] *= P["widen"][1]
    if "neck" in P:
        ni = IDX[P["neck"][0]]
        for v in me.vertices:
            if v.co[ni] < P["neck"][1]: v.co[ni] -= P["neck"][2]
    if "stretch" in P:
        si_ = IDX[P["stretch"][0]]
        for v in me.vertices:
            if v.co[si_] > P["stretch"][1]: v.co[si_] += P["stretch"][2]
    for mk, (mlo, mhi) in P.get("marks", {}).items():
        sel = [me.vertices[i].co for i, c in enumerate(model_co) if all(mlo[k] <= c[k] <= mhi[k] for k in range(3))]
        if sel:
            mid = sum(sel, Vector((0.0, 0.0, 0.0))) / len(sel)
            print("PROP mark %s %s (%.1f, %.1f, %.1f) lo (%.1f, %.1f, %.1f) hi (%.1f, %.1f, %.1f) n %d" % (name, mk, mid.x, mid.y, mid.z,
                  min(c.x for c in sel), min(c.y for c in sel), min(c.z for c in sel), max(c.x for c in sel), max(c.y for c in sel), max(c.z for c in sel), len(sel)))
        else: print("PROP mark %s %s none" % (name, mk))
    if "blade" in P:
        si, ti = IDX[P["blade"][0]], IDX[P["blade"][1]]
        st, root_, tip_ = P["blade"][2:]
        far_ = max(abs(v.co[si]) for v in me.vertices)
        flat = max((abs(v.co[ti]) for v in me.vertices if abs(v.co[si]) > st + 2.0), default=1.0)
        for v in me.vertices:
            d = abs(v.co[si])
            if d > st:
                t = min(1.0, (d - st) / max(far_ - st, 1.0))
                k = min(1.0, (d - st) / 1.5)   # (eases out of the collar)
                want = (v.co[ti] / max(flat, 1e-4)) * (root_ + (tip_ - root_) * t)
                v.co[ti] = v.co[ti] + (want - v.co[ti]) * k
    # one rich brown for the wood
    if "wood" in P:
        import numpy as np
        hue_, sat_, val_, hue_max = P["wood"]
        arr = np.array(pix, dtype=np.float32).reshape(H, W, 4)
        r_, g_c, b_ = arr[..., 0], arr[..., 1], arr[..., 2]
        V = arr[..., :3].max(axis=2); C = V - arr[..., :3].min(axis=2)
        S = C / np.maximum(V, 1e-4)
        Hu = np.degrees(np.arctan2(np.sqrt(3.0) * (g_c - b_), 2.0 * r_ - g_c - b_)) % 360.0   # (near enough for browns)
        wood = (S > 0.35) & (V > 0.015) & ((Hu < hue_max) | (Hu > 340.0))
        fleck = None
        def blur(a_, rad):
            for ax in (0, 1):
                c = np.cumsum(a_, axis=ax, dtype=np.float64)
                n = a_.shape[ax]
                hi_ = np.minimum(np.arange(n) + rad, n - 1); lo_ = np.maximum(np.arange(n) - rad - 1, -1)
                t = np.take(c, hi_, axis=ax)
                b_v = np.take(c, np.maximum(lo_, 0), axis=ax)
                shape = [1, 1]; shape[ax] = n
                a_ = t - np.where(lo_.reshape(shape) < 0, 0.0, b_v)
            return a_
        rad = max(4, W // 28)
        wt = blur(wood.astype(np.float64), rad)
        slow = blur(V * wood, rad) / np.maximum(wt, 1.0)   # (brightness in the wood nearby, not counting what is not wood)
        fine = np.where(wood, V / np.maximum(slow, 0.02), 1.0)
        if P.get("unfleck"):
            share = wt / np.maximum(blur(np.ones_like(V, dtype=np.float64), rad), 1.0)
            fleck = (~wood) & (V > 0.015) & (S <= 0.35) & (share > P["unfleck"])
            wood = wood | fleck; fine = np.where(fleck, 1.0, fine)
            print("PROP", name, "unfleck: %.2f%% of the picture made wood" % (100.0 * fleck.mean()))
        mean_ = max(float(V[wood].mean()) if wood.any() else 0.4, 0.02)
        Vn = val_ * (1.0 + 0.6 * (fine - 1.0)) * np.power(np.maximum(slow, 0.02) / mean_, 0.25)
        dh = ((Hu + 180.0) % 360.0 - 180.0) - hue_
        Hn = np.radians(hue_ + 0.15 * np.clip(dh, -40.0, 40.0))
        Sn = np.clip(sat_ + 0.15 * (S - sat_), 0.0, 1.0)
        # (back to RGB from hue, saturation, value, for hues in the browns)
        Vn = np.clip(Vn, 0.0, 1.0)
        h6 = (np.degrees(Hn) % 360.0) / 60.0
        X_ = 1.0 - np.abs(h6 % 2.0 - 1.0)
        Cn = Vn * Sn; Xn = Cn * X_; m_ = Vn - Cn
        Rn = np.where(h6 < 1.0, Cn, np.where(h6 >= 5.0, Cn, Xn)); Gn = np.where(h6 < 1.0, Xn, np.where(h6 >= 5.0, 0.0, Cn)); Bn = np.where(h6 >= 5.0, Xn, 0.0)
        arr[..., 0] = np.where(wood, Rn + m_, r_); arr[..., 1] = np.where(wood, Gn + m_, g_c); arr[..., 2] = np.where(wood, Bn + m_, b_)
        pix = arr.reshape(-1).tolist()
        graded = bpy.data.images.new("Graded", W, H, alpha=True)   # (a packed picture copies from its file, so the change goes in a new one)
        graded.pixels.foreach_set(arr.reshape(-1))
        graded.update()
        base = graded
        print("PROP", name, "wood %.0f%% of the picture" % (100.0 * wood.mean()))
    # paint (2026-11-16, the library shields: clean off the grime and scratches, keep the design): each pixel of the picture goes to the first class
    # whose test (hue degrees, saturation, value, value blurred over 'near' pixels: numpy arrays) it passes; a class repaints its pixels in one clean
    # colour ('to': hue, saturation, value; None keeps each pixel's own hue and saturation), keeping 'slow' of the gentle shading and 'fine' of the
    # fine detail (scratches, grime and specks are fine detail: 0.1-0.3 cleans them off). 'rough'/'metal' set the class in the metal/roughness picture
    # (made if the model has none). tier: the classes that take the tier's colour (the mask, by class, decided once per face).
    cls_map = None
    if "paint" in P and not P.get("src_paint"):
        import numpy as np
        arr = np.array(pix, dtype=np.float32).reshape(H, W, 4)
        arr, mr, cls_map, used = paint_pixels(P, name, arr, mr)
        pix = arr.reshape(-1).tolist()
        graded = bpy.data.images.new("GradedPaint", W, H, alpha=True)
        graded.pixels.foreach_set(arr.reshape(-1)); graded.update(); base = graded
    def cls_at(u, v):
        x = min(W - 1, max(0, int((u % 1.0) * W))); y = min(H - 1, max(0, int((v % 1.0) * H)))
        return int(cls_map[y, x])
    tier_cls = set(P["tier"]) if "tier" in P else None
    # more metal (owner, 2026-11-11: the tier's colour should stand out; the same model in every tier): whole parts made metal, and raised metal bands
    forced = set(); plain = set()
    if "metal" in P or "bands" in P or "plain" in P:
        forced, plain = add_metal(me, P, M3, pix, W, H, normal)
    # refine: faces the picture's metal/wood edge runs through are split, 'refine' times over, so the per-face mask (clean) follows the edge closely
    # instead of drawing teeth across big triangles (the bow's fittings, 2026-11-13). Only with clean and without metal/bands/plain (those go by face index).
    if P.get("refine") and P.get("clean") and not (forced or plain):
        rg_ = P.get("rg")
        def votes_of(f, uvl):
            uvs = [Vector(l[uvl].uv) for l in f.loops]
            c_ = sum(uvs, Vector((0.0, 0.0))) / len(uvs)
            pts = [c_] + [c_ + (q - c_) * 0.85 for q in uvs] + [(uvs[i] + uvs[(i + 1) % len(uvs)]) * 0.5 * 0.85 + c_ * 0.15 for i in range(len(uvs))]
            pc_ = f.calc_center_median()
            return [(rg_(pc_, colour_at(q.x, q.y))[0] > 0.5 if rg_ else is_metal_px(*colour_at(q.x, q.y))) for q in pts]
        def is_metal_px(r, g_, b):
            mx, mn = max(r, g_, b), min(r, g_, b)
            return (mx - mn) / max(mx, 1e-4) < 0.30 or mx < 0.06
        t0 = sum(len(p.vertices) - 2 for p in me.polygons)
        bm = bmesh.new(); bm.from_mesh(me)
        for _round in range(int(P["refine"])):
            uvl = bm.loops.layers.uv.active
            mixed = [f for f in bm.faces if len(set(votes_of(f, uvl))) > 1]
            if not mixed: break
            edges = list({e for f in mixed for e in f.edges})
            bmesh.ops.subdivide_edges(bm, edges=edges, cuts=1, use_grid_fill=True)
            bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
        bm.to_mesh(me); bm.free(); me.update()
        print("PROP", name, "refined edges: tris from", t0, "to", sum(len(p.vertices) - 2 for p in me.polygons))
    # the mask
    uv = me.uv_layers.active.data
    if "Mask" in me.color_attributes: me.color_attributes.remove(me.color_attributes["Mask"])
    ca = me.color_attributes.new("Mask", "BYTE_COLOR", "CORNER")
    vm = {}; woodfaces = []
    def is_metal(r, g_, b):
        mx, mn = max(r, g_, b), min(r, g_, b)
        sat = (mx - mn) / max(mx, 1e-4)
        return sat < 0.30 or mx < 0.06   # (dark steel counts, whatever faint hue it has)
    rg = P.get("rg")
    def tier_vote(pc_, q):
        if tier_cls is not None and cls_map is not None: return cls_at(q.x, q.y) in tier_cls
        c_ = colour_at(q.x, q.y)
        return rg(pc_, c_)[0] > 0.5 if rg else is_metal(*c_)
    # islands: with clean, patches of faces smaller than this many cm2 take the answer of what surrounds them (the picture's painted shade and specks
    # inside a fitting left dark holes in its tint, 2026-11-13)
    island_val = {}
    if P.get("clean") and P.get("islands"):
        for poly in me.polygons:
            uvs = [Vector(uv[li_].uv) for li_ in poly.loop_indices]
            c_ = sum(uvs, Vector((0.0, 0.0))) / len(uvs)
            pts = [c_] + [c_ + (q - c_) * 0.6 for q in uvs]
            votes = sum(1 for q in pts if tier_vote(poly.center, q))
            island_val[poly.index] = 1.0 if votes * 2 > len(pts) else 0.0
        nb = {}
        by_edge = {}
        for poly in me.polygons:
            for ek in poly.edge_keys:   # (by where the edge is, not its vertices: the library models are split along their UV seams)
                pk_ = tuple(sorted(tuple(round(c, 2) for c in me.vertices[vi_].co) for vi_ in ek))
                by_edge.setdefault(pk_, []).append(poly.index)
        for fs in by_edge.values():
            for a_ in fs:
                for b_ in fs:
                    if a_ != b_: nb.setdefault(a_, set()).add(b_)
        area = {p.index: p.area for p in me.polygons}
        flipped = 0
        for want in (0.0, 1.0):   # (first the holes in the tint, then stray tinted specks)
            seen = set()
            for start in list(island_val):
                if start in seen or island_val[start] != want: continue
                comp = [start]; seen.add(start); k_ = 0
                while k_ < len(comp):
                    for n_ in nb.get(comp[k_], ()):
                        if n_ not in seen and island_val[n_] == want: seen.add(n_); comp.append(n_)
                    k_ += 1
                if sum(area[i] for i in comp) < P["islands"]:
                    for i in comp: island_val[i] = 1.0 - want
                    flipped += len(comp)
        print("PROP", name, "islands: %d faces flipped" % flipped)
    for poly in me.polygons:
        face_val = None
        if poly.index in island_val:
            face_val = island_val[poly.index]
            if poly.index in plain: face_val = 0.0
        elif P.get("clean"):
            # (one answer per face, from inside it: a corner on the edge of a wood patch in the picture picks up the grey seam around it, which drew gold streaks along the wood)
            uvs = [Vector(uv[li_].uv) for li_ in poly.loop_indices]
            c_ = sum(uvs, Vector((0.0, 0.0))) / len(uvs)
            pts = [c_] + [c_ + (q - c_) * 0.6 for q in uvs]
            pc_ = poly.center
            votes = sum(1 for q in pts if tier_vote(pc_, q))
            face_val = 1.0 if votes * 2 > len(pts) else 0.0
            if poly.index in plain: face_val = 0.0
        for li_ in poly.loop_indices:
            r, g_, b = colour_at(*uv[li_].uv)
            mx, mn = max(r, g_, b), min(r, g_, b)
            sat = (mx - mn) / max(mx, 1e-4)
            vi = me.loops[li_].vertex_index
            gval = rg(me.vertices[vi].co, (r, g_, b))[1] if rg else 0.0
            if face_val is not None: val = face_val
            elif rg: val = rg(me.vertices[vi].co, (r, g_, b))[0]
            elif P.get("mask") == "grey": val = 1.0 if is_metal(r, g_, b) else 0.0
            elif P.get("mask") == "colour": val = 1.0 if sat > 0.45 and g_ < 0.35 * r else 0.0   # (the red liquid, not the tan cork)
            else: val = 0.0
            if "also" in P and me.vertices[vi].co[IDX[P["also"][0]]] > P["also"][1]: val = 1.0
            if "far" in P and abs(me.vertices[vi].co[li]) <= P["far"]: val = 0.0
            if "below" in P and me.vertices[vi].co[IDX[P["below"][0]]] >= P["below"][1]: val = 0.0
            if poly.index in forced: val = 1.0
            ca.data[li_].color = (val, gval, 0.0, 1.0)   # (red: the tier's or the potion's colour; green: the second colour, an arrow's feathers)
        if face_val == 0.0 and poly.index not in forced:
            woodfaces.append([tuple(uv[li_].uv) for li_ in poly.loop_indices])
    me.update()
    # a face judged wood shows only wood: the picture's grey seams inside it (pale streaks along the stock and limbs) take the wood's colour from nearby
    if woodfaces and "wood" in P:
        fill = np.stack([blur(arr[..., c] * wood, rad) for c in range(3)], axis=-1) / np.maximum(wt, 1.0)[..., None]
        far_ = wt < 1.0
        mean_rgb = arr[..., :3][wood].mean(axis=0) if wood.any() else np.array([0.28, 0.12, 0.05])
        fill[far_] = mean_rgb
        in_wood = np.zeros((H, W), dtype=bool)
        for f_uv in woodfaces:
            q = [(u_ * W - 0.5, v_ * H - 0.5) for u_, v_ in f_uv]
            for k_ in range(1, len(q) - 1):
                (x0, y0), (x1, y1), (x2, y2) = q[0], q[k_], q[k_ + 1]
                den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
                if abs(den) < 1e-9: continue
                xa = max(0, int(math.floor(min(x0, x1, x2))) - 1); xb = min(W - 1, int(math.ceil(max(x0, x1, x2))) + 1)
                ya = max(0, int(math.floor(min(y0, y1, y2))) - 1); yb = min(H - 1, int(math.ceil(max(y0, y1, y2))) + 1)
                if xb < xa or yb < ya: continue
                gx, gy = np.meshgrid(np.arange(xa, xb + 1), np.arange(ya, yb + 1))
                l0 = ((y1 - y2) * (gx - x2) + (x2 - x1) * (gy - y2)) / den
                l1 = ((y2 - y0) * (gx - x2) + (x0 - x2) * (gy - y2)) / den
                l2 = 1.0 - l0 - l1
                A2 = abs(den)
                hs = [A2 / max(math.hypot(x1 - x2, y1 - y2), 1e-6), A2 / max(math.hypot(x2 - x0, y2 - y0), 1e-6), A2 / max(math.hypot(x0 - x1, y0 - y1), 1e-6)]
                ok = (l0 * hs[0] >= -1.0) & (l1 * hs[1] >= -1.0) & (l2 * hs[2] >= -1.0)   # (a pixel's width past each edge)
                in_wood[ya:yb + 1, xa:xb + 1] |= ok
        fix = in_wood & ~wood
        arr[..., :3][fix] = fill[fix]
        graded.pixels.foreach_set(arr.reshape(-1)); graded.update(); base = graded
        print("PROP", name, "wood faces %d, seam pixels evened %.1f%%" % (len(woodfaces), 100.0 * fix.mean()))
        # (2026-11-16) ...and plain wood in the metal/roughness picture too. Meshy painted the bow's pale pinstripes, and the longbow's whole stave, as polished
        # metal: evened to brown above, those faces still mirrored the dark surroundings (the sun gives no shine), the black holes and blotches along the limbs.
        # Wood faces: no metal, and a polished wood's roughness. (The tinted fittings take the tier's metal and roughness from the material, not this picture.)
        if mr:
            MW, MH = mr.size
            ma = np.array(mr.pixels[:], dtype=np.float32).reshape(MH, MW, 4)
            wm = in_wood[np.ix_(np.arange(MH) * H // MH, np.arange(MW) * W // MW)]
            was = float((ma[..., 2][wm] > 0.5).mean()) if wm.any() else 0.0
            ma[..., 2][wm] = 0.0
            ma[..., 1][wm] = np.clip(ma[..., 1][wm], 0.42, 0.62)
            mrc = bpy.data.images.new("GradedMR", MW, MH, alpha=True)
            mrc.colorspace_settings.name = "Non-Color"
            mrc.pixels.foreach_set(ma.reshape(-1)); mrc.update(); mr = mrc
            print("PROP", name, "wood faces made non-metal: %.1f%% of their pixels were metal" % (100.0 * was))
    # despeck: (fraction) pixels darker than this share of what is around them take the colour around them (the bow's painted rust specks read as pits, 2026-11-16)
    if P.get("despeck") and "wood" in P:
        Vd = arr[..., :3].max(axis=2); used = Vd > 0.015
        rad_d = max(2, W // 256)
        cnt = blur(used.astype(np.float64), rad_d)
        locV = blur(np.where(used, Vd, 0.0), rad_d) / np.maximum(cnt, 1.0)
        spk = used & (Vd < P["despeck"] * locV)
        loc = np.stack([blur(np.where(used, arr[..., c], 0.0), rad_d) for c in range(3)], axis=-1) / np.maximum(cnt, 1.0)[..., None]
        arr[..., :3][spk] = loc[spk]
        graded.pixels.foreach_set(arr.reshape(-1)); graded.update(); base = graded
        print("PROP", name, "despeck: %.2f%% of the picture lifted" % (100.0 * spk.mean()))
    # the pictures
    tex_side = P.get("repaint", {}).get("tex", 1024)   # (repaint: a sharper picture, so the painted edges stay crisp close up)
    shrink(base, tex_side, os.path.join(out, "T_%s_Base.png" % name))
    if baked_extra and "Mask" in baked_extra:   # (src_paint: the tier mask baked from the model's own picture; repaint: drawn from its shape)
        shrink(baked_extra["Mask"], tex_side, os.path.join(out, "T_%s_Mask.png" % name))
        print("PROP", name, "tier mask baked")
    elif tier_cls is not None and cls_map is not None:
        # (the shields: the tier's colour by pixel, not by face: a picture of its own, M_Prop's MaskTex. Clean edges where the bronze meets the planks,
        # which a mask by face drew as teeth across the big triangles of a 6000-triangle model)
        import numpy as np
        mk = np.isin(cls_map, list(tier_cls)).astype(np.float32)
        for _d in range(6):   # (grown into the empty margin round each island, so the edges of a tinted part don't filter toward untinted)
            nb_ = np.maximum.reduce([np.roll(mk, 1, 0), np.roll(mk, -1, 0), np.roll(mk, 1, 1), np.roll(mk, -1, 1)])
            mk = np.where(used, mk, np.maximum(mk, nb_))
        ma_ = np.zeros((H, W, 4), dtype=np.float32); ma_[..., 0] = mk; ma_[..., 1] = mk; ma_[..., 2] = mk; ma_[..., 3] = 1.0
        mim = bpy.data.images.new("GradedMask_%s" % name, W, H, alpha=True); mim.colorspace_settings.name = "Non-Color"
        mim.pixels.foreach_set(ma_.reshape(-1)); mim.update()
        shrink(mim, 1024, os.path.join(out, "T_%s_Mask.png" % name))
        print("PROP", name, "tier mask by pixel: %.0f%% of the picture" % (100.0 * mk.mean()))
    if normal: shrink(normal, P.get("repaint", {}).get("normal", 512), os.path.join(out, "T_%s_Normal.png" % name))
    if mr: shrink(mr, 512, os.path.join(out, "T_%s_MR.png" % name))
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    xs = [v.co for v in me.vertices]
    print("PROP", name, "tris", tris, "lo", tuple(round(min(c[i] for c in xs), 1) for i in range(3)), "hi", tuple(round(max(c[i] for c in xs), 1) for i in range(3)),
          "masked %.0f%%" % (100.0 * sum(c.color[0] > 0.5 for c in ca.data) / max(1, len(ca.data))))
    fbx = os.path.join(out, name + ".fbx")
    # (Blender's y -> the game's -y under the importer's scene conversion, so y is flipped on the way out: what comes in has the frame above)
    for v in me.vertices: v.co.y = -v.co.y
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.reverse_faces(bm, faces=bm.faces); bm.to_mesh(me); bm.free(); me.update()
    # the normals, turned the same way
    turned = [None] * len(me.loops)
    own = me.attributes["OwnN"]
    for li_ in range(len(me.loops)):
        n_ = (M3 @ Vector(own.data[li_].vector)).normalized()
        turned[li_] = (n_.x, -n_.y, n_.z)
    me.normals_split_custom_set(turned)
    me.update()
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, object_types={"MESH"}, apply_scale_options="FBX_SCALE_NONE", global_scale=0.01, apply_unit_scale=True,
                             axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", colors_type="LINEAR", use_tspace=True, bake_anim=False, add_leaf_bones=False, path_mode="COPY", embed_textures=False)
    print("PROP wrote", fbx)
