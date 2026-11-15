#pragma once

#include "CoreMinimal.h"

class UWorld;
class UMaterialInstanceDynamic;

// The world palette (2026-11-15; Docs/Palette.md). One file, Content/Data/Palette.ini, holds the world's named colours (grass, ground,
// leaves, bark, water, haze) and the knobs that move them all together (Autumn, Warmth, Greenness, Saturation, Brightness).
// - The built materials read their colours and the knobs' grade from the collection /Game/MPC_JeebPalette (Tools/palette.py builds
//   it from the same file); this writes the knobs' matrices and any edited colour into it while the game runs.
// - Colours the code sets on material instances (tree tints, the realistic grass's tint) come from Raw(): those materials grade
//   themselves. Things that are not materials (the haze, the water's scattering) take Graded(), the same grade done here.
// Console: jeeb.Palette.Autumn|Warmth|Greenness|Saturation|Brightness <-1..1>, jeeb.Palette.Reload, jeeb.Palette.Set <Name> <R G B>.
// At the default knobs every grade is exactly the identity: the look is as before the palette.
namespace JeebPalette
{
	enum class EClass : uint8 { Plant, Evergreen, World, Sky, None };

	// a named colour as Palette.ini has it (black and a warning if the name is unknown)
	FLinearColor Raw(FName Key);

	// a named colour graded by the knobs for its class (for what is not a material: the haze, the water)
	FLinearColor Graded(FName Key);

	// any colour graded as a class would be
	FLinearColor Grade(const FLinearColor& C, EClass Class);

	// sets a material instance's vector parameter to a named colour now and again whenever the file is reloaded or the colour set
	// (bGraded: the knobs' grade done here too, for materials that don't grade themselves)
	void Bind(UMaterialInstanceDynamic* M, FName Param, FName Key, bool bGraded = false);

	// sets the knobs' grade as material parameters (Pal<name>) now and after every change, for materials that cannot read the
	// collection (a material may read at most two collections; the Fab grass's wind already reads two: palette.py PARAM_MODE)
	void BindGrade(UMaterialInstanceDynamic* M);

	// writes the palette into a world's collection instance (KnightGameState's BeginPlay; changes later go to every game world)
	void ApplyToWorld(UWorld* World);

	// fires after any change (knobs, reload, a colour set): for code that pushes graded colours itself (the water)
	FSimpleMulticastDelegate& OnChanged();
}
