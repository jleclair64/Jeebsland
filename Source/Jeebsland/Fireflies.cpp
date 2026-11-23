#include "Fireflies.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "GroundMask.h"
#include "DayNight.h"
#include "Fill.h"
#include "OakHill.h"
#include "SteeleCreek.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

// jeeb.Critters: 1 the fireflies and the frogs (Frogs.cpp) are there, 0 neither
TAutoConsoleVariable<int32> CVarCritters(TEXT("jeeb.Critters"), 1, TEXT("1: night fireflies and the frogs on the river banks and lily pads; 0: neither (to compare frame times)"));
static TAutoConsoleVariable<float> CVarFireflyDensity(TEXT("jeeb.Firefly.Density"), 1.f, TEXT("Fireflies against the usual spread (0 none, 1 as placed, 2 twice as many; live, squares are worked out again)"));
static TAutoConsoleVariable<float> CVarFireflySize(TEXT("jeeb.Firefly.Size"), 1.f, TEXT("Firefly glow size scale (live)"));

AFireflies::AFireflies()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Bugs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Bugs"));
	Bugs->SetupAttachment(RootComponent);
	Bugs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bugs->SetCastShadow(false);
	Bugs->bAffectDynamicIndirectLighting = false;
	Bugs->bAffectDistanceFieldLighting = false;
	ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Ball.Succeeded()) Bugs->SetStaticMesh(Ball.Object);
}

void AFireflies::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Flame.M_Flame")))
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Mat, this);
		M->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.75f, 1.0f, 0.25f));   // a yellow-green glow
		M->SetScalarParameterValue(TEXT("Intensity"), 5.f);
		Bugs->SetMaterial(0, M);
	}
	else UE_LOG(LogTemp, Warning, TEXT("M_Flame is missing: run Tools/BuildMaterials.ps1. Fireflies will not glow."));
	for (int32 i = 0; i < MaxBugs; ++i) Bugs->AddInstance(FTransform(FQuat::Identity, FVector(0, 0, -9000.f), FVector(0.001f)));
	Bugs->SetVisibility(false);
}

// Grass or woods only: not snow, sand, rock, mud, water, roads, roofs or the towns
bool AFireflies::GoodGround(float X, float Y, float& OutZ)
{
	if (X < JeebWorld::MinX || X > JeebWorld::MaxX || Y < JeebWorld::MinY || Y > JeebWorld::MaxY) return false;
	if (JeebTerrain::NoGroundCover(X, Y) || JeebWorld::NearRiver(X, Y, 400.f) || Y - JeebWorld::CoastY(X) > -600.f) return false;
	if (JeebTerrain::PathClearance(X, Y) < 250.f || JeebTerrain::BuildingNearness(X, Y) > 0.f) return false;
	if (JeebOakHill::Enabled() && JeebOakHill::InBox(X, Y, 0.f)) return false;   // (the town is lit by its lamps)
	if (JeebSteele::InBox(X, Y, 0.f)) return false;
	const float Z = JeebTerrain::DrawnHeight(X, Y);
	const JeebGround::FMask M = JeebGround::At(X, Y, Z, JeebGround::Upness(X, Y), false);
	if (M.Rock > 0.2f || M.Sand > 0.12f || M.Mud > 0.25f || M.Snow > 0.02f || M.Worn > 0.3f) return false;
	if (JeebFill::Covered(X, Y)) return false;
	OutZ = Z;
	return true;
}

void AFireflies::WorkOutCell(int32 I, int32 J)
{
	const int64 Key = (int64(I) << 32) ^ int64(uint32(J));
	TArray<FBug>& List = Cells.Add(Key);
	FRandomStream R(int32(HashCombine(GetTypeHash(I * 7919 + 13), GetTypeHash(J * 104729 + 5))) ^ 0x1F1E);
	const float Dens = CVarFireflyDensity.GetValueOnGameThread();
	const float X0 = I * CellSize, Y0 = J * CellSize;
	const float Wood = JeebTerrain::WoodsDensity(X0 + CellSize * 0.5f, Y0 + CellSize * 0.5f);
	// open meadow holds about one in two squares, the woods more (up to about three in four, and sometimes two)
	const float Chance = (0.42f + 0.40f * FMath::Clamp((Wood - 0.4f) / 0.4f, 0.f, 1.f)) * Dens;
	const int32 Want = (R.FRand() < Chance ? 1 : 0) + (R.FRand() < Chance * 0.25f ? 1 : 0);
	for (int32 n = 0; n < Want; ++n)
		for (int32 Try = 0; Try < 4; ++Try)
		{
			const float X = X0 + R.FRand() * CellSize, Y = Y0 + R.FRand() * CellSize;
			float Z;
			if (!GoodGround(X, Y, Z)) continue;
			FBug B;
			B.Rise = R.FRandRange(70.f, 240.f);
			B.Home = FVector(X, Y, Z);
			B.Phase = R.FRandRange(0.f, 2.f * PI);
			B.Rate = R.FRandRange(0.18f, 0.42f);
			B.Blink = R.FRandRange(0.45f, 0.9f);
			B.Radius = R.FRandRange(60.f, 170.f);
			List.Add(B);
			break;
		}
}

