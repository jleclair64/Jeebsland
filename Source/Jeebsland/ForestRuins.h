#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ForestRuins.generated.h"

class UInstancedStaticMeshComponent;

// Old stone ruins scattered sparingly through the forest round Oak Hill (owner, 2026-11-16: "Scatter in the forest but sparingly. Maybe
// only like 10 of them so far."). The model is /Game/WorldProps/SM_Ruin (a Meshy community model, CC0: Art/WorldProps/Sources.md; made by
// Tools/BuildWorldProps.ps1), with its own convex hulls as collision.
//  - Where: about JeebForestRuins::Count spots in the forest ring (within JeebOakForest::Reach of the plaza, 25 m clear of the town's box,
//    clear of the original square, the TownLab site, water, river banks and roads), on ground flat enough, at least MinApart apart, each
//    with a random turn, and never on a tree trunk, log, stump or rock of AOakForest (its solids, which every machine has).
//  - Deterministic from a fixed seed and the ground's height at load, the same on the server and every client: log JEEBRUINS hash to
//    compare machines. Not replicated. Spawned by AKnightGameState right after AOakForest.
//  - The server builds the solid collision too (hidden); clients also clear the forest's bushes and plants from under each ruin.
namespace JeebForestRuins
{
	constexpr int32 Count = 10;
	constexpr float MinApart = 6000.f;   // cm between ruins
}

UCLASS()
class AForestRuins : public AActor
{
	GENERATED_BODY()

public:
	AForestRuins();

	// Where the ruins stand (world X, Y, ground Z; W = yaw in degrees), once BeginPlay has run
	const TArray<FVector4>& Spots() const { return Placed; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Ruins = nullptr;
	TArray<FVector4> Placed;
};
