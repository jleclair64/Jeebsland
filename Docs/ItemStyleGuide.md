# Item style guide (owner approved, 2026-11-05)

How to make weapons, tools and other items land on the look the owner wants, first time, without another round of "too realistic" and "too cartoonish". Read this before designing any item. For the world and the armour, see "Art direction" in `CLAUDE.md`.

## The look, in the owner's words

"Not quite realistic but a tiny bit cartoonish." The middle ground between cartoonish/chunky and realistic/slim.

| | Too realistic (rejected) | **The target (approved)** | Too cartoonish (rejected) |
|---|---|---|---|
| Shape | thin and true to life | sleek, natural proportions, only a little bolder than life | fat, toy-like, banana-shaped |
| Edges | worn, chipped | crisp bevels | soft blobs, no edges |
| Surface | rust, dirt, scratches, photo-like | hand-painted, clean, polished wood and dark steel | plain, smooth, little painted detail |
| Colour | muddy, weathered | rich and clean | flat |

The approved pictures are in `Docs/ItemStyle/`. Look at them first. They are the best definition of the target:
- `pickaxe.png`, `longbow.png`: approved shapes and finish. Their wood is more orange or red than the owner now wants, so the final models were shifted towards brown.
- `crossbow_except_the_wood_fade.png`: approved shape. The owner did not like its wood changing from yellow to red, so its wood was evened to brown in the import step, keeping a little variance.

The owner's own reference pictures showed a slim crossbow with a tapering tiller, clean painted wood, thin dark steel rails, a slim recurved prod and small steel tip caps. They are not saved here; `Docs/ItemStyle/` carries the same idea.

## Rules that came out of the owner's feedback

- **Wood is brown.** Rich, polished, medium to dark brown, like stained walnut. Grain is just a touch: you notice it up close, it never dominates. Not yellow, not orange. Darker or redder browns are fine as long as they read as brown.
- **A little variance in the wood, no drastic change.** Natural variation in tone is wanted; flat single-colour wood is not. A fade from one colour to another along the wood (yellow to red) is not. A deliberate separate part (a leather wrap, a thin inlay) is fine.
- "Cartoonish" never means chunky or fat here. If in doubt, go slimmer.
- Armour is the exception: it may lean more towards realism.

## Proportions to aim for

Measured on the approved models, as a share of the item's length:

| Item | Approved | Rejected as too chunky |
|---|---|---|
| Pickaxe | handle about 5% thick, head span about 54% | handle 11%, head span 79% |
| Longbow | limbs about 4% deep, tips bent back about 10% | stave 7-9% |
| Crossbow | stock 9% wide and 11% tall at its widest, prod span 91%, prod about 3% thick | prod about 13% thick, span 73% |

A hand must close round whatever it holds: a grip is about 5 to 7 cm across in the game. A piece wider than that is held by a narrower part (the potion is held by its neck).

## The process (this is what worked)

1. **Draw a picture first, and check it.** `meshy_text_to_image`, model `nano-banana-2-1`, `remove_background` on, 4 credits. Whole object in frame, three-quarter view (straight side-on for a bow). Redo the picture until it is right. It is far cheaper than redoing a model. When the owner is around, show them the pictures before going to 3D.
2. **Make the model from the picture.** `meshy_image_to_3d` with `input_task_id` = the picture, `ai_model` `meshy-t2.5`, `model_type` `smart-topology`, `target_polycount` 3000 to 5000, `texture_model` `meshy-7`, `enable_pbr` on, formats `glb`. 15 credits, about a minute. Make one as a test before a batch.
3. **Check it** from four sides with `Tools/prop_view.py`: `blender -b --factory-startup --python Tools/prop_view.py -- <in.glb> <out_prefix>`.
4. **Colour wrong, shape right?** Fix it for free in `Tools/prop_clean.py`: its `wood` setting turns the wood in the model's own picture into one rich brown, keeping a little variance and softening the grain. Only then think about `meshy_retexture` (10 credits): twice on the crossbow it lost what the picture had. One attempt came out flat and orange, the other painted the wooden prod and the top of the stock as steel.
5. **Into the game:** the `.glb` goes in `Art/Props`, a line in `PROPS` at the top of `Tools/prop_clean.py` sets its axes, size and grip, then `Tools/BuildProps.ps1` with the editor closed. **Judge it in the game, in sun and in shade, and search the log for "Failed to compile Material":** a Blender render says nothing about how it reads there. The game's lighting makes colours darker and redder, so the wood values that read as brown there are about hue 25, saturation 0.74, value 0.28 (in `wood`, linear colour).

Why not text-to-3D? Three tries at each model missed. It has no style setting, and naming a part to leave out seemed to invite it (a "no scope" crossbow grew a scope). Pictures fixed both.

Single-picture models can come out too flat from the side, since the back is a guess: the pickaxe's arms were 3 mm thick and had to be thickened. Measure depth as well as looks.

## Prompt wording that hit the target

Describe the object, then finish every picture prompt with this line, unchanged:

> Slightly stylised, not quite realistic and a tiny bit cartoonish: sleek natural proportions, crisp bevels, hand-painted textures, rich clean colours, no rust, no dirt.

Start with "Stylised 3D fantasy game weapon, three-quarter view, whole object in frame on a plain white background." Say "slim" or "slender" for the shape. Name the colours. For wood, use: "rich polished medium-dark brown wood with a subtle touch of painted grain and a little natural variation in tone, like stained walnut: brown, not yellow, not orange". "One single even colour" overshoots into flat, orange-looking wood, and "two-tone" or "honey-gold" brings the fade or the yellow back. The approved pickaxe, for example, with its wood wording updated:

> Stylised 3D fantasy game tool, three-quarter view, whole object in frame on a plain white background. A slim, well-made miner's pickaxe. A long straight slender wooden handle, rich polished medium-dark brown wood with a subtle touch of painted grain, a dark leather wrap near the lower end and a small dark steel end cap. A dark steel head with two long tapering curved arms ending in crisp points, set in a neat steel collar. Slightly stylised, not quite realistic and a tiny bit cartoonish: sleek natural proportions, crisp bevels, hand-painted textures, rich clean colours, no rust, no dirt.

Wording that overshot: "chunky", "thick", "exaggerated", "toy-like", "cartoon" (too cartoonish); "worn", "forged", "iron bands and rope", with no style line (too realistic). "Breath of the Wild crossed with Skyrim and Overwatch" plus "soft bevels" came out a little too round and heavy on its own.

## Tier colours

`M_Prop` tints what the vertex colour marks with the tier's colour, and `Tools/prop_clean.py` sets that mask. Steel takes the tier; wood and leather keep their own colours, so an emerald crossbow has green fittings on brown wood.

Give every item enough steel for its tier to show at a glance (owner, 2026-11-11: "more colour so they stand out", the same model in every tier). Small fittings alone left a gold crossbow reading as brown wood. `prop_clean.py` adds it without a new model: `metal` turns whole parts to steel (the crossbow's prod), `bands` cuts clean raised steel bands and plates into the model (ferrules, sleeves, tip caps, a deck plate), `plain` keeps a part wood whatever the picture painted there, and `clean` decides the mask once per face, so the wood shows no tinted streaks. Plan the steel into the picture prompt for new items: steel caps, collars, bands and plates, about a third of what you see.

## What it costs

About 4 credits a picture and 15 a model: under 25 credits for a finished item if the picture is right the first or second time.
