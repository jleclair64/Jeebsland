#include "OpenWorldActor.h"
#include "Caves.h"
#include "Broom.h"
#include "OpenWorld.h"
#include "SilverPoint.h"
#include "SilverClock.h"
#include "Terrain.h"
#include "WorldTerrain.h"
#include "ProceduralMeshComponent.h"
#include "TownLabSwaps.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "DayNight.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"

float AOpenWorld::SnowNow = 0.f;
float AOpenWorld::StormNow = 0.f;
float AOpenWorld::SilverMistNow = 0.f;

namespace
{
	TAutoConsoleVariable<float> CVarSnowForce(TEXT("jeeb.Weather.Force"), -1.f, TEXT("The mountain-top weather on this machine: -1 the shared weather (one per game day: a snowstorm or blue skies), 0 always blue skies, 1 always a snowstorm (anything between: that share). Looks only; the storm shows only above the storm height (Tuning.ini [Weather])."));
	TAutoConsoleVariable<float> CVarSilverFog(TEXT("jeeb.Silver.Fog"), 1.f, TEXT("Silver Point summit fog strength when its fog comes (1 as built, 0 none; live, looks only)"));
	TAutoConsoleVariable<float> CVarSilverForce(TEXT("jeeb.Silver.Weather"), -1.f, TEXT("Test: Silver Point summit weather, -1 the shared schedule, 0 clear, 1 fog, 2 snowfall"));
	TAutoConsoleVariable<float> CVarSnowAmount(TEXT("jeeb.Snow.Amount"), 1.f, TEXT("How many flakes fall when it snows (1 as built: about 3000 round the camera on the peaks; 0 none, 1.4 at most)"));
	TAutoConsoleVariable<float> CVarSnowSize(TEXT("jeeb.Snow.Size"), 1.8f, TEXT("Flake size (1.8 as built: 2.3 to 5 cm)"));

	TAutoConsoleVariable<float> CVarCandles(TEXT("jeeb.Candles"), 1.f, TEXT("The floating candles over Silver Point: 0 hides them, 1 as built (60; a looks-only switch)."));
	TAutoConsoleVariable<float> CVarCandleLight(TEXT("jeeb.Candles.Light"), 1.f, TEXT("The floating candles: strength of the few real lights on the candles nearest you (0 none)."));
	TAutoConsoleVariable<float> CVarCandleGlow(TEXT("jeeb.Candles.Glow"), 1.f, TEXT("The floating candles: brightness of the flames and halos (0 hides them)."));
	TAutoConsoleVariable<float> CVarCandleBy(TEXT("jeeb.Candles.Day"), 0.5f, TEXT("The floating candles: how bright the flames are in full daylight, against 1 at night."));
	constexpr int32 NumCandles = 60;
	constexpr int32 NumCandleLights = 6;
	constexpr float CandleSeeReach = 25000.f;   // (cm from the site's middle past which the candles are put away)

	constexpr int32 NumFlakes = 4400;          // (jeeb.Snow.Amount 1.4 shows them all)
	constexpr float BoxR = 1500.f;             // the flakes' box: 30 m square round a spot 5 m ahead of the camera,
	constexpr float BoxBelow = 500.f, BoxAbove = 1100.f;   // from 5 m under it to 11 m over it
	const FVector Wind(55.f, 24.f, 0.f);       // cm/s: a light steady drift

	// under a town roof (flakes are left out there, so it never snows indoors)
	bool UnderRoof(float X, float Y)
	{
		const FVector2D P(X, Y);
		if (JeebOakHill::Enabled() && JeebOakHill::InBox(X, Y) && JeebOakHill::OnBuilding(X, Y)) return true;
		if (JeebSteele::InBox(X, Y) && JeebSteele::OnBuilding(X, Y)) return true;
		if (JeebSilver::OnBuilding(X, Y)) return true;   // (Silver Point, 2026-11-26)
		if (FMath::Abs(X) < 9000.f && FMath::Abs(Y) < 9000.f)
		{
			for (const FVector2D& S : JeebTerrain::HutSites()) if (FVector2D::DistSquared(P, S) < 480.f * 480.f) return true;
			for (const JeebTerrain::FHutDef& H : JeebTerrain::VillageHuts()) if (FVector2D::DistSquared(P, H.Pos) < 480.f * 480.f) return true;
			if (FVector2D::DistSquared(P, FVector2D(JeebTerrain::CottagePos().X, JeebTerrain::CottagePos().Y)) < 480.f * 480.f) return true;
		}
		return false;
	}
}

AOpenWorld::AOpenWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Plane.Plane"));   // (soft round flakes: camera-facing quads with M_Snowflake, 2026-11-26; they were cubes)
	Flakes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SnowFlakes"));
	Flakes->SetupAttachment(Root);
	Flakes->SetStaticMesh(Cube.Object);
	Flakes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flakes->SetCanEverAffectNavigation(false);
	Flakes->SetCastShadow(false);
	Flakes->bAffectDistanceFieldLighting = false;
	Rand.Initialize(4242);
}

void AOpenWorld::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);
	BuildHouses();
	// (the broom stall on the plaza went 2026-11-27: the wizard's shop is at the broom shop now, JeebBroom::StallSpot)
	BuildSilverGround();
	if (IsNetMode(NM_DedicatedServer)) return;   // (no snow to draw on a server)
	BuildCandles();
	UMaterialInterface* FlakeMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Snowflake.M_Snowflake"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (!FlakeMat) UE_LOG(LogTemp, Warning, TEXT("JEEBSNOW M_Snowflake missing (run Tools/BuildSnowflake.ps1): flakes fall back to plain squares"));
	if (UMaterialInterface* Base = FlakeMat ? FlakeMat : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color")))
		if (UMaterialInstanceDynamic* M = Flakes->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.5f, 1.52f, 1.58f));
	TArray<FTransform> X;
	Flake.SetNum(NumFlakes); NewT.SetNum(NumFlakes); PrevT.SetNum(NumFlakes); Seen.SetNumZeroed(NumFlakes);
	for (int32 i = 0; i < NumFlakes; ++i)
	{
		FFlake& F = Flake[i];
		F.Pos = FVector(Rand.FRandRange(-BoxR, BoxR), Rand.FRandRange(-BoxR, BoxR), Rand.FRandRange(-BoxBelow, BoxAbove));
		F.Fall = Rand.FRandRange(90.f, 170.f);
		F.Phase = Rand.FRandRange(0.f, 6.28f);
		F.Size = Rand.FRandRange(0.013f, 0.028f);
		F.Spin = Rand.FRandRange(0.5f, 1.6f);
		F.bUnderRoof = false;
		X.Add(FTransform(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.001f)));
		NewT[i] = PrevT[i] = X.Last();
	}
	Flakes->AddInstances(X, false, true, false);
	Flakes->SetHasPerInstancePrevTransforms(true);   // motion vectors for TSR: no ghost trails
	SetActorTickEnabled(true);
}

