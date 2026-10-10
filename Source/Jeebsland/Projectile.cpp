#include "Projectile.h"
#include "KnightCharacter.h"
#include "HealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Inventory.h"

AProjectile::AProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Body->SetStaticMesh(Cube.Object);
	BallMesh = Ball.Object;
	SetLifeSpan(8.f);
}

void AProjectile::Launch(AKnightCharacter* InShooter, const FVector& Dir, float InSpeed, float MaxRange, int32 InDamage, bool bInCrit, ESkillType InSkill, bool bMagic, int32 Tier)
{
	Shooter = InShooter;
	Velocity = Dir.GetSafeNormal() * InSpeed;
	Range = MaxRange;
	Damage = InDamage;
	bCrit = bInCrit;
	Skill = InSkill;
	bBolt = bMagic;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	FLinearColor Col;
	if (bMagic)
	{
		Body->SetStaticMesh(BallMesh);
		Body->SetWorldScale3D(FVector(0.28f));
		Col = (InSkill == ESkillType::Magic && Tier >= 100) ? FLinearColor(2.6f, 0.5f, 3.f) : FLinearColor(0.6f, 1.5f, 3.2f);
	}
	else
	{
		Body->SetWorldScale3D(FVector(0.7f, 0.025f, 0.025f));
		static const FLinearColor Tips[JeebItems::NumTiers] = {
			FLinearColor(0.35f, 0.2f, 0.08f), FLinearColor(0.7f, 0.4f, 0.2f), FLinearColor(0.35f, 0.35f, 0.4f),
			FLinearColor(0.6f, 0.6f, 0.7f), FLinearColor(0.4f, 1.2f, 1.6f) };
		Col = Tips[FMath::Clamp(Tier, 0, JeebItems::NumTiers - 1)];
	}
	if (UMaterialInstanceDynamic* M = Body->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), Col);
	SetActorRotation(Velocity.Rotation());
}

void AProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBolt) Velocity.Z -= 400.f * DeltaSeconds;   // arrows sag
	const FVector From = GetActorLocation();
	const FVector To = From + Velocity * DeltaSeconds;

	// Anything with health close to the path?
	UHealthComponent* Hit = nullptr;
	float BestT = 2.f;
	for (UHealthComponent* H : UHealthComponent::All())
	{
		if (!H || !H->IsAlive() || H->GetOwner() == Shooter.Get()) continue;
		const FVector C = H->HitCentre();
		const FVector Seg = To - From;
		const float L2 = FMath::Max(Seg.SizeSquared(), 1.f);
		const float T = FMath::Clamp(FVector::DotProduct(C - From, Seg) / L2, 0.f, 1.f);
		if (FVector::Dist(C, From + Seg * T) < H->HitRadius && T < BestT) { BestT = T; Hit = H; }
	}
	if (Hit)
	{
		if (AKnightCharacter* K = Shooter.Get()) K->DealDamage(Hit, Damage, bCrit, Skill);
		Destroy();
		return;
	}
	// Solid ground or walls
	FHitResult Blocked;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(Projectile), false, Shooter.Get());
	if (GetWorld()->LineTraceSingleByChannel(Blocked, From, To, ECC_Visibility, Q))
	{
		Destroy();
		return;
	}
	SetActorLocation(To);
	if (!bBolt) SetActorRotation(Velocity.Rotation());
	Travelled += (To - From).Size();
	if (Travelled > Range) Destroy();
}
