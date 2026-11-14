#include "Pond.h"
#include "Terrain.h"
#include "Inventory.h"
#include "WaterLook.h"
#include "ProceduralMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"

// ======================================================================================= fishing spot
AFishingSpot::AFishingSpot()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Skill = ESkillType::Fishing;
	GatherSeconds = 5.f;
	RespawnSeconds = 1.f;
	ItemType = EItemType::Perch;
	XPReward = 8;
	MaxCharges = 1000000;
	StartText = TEXT("You cast your line...");
	SuccessText = TEXT("You caught a fish!");
	EmptyText = TEXT("The fish are not biting.");

	ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	auto Make = [&](const TCHAR* Name, UStaticMesh* Mesh, const FVector& Scale)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Root);
		C->SetStaticMesh(Mesh);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetVisibility(false);
		return C;
	};
	Bobber = Make(TEXT("Bobber"), Sphere.Object, FVector(0.14f, 0.14f, 0.14f));
	Line = Make(TEXT("Line"), Cyl.Object, FVector(0.008f, 0.008f, 1.f));
	Ripple = Make(TEXT("Ripple"), Cyl.Object, FVector(1.f, 1.f, 0.01f));
	Line->SetUsingAbsoluteLocation(true);
	Line->SetUsingAbsoluteRotation(true);
	Line->SetUsingAbsoluteScale(true);
}

void AFishingSpot::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	auto Tint = [&](UStaticMeshComponent* C, const FLinearColor& Col)
	{
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), Col);
	};
	Tint(Bobber, FLinearColor(0.9f, 0.1f, 0.08f));
	Tint(Line, FLinearColor(0.9f, 0.9f, 0.85f));
	// Two thin rings always spread out from the spot on the water, so you can see where to fish (2026-11-13: were a solid pale
	// disc, which read as a white blob on the new water). Each sinks a little as it grows, so the water dims it as it fades.
	if (!IsNetMode(NM_DedicatedServer))
	{
		const int32 Segs = 48;
		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FLinearColor> Col;
		for (int32 k = 0; k < Segs; ++k)
		{
			const float A = 2.f * PI * k / Segs;
			V.Add(FVector(88.f * FMath::Cos(A), 88.f * FMath::Sin(A), 0.f));
			V.Add(FVector(100.f * FMath::Cos(A), 100.f * FMath::Sin(A), 0.f));
		}
		for (int32 k = 0; k < Segs; ++k)
		{
			const int32 I0 = 2 * k, O0 = 2 * k + 1, I1 = 2 * ((k + 1) % Segs), O1 = I1 + 1;
			T.Append({ I0, O1, O0, I0, I1, O1 });
		}
		N.Init(FVector::UpVector, V.Num()); Col.Init(FLinearColor::White, V.Num());
		for (int32 r = 0; r < 2; ++r)
		{
			UProceduralMeshComponent* Ring = NewObject<UProceduralMeshComponent>(this);
			Ring->SetupAttachment(GetRootComponent());
			Ring->SetUsingAbsoluteScale(true);
			Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ring->SetCastShadow(false);
			Ring->RegisterComponent();
			Ring->CreateMeshSection_LinearColor(0, V, T, N, TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), false);
			if (UMaterialInstanceDynamic* M = Ring->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.20f, 0.25f, 0.28f));
			Rings.Add(Ring);
		}
	}
}

