#pragma once

#include "CoreMinimal.h"

class UWorld;
class AForest;

// TEMPORARY test setting (owner, 2026-11-15: "make the testing forests in Field so that it has grass too"): a row of small sample groves in the real world,
// one per tree source, on open meadow east of Oak Hill (TreeGroves.cpp), for the owner to walk through and pick which trees the world uses:
// Project Nature's Spruce Forest, DZ Sample Trees, Megaplants English Oak and Hornbeam, and the world's own trees for comparison. Each stands under its
// own sign. Non-Shipping builds only; `jeeb.Test.Groves 0` (console, or `[SystemSettings] jeeb.Test.Groves=0` in DefaultEngine.ini) leaves them out at the next load.
// Remove them for good: delete TreeGroves.h/.cpp, the call in AKnightGameState::BuildWorld (marked GROVES), and the Color < 0 line in AutumnDecor.cpp.
namespace JeebGroves
{
	// Plants the groves (Forest: the world's woods, for the grove of its own trees). Does nothing in a Shipping build or with jeeb.Test.Groves 0.
	void Build(UWorld* World, AForest* Forest);
}
