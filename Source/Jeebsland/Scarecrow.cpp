#include "Scarecrow.h"
#include "HealthComponent.h"
#include "KnightCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"

AScarecrow::AScarecrow()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Solid = CreateDefaultSubobject<UBoxComponent>(TEXT("Solid"));
	Solid->SetupAttachment(Root);
	Solid->SetBoxExtent(FVector(24.f, 24.f, 110.f));
	Solid->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	Solid->SetCollisionProfileName(TEXT("BlockAll"));
	Solid->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(Root);
	ArmRPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ArmRPivot"));
	ArmRPivot->SetupAttachment(BodyPivot);
	ArmRPivot->SetRelativeLocation(FVector(0.f, 0.f, 150.f));

	Hp = CreateDefaultSubobject<UHealthComponent>(TEXT("Hp"));
	Hp->Max = Hp->Cur = 60;
	Hp->BarHeight = 300.f;
	Hp->HitHeight = 130.f;
	Hp->HitRadius = 85.f;

	ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));

	int32 N = 0;
	auto Part = [&](USceneComponent* Parent, UStaticMesh* M, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Col)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("P%d"), N++));
		C->SetupAttachment(Parent);
		C->SetStaticMesh(M);
		C->SetRelativeLocation(Loc);
		C->SetRelativeRotation(Rot);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		Parts.Add(TPair<UStaticMeshComponent*, FLinearColor>(C, Col));
	};
	const FLinearColor Wood(0.25f, 0.15f, 0.07f), Cloth(0.42f, 0.16f, 0.08f), Straw(0.75f, 0.58f, 0.18f);
	Part(BodyPivot, Cyl.Object, FVector(0, 0, 100), FRotator::ZeroRotator, FVector(0.09f, 0.09f, 2.0f), Wood);                 // post
	Part(BodyPivot, Cube.Object, FVector(0, 0, 130), FRotator::ZeroRotator, FVector(0.38f, 0.24f, 0.56f), Cloth);               // shirt
	Part(BodyPivot, Cube.Object, FVector(0, 0, 98), FRotator::ZeroRotator, FVector(0.30f, 0.20f, 0.14f), Straw);                // straw at the waist
	Part(BodyPivot, Cyl.Object, FVector(0, -42, 150), FRotator(0.f, 0.f, 90.f), FVector(0.08f, 0.08f, 0.85f), Wood);            // left arm (fixed)
	Part(BodyPivot, Cube.Object, FVector(0, -85, 150), FRotator::ZeroRotator, FVector(0.16f, 0.16f, 0.16f), Straw);
	Part(ArmRPivot, Cyl.Object, FVector(0, 42, 0), FRotator(0.f, 0.f, 90.f), FVector(0.08f, 0.08f, 0.85f), Wood);             // right arm swings
	Part(ArmRPivot, Cube.Object, FVector(0, 85, 0), FRotator::ZeroRotator, FVector(0.16f, 0.16f, 0.16f), Straw);
	Part(BodyPivot, Sphere.Object, FVector(0, 0, 178), FRotator::ZeroRotator, FVector(0.46f, 0.46f, 0.42f), FLinearColor(1.3f, 0.45f, 0.04f)); // pumpkin head
	Part(BodyPivot, Cube.Object, FVector(21, -9, 184), FRotator::ZeroRotator, FVector(0.05f, 0.07f, 0.07f), FLinearColor(4.f, 2.f, 0.2f));    // glowing eyes
	Part(BodyPivot, Cube.Object, FVector(21, 9, 184), FRotator::ZeroRotator, FVector(0.05f, 0.07f, 0.07f), FLinearColor(4.f, 2.f, 0.2f));
	Part(BodyPivot, Cube.Object, FVector(21, 0, 168), FRotator::ZeroRotator, FVector(0.05f, 0.22f, 0.05f), FLinearColor(4.f, 2.f, 0.2f));    // grin
	Part(BodyPivot, Cone.Object, FVector(0, 0, 255), FRotator::ZeroRotator, FVector(0.52f, 0.52f, 0.44f), Wood);                // hat
	Part(BodyPivot, Cyl.Object, FVector(0, 0, 232), FRotator::ZeroRotator, FVector(0.84f, 0.84f, 0.03f), Wood);                  // brim
}

