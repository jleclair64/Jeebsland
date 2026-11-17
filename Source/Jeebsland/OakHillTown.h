#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GroundDecor.h"
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

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UProceduralMeshComponent* Ground = nullptr;
	uint32 Hash = 2166136261u;   // checksum of everything solid (logged as JEEBOAK hash, to compare machines)
	void BuildGround();
	void BuildBuildings();
	TArray<JeebDecor::FFootprint> Foundations;   // each building's walls and door, for the stones and shrubs at its foundation (GroundDecor.h)
	void LogChecks();
};