void AFireflies::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const bool bOn = CVarCritters.GetValueOnGameThread() != 0 && CVarFireflyDensity.GetValueOnGameThread() > 0.f;
	const float Target = bOn ? FMath::Clamp((ADayNight::Night - 0.35f) / 0.4f, 0.f, 1.f) : 0.f;
	Fade = FMath::FInterpTo(Fade, Target, DeltaSeconds, 0.5f);
	if (Fade <= 0.002f)
	{
		if (bShown) { bShown = false; Bugs->SetVisibility(false); }
		return;
	}
	APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!P) return;
	if (!bShown) { bShown = true; Bugs->SetVisibility(true); }
	const FVector Pl = P->GetActorLocation();
	if (LastDensity != CVarFireflyDensity.GetValueOnGameThread()) { LastDensity = CVarFireflyDensity.GetValueOnGameThread(); Cells.Reset(); }

	// the squares round the player (a few new ones a frame at most), and forget the far ones
	const int32 I0 = FMath::FloorToInt((Pl.X - Reach) / CellSize), I1 = FMath::FloorToInt((Pl.X + Reach) / CellSize);
	const int32 J0 = FMath::FloorToInt((Pl.Y - Reach) / CellSize), J1 = FMath::FloorToInt((Pl.Y + Reach) / CellSize);
	int32 Budget = Cells.Num() == 0 ? 400 : 6;
	for (int32 i = I0; i <= I1 && Budget > 0; ++i)
		for (int32 j = J0; j <= J1 && Budget > 0; ++j)
			if (!Cells.Contains((int64(i) << 32) ^ int64(uint32(j)))) { WorkOutCell(i, j); --Budget; }
	CellTimer += DeltaSeconds;
	if (CellTimer > 10.f)
	{
		CellTimer = 0.f;
		for (auto It = Cells.CreateIterator(); It; ++It)
		{
			const int32 I = int32(It.Key() >> 32), J = int32(uint32(It.Key() & 0xffffffff));
			if (FMath::Abs((I + 0.5f) * CellSize - Pl.X) > Reach * 2.f || FMath::Abs((J + 0.5f) * CellSize - Pl.Y) > Reach * 2.f) It.RemoveCurrent();
		}
	}

	// draw the fireflies within reach; the ones near its edge fade out with distance
	const float T = GetWorld()->GetTimeSeconds();
	const float Size0 = 0.05f * CVarFireflySize.GetValueOnGameThread();
	int32 Used = 0;
	for (int32 i = I0; i <= I1; ++i)
		for (int32 j = J0; j <= J1; ++j)
		{
			const TArray<FBug>* L = Cells.Find((int64(i) << 32) ^ int64(uint32(j)));
			if (!L) continue;
			for (const FBug& B : *L)
			{
				if (Used >= MaxBugs) break;
				const float D = FVector2D::Distance(FVector2D(B.Home), FVector2D(Pl));
				if (D > Reach) continue;
				const float Near = 1.f - FMath::Clamp((D - Reach * 0.6f) / (Reach * 0.4f), 0.f, 1.f);
				const float A = T * B.Rate + B.Phase;
				const float Px = B.Home.X + FMath::Cos(A) * B.Radius, Py = B.Home.Y + FMath::Sin(A * 1.3f) * B.Radius;
				const FVector Pos(Px, Py, FMath::Max(JeebTerrain::DrawnHeight(Px, Py), B.Home.Z) + B.Rise + 30.f * FMath::Sin(A * 0.9f + B.Phase));
				const float Pulse = 0.12f + 0.88f * FMath::Pow(0.5f + 0.5f * FMath::Sin(T * B.Blink * 2.2f + B.Phase * 3.f), 4.f);   // slow pulses of light
				const float Size = Size0 * Fade * Near * (0.3f + 0.7f * Pulse);
				Bugs->UpdateInstanceTransform(Used++, FTransform(FQuat::Identity, Pos, FVector(FMath::Max(Size, 0.001f))), true, false, true);
			}
		}
	for (int32 k = Used; k < MaxBugs; ++k) Bugs->UpdateInstanceTransform(k, FTransform(FQuat::Identity, FVector(0, 0, -9000.f), FVector(0.001f)), true, false, true);
	Bugs->MarkRenderStateDirty();
}
