#include "Village.h"
#include "Cottage.h"
#include "Terrain.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Npc.h"

AVillage::AVillage()
{
	PrimaryActorTick.bCanEverTick = false;   // (the fountain it animated went with Blackrock, 2026-11-22)
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));

	Log       = MakeISM(TEXT("Log"),       CylMesh.Object,  FLinearColor(0.28f, 0.16f, 0.07f), true);
	Tip       = MakeISM(TEXT("Tip"),       ConeMesh.Object, FLinearColor(0.22f, 0.12f, 0.05f), true);
	Beam      = MakeISM(TEXT("Beam"),      CubeMesh.Object, FLinearColor(0.20f, 0.11f, 0.05f), true);
	Stone     = MakeISM(TEXT("Stone"),     CubeMesh.Object, FLinearColor(0.30f, 0.30f, 0.33f), true);
	StoneRing = MakeISM(TEXT("StoneRing"), CylMesh.Object,  FLinearColor(0.34f, 0.34f, 0.37f), true);
	Roof      = MakeISM(TEXT("Roof"),      ConeMesh.Object, FLinearColor(0.38f, 0.10f, 0.06f), true);
	Water     = MakeISM(TEXT("Water"),     CylMesh.Object,  FLinearColor(0.04f, 0.10f, 0.18f), false);
	Plank     = MakeISM(TEXT("Plank"),     CubeMesh.Object, FLinearColor(0.36f, 0.22f, 0.10f), true);
	Awning    = MakeISM(TEXT("Awning"),    CubeMesh.Object, FLinearColor(0.60f, 0.10f, 0.07f), true);
	Gold      = MakeISM(TEXT("Gold"),      CubeMesh.Object, FLinearColor(0.95f, 0.70f, 0.10f), true);
	Ember     = MakeISM(TEXT("Ember"),     CubeMesh.Object, FLinearColor(5.0f, 1.5f, 0.2f), false);      // bright: glows
	Iron      = MakeISM(TEXT("Iron"),      CubeMesh.Object, FLinearColor(0.10f, 0.10f, 0.12f), true);
}

UInstancedStaticMeshComponent* AVillage::MakeISM(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color, bool bShadow)
{
	UInstancedStaticMeshComponent* C = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	C->SetupAttachment(RootComponent);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision); // solid parts use the invisible boxes below
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(bShadow);
	C->bAffectDynamicIndirectLighting = false;
	C->bAffectDistanceFieldLighting = false;
	Tints.Add(TPair<UInstancedStaticMeshComponent*, FLinearColor>(C, Color));
	return C;
}

void AVillage::AddSolid(const FVector& Center, const FVector& HalfExtent, const FQuat& Rot)
{
	UBoxComponent* B = NewObject<UBoxComponent>(this);
	B->SetupAttachment(RootComponent);
	B->SetBoxExtent(HalfExtent);
	B->SetRelativeLocationAndRotation(Center, Rot);
	B->SetCollisionProfileName(TEXT("BlockAll"));
	B->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	B->RegisterComponent();
}