// A placeholder house at each town still to be built: the rebuilt Oak Hill house and its collision model (Steele Creek's houses are the
// same pair), on the site's level ground, its door toward the middle of the site
void AOpenWorld::BuildHouses()
{
	const JeebOakHill::FModel* HouseModel = JeebOakHill::FindModel(TEXT("OakHillHouse"));
	const TCHAR* MeshPath = nullptr;
	for (const JeebTownLab::FLabSwap& S : JeebTownLab::ModelSwaps) if (FCString::Strcmp(S.Model, TEXT("OakHillHouse")) == 0) MeshPath = S.Mesh;
	if (!HouseModel || !MeshPath) { UE_LOG(LogTemp, Warning, TEXT("JEEBOPEN no house model: the town sites stay empty")); return; }
	const bool bServer = IsNetMode(NM_DedicatedServer);
	struct FKit { const JeebOakHill::FModel* Model = nullptr; UStaticMesh* Mesh = nullptr; UInstancedStaticMeshComponent* Solid = nullptr; };
	auto Load = [&](const JeebOakHill::FModel* M, const FString& Path) -> FKit
	{
		FKit K; K.Model = M;
		const FString Name = FPaths::GetBaseFilename(Path);
		K.Mesh = bServer ? nullptr : LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *Path, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
		UStaticMesh* Col = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s_Col.%s_Col"), *Path, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
		if (Col)
		{
			K.Solid = NewObject<UInstancedStaticMeshComponent>(this);
			K.Solid->SetStaticMesh(Col);
			K.Solid->SetupAttachment(GetRootComponent());
			K.Solid->SetMobility(EComponentMobility::Static);
			K.Solid->SetVisibility(false);
			K.Solid->SetHiddenInGame(true);
			K.Solid->SetCastShadow(false);
			K.Solid->SetCollisionProfileName(TEXT("BlockAll"));
			K.Solid->SetCanEverAffectNavigation(false);
			K.Solid->RegisterComponent();
		}
		return K;
	};
	const FKit House = Load(HouseModel, FString(MeshPath));
	uint32 Hash = 2166136261u;
	auto HashIn = [&](float V) { const int32 Q = FMath::RoundToInt(V * 10.f); for (int32 b = 0; b < 4; ++b) { Hash ^= uint32((Q >> (8 * b)) & 0xFF); Hash *= 16777619u; } };
	int32 Placed = 0;
	auto Put = [&](const FKit& K, const FVector2D& At, float Yaw, const TCHAR* What)
	{
		const JeebOakHill::FModel* Model = K.Model;
		UStaticMesh* Mesh = K.Mesh;
		UInstancedStaticMeshComponent* Solid = K.Solid;
		const float ScaleZ = Model->ScaleZ > 0.f ? Model->ScaleZ : Model->Scale;
		const float Z = JeebTerrain::HeightAt(At.X, At.Y);
		const FTransform Xf(FRotator(0.f, Yaw, 0.f), FVector(At.X, At.Y, Z), FVector(Model->Scale, Model->Scale, ScaleZ));
		if (Mesh)
		{
			UStaticMeshComponent* B = NewObject<UStaticMeshComponent>(this);
			B->SetStaticMesh(Mesh);
			B->SetupAttachment(GetRootComponent());
			B->SetMobility(EComponentMobility::Static);
			B->SetWorldTransform(Xf);
			B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			B->SetCustomPrimitiveDataFloat(0, FMath::Frac(0.37f * (Placed + 1)));
			// (2026-11-27: the windows light at dusk, as Oak Hill's: one dynamic copy per building material, its Night set from ADayNight::Dusk in Tick)
			if (!bServer) for (int32 i = 0; i < B->GetNumMaterials(); ++i)
				if (UMaterialInterface* M = B->GetMaterial(i))
				{
					int32 Got = GlowMats.Find(M);
					if (Got == INDEX_NONE) { GlowMats.Add(M); UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(M, this); GlowMids.Add(Mid); GlowBase.Add(Mid->K2_GetScalarParameterValue(TEXT("GlowStrength"))); Got = GlowMids.Num() - 1; }
					B->SetMaterial(i, GlowMids[Got]);
				}
			B->RegisterComponent();
		}
		if (Solid) Solid->AddInstance(Xf, true);
		else
		{
			// (no collision model: a box over the walls. Unreal mirrors Blender's Y: the walls run from (WX0, -WY1) to (WX1, -WY0))
			const FVector2D Lo(Model->WX0 * Model->Scale, -Model->WY1 * Model->Scale), Hi(Model->WX1 * Model->Scale, -Model->WY0 * Model->Scale);
			const FVector2D M = (Lo + Hi) * 0.5f, Half = (Hi - Lo) * 0.5f;
			UBoxComponent* Walls = NewObject<UBoxComponent>(this);
			Walls->SetupAttachment(GetRootComponent());
			Walls->SetBoxExtent(FVector(Half.X, Half.Y, 300.f));
			Walls->SetWorldLocationAndRotation(Xf.TransformPositionNoScale(FVector(M.X, M.Y, 300.f)), FRotator(0.f, Yaw, 0.f));
			Walls->SetCollisionProfileName(TEXT("BlockAll"));
			Walls->SetMobility(EComponentMobility::Static);
			Walls->SetCanEverAffectNavigation(false);
			Walls->RegisterComponent();
		}
		HashIn(At.X); HashIn(At.Y); HashIn(Z); HashIn(Yaw);
		++Placed;
		UE_LOG(LogTemp, Log, TEXT("JEEBOPEN %s at %.0f %.0f %.0f yaw %.0f (%s, %s%s)"), What, At.X, At.Y, Z, Yaw, Model->Name, Solid ? TEXT("its collision model") : TEXT("a box over the walls"), Mesh || bServer ? TEXT("") : TEXT(", NO MESH"));
	};
	// Silver Point (2026-11-26): the wizard village (SilverPoint.h) in place of the placeholder house
	TMap<FString, FKit> SilverKits;
	for (const JeebSilver::FPlot& P : JeebSilver::Plots())
	{
		FKit* K = SilverKits.Find(P.Kind);
		if (!K) K = &SilverKits.Add(P.Kind, Load(JeebOakHill::FindModel(P.Model), FString::Printf(TEXT("/Game/TownLab/Silver/SM_TL_SP%s"), P.Kind)));
		Put(*K, P.Centre, P.Yaw, *FString::Printf(TEXT("Silver Point %s"), P.Id));
		if (!bServer && FCString::Strcmp(P.Kind, TEXT("TownHall")) == 0)   // (its working clock on Central time: SilverClock.h, 2026-11-27)
			ASilverClock::PlaceOn(GetWorld(), FVector(P.Centre.X, P.Centre.Y, JeebTerrain::HeightAt(P.Centre.X, P.Centre.Y)), P.Yaw);
	}
	{
		// (a check: walls of two buildings within 1.5 m of each other, a building on a road, a building off the level ground)
		const TArray<JeebSilver::FPlot>& Ps = JeebSilver::Plots();
		const JeebOpen::FSite* Site = JeebSilver::Site();
		for (int32 i = 0; i < Ps.Num(); ++i)
		{
			const FVector2D Ax = JeebSilver::Dir(Ps[i].Yaw), Ay = JeebSilver::Dir(Ps[i].Yaw + 90.f);
			float Road = 1.0e9f, Far = 0.f;
			for (int32 a = 0; a <= 8; ++a) for (int32 b = 0; b <= 8; ++b)
			{
				const FVector2D L(FMath::Lerp(Ps[i].WallLo.X, Ps[i].WallHi.X, a / 8.f), FMath::Lerp(Ps[i].WallLo.Y, Ps[i].WallHi.Y, b / 8.f));
				const FVector2D W = Ps[i].Centre + Ax * L.X + Ay * L.Y;
				Road = FMath::Min(Road, JeebTerrain::PathClearance(W.X, W.Y));
				if (Site) Far = FMath::Max(Far, float(FVector2D::Distance(W, Site->Pos)));
				for (int32 j = 0; j < Ps.Num(); ++j)
					if (j != i && JeebSilver::WallSD(Ps[j], W.X, W.Y) < 150.f) { UE_LOG(LogTemp, Warning, TEXT("JEEBSILVER %s is within 1.5 m of %s"), Ps[i].Id, Ps[j].Id); a = b = 99; break; }
			}
			UE_LOG(LogTemp, Log, TEXT("JEEBSILVER plot %s (%s) at %.0f %.0f yaw %.0f door %.0f: road %.0f cm off, walls reach %.0f m from the middle"), Ps[i].Id, Ps[i].Kind, Ps[i].Centre.X, Ps[i].Centre.Y, Ps[i].Yaw, Ps[i].DoorYaw, Road, Far / 100.f);
		}
	}
	for (const JeebOpen::FSite& S : JeebOpen::Sites())
	{
		if (FCString::Strcmp(S.Name, TEXT("Silver Point")) == 0 && JeebSilver::Plots().Num() > 0) continue;
		Put(House, S.House, FRotator::NormalizeAxis(S.HouseYaw - HouseModel->DoorYaw), *FString::Printf(TEXT("house for %s"), S.Name));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBOPEN houses: %d buildings (Silver Point's village and the placeholder houses); hash=%08x%s"), Placed, Hash, bServer ? TEXT(" (server: solids only)") : TEXT(""));
}

// The candles: laid out from a fixed seed on the first site (Silver Point), over the open ground round the middle and round the
// placeholder house (never inside it), 2.5 to 6 m up. Every machine gets the same layout; the bobbing is local.
void AOpenWorld::BuildCandles()
{
	const TArray<JeebOpen::FSite>& Sites = JeebOpen::Sites();
	if (Sites.Num() == 0) return;
	const JeebOpen::FSite& S = Sites[0];
	CandleSite = S.Pos;
	FRandomStream R(51515);
	const float Reach = JeebSilver::Plots().Num() > 0 ? JeebSilver::PlazaR - 150.f : S.Radius * 0.7f + 300.f;   // (2026-11-26: over the wizard village's plaza)   // (over the middle and the green round it; the edge stays open)
	int32 Tries = 0;
	while (Candle.Num() < NumCandles && Tries++ < 2000)
	{
		const float A = R.FRandRange(0.f, 6.2832f), D = Reach * FMath::Sqrt(R.FRand());
		const FVector2D P = S.Pos + FVector2D(FMath::Cos(A), FMath::Sin(A)) * D;
		if (FVector2D::Distance(P, S.House) < 1100.f) continue;   // (the house is about 9 m across; keep clear of it and its eaves)
		if (JeebSilver::OnBuilding(P.X, P.Y, 1.5f)) continue;   // (and the village's buildings and eaves, 2026-11-26)
		const float Z = JeebTerrain::HeightAt(P.X, P.Y);
		FCandle C;
		C.Height = R.FRandRange(250.f, 600.f);
		C.Home = FVector(P.X, P.Y, Z + C.Height);
		C.Bob = R.FRandRange(8.f, 22.f); C.BobRate = R.FRandRange(0.9f, 1.8f); C.BobPhase = R.FRandRange(0.f, 6.2832f);
		C.Drift = R.FRandRange(15.f, 45.f); C.DriftRate = R.FRandRange(0.2f, 0.5f); C.DriftPhase = R.FRandRange(0.f, 6.2832f);
		C.Phase = R.FRandRange(0.f, 6.2832f);
		C.Lean = R.FRandRange(-4.f, 4.f);
		Candle.Add(C);
	}
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* ColorMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	UMaterialInterface* HaloMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Flame.M_Flame"));
	auto Make = [this](UStaticMesh* M)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetupAttachment(GetRootComponent());
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetCastShadow(false);
		C->bAffectDynamicIndirectLighting = false;
		C->bAffectDistanceFieldLighting = false;
		C->SetCullDistances(0, 9000);
		C->RegisterComponent();
		C->SetWorldTransform(FTransform::Identity);
		return C;
	};
	if (!Cyl || !Cone || !Ball || Candle.Num() == 0) return;
	CandleBody = Make(Cyl); CandleFlame = Make(Cone); CandleHalo = Make(Ball);
	if (ColorMat)
	{
		if (UMaterialInstanceDynamic* M = CandleBody->CreateDynamicMaterialInstance(0, ColorMat)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.85f, 0.74f, 0.52f));
		if (UMaterialInstanceDynamic* M = CandleFlame->CreateDynamicMaterialInstance(0, ColorMat)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(5.f, 1.9f, 0.3f));
	}
	if (HaloMat) if (UMaterialInstanceDynamic* M = CandleHalo->CreateDynamicMaterialInstance(0, HaloMat)) { M->SetScalarParameterValue(TEXT("Intensity"), 0.3f); M->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.f, 0.5f, 0.14f)); }
	TArray<FTransform> X;
	for (int32 i = 0; i < Candle.Num(); ++i) X.Add(FTransform(FQuat::Identity, Candle[i].Home, FVector(0.001f)));
	UInstancedStaticMeshComponent* Parts[3] = { CandleBody, CandleFlame, CandleHalo };
	for (int32 k = 0; k < 3; ++k)
	{
		Parts[k]->AddInstances(X, false, true, false);
		Parts[k]->SetHasPerInstancePrevTransforms(true);   // motion vectors for TSR: no smears while they bob
		CandleNow[k] = X; CandlePrev[k] = X;
	}
	for (int32 i = 0; i < NumCandleLights; ++i)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(GetRootComponent());
		L->SetMobility(EComponentMobility::Movable);
		L->SetCastShadows(false);
		L->SetLightColor(FLinearColor(1.f, 0.62f, 0.28f));
		L->SetAttenuationRadius(900.f);
		L->SetSourceRadius(2.f);
		L->SetIntensity(0.f);
		L->SetVisibility(false);
		L->RegisterComponent();
		CandleLights.Add(L);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBCANDLES %d floating candles over %s at %.0f %.0f"), Candle.Num(), S.Name, S.Pos.X, S.Pos.Y);
}

