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
	Halo = Make(TEXT("MoonHalo"));
	Dark->SetTranslucentSortPriority(0);   // in front of the stars, behind the lit part
	Halo->SetTranslucentSortPriority(1);
	Lit->SetTranslucentSortPriority(2);

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

	// A soft glow round the moon, so its light spills into the sky instead of ending at a hard circle
	{
		const float H = MoonRadius * 3.2f;
		TArray<FVector> V = { FVector(-20.f, -H, -H), FVector(-20.f, H, -H), FVector(-20.f, H, H), FVector(-20.f, -H, H) };
		TArray<FVector2D> UV = { FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) };
		TArray<FVector> N = { FVector(1, 0, 0), FVector(1, 0, 0), FVector(1, 0, 0), FVector(1, 0, 0) };
		Halo->CreateMeshSection_LinearColor(0, V, { 0, 1, 2, 0, 2, 3 }, N, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		if (UMaterialInterface* HaloMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_MoonHalo.M_MoonHalo")))
		{
			HaloMid = UMaterialInstanceDynamic::Create(HaloMat, this);
			Halo->SetMaterial(0, HaloMid);
		}
	}

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
	// Three columns per row: the terminator fades in over a soft band (vertex alpha), then full light out to the limb
	const int32 Rows = 64;
	const float Soft = 0.12f;
	TArray<FVector> V, N; TArray<int32> I; TArray<FVector2D> UV; TArray<FLinearColor> Col;
	for (int32 j = 0; j <= Rows; ++j)
	{
		const float Y = -1.f + 2.f * j / Rows;
		const float W = FMath::Sqrt(FMath::Max(0.f, 1.f - Y * Y));
		const float Xs[3] = { FMath::Max(C - Soft, -1.f) * W, FMath::Min(C + Soft, 1.f) * W, W };
		for (int32 k = 0; k < 3; ++k)
		{
			V.Add(FVector(40.f, Xs[k] * MoonRadius, Y * MoonRadius));
			N.Add(FVector(1, 0, 0));
			UV.Add(DiscUV(Xs[k] * MoonRadius, Y * MoonRadius, MoonRadius));
			Col.Add(FLinearColor(1.f, 1.f, 1.f, k == 0 ? 0.f : 1.f));
		}
	}
	for (int32 j = 0; j < Rows; ++j)
		for (int32 k = 0; k < 2; ++k)
		{
			const int32 A = j * 3 + k, B = A + 1, Cc = A + 3, D = Cc + 1;
			I.Append({ A, B, D,  A, D, Cc });
		}
	Lit->CreateMeshSection_LinearColor(0, V, I, N, UV, Col, TArray<FProcMeshTangent>(), false);
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

	// Bright and golden when the sun is low; a pale ghost in the noon sky
	const float N = ADayNight::Dusk;
	const float Bright = FMath::Lerp(1.2f, 6.5f, N);
	const FLinearColor Tint = FMath::Lerp(FLinearColor(1.f, 0.97f, 0.92f), MoonGlow, N);
	if (LitMid)
	{
		LitMid->SetScalarParameterValue(TEXT("Bright"), Bright);
		LitMid->SetVectorParameterValue(TEXT("Tint"), Tint);
	}
	// The glow comes from the lit part only: centred on it, as strong as the lit share of the disc, and never over the
	// disc itself (so the shadowed part stays plain sky)
	if (HaloMid)
	{
		const float C = -1.f + 1.9f * Phase;
		const float LitShare = FMath::Clamp((1.f - C) * 0.5f, 0.f, 1.f);
		const float Offset = 0.45f * (1.f + C) * MoonRadius;          // from the disc centre toward the lit limb
		const float H = MoonRadius * 3.2f;                              // half the halo square
		Halo->SetRelativeLocation(FVector(0.f, Offset, 0.f));
		HaloMid->SetScalarParameterValue(TEXT("Bright"), Bright * LitShare);
		HaloMid->SetVectorParameterValue(TEXT("Tint"), Tint);
		HaloMid->SetVectorParameterValue(TEXT("DiscCenter"), FLinearColor(0.5f - Offset / (2.f * H), 0.5f, 0.f));
		HaloMid->SetScalarParameterValue(TEXT("DiscRadius"), MoonRadius / (2.f * H));
	}
	if (DarkMid)
	{
		DarkMid->SetScalarParameterValue(TEXT("Opacity"), 0.f);   // the sky never gets dark enough to need the shadowed disc
		DarkMid->SetScalarParameterValue(TEXT("EarthShine"), 0.f);
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
