#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KnightMesh.h"
#include "HitBoxes.h"
class FKnightAnimator;
#include "FoeKnight.generated.h"

class UCapsuleComponent;
class UProceduralMeshComponent;
class UHealthComponent;
class AKnightCharacter;

// A sparring knight in iron, with a sword and a kite shield, who guards the old pig pen. Moderate-to-easy: he telegraphs
// every swing (the sword goes up high for a moment before it comes down), raises his shield now and then after being hit,
// and never leaves the pen. Beaten, he drops a little purse and is back on his feet 20 seconds later.
// He is the same knight as the player's (FKnightAnimator, in iron), with the same clips; shield and sword from KnightDesign.h / WeaponDesign.h.
// Online he is the server's: it runs his fighting (against the nearest player) and sends every player where he is, what he
// is doing (RepAct, bRepBlock) and whether he is down; their machines only animate him.
UCLASS()
class AFoeKnight : public AActor
{
	GENERATED_BODY()

public:
	AFoeKnight();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void Setup(const FVector2D& InPenCenter, const FVector2D& InPenHalf);

	// A foe from Foes.csv (a bandit) instead of the pen knight (2026-11-18; FoeData.h, the brain in FoeBandit.cpp): the server sets it before
	// FinishSpawning (AFoeCamps). Spot: where he stands at his camp; Centre: the camp's fire (his leash is measured from it).
	// Online the kind travels in RepKind with the actor, so every machine dresses him the same way; everything else is the pen knight's.
	void SetupFoe(int32 InKind, int32 InCamp, const FVector2D& Spot, const FVector2D& Centre, int32 Seed);
	bool IsBandit() const { return RepKind != 255; }
	int32 FoeKind() const { return RepKind == 255 ? INDEX_NONE : int32(RepKind); }
	int32 GetCamp() const { return Camp; }
	FString FoeName() const;           // what players see over his head ("" for the pen knight)
	bool IsDown() const { return DeathT > 0.f; }
	void SetSleeping(bool bSleep);     // (server, AFoeCamps) no player within reach: a slow tick, sparse updates
	bool IsSleeping() const { return bSleeping; }
	uint8 MindNow() const { return uint8(Mind); }   // (tests and logs) 0 at home, 1 fighting, 2 walking home
	AActor* TargetNow() const;

	// A still sparring knight (MoveLab): he stands where he was put, never fights back, walks or falls; his health fills
	// again 3 s after the last blow (Smash's training mode). Set before FinishSpawning.
	bool bStill = false;
	float StillZ = 0.f;   // (MoveLab) the height a still knight stands at: the flat test floor, not the open world's ground under it
	float FloorZ(float X, float Y) const;   // where his middle stands at (X, Y)
	// ... holding his shield up the whole time (MoveLab's second one). Set before FinishSpawning.
	bool bStillGuard = false;
	// Both knights' shields guard the way a player's does (owner, 2026-10-31): a blow on the raised shield never hurts him; the
	// shield's guard (an iron kite shield's) takes all of it. The blow that empties it breaks the guard: the shield drops and
	// stays down until the guard is full again (GuardBack s after the last blow on it; the still knight 3 s, like his health).
	// A kick on the raised shield knocks him down (KnockDown): he falls on his back, lies a moment, then gets straight up or
	// rolls away to one side first (the still knight takes each in turn and walks back to his spot; the pen knight picks).