void AOpenWorld::TickCandles(float Dt, const FVector& Cam)
{
	if (!CandleBody) return;
	const float On = CVarCandles.GetValueOnGameThread();
	const bool bNear = On > 0.f && FVector2D::Distance(FVector2D(Cam.X, Cam.Y), CandleSite) < CandleSeeReach;
	if (!bNear)
	{
		CandleBody->SetVisibility(false); CandleFlame->SetVisibility(false); CandleHalo->SetVisibility(false);
		for (UPointLightComponent* L : CandleLights) L->SetVisibility(false);
		bCandleSeen = false;
		return;
	}
	CandleBody->SetVisibility(true); CandleFlame->SetVisibility(true); CandleHalo->SetVisibility(true);
	// lit by the night: dim flames by day, full from the dusk on (ADayNight::Night is 0 with the sun up, 1 from ten degrees under)
	const float Dark = JeebWorld::Smooth(0.f, 0.6f, ADayNight::Night);
	const float Lit = FMath::Lerp(FMath::Clamp(CVarCandleBy.GetValueOnGameThread(), 0.f, 1.f), 1.f, Dark) * FMath::Max(0.f, CVarCandleGlow.GetValueOnGameThread());
	const float T = Time;
	TArray<TPair<float, int32>, TInlineAllocator<64>> Near;
	const int32 N = Candle.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const FCandle& C = Candle[i];
		const float Bob = C.Bob * FMath::Sin(T * C.BobRate + C.BobPhase);
		const FVector P = C.Home + FVector(C.Drift * FMath::Sin(T * C.DriftRate + C.DriftPhase), C.Drift * FMath::Cos(T * C.DriftRate * 0.83f + C.DriftPhase * 1.7f), Bob);
		// the flame: a soft quick waver and now and then a little gutter, each its own
		const float Waver = 0.07f * FMath::Sin(T * 8.3f + C.Phase) + 0.05f * FMath::Sin(T * 13.7f + 1.7f * C.Phase) + 0.05f * FMath::PerlinNoise1D(T * 5.f + C.Phase * 9.f);
		const float Gust = FMath::Clamp((FMath::PerlinNoise1D(T * 0.55f + C.Phase * 13.f) - 0.25f) * 4.f, 0.f, 1.f);
		const float B = FMath::Max(0.45f, 0.97f + Waver - 0.3f * Gust);
		const FQuat Q = FRotator(C.Lean * FMath::Sin(T * 0.6f + C.Phase), 0.f, C.Lean * FMath::Cos(T * 0.5f + C.Phase)).Quaternion();
		const float BodyH = 34.f, BodyR = 3.4f;
		// (cylinder: 100 cm tall and across, centred; cone: 100 tall, tip up, centred; sphere 100 across)
		const FVector Top = P + Q.RotateVector(FVector(0.f, 0.f, BodyH * 0.5f));
		const float Show = FMath::Min(Lit, 1.f);
		const float FlameH = 11.f * B * Show, FlameW = 3.4f * Show * (0.9f + 0.1f * B);
		CandleNow[0][i] = FTransform(Q, P, FVector(BodyR * 2.f, BodyR * 2.f, BodyH) * 0.01f);
		CandleNow[1][i] = FTransform(Q, Top + Q.RotateVector(FVector(0.f, 0.f, FlameH * 0.5f + 0.5f)), FVector(FMath::Max(FlameW, 0.001f), FMath::Max(FlameW, 0.001f), FMath::Max(FlameH, 0.001f)) * 0.01f);
		CandleNow[2][i] = FTransform(Q, Top + Q.RotateVector(FVector(0.f, 0.f, 4.f)), FVector(FMath::Max(FMath::Min(40.f * (0.7f + 0.3f * B), 80.f) * Show, 0.01f)) * 0.01f);
		Near.Emplace(float(FVector::DistSquared(Top, Cam)), i);
	}
	if (!bCandleSeen) { for (int32 k = 0; k < 3; ++k) CandlePrev[k] = CandleNow[k]; bCandleSeen = true; }
	UInstancedStaticMeshComponent* Parts[3] = { CandleBody, CandleFlame, CandleHalo };
	for (int32 k = 0; k < 3; ++k)
	{
		Parts[k]->BatchUpdateInstancesTransforms(0, CandleNow[k], CandlePrev[k], true, false, true);
		CandlePrev[k] = CandleNow[k];
	}
	// the real lights: the nearest few candles, faded out with distance so none pops on
	Near.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	const float LightK = FMath::Max(0.f, CVarCandleLight.GetValueOnGameThread());
	for (int32 l = 0; l < CandleLights.Num(); ++l)
	{
		UPointLightComponent* L = CandleLights[l];
		if (!Near.IsValidIndex(l) || LightK <= 0.f) { L->SetVisibility(false); continue; }
		const float Dist = FMath::Sqrt(Near[l].Key);
		const float Fade = 1.f - JeebWorld::Smooth(2500.f, 4500.f, Dist);
		const FCandle& C = Candle[Near[l].Value];
		const float Flick = 0.85f + 0.1f * FMath::Sin(T * 9.f + C.Phase) + 0.07f * FMath::PerlinNoise1D(T * 4.f + C.Phase * 5.f);
		L->SetVisibility(Fade > 0.01f);
		L->SetWorldLocation(CandleNow[1][Near[l].Value].GetLocation());
		L->SetIntensity(120.f * LightK * Lit * Flick * Fade * FMath::Lerp(0.5f, 1.f, Dark));
	}
}

void AOpenWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickRoadWalk(DeltaSeconds);
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;   // (this machine's own view: looks only)
	if (!PC || !PC->PlayerCameraManager || !Flakes || Flake.Num() == 0) return;
	Time += DeltaSeconds;
	TickCandles(DeltaSeconds, PC->PlayerCameraManager->GetCameraLocation());
	if (SilverGroundMid) AWorldTerrain::SetUnderGrass(SilverGroundMid, AWorldTerrain::LowSunFill());   // (the cobbles' dusk fill, as Oak Hill's ground)
	if (FMath::Abs(ADayNight::Dusk - LastDusk) > 0.005f)   // (the village's windows: dark by day, amber at dusk, as ATownLabTown::Tick)
	{
		LastDusk = ADayNight::Dusk;
		for (int32 i = 0; i < GlowMids.Num(); ++i)
		{
			if (GlowBase[i] > 0.f) GlowMids[i]->SetScalarParameterValue(TEXT("GlowStrength"), GlowBase[i] * FMath::Lerp(0.9f, 1.35f, LastDusk));
			GlowMids[i]->SetScalarParameterValue(TEXT("Night"), LastDusk);
		}
	}
	const AGameStateBase* GS = W->GetGameState();
	const double Clock = GS ? GS->GetServerWorldTimeSeconds() : double(W->GetTimeSeconds());
	const float Force = CVarSnowForce.GetValueOnGameThread();
	SnowNow = Force >= 0.f ? FMath::Min(Force, 1.f) : JeebOpen::Snowing(Clock);
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FVector Fwd = PC->PlayerCameraManager->GetCameraRotation().Vector();
	StormNow = SnowNow * JeebOpen::StormAlt(Cam.Z) * (1.f - ACaves::CameraInside);   // (none inside a cave)   // (the storm where the camera is: ADayNight thickens the fog with it)
	// (2026-11-22, owner: "Why is it snowing in Oak hill? Snow only supposed to fall in snowy areas") the flakes show only where snow lies on
	// the ground round the camera (JeebOpen's snow map, sampled at five spots 25 m apart twice a second), fading in over the snow's edge
	CoverAge += DeltaSeconds;
	if (CoverAge > 0.5f)
	{
		CoverAge = 0.f;
		float Sum = 0.f;
		for (const FVector2D& O : { FVector2D(0.f, 0.f), FVector2D(2500.f, 0.f), FVector2D(-2500.f, 0.f), FVector2D(0.f, 2500.f), FVector2D(0.f, -2500.f) })
			Sum += JeebOpen::SnowOnGround(Cam.X + O.X, Cam.Y + O.Y);
		CoverWant = JeebWorld::Smooth(0.05f, 0.45f, Sum / 5.f) * (1.f - ACaves::CameraInside);   // (no snow falls in a cave, Caves.h)
	}
	CoverNow = FMath::FInterpConstantTo(CoverNow, CoverWant, DeltaSeconds, ACaves::CameraInside > 0.05f ? 3.f : 0.25f);   // (2026-11-26: gone at once on stepping into a cave; flakes hung in the Storm Peak passage)
	TickSilverWeather(DeltaSeconds, Clock, Cam);
	const float Amount = FMath::Max(CoverNow * SnowNow * JeebOpen::StormAlt(Cam.Z), SilverSnowNow) * FMath::Clamp(CVarSnowAmount.GetValueOnGameThread(), 0.f, 2.f);
	const int32 Want = FMath::Clamp(FMath::RoundToInt(NumFlakes * Amount), 0, NumFlakes);
	if (Want == 0 && Shown == 0) return;
	const float SizeK = FMath::Clamp(CVarSnowSize.GetValueOnGameThread(), 0.2f, 4.f);
	const FVector C = Cam + FVector(Fwd.X, Fwd.Y, 0.f).GetSafeNormal() * 500.f;
	const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
	const FQuat Face = FRotationMatrix::MakeFromZX(-CamRot.Vector(), FRotationMatrix(CamRot).GetScaledAxis(EAxis::Y)).ToQuat();
	const int32 Upto = FMath::Max(Want, Shown);
	for (int32 i = 0; i < Upto; ++i)
	{
		FFlake& F = Flake[i];
		if (i >= Want)   // (the snow is easing off: this one is put away)
		{
			NewT[i] = FTransform(FQuat::Identity, FVector(C.X, C.Y, -100000.f), FVector(0.001f));
			PrevT[i] = NewT[i]; Seen[i] = 0;
			continue;
		}
		bool bJumped = !Seen[i];
		F.Pos += (Wind + FVector(32.f * FMath::Sin(Time * 0.8f + F.Phase), 32.f * FMath::Cos(Time * 0.65f + F.Phase * 1.7f), -F.Fall)) * DeltaSeconds;
		// kept inside the box round the camera: a flake that leaves one side comes back in on the other
		FVector Rel = F.Pos - C;
		if (FMath::Abs(Rel.X) > BoxR) { Rel.X = FMath::Fmod(FMath::Fmod(Rel.X + BoxR, 2.f * BoxR) + 2.f * BoxR, 2.f * BoxR) - BoxR; bJumped = true; }
		if (FMath::Abs(Rel.Y) > BoxR) { Rel.Y = FMath::Fmod(FMath::Fmod(Rel.Y + BoxR, 2.f * BoxR) + 2.f * BoxR, 2.f * BoxR) - BoxR; bJumped = true; }
		if (Rel.Z < -BoxBelow || Rel.Z > BoxAbove) { const float Hgt = BoxBelow + BoxAbove; Rel.Z = FMath::Fmod(FMath::Fmod(Rel.Z + BoxBelow, Hgt) + Hgt, Hgt) - BoxBelow; bJumped = true; }
		if (bJumped) { F.Pos = C + Rel; F.bUnderRoof = UnderRoof(F.Pos.X, F.Pos.Y); }
		// (never in the camera's face: a flake within a couple of metres of it shrinks away, or it reads as a white block)
		const float S = F.bUnderRoof ? 0.001f : F.Size * SizeK * FMath::Max(0.03f, JeebWorld::Smooth(120.f, 320.f, float(FVector::Dist(F.Pos, Cam))));
		const FTransform Now(Face, F.Pos, FVector(S * 1.25f));   // (a round flake faces the camera; its spin would not show)
		PrevT[i] = bJumped ? Now : NewT[i];
		NewT[i] = Now;
		Seen[i] = 1;
	}
	if (Upto > 0)
	{
		const TArray<FTransform> N(NewT.GetData(), Upto), P(PrevT.GetData(), Upto);
		Flakes->BatchUpdateInstancesTransforms(0, N, P, true, false, true);
	}
	Shown = Want;
}

