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
}
