#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Npc.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;

UENUM()
enum class ENpcRole : uint8
{
	Shopkeeper,   // the store
	Blacksmith,   // the smithy
	GateKnight,   // walks the road outside the village and chats
	Villager,     // one of ten villagers who stroll about the village and tell their stories
	Banker        // keeps the vault
};

// A villager built from simple shapes. Talk to them with E: they greet you and tell a story about Jeebsland,
// and the shopkeeper and blacksmith then open their trading screens. Nobody replies to what you say.
UCLASS()
class ANpc : public AActor
{
	GENERATED_BODY()

public:
	ANpc();

	// Called right after spawning: builds the body for this role. Patrol points (if any) are set separately.
	void Setup(ENpcRole InRole, int32 InVillagerIdx = 0);
	void SetPatrol(const TArray<FVector2D>& Points) { Patrol = Points; }

	ENpcRole GetRole() const { return Role; }
	FString GetNpcName() const;
	bool OffersTrade() const { return Role == ENpcRole::Shopkeeper || Role == ENpcRole::Blacksmith || Role == ENpcRole::Banker; }
	int32 SpeakerIdx() const;   // which storyteller this is (see Lore.h)
	// What they say on this visit, one entry per click
	TArray<FString> MakeConversation();
	void SetTalking(bool bOn) { bTalking = bOn; }

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() UCapsuleComponent* Capsule = nullptr;
	UPROPERTY() USceneComponent* BodyRoot = nullptr;
	UPROPERTY() USceneComponent* HeadPivot = nullptr;
	UPROPERTY() USceneComponent* ArmRPivot = nullptr;
	UPROPERTY() USceneComponent* ArmLPivot = nullptr;
	UPROPERTY() USceneComponent* LegLPivot = nullptr;
	UPROPERTY() USceneComponent* LegRPivot = nullptr;

	ENpcRole Role = ENpcRole::Shopkeeper;
	bool bTalking = false;
	int32 Visits = 0;
	float HomeYaw = 0.f;
	float AnimT = 0.f;
	TArray<FVector2D> Patrol;
	int32 PatrolIdx = 0;
	float PauseT = 0.f;
	float WalkBlend = 0.f;
	int32 VillagerIdx = 0;
	float HalfH = 92.f;       // capsule half height (smaller for the boy)
	float StuckT = 0.f;
};
