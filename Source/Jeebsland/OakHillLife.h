#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OakHillLife.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class AOakHillTown;

// Oak Hill's movement and life (2026-11-12, "a town that moves feels finished"): soft chimney smoke that drifts with the
// wind, windows and the forge that glow more as the sun lowers, and a few flickering lamps at the shop fronts.
// Purely visual (never on a dedicated server, nothing replicated): every machine's own copy does it from the buildings
// AOakHillTown placed (their positions, and the chimneys measured on the models: see the table in OakHillLife.cpp).
// Cloth wind for banners and awnings is a separate material (M_ClothWind, Tools/build_town_life_materials.py).
UCLASS()
class AOakHillLife : public AActor
{
	GENERATED_BODY()

public:
	AOakHillLife();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Puffs = nullptr;   // every smoke puff of every chimney, one set of instances (M_ChimneySmoke)
	UPROPERTY() TArray<UMaterialInstanceDynamic*> GlowMids;       // one per distinct building material (shared by the copies, so no extra Nanite materials)
	UPROPERTY() TArray<UPointLightComponent*> Lamps;

	struct FStack { FVector Base; float Phase; float Strength; float Size; float Dark; };   // one chimney's column of smoke
	struct FGlowMat { int32 Mid; float Base; };                                            // the glow each material started with (houses keep their 35% dimmer ratio)
	struct FLamp { float Base; float Phase; };
	TArray<FStack> Stacks;
	TArray<FGlowMat> GlowBase;
	TArray<FLamp> LampData;
	FVector Centre = FVector::ZeroVector;
	bool bSetUp = false;
	bool bPuffsOn = true;
	float LastGlowDusk = -1.f;
	void SetUp();
	void SetUpGlow(AOakHillTown* Town);
	void UpdateGlow();
};
