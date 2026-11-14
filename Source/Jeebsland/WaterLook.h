#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UPrimitiveComponent;

// The water's look (2026-11-13): every water sheet (the sea and rivers in WorldTerrain.cpp, the pond in Pond.cpp, the castle's
// moat, MoveLab's pond) wears M_Water, Unreal's Single Layer Water shading built by Tools/water_material.py (see its top for
// the look): see-through and tinted by depth, reflecting the sky and the banks, rippling, the rivers drifting downstream, a soft
// foam line at the banks. One material instance is shared by all of them; the jeeb.Water.* console variables override its
// numbers in a running game for tuning (e.g. -ExecCmds="jeeb.Water.Absorb 0.01 0.003 0.0024"), with no build.
// Water is drawn on players' machines only; nothing here collides or changes a surface's height (where the water is, for
// swimming: JeebWater.h).
namespace JeebWaterLook
{
	// M_Water's shared instance, or null if it is missing (or with the test switch -OldWater, for before/after pictures).
	UMaterialInterface* Material();

	// Puts the water on a component's material slot; without M_Water it falls back to the old flat colour in M_Color.
	void Apply(UPrimitiveComponent* C, int32 Slot, const FLinearColor& OldColor);

	// A river's flow for its sheet's vertex colours (M_Water reads R, G = direction x 0.5 + 0.5, B = 0; still water is white).
	inline FLinearColor FlowColor(const FVector2D& Dir)
	{
		const FVector2D D = Dir.GetSafeNormal();
		return FLinearColor(0.5f + 0.5f * D.X, 0.5f + 0.5f * D.Y, 0.f, 1.f);
	}
}
