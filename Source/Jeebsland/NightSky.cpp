#include "NightSky.h"
#include "DayNight.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
	const FLinearColor CometBlue(0.45f, 0.70f, 1.0f), CometRed(1.0f, 0.42f, 0.30f);
}

// A flat 100 x 100 card in XY (normal +Z), UVs 0..1 with U running along +X (or a window of the texture)
static UStaticMesh* MakeCard(UObject* Outer, UMaterialInterface* Mat, FVector2D UV0 = FVector2D(0, 0), FVector2D UV1 = FVector2D(1, 1))
{
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Sky"), FName("Sky")));
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Sign = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> GroupSlot = Attr.GetPolygonGroupMaterialSlotNames();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	GroupSlot[Group] = FName("Sky");
	auto Corner = [&](float X, float Y)
	{
		const FVertexID V = MD.CreateVertex();
		Pos[V] = FVector3f(X, Y, 0.f);
		const FVertexInstanceID I = MD.CreateVertexInstance(V);
		Nrm[I] = FVector3f(0.f, 0.f, 1.f);
		Tan[I] = FVector3f(1.f, 0.f, 0.f);
		Sign[I] = 1.f;
		UVs[I] = FVector2f(FMath::Lerp(float(UV0.X), float(UV1.X), (X + 50.f) / 100.f), FMath::Lerp(float(UV0.Y), float(UV1.Y), (Y + 50.f) / 100.f));
		return I;
	};
	const FVertexInstanceID A = Corner(-50.f, -50.f), B = Corner(50.f, -50.f), C = Corner(50.f, 50.f), D = Corner(-50.f, 50.f);
	MD.CreatePolygon(Group, { A, B, C });
	MD.CreatePolygon(Group, { A, C, D });
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	SM->BuildFromMeshDescriptions(Descs, Params);
	return SM;
}

ANightSky::ANightSky()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the camera has moved, so the dome never lags behind it
	Rand.Initialize(8128);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	auto MakeISM = [&](const TCHAR* Name, int32 Sort)
	{
		UInstancedStaticMeshComponent* C = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bAffectDynamicIndirectLighting = false;
		C->bAffectDistanceFieldLighting = false;
		C->SetTranslucentSortPriority(Sort);   // stars behind the moon, the comet in front of the stars
		C->bNeverDistanceCull = true;
		return C;
	};
	Stars = MakeISM(TEXT("Stars"), -10);
	Streaks = MakeISM(TEXT("Streaks"), -9);
	CometTail = MakeISM(TEXT("CometTail"), -8);
	CometHead = MakeISM(TEXT("CometHead"), -7);
	CloudRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CloudRoot"));
	CloudRoot->SetupAttachment(RootComponent);
	for (int32 k = 0; k < 4; ++k)   // one per cloud shape in the picture
	{
		UInstancedStaticMeshComponent* C = MakeISM(*FString::Printf(TEXT("Clouds%d"), k), 5);   // in front of the moon and stars
		C->SetupAttachment(CloudRoot);
		CloudSprites.Add(C);
	}
}

FTransform ANightSky::StreakTransform(const FVector& HeadDir, const FVector& TailDir, float Length, float Width) const
{
	// The card's +X end (bright) sits at the head; it lies on the dome facing the middle
	const FVector Head = HeadDir * Radius, Tail = TailDir * Radius;
	const FVector Along = (Head - Tail).GetSafeNormal();
	const FVector Mid = (Head + Tail) * 0.5f;
	const FQuat Q = FRotationMatrix::MakeFromXZ(Along, -Mid.GetSafeNormal()).ToQuat();
	return FTransform(Q, Mid, FVector(FMath::Max(Length, 1.f) / 100.f, FMath::Max(Width, 0.01f) / 100.f, 1.f));
}

