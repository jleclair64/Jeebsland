#include "SmokeCloud.h"
#include "KnightCharacter.h"
#include "Horse.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

ASmokeCloud::ASmokeCloud()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Puffs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Puffs"));
	Puffs->SetupAttachment(RootComponent);
	Puffs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Puffs->SetCastShadow(false);
	Puffs->bAffectDynamicIndirectLighting = false;
	Puffs->bAffectDistanceFieldLighting = false;
	ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Ball.Succeeded()) Puffs->SetStaticMesh(Ball.Object);
}

void ASmokeCloud::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Smoke.M_Smoke"))) Puffs->SetMaterial(0, Mat);
	else UE_LOG(LogTemp, Warning, TEXT("M_Smoke is missing: run Tools/BuildMaterials.ps1. The poison cloud will be invisible."));

	FRandomStream Rand(777);
	for (int32 i = 0; i < 40; ++i)
	{
		FPuff P;
		const float A = Rand.FRandRange(0.f, 2.f * PI), D = FMath::Sqrt(Rand.FRand()) * Radius * 0.85f;
		P.Base = FVector(FMath::Cos(A) * D, FMath::Sin(A) * D, Rand.FRandRange(30.f, Radius * 0.75f));
		P.Size = Rand.FRandRange(1.5f, 2.9f);
		P.Phase = Rand.FRandRange(0.f, 2.f * PI);
		P.Speed = Rand.FRandRange(0.35f, 0.9f);
		PuffData.Add(P);
		Puffs->AddInstance(FTransform(FRotator::ZeroRotator, P.Base, FVector(P.Size)));
	}
}

void ASmokeCloud::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float T = GetWorld()->GetTimeSeconds();
	for (int32 i = 0; i < PuffData.Num(); ++i)   // each puff drifts round and breathes a little
	{
		const FPuff& P = PuffData[i];
		const FVector Pos = P.Base + FVector(FMath::Sin(T * P.Speed + P.Phase) * 45.f, FMath::Cos(T * P.Speed * 0.8f + P.Phase) * 45.f, FMath::Sin(T * 0.6f + P.Phase) * 25.f);
		const float S = P.Size * (1.f + 0.1f * FMath::Sin(T * 1.2f + P.Phase * 2.f));
		Puffs->UpdateInstanceTransform(i, FTransform(FRotator(0.f, T * 10.f + P.Phase * 20.f, 0.f), Pos, FVector(S)), false, i == PuffData.Num() - 1, false);
	}

	HitTimer += DeltaSeconds;
	if (HitTimer < HitEvery) return;
	HitTimer = 0.f;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	AKnightCharacter* Knight = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
	if (!Knight) return;
	const FVector Here = GetActorLocation();
	auto Inside = [&](const FVector& P) { return FVector::Dist2D(P, Here) < Radius && FMath::Abs(P.Z - Here.Z) < Radius; };
	const bool bKnightIn = Inside(Knight->GetActorLocation());
	if (bKnightIn) Knight->Hurt(KnightHit, this);
	bool bHorseIn = bKnightIn && Knight->IsMounted();   // riding, the horse is in it with you
	for (TActorIterator<AHorse> It(GetWorld()); It && !bHorseIn; ++It) bHorseIn = Inside(It->GetActorLocation());   // or standing in it
	if (bHorseIn) Knight->HurtHorse(HorseHit);
}
