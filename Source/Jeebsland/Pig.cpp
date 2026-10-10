#include "Pig.h"
#include "HealthComponent.h"
#include "Terrain.h"
#include "SkillComponent.h"
#include "Loot.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Engine.h"

APig::APig()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(Root);
	Hp = CreateDefaultSubobject<UHealthComponent>(TEXT("Hp"));
	Hp->Max = Hp->Cur = 25;
	Hp->BarHeight = 115.f;
	Hp->HitHeight = 50.f;
	Hp->HitRadius = 80.f;

	ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));

	const FLinearColor Pink(0.92f, 0.55f, 0.58f);
	const FLinearColor DarkPink(0.80f, 0.38f, 0.44f);
	const FLinearColor Eye(0.02f, 0.02f, 0.02f);

	// One part: mesh, offset, rotation, scale (in 100 cm units), colour
	auto Part = [&](const TCHAR* Name, UStaticMesh* Mesh, FVector Loc, FRotator Rot, FVector Scale, FLinearColor Color)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(BodyPivot);
		C->SetStaticMesh(Mesh);
		C->SetRelativeLocation(Loc);
		C->SetRelativeRotation(Rot);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		Parts.Add(TPair<UStaticMeshComponent*, FLinearColor>(C, Color));
	};

	Part(TEXT("Body"),  Sphere.Object, FVector(0, 0, 52),   FRotator::ZeroRotator, FVector(1.05f, 0.62f, 0.62f), Pink);
	Part(TEXT("Head"),  Sphere.Object, FVector(55, 0, 56),  FRotator::ZeroRotator, FVector(0.42f, 0.38f, 0.38f), Pink);
	Part(TEXT("Snout"), Cyl.Object,    FVector(76, 0, 52),  FRotator(90.f, 0.f, 0.f), FVector(0.17f, 0.17f, 0.1f), DarkPink);
	Part(TEXT("EarL"),  Cone.Object,   FVector(52, -15, 78), FRotator(0.f, 0.f, -25.f), FVector(0.12f, 0.12f, 0.16f), DarkPink);
	Part(TEXT("EarR"),  Cone.Object,   FVector(52, 15, 78),  FRotator(0.f, 0.f, 25.f), FVector(0.12f, 0.12f, 0.16f), DarkPink);
	Part(TEXT("EyeL"),  Sphere.Object, FVector(69, -12, 64), FRotator::ZeroRotator, FVector(0.06f, 0.06f, 0.06f), Eye);
	Part(TEXT("EyeR"),  Sphere.Object, FVector(69, 12, 64),  FRotator::ZeroRotator, FVector(0.06f, 0.06f, 0.06f), Eye);
	Part(TEXT("Tail"),  Sphere.Object, FVector(-56, 0, 62), FRotator::ZeroRotator, FVector(0.1f, 0.1f, 0.1f), DarkPink);
	Part(TEXT("LegFL"), Cyl.Object,    FVector(32, -18, 18), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 0.36f), DarkPink);
	Part(TEXT("LegFR"), Cyl.Object,    FVector(32, 18, 18),  FRotator::ZeroRotator, FVector(0.12f, 0.12f, 0.36f), DarkPink);
	Part(TEXT("LegBL"), Cyl.Object,    FVector(-32, -18, 18), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 0.36f), DarkPink);
	Part(TEXT("LegBR"), Cyl.Object,    FVector(-32, 18, 18), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 0.36f), DarkPink);
}

void APig::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (const TPair<UStaticMeshComponent*, FLinearColor>& P : Parts)
	{
		if (UMaterialInstanceDynamic* M = P.Key->CreateDynamicMaterialInstance(0, Base))
		{
			M->SetVectorParameterValue(TEXT("Color"), P.Value);
		}
	}
	Hp->OnDamaged = [this](AActor* From) { TakeHit(From); };
	Wait = FMath::FRandRange(0.f, 2.f);
	WalkPhase = FMath::FRandRange(0.f, 6.f);
	Target = FVector2D(GetActorLocation().X, GetActorLocation().Y);
}

void APig::Setup(const FVector2D& InPenCenter, const FVector2D& InPenHalf)
{
	PenCenter = InPenCenter;
	PenHalf = InPenHalf;
}

bool APig::IsAlive() const { return Hp && Hp->Cur > 0; }

void APig::PickTarget()
{
	Target = PenCenter + FVector2D(FMath::FRandRange(-1.f, 1.f) * (PenHalf.X - 120.f), FMath::FRandRange(-1.f, 1.f) * (PenHalf.Y - 120.f));
}

void APig::SetVisible(bool bVisible)
{
	SetActorHiddenInGame(!bVisible);
}

