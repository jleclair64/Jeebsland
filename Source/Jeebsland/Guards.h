#pragma once

// Town guards and the alarm (2026-11-26, Docs/LootPlan.md "Town guards and the alarm").
//  - A heard lock pick (HouseLoot.cpp FinishBoxPick) raises the alarm: the townsperson who heard it shouts (a speech bubble and a red "!"),
//    stops and looks at the thief; townsfolk near them turn and look too (a yellow "!"). Looks only, on players' machines (MulticastAlarm).
//  - Oak Hill's guards (ATownGuard, [Guards] GuardCount) walk the plaza's ring and the longest lanes. The server sends the nearest free one
//    after the thief: caught in town, the stolen goods are taken back, a fine is paid in ingots and the thief is knocked down; out of town,
//    hidden for a while or out of time, the guard gives up and walks back.
//  - Never abusable: no health, no hurt boxes, no loot, no pawn collision, never attacks, never leaves Oak Hill.
// Online: the guards are the server's (replicated, always relevant); townsfolk are built on every machine, so the shout names them.
// Switches: jeeb.Guards 0 (server, at world build) leaves the guards out. Test: -GuardNetTest / -GuardNetA / -GuardNetB (Tools/guards_net.sh).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
struct FInvSlot;
#include "Guards.generated.h"

class AKnightCharacter;
class ANpc;
class FKnightAnimator;
class UCapsuleComponent;

namespace JeebGuards
{
	// (server, KnightGameState's world build) Oak Hill's guards
	void SpawnAll(UWorld* World);
	// (server) a heard pick: Heard is the townsperson who heard it
	void RaiseAlarm(AKnightCharacter* Thief, ANpc* Heard);
	// (server, HouseLoot Give) what a player took from a house container, so a guard can take it back
	void NoteStolen(AKnightCharacter* Thief, const TArray<FInvSlot>& Items, int32 Ingots);
	// (players' machines, HUD) the thief this player is being chased as: true while a guard chases this knight
	bool IsChased(const AKnightCharacter* K);
}

UCLASS()
class ATownGuard : public AActor
{
	GENERATED_BODY()

public:
	ATownGuard();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// (server, before FinishSpawning) the beat he walks: Loop true goes round and round, else back and forth
	void SetBeat(const TArray<FVector2D>& Pts, bool bLoop, int32 InIdx) { Beat = Pts; bBeatLoop = bLoop; GuardIdx = InIdx; }

	enum class EMode : uint8 { Patrol, Chase, Return };
	EMode Mode() const { return EMode(RepMode); }
	AActor* ChaseTarget() const { return RepTarget; }
	// (HUD) his speech bubble now ("" none) and how much of it is left (0..1)
	FString SayingNow(float& OutAlpha) const;
	float HeadZ() const;

	// (server) sends him after Thief; false if he is busy
	bool StartChase(AKnightCharacter* Thief);
	bool IsFree() const { return RepMode != uint8(EMode::Chase); }

	// The shout, on every machine: the townsperson Name near At shouts Line and looks at Thief's spot; townsfolk near them notice
	UFUNCTION(NetMulticast, Reliable) void MulticastAlarm(const FString& NpcName, FVector_NetQuantize NpcAt, FVector_NetQuantize ThiefAt, const FString& Line);
	// A line over his own head (every machine)
	UFUNCTION(NetMulticast, Reliable) void MulticastSay(const FString& Line);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UCapsuleComponent* Capsule = nullptr;
	UPROPERTY() USceneComponent* WristR = nullptr;
	TSharedPtr<FKnightAnimator> Anim;
	float AnimAccum = 0.f;
	FVector SmVel = FVector::ZeroVector, LastLoc = FVector::ZeroVector;
	void TickBody(float Dt);

	UPROPERTY(Replicated) uint8 RepMode = 0;
	UPROPERTY(Replicated) AActor* RepTarget = nullptr;
	FString SayLine; double SayUntil = -1.0;

	// (server) the beat and the chase
	TArray<FVector2D> Beat;
	bool bBeatLoop = false;
	int32 GuardIdx = 0, BeatAt = 0, BeatDir = 1;
	float BeatWait = 0.f;
	TWeakObjectPtr<AKnightCharacter> Thief;
	FVector LastSeen = FVector::ZeroVector;
	float ChaseT = 0.f, OutsideT = 0.f, HiddenT = 0.f, BlockedT = 0.f, SeeT = 0.f;
	float FeetZ = 0.f;
	bool Step(const FVector2D& To, float Speed, float Dt);   // a step toward To; true when there
	float FloorAt(const FVector2D& P);
	void EndChase(const TCHAR* Why, bool bCaught);
	void Catch(AKnightCharacter* K);
	void SetMode(EMode M) { RepMode = uint8(M); }
	float Yaw = 0.f;

	// (cost note: microseconds spent in guards' ticks, logged every 10 s)
	static double CostUs; static int32 CostTicks; static double CostLogAt;
};
