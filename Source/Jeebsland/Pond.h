#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ResourceNode.h"
#include "Pond.generated.h"

class UStaticMeshComponent;

// A place on the water to fish. Click with a fishing pole in hand: after a few seconds something bites.
// Perch bite at any level, trout from Fishing level 5, golden carp (rare) from level 12. A better pole is quicker.
UCLASS()
class AFishingSpot : public AResourceNode
{
	GENERATED_BODY()

public:
	AFishingSpot();
		virtual void Tick(float DeltaSeconds) override;
	virtual float ReachRadius() const override { return 520.f; }   // you can fish from the bank, or wade in
	virtual float MinFacing() const override { return 0.25f; }
	virtual FString PromptText() const override { return TEXT("Click to cast your line (needs a fishing pole)"); }

	// (2026-11-26, the fishing redo: Docs/FishingPlan.md) The server waits for a bite (shorter at a higher Fishing level), then the
	// player has FishReelWindow seconds to click (Reel) or it gets away. Trout, salmon and bass have sizes (JeebRecords.h).
	void Reel(class AKnightCharacter* Knight);                 // (server) the click while fishing
	void ShowBite();                                           // (players' machines) the float goes under
	void ShowCatch(AActor* Knight, EItemType Item, float Size); // (players' machines) the fish comes up flopping on the line, then hangs limp
	bool bRiver = false;                                       // a river spot: trout, salmon and bass (the pond keeps perch, trout and carp)
	bool IsRiverSpot() const { return bRiver; }
	// The river spots, the same on every machine (called where the pond is made)
	static void SpawnRiverSpots(UWorld* World);

protected:
	virtual void BeginPlay() override;
	virtual void RollYield(USkillComponent* Skills, EItemType& OutItem, int32& OutXP) override;
	virtual void OnYield(AActor* Gatherer, EItemType Item) override;
	virtual int32 YieldSizeTenths(EItemType Item) override;   // (the fish just rolled keeps its length in the pack)
	virtual bool BeginOwnGather(class AKnightCharacter* Knight) override;

private:
	UPROPERTY() UStaticMeshComponent* Bobber = nullptr;
	UPROPERTY() UStaticMeshComponent* Line = nullptr;
	UPROPERTY() UStaticMeshComponent* Ripple = nullptr;
	UPROPERTY() TArray<TObjectPtr<class UProceduralMeshComponent>> Rings;   // two thin rings spreading on the water (players' machines only)
	float Clock = 0.f;
	// server: the cast under way
	int32 CastId = 0;
	bool bBiting = false;
	double BiteAt = 0.0;
	float LastSize = 0.f;
	void ScheduleBite();
	// players' machines
	double LocalBiteAt = -100.0;
	double CatchAt = -100.0;
	TWeakObjectPtr<AActor> CatchBy;
	UPROPERTY() USceneComponent* CatchRoot = nullptr;
	UPROPERTY() TArray<TObjectPtr<class UProceduralMeshComponent>> CatchParts;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Swimmers;   // fish under the water near the spot (the fish models, or the old code-built shape)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CatchModel = nullptr;   // the caught fish, when its model is in the game
	bool bSwimStatic = false, bSwimSetUp = false;
	TArray<FVector4f> SwimPlan;   // per swimmer: radius, speed (rad/s), phase, depth
	AActor* LocalFisher() const;
	FVector RodTip(const AActor* G) const;
};

// The pond itself: bowl-shaped ground (see Terrain.h), a water disc, a pier, reeds, rocks, lily pads and the fishing spots.
UCLASS()
class AFishingPond : public AActor
{
	GENERATED_BODY()

public:
	AFishingPond();

protected:
	virtual void BeginPlay() override;
};