	// Tuning
	static constexpr int32 MaxHp = 90;
	static constexpr int32 HitMin = 11, HitMax = 15;   // raw damage of his swing (your armour softens it)
	static constexpr int32 Tier = 1;                   // iron (tier 1 since the 2026-10-30 redo)
	static constexpr float GuardBack = 6.f;            // the pen knight's broken guard is whole again this long after the last blow on it
	static constexpr float KnockFall = 0.35f, KnockLie = 1.f, KnockRoll = 0.5f, KnockRise = 0.45f, KnockRollDist = 130.f;
	static constexpr float StrikeReach = 200.f;        // (centre to centre; was 240, longer than a player's sword reaches)
	static constexpr float StrikeStun = 0.35f, StrikePush = 260.f;   // his cut's hitstun and push on a player

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UCapsuleComponent* Capsule = nullptr;
	UPROPERTY() USceneComponent* WristR = nullptr;   // the sword's joint in his right hand, placed by the pose
	UPROPERTY() TArray<UProceduralMeshComponent*> Meshes;
	TSharedPtr<FKnightAnimator> Anim;   // his body, on every machine: the hit boxes and hurt boxes read its pose
	float AnimAccum = 0.f;
	FVector SmVel = FVector::ZeroVector;
	void TickBody(float Dt);        // his pose, after whatever he did this frame
	UPROPERTY() TArray<UProceduralMeshComponent*> ShieldMeshes;   // his shield's parts
	UPROPERTY() UHealthComponent* Hp = nullptr;
	UPROPERTY() class UStealthSense* Stealth = nullptr;   // (2026-11-26) the shared detection meter (Stealth.h): whom a bandit has noticed

	FVector2D PenCenter = FVector2D::ZeroVector;
	FVector2D PenHalf = FVector2D(700.f, 450.f);

	// State
	enum class EAct : uint8 { Idle, Windup, Strike, Recover, Stun };   // (Stun: hitstun, StunDur s)
	float StunDur = 0.f;
	// Launched by a low sweep: flying (the server moves him; AirVel), landing on his back. GroundZ: where he lands
	bool bAir = false;
	FVector AirVel = FVector::ZeroVector;
	float GroundZ = 0.f;
	void Launch(AActor* From);
	void OnReact(AActor* From, const JeebHit::FReact& R);
	EAct Act = EAct::Idle;
	float ActT = 0.f;
	float AttackCool = 1.5f;
	float BlockT = 0.f;            // > 0 while the shield is up
	float BlockAlpha = 0.f;
	float StrafeDir = 1.f, StrafeT = 0.f;
	FVector2D WanderTo = FVector2D::ZeroVector;
	float WanderWait = 0.f;
	float HitFlash = 0.f;
	float GuardPool = 0.f, GuardMax = 0.f;   // his shield's guard
	bool bGuardBroken = false;
	float GuardHitTime = -100.f;
	bool TakeOnShield(int32 Amount, AActor* From);   // (server) the shield takes a blow; returns false if it isn't up and facing it
	// Knocked down by a kick (all machines play it from RepKnock: a count of knockdowns, times 4, plus how he gets up)
	float KnockT = -1.f;           // < 0 on his feet; seconds since the kick
	uint8 KnockOpt = 0;            // 0 straight up, 1 roll to his left, 2 to his right (never the same twice running)
	int32 KnockCount = 0;
	FVector Post = FVector::ZeroVector; float PostYaw = 0.f;   // (the still knight's spot)
	UPROPERTY(ReplicatedUsing = OnRep_Knock) uint8 RepKnock = 0;
	UFUNCTION() void OnRep_Knock();
	void KnockDown(AActor* From, bool bDrop = false);   // (bDrop: the low sweep, straight down where he stands, his guard untouched)
	void TickKnock(float Dt);      // the knockdown's timeline
	float DeathT = 0.f;           // 0 = alive; counts up while falling, then waits for the respawn
	float RespawnT = 0.f;
	bool bStruck = false;
	FRandomStream Rand;
	FVector LastLoc = FVector::ZeroVector;   // (his steps are worked out from how far he moved since the last frame)

	UPROPERTY(ReplicatedUsing = OnRep_Act) uint8 RepAct = 0;
	UPROPERTY(Replicated) bool bRepBlock = false;
	UPROPERTY(ReplicatedUsing = OnRep_Down) bool bRepDown = false;
	UFUNCTION() void OnRep_Act();
	UFUNCTION() void OnRep_Down();
	class AKnightCharacter* FindTarget(float& OutDist) const;   // the nearest player still standing

