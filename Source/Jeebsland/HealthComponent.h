#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

// Hit points for anything you can hit (pigs, scarecrows). Every live one registers itself so the HUD can draw bars
// and the knight's swings and arrows can find targets. The owner reacts through OnDamaged.
UCLASS()
class UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	int32 Max = 20;
	int32 Cur = 20;
	float BarHeight = 150.f;        // how far above the actor's origin the bar floats
	float HitHeight = 70.f;         // centre of the body for arrows and bolts
	float HitRadius = 90.f;
	float LastHitTime = -1000.f;
	TFunction<void(AActor* From)> OnDamaged;

	bool IsAlive() const { return Cur > 0; }
	void Reset() { Cur = Max; LastHitTime = -1000.f; }
	// Returns the damage applied. Spawns the floating number and calls OnDamaged.
	int32 ApplyDamage(int32 Amount, AActor* From, bool bCrit);
	// Bars show for 15 seconds after a hit, and whenever health is under half
	bool ShowBar(float Now) const;
	FVector HitCentre() const;

	static TArray<UHealthComponent*>& All();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};