void ANightSky::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* StarMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Star.M_Star"));
	UMaterialInterface* StreakMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Streak.M_Streak"));
	UMaterialInterface* CloudMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Cumulus.M_Cumulus"));
	if (!StarMat || !StreakMat)
	{
		UE_LOG(LogTemp, Warning, TEXT("Sky materials missing: run Tools/BuildMaterials.ps1. No stars or comets."));
		SetActorTickEnabled(false);
		return;
	}
	auto Mid = [&](UMaterialInterface* M) { UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(M, this); StarMids.Add(D); return D; };

	// ---- Stars: soft points scattered over the sky, a few much brighter than the rest ----
	{
		UMaterialInstanceDynamic* M = Mid(StarMat);
		M->SetScalarParameterValue(TEXT("Boost"), 9.f);   // bright enough to show in a dusky, not-quite-dark sky
		Stars->SetStaticMesh(MakeCard(this, StarMat));
		Stars->SetMaterial(0, M);
		TArray<FTransform> Xf;
		while (Xf.Num() < 2200)
		{
			FVector D = Rand.GetUnitVector();
			if (D.Z < -0.05f) D.Z = -D.Z;   // only the upper half of the sky (and a sliver below)
			const float S = Rand.FRandRange(55.f, 95.f) * (Rand.FRand() < 0.04f ? 1.8f : 1.f);
			Xf.Add(FTransform(FRotationMatrix::MakeFromZ(-D).ToQuat(), D * Radius, FVector(S / 100.f)));
		}
		Stars->AddInstances(Xf, false, false, false);
	}

	// ---- Shooting stars (hidden until one flashes across) ----
	{
		UMaterialInstanceDynamic* M = Mid(StreakMat);
		Streaks->SetStaticMesh(MakeCard(this, StreakMat));
		Streaks->SetMaterial(0, M);
		TArray<FTransform> Xf;
		for (int32 i = 0; i < NumShooting; ++i) Xf.Add(FTransform(FQuat::Identity, FVector(0, 0, -Radius), FVector(0.001f)));
		Streaks->AddInstances(Xf, false, false, false);
	}

	// ---- The comet: a fanned tail and a soft round head, tinted blue or red each time one appears ----
	{
		TailMid = Mid(StreakMat);
		TailMid->SetScalarParameterValue(TEXT("Intensity"), 1.6f);
		TailMid->SetScalarParameterValue(TEXT("Fan"), 1.f);
		CometTail->SetStaticMesh(MakeCard(this, StreakMat));
		CometTail->SetMaterial(0, TailMid);
		CometTail->AddInstance(FTransform(FQuat::Identity, FVector(0, 0, -Radius), FVector(0.001f)));
		HeadMid = Mid(StarMat);
		HeadMid->SetScalarParameterValue(TEXT("Twinkle"), 0.f);
		HeadMid->SetScalarParameterValue(TEXT("BrightMin"), 1.f);
		HeadMid->SetScalarParameterValue(TEXT("Boost"), 10.f);
		HeadMid->SetScalarParameterValue(TEXT("Soft"), 1.f);
		CometHead->SetStaticMesh(MakeCard(this, StarMat));
		CometHead->SetMaterial(0, HeadMid);
		CometHead->AddInstance(FTransform(FQuat::Identity, FVector(0, 0, -Radius), FVector(0.001f)));
	}

	// ---- Puffy clouds ----
	if (CloudMat)
	{
		CloudMid = UMaterialInstanceDynamic::Create(CloudMat, this);
		for (int32 k = 0; k < CloudSprites.Num(); ++k)
		{
			const FVector2D UV0(0.5f * (k % 2), 0.5f * (k / 2));
			CloudSprites[k]->SetStaticMesh(MakeCard(this, CloudMat, UV0, UV0 + FVector2D(0.5f, 0.5f)));
			CloudSprites[k]->SetMaterial(0, CloudMid);
		}
		BuildClouds();
	}
}

