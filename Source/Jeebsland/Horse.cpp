#include "Horse.h"
#include "Terrain.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"

AHorse::AHorse()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(Root);

	ConstructorHelpers::FObjectFinder<UStaticMesh> SphereF(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> CylF(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> ConeF(TEXT("/Engine/BasicShapes/Cone.Cone"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeF(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = SphereF.Object; UStaticMesh* Cyl = CylF.Object; UStaticMesh* Cone = ConeF.Object; UStaticMesh* Cube = CubeF.Object;

	const FLinearColor Coat(0.40f, 0.20f, 0.09f);
	const FLinearColor Dark(0.07f, 0.045f, 0.03f);
	const FLinearColor Hoof(0.05f, 0.04f, 0.04f);
	const FLinearColor White(0.92f, 0.90f, 0.86f);
	const FLinearColor Leather(0.26f, 0.12f, 0.06f);
	const FLinearColor Blanket(0.72f, 0.09f, 0.08f);

	int32 Count = 0;
	auto Pivot = [&](const TCHAR* Name, USceneComponent* Parent, const FVector& Loc) -> USceneComponent*
	{
		USceneComponent* P = CreateDefaultSubobject<USceneComponent>(Name);
		P->SetupAttachment(Parent);
		P->SetRelativeLocation(Loc);
		return P;
	};
	auto Part = [&](USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Loc, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Part%d"), Count++));
		C->SetupAttachment(Parent);
		C->SetStaticMesh(Mesh);
		C->SetRelativeLocation(Loc);
		C->SetRelativeRotation(Rot);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		Parts.Add(TPair<UStaticMeshComponent*, FLinearColor>(C, Color));
	};

	// ---- Body ----
	Part(BodyPivot, Sphere, FVector(0, 0, 108), FRotator::ZeroRotator, FVector(2.0f, 0.72f, 0.78f), Coat);
	Part(BodyPivot, Sphere, FVector(72, 0, 106), FRotator::ZeroRotator, FVector(0.9f, 0.74f, 0.85f), Coat);       // chest
	Part(BodyPivot, Sphere, FVector(-66, 0, 110), FRotator::ZeroRotator, FVector(0.95f, 0.76f, 0.82f), Coat);     // rump
	// Saddle and blanket
	Part(BodyPivot, Cube, FVector(-4, 0, 148), FRotator::ZeroRotator, FVector(0.55f, 0.62f, 0.04f), Blanket);
	Part(BodyPivot, Cube, FVector(-4, 0, 154), FRotator::ZeroRotator, FVector(0.42f, 0.36f, 0.1f), Leather);
	Part(BodyPivot, Cube, FVector(15, 0, 160), FRotator::ZeroRotator, FVector(0.1f, 0.2f, 0.14f), Leather);      // pommel
	Part(BodyPivot, Cube, FVector(-24, 0, 158), FRotator::ZeroRotator, FVector(0.1f, 0.3f, 0.12f), Leather);     // cantle

	// ---- Neck and head (the neck pivot lets the head bob and graze) ----
	NeckPivot = Pivot(TEXT("NeckPivot"), BodyPivot, FVector(78, 0, 120));
	Part(NeckPivot, Cyl, FVector(21.5f, 0, 27.6f), FRotator(-38.f, 0.f, 0.f), FVector(0.24f, 0.24f, 0.7f), Coat);
	for (int32 k = 0; k < 6; ++k)   // mane along the back of the neck
	{
		const float T = (k + 0.5f) / 6.f;
		Part(NeckPivot, Cube, FVector(43.f * T - 9.f, 0, 55.f * T + 4.f), FRotator(-38.f, 0.f, 0.f), FVector(0.07f, 0.06f, 0.16f), Dark);
	}
	Part(NeckPivot, Sphere, FVector(62, 0, 46), FRotator(-25.f, 0.f, 0.f), FVector(0.55f, 0.23f, 0.25f), Coat);    // head
	Part(NeckPivot, Sphere, FVector(84, 0, 36), FRotator(-25.f, 0.f, 0.f), FVector(0.22f, 0.2f, 0.2f), Dark);      // muzzle
	Part(NeckPivot, Sphere, FVector(66, 0, 53), FRotator(-25.f, 0.f, 0.f), FVector(0.34f, 0.07f, 0.1f), White);    // blaze
	Part(NeckPivot, Cone, FVector(46, -9, 68), FRotator::ZeroRotator, FVector(0.07f, 0.07f, 0.15f), Coat);
	Part(NeckPivot, Cone, FVector(46, 9, 68), FRotator::ZeroRotator, FVector(0.07f, 0.07f, 0.15f), Coat);
	Part(NeckPivot, Sphere, FVector(68, -11, 52), FRotator::ZeroRotator, FVector(0.06f, 0.05f, 0.06f), Hoof);
	Part(NeckPivot, Sphere, FVector(68, 11, 52), FRotator::ZeroRotator, FVector(0.06f, 0.05f, 0.06f), Hoof);

	// ---- Tail ----
	TailPivot = Pivot(TEXT("TailPivot"), BodyPivot, FVector(-108, 0, 122));
	Part(TailPivot, Cyl, FVector(-12, 0, -33), FRotator(-20.f, 0.f, 0.f), FVector(0.11f, 0.11f, 0.72f), Dark);

	// ---- Legs (each swings from its hip) ----
	auto Leg = [&](const TCHAR* Name, float X, float Y) -> USceneComponent*
	{
		USceneComponent* P = Pivot(Name, Root, FVector(X, Y, 80));
		Part(P, Cyl, FVector(0, 0, -32), FRotator::ZeroRotator, FVector(0.14f, 0.14f, 0.64f), Coat);
		Part(P, Cyl, FVector(0, 0, -71), FRotator::ZeroRotator, FVector(0.17f, 0.17f, 0.16f), Hoof);
		return P;
	};
	LegFL = Leg(TEXT("LegFL"), 64, -20);
	LegFR = Leg(TEXT("LegFR"), 64, 20);
	LegBL = Leg(TEXT("LegBL"), -66, -20);
	LegBR = Leg(TEXT("LegBR"), -66, 20);
}

void AHorse::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (const TPair<UStaticMeshComponent*, FLinearColor>& P : Parts)
	{
		if (UMaterialInstanceDynamic* M = P.Key->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), P.Value);
	}
	LastPos = GetActorLocation();
	GrazeT = FMath::FRandRange(3.f, 10.f);
}

void AHorse::CallTo(AActor* Target)
{
	if (HasRider() || !Target) return;
	CallTarget = Target;
	bCalled = true;
}

void AHorse::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	Clock += Dt;

	if (AActor* R = Rider.Get())
	{
		// Carried along by the rider
		const FVector L = R->GetActorLocation();
		// the horse eases round after the rider, so a turn never looks like a jerk
		const FRotator Turned = FMath::RInterpTo(GetActorRotation(), FRotator(0.f, R->GetActorRotation().Yaw, 0.f), Dt, 7.f);
		SetActorLocationAndRotation(FVector(L.X, L.Y, L.Z - 96.f), Turned);
	}
	else if (bCalled && CallTarget.IsValid())
	{
		// Gallop to whoever called, stopping a few steps away
		const FVector Me = GetActorLocation();
		const FVector To = CallTarget->GetActorLocation() - Me;
		const float D = To.Size2D();
		if (D < 230.f) bCalled = false;
		else
		{
			const FVector2D Dir = FVector2D(To.X, To.Y).GetSafeNormal();
			const float Speed = FMath::Min(1040.f, 240.f + D * 1.2f);   // 20% slower than before
			FVector NewLoc(Me.X + Dir.X * Speed * Dt, Me.Y + Dir.Y * Speed * Dt, 0.f);
			NewLoc.Z = JeebTerrain::HeightAt(NewLoc.X, NewLoc.Y);
			SetActorLocation(NewLoc);
			const FRotator Want(0.f, FVector(Dir.X, Dir.Y, 0.f).Rotation().Yaw, 0.f);
			SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Want, Dt, 260.f));
		}
	}

	// How fast are we actually moving? That drives the legs.
	const FVector Now = GetActorLocation();
	const float Speed = FVector2D(Now.X - LastPos.X, Now.Y - LastPos.Y).Size() / FMath::Max(Dt, 0.001f);
	LastPos = Now;
	SpeedBlend = FMath::FInterpTo(SpeedBlend, FMath::Clamp(Speed / 250.f, 0.f, 1.f), Dt, 8.f);
	// Gait: a trot swings the legs in diagonal pairs; at a full gallop the two front legs swing together and the two back legs
	// swing together (a bound), the back pair a little ahead of the front, with the whole body rocking and lifting with each stride.
	const float Gallop = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((Speed - 850.f) / 250.f, 0.f, 1.f));
	if (SpeedBlend > 0.02f) Phase += Dt * (4.f + Speed * 0.006f) * FMath::Lerp(1.f, 0.85f, Gallop);

	const float Amp = (25.f + 30.f * Gallop) * SpeedBlend;
	const float SinA = FMath::Sin(Phase), SinFront = FMath::Sin(Phase), SinBack = FMath::Sin(Phase + 2.1f);
	const float FL = FMath::Lerp(SinA, SinFront, Gallop), FR = FMath::Lerp(-SinA, SinFront, Gallop);
	const float BL = FMath::Lerp(-SinA, SinBack, Gallop), BR = FMath::Lerp(SinA, SinBack, Gallop);
	LegFL->SetRelativeRotation(FRotator(FL * Amp, 0.f, 0.f));
	LegFR->SetRelativeRotation(FRotator(FR * Amp, 0.f, 0.f));
	LegBL->SetRelativeRotation(FRotator(BL * Amp, 0.f, 0.f));
	LegBR->SetRelativeRotation(FRotator(BR * Amp, 0.f, 0.f));

	const float Bob = FMath::Lerp(4.f * FMath::Abs(SinA), 9.f * FMath::Abs(FMath::Sin(Phase + 1.0f)), Gallop);
	BodyPivot->SetRelativeLocation(FVector(0.f, 0.f, Bob * SpeedBlend + 1.5f * FMath::Sin(Clock * 1.6f) * (1.f - SpeedBlend)));
	const float Rock = FMath::Lerp(2.5f * FMath::Sin(Phase * 2.f), 6.f * FMath::Sin(Phase + 0.6f), Gallop);   // the body pitches with each stride
	BodyPivot->SetRelativeRotation(FRotator(Rock * SpeedBlend, 0.f, 0.f));

	// Head: bobs while moving; now and then it lowers to graze when standing free
	GrazeT -= Dt;
	float Graze = 0.f;
	if (!HasRider() && SpeedBlend < 0.05f && GrazeT < 0.f)
	{
		const float U = -GrazeT;
		Graze = FMath::Sin(FMath::Clamp(U / 4.f, 0.f, 1.f) * PI);
		if (U > 4.f) GrazeT = FMath::FRandRange(6.f, 14.f);
	}
	NeckPivot->SetRelativeRotation(FRotator(-48.f * Graze + 5.f * FMath::Sin(Phase * 2.f) * SpeedBlend, 0.f, 0.f));
	TailPivot->SetRelativeRotation(FRotator(-35.f * SpeedBlend, 14.f * FMath::Sin(Clock * 2.6f), 0.f));
}
