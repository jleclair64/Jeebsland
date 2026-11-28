#pragma once

// House loot (2026-11-26, Docs/LootPlan.md): the chests, cupboards and crates already standing in the walk-in town houses
// (Content/Data/Interior_<PlotId>.csv) hold things to take: potions, arrows, ore, ingots (Content/Data/HouseLoot.csv).
//  - Built from the room lists in AHouseInterior::Build on the server and every client alike, so every machine knows the same
//    containers, their locks and their places with nothing replicated. Requests name a container by its world spot.
//  - Locked ones need the Lockpicking skill: a short pick (the server holds the timer), a chance that rises with level, and a failed
//    pick may be heard by a townsperson nearby (the container is barred to you for a while; stealth-sensing NPCs in range are alerted).
//    Higher locks hold better loot.
//  - The server decides everything (EInvOp::BoxSearch / BoxPick, AKnightCharacter::FinishBoxPick): reach, level, rate, emptiness.
//    A container emptied by anyone refills after [Loot] LootRefillSeconds (per container: one shared world).

#include "CoreMinimal.h"

class AHouseInterior;
class AKnightCharacter;

namespace JeebHouseLoot
{
	enum class EBoxKind : uint8 { Chest, Cupboard, Crate };

	struct FLootBox
	{
		TWeakObjectPtr<AActor> House;     // (its house; or the Deeps, DeepCaves.h, 2026-11-28)
		FString Key;                       // "<PlotId>#<n>": its house and its number among that house's containers
		FVector At = FVector::ZeroVector;  // its foot in the world (the request names it by this)
		FVector Top = FVector::ZeroVector; // where the E prompt hangs
		FVector Stand = FVector::ZeroVector; // a spot in the room in front of it (tests)
		EBoxKind Kind = EBoxKind::Chest;
		int32 Lock = 0;                    // Lockpicking level needed; 0 = not locked
		int32 Tier = 0;                    // loot tier (HouseLoot.csv)
		// (server)
		double RefillAt = -1.0;            // world time it is full again; before then it is empty
		TMap<TWeakObjectPtr<AKnightCharacter>, double> BarredUntil;
	};

	// Whether a room-list mesh is a container, and which kind
	bool IsContainerMesh(const FString& Mesh, EBoxKind& OutKind);
	// AHouseInterior::Build: one container (Nth of its house), its world transform, and the room's middle in the world
	void AddBox(AActor* House, const FString& PlotId, int32 N, EBoxKind Kind, const FTransform& Xf, const FVector& RoomMid, float FloorWorldZ);
	void RemoveHouse(const AActor* House);
	// The container this knight can use now (nearest in reach, roughly in front of him), or null
	FLootBox* InReach(const AActor* Knight);
	FLootBox* FindAt(const UWorld* World, const FVector& At);
	const TCHAR* KindName(EBoxKind K);
	int32 NumBoxes(const UWorld* World);

	// (server) the requests
	void Search(AKnightCharacter* K, const FVector& At);
	void StartPick(AKnightCharacter* K, const FVector& At);

	// (test builds) the online test: -LootNetTest on the server, -LootNetA / -LootNetB on the two players (Tools/loot_net.sh)
	void TestTick(AHouseInterior* House, float Dt);
}
