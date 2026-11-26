#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;

// Trophy records (2026-11-26, Docs/FishingPlan.md): the size of every trout, salmon and bass caught and the points on every
// stag's antlers. Kept by the server only, in Saved/Boards/World_<port>.json: each world server writes only its own file and
// reads every world's file to make the boards, so worlds never write over each other and every world shows the same boards
// (they share Saved/ on the host, like Saved/Accounts and Saved/Characters).
//   All-time board: the largest ever per species, any size.
//   Weekly board:   board fish only (at or over BoardAt), caught since the last Sunday midnight (the host's local time).
//   Personal bests: each account's largest per species (the RECORDS tab of the Escape screen).
namespace JeebRecords
{
	enum ESpecies : int32 { S_Trout, S_Salmon, S_Bass, S_Antlers, S_Num };

	const TCHAR* SpeciesName(int32 S);
	// Board threshold: 36 in trout and salmon, 21 in bass, 14 antler points
	float BoardAt(int32 S);
	// A size on the species' normal curve (inches; whole points for antlers)
	float RollSize(int32 S);
	FString SizeText(int32 S, float Size);

	// (server) A catch or a kill. Writes this world's file when it changes a board or a best. OutNote: "A new personal best!" etc. (may be empty)
	void Record(UWorld* World, const FString& AccountKey, const FString& Name, int32 Species, float Size, FString& OutNote);
	// (server) What the RECORDS tab shows this account, as lines: "T|title", "R|a|b|c|d" (columns), "N|note"
	FString Report(UWorld* World, const FString& AccountKey);

	// Who an actor is for the records: the logged-in account (online), else "local"
	void WhoIs(AActor* Knight, FString& OutKey, FString& OutName);
}
