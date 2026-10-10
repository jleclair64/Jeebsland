#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Horse.generated.h"

class UStaticMeshComponent;

// A chestnut horse built from simple shapes. Press T to call it; it gallops to you. Press E beside it to mount.
// While you ride, the horse follows the knight (the knight's movement code gives the speed), so it is twice as fast
// as walking and twice as fast as sprinting. Its legs and head animate to match.
UCLASS()
class AHorse : public AActor
{
	GENERATED_BODY()

public:
	AHorse();

	void CallTo(AActor* Target);
	void SetRider(AActor* InRider) { Rider = InRider; bCalled = false; }
	bool HasRider() const { return Rider.IsValid(); }
	bool IsCalled() const { return bCalled; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() USceneComponent* BodyPivot = nullptr;
	UPROPERTY() USceneComponent* NeckPivot = nullptr;
	UPROPERTY() USceneComponent* TailPivot = nullptr;
	UPROPERTY() USceneComponent* LegFL = nullptr;
	UPROPERTY() USceneComponent* LegFR = nullptr;
	UPROPERTY() USceneComponent* LegBL = nullptr;
	UPROPERTY() USceneComponent* LegBR = nullptr;
	TArray<TPair<UStaticMeshComponent*, FLinearColor>> Parts;

	TWeakObjectPtr<AActor> Rider;
	TWeakObjectPtr<AActor> CallTarget;
	bool bCalled = false;
	float Phase = 0.f;
	float Clock = 0.f;
	float SpeedBlend = 0.f;
	float GrazeT = 0.f;
	FVector LastPos = FVector::ZeroVector;
};
