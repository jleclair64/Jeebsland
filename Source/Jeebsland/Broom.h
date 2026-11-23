#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JeebBroomData.h"
#include "Broom.generated.h"

class UStaticMeshComponent;

// Silver Point's broom stall (2026-11-22): a little market stall with brooms leaning on it, beside the placeholder house. Every machine
// places its own from the fixed plan (JeebBroom::StallSpot); E within StallReach opens the broom shop (AKnightCharacter::OpenBroomShop), and
// the server sells only to someone standing that close (EInvOp::BuyBroom). Looks only: no collision but the counter.
UCLASS()
class ABroomStall : public AActor
{
	GENERATED_BODY()
public:
	ABroomStall();
	static void PlaceIn(UWorld* World);   // (AOpenWorld::BeginPlay: once per world)
protected:
	virtual void BeginPlay() override;
private:
	TArray<TPair<UStaticMeshComponent*, FLinearColor>> Parts;
};