// (2026-11-27, owner: "some fog near the top of the mountain that comes and goes, and snow that comes and goes but not at the same time as
// the fog") Silver Point's summit weather from JeebOpen::SilverWeather (the shared clock, so every player sees the same): a local fog
// volume round the top, and the flakes (Tick) falling round the camera while it is up there. The day's storm, when it reaches the top,
// keeps the fog away too. Looks only, this machine's own view.
void AOpenWorld::TickSilverWeather(float Dt, double Clock, const FVector& Cam)
{
	float Fog = 0.f, Snow = 0.f;
	JeebOpen::SilverWeather(Clock, Fog, Snow);
	if (const float Fw = CVarSilverForce.GetValueOnGameThread(); Fw >= 0.f) { Fog = FMath::IsNearlyEqual(Fw, 1.f) ? 1.f : 0.f; Snow = FMath::IsNearlyEqual(Fw, 2.f) ? 1.f : 0.f; }
	const FVector2D Mid = JeebOpen::SilverMid();
	const float D = float(FVector2D::Distance(FVector2D(Cam.X, Cam.Y), Mid));
	Fog *= 1.f - FMath::Clamp(SnowNow * JeebOpen::StormAlt(JeebOpen::SilverTop), 0.f, 1.f);
	// the snowfall: only with the camera on the top or its upper slopes
	const float Near = JeebWorld::Smooth(17000.f, 10000.f, D) * JeebWorld::Smooth(JeebOpen::SilverTop - 4500.f, JeebOpen::SilverTop - 1500.f, Cam.Z) * (1.f - ACaves::CameraInside);
	SilverSnowNow = FMath::FInterpConstantTo(SilverSnowNow, Snow * Near * 0.85f, Dt, 0.3f);
	if (Mid.X > 1.0e8f) return;
	// (round 2) the mist banks, and the height fog thickened while the camera is in the fog on the top or its upper slopes
	TickSilverMist(Fog * FMath::Clamp(CVarSilverFog.GetValueOnGameThread(), 0.f, 2.f), Cam);
	SilverMistNow = Fog * JeebWorld::Smooth(20000.f, 12000.f, D) * JeebWorld::Smooth(JeebOpen::SilverTop - 6000.f, JeebOpen::SilverTop - 2000.f, Cam.Z) * (1.f - ACaves::CameraInside) * FMath::Clamp(CVarSilverFog.GetValueOnGameThread(), 0.f, 1.5f);
	if (!SilverFog)
	{
		SilverFog = NewObject<ULocalFogVolumeComponent>(this, TEXT("SilverFog"));
		SilverFog->SetupAttachment(GetRootComponent());
		SilverFog->RegisterComponent();
		SilverFog->SetWorldLocation(FVector(Mid.X, Mid.Y, JeebOpen::SilverTop + 1500.f));
		SilverFog->SetWorldScale3D(FVector(26.f, 26.f, 12.f));   // (a unit sphere of 5 m: about 130 m across the top, 60 m high)
		SilverFog->SetHeightFogExtinction(0.f);
		SilverFog->SetFogAlbedo(FLinearColor(0.86f, 0.88f, 0.92f));
		SilverFog->SetFogPhaseG(0.2f);
		SilverFog->SetRadialFogExtinction(0.f);
		SilverFog->SetVisibility(false);
	}
	const float Want = Fog * FMath::Clamp(CVarSilverFog.GetValueOnGameThread(), 0.f, 4.f);
	if (FMath::Abs(Want - SilverFogNow) > 0.002f)
	{
		SilverFogNow = Want;
		SilverFog->SetRadialFogExtinction(1.6f * Want);
		SilverFog->SetVisibility(Want > 0.002f);
	}
}


