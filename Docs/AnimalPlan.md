# Animal plan (2026-11-13, for the owner to approve; nothing built yet)

The owner wants pigeons, chickens and crows round the towns first, then pigs, cows, cats and dogs. The downloads are in `Art/Animals/Originals` (git-ignored); their pages are in `Art/Animals/Sources.md`.

## What we have

- **Pigeon pack** (CYNICz): 14 still poses (standing, walking, pecking, preening, wings up, flying). Each is about 27k triangles, so each needs cutting to about 2-3k.
- **Two rigged hens**: ChickenRigged (57 bones, no wing bones) and KFC (49 bones, with wings). Neither has animations.
- **Stylized hen**: a nice shape with no skeleton.
- **Crows**: no model yet.

## Proposed way to animate them

**Birds (pigeons, chickens, crows): pose swapping.**
- Each bird is a short flip-book of still poses, swapped 8-12 times a second. Breath of the Wild's small birds read much the same way.
- The pigeon pack is already made for this.
- For the hen, we pose the KFC rig in Blender into about 8 stills: stand, two walk steps, peck down, peck up, head turn, wings out, sit.
- One instanced mesh per pose means dozens of birds cost almost nothing (well under 0.1 ms).

**Behaviour, in code like the cats and bats:**
- Flocks wander, peck, preen, and hop a step.
- Pigeons and crows burst into the air when a knight comes within about 3 m and land again 15-30 m away.
- Chickens run off flapping instead of flying.
- Crows sit on roofs, fences and the castle ruins.

**Online:** they are scenery, so every machine runs its own from a fixed seed, as the cats do. Nothing goes to the server until animals become something players interact with (meat, eggs, quests).

**Crows:** either recolour the pigeon poses black with a heavier beak (free; close enough from afar), or find or make a real crow (Meshy, about 20-30 credits, then posed the same way).

## Four-legged animals (pigs, cows, dogs; cats are code-built already)

- Mixamo has no animal clips, and pose swapping looks jerky on legs.
- These need a real walk cycle: a rigged model with walk, idle, graze or sniff, and run clips.
- **Option A: Meshy's rigging and animation.** It costs credits per model and only covers some body types; we'd check the price first.
- **Option B: library models** that already come rigged and animated, if any are good.
- After either, they run through the same `FKnightAnimator`-style clip player the knight uses.

## Questions for the owner

1. Is pose swapping fine for birds?
2. How many: for example 2-3 pigeon flocks of 6-10 on the plaza and roads, 6-8 hens by houses or a coop, and 3-5 crows on the ruins and fences?
3. Crows: recoloured pigeons, or a real crow model?
4. For pigs, cows and dogs, may we spend Meshy animation credits once the price is known, or should we look in the library first?

Suggested model: Opus 5.5, high effort (a new system, with motion review).
