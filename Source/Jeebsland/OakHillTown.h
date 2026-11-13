#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OakHillTown.generated.h"

class UProceduralMeshComponent;
class AForest;

// Oak Hill (2026-11-11): the town's ground east of the original square (a 2 m grid, like the square's own), its buildings
// (the Meshy models in /Game/Buildings, each on a levelled plot with a solid box for its walls) and its trees. The land's
// shape, roads and plots are JeebOakHill (OakHill.h). Not replicated: every machine builds the same town from the same
// numbers; a dedicated server builds only the ground's collision and the walls.
UCLASS()
class AOakHillTown : public AActor
{
	GENERATED_BODY()

public:
	AOakHillTown();
	// The big oak on the plaza and trees round the town, through the woods' own tree maker (the same on every machine)
	static void PlantTrees(AForest* Forest);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UProceduralMeshComponent* Ground = nullptr;
	uint32 Hash = 2166136261u;   // checksum of everything solid (logged as JEEBOAK hash, to compare machines)
	void BuildGround();
	void BuildBuildings();
	void LogChecks();
	// Barrels, crates, sacks, a cart and a rope coil behind and beside the market stall (owner, 2026-11-12: stuff behind it
	// too, like barrels or other junk, to fill in the scene): simple shapes, separate from the building's model
	// (bBehind: also the crates, barrels, sacks and cart behind it; a stall with no yard behind gets only the pieces beside it)
	void BuildStoreClutter(const FVector& At, float FrontYaw, float HalfWidthCm, float HalfDepthCm, bool bBehind = true);
};
