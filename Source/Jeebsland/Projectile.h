#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SkillComponent.h"
#include "Projectile.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class AKnightCharacter;

// An arrow or a magic bolt. Damage is rolled when it is fired; it hits the first thing with health on its way.
UCLASS()
class AProjectile : public AActor
{
	GENERATED_BODY()

public:
	AProjectile();

	// bMagic: glowing bolt that flies straight; otherwise an arrow that drops a little
	void Launch(AKnightCharacter* InShooter, const FVector& Dir, float InSpeed, float MaxRange, int32 InDamage, bool bInCrit, ESkillType InSkill, bool bMagic, int32 Tier);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() UStaticMeshComponent* Body = nullptr;
	TWeakObjectPtr<AKnightCharacter> Shooter;
	FVector Velocity = FVector::ZeroVector;
	float Travelled = 0.f;
	float Range = 3000.f;
	int32 Damage = 1;
	bool bCrit = false;
	bool bBolt = false;
	ESkillType Skill = ESkillType::Archery;
	UPROPERTY() UStaticMesh* BallMesh = nullptr;
};
