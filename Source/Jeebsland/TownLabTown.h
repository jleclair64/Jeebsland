#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TownLabTown.generated.h"

class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class AForest;
namespace JeebTownLab { struct FSite; }

// The rebuilt Oak Hill (2026-11-14; TownLab.h): Oak Hill's own plan built the way professional games do it
// (Docs/TownLab/FrameBudget.md). Since the owner approved it, it is the real Oak Hill in Field (bRealSite: at the real plaza, on the
// real land, spawned by AKnightGameState while JeebTownLab::RealTown()); in the TownLab test map a second one stands 300 m east.
//  - one ground mesh on a 1 m grid whose single material (M_TL_Ground) paints grass, earth and mossy cobbled streets itself from its
//    vertex colours, instead of a ground plus separate road ribbons (fewer draws, no overlapping layers); at the real site its river
//    banks (mud, rock) are drawn with the world's own ground material in a second section, so they meet the river as before
//  - one plain component per building (Nanite draws them in one pass), the rebuilt SM_TL_* models where they exist (ModelSwaps),
//    each with its solid shape (SM_*_Col, on every machine: the server decides where players walk)
//  - each rebuilt model's decor (TownLabDecor.h: ivy, moss, stones, props, lanterns with a small real light), in instanced sets
//  - the foundation stones, shrubs and flowers, boulders round the edge (its trees: AOakForest, OakForest.cpp)
//  - the grass: the meadow switches to 40 m of dense grass in 15 m squares while you stand in the town (Meadow.cpp)
// Not replicated: every machine builds the same from fixed numbers (JEEBOAK hash to compare); a dedicated server builds only the
// ground's collision, the buildings' solid shapes and the boulders, all hidden.
UCLASS()
class ATownLabTown : public AActor
{
	GENERATED_BODY()

public:
	ATownLabTown();
	// Rounded boulders round a town (PlazaX, PlazaY: that town's plaza; Site: also keep off its streets and buildings)
	static void PlaceBoulders(AActor* Owner, float PlazaX, float PlazaY, const JeebTownLab::FSite* Site = nullptr);
	// The old model a rebuilt mesh stands in for ("SM_TL_House" -> "SM_OakHillHouse_300k": the same frame, so the chimneys measured
	// on the old models fit, AOakHillLife), or the name itself
	static FString OldMeshName(const FString& MeshName);

	UPROPERTY() bool bRealSite = false;   // set before BeginPlay (SpawnActorDeferred): the real Oak Hill in Field
	bool IsReal() const { return bRealSite; }
	int32 DrawSets = 0;   // how many drawn components the town made (logged)

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() UProceduralMeshComponent* Ground = nullptr;
	const JeebTownLab::FSite& Site() const;
	const TCHAR* Tag() const { return bRealSite ? TEXT("JEEBOAK") : TEXT("JEEBLAB"); }
	uint32 Hash = 2166136261u;   // checksum of everything solid (logged, to compare machines)
	void BuildGround();
	void BuildBuildings();
	// Window glow: one shared dynamic copy per building material, following the sun
	UPROPERTY() TArray<class UMaterialInstanceDynamic*> GlowMids;
	TArray<float> GlowBase;
	UPROPERTY() TArray<class UPointLightComponent*> LanternLights;
	float LastDusk = -1.f;
	UPROPERTY() class UMaterialInstanceDynamic* GroundMid = nullptr;
};
