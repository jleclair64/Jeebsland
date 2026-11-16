#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fill.generated.h"

class UInstancedStaticMeshComponent;
class UPointLightComponent;

// The world fill (Docs/FillPlan.md, 2026-11-15): the filler props (barrels, crates, carts, bushes, fences, wells ...) round Oak Hill and
// the original square's places, from Content/Data/Fill*.csv (FillData.h). Spawned by AKnightGameState::BuildWorld after everything
// else it must keep clear of. Not replicated and never ticks: every machine builds the same pieces from fixed seeds and the same data
// (JEEBFILL ... hash to compare machines). A dedicated server builds only the solids, so it stops a player where he sees a prop.
//  - Oak Hill: FillOakHill.csv's hand-placed sets first, then each zone's rules (FillZones.csv) in 32 m cells, each cell seeded by its
//    own index, so a change in one place never reshuffles the rest.
//  - Nothing overlaps: a 25 cm occupancy grid (made per 32 m square on demand) holds the buildings, walls, water, roads, doors and
//    the lanes from them to the road, shop counters, NPC walks, ladders, existing decor and solids, reserved tree spots, and every
//    placed piece (0.1 m between pieces, 0.3 m from anything else, roads 0.4 m, posts 0.2 m).
//  - Checked after placing: a flood fill with the knight's capsule from the spawn reaches every door, counter, yard and plot
//    (JEEBFILL UNREACHABLE), no two footprints overlap (JEEBFILL OVERLAP), and the largest empty spot in town (JEEBFILL GAP).
// Switches: -NoFill, -FillZone=<name> / jeeb.Fill.Zone (one zone only), -FillDump (Saved/Fill: the grid, zones and shapes for
// Tools/FillMap.ps1), -FillData=<folder> (test CSVs), -FillQuit (quit once built), -FillGallery (every kind once, floating far off,
// for test pictures); jeeb.Fill.Show 0/1, jeeb.Fill.CullScale.
UCLASS()
class AWorldFill : public AActor
{
	GENERATED_BODY()

public:
	AWorldFill();
	uint32 Hash = 2166136261u;   // everything placed (logged: compare machines)

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void Build();
	void UpdateLights();
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> DrawSets;
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> SolidSets;
	UPROPERTY() TArray<UPointLightComponent*> Lights;
	TArray<uint8> DrawCull;   // (each draw set's cull class, for jeeb.Fill.CullScale)
	FTimerHandle LightTimer;
	float LastDusk = -1.f;
	friend struct FFillBuilder;
	friend void JeebFillApplyCull();
	friend void JeebFillApplyShow();
};

namespace JeebFill
{
	// Ground under a solid fill piece bigger than 1 m^2 (wagons, stalls, wells, beds), inside its collision footprint with a dithered
	// soft rim: the grass and the fallen leaves skip it
	bool Covered(float X, float Y);
	bool AnyCovered();
}

// (AutumnDecor.cpp: drop the fallen leaves and the meadow's card bushes on covered ground once the fill is built; clients only)
void JeebAutumnDropCovered(UWorld* World);
void JeebMeadowDropCovered(UWorld* World);
