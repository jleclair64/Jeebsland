#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkillComponent.h"
#include "ResourceNode.generated.h"

// Base class for anything the player can gather from (rocks, trees, ...).
// Press E once nearby: the character works for GatherSeconds, then gets the item(s) and XP.
// A node has a number of charges; when they run out it is empty until it respawns.
UCLASS(Abstract)
class AResourceNode : public AActor
{
	GENERATED_BODY()

public:
	AResourceNode();

	// Returns true if the click was dealt with (started, or refused with a message), false if nothing happened
	bool StartGathering(AActor* Gatherer);
	void CancelGathering();
	bool IsBeingGathered() const { return bBeingGathered; }
	// For landmarks that must stay put (the redwood that carries a fort): gathering shows this message instead
	void Lock(const FString& Message) { Charges = 0; EmptyText = Message; bLockedNode = true; }
	// Online: the server's count for this node (-1 = full), applied to this machine's copy
	void ApplyCharges(int32 N);
		AActor* GetCurrentGatherer() const { return CurrentGatherer.Get(); }
	// How far away (flat distance) and how squarely the player must face the node to start working on it
	virtual float ReachRadius() const { return 300.f; }
	virtual float MinFacing() const { return 0.5f; }
	virtual FString PromptText() const { return FString(); }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "Gathering") ESkillType Skill = ESkillType::Mining;
	UPROPERTY(EditAnywhere, Category = "Gathering") float GatherSeconds = 2.f;
	UPROPERTY(EditAnywhere, Category = "Gathering") float RespawnSeconds = 300.f; // 5 minutes
	UPROPERTY(EditAnywhere, Category = "Gathering") EItemType ItemType = EItemType::CopperOre;
	UPROPERTY(EditAnywhere, Category = "Gathering") int32 XPReward = 5;        // per gather
	UPROPERTY(EditAnywhere, Category = "Gathering") int32 ItemsPerGather = 1;  // per gather
	UPROPERTY(EditAnywhere, Category = "Gathering") int32 MaxCharges = 1;      // gathers before empty

	FString StartText = TEXT("You start gathering...");
	FString SuccessText = TEXT("You gather something.");
	FString EmptyText = TEXT("There's nothing left here. Give it a moment.");

	// Subclasses update their visuals (shrink, hide, ...) when the charges change
	virtual void OnChargesChanged(int32 Remaining, int32 Max) {}
	// Subclasses may decide what each gather gives (fishing picks a fish by your level). Default: ItemType and XPReward.
	virtual void RollYield(USkillComponent* Skills, EItemType& OutItem, int32& OutXP) {}
	int32 RequiredLevel() const;   // skill level needed (coal, diamond, pine and redwood need more than 1)

private:
	TWeakObjectPtr<AActor> CurrentGatherer;
	FTimerHandle GatherTimer;
	FTimerHandle RespawnTimer;
	int32 Charges = 1;
	float EffectiveSeconds = 2.f;
	bool bBeingGathered = false;

	bool bLockedNode = false;

	void FinishGathering();
	void Respawn();
	void ChargesChanged();   // redraws, and (online, on the server) tells every player
	// A message for whoever is gathering (sent to their machine when the server runs it)
	static void Tell(AActor* Who, float Seconds, const FColor& Color, const FString& Text);
};
