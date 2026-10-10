#pragma once

#include "CoreMinimal.h"
#include "ResourceNode.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "MiningRock.generated.h"

// A rock with 5 mines in it. Each time it (re)fills it rolls its ore:
//   70% copper (5 XP per mine), 20% coal (10 XP, black chunks),
//   10% diamond (30 XP, triangular crystals instead of round chunks).
UCLASS()
class AMiningRock : public AResourceNode
{
	GENERATED_BODY()

public:
	AMiningRock();

protected:
	virtual void BeginPlay() override;
	virtual void OnChargesChanged(int32 Remaining, int32 Max) override;

private:
	UPROPERTY() UProceduralMeshComponent* Main;      // the faceted boulder with a few smaller stones around its foot
	UPROPERTY() TArray<UProceduralMeshComponent*> Ore; // chunky ore lumps on its surface
	UPROPERTY() UProceduralMeshComponent* GemA;
	UPROPERTY() UProceduralMeshComponent* GemB;
	UPROPERTY() UProceduralMeshComponent* GemC;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> OreMats;
	UPROPERTY() TArray<UMaterialInstanceDynamic*> GemMats;

	TArray<FVector> OreBaseScale;
	FVector GemScaleA;
	FVector GemScaleB;
	FVector GemScaleC;
	bool bDiamond = false;

	void RollOre();
	void UpdateVisuals(int32 Remaining, int32 Max);
};
