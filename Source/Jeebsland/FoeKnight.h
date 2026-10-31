#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KnightMesh.h"
#include "HitBoxes.h"
#include "FoeKnight.generated.h"

class UCapsuleComponent;
class UProceduralMeshComponent;
class UHealthComponent;

// A sparring knight in iron, with a sword and a kite shield, who guards the old pig pen. Moderate-to-easy: he telegraphs
// every swing (the sword goes up high for a moment before it comes down), raises his shield now and then after being hit,
// and never leaves the pen. Beaten, he drops a little purse and is back on his feet 20 seconds later.
// He is built from the same body shapes as the player's knight (KnightDesign.h / WeaponDesign.h).
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

	// A still sparring knight (MoveLab): he stands where he was put, never fights back, walks or falls; his health fills
	// again 3 s after the last blow (Smash's training mode). Set before FinishSpawning.
	bool bStill = false;
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
	UPROPERTY() USceneComponent* BodyRoot = nullptr;
	UPROPERTY() USceneComponent* UpperPivot = nullptr;
	UPROPERTY() USceneComponent* HeadPivot = nullptr;
	UPROPERTY() USceneComponent* PlumePivot = nullptr;
	UPROPERTY() USceneComponent* ArmL = nullptr;
	UPROPERTY() USceneComponent* ArmR = nullptr;
	UPROPERTY() USceneComponent* ElbowL = nullptr;
	UPROPERTY() USceneComponent* ElbowR = nullptr;
	UPROPERTY() USceneComponent* WristR = nullptr;   // (the model's right fist hangs off it; it never turns on the pen knight)
	UPROPERTY() USceneComponent* HipL = nullptr;
	UPROPERTY() USceneComponent* HipR = nullptr;
	UPROPERTY() USceneComponent* KneeL = nullptr;
	UPROPERTY() USceneComponent* KneeR = nullptr;
	UPROPERTY() USceneComponent* AnkleL = nullptr;   // the feet turn here to stay flat on the ground
	UPROPERTY() USceneComponent* AnkleR = nullptr;
	UPROPERTY() USceneComponent* CapeUpper = nullptr;
	UPROPERTY() USceneComponent* CapeLower = nullptr;
	UPROPERTY() TArray<UProceduralMeshComponent*> Meshes;
	UPROPERTY() UHealthComponent* Hp = nullptr;

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
	void KnockDown(AActor* From);
	void TickKnock(float Dt);      // the knockdown's timeline and pose (after the rest of the pose)
	float DeathT = 0.f;           // 0 = alive; counts up while falling, then waits for the respawn
	float RespawnT = 0.f;
	float GaitPhase = 0.f, MoveBlend = 0.f;
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
};