	UProceduralMeshComponent* MakePart(USceneComponent* Attach, int32 PivotId, const KM::FMD& Md);
	void Paint(UProceduralMeshComponent* C, const FLinearColor& Color, bool bMetal);
	void OnHurt(AActor* From);
	void Rise();
	bool MoveTowards(const FVector2D& To, float Speed, float Dt);
	FVector2D ClampToPen(const FVector2D& P) const;

	// What he is (the pen knight's numbers unless SetupFoe gave him a row of Foes.csv)
	UPROPERTY(Replicated) uint8 RepKind = 255;   // Foes.csv row, 255 = the pen knight
	int32 FoeTier = Tier, FoeShield = 0, FoeHitMin = HitMin, FoeHitMax = HitMax;
	float FoeTakes = -1.f, ChaseSpeed = 300.f, WindupT = 0.65f, CoolMin = 1.6f, CoolMax = 2.6f, BlockChance = 0.4f, RespawnSecs = 20.f;
	void ApplyKind();                  // the stats from his Foes.csv row (every machine)
	bool HasShieldArm() const { return FoeShield >= 0; }

	// The bandit's mind (server only; FoeBandit.cpp)
	enum class EMind : uint8 { Home, Fight, Return };
	EMind Mind = EMind::Home;
	int32 Camp = -1;
	FVector2D CampAt = FVector2D::ZeroVector;
	TWeakObjectPtr<AKnightCharacter> Target, LastAttacker;
	float LastAttackT = -100.f, LostT = 0.f, SenseT = 0.f, StuckT = 0.f, DownFor = 0.f, LastHurtT = -100.f;
	float StepSide = 1.f;                  // the way he steps round something in his path, kept until it is clear
	double StepSideAt = -10.0;             // (when StepSide last changed: it holds for StepSideHold, 0.6 s, so he never jitters left-right against a trunk)
	float StrollWorst = 0.f, StrollTurnT = 0.f;   // (test log -LogStroll: the worst heading-vs-step angle on this stroll leg, and how long he turned on the spot first)
	// (2026-11-18 night: stepping round something he faces the way he walks, not his foe, so he never crab-walks; FoeBandit.cpp BanditMove)
	float WalkYaw = 0.f;                   // the way his recent steps went, eased (degrees)
	double DetourAt = -10.0, LastStepAt = -10.0;   // (when he last stepped off the straight line round something, and last stepped at all)
	float DetourWorst = 0.f;               // (test log -LogDetour: the worst facing-vs-step angle while detouring, logged twice a second)
	double DetourLogAt = -10.0;
	int32 DetourSteps = 0, DetourCrab = 0;
	bool BanditDetouring() const;          // stepping round something just now: face WalkYaw
	void NoteStep(const FVector2D& StepDir, float OffAng, float Dt);   // (BanditMove: a step was taken, OffAng degrees off the straight line)
	float RegenAcc = 0.f;              // (health coming back at camp, a point at a time)
	bool bSleeping = false;
	void SetMind(EMind M, const TCHAR* Why);
	AKnightCharacter* BanditTarget(float& OutDist, float Dt);   // whom he fights now (null: nobody; he goes home or strolls)
	void BanditRoam(float Dt, float& Yaw);                       // nobody to fight: strolls round the fire, or walks home
	bool BanditMove(const FVector2D& To, float Speed, float Dt); // a step over the ground, round trees and rocks, never into a town or deep water
	void BanditHurt(AActor* From);                               // (server) hit: whoever did it is the one he fights
	void BanditBeaten(AActor* From);                             // (server) beaten: loot, XP, quests
	void BanditDownTick(float Dt);                               // (server) lying beaten: sinks away, then stands at camp again
	static class AKnightCharacter* Hitter(AActor* From);         // the player behind a blow (an arrow: whoever shot it)
	friend struct FFoeTestDriver;   // (-JeebFoeTest, FoeCamps.cpp)
	friend class AFoeCamps;
};
