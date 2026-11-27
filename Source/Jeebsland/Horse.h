#pragma once

// Horses (2026-11-22, agent horse; replaces the single-player test horse of 2026-11-16). Docs/HowToTest.md "Horses".
//  - AHorse is one horse. Online it is the SERVER's: the server moves it, decides who rides it, who owns it and its health; every player's
//    machine is sent where it is (FDeerMove, as the stag), its state, its rider, its owner and whether it wears a saddle. While someone
//    rides it, every machine simply carries it under that rider (the rider's movement is the knight's own, predicted as usual).
//  - Wild horses roam a meadow beside each stable (AStable). Any knight can ride one (E). Riding one for HorseTameSecs tames it for the
//    stablemaster's quest ("A Horse of Your Own", Quests.csv StableHorse); finishing that quest makes it yours.
//  - Your own horse waits at the stable you last used. T calls it (it comes running, or appears near you if it can't get to you),
//    holding T for a second makes it follow you (or stop following). Anyone may ride your horse; when you press T, they are put down.
//  - Horses are only hurt by players inside PvP zones (JeebPvp::IsInPvpZone, PvpZones.h; none exist yet). A horse that falls goes back
//    to its stable and rests there for HorseRestSecs before it can be called again.
//  - URiderComponent (on the knight) holds whether you own a horse and where it is stabled (saved with the character, JeebSave).
// The body is a skinned library model in /Game/Animals (SK_Horse, Tools/BuildDeer.ps1, Tools/horse_retarget.py), played by the stag's
// code-driven animator (UDeerAnimInstance, Deer.h). The saddle is /Game/Animals/SM_Saddle, shown when the horse wears one.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Deer.h"
#include "Horse.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UHealthComponent;
class AKnightCharacter;
class AStable;

// What a horse is doing (sent to players as a number: add new ones at the end)
enum class EHorseState : uint8 { Idle, Walk, Run, Ridden, Dead, Graze };

namespace JeebHorse
{
	struct FStableDef
	{
		FName Key;
		FString Name;
		FVector2D Pos = FVector2D::ZeroVector;    // the yard's middle, world cm
		float Yaw = 0.f;                          // the yard's open side faces this way (degrees, Unreal yaw)
		FVector2D Wild = FVector2D::ZeroVector;   // the wild horses' meadow, world cm
		int32 WildCount = 3;
	};
	const TArray<FStableDef>& Stables();         // Content/Data/Stables.csv
	const FStableDef* FindStable(FName Key);
	const FStableDef* NearestStable(const FVector2D& At, float MaxDist = 1.0e9f);
	int32 SaddleId();                             // the saddle item (Accessories.csv "Leather Saddle"), 0 if missing
	bool WearsSaddle(const AKnightCharacter* K);
	// The Horse Riding skill: top speed and gallop length grow with the level (Tuning.ini [Horse])
	float SpeedMul(int32 Level);
	float GallopSecs(int32 Level);
	constexpr float Scale = 1.25f;                // the model at 1.25: a 15-hand riding horse, the saddle at the old test horse's height
	constexpr float Reach = 300.f;                // how near a knight must be to mount (cm; the server allows lag on top)
	extern const FName QuestKey;                  // the stablemaster's quest that gives you your own horse
	// The automatic jump (owner 2026-11-22): something low ahead along Dir within Reach (a log, a rock, a fence, a low wall) with open,
	// fairly level ground beyond it. OutTop = its height over Feet, OutDist = how far ahead its face is. Taller things and trees still block.
	constexpr float JumpMaxTop = 150.f;
	bool JumpProbe(const UWorld* W, const FVector& Feet, const FVector2D& Dir, float Reach, const AActor* Ignore1, const AActor* Ignore2, float& OutTop, float& OutDist);
}

UCLASS()
class AHorse : public AActor
{
	GENERATED_BODY()

public:
	AHorse();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- the server's side ----
	// (before FinishSpawning) Wild: a meadow horse of Stable; otherwise Owner's horse, waiting at Stable
	void SetupHorse(AStable* InStable, bool bInWild, AKnightCharacter* InOwner, bool bInSaddled, int32 Seed);
	void CallTo(AKnightCharacter* K);                    // run to K (teleports near K when it can't get there)
	void SetFollow(AKnightCharacter* K, bool bOn);       // keep near K while K is on foot
	void SetRider(AKnightCharacter* K);                  // (server, from the knight's mount and dismount) null: nobody
	void MakeOwned(AKnightCharacter* K);                 // a tamed wild horse becomes K's
	void PutSaddleOn() { bRepSaddled = true; }
	void SendHome(AStable* To, float RestSecs);          // back to a stable (fell, or its owner left)
	float RestLeft() const;                              // seconds before it can be called again (server)
	TWeakObjectPtr<AKnightCharacter> TamedBy;            // (server) the last rider who calmed it

