#pragma once

// Wild goblin pickpockets round Steele Creek (2026-11-27, owner's request; Docs/ProjectNotes.md "Goblin pickpockets").
//  - AGoblin is one goblin, the SERVER's: it lurks behind a tree or bush near a player, creeps up from behind (out of the player's view
//    cone), dashes in and steals 5-20 ingots on touch, then flees and hides again. It flees at once when swung at, shot at (an arrow or bolt
//    passing close) or hit. 1 hp: any arrow kills it, and the killer gets half of what it stole since it last died.
//  - Never inside the safe circle round the town's big central buildings ([Goblins] GoblinSafeRadius), never outside the outer radius.
//  - The victim's message comes ~1 s after the steal, server to that player only (USkillComponent::Say).
//  - AGoblinBand (server only) finds the hiding spots (the trees and bushes the world built, by world query, so a new town layout just
//    works), spawns GoblinCount goblins and tells them about arrows and bolts flying by.
// Players' machines are sent where each goblin is, its facing, speed and state, and glide it on their own ground; the body is Silver
// Point's goblin (People.csv "Goblin", /Game/People) played by UNpcAnimInstance (walk clip slow and crouched to creep, fast to dash).
// Tunables: Tuning.ini [Goblins]. Tests: Tools/goblin_net.sh (-GoblinNetTest/-GoblinNetA/-GoblinNetB), Tools/goblin_lab.sh (-GoblinLab=X_Y).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HitBoxes.h"
#include "Goblins.generated.h"

class USkeletalMeshComponent;
class UHealthComponent;
class AKnightCharacter;
class AGoblinBand;

namespace JeebGoblins { void SpawnBand(UWorld* World); }   // (server, KnightGameState's world build)

enum class EGoblinState : uint8 { Lurk, Creep, Dash, Flee, Hide, Dead };

USTRUCT()
struct FGoblinMove
{
	GENERATED_BODY()
	UPROPERTY() FVector_NetQuantize Loc = FVector::ZeroVector;
	UPROPERTY() uint16 Yaw = 0;
	UPROPERTY() uint16 Speed = 0;
};

UCLASS()
class AGoblin : public AActor
{
	GENERATED_BODY()

public:
	AGoblin();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	EGoblinState StateNow() const { return EGoblinState(RepState); }
	bool IsDown() const { return StateNow() == EGoblinState::Dead; }
	int32 StolenSinceDeath() const { return Stolen; }
	// (server) something threatening at From: run
	void Startle(const FVector& From, const TCHAR* Why);
	UPROPERTY() UHealthComponent* Hp = nullptr;

	int32 Idx = 0;
	TWeakObjectPtr<AGoblinBand> Band;
	float MaxSpeedSeen = 0.f;   // (server, tests) the fastest it has gone, cm/s
	bool bTestHold = false;     // (tests) stands still, still senses
	bool bTestHardHold = false; // (tests) stands still even when startled
	FString LastWhy;
#if !UE_BUILD_SHIPPING
	int32 LabRole = -1;   // (-GoblinLab) 0 hides crouched, 1 dashes laps
	FVector2D LabA = FVector2D::ZeroVector, LabB = FVector2D::ZeroVector;
#endif

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() USkeletalMeshComponent* Body = nullptr;
	UPROPERTY(ReplicatedUsing = OnRep_Move) FGoblinMove RepMove;
	UPROPERTY(Replicated) uint8 RepState = 0;
	UFUNCTION() void OnRep_Move();
	double MoveAt = 0.0;
	float BodyYaw = 0.f, Crouch = 0.f, Lean = 0.f;
	void BuildBody();
	void TickLook(float Dt);
	void GatherHurt(TArray<JeebHit::FCapsule>& Out) const;

	// server
	FRandomStream Rand;
	float StateT = 0.f, SenseT = 0.f, Speed = 0.f, Yaw = 0.f, BlockedT = 0.f;
	FVector2D Goal = FVector2D::ZeroVector, ThreatAt = FVector2D::ZeroVector;
	int32 Stolen = 0;
	TWeakObjectPtr<AKnightCharacter> Target;
	void SetState(EGoblinState S, const TCHAR* Why);
	void TickMind(float Dt);
	bool Step(const FVector2D& To, float Want, float Dt);   // true when there
	void SendMove();
	void Steal(AKnightCharacter* K);
	void Die(AActor* From);
	void Rise();
	void PickHide(const FVector2D& AwayFrom);
	AKnightCharacter* PickTarget() const;
	bool SeenBy(const AKnightCharacter* K) const;
	friend class AGoblinBand;
	friend struct FGoblinTest;
};

UCLASS()
class AGoblinBand : public AActor
{
	GENERATED_BODY()
public:
	AGoblinBand();
	virtual void Tick(float DeltaSeconds) override;
	TArray<FVector2D> Spots;   // trees and bushes in the ring
	TArray<TWeakObjectPtr<AGoblin>> Goblins;
	// a hiding spot near Near, away from Away, at least MinFromPlayers from every player
	FVector2D HideSpot(const FVector2D& Near, const FVector2D& Away, float MinFromPlayers, FRandomStream& R) const;
	static FVector2D Centre();
	static bool InSafe(const FVector2D& P, float Pad = 0.f);
	static bool InRange(const FVector2D& P);   // inside the outer radius and outside the safe circle
	static bool StealableSpot(const FVector2D& P);   // ...and not inside any building
protected:
	virtual void BeginPlay() override;
private:
	float BuildClock = 0.f;
	bool bBuilt = false;
	TMap<TWeakObjectPtr<AActor>, FVector> ProjLast;
	void Build();
	void TickShots();
};
