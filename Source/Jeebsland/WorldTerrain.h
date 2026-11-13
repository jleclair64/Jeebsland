#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldTerrain.generated.h"

class UProceduralMeshComponent;

// The whole world's land outside the original square round the start (2026-10-30): an 8 m grid over WorldMap.h's bounds,
// in chunks (so the far ones can be skipped when out of view), walkable everywhere, painted with M_Ground like the square and
// Oak Hill (since 2026-11-12: grass, rock on steep slopes, sand on the beaches, mud in the river beds, from JeebGround's mask
// in the vertex colours, GroundMask.h). Plus the water: the ocean and the
// rivers, flat sheets at their levels. Built the same on every machine from JeebTerrain::HeightAt.
UCLASS()
class AWorldTerrain : public AActor
{
	GENERATED_BODY()

public:
	AWorldTerrain();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() TArray<UProceduralMeshComponent*> Chunks;
	UPROPERTY() TArray<UProceduralMeshComponent*> Water;
	void BuildLand();
	void BuildWater();
};
