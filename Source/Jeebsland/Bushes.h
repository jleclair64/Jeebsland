#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Bushes.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// The bushes (2026-11-18 overhaul, owner: "too many bushes in the game, they aren't colored correctly, and they all look too cartoony and
// not fluffy ... get rid of all the bushes we have"). Every old bush went (the meadow's leaf-card domes, the TownDecor bush, shrub, plant
// clump and hedge models, the forest's undergrowth bushes). The new ones (/Game/Bushes, Tools/BuildBushes.ps1) are clusters of leaf sprays
// cut from the DZ tree packs' own leaf pictures, in the pack trees' leaf materials, so they share the trees' leaves, wind and colour:
//   BushFull (aspen, about 2 m), BushSmall (aspen, about 1.2 m), BushFine (cork oak, about 1.7 m), BushLow (a low, arching cork-oak
//   clump for undergrowth and banks), BushHedge (a 2.5 m cork-oak hedge, for gardens through the fill), and a flowering bush (BushSmall
//   with the meadow's own flowers through it).
// This actor places them where bushes grow, far fewer than before: along forest edges and in clearings (where the trees' canopy, JeebCanopy,
// meets open ground), a few under the trees, and on river banks and lake shores (wherever JeebWorld::InWater says water is, so new lakes
// get theirs too). Never on roads, in water, on buildings, on fill pieces or solids (an overlap test against the world's collision), in
// Oak Hill's town box (except the river banks) or on the original square; the towns' bushes come from the world fill's plots
// (FillUses.csv: Bush1 in yard corners and along fences, Hedge1 along garden fences), so they follow the buildings when the layout moves.
// Visual only: clients build it (never a dedicated server), from a fixed seed, so every client sees the same bushes. No collision.
// Live: jeeb.Bush.Density (0 none, 1 as placed, more is denser; rebuilds), jeeb.Bush.Reach (draw distance scale), jeeb.Bush.Shade (how much
// darker than the trees' green, jeeb.Trees.DzGreen), jeeb.Bush.Shadows. Log JEEBBUSH (counts per kind and where, and the time taken).
UCLASS()
class ABushes : public AActor
{
	GENERATED_BODY()

public:
	ABushes();
	void Rebuild();
	void ApplyLook();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() TArray<UHierarchicalInstancedStaticMeshComponent*> Sets;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> Mids;   // one per leaf material, shared with the fill's bushes
	TArray<float> SetReach;
};
