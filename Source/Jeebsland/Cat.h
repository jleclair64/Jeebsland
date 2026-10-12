#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cat.generated.h"

class UStaticMeshComponent;

// A cat that roams near the town. It strolls to a spot, sits for a while (tail flicking), and trots off if you come close.
// Built from simple shapes; its colours are set by the spawner.
UCLASS()
class ACat : public AActor
{
	GENERATED_BODY()

public:
	ACat();
	virtual void Tick(float DeltaSeconds) override;
	void Setup(const FVector2D& InHome, float InRoamRadius, const FLinearColor& InFur, const FLinearColor& InBelly, int32 Seed);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() USceneComponent* BodyPivot = nullptr;
	UPROPERTY() USceneComponent* HeadPivot = nullptr;
	UPROPERTY() USceneComponent* TailPivot = nullptr;
	UPROPERTY() USceneComponent* LegFL = nullptr;
	UPROPERTY() USceneComponent* LegFR = nullptr;
	UPROPERTY() USceneComponent* LegBL = nullptr;
	UPROPERTY() USceneComponent* LegBR = nullptr;
	TArray<TPair<UStaticMeshComponent*, int32>> Parts;   // component and its colour role (0 fur, 1 belly, 2 eyes, 3 nose and ears)

	FVector2D Home = FVector2D::ZeroVector;
	float RoamRadius = 800.f;
	FLinearColor Fur = FLinearColor(0.8f, 0.45f, 0.15f), Belly = FLinearColor(0.95f, 0.9f, 0.8f);
	FRandomStream Rand;

	enum class EState : uint8 { Sit, Walk, Flee };
	EState State = EState::Sit;
	float StateT = 0.f;
	FVector2D Goal = FVector2D::ZeroVector;
	float Phase = 0.f, Walking = 0.f, SitAlpha = 0.f;

	bool Open(const FVector2D& P) const;
	void PickGoal();
};