void APig::Respawn()
{
	Hp->Reset();
	DeathT = 0.f;
	FleeTimer = 0.f;
	BodyPivot->SetRelativeRotation(FRotator::ZeroRotator);
	BodyPivot->SetRelativeScale3D(FVector::OneVector);
	PickTarget();
	SetActorLocation(FVector(Target.X, Target.Y, JeebTerrain::HeightAt(Target.X, Target.Y)));
	SetVisible(true);
	Wait = 1.f;
}

void APig::TakeHit(AActor* Attacker)
{
	if (DeathT > 0.f) return;
	HitFlash = 0.25f;

	const FVector Away = (GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal2D();
	if (Hp->Cur > 0)
	{
		// Squeal and bolt away from the attacker
		FleeTimer = 2.5f;
		const FVector2D P(GetActorLocation().X, GetActorLocation().Y);
		Target = FVector2D(
			FMath::Clamp(P.X + Away.X * 500.f, PenCenter.X - PenHalf.X + 100.f, PenCenter.X + PenHalf.X - 100.f),
			FMath::Clamp(P.Y + Away.Y * 500.f, PenCenter.Y - PenHalf.Y + 100.f, PenCenter.Y + PenHalf.Y - 100.f));
		Wait = 0.f;
		return;
	}

	// Dead: raw pork is left where it fell
	DeathT = 0.001f;
	RespawnT = 45.f;
		ALootDrop::SpawnPork(GetWorld(), GetActorLocation());   // raw pork on the ground: press E to take it
}

void APig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = DeltaSeconds;

	if (Hp->Cur <= 0)
	{
		// Tip over, then vanish; a new pig comes back later
		if (DeathT > 0.f && DeathT < 1.f)
		{
			DeathT += Dt * 2.5f;
			const float K = FMath::Clamp(DeathT, 0.f, 1.f);
			BodyPivot->SetRelativeRotation(FRotator(0.f, 0.f, 90.f * K * K * (3.f - 2.f * K)));
			if (DeathT >= 1.f) { SetVisible(false); DeathT = 2.f; }
		}
		RespawnT -= Dt;
		if (RespawnT <= 0.f) Respawn();
		return;
	}

	const FVector Loc = GetActorLocation();
	FVector2D P(Loc.X, Loc.Y);

	if (HitFlash > 0.f) HitFlash -= Dt;
	if (FleeTimer > 0.f) FleeTimer -= Dt;

	const FVector2D ToTarget = Target - P;
	const float Dist = ToTarget.Size();
	float DesiredSpeed = 0.f;
	if (Dist > 40.f && Wait <= 0.f)
	{
		DesiredSpeed = (FleeTimer > 0.f) ? 320.f : 85.f;
	}
	else
	{
		Wait -= Dt;
		if (Dist <= 40.f && Wait <= 0.f && FleeTimer <= 0.f)
		{
			Wait = FMath::FRandRange(1.5f, 5.f); // stand and snuffle
			PickTarget();
		}
		else if (Dist <= 40.f && FleeTimer > 0.f)
		{
			PickTarget();
		}
	}
	Speed = FMath::FInterpTo(Speed, DesiredSpeed, Dt, 6.f);

	if (Speed > 5.f && Dist > 1.f)
	{
		const FVector2D Dir = ToTarget / Dist;
		P += Dir * Speed * Dt;
		P.X = FMath::Clamp(P.X, PenCenter.X - PenHalf.X + 60.f, PenCenter.X + PenHalf.X - 60.f);
		P.Y = FMath::Clamp(P.Y, PenCenter.Y - PenHalf.Y + 60.f, PenCenter.Y + PenHalf.Y - 60.f);
		const float WantYaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, WantYaw, 360.f * Dt);
		SetActorRotation(FRotator(0.f, Yaw, 0.f));
	}
	SetActorLocation(FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y)));

	// Trotting bob, and a little wiggle when hit
	WalkPhase += Dt * (4.f + Speed * 0.03f);
	const float Trot = FMath::Clamp(Speed / 100.f, 0.f, 1.f);
	const float Hurt = FMath::Clamp(HitFlash / 0.25f, 0.f, 1.f);
	BodyPivot->SetRelativeLocation(FVector(0.f, 0.f, 3.f * Trot * FMath::Abs(FMath::Sin(WalkPhase))));
	BodyPivot->SetRelativeRotation(FRotator(-6.f * Hurt, 8.f * Trot * FMath::Sin(WalkPhase), 4.f * Hurt * FMath::Sin(Dt * 0.f + WalkPhase * 3.f)));
}
