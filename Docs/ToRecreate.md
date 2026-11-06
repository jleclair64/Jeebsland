# Things to recreate on the new knight

On 2026-11-05 the new suit became the only knight, and the old knight was removed. These things went with him or no longer show. The ideas are kept here so they can be made again. The items themselves still exist: they can be worn or carried, and they keep their stats.

## Worn items that no longer show on him
- **Amulet:** a chain and pendant on the collar. It still gives its bonus but is not drawn.
- **Rings:** they were drawn on the fingers. They still give their bonus but are not drawn.
- **Red Helm Brush (crest):** a red horsehair brush along the top of the helm, +5% speed on foot. It still gives the speed but is not drawn. The new helmet needs its own brush.

## The cape
Taken off as the default look on 2026-11-05 (owner: it should look better, and it gets in the way of testing). Capes are still ordinary items, bought and worn in any tier. When it is remade, it must also stop going through the quiver when he has the bow in hand.

## The three armour styles
Light, Classic and Heavy are now only numbers on this one model (`Content/Data/ArmourStyles.csv`: how much each stops and weighs). Items and saves did not change. Their old looks, to remake as new models if wanted:
- **Light:** the crusader tunic with sleeves and a light helm. It was the old `Art/Knight Tunic`.
- **Classic:** the steel Meshy knight.
- **Heavy:** the very first Meshy knight, broad with big pauldrons (`Art/Knight/knight.glb`, last in git at commit a0f6b4f).
- The old models are in git history. The three new-style mannequins on the MoveLab armour wall all wear the one suit for now.

## Moves that changed
- **The spear's charged thrust:** hold to charge, let go to thrust, with reach growing with the charge. The spear now plays the sword's moves with long hit boxes, until it gets its own (owner, 2026-11-05: other weapons use the sword clips for now).
- **Weapons at rest:** swords used to hang tip down at his side. They now rest as the sword clips hold them.
- **The bow's arm pose:** the old code-built reach for the string (the elbow behind the hand, the arrow beside his right cheek) was replaced by the Mixamo bow clips. The old numbers were `FBowTune` in `KnightCharacterBow.cpp`; `git log` before this date has the whole `PoseBow`.
- **Idle breathing and weight shift:** the old pose breathed every 3.8 s and shifted weight. The new one has no idle clip yet, so he stands still. A Mixamo idle would bring this back.
- **Riding:** there is no horse in the game right now. The old knight had a riding pose, and the new one needs a clip when the horse returns.
- **Other weapons' own moves:** axes, the battle axe, the katana's second blade, wands and staffs each deserve their own clips. Tools use Mixamo's axe clips, and wands and staffs borrow the thrust's arms.
- **Own clips still missing:** a proper sit on a bench (the Mixamo sit is close), and a fishing idle tuned to the rod.

## Still drawn the old way
- **Loot bags, item icons and the wall's small shields:** these still use the code-built shapes (`KnightDesign.h`, `ItemModels.h`). They should eventually show the new armour.
- **The shield, the sword and every held item:** still the code-built models in `KnightDesign.h` and `WeaponDesign.h`, placed by the new pose. These were not part of the old knight's body, so they stay.
