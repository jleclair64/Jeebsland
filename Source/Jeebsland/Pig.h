#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Pig.generated.h"

class UStaticMeshComponent;
class UHealthComponent;

// A pig that wanders around its pen. Three sword hits kill it and it drops bacon; a new pig turns up after a while.
UCLASS()
class APig : public AActor
{
	GENERATED_BODY()

public:
	APig();

	// Called by the pen right after spawning
	void Setup(const FVector2D& InPenCenter, const FVector2D& InPenHalf);

	bool IsAlive() const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() USceneComponent* BodyPivot = nullptr;
	TArray<TPair<UStaticMeshComponent*, FLinearColor>> Parts;

	FVector2D PenCenter = FVector2D::ZeroVector;
	FVector2D PenHalf = FVector2D(500.f, 400.f);
	FVector2D Target = FVector2D::ZeroVector;

	UPROPERTY() UHealthComponent* Hp = nullptr;
	void TakeHit(AActor* Attacker);   // reaction after damage has been applied: flee, or die and drop bacon
	float Wait = 0.f;
	float FleeTimer = 0.f;
	float WalkPhase = 0.f;
	float DeathT = 0.f;
	float RespawnT = 0.f;
	float HitFlash = 0.f;
	float Speed = 0.f;

	void PickTarget();
	void SetVisible(bool bVisible);
	void Respawn();
};
