#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "KnightGameState.generated.h"

// One rock, tree or fishing spot that is not full (online: the server's count, sent to every player)
USTRUCT()
struct FNodeCharge
{
	GENERATED_BODY()
	UPROPERTY() FVector_NetQuantize Loc = FVector::ZeroVector;
	UPROPERTY() int32 Charges = 0;
};

// Builds the world. The game state exists on the server AND on every client (the game mode lives only on the server), so
// each machine builds its own copy of the procedural world here, the same way every time (fixed random seeds).
// Rule for multiplayer: anything built here is LOCAL to each machine. Things players change or fight (rocks, trees, enemies,
// drops, the horse) must later become server-owned replicated actors; for now each machine has its own copy of those.
UCLASS()
class AKnightGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AKnightGameState();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Online, every machine has its own copy of the rocks, trees and fishing spots, but the server's charges are the real ones.
	// The server lists every node that is not full here; each player's machine sets its copies to match.
	void SetNodeCharges(const FVector& Loc, int32 Charges, int32 Max);

	// The trading post's stock this hour, as the server has it (so everyone sees the same "3 left")
	UPROPERTY(Replicated) int64 StoreHour = -1;
	UPROPERTY(Replicated) TArray<int32> StoreLeft;
	void SyncStore();                        // server: after a sale

	// A floating damage number, from the server to every player (see JeebCombat::AddNumber)
	UFUNCTION(NetMulticast, Unreliable) void MulticastNumber(FVector_NetQuantize Loc, int32 Amount, uint8 Type);
	static void ApplyStore(UWorld* World);   // a player's machine: puts the server's counts into its own store list

protected:
	virtual void BeginPlay() override;

private:
	void BuildWorld();

	UPROPERTY(ReplicatedUsing = OnRep_Nodes) TArray<FNodeCharge> NodeCharges;
	UFUNCTION() void OnRep_Nodes();

	TWeakObjectPtr<class AExponentialHeightFog> FogActor;
	float FogNow = 0.012f;

	// Test screenshot mode (-JeebShot, single player only)
	bool bShotMode = false;
	bool bShotTaken = false;
	float ShotClock = 0.f;
	FRotator ShotRot = FRotator::ZeroRotator;
	FString ShotName = TEXT("shot");
};