void AScarecrow::BeginPlay()
{
	Super::BeginPlay();
	HomeYaw = GetActorRotation().Yaw;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (const TPair<UStaticMeshComponent*, FLinearColor>& P : Parts)
	{
		if (UMaterialInstanceDynamic* M = P.Key->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), P.Value);
	}
	Hp->OnDamaged = [this](AActor* From) { OnHurt(From); };
}

void AScarecrow::OnHurt(AActor* From)
{
	HitFlash = 0.3f;
	if (Hp->Cur <= 0 && DeathT <= 0.f)
	{
		DeathT = 0.001f;
		RespawnT = 15.f;
		AttackT = -1.f;
		Solid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AScarecrow::Rise()
{
	Hp->Reset();
	DeathT = 0.f;
	HitFlash = 0.f;
	AttackCool = 2.f;
	BodyPivot->SetRelativeRotation(FRotator::ZeroRotator);
	BodyPivot->SetRelativeScale3D(FVector::OneVector);
	SetActorHiddenInGame(false);
	Solid->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AScarecrow::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = DeltaSeconds;

	if (DeathT > 0.f)
	{
		// Topple over, vanish, wait, then stand up again
		if (DeathT < 1.f)
		{
			DeathT += Dt * 2.5f;
			const float K = FMath::Clamp(DeathT, 0.f, 1.f);
			BodyPivot->SetRelativeRotation(FRotator(0.f, 0.f, 85.f * K * K));
			if (DeathT >= 1.f) { SetActorHiddenInGame(true); DeathT = 2.f; }
		}
		RespawnT -= Dt;
		if (RespawnT <= 0.f) Rise();
		return;
	}

	if (HitFlash > 0.f) HitFlash -= Dt;
	const float Hurt = FMath::Clamp(HitFlash / 0.3f, 0.f, 1.f);

	APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);
	AKnightCharacter* Knight = Cast<AKnightCharacter>(P);
	float Yaw = GetActorRotation().Yaw;
	float Dist = 1.0e9f;
	if (Knight)
	{
		const FVector To = Knight->GetActorLocation() - GetActorLocation();
		Dist = To.Size2D();
		if (Dist < 700.f)
		{
			const float Want = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
			Yaw = FMath::FixedTurn(Yaw, Want, 120.f * Dt);
			SetActorRotation(FRotator(0.f, Yaw, 0.f));
		}
	}

	// A light shove at close range
	AttackCool -= Dt;
	if (AttackT < 0.f && Knight && Dist < 230.f && AttackCool <= 0.f && Hp->Cur > 0)
	{
		AttackT = 0.f;
		bAttackLanded = false;
		AttackCool = 2.4f;
	}
	float ArmYaw = 0.f;
	if (AttackT >= 0.f)
	{
		AttackT += Dt;
		const float T = AttackT / 0.7f;
		ArmYaw = (T < 0.4f) ? FMath::Lerp(0.f, 25.f, T / 0.4f) : FMath::Lerp(-95.f, 0.f, FMath::Clamp((T - 0.55f) / 0.45f, 0.f, 1.f));
		if (T >= 0.4f && T < 0.55f) ArmYaw = FMath::Lerp(25.f, -95.f, (T - 0.4f) / 0.15f);
		if (!bAttackLanded && T >= 0.5f)
		{
			bAttackLanded = true;
			if (Knight && Dist < 260.f) Knight->Hurt(FMath::RandRange(3, 6), this);
		}
		if (T >= 1.f) AttackT = -1.f;
	}
	ArmRPivot->SetRelativeRotation(FRotator(0.f, ArmYaw, 0.f));

	// Sway, and a lean away when hit
	const float Sway = 2.f * FMath::Sin(GetWorld()->GetTimeSeconds() * 1.3f + GetActorLocation().X * 0.01f);
	BodyPivot->SetRelativeRotation(FRotator(0.f, 0.f, Sway + 14.f * Hurt * FMath::Sin(Hurt * 12.f)));
}