// (round 2, 2026-11-28) the summit fog's banks of mist: 40 soft cloud cards from a fixed seed (the same on every machine; the fog's
// coming and going is the shared schedule), lying round the upper slopes from just under the top to 25 m over it and a few drifting over
// the village. Each turns to face the camera every frame and drifts slowly round the peak; a card the camera comes close to shrinks away
// so none is seen edge on or from inside. Lit like LOW's cloud ring (ADayNight::TrailLight: white at noon, warm and dim at the low sun).
// One mesh, one draw; it is rebuilt only while the fog shows. Looks only.
void AOpenWorld::TickSilverMist(float Fog, const FVector& Cam)
{
	const FVector2D Mid = JeebOpen::SilverMid();
	if (!SilverMist)
	{
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Cumulus.M_Cumulus"));
		if (!Mat) { UE_LOG(LogTemp, Warning, TEXT("JEEBMOUNT M_Cumulus is missing: no summit mist")); MistShown = 0.f; return; }
		SilverMist = NewObject<UProceduralMeshComponent>(this, TEXT("SilverMist"));
		SilverMist->SetupAttachment(GetRootComponent());
		SilverMist->SetUsingAbsoluteLocation(true);
		SilverMist->SetUsingAbsoluteRotation(true);
		SilverMist->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SilverMist->SetCastShadow(false);
		SilverMist->bAffectDistanceFieldLighting = false;
		SilverMist->SetVisibleInRayTracing(false);
		SilverMist->RegisterComponent();
		SilverMist->SetWorldLocation(FVector::ZeroVector);
		SilverMistMid = UMaterialInstanceDynamic::Create(Mat, this);
		SilverMist->SetMaterial(0, SilverMistMid);
		FRandomStream R(6161);
		for (int32 i = 0; i < 40; ++i)
		{
			FMistCard M;
			M.Phi = R.FRandRange(0.f, 2.f * PI);
			const bool bOver = i < 6;   // (a few over the village itself)
			M.R = bOver ? R.FRandRange(1000.f, 6500.f) : R.FRandRange(8000.f, 19000.f);
			const FVector2D P = Mid + FVector2D(FMath::Cos(M.Phi), FMath::Sin(M.Phi)) * M.R;
			M.Z = FMath::Max(JeebTerrain::HeightAt(P.X, P.Y), JeebOpen::SilverTop - 4500.f) + R.FRandRange(bOver ? 600.f : -300.f, bOver ? 2500.f : 1500.f);
			M.Size = bOver ? R.FRandRange(2500.f, 4000.f) : R.FRandRange(5000.f, 9500.f);   // (round 2: big and soft, a cloud cap round the top, not stickers)
			M.Speed = R.FRandRange(0.004f, 0.011f) * (R.FRand() < 0.5f ? -1.f : 1.f);
			M.Phase = R.FRandRange(0.f, 2.f * PI);
			M.Tile = R.RandRange(0, 3);
			M.bFlip = R.FRand() < 0.5f;
			MistCards.Add(M);
		}
		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> UV; TArray<FLinearColor> C; TArray<FProcMeshTangent> Tg;
		for (int32 i = 0; i < MistCards.Num(); ++i)
		{
			const FMistCard& M = MistCards[i];
			const float U0 = (M.Tile % 2) * 0.5f, V0 = (M.Tile / 2) * 0.5f, Ul = M.bFlip ? U0 + 0.5f : U0, Ur = M.bFlip ? U0 : U0 + 0.5f;
			for (int32 k = 0; k < 4; ++k) { V.Add(FVector::ZeroVector); N.Add(FVector::UpVector); C.Add(FLinearColor::White); }
			UV.Append({ FVector2D(Ul, V0 + 0.5f), FVector2D(Ur, V0 + 0.5f), FVector2D(Ur, V0), FVector2D(Ul, V0) });
			const int32 F = i * 4;
			T.Append({ F, F + 2, F + 1, F, F + 3, F + 2 });
		}
		SilverMist->CreateMeshSection_LinearColor(0, V, T, N, UV, C, Tg, false);
		SilverMist->SetVisibility(false);
		MistShown = 0.f;
	}
	const float Show = Fog > 0.01f && FVector2D::Distance(FVector2D(Cam.X, Cam.Y), Mid) < 260000.f ? Fog : 0.f;
	if (Show <= 0.f) { if (MistShown > 0.f) { SilverMist->SetVisibility(false); MistShown = 0.f; } return; }
	if (MistShown <= 0.f) SilverMist->SetVisibility(true);
	MistShown = Show;
	const float Tm = Time;
	TArray<FVector> V; V.Reserve(MistCards.Num() * 4);
	TArray<FVector> N; N.Reserve(MistCards.Num() * 4);
	for (const FMistCard& M : MistCards)
	{
		const float Phi = M.Phi + M.Speed * Tm * 0.25f + 0.05f * FMath::Sin(Tm * 0.02f + M.Phase);
		const FVector Ctr(Mid.X + FMath::Cos(Phi) * M.R, Mid.Y + FMath::Sin(Phi) * M.R, M.Z + 250.f * FMath::Sin(Tm * 0.031f + M.Phase));
		FVector To = Cam - Ctr; const float Dist = To.Size(); To /= FMath::Max(Dist, 1.f);
		FVector Side = FVector::CrossProduct(FVector::UpVector, To).GetSafeNormal();
		if (Side.IsNearlyZero()) Side = FVector::ForwardVector;
		const FVector Upv = FVector::CrossProduct(To, Side);
		const float S = M.Size * JeebWorld::Smooth(M.Size * 0.5f, M.Size * 1.4f, Dist) * (1.f + 0.06f * FMath::Sin(Tm * 0.05f + M.Phase * 3.f));
		const FVector Base = Ctr - Upv * S * 0.79f;   // (the heap's flat base, as LOW's ring)
		V.Append({ Base - Side * S * 0.5f, Base + Side * S * 0.5f, Base + Side * S * 0.5f + Upv * S, Base - Side * S * 0.5f + Upv * S });
		for (int32 k = 0; k < 4; ++k) N.Add(To);
	}
	SilverMist->UpdateMeshSection(0, V, N, TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>());
	const FLinearColor L = ADayNight::TrailLight;
	SilverMistMid->SetVectorParameterValue(TEXT("Lit"), FMath::Lerp(L, FLinearColor(0.95f, 0.96f, 1.f), 0.35f));
	SilverMistMid->SetVectorParameterValue(TEXT("Shade"), FMath::Lerp(L, FLinearColor(0.62f, 0.68f, 0.8f), 0.55f));
	SilverMistMid->SetScalarParameterValue(TEXT("Bright"), 1.5f * FMath::Lerp(1.f, 0.2f, ADayNight::Night));
	SilverMistMid->SetScalarParameterValue(TEXT("Opacity"), 0.6f * FMath::Clamp(Show, 0.f, 1.f));
}
// -SilverRoadWalk (test builds, single player): the knight is stood at the mountain road's foot and runs it to the village, steering a
// few metres ahead along its line; -SilverRoadWalkShare=0.4 walks. Logs JEEBROADWALK once a second (along, height, speed) and quits at
// the top or after 12 minutes.
void AOpenWorld::TickRoadWalk(float Dt)
{
#if !UE_BUILD_SHIPPING
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("SilverRoadWalk"));
	if (!bOn) return;
	static float Clock = 0.f, Acc = 0.f, Share = -1.f, Along = 0.f, MaxV = 0.f, MinRun = 1.0e9f;
	static int32 I = 0, Stuck = 0;
	static FVector Last = FVector::ZeroVector;
	if (Share < 0.f) { Share = 1.f; FParse::Value(FCommandLine::Get(), TEXT("SilverRoadWalkShare="), Share); }
	Clock += Dt;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ACharacter* P = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	const TArray<FVector2D>& L = JeebOpen::SilverRoadLine();
	if (!P || L.Num() < 20) return;
	if (Clock < 4.f) return;
	const float Half = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (I == 0)
	{
		I = 1;
		const FVector2D S = L[0], Dir = (L[10] - L[0]).GetSafeNormal();
		P->SetActorLocationAndRotation(FVector(S.X, S.Y, JeebTerrain::HeightAt(S.X, S.Y) + Half + 5.f), FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		Last = P->GetActorLocation();
		UE_LOG(LogTemp, Display, TEXT("JEEBROADWALK start at %.0f %.0f, %d points, share %.2f"), S.X, S.Y, L.Num(), Share);
		return;
	}
	const FVector Loc = P->GetActorLocation();
	const FVector2D Me(Loc.X, Loc.Y);
	for (int32 k = I; k < FMath::Min(I + 60, L.Num()); ++k) if (FVector2D::DistSquared(L[k], Me) < FVector2D::DistSquared(L[I], Me)) I = k;
	const int32 T = FMath::Min(I + 8, L.Num() - 1);
	const FVector2D To = (L[T] - Me).GetSafeNormal();
	P->AddMovementInput(FVector(To.X, To.Y, 0.f), Share);
	PC->SetControlRotation(FRotator(-10.f, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)), 0.f));
	Acc += Dt;
	if (Acc < 1.f) return;
	const float Moved = FVector::Dist2D(Loc, Last) / Acc;
	Along += FVector::Dist2D(Loc, Last);
	Last = Loc; Acc = 0.f;
	const FVector V = P->GetVelocity();
	MaxV = FMath::Max(MaxV, V.Z); if (Clock > 8.f) MinRun = FMath::Min(MinRun, Moved);
	Stuck = Moved < 100.f ? Stuck + 1 : 0;
	const float Off = FVector2D::Distance(Me, L[I]);
	const float Top = FVector2D::Distance(Me, JeebOpen::SilverMid());
	UE_LOG(LogTemp, Display, TEXT("JEEBROADWALK t %.0f point %d/%d along %.0f m feet %.1f m ground %.1f m speed %.0f cm/s vz %.0f off the line %.0f cm, %.0f m from the middle"),
		Clock, I, L.Num(), Along / 100.f, (Loc.Z - Half) / 100.f, JeebTerrain::HeightAt(Loc.X, Loc.Y) / 100.f, Moved, V.Z, Off, Top / 100.f);
	if (Top < 4000.f || Clock > 720.f || Stuck > 8)
	{
		UE_LOG(LogTemp, Display, TEXT("JEEBROADWALK %s after %.0f s, %.0f m; slowest second %.0f cm/s"), Top < 4000.f ? TEXT("PASS reached the village") : (Stuck > 8 ? TEXT("FAIL stuck") : TEXT("FAIL out of time")), Clock, Along / 100.f, MinRun);
		PC->ConsoleCommand(TEXT("quit"));
	}
