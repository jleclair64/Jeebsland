#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkillComponent.h"
#include "Projectile.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UPointLightComponent;
class AKnightCharacter;

// How an arrow or bolt was fired (sent to every player, who fly their own copy of it). A deflected arrow is launched again from
// where it glanced off (Seq counts the launches).
USTRUCT()
struct FProjLaunch
{
	GENERATED_BODY()
	UPROPERTY() FVector_NetQuantize Start = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantizeNormal Dir = FVector::ForwardVector;
	UPROPERTY() float Speed = 0.f;
	UPROPERTY() float Range = 3000.f;
	UPROPERTY() bool bBolt = false;
	UPROPERTY() uint8 Tier = 0;
	UPROPERTY() bool bPurple = false;
	UPROPERTY() int32 ArrowId = 0;   // which arrows (0: a bolt): fire arrows glow and burn
	UPROPERTY() uint8 Seq = 0;
	UPROPERTY() float Sag = 300.f;   // cm/s/s it drops (a weakly drawn arrow drops fast and lands close)
};

// Where an arrow ended up (owner, 2026-10-31): stuck in the ground, a tree, a shield or a body for StuckSeconds. On (with Comp)
// is what it is stuck in; Loc and Rot are then relative to that part. Arrows in the world can be picked up again (E); arrows
// that struck a knight's armour or shield are spent (never offered, so nobody is asked mid-fight).
USTRUCT()
struct FProjStuck
{
	GENERATED_BODY()
	UPROPERTY() bool bStuck = false;
	UPROPERTY() TObjectPtr<AActor> On = nullptr;
	UPROPERTY() FName Comp;            // a joint by its name, or "Shield" + Index (the raised shield's parts on every machine)
	UPROPERTY() uint8 Index = 0;
	UPROPERTY() FVector_NetQuantize10 Loc = FVector::ZeroVector;
	UPROPERTY() FRotator Rot = FRotator::ZeroRotator;
	UPROPERTY() bool bPickable = false;
};

// An arrow or a magic bolt. Damage is rolled when it is fired; it lands on the first hit box, raised shield or solid thing on
// its way. Online the server fires it and does the hitting; every player's machine flies a copy for show.
UCLASS()
class AProjectile : public AActor
{
	GENERATED_BODY()

public:
	AProjectile();

	// bMagic: glowing bolt that flies straight; otherwise an arrow that drops a little. ArrowId: which arrows (fire arrows burn).
	void Launch(AKnightCharacter* InShooter, const FVector& Dir, float InSpeed, float MaxRange, int32 InDamage, bool bInCrit, ESkillType InSkill, bool bMagic, int32 Tier, int32 InArrowId = 0, float InSag = ArrowSag);

	// What the reticle is on: how far along A..B (0..1; 2 = nothing) the first body or raised shield with hit boxes lies, other
	// than Ignore. (Knights and foes are not solid to the world's traces, so aiming with a trace alone looks straight through them.)
	static float FirstBodyAlong(UWorld* World, const FVector& A, const FVector& B, const AActor* Ignore);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Picking it up (E): only arrows lying in the world, not spent ones
	bool CanPickUp() const { return Stuck.bStuck && Stuck.bPickable && !IsHidden(); }
	int32 GetArrowId() const { return L.ArrowId; }
	static constexpr float StuckSeconds = 120.f;
	static constexpr int32 BurnTicks = 3, BurnDamage = 2;   // fire arrows: BurnDamage points BurnTicks times, BurnEvery s apart, after the hit
	static constexpr float BurnEvery = 0.7f;
	static constexpr float ArrowSag = 300.f;   // cm/s/s: arrows drop a little (bolts fly straight)
	static constexpr float TrailLife = 0.35f;   // s a bit of the white streak behind a flying arrow lasts
	static constexpr float TrailWidth = 4.5f;   // cm either side of its middle, where it is newest
	static constexpr float FlyScale = 1.7f;     // arrows are drawn this much bigger while flying (easier to follow), true size once they land

private:
	UPROPERTY() USceneComponent* Root = nullptr;
	UPROPERTY() UStaticMeshComponent* Body = nullptr;    // the shaft (or the bolt's ball)
	UPROPERTY() UStaticMeshComponent* Head = nullptr;    // the arrowhead (the tip is the actor's own position)
	UPROPERTY() UStaticMeshComponent* Fletch = nullptr;
	UPROPERTY() UPointLightComponent* Glow = nullptr;   // (fire arrows, players' machines only)
	UPROPERTY() class UProceduralMeshComponent* Trail = nullptr;   // the white streak behind it (players' machines only)
	TArray<TPair<FVector, float>> TrailPts;   // where its tail was, and when
	void TickTrail();
	UPROPERTY(Replicated) AKnightCharacter* Shooter = nullptr;
	UPROPERTY(ReplicatedUsing = OnRep_Launch) FProjLaunch L;
	UPROPERTY(ReplicatedUsing = OnRep_Stuck) FProjStuck Stuck;
	UFUNCTION() void OnRep_Launch();
	UFUNCTION() void OnRep_Stuck();
	void ApplyLaunch();   // the look and the flight, from L
	void ApplyStuck();    // put it where Stuck says (every machine)
	void StickAt(const FVector& Tip, AActor* On, USceneComponent* Comp, FName CompName, uint8 Index, bool bPickable);   // (server)
	void Deflect(const FVector& At, const FVector& Away);   // (server) glanced off the edge of a raised shield
	void Burn(AActor* Target);   // (server) a fire arrow's hit keeps burning
	USceneComponent* FindPart(AActor* On, FName CompName, uint8 Index) const;
	uint8 SeenSeq = 255;
	bool bFlying = false;
	bool bSpent = false;          // glanced off a shield: no damage, and no picking it up
	FVector Velocity = FVector::ZeroVector;
	float Travelled = 0.f;
	int32 Damage = 1;
	bool bCrit = false;
	ESkillType Skill = ESkillType::Archery;
	UPROPERTY() UStaticMesh* BallMesh = nullptr;
};
