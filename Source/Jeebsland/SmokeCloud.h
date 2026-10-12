#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "SmokeCloud.generated.h"

// A drifting cloud of purple poison smoke. Standing in it hurts the knight quickly, and the horse too (ridden or standing
// by). A test of how hazards will look and feel: the puffs are translucent balls (M_Smoke) that slowly swirl.
UCLASS()
class ASmokeCloud : public AActor
{
	GENERATED_BODY()

public:
	ASmokeCloud();
	virtual void Tick(float DeltaSeconds) override;

	float Radius = 300.f;          // how far across the poison reaches
	float HitEvery = 0.5f;         // seconds between hits
	int32 KnightHit = 14;          // damage to the knight per hit: it ignores armour and shields
	float HorseHit = 18.f;         // damage to the horse per hit (of 100)

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UInstancedStaticMeshComponent* Puffs = nullptr;
	struct FPuff { FVector Base; float Size; float Phase; float Speed; };
	TArray<FPuff> PuffData;
	float HitTimer = 0.f;
};