#endif
}

#if !UE_BUILD_SHIPPING
// Test only (single player, or a listen server's own knight): jeeb.Test.GoTown N puts your knight at a town site (1 Silver Point,
// 2 Anchorage, 3 Birch Grove, 4 Palmer Square: the order of JeebOpen::Sites), 12 m short of its middle so the name shows on walking in;
// jeeb.Test.GoPeak puts him on a snowy shoulder of the northern range, looking south over the map
namespace
{
	void GoTo(UWorld* W, const FVector2D& At, float Yaw)
	{
		if (!W) return;
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
			if (APlayerController* PC = It->Get())
				if (APawn* P = PC->GetPawn())
					if (P->HasAuthority())
					{
						P->TeleportTo(FVector(At.X, At.Y, JeebTerrain::HeightAt(At.X, At.Y) + 130.f), FRotator(0.f, Yaw, 0.f));
						PC->SetControlRotation(FRotator(-8.f, Yaw, 0.f));
					}
	}
	FAutoConsoleCommandWithWorldAndArgs GoTown(TEXT("jeeb.Test.GoTown"), TEXT("Test: jeeb.Test.GoTown N puts your knight at a town site still to be built (1 Silver Point, 2 Anchorage, 3 Birch Grove, 4 Palmer Square; single player)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TArray<JeebOpen::FSite>& S = JeebOpen::Sites();
			const int32 N = Args.Num() ? FCString::Atoi(*Args[0]) - 1 : 0;
			if (!S.IsValidIndex(N)) return;
			const FVector2D Out = (S[N].Pos - S[N].House).GetSafeNormal();   // (away from the house, on the far side of the middle)
			const FVector2D At = S[N].Pos + Out * (S[N].Radius + 1200.f);
			UE_LOG(LogTemp, Display, TEXT("JEEBGOTOWN %d: site middle %.0f %.0f, knight at %.0f %.0f"), N + 1, S[N].Pos.X, S[N].Pos.Y, At.X, At.Y);   // (for -ShotEyes coordinates)
			GoTo(W, At, FMath::RadiansToDegrees(FMath::Atan2(-Out.Y, -Out.X)));
		}));
	FAutoConsoleCommandWithWorld GoPeak(TEXT("jeeb.Test.GoPeak"), TEXT("Test: puts your knight high on the northern range, looking south over the map (single player)"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
		{
			// the highest ground on a coarse sweep of the range
			FVector2D Best(88000.f, 20000.f); float Top = -1.0e9f;
			for (float X = 62000.f; X < JeebWorld::MaxX - 6000.f; X += 1600.f)
				for (float Y = JeebWorld::MinY + 8000.f; Y < 84000.f; Y += 1600.f) { const float Z = JeebTerrain::HeightAt(X, Y); if (Z > Top) { Top = Z; Best = FVector2D(X, Y); } }
			GoTo(W, Best, 180.f);
		}));
}
#endif

