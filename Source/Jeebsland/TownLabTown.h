#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TownLabTown.generated.h"

class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class AForest;

// TownLab's rebuilt Oak Hill (2026-11-14, the test map only; TownLab.h): Oak Hill's own plan 300 m east of the real town, built
// the way professional games do it (Docs/TownLab/FrameBudget.md):
//  - one ground mesh on a 1 m grid whose single material (M_TL_Ground) paints grass, earth and mossy cobbled streets itself
//    from its vertex colours, instead of a ground plus separate road ribbons (fewer draws, no overlapping layers)
//  - every copy of a building model drawn as ONE instanced set (16 houses: one draw, not 16), and its solid shape likewise
//  - the foundation stones, shrubs and flowers instanced as before (one set per kind)
//  - the grass: the same meadow, which switches to 40 m of dense grass in 15 m squares while you stand here (Meadow.cpp)
// Not replicated, visual plus solid ground and walls (every machine builds the same).
UCLASS()
class ATownLabTown : public AActor
{
	GENERATED_BODY()

public:
	ATownLabTown();
	// The trees round the rebuilt town (the woods' own tree maker, like AOakHillTown::PlantTrees, with more dark pines in the ring)
	static void PlantTrees(AForest* Forest);
	// Rounded boulders round a town, the same set round both (PlazaX, PlazaY: that town's plaza): the lab's shared surroundings
	static void PlaceBoulders(AActor* Owner, float PlazaX, float PlazaY);

	int32 DrawSets = 0;   // how many drawn components the town made (logged, JEEBLAB)

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UProceduralMeshComponent* Ground = nullptr;
	void BuildGround();
	void BuildBuildings();
	void BuildPlazaOak();
};
