#pragma once

// The huntable animals (2026-11-22; Docs/DeerPlan.md): the stag, in small herds in meadows and at forest edges.
//  - ADeer is one animal. Online it is the SERVER's: the server runs its mind (graze, look up, watch, bolt, calm down), moves it and takes
//    its damage (arrows and blades find it through UHealthComponent and its own hurt boxes, like the bandits). It sends every player where it
//    is, which way it faces, how fast it goes and what it is doing; their machines glide it between updates and play the clips. Nothing is
//    ever taken from a player's machine.
//  - ADeerHerds exists on the server only: it reads Content/Data/Animals.csv and AnimalHerds.csv (FoeData.h), finds good ground for every
//    herd, places the animals and lets herds nobody is near sleep (a slow tick, sparse updates).
// Its body is a skinned model in /Game/Animals (Tools/BuildDeer.ps1) with seven clips played by a small code-driven animator (as the
// townspeople's, NpcBody.h): no Blueprint, no anim graph.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "HitBoxes.h"
#include "AnimalTire.h"
#include "Deer.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
class UHealthComponent;
class AKnightCharacter;
class ADeerHerds;

// What it is doing (sent to players as a number: add new ones at the end)
enum class EDeerState : uint8 { Graze, Idle, Look, Walk, Alert, Flee, Dead };

namespace JeebDeer
{
	enum EClip { C_Idle, C_Look, C_Walk, C_Run, C_Graze, C_Alert, C_Death, C_Count };
}

// Plays the animal's clips with cross-fades; walk and run are played at the speed its feet really cover
UCLASS(Transient, NotBlueprintable)
class UDeerAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	void SetClips(UAnimSequence* const* In, float StartAt);
	void Drive(int32 InClip, float InRate) { Want = InClip; Rate = InRate; }

	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Clip[7];
	int32 Want = 0;
	float Rate = 1.f, Start = 0.f;
	bool bNewClips = false;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};

USTRUCT()
struct FDeerAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()
	FDeerAnimProxy() {}
	FDeerAnimProxy(UAnimInstance* In) : FAnimInstanceProxy(In) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	UAnimSequence* Clip[7] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	float Time[7] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	float Weight[7] = { 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
};

// Where it is, as the server sends it (players' machines put it on their own ground: the height is the same everywhere)
USTRUCT()
struct FDeerMove
{
	GENERATED_BODY()
	UPROPERTY() FVector_NetQuantize Loc = FVector::ZeroVector;
	UPROPERTY() uint16 Yaw = 0;      // 0..65535 = 0..360 degrees
	UPROPERTY() uint16 Speed = 0;    // cm/s along its facing
};

UCLASS()
class ADeer : public AActor
{
	GENERATED_BODY()

public:
	ADeer();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// (server, before FinishSpawning) Kind: Animals.csv row; Herd: its herd in ADeerHerds; Home: the herd's home; Size: 0.92 to 1.08
	void SetupDeer(int32 InKind, int32 InHerd, ADeerHerds* InHerds, const FVector2D& InHome, int32 Seed, float Size);
	EDeerState StateNow() const { return static_cast<EDeerState>(RepState); }
	bool IsDown() const { return StateNow() == EDeerState::Dead; }
	int32 GetHerd() const { return Herd; }
	int32 Kind() const { return RepKind; }
	void SetSleeping(bool bSleep);                       // (server, ADeerHerds) nobody near: a slow tick, sparse updates
	bool IsSleeping() const { return bSleeping; }
	// (server) the herd's alarm: bBolt false = lift the head and watch From; true = run from it
	void Startle(const FVector2D& From, bool bBolt, const FVector2D& HerdDir);
	float RespawnSecs = 300.f;
	UPROPERTY() UHealthComponent* Hp = nullptr;

#if !UE_BUILD_SHIPPING
	int32 LabRole = -1;   // (-DeerLab test pictures) 0 stands and cycles its clips, 1 runs laps, 2 walks laps, 3 falls after a while
	FVector2D LabA = FVector2D::ZeroVector, LabB = FVector2D::ZeroVector;
#endif

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() USkeletalMeshComponent* Body = nullptr;
	UPROPERTY() USceneComponent* JBody = nullptr;   // plain joints: arrows that hit it stick to the nearest (Projectile.cpp), the same names on every machine
	UPROPERTY() USceneComponent* JNeck = nullptr;
	UPROPERTY() USceneComponent* JHead = nullptr;
	UPROPERTY() UDeerAnimInstance* Anim = nullptr;

