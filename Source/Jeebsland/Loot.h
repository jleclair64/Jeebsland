#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkillComponent.h"
#include "Loot.generated.h"

class UStaticMeshComponent;

enum class ELootLook : uint8 { Meat, Beast, Pack, Item };   // Item: the dropped thing itself, modelled (see ItemModels.h)

// Things left behind when something dies: raw pork from a pig, hides and bones from a dangerous beast, or a pack with
// everything another knight was carrying. A little glowing speck marks it; press E beside it to take it all.
// It fades away after four minutes (a sack left by a death lasts ten).
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
	// The sack left by someone who died: their pack, their weapon bar and their worn gear, kept apart so the loot window can show
	// them the way the owner had them. Where: 0 = pack, 1 = weapon bar, 2 = worn.
	void SetupSack(const TArray<FInvSlot>& InItems, const TArray<uint8>& InWhere, int32 InIngots);
	const TArray<FInvSlot>& GetItems() const { return Items; }
	uint8 WhereOf(int32 i) const { return Where.IsValidIndex(i) ? Where[i] : 0; }
	int32 GetIngots() const { return Ingots; }
	bool IsSack() const { return Look == ELootLook::Pack; }
	bool TakeOne(int32 Index, AActor* Taker);   // one entry into the taker's pack (false if it does not fit)
	void TakeIngots(AActor* Taker);
	// Puts a sack item straight into a weapon-bar slot (Hot >= 0) or a worn slot (Kind). What was there goes to the taker's pack,
	// or (pack full) takes the item's place in the sack. False if the item does not belong in that slot.
	bool EquipTo(int32 Index, AActor* Taker, int32 Hot, int32 Kind, bool bSecond);
	void TakeEverythingFits(AActor* Taker) { TakeAll(Taker); }

	static ALootDrop* SpawnPork(UWorld* World, const FVector& Where);                     // 2 raw pork
	static ALootDrop* SpawnBeast(UWorld* World, const FVector& Where);                    // hide and bones, for wolves, bears and the like
	static ALootDrop* SpawnPack(UWorld* World, const FVector& Where, USkillComponent* From);   // empties From's pack (and ingots) into a bag

private:
	UPROPERTY() USceneComponent* Origin = nullptr;
	UPROPERTY() UStaticMeshComponent* Body = nullptr;
	UPROPERTY() UStaticMeshComponent* Flap = nullptr;
	UPROPERTY() UStaticMeshComponent* Tie = nullptr;
	UPROPERTY() UStaticMeshComponent* Glow = nullptr;
	UPROPERTY() TArray<class UProceduralMeshComponent*> ItemComps;
	void BuildItemLook();

	TArray<FInvSlot> Items;
	TArray<uint8> Where;
	int32 Ingots = 0;
	ELootLook Look = ELootLook::Meat;
	float Clock = 0.f;
	FVector GlowBase = FVector::ZeroVector;
};
