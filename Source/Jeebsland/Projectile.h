#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkillComponent.h"
#include "Projectile.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class AKnightCharacter;

// How an arrow or bolt was fired (sent to every player, who fly their own copy of it)
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
};

// An arrow or a magic bolt. Damage is rolled when it is fired; it hits the first thing with health on its way.
// Online the server fires it and does the hitting; every player's machine flies a copy for show.
UCLASS()
class AProjectile : public AActor
{
	GENERATED_BODY()

public:
	AProjectile();

	// bMagic: glowing bolt that flies straight; otherwise an arrow that drops a little
	void Launch(AKnightCharacter* InShooter, const FVector& Dir, float InSpeed, float MaxRange, int32 InDamage, bool bInCrit, ESkillType InSkill, bool bMagic, int32 Tier);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY() UStaticMeshComponent* Body = nullptr;
	UPROPERTY(Replicated) AKnightCharacter* Shooter = nullptr;
	UPROPERTY(ReplicatedUsing = OnRep_Launch) FProjLaunch L;
	UFUNCTION() void OnRep_Launch();
	void ApplyLaunch();   // the look and the flight, from L
	bool bFlying = false;
	FVector Velocity = FVector::ZeroVector;
	float Travelled = 0.f;
	int32 Damage = 1;
	bool bCrit = false;
	ESkillType Skill = ESkillType::Archery;
	UPROPERTY() UStaticMesh* BallMesh = nullptr;
};