void AFishingSpot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
		Clock += DeltaSeconds;
	const FVector Spot = GetActorLocation();

	// Ripples spread out from the spot all the time, so you can see where to fish
	for (int32 r = 0; r < Rings.Num(); ++r)
	{
		const float R = FMath::Frac(Clock / 2.4f + 0.5f * r);
		const float S = 0.2f + 0.85f * R;   // radius 20 cm to 1.05 m
		Rings[r]->SetWorldScale3D(FVector(S, S, 1.f));
		Rings[r]->SetWorldLocation(Spot + FVector(0.f, 0.f, 0.6f - 9.f * R * R));
	}

	const bool bOn = IsBeingGathered();
	if (Bobber->IsVisible() != bOn)
	{
		Bobber->SetVisibility(bOn);
		Line->SetVisibility(bOn);
	}
	if (!bOn) return;
	AActor* G = GetCurrentGatherer();
	if (!G) return;

	const FVector BobPos = Spot + FVector(0.f, 0.f, 5.f + 3.f * FMath::Sin(Clock * 3.f));
	Bobber->SetWorldLocation(BobPos);


	// The line runs from the tip of the pole to the float
	const FVector Tip = G->GetActorLocation() + G->GetActorForwardVector() * 150.f + G->GetActorRightVector() * 25.f + FVector(0.f, 0.f, 75.f);
	const FVector D = BobPos - Tip;
	const float Len = FMath::Max(D.Size(), 1.f);
	Line->SetWorldLocationAndRotation((Tip + BobPos) * 0.5f, FRotationMatrix::MakeFromZ(D / Len).Rotator());
	Line->SetWorldScale3D(FVector(0.008f, 0.008f, Len / 100.f));
}

void AFishingSpot::RollYield(USkillComponent* Skills, EItemType& OutItem, int32& OutXP)
{
	const int32 L = Skills ? Skills->GetLevel(ESkillType::Fishing) : 1;
	const float CarpChance = (L >= JeebItems::FishingLevelFor(EItemType::GoldenCarp)) ? 0.12f : 0.f;
	const float TroutChance = (L >= JeebItems::FishingLevelFor(EItemType::Trout)) ? 0.35f : 0.f;
	const float Roll = FMath::FRand();
	if (Roll < CarpChance) OutItem = EItemType::GoldenCarp;
	else if (Roll < CarpChance + TroutChance) OutItem = EItemType::Trout;
	else OutItem = EItemType::Perch;
	OutXP = JeebItems::FishXP(OutItem);
	const TCHAR* Name = (OutItem == EItemType::GoldenCarp) ? TEXT("golden carp") : (OutItem == EItemType::Trout ? TEXT("trout") : TEXT("perch"));
	SuccessText = FString::Printf(TEXT("You caught a %s! (+%d XP)"), Name, OutXP);
}

