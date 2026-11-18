#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SteeleCreekTown.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;

// Steele Creek (2026-11-18; the plan is SteeleCreek.h): the smithing town in the southwest forest, built the way the rebuilt Oak Hill
// is (ATownLabTown): one plain component per building from the TownLab models (Nanite), each with its solid shape (SM_*_Col, on every
// machine), each model's own decor and lanterns, window glow following the sun. Round it a ring of the forest packs' trees (spruce
// mostly, some aspen and cork oak) with solid trunks, the quarry's high-tier rocks (AMiningRock), and the town's people (ANpc: a troll
// blacksmith, a wizard banker, a knight at the store, villagers). The streets are dirt roads (Terrain.h PathDefs); the props are the
// town fill's (Fill.cpp, FillOakHill.csv rows with Site "Steele").
// Not replicated: every machine builds the same from fixed numbers (JEEBSTEELE hash to compare); a dedicated server builds the solid
// shapes (hidden), the rocks and the people.
UCLASS()
class ASteeleCreekTown : public AActor
{
	GENERATED_BODY()

public:
	ASteeleCreekTown();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void BuildBuildings();
	void BuildWoods();
	void PlaceProps();
	void SpawnRocks();
	void SpawnPeople();
	uint32 Hash = 2166136261u;   // checksum of everything solid (logged, to compare machines)
	int32 DrawSets = 0;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> GlowMids;
	TArray<float> GlowBase;
	UPROPERTY() TArray<UPointLightComponent*> LanternLights;
	UPROPERTY() TArray<UInstancedStaticMeshComponent*> TreeSets;   // (drawn trees: their shadows follow jeeb.Steele.TreeShadows)
	float LastDusk = -1.f;
	int32 LastTreeShadows = -1;
	int32 NetTestStage = 0;      // (test only: -SteeleNetTest)
	double NetTestT0 = 0.0;
};
