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
	// The plane of the Milky Way: a great circle tipped so it arches high across the sky
	const FVector GalaxyNormal = FVector(0.80f, 0.45f, 0.40f).GetSafeNormal();
}

// A flat 100 x 100 card in XY (normal +Z), UVs 0..1 with U running along +X
static UStaticMesh* MakeCard(UObject* Outer, UMaterialInterface* Mat)
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
		UVs[I] = FVector2f((X + 50.f) / 100.f, (Y + 50.f) / 100.f);
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

	auto MakeISM = [&](const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* C = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bAffectDynamicIndirectLighting = false;
		C->bAffectDistanceFieldLighting = false;
		C->SetTranslucentSortPriority(-10);   // behind the moon
		C->bNeverDistanceCull = true;
		return C;
	};
	Stars = MakeISM(TEXT("Stars"));
	Streaks = MakeISM(TEXT("Streaks"));
	CometTail = MakeISM(TEXT("CometTail"));
	CometHead = MakeISM(TEXT("CometHead"));
	MilkyWay = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MilkyWay"));
	MilkyWay->SetupAttachment(RootComponent);
	MilkyWay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MilkyWay->SetCastShadow(false);
	MilkyWay->SetTranslucentSortPriority(-11);
	MilkyWay->bNeverDistanceCull = true;
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
	UMaterialInterface* MilkyMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_MilkyWay.M_MilkyWay"));
	if (!StarMat || !StreakMat || !MilkyMat)
	{
		UE_LOG(LogTemp, Warning, TEXT("Night sky materials missing: run Tools/BuildMaterials.ps1. No stars tonight."));
		SetActorTickEnabled(false);
		return;
	}
	auto Mid = [&](UMaterialInterface* M) { UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(M, this); Mids.Add(D); return D; };

	// ---- Stars: thousands of soft points, thicker toward the Milky Way, a few much brighter than the rest ----
	{
		UMaterialInstanceDynamic* M = Mid(StarMat);
		Stars->SetStaticMesh(MakeCard(this, StarMat));
		Stars->SetMaterial(0, M);
		TArray<FTransform> Xf;
		while (Xf.Num() < 2600)
		{
			FVector D = Rand.GetUnitVector();
			if (D.Z < -0.05f) D.Z = -D.Z;                                   // only the upper half of the sky (and a sliver below)
			const float Band = FMath::Abs(FVector::DotProduct(D, GalaxyNormal));
			if (Rand.FRand() > 0.35f + 0.65f * FMath::Exp(-FMath::Square(Band / 0.22f))) continue;
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

	// ---- The comet: a long soft tail and a bright head ----
	{
		UMaterialInstanceDynamic* T = Mid(StreakMat);
		T->SetScalarParameterValue(TEXT("Intensity"), 0.9f);
		CometTail->SetStaticMesh(MakeCard(this, StreakMat));
		CometTail->SetMaterial(0, T);
		CometTail->AddInstance(FTransform(FQuat::Identity, FVector(0, 0, -Radius), FVector(0.001f)));
		UMaterialInstanceDynamic* H = Mid(StarMat);
		H->SetScalarParameterValue(TEXT("Twinkle"), 0.f);
		H->SetScalarParameterValue(TEXT("Boost"), 14.f);
		CometHead->SetStaticMesh(MakeCard(this, StarMat));
		CometHead->SetMaterial(0, H);
		CometHead->AddInstance(FTransform(FQuat::Identity, FVector(0, 0, -Radius), FVector(0.001f)));
	}

	// ---- The Milky Way: a painted band across the sky ----
	BuildMilkyWay();
	MilkyWay->SetMaterial(0, Mid(MilkyMat));
}

void ANightSky::BuildMilkyWay()
{
	// Two axes in the galactic plane, and the band runs ±16 degrees either side of it
	const FVector U = FVector::CrossProduct(GalaxyNormal, FVector::UpVector).GetSafeNormal();
	const FVector V = FVector::CrossProduct(GalaxyNormal, U).GetSafeNormal();
	const int32 Around = 120, Across = 8;
	const float HalfWidth = FMath::DegreesToRadians(16.f);
	TArray<FVector> Verts, Normals;
	TArray<FVector2D> UV;
	TArray<int32> Tris;
	for (int32 i = 0; i <= Around; ++i)
	{
		const float T = 2.f * PI * i / Around;
		const FVector OnCircle = U * FMath::Cos(T) + V * FMath::Sin(T);
		for (int32 j = 0; j <= Across; ++j)
		{
			const float W = FMath::Lerp(-HalfWidth, HalfWidth, float(j) / Across);
			const FVector D = (OnCircle * FMath::Cos(W) + GalaxyNormal * FMath::Sin(W)).GetSafeNormal();
			Verts.Add(D * (Radius - 200.f));
			Normals.Add(-D);
			UV.Add(FVector2D(float(i) / Around, float(j) / Across));
		}
	}
	for (int32 i = 0; i < Around; ++i)
		for (int32 j = 0; j < Across; ++j)
		{
			const int32 A = i * (Across + 1) + j, B = A + 1, C = A + (Across + 1), D = C + 1;
			Tris.Append({ A, C, B, B, C, D });
		}
	MilkyWay->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
}

void ANightSky::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->PlayerCameraManager) SetActorLocation(PC->PlayerCameraManager->GetCameraLocation());

	const float N = ADayNight::Night;
	for (UMaterialInstanceDynamic* M : Mids) M->SetScalarParameterValue(TEXT("Night"), N);
	const bool bShow = N > 0.01f;
	Stars->SetVisibility(bShow);
	MilkyWay->SetVisibility(bShow);

	// ---- Shooting stars: every few seconds in a dark sky, one streaks down and burns out ----
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

	// ---- A comet on some nights: hangs in the sky all night, drifting slowly, tail pointing away from the sun ----
	if (N > 0.7f && !bNightCounted)
	{
		bNightCounted = true;
		bComet = Rand.FRand() < 0.5f;
		if (bComet)
		{
			CometDir = FRotator(Rand.FRandRange(25.f, 45.f), Rand.FRandRange(0.f, 360.f), 0.f).Vector();
			CometDrift = FVector::CrossProduct(CometDir, FVector::UpVector).GetSafeNormal() * FMath::DegreesToRadians(0.25f);
		}
	}
	if (N < 0.3f) bNightCounted = false;
	CometFade = FMath::FInterpTo(CometFade, (bComet && N > 0.5f) ? 1.f : 0.f, DeltaSeconds, 0.5f);
	if (CometFade > 0.01f)
	{
		CometDir = (CometDir + CometDrift * DeltaSeconds).GetSafeNormal();
		FVector Away = (CometDir - ADayNight::SunDir * FVector::DotProduct(CometDir, ADayNight::SunDir)).GetSafeNormal();
		Away = (Away + FVector::UpVector * 0.6f).GetSafeNormal();                          // tails stream up and away from the sun below
		const FVector TailEnd = (CometDir + Away * FMath::DegreesToRadians(12.f)).GetSafeNormal();
		CometTail->UpdateInstanceTransform(0, StreakTransform(CometDir, TailEnd, Radius * FMath::DegreesToRadians(12.f), 650.f * CometFade), false, true, true);
		CometHead->UpdateInstanceTransform(0, FTransform(FRotationMatrix::MakeFromZ(-CometDir).ToQuat(), CometDir * (Radius - 50.f), FVector(2.6f * CometFade)), false, true, true);
	}
	else if (bComet || CometFade > 0.f)
	{
		CometTail->UpdateInstanceTransform(0, FTransform(FQuat::Identity, FVector(0, 0, -Radius), FVector(0.001f)), false, true, true);
		CometHead->UpdateInstanceTransform(0, FTransform(FQuat::Identity, FVector(0, 0, -Radius), FVector(0.001f)), false, true, true);
		if (!bComet) CometFade = 0.f;
	}
}
