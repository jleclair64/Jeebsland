#include "BatSwarm.h"
#include "Terrain.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

namespace
{
	constexpr int32 NumBats = 24;
	constexpr float SwarmSeconds = 16.f;
	constexpr float SwarmSpeed = 480.f;    // how fast the whole cluster travels
}

ABatSwarm::ABatSwarm()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto Make = [&](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UInstancedStaticMeshComponent* C = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		C->SetupAttachment(Root);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetCanEverAffectNavigation(false);
		return C;
	};
	Body = Make(TEXT("BatBody"), Sphere.Object);
	Wing = Make(TEXT("BatWing"), Cube.Object);
}

void ABatSwarm::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);
	Rand.Initialize(777);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (UInstancedStaticMeshComponent* C : { Body, Wing })
	{
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.035f, 0.03f, 0.045f));
	}
	TArray<FTransform> Hidden;
	for (int32 i = 0; i < NumBats; ++i) Hidden.Add(FTransform(FQuat::Identity, FVector(0, 0, -9000.f), FVector(0.001f)));
	Body->AddInstances(Hidden, false, false, false);
	TArray<FTransform> HiddenW;
	for (int32 i = 0; i < NumBats * 2; ++i) HiddenW.Add(FTransform(FQuat::Identity, FVector(0, 0, -9000.f), FVector(0.001f)));
	Wing->AddInstances(HiddenW, false, false, false);
	Body->SetVisibility(false);
	Wing->SetVisibility(false);
}

void ABatSwarm::StartSwarm()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APawn* P = PC ? PC->GetPawn() : nullptr;
	if (!P) return;

	const float Ang = Rand.FRandRange(0.f, 2.f * PI);
	Dir = FVector2D(FMath::Cos(Ang), FMath::Sin(Ang));
	const FVector Loc = P->GetActorLocation();
	Target = FVector(Loc.X + Rand.FRandRange(-900.f, 900.f), Loc.Y + Rand.FRandRange(-900.f, 900.f), 0.f);

	Bats.Reset();
	for (int32 i = 0; i < NumBats; ++i)
	{
		FBat B;
		B.Radius = Rand.FRandRange(120.f, 430.f);
		B.Speed = Rand.FRandRange(1.1f, 2.3f) * (Rand.FRand() < 0.5f ? 1.f : -1.f);
		B.Phase = Rand.FRandRange(0.f, 6.28f);
		B.HeightOff = Rand.FRandRange(-120.f, 260.f);
		B.Flap = Rand.FRandRange(15.f, 24.f);
		B.Size = Rand.FRandRange(0.85f, 1.35f);
		B.Lag = Rand.FRandRange(-260.f, 260.f);
		Bats.Add(B);
	}
	T = 0.f;
	bActive = true;
	Body->SetVisibility(true);
	Wing->SetVisibility(true);
}

FVector ABatSwarm::BatPos(int32 Index, float Time) const
{
	const FBat& B = Bats[Index];
	const FVector2D Perp(-Dir.Y, Dir.X);
	// The whole cluster crosses the area along a line; each bat circles around the middle of it
	const float Along = SwarmSpeed * (Time - SwarmSeconds * 0.5f) + B.Lag;
	const float Ang = B.Phase + B.Speed * Time;
	const FVector2D Flat = FVector2D(Target.X, Target.Y) + Dir * (Along + FMath::Cos(Ang) * B.Radius * 0.8f) + Perp * (FMath::Sin(Ang) * B.Radius);
	const float Z = JeebTerrain::HeightAt(Flat.X, Flat.Y) + 950.f + 160.f * FMath::Sin(Time * 0.6f) + B.HeightOff + 80.f * FMath::Sin(Ang * 1.7f + B.Phase);
	return FVector(Flat.X, Flat.Y, Z);
}

void ABatSwarm::HideAll()
{
	bActive = false;
	Body->SetVisibility(false);
	Wing->SetVisibility(false);
}

void ABatSwarm::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bActive)
	{
		WaitTimer -= DeltaSeconds;
		if (WaitTimer <= 0.f)
		{
			StartSwarm();
			WaitTimer = Rand.FRandRange(50.f, 90.f); // the next one comes after a while
		}
		return;
	}

	T += DeltaSeconds;
	if (T >= SwarmSeconds) { HideAll(); return; }
	const float Fade = FMath::Clamp(FMath::Min(T / 1.2f, (SwarmSeconds - T) / 1.5f), 0.f, 1.f);

	for (int32 i = 0; i < Bats.Num(); ++i)
	{
		const FBat& B = Bats[i];
		const FVector Pos = BatPos(i, T);
		FVector Vel = BatPos(i, T + 0.05f) - Pos;
		if (Vel.SizeSquared() < 1e-4f) Vel = FVector(Dir.X, Dir.Y, 0.f);
		const FQuat Q = FRotationMatrix::MakeFromX(Vel.GetSafeNormal()).ToQuat();
		const float S = B.Size * Fade;
		const bool bLast = (i == Bats.Num() - 1);

		// body: a small dark oval
		Body->UpdateInstanceTransform(i, FTransform(Q, Pos, FVector(0.34f, 0.16f, 0.14f) * S), true, bLast, true);

		// wings: flat panels that flap up and down about the body
		const float Flap = FMath::Sin(T * B.Flap + B.Phase) * FMath::DegreesToRadians(55.f);
		for (int32 w = 0; w < 2; ++w)
		{
			const float Sd = (w == 0) ? 1.f : -1.f;
			const FQuat Rq(FVector::XAxisVector, Sd * Flap);
			const FVector Centre = Pos + Q.RotateVector(Rq.RotateVector(FVector(0.f, Sd * 26.f * S, 0.f)) + FVector(0.f, Sd * 8.f * S, 0.f));
			Wing->UpdateInstanceTransform(i * 2 + w, FTransform(Q * Rq, Centre, FVector(0.22f, 0.5f, 0.012f) * S), true, bLast && w == 1, true);
		}
	}
}