	UPROPERTY(Replicated) uint8 RepKind = 0;
	UPROPERTY(Replicated) uint8 RepSize = 128;                           // 0..255 = 0.92..1.08 of the kind's scale
	UPROPERTY(ReplicatedUsing = OnRep_Move) FDeerMove RepMove;
	UPROPERTY(ReplicatedUsing = OnRep_State) uint8 RepState = 0;
	UFUNCTION() void OnRep_Move();
	UFUNCTION() void OnRep_State();
	double MoveAt = 0.0;          // (players' machines) when RepMove last arrived
	bool bPlaced = false;

	// Every machine: the look
	void BuildBody();
	void TickLook(float Dt, float Speed);
	float Scale() const;
	float HeadDown = 0.f;         // 0 head up .. 1 grazing (hurt boxes and joints follow it)
	float DeadFor = 0.f, Fall = 0.f;
	float SlopePitch = 0.f;
	void GatherHurt(TArray<JeebHit::FCapsule>& Out) const;

	// The server's side
	int32 Herd = -1;
	TWeakObjectPtr<ADeerHerds> Herds;
	FVector2D Home = FVector2D::ZeroVector, Goal = FVector2D::ZeroVector, ThreatAt = FVector2D::ZeroVector, HerdFlee = FVector2D::ZeroVector;
	FRandomStream Rand;
	FAnimalTire Tire;   // (shared by every animal: AnimalTire.h)
	float StateT = 0.f, StateFor = 5.f, SenseT = 0.f, CalmT = 0.f, AlertD0 = 0.f, Speed = 0.f, RespawnT = 0.f, StepSide = 1.f, StuckT = 0.f;
	double StepSideAt = -10.0;
	bool bSleeping = false, bGoHome = false;
	void SetState(EDeerState S, const TCHAR* Why);
	void TickMind(float Dt);
	void TickDown(float Dt);
	bool Sense(FVector2D& OutAt, float& OutDist) const;     // the nearest player it can see or hear
	bool Step(const FVector2D& Dir, float Want, float TurnRate, float Dt);   // turns toward Dir round whatever is in the way and goes forward; false if walled in
	bool Clear(const FVector2D& From, const FVector2D& To) const;
	void OnHurt(AActor* From);
	void Die(AActor* From);
	void Rise();
	void SendMove();
	friend class ADeerHerds;
	friend struct FDeerTestDriver;
};

UCLASS()
class ADeerHerds : public AActor
{
	GENERATED_BODY()

public:
	ADeerHerds();
	virtual void Tick(float DeltaSeconds) override;

	struct FHerd
	{
		FName Key;
		int32 Kind = 0;
		FVector2D Home = FVector2D::ZeroVector;
		TArray<TWeakObjectPtr<ADeer>> Deer;
		bool bAwake = true;
	};
	const TArray<FHerd>& GetHerds() const { return Herds; }
	// (server) one of the herd noticed or was hurt: the others within earshot look up, or bolt with it
	void Alarm(int32 Herd, const ADeer* By, const FVector2D& From, bool bBolt);
	static bool GoodGround(UWorld* World, const FVector2D& P, float MaxSpread);   // dry, level, clear of solids, off roads and out of towns

	static constexpr float SleepRange = 18000.f;   // cm from every one of a herd: no player nearer, it sleeps

protected:
	virtual void BeginPlay() override;

private:
	TArray<FHerd> Herds;
	float SleepClock = 0.f, BuildClock = 0.f;
	bool bWantBuild = false;
	void Build();
	void SpawnHerd(FName Key, int32 Kind, const FVector2D& Home, int32 Count, int32 Seed);
	void TickSleep();
#if !UE_BUILD_SHIPPING
	friend struct FDeerTestDriver;
	int32 TestStage = 0;
	float TestClock = 0.f, TestNext = 0.f;
	void BuildLab();
#endif
};
