#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldTerrain.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

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
	virtual void Tick(float DeltaSeconds) override;
	// The one ground material every ground mesh draws with (the world's chunks, the original square, the rebuilt Oak Hill's river
	// banks): a dynamic copy of M_Ground shared by all of them, made at the first ask, whose SkyFill (the low sun's fill on flat
	// ground, so it stays as bright as the grass blades: Tools/world_material.py) this actor follows from ADayNight::Dusk.
	// Null on a dedicated server (nothing is drawn there).
	static UMaterialInterface* SharedGround(UWorld* World);
	// The low sun's fill and the ground among the grass blades (2026-11-15, owner: the ground "seems to glow" in shade at dusk): sets
	// SkyFill (Fill) and the under-grass values (grass_field.py: FillFrom/FillTo from jeeb.Grass.PackFade, UnderFill, Floor) on a
	// ground material (M_Ground's shared copy, or Oak Hill's lawn, M_TL_Ground). Returns false if nothing changed since the last call.
	static bool SetUnderGrass(UMaterialInstanceDynamic* Mid, float Fill);
	// The one low-sun fill for every ground (the world's and Oak Hill's lawn): jeeb.Ground.SkyFill times ADayNight::Dusk
	static float LowSunFill();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() TArray<UProceduralMeshComponent*> Chunks;
	UPROPERTY() TArray<UProceduralMeshComponent*> Water;
	void BuildLand();
	void BuildFine();   // (the 2 m patches: Steele Creek, the lake, the split)
	void BuildWater();
};
