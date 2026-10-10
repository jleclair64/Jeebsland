#include "Moon.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

AMoon::AMoon()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	auto Make = [&](const TCHAR* Name)
	{
		UProceduralMeshComponent* C = CreateDefaultSubobject<UProceduralMeshComponent>(Name);
		C->SetupAttachment(Root);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bAffectDynamicIndirectLighting = false;
		return C;
	};
	Dark = Make(TEXT("MoonDark"));
	Lit = Make(TEXT("MoonLit"));

	// Where in the sky: opposite the low golden sun, about 35 degrees up
	SkyDir = FRotator(35.f, 205.f, 0.f).Vector();
}

void AMoon::BeginPlay()
{
	Super::BeginPlay();

	// The dim whole disc (a fan of triangles), facing +X
	{
		TArray<FVector> V; TArray<int32> I; TArray<FVector> N;
		V.Add(FVector(0, 0, 0)); N.Add(FVector(1, 0, 0));
		const int32 Seg = 64;
		for (int32 i = 0; i < Seg; ++i)
		{
			const float A = 2.f * PI * i / Seg;
			V.Add(FVector(0.f, FMath::Cos(A) * MoonRadius, FMath::Sin(A) * MoonRadius));
			N.Add(FVector(1, 0, 0));
		}
		for (int32 i = 0; i < Seg; ++i)
		{
			const int32 A = 1 + i, B = 1 + (i + 1) % Seg;
			I.Append({ 0, A, B,  0, B, A });
		}
		Dark->CreateMeshSection_LinearColor(0, V, I, N, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	}
	BuildLit(0.f);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	if (UMaterialInstanceDynamic* M = Dark->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.055f, 0.09f));
	if (UMaterialInstanceDynamic* M = Lit->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(7.f, 7.f, 6.4f)); // very bright, so it glows
}

// Phase: 0 = full moon, 1 = a thin fingernail. The lit part lies between the terminator (an ellipse edge) and the right limb.
void AMoon::BuildLit(float Phase)
{
	const float C = -1.f + 1.9f * Phase;   // terminator position: -1 = whole disc lit, +0.9 = a sliver
	const int32 Rows = 48;
	TArray<FVector> V, N; TArray<int32> I;
	for (int32 j = 0; j <= Rows; ++j)
	{
		const float Y = -1.f + 2.f * j / Rows;
		const float W = FMath::Sqrt(FMath::Max(0.f, 1.f - Y * Y));
		V.Add(FVector(40.f, C * W * MoonRadius, Y * MoonRadius)); N.Add(FVector(1, 0, 0));
		V.Add(FVector(40.f, W * MoonRadius, Y * MoonRadius));      N.Add(FVector(1, 0, 0));
	}
	for (int32 j = 0; j < Rows; ++j)
	{
		const int32 A = j * 2, B = j * 2 + 1, Cc = j * 2 + 2, D = j * 2 + 3;
		I.Append({ A, B, D,  A, D, Cc,  A, D, B,  A, Cc, D });
	}
	Lit->CreateMeshSection_LinearColor(0, V, I, N, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	LastPhase = Phase;
}

void AMoon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;

	// Slow, smooth change: full -> fingernail over 5 minutes, then back again
	const float Phase = 0.5f * (1.f - FMath::Cos(PI * Age / PhaseLegSeconds));
	if (FMath::Abs(Phase - LastPhase) > 0.003f)
	{
		BuildLit(Phase);
		// A new mesh section needs its material again
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
		if (UMaterialInstanceDynamic* M = Lit->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(7.f, 7.f, 6.4f));
	}

	// The moon hangs at the same spot in the sky wherever you walk, turned to face you
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
	{
		const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
		const FQuat Face = FRotationMatrix::MakeFromXZ(-SkyDir, FVector::UpVector).ToQuat() * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(25.f));
		SetActorLocationAndRotation(Cam + SkyDir * SkyDistance, Face);
	}
}
