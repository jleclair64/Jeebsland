#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HitBoxes.h"
#include "HealthComponent.generated.h"

// Hit points for anything you can hit (pigs, scarecrows). Every live one registers itself so the HUD can draw bars
// and the knight's swings and arrows can find targets. The owner reacts through OnDamaged.
UCLASS()
class UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	// (online: the server's numbers, sent to every player when the owner is a replicated actor)
	UPROPERTY(Replicated) int32 Max = 20;
	UPROPERTY(ReplicatedUsing = OnRep_Cur) int32 Cur = 20;
	float BarHeight = 150.f;        // how far above the actor's origin the bar floats
	float HitHeight = 70.f;         // centre of the body for arrows and bolts
	float HitRadius = 90.f;
	float LastHitTime = -1000.f;
	TFunction<void(AActor* From)> OnDamaged;
	// Optional: lets the owner soften a blow before it lands (a raised shield). Returns the damage that gets through.
	TFunction<int32(int32 Amount, AActor* From)> ModifyDamage;
	// On players' machines (online): the server says it was hit (for the flinch). OnDamaged only runs on the server.
	TFunction<void()> OnHitSeen;
	// Optional: the owner's hurt boxes (HitBoxes.h), where a hit box must touch for a blow to land. Unset: an upright capsule
	TFunction<void(TArray<JeebHit::FCapsule>& Out)> GatherHurt;
	// Optional: a raised shield's wedge (HitBoxes.h); returns false while the shield is down. Hit boxes check it first.
	TFunction<bool(JeebHit::FShieldWedge& Out)> GatherShield;
	// A hit box struck the raised shield (server): the owner works out how much the shield stops (and shows it); returns the
	// damage that still gets through
	TFunction<int32(int32 Amount, AActor* From)> ShieldBlock;
	// Optional: a kick struck it while its shield was up (server): it is knocked down
	TFunction<void(AActor* From)> KnockDown;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsAlive() const { return Cur > 0; }
	void Reset() { Cur = Max; LastHitTime = -1000.f; }
	// Returns the damage applied. Spawns the floating number and calls OnDamaged.
	// bHitBoxes: it came through hit boxes, which have already dealt with the shield (ModifyDamage is skipped)
	int32 ApplyDamage(int32 Amount, AActor* From, bool bCrit, bool bHitBoxes = false);
	// Bars show for 15 seconds after a hit, and whenever health is under half
	bool ShowBar(float Now) const;
	FVector HitCentre() const;

	static TArray<UHealthComponent*>& All();

protected:
	UFUNCTION() void OnRep_Cur(int32 Old);
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};
