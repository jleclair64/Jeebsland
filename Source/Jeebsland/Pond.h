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

protected:
	virtual void BeginPlay() override;
	virtual void RollYield(USkillComponent* Skills, EItemType& OutItem, int32& OutXP) override;

private:
	UPROPERTY() UStaticMeshComponent* Bobber = nullptr;
	UPROPERTY() UStaticMeshComponent* Line = nullptr;
	UPROPERTY() UStaticMeshComponent* Ripple = nullptr;
	float Clock = 0.f;
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
