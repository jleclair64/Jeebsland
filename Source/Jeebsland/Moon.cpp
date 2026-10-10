#include "Moon.h"
#include "DayNight.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

static const FLinearColor MoonGlow(1.0f, 0.80f, 0.45f);   // harvest-moon yellow (the brightness is a separate parameter)

AMoon::AMoon()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the camera moves, so the moon never jitters
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	auto Make = [&](const TCHAR* Name)
	{
		UProceduralMeshComponent* C = CreateDefaultSubobject<UProceduralMeshComponent>(Name);
		C->SetupAttachment(Root);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bAffectDynamicIndirectLighting = false;
		C->bNeverDistanceCull = true;
		return C;
	};
	Dark = Make(TEXT("MoonDark"));
	Lit = Make(TEXT("MoonLit"));
	Dark->SetTranslucentSortPriority(0);   // in front of the stars, behind the lit part
	Lit->SetTranslucentSortPriority(1);

	// Where in the sky: low in the south-west, opposite the setting sun
	SkyDir = ADayNight::MoonDir;
}

// The moon's picture is laid on the disc: (Y, Z) in -R..R maps to UV 0..1, north up
static FVector2D DiscUV(float Y, float Z, float R) { return FVector2D(0.5f + 0.5f * Y / R, 0.5f - 0.5f * Z / R); }

void AMoon::BeginPlay()
{
	Super::BeginPlay();

	// The whole disc (a fan of triangles), facing +X. At night it is a dark, faintly earth-lit shape that hides the stars behind it.
	{
		TArray<FVector> V; TArray<int32> I; TArray<FVector> N; TArray<FVector2D> UV;
		V.Add(FVector(0, 0, 0)); N.Add(FVector(1, 0, 0)); UV.Add(FVector2D(0.5f, 0.5f));
		const int32 Seg = 96;
		for (int32 i = 0; i < Seg; ++i)
		{
			const float A = 2.f * PI * i / Seg;
			const float Y = FMath::Cos(A) * MoonRadius, Z = FMath::Sin(A) * MoonRadius;
			V.Add(FVector(0.f, Y, Z));
			N.Add(FVector(1, 0, 0));
			UV.Add(DiscUV(Y, Z, MoonRadius));
		}
		for (int32 i = 0; i < Seg; ++i)
		{
			const int32 A = 1 + i, B = 1 + (i + 1) % Seg;
			I.Append({ 0, A, B });
		}
		Dark->CreateMeshSection_LinearColor(0, V, I, N, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	}
	BuildLit(0.f);

	UMaterialInterface* LitMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Moon.M_Moon"));
	UMaterialInterface* DarkMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_MoonDark.M_MoonDark"));
	if (LitMat && DarkMat)
	{
		LitMid = UMaterialInstanceDynamic::Create(LitMat, this);
		DarkMid = UMaterialInstanceDynamic::Create(DarkMat, this);
		LitMid->SetVectorParameterValue(TEXT("Tint"), MoonGlow);
		Lit->SetMaterial(0, LitMid);
		Dark->SetMaterial(0, DarkMid);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("M_Moon is missing: run Tools/BuildMaterials.ps1. The moon stays a plain disc."));
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
		if (UMaterialInstanceDynamic* M = Dark->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.07f, 0.06f, 0.04f));
		LitMid = Lit->CreateDynamicMaterialInstance(0, Base);
		if (LitMid) LitMid->SetVectorParameterValue(TEXT("Color"), FLinearColor(7.f, 5.4f, 2.2f));
	}
}

// Phase: 0 = full moon, 1 = a thin fingernail. The lit part lies between the terminator (an ellipse edge) and the right limb.
void AMoon::BuildLit(float Phase)
{
	const float C = -1.f + 1.9f * Phase;   // terminator position: -1 = whole disc lit, +0.9 = a sliver
	const int32 Rows = 64;
	TArray<FVector> V, N; TArray<int32> I; TArray<FVector2D> UV;
	for (int32 j = 0; j <= Rows; ++j)
	{
		const float Y = -1.f + 2.f * j / Rows;
		const float W = FMath::Sqrt(FMath::Max(0.f, 1.f - Y * Y));
		V.Add(FVector(40.f, C * W * MoonRadius, Y * MoonRadius)); N.Add(FVector(1, 0, 0)); UV.Add(DiscUV(C * W * MoonRadius, Y * MoonRadius, MoonRadius));
		V.Add(FVector(40.f, W * MoonRadius, Y * MoonRadius));     N.Add(FVector(1, 0, 0)); UV.Add(DiscUV(W * MoonRadius, Y * MoonRadius, MoonRadius));
	}
	for (int32 j = 0; j < Rows; ++j)
	{
		const int32 A = j * 2, B = j * 2 + 1, Cc = j * 2 + 2, D = j * 2 + 3;
		I.Append({ A, B, D,  A, D, Cc });
	}
	Lit->CreateMeshSection_LinearColor(0, V, I, N, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	if (LitMid) Lit->SetMaterial(0, LitMid);   // a rebuilt section needs its material again
	LastPhase = Phase;
}

void AMoon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;

	// Slow, smooth change: full -> fingernail over 5 minutes, then back again
	const float Phase = 0.5f * (1.f - FMath::Cos(PI * Age / PhaseLegSeconds));
	if (FMath::Abs(Phase - LastPhase) > 0.003f) BuildLit(Phase);

	// Bright and golden at night; a pale ghost in a daytime sky
	const float N = ADayNight::Night;
	if (LitMid)
	{
		LitMid->SetScalarParameterValue(TEXT("Bright"), FMath::Lerp(0.18f, 3.2f, N));
		LitMid->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor(1.f, 0.97f, 0.92f), MoonGlow, N));
	}
	if (DarkMid)
	{
		DarkMid->SetScalarParameterValue(TEXT("Opacity"), N);
		DarkMid->SetScalarParameterValue(TEXT("EarthShine"), 0.05f * N);
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