// Silver Point's cobbles (2026-11-27, agent silverpt; owner: "cobbled plaza and streets ... no visible tiling"): one 1 m grid over the
// village (64 m round the middle) drawn with Oak Hill's own ground material, M_TL_Ground, which paints mossy flagstone cobbles, worn
// earth, moss at the walls' feet and grass from the vertex colours (R the street, G worn earth, B a wall's foot; ATownLabTown::BuildGround):
// two samplings of the cobbles at unrelated sizes swapped in 10 m patches, fading to their average far off, so no repeat lines up.
// The grid sits 8 cm over the world's own ground (above the dirt road's ribbon, which it covers in the village) and has no collision:
// players and people walk on the world's ground as before. Grass, flowers and snow keep off the cobbles (JeebSilverOnBuilding).
// Clients only; the same on every machine (fixed numbers).
void AOpenWorld::BuildSilverGround()
{
	const JeebOpen::FSite* S = JeebSilver::Site();
	if (!S || JeebSilver::Plots().Num() == 0 || IsNetMode(NM_DedicatedServer)) return;
	const float Step = 100.f, Half = 6400.f, Lift = 8.f;
	const int32 N = FMath::RoundToInt(2.f * Half / Step);
	const FVector2D O = S->Pos - FVector2D(Half, Half);
	TArray<FVector> V, Nrm; TArray<FVector2D> UV; TArray<FLinearColor> Col; TArray<int32> Tri;
	const int32 W = N + 1;
	V.Reserve(W * W); Nrm.Reserve(W * W); UV.Reserve(W * W); Col.Reserve(W * W);
	TArray<FVector2D> Doors;
	for (const JeebSilver::FPlot& P : JeebSilver::Plots()) Doors.Add(JeebSilver::DoorSpot(P, 60.f));
	for (int32 i = 0; i <= N; ++i)
		for (int32 j = 0; j <= N; ++j)
		{
			const float X = O.X + i * Step, Y = O.Y + j * Step;
			const float Z = JeebTerrain::DrawnHeight(X, Y);
			V.Add(FVector(X, Y, Z + Lift));
			const float DX = (JeebTerrain::DrawnHeight(X + 50.f, Y) - JeebTerrain::DrawnHeight(X - 50.f, Y)) / 100.f;
			const float DY = (JeebTerrain::DrawnHeight(X, Y + 50.f) - JeebTerrain::DrawnHeight(X, Y - 50.f)) / 100.f;
			Nrm.Add(FVector(-DX, -DY, 1.f).GetSafeNormal());
			UV.Add(FVector2D(X / 1000.f, Y / 1000.f));
			const float Clear = JeebSilver::StreetClearance(X, Y);
			const float Street = FMath::Clamp((120.f - Clear) / 295.f, 0.f, 1.f);   // (as Oak Hill's: 1 down the middle, ~0.4 at the edge)
			float Worn = 0.55f * (1.f - JeebWorld::Smooth(-20.f, 140.f, Clear));    // (a fringe of packed earth along the cobbles)
			for (const FVector2D& D : Doors) Worn = FMath::Max(Worn, 1.f - JeebWorld::Smooth(60.f, 220.f, float(FVector2D::Distance(D, FVector2D(X, Y)))));
			Worn = FMath::Max(Worn, 1.f - JeebWorld::Smooth(-60.f, 120.f, JeebTerrain::PathClearance(X, Y)));   // (the dirt road, where it runs on past the cobbles)
			float WallD = 1.0e9f;
			for (const JeebSilver::FPlot& P : JeebSilver::Plots())
				if (FVector2D::DistSquared(P.Centre, FVector2D(X, Y)) < FMath::Square(P.Reach + 400.f)) WallD = FMath::Min(WallD, JeebSilver::WallSD(P, X, Y));
			const float Wall = 1.f - JeebWorld::Smooth(0.f, 260.f, FMath::Max(WallD, 0.f));
			// (2026-11-27, the mountain; owner: "the summit to be mostly grey cobblestone and snow", "snow between the cobbles") grey stones
			// (CobbleGrey) paved over most of the top between the lanes too, with a few ragged gaps, easing into the snow at the rim; the
			// alpha is the snow: whole off the stones, in the joints and patches over the stones on the lanes, thinner where feet wear it
			const FVector2D Pq(X, Y);
			const float Dm = float(FVector2D::Distance(Pq, S->Pos));
			const float Fill = JeebWorld::Smooth(-0.55f, -0.2f, FMath::PerlinNoise2D(Pq / 1700.f + FVector2D(3.3f, 1.2f)) + 0.4f * FMath::PerlinNoise2D(Pq / 600.f + FVector2D(-2.1f, 5.7f))) * JeebWorld::Smooth(6300.f, 5400.f, Dm);
			const float Lane = FMath::Clamp(Street * 2.f, 0.f, 1.f);
			const float Paved = FMath::Max(Street, 0.9f * Fill);
			float Snow = FMath::Min(FMath::Lerp(1.f, 0.6f, Fill), FMath::Lerp(1.f, 0.42f, Lane)) - 0.15f * Worn + 0.12f * FMath::PerlinNoise2D(Pq / 350.f + FVector2D(8.8f, -4.4f));
			Snow = FMath::Clamp(Snow, 0.05f, 1.f);
			Col.Add(FLinearColor(Paved, FMath::Clamp(Worn, 0.f, 1.f), Wall, Snow));
		}
	int32 Cells = 0;
	for (int32 i = 0; i < N; ++i)
		for (int32 j = 0; j < N; ++j)
		{
			const float CX = O.X + (i + 0.5f) * Step, CY = O.Y + (j + 0.5f) * Step;
			if (FVector2D::DistSquared(S->Pos, FVector2D(CX, CY)) > FMath::Square(Half)) continue;   // (a disc: its rim is grass on grass)
			{
				// (round 2, 2026-11-28: the disc's square-stepped rim showed its own snow against the world's) only cells that hold stones, wear
				// or a wall's fringe are drawn; elsewhere the world's own snow shows, so the edge follows the stones' ragged outline
				float Mx = 0.f;
				for (const int32 K : { i * W + j, (i + 1) * W + j, i * W + j + 1, (i + 1) * W + j + 1 }) Mx = FMath::Max(Mx, FMath::Max3(Col[K].R * 1.f, Col[K].G * 0.5f, Col[K].B * 0.8f));
				if (Mx < 0.04f) continue;
			}
			bool bInside = true;   // (a cell wholly inside a building's walls is never seen)
			for (const FVector2D& Q : { FVector2D(CX - 50.f, CY - 50.f), FVector2D(CX + 50.f, CY + 50.f), FVector2D(CX - 50.f, CY + 50.f), FVector2D(CX + 50.f, CY - 50.f) })
				if (!JeebSilver::OnBuilding(Q.X, Q.Y, -0.6f)) { bInside = false; break; }
			if (bInside) continue;
			const int32 A = i * W + j, B = A + W, C = A + 1, D = B + 1;
			Tri.Append({ A, C, B, B, C, D });
			++Cells;
		}
	if (Tri.Num() >= 3)   // (face up as Unreal counts it, as ATownLabTown::BuildGround)
	{
		const FVector Fn = FVector::CrossProduct(V[Tri[1]] - V[Tri[0]], V[Tri[2]] - V[Tri[0]]);
		if (Fn.Z > 0.f) for (int32 t = 0; t < Tri.Num(); t += 3) Swap(Tri[t + 1], Tri[t + 2]);
	}
	SilverGround = NewObject<UProceduralMeshComponent>(this, TEXT("SilverGround"));
	SilverGround->SetupAttachment(GetRootComponent());
	SilverGround->RegisterComponent();
	SilverGround->CreateMeshSection_LinearColor(0, V, Tri, Nrm, UV, Col, TArray<FProcMeshTangent>(), false);
	SilverGround->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SilverGround->SetCastShadow(false);
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TownLab/M_TL_Ground.M_TL_Ground")))
	{
		SilverGroundMid = UMaterialInstanceDynamic::Create(Mat, this);
		SilverGround->SetMaterial(0, SilverGroundMid);
		SilverGroundMid->SetScalarParameterValue(TEXT("CobbleGrey"), 1.f);   // (2026-11-27: grey stones on the summit, Tools/townlab_build.py)
	}
	else UE_LOG(LogTemp, Warning, TEXT("JEEBSILVER missing /Game/TownLab/M_TL_Ground: no cobbles"));
	UE_LOG(LogTemp, Log, TEXT("JEEBSILVER cobbles: %d cells, %d triangles"), Cells, Tri.Num() / 3);
}
