#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Scarecrow.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UHealthComponent;

// A pumpkin-headed scarecrow you can fight. It shoves back lightly when you stand close. Beaten, it falls apart and
// is standing again 15 seconds later.
UCLASS()
class AScarecrow : public AActor
{
	GENERATED_BODY()

public:
	AScarecrow();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() UBoxComponent* Solid = nullptr;
	UPROPERTY() USceneComponent* BodyPivot = nullptr;
	UPROPERTY() USceneComponent* ArmRPivot = nullptr;
	UPROPERTY() UHealthComponent* Hp = nullptr;
	TArray<TPair<UStaticMeshComponent*, FLinearColor>> Parts;

	float HomeYaw = 0.f;
	float HitFlash = 0.f;
	float DeathT = 0.f;        // 0 = standing, then counts up while falling, then waits
	float RespawnT = 0.f;
	float AttackCool = 1.5f;
	float AttackT = -1.f;      // <0 = not attacking
	bool bAttackLanded = false;

	void OnHurt(AActor* From);
	void Rise();
};
