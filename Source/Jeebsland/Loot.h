#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkillComponent.h"
#include "Loot.generated.h"

class UStaticMeshComponent;

enum class ELootLook : uint8 { Meat, Beast, Pack };

// Things left behind when something dies: raw pork from a pig, hides and bones from a dangerous beast, or a pack with
// everything another knight was carrying. A little glowing speck marks it; press E beside it to take it all.
// It fades away after four minutes.
UCLASS()
class ALootDrop : public AActor
{
	GENERATED_BODY()

public:
	ALootDrop();
	virtual void Tick(float DeltaSeconds) override;

	void Setup(ELootLook InLook, const TArray<FInvSlot>& InItems, int32 InIngots);
	FString Describe() const;
	// Moves what fits into the taker's pack. The drop disappears once it is empty.
	void TakeAll(AActor* Taker);

	static ALootDrop* SpawnPork(UWorld* World, const FVector& Where);                     // 2 raw pork
	static ALootDrop* SpawnBeast(UWorld* World, const FVector& Where);                    // hide and bones, for wolves, bears and the like
	static ALootDrop* SpawnPack(UWorld* World, const FVector& Where, USkillComponent* From);   // empties From's pack (and ingots) into a bag

private:
	UPROPERTY() USceneComponent* Origin = nullptr;
	UPROPERTY() UStaticMeshComponent* Body = nullptr;
	UPROPERTY() UStaticMeshComponent* Flap = nullptr;
	UPROPERTY() UStaticMeshComponent* Tie = nullptr;
	UPROPERTY() UStaticMeshComponent* Glow = nullptr;

	TArray<FInvSlot> Items;
	int32 Ingots = 0;
	ELootLook Look = ELootLook::Meat;
	float Clock = 0.f;
	FVector GlowBase = FVector::ZeroVector;
};
