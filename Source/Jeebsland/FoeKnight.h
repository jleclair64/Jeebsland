#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KnightMesh.h"
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

	// Tuning
	static constexpr int32 MaxHp = 90;
	static constexpr int32 HitMin = 11, HitMax = 15;   // raw damage of his swing (your armour softens it)
	static constexpr int32 Tier = 1;                   // iron (tier 1 since the 2026-10-30 redo)
	static constexpr float BlockStops = 0.7f;          // a blow on his raised shield loses 70%

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
	enum class EAct : uint8 { Idle, Windup, Strike, Recover };
	EAct Act = EAct::Idle;
	float ActT = 0.f;
	float AttackCool = 1.5f;
	float BlockT = 0.f;            // > 0 while the shield is up
	float BlockAlpha = 0.f;
	float StrafeDir = 1.f, StrafeT = 0.f;
	FVector2D WanderTo = FVector2D::ZeroVector;
	float WanderWait = 0.f;
	float HitFlash = 0.f;
	float DeathT = 0.f;            // 0 = alive; counts up while falling, then waits for the respawn
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