// ======================================================================================= the pond
AFishingPond::AFishingPond()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AFishingPond::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	auto MakeISM = [&](UStaticMesh* Mesh, const FLinearColor& Color, bool bShadow)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(bShadow);
		C->bAffectDynamicIndirectLighting = false;
		C->bAffectDistanceFieldLighting = false;
		C->RegisterComponent();
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), Color);
		return C;
	};
	UInstancedStaticMeshComponent* Plank = MakeISM(Cube, FLinearColor(0.36f, 0.22f, 0.10f), true);
	UInstancedStaticMeshComponent* Post = MakeISM(Cyl, FLinearColor(0.20f, 0.11f, 0.05f), true);
	UInstancedStaticMeshComponent* Stem = MakeISM(Cyl, FLinearColor(0.14f, 0.22f, 0.05f), false);
	UInstancedStaticMeshComponent* Cap = MakeISM(Cyl, FLinearColor(0.24f, 0.12f, 0.05f), false);
	UInstancedStaticMeshComponent* Rock = MakeISM(Sphere, FLinearColor(0.30f, 0.30f, 0.33f), true);
	UInstancedStaticMeshComponent* Pad = MakeISM(Cyl, FLinearColor(0.10f, 0.30f, 0.08f), false);
	UInstancedStaticMeshComponent* Flower = MakeISM(Sphere, FLinearColor(0.9f, 0.4f, 0.6f), false);
	UInstancedStaticMeshComponent* Lamp = MakeISM(Cube, FLinearColor(5.0f, 2.0f, 0.4f), false);

	const FVector2D C = JeebTerrain::PondCenter();
	const float Rim = JeebTerrain::PondRim();
	const float WZ = JeebTerrain::PondWaterZ();
	auto At = [&](float Ang, float D) { return FVector2D(C.X + D * FMath::Cos(Ang), C.Y + D * FMath::Sin(Ang)); };

	// ---- The water (2026-11-13): a flat disc of M_Water (WaterLook.h), drawn on players' machines only, no collision. Its surface
	// is where the old 4 cm cylinder's top was, PondWaterZ + 2 (swimming reads the level from JeebWater.h). Still water: white.
	if (!IsNetMode(NM_DedicatedServer))
	{
		const int32 Segs = 64;
		const float Zs = WZ + 2.f, Rw = JeebTerrain::PondWaterR;
		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FLinearColor> Col;
		V.Add(FVector(C.X, C.Y, Zs));
		for (int32 k = 0; k < Segs; ++k) { const float A = 2.f * PI * k / Segs; V.Add(FVector(C.X + Rw * FMath::Cos(A), C.Y + Rw * FMath::Sin(A), Zs)); }
		for (int32 k = 0; k < Segs; ++k) { const int32 P1 = 1 + k, P2 = 1 + (k + 1) % Segs; T.Append({ 0, P2, P1, 0, P1, P2 }); }   // (both windings)
		N.Init(FVector::UpVector, V.Num()); Col.Init(FLinearColor::White, V.Num());
		UProceduralMeshComponent* Water = NewObject<UProceduralMeshComponent>(this);
		Water->SetupAttachment(RootComponent);
		Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Water->SetCastShadow(false);
		Water->RegisterComponent();
		Water->CreateMeshSection_LinearColor(0, V, T, N, TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), false);
		JeebWaterLook::Apply(Water, 0, FLinearColor(0.06f, 0.24f, 0.36f));
		// The Field map still holds the UE template's grid floor (Floor_0, SM_Template_Map_Floor, MI_ProcGrid) at Z -0.5, hidden
		// under the land everywhere except in this pond's bowl, where it drew as a grey checker over the water. Hide it on screen
		// (its collision is left alone: it is not ours to remove here).
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			const UStaticMeshComponent* SM = It->GetStaticMeshComponent();
			if (SM && SM->GetStaticMesh() && SM->GetStaticMesh()->GetName().Contains(TEXT("Template_Map_Floor"))) It->SetActorHiddenInGame(true);
		}
	}
	// That template floor's collision goes too, on EVERY machine, the server included (2026-11-14, swimming): everywhere else it lies under
	// the land and touches nothing, but in this bowl it was an invisible floor at the waterline that held the knight on top of the water
	// (a trace probe found him standing on Floor_0 at Z 0), so the pond could never be swum in.
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		const UStaticMeshComponent* SM = It->GetStaticMeshComponent();
		if (SM && SM->GetStaticMesh() && SM->GetStaticMesh()->GetName().Contains(TEXT("Template_Map_Floor"))) It->SetActorEnableCollision(false);
	}

	// ---- The pier: from the north bank out over the water ----
	{
		const float D0 = 770.f, D1 = 320.f;
		const float Mid = (D0 + D1) * 0.5f, Len = D0 - D1;
		const float DeckZ = Rim + 2.f;   // the walking surface
		Plank->AddInstance(FTransform(FQuat::Identity, FVector(C.X, C.Y + Mid, DeckZ - 5.f), FVector(1.5f, Len / 100.f, 0.1f)));
		for (float Y = D1 + 20.f; Y < D0; Y += 36.f)   // darker boards laid across, for the look of planks
			Plank->AddInstance(FTransform(FQuat::Identity, FVector(C.X, C.Y + Y, DeckZ - 0.5f), FVector(1.56f, 0.30f, 0.02f)));
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			for (float Y = D1 + 10.f; Y <= D0; Y += 110.f)
			{
				const float Px = C.X + sd * 68.f, Py = C.Y + Y;
				const float G = JeebTerrain::HeightAt(Px, Py);
				const float PL = DeckZ - G;
				Post->AddInstance(FTransform(FQuat::Identity, FVector(Px, Py, G + PL * 0.5f), FVector(0.14f, 0.14f, PL / 100.f)));
			}
			// a taller post with a lantern at the end
			Post->AddInstance(FTransform(FQuat::Identity, FVector(C.X + sd * 68.f, C.Y + D1 + 10.f, DeckZ + 55.f), FVector(0.12f, 0.12f, 1.1f)));
			Lamp->AddInstance(FTransform(FQuat::Identity, FVector(C.X + sd * 68.f, C.Y + D1 + 10.f, DeckZ + 118.f), FVector(0.14f, 0.14f, 0.2f)));
		}
		UBoxComponent* B = NewObject<UBoxComponent>(this);
		B->SetupAttachment(RootComponent);
		B->SetBoxExtent(FVector(75.f, Len * 0.5f, 6.f));
		B->SetRelativeLocation(FVector(C.X, C.Y + Mid, DeckZ - 6.f));
		B->SetCollisionProfileName(TEXT("BlockAll"));
		B->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		B->RegisterComponent();
	}

	// ---- Reeds (cattails) around the edge, leaving the pier clear ----
	FRandomStream R(5151);
	for (int32 i = 0; i < 46; ++i)
	{
		const float Ang = R.FRandRange(0.f, 2.f * PI);
		if (FMath::Abs(FMath::FindDeltaAngleRadians(Ang, 0.5f * PI)) < 0.28f) continue;
		const FVector2D P = At(Ang, R.FRandRange(560.f, 700.f));
		const float G = JeebTerrain::HeightAt(P.X, P.Y);
		for (int32 k = 0; k < 3; ++k)
		{
			const FVector2D Q = P + FVector2D(R.FRandRange(-30.f, 30.f), R.FRandRange(-30.f, 30.f));
			const float Gq = JeebTerrain::HeightAt(Q.X, Q.Y);
			const float H = R.FRandRange(80.f, 150.f);
			Stem->AddInstance(FTransform(FRotator(R.FRandRange(-6.f, 6.f), 0.f, R.FRandRange(-6.f, 6.f)).Quaternion(), FVector(Q.X, Q.Y, Gq + H * 0.5f), FVector(0.04f, 0.04f, H / 100.f)));
			Cap->AddInstance(FTransform(FQuat::Identity, FVector(Q.X, Q.Y, Gq + H - 8.f), FVector(0.07f, 0.07f, 0.3f)));
		}
		(void)G;
	}

	// ---- Rocks along the shore ----
	for (int32 i = 0; i < 22; ++i)
	{
		const float Ang = R.FRandRange(0.f, 2.f * PI);
		if (FMath::Abs(FMath::FindDeltaAngleRadians(Ang, 0.5f * PI)) < 0.25f) continue;
		const FVector2D P = At(Ang, R.FRandRange(620.f, 800.f));
		const float G = JeebTerrain::HeightAt(P.X, P.Y);
		const float S = R.FRandRange(0.4f, 1.1f);
		Rock->AddInstance(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f).Quaternion(), FVector(P.X, P.Y, G + 6.f), FVector(S * 1.2f, S, S * 0.55f)));
	}

	// ---- Lily pads, some with a pink flower ----
	for (int32 i = 0; i < 20; ++i)
	{
		const FVector2D P = At(R.FRandRange(0.f, 2.f * PI), FMath::Sqrt(R.FRand()) * 430.f);
		const float S = R.FRandRange(0.4f, 0.8f);
		Pad->AddInstance(FTransform(FQuat::Identity, FVector(P.X, P.Y, WZ + 1.5f), FVector(S, S, 0.015f)));
		if (i % 3 == 0) Flower->AddInstance(FTransform(FQuat::Identity, FVector(P.X, P.Y, WZ + 7.f), FVector(0.14f, 0.14f, 0.1f)));
	}

	// ---- Places to fish: a ring of spots in the water, and one off the end of the pier ----
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 k = 0; k < 8; ++k)
	{
		const FVector2D P = At(FMath::DegreesToRadians(22.5f + 45.f * k), 450.f);
		World->SpawnActor<AFishingSpot>(FVector(P.X, P.Y, WZ + 2.f), FRotator::ZeroRotator, Params);
	}
	World->SpawnActor<AFishingSpot>(FVector(C.X, C.Y + 180.f, WZ + 2.f), FRotator::ZeroRotator, Params);
}