	// ---- every machine ----
	AKnightCharacter* GetRider() const { return RepRider; }
	bool HasRider() const { return RepRider != nullptr; }
	AKnightCharacter* GetOwnerKnight() const { return RepOwner; }
	bool IsWild() const { return bRepWild; }
	bool IsSaddled() const { return bRepSaddled; }
	bool IsFollowing() const { return bFollow; }
	bool IsDown() const { return EHorseState(RepState) == EHorseState::Dead; }
	EHorseState StateNow() const { return EHorseState(RepState); }
	AStable* GetStable() const { return Stable.Get(); }
	UPROPERTY() UHealthComponent* Hp = nullptr;

#if !UE_BUILD_SHIPPING
	FVector HoofPrev[4]; double HoofPrevT = 0.0;   // (-HoofLog)
	int32 LabRole = -1;   // (-HorseLab test pictures) 0 stands and cycles its clips, 1 canters laps, 2 walks laps, 3 carries the player at a gallop (-HorseLabRide)
	FVector2D LabA = FVector2D::ZeroVector, LabB = FVector2D::ZeroVector;
	static void BuildLab(UWorld* World);
#endif

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() USkeletalMeshComponent* Body = nullptr;
	UPROPERTY() UStaticMeshComponent* SaddleMesh = nullptr;
	UPROPERTY() UDeerAnimInstance* Anim = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_Move) FDeerMove RepMove;
	UPROPERTY(Replicated) uint8 RepState = 0;
	UPROPERTY(Replicated) AKnightCharacter* RepRider = nullptr;
	UPROPERTY(Replicated) AKnightCharacter* RepOwner = nullptr;
	UPROPERTY(Replicated) bool bRepWild = true;
	UPROPERTY(Replicated) bool bRepSaddled = false;
	UPROPERTY(Replicated) bool bFollow = false;
	UFUNCTION() void OnRep_Move();
	// the automatic jump while it runs by itself (called, following): the server starts it, every machine draws the same arc
	UPROPERTY(ReplicatedUsing = OnRep_Jump) uint8 RepJumpSeq = 0;
	UPROPERTY(Replicated) uint8 RepJumpH = 0;   // the arc's height, in 2 cm
	UFUNCTION() void OnRep_Jump();
	float JumpT = -1.f, JumpH = 0.f, JumpFromZ = 0.f, JumpPitch = 0.f;
	FVector2D JumpDir = FVector2D::ZeroVector;
	static constexpr float JumpDur = 0.8f;
	void StartJump(float Top, const FVector2D& Dir);
	float JumpZ(float GroundZ) const;            // the arc over this spot's ground while JumpT runs
	void StepJump(float Dt);                     // JumpT and the nose's pitch along the arc
	double MoveAt = 0.0;
	bool bPlaced = false;

	void BuildBody();
	void TickLook(float Dt, float Speed, bool bBack);
	void GatherHurt(TArray<JeebHit::FCapsule>& Out) const;
	float SlopePitch = 0.f, SlopeRoll = 0.f, Fall = 0.f;

	// the server's mind
	enum class EGoal : uint8 { Stay, Wander, Call, Follow };
	EGoal Goal = EGoal::Stay;
	TWeakObjectPtr<AKnightCharacter> Target;
	TWeakObjectPtr<AStable> Stable;
	FVector2D Home = FVector2D::ZeroVector, WanderTo = FVector2D::ZeroVector;
	FRandomStream Rand;
	float Speed = 0.f, StateT = 0.f, StuckT = 0.f, ProgressT = 0.f, ProgressD = 0.f, DeadT = 0.f, TameT = 0.f;
	double RestUntil = -1.0;
	bool bBacking = false;
	void SetState(EHorseState S);
	void TickMind(float Dt);
	void TickRidden(float Dt);
	bool StepToward(const FVector2D& To, float Want, float Dt);   // false when walled in
	bool CatchUp(const AKnightCharacter* K);                        // appear a little way behind K (the Lego Star Wars catch-up)
	bool Clear(const FVector& At) const;
	void OnHurt(AActor* From);
	void Die(AActor* From);
	void SendMove();
	friend struct FHorseTestDriver;
};

// A stable: a fenced yard with hay, water and a hitching rail (built from the TownDecor props on every machine), where owned horses
// wait, and the wild horses' meadow nearby (the server keeps WildCount of them there).
UCLASS()
class AStable : public AActor
{
	GENERATED_BODY()

public:
	AStable();
	virtual void Tick(float DeltaSeconds) override;
	void SetupStable(const JeebHorse::FStableDef& D);
	FName GetKey() const { return Def.Key; }
	const JeebHorse::FStableDef& GetDef() const { return Def; }
	FVector2D StallSpot() const;
	static AStable* Find(const UWorld* World, FName Key);
	static AStable* Nearest(const UWorld* World, const FVector2D& At, float MaxDist = 1.0e9f);
	TArray<TWeakObjectPtr<AHorse>> Wild;   // (server)

protected:
	virtual void BeginPlay() override;

private:
	JeebHorse::FStableDef Def;
	float WildClock = 0.f;
	int32 WildSeed = 0;
	void BuildYard();
	void SpawnWild();
	friend struct FHorseTestDriver;
};

// On the knight: does this character own a horse, and where is it stabled (saved, JeebSave). The server spawns the horse at its
// stable when the character comes into the world and takes it away when they leave.
UCLASS()
class URiderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URiderComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(Replicated) bool bOwnsHorse = false;   // (sent to the owner only)
	FName StableKey;                                 // the stable last used (server)
	TWeakObjectPtr<AHorse> Horse;                    // (server) the owned horse in the world
	void GrantHorse(AHorse* Tamed);                  // (server) the quest's reward: Tamed becomes yours (or a new one at the stable)
	AHorse* MyHorse() const { return Horse.Get(); }
	// saving (JeebSave)
	void Capture(bool& bOut, FString& OutStable) const { bOut = bOwnsHorse; OutStable = StableKey.ToString(); }
	void Restore(bool bIn, const FString& InStable);
};