void AVillage::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	// Logs, stakes, beams and planks use real-looking wood (grain up for the logs, along for the beams), if the materials are built
	UMaterialInterface* WoodV = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_WoodV.M_WoodV"));
	UMaterialInterface* WoodH = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_WoodH.M_WoodH"));
	for (const TPair<UInstancedStaticMeshComponent*, FLinearColor>& T : Tints)
	{
		UMaterialInterface* Mat = Base;
		if (T.Key == Log || T.Key == Tip) { if (WoodV) Mat = WoodV; }
		else if (T.Key == Beam || T.Key == Plank) { if (WoodH) Mat = WoodH; }
		if (UMaterialInstanceDynamic* M = T.Key->CreateDynamicMaterialInstance(0, Mat))
		{
			M->SetVectorParameterValue(TEXT("Color"), T.Value);
		}
	}

	TMap<UInstancedStaticMeshComponent*, TArray<FTransform>> Batch;
	auto Put = [&](UInstancedStaticMeshComponent* C, const FTransform& T) { Batch.FindOrAdd(C).Add(T); };
	auto Ground = [](float X, float Y) { return JeebTerrain::HeightAt(X, Y); };
	FRandomStream R(31337);

	// (2026-11-22: Blackrock, the walled village west of here, was taken out of the game: its palisade, gates, walkway,
	// ladders, towers, fountain, benches, the five huts inside and Sir Aldous are gone. The site stays level for a new town.)

	// (2026-11-22: the market outside the old gate, the store (Brindle), smithy (Hobb) and bank (Pell) with their keepers, was
	// taken out too. Oak Hill and Steele Creek each have all three.)

	for (TPair<UInstancedStaticMeshComponent*, TArray<FTransform>>& Pair : Batch)
	{
		Pair.Key->AddInstances(Pair.Value, false, false, false);
	}

	// ---------------- Huts ----------------
	UWorld* World = GetWorld();
	auto Hut = [&](float X, float Y, float YawDeg, int32 LockLevel = 0, int32 Loot = 0)
	{
		const float S = R.FRandRange(0.92f, 1.06f);
		if (ACottage* H = World->SpawnActor<ACottage>(FVector(X, Y, Ground(X, Y)), FRotator(0.f, YawDeg, 0.f)))
		{
			H->SetActorScale3D(FVector(S));
			if (LockLevel > 0) H->Configure(LockLevel, Loot);
		}
	};


	// Outside, each one facing the nearest road
	// Four of the eight are locked, from easy to hard, each with a stash of ingots inside
	{
		static const int32 LockLv[8]   = { 0, 1, 0, 5, 0, 0, 10, 15 };
		static const int32 LootAmt[8]  = { 0, 60, 0, 150, 0, 0, 350, 600 };
		int32 Idx = 0;
		for (const FVector2D& Site : JeebTerrain::HutSites())
		{
			const FVector2D Near = JeebTerrain::NearestPathPoint(Site.X, Site.Y);
			const float Yaw = FVector(Near.X - Site.X, Near.Y - Site.Y, 0.f).Rotation().Yaw;
			Hut(Site.X, Site.Y, Yaw, LockLv[Idx % 8], LootAmt[Idx % 8]);
			++Idx;
		}
	}

	// ---------------- The villagers ----------------
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// Seven villagers (of the old ten; the three of the old village site's plaza went with it), each keeping to their own corners
		// of the map (the graveyard, the pond, a hut on the hills, the road, the castle forecourt, outside the pig pen)
		auto RingAround = [](const FVector2D& Centre, float Radius, float StartDeg, float Dir)
		{
			TArray<FVector2D> Pts;
			for (int32 k = 0; k < 6; ++k)
			{
				const float A = FMath::DegreesToRadians(StartDeg + Dir * k * 60.f);
				Pts.Add(Centre + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
			}
			return Pts;
		};
		auto RoadWalk = [](float Frac)
		{
			const TArray<FVector2D>& Road = JeebTerrain::PathDefs()[0].Pts;   // the ring road
			const int32 N = Road.Num();
			const int32 I0 = FMath::Clamp(int32(Frac * N), 0, N - 9);
			TArray<FVector2D> Pts;
			Pts.Add(Road[I0]); Pts.Add(Road[I0 + 5]); Pts.Add(Road[I0 + 9]); Pts.Add(Road[I0 + 5]);
			return Pts;
		};
		const FVector2D Pen = JeebTerrain::PenCenter();
		const FVector2D Cas = JeebTerrain::CastlePos();
		for (int32 i = 0; i < 10; ++i)
		{
			TArray<FVector2D> Beat;
			switch (i)
			{
			case 3: Beat = RingAround(JeebTerrain::HutSites()[0], 620.f, 40.f, 1.f); break;                               // Garrick, near the copper hills
			case 4: Beat = RingAround(JeebTerrain::HutSites()[6], 620.f, 200.f, -1.f); break;                              // Wren, at a hut in the north
			case 5: Beat = RingAround(FVector2D(JeebTerrain::GraveyardPos().X, JeebTerrain::GraveyardPos().Y), 520.f, 0.f, 1.f); break;   // Fenwick, among the graves
			case 6: Beat = RingAround(JeebTerrain::PondCenter(), 830.f, 100.f, -1.f); break;                               // Sable, round the pond
			case 9: Beat = { Pen + FVector2D(-500.f, -900.f), Pen + FVector2D(0.f, -960.f), Pen + FVector2D(500.f, -900.f), Pen + FVector2D(0.f, -960.f) }; break;   // Hettie, outside the pen
			case 1: Beat = RoadWalk(0.30f); break;                                                                         // Tobin, on the road
			case 8: Beat = { Cas + FVector2D(-1100.f, -300.f), Cas + FVector2D(-950.f, 0.f), Cas + FVector2D(-1100.f, 300.f), Cas + FVector2D(-950.f, 0.f) }; break;   // Peregrine, at the castle
			default: continue;   // (0, 2 and 7 strolled Blackrock's plaza and huts: gone with the town, 2026-11-22)
			}
			const FVector2D S = Beat[0];
			if (ANpc* N = World->SpawnActor<ANpc>(FVector(S.X, S.Y, Ground(S.X, S.Y) + 92.f), FRotator(0.f, i * 36.f + 90.f, 0.f), Params))
			{
				N->Setup(ENpcRole::Villager, i);
				N->SetPatrol(Beat);
			}
		}
	}
}