// Fair-weather clouds: soft puffs scattered round the sky, thicker toward the horizon, flat side down
void ANightSky::BuildClouds()
{
	TArray<TArray<FTransform>> Xf;
	Xf.SetNum(CloudSprites.Num());
	for (int32 i = 0; i < 90; ++i)
	{
		const float El = 3.f + 34.f * FMath::Pow(Rand.FRand(), 1.6f);       // most clouds sit lowish in the sky
		const FVector D = FRotator(El, Rand.FRandRange(0.f, 360.f), 0.f).Vector();
		const FVector Up = (FVector::UpVector - D * FVector::DotProduct(D, FVector::UpVector)).GetSafeNormal();
		const float Width = CloudRadius * FMath::DegreesToRadians(Rand.FRandRange(9.f, 20.f) * FMath::Lerp(0.75f, 1.f, El / 37.f));
		const FQuat Q = FRotationMatrix::MakeFromYZ(-Up, -D).ToQuat();   // texture top toward the sky, facing the middle
		Xf[Rand.RandRange(0, CloudSprites.Num() - 1)].Add(FTransform(Q, D * (CloudRadius - i * 4.f), FVector(Width / 100.f, Width * 0.55f / 100.f, 1.f)));
	}
	for (int32 k = 0; k < CloudSprites.Num(); ++k) CloudSprites[k]->AddInstances(Xf[k], false, false, false);
}
void ANightSky::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->PlayerCameraManager) SetActorLocation(PC->PlayerCameraManager->GetCameraLocation());

	// Stars come out as the sun sinks toward the horizon
	const float N = ADayNight::Dusk;
	for (UMaterialInstanceDynamic* M : StarMids) M->SetScalarParameterValue(TEXT("Night"), N);
	Stars->SetVisibility(N > 0.01f);

	// Clouds: white with soft blue-grey undersides in the high sun, flushed peach and lilac as it sinks, slowly drifting round
	if (CloudMid)
	{
		const float E = ADayNight::SunElevation;
		// golden first, then a deep rose-red as the sun reaches the bottom of its loop
		const float Golden = 1.f - FMath::SmoothStep(22.f, 34.f, E);
		const float Red = 1.f - FMath::SmoothStep(13.f, 24.f, E);   // red for most of the sun's low swing, deepest at the bottom
		const FLinearColor Lit = FMath::Lerp(FMath::Lerp(FLinearColor(1.0f, 1.0f, 1.0f), FLinearColor(1.0f, 0.70f, 0.42f), Golden), FLinearColor(1.0f, 0.36f, 0.24f), Red);
		const FLinearColor Shade = FMath::Lerp(FMath::Lerp(FLinearColor(0.70f, 0.76f, 0.88f), FLinearColor(0.62f, 0.50f, 0.58f), Golden), FLinearColor(0.50f, 0.22f, 0.32f), Red);
		CloudMid->SetVectorParameterValue(TEXT("Lit"), Lit);
		CloudMid->SetVectorParameterValue(TEXT("Shade"), Shade);
		CloudMid->SetScalarParameterValue(TEXT("Bright"), FMath::Lerp(2.1f, 1.8f, Golden));
		CloudRoot->AddLocalRotation(FRotator(0.f, DeltaSeconds * 0.15f, 0.f));
	}

	// ---- Shooting stars: every few seconds while the sun is low, one streaks down and burns out ----
	NextShooting -= DeltaSeconds;
	if (N > 0.6f && NextShooting <= 0.f)
	{
		NextShooting = Rand.FRandRange(3.f, 9.f);
		for (FShooting& S : Shooting)
		{
			if (S.bLive) continue;
			const float Az = Rand.FRandRange(0.f, 360.f), El = Rand.FRandRange(30.f, 70.f);
			S.Start = FRotator(El, Az, 0.f).Vector();
			const FVector Side = FVector::CrossProduct(S.Start, FVector::UpVector).GetSafeNormal() * (Rand.FRand() < 0.5f ? 1.f : -1.f);
			S.Dir = (Side * Rand.FRandRange(0.6f, 1.f) - FVector::UpVector * Rand.FRandRange(0.3f, 0.8f)).GetSafeNormal();
			S.Speed = FMath::DegreesToRadians(Rand.FRandRange(25.f, 45.f));
			S.Life = Rand.FRandRange(0.5f, 1.1f);
			S.Age = 0.f;
			S.bLive = true;
			break;
		}
	}
	for (int32 i = 0; i < NumShooting; ++i)
	{
		FShooting& S = Shooting[i];
		FTransform T(FQuat::Identity, FVector(0, 0, -Radius), FVector(0.001f));
		if (S.bLive)
		{
			S.Age += DeltaSeconds;
			if (S.Age >= S.Life) S.bLive = false;
			else
			{
				const float K = S.Age / S.Life;
				const FVector Head = (S.Start + S.Dir * S.Speed * S.Age).GetSafeNormal();
				const float TailLen = FMath::DegreesToRadians(7.f) * FMath::Sin(PI * K);   // grows, then burns out
				const FVector Tail = (Head - S.Dir * TailLen).GetSafeNormal();
				T = StreakTransform(Head, Tail, Radius * TailLen + 60.f, 70.f * (1.f - K * 0.6f));
			}
		}
		Streaks->UpdateInstanceTransform(i, T, false, i == NumShooting - 1, true);
	}

	// ---- A comet on some evenings: one at a time, blue or red, drifting slowly with its tail away from the sun ----
	if (N > 0.7f && !bDuskCounted)
	{
		bDuskCounted = true;
		bComet = Rand.FRand() < 0.5f;
		if (bComet)
		{
			const FLinearColor Col = Rand.FRand() < 0.5f ? CometBlue : CometRed;
			TailMid->SetVectorParameterValue(TEXT("Tint"), Col);
			HeadMid->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(Col, FLinearColor::White, 0.45f));
			CometDir = FRotator(Rand.FRandRange(25.f, 45.f), Rand.FRandRange(0.f, 360.f), 0.f).Vector();
			CometDrift = FVector::CrossProduct(CometDir, FVector::UpVector).GetSafeNormal() * FMath::DegreesToRadians(0.25f);
		}
	}
	if (N < 0.3f) { bDuskCounted = false; bComet = false; }
	CometFade = FMath::FInterpTo(CometFade, (bComet && N > 0.5f) ? 1.f : 0.f, DeltaSeconds, 0.5f);
	if (CometFade > 0.01f)
	{
		CometDir = (CometDir + CometDrift * DeltaSeconds).GetSafeNormal();
		FVector Away = (CometDir - ADayNight::SunDir * FVector::DotProduct(CometDir, ADayNight::SunDir)).GetSafeNormal();
		Away = (Away + FVector::UpVector * 0.6f).GetSafeNormal();
		const float Len = FMath::DegreesToRadians(12.f);
		const FVector TailEnd = (CometDir + Away * Len).GetSafeNormal();
		// the tail starts a little inside the head, so the round glowing head is the comet's front
		const FVector TailStart = (CometDir + Away * Len * 0.04f).GetSafeNormal();
		CometTail->UpdateInstanceTransform(0, StreakTransform(TailStart, TailEnd, Radius * Len, 900.f * CometFade), false, true, true);
		CometHead->UpdateInstanceTransform(0, FTransform(FRotationMatrix::MakeFromZ(-CometDir).ToQuat(), CometDir * (Radius - 50.f), FVector(5.f * CometFade)), false, true, true);
	}
	else
	{
		CometTail->UpdateInstanceTransform(0, FTransform(FQuat::Identity, FVector(0, 0, -Radius), FVector(0.001f)), false, true, true);
		CometHead->UpdateInstanceTransform(0, FTransform(FQuat::Identity, FVector(0, 0, -Radius), FVector(0.001f)), false, true, true);
	}
}