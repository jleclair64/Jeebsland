#pragma once

#include "CoreMinimal.h"

// The tree colours' shared knobs (2026-11-16, owner: "the color of the trees feels too bright and colorful"). One global saturation for
// every autumn leaf colour (the DZ forests, the town's trees, the leaves on the ground and the spruce needles' multiplier), live from
// jeeb.Trees.Saturation (1 = as the sliders say, 0 = grey). Defined here (not in TreeGroves.cpp) so Shipping has it too.
namespace JeebTreeLook
{
	float Saturation();
	// a colour pulled toward its grey by Saturation()
	FLinearColor Saturate(const FLinearColor& C);
	// reads "r g b" (also "r,g,b" or "r_g_b"); Def where a number is missing
	FLinearColor ParseRGB(const FString& S, const FLinearColor& Def);
	// fires when jeeb.Trees.Saturation changes
	FSimpleMulticastDelegate& OnChanged();
	// (2026-11-21, owner: the spruces' cones "less orange and more dark brown/green") The cones are painted into the Spruce Forest pack's
	// needle and far-card colour textures, and the needle tint (no blue) turned them bright orange. Tools/BuildSpruceCones.ps1 makes, in
	// /Game/TreeGroves/SpruceCones, a copy of each of those textures with the cone texels repainted, and a copy of each needle material
	// that draws the repainted texture (the pack hard-wires its needle texture, so a parameter cannot swap it; the pack's own files are
	// never changed). ConeLeaf: a pack needle material's copy, or the material itself where there is none (make the dynamic instance from
	// what it returns). DarkCones: swaps a far card's colour texture parameter for its copy; returns how many it swapped.
	class UMaterialInterface* ConeLeaf(class UMaterialInterface* M);
	int32 DarkCones(class UMaterialInstanceDynamic* D);
}
