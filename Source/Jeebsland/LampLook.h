#pragma once

#include "CoreMinimal.h"

class UMeshComponent;
class ULightComponent;

// The town's lamp look (2026-11-16, owner: "make the lamp lights more yellow, they look like harsh led"). Every lantern, lamp post, sconce and
// torch is a warm oil flame: the glass is amber with a brighter core (the TownDecor parents' GlowTint x GlowAmount; the glass picture alone
// clipped to white at GlowAmount 5) and the point lights share its colour. Live: jeeb.Lamp.Tint "r g b" and jeeb.Lamp.Glow (the glass's emissive).
namespace JeebLamp
{
	// the lamp colour (linear): the glass tint and the lights'
	FLinearColor Colour();
	// the point lights' colour: the same, a little paler
	FLinearColor LightColour();
	// swaps every "_Glow" material slot of a mesh component for a shared dynamic copy in the lamp look
	void ApplyToMesh(UMeshComponent* C, UObject* Outer);
	// sets the light's colour to the lamp colour now and whenever jeeb.Lamp.Tint changes
	void TrackLight(ULightComponent* L);
}
