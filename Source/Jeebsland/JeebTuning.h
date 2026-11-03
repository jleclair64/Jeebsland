#pragma once

#include "CoreMinimal.h"

// ===================================================================================================
// Tuning numbers (owner, 2026-11-02): how fast, how far, how strong and how long things are (movement, the bow, every sword
// move, hitstun, knockdowns, the guard, armour) live in ONE text file, Content/Data/Tuning.ini, not in the code. Code says
// how a move works; the file says its numbers. The list of numbers is JeebTuningList.h.
//  - Read with JeebTune::V().Name (always the current values).
//  - Live: while the game runs (editor Play, or a test game) the file is checked twice a second; save it and the next arrow,
//    jump or swing uses the new numbers (JeebTune::Poll, called from the knight's Tick). Version() goes up on each reload.
//  - A missing or unreadable line keeps its fallback from JeebTuningList.h and logs "Jeebsland tuning:"; so does a line the
//    list does not know (a typo).
//  - Online: the server's copy decides damage, guard and timing; movement numbers (speeds, jump, dash slide) must match on
//    players' machines too, so worlds and players must run the same file (the packaged game ships it).
// ===================================================================================================
namespace JeebTune
{
	struct FValues
	{
#define JEEB_TUNE(Section, Name, Default) float Name = Default;
#include "JeebTuningList.h"
#undef JEEB_TUNE
	};

	// The current values (loaded the first time they are asked for)
	const FValues& V();
	// The same values without loading them (for binding references at start-up, before the game has found its files:
	// AKnightCharacter's tuning names; they read the fallbacks until the first Poll or V() loads the file)
	FValues& Storage();
	// Reloads the file if it changed since it was last read (at most twice a second); cheap to call every frame
	void Poll();
	// Goes up by one each time the file is (re)loaded
	int32 Version();
}
