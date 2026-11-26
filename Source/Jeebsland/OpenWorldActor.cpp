#include "OpenWorldActor.h"
#include "Broom.h"
#include "OpenWorld.h"
#include "Terrain.h"
#include "TownLabSwaps.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
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

namespace
{
	TAutoConsoleVariable<float> CVarSnowForce(TEXT("jeeb.Weather.Force"), -1.f, TEXT("The mountain-top weather on this machine: -1 the shared weather (one per game day: a snowstorm or blue skies), 0 always blue skies, 1 always a snowstorm (anything between: that share). Looks only; the storm shows only above the storm height (Tuning.ini [Weather])."));
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
	ABroomStall::PlaceIn(GetWorld());   // (Silver Point's broom stall, 2026-11-22: Broom.cpp)
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
	const JeebOakHill::FModel* Model = JeebOakHill::FindModel(TEXT("OakHillHouse"));
	const TCHAR* MeshPath = nullptr;
	for (const JeebTownLab::FLabSwap& S : JeebTownLab::ModelSwaps) if (FCString::Strcmp(S.Model, TEXT("OakHillHouse")) == 0) MeshPath = S.Mesh;
	if (!Model || !MeshPath) { UE_LOG(LogTemp, Warning, TEXT("JEEBOPEN no house model: the town sites stay empty")); return; }
	const bool bServer = IsNetMode(NM_DedicatedServer);
	const FString Path(MeshPath), Name = FPaths::GetBaseFilename(Path);
	UStaticMesh* Mesh = bServer ? nullptr : LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *Path, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UStaticMesh* Col = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s_Col.%s_Col"), *Path, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UInstancedStaticMeshComponent* Solid = nullptr;
	if (Col)
	{
		Solid = NewObject<UInstancedStaticMeshComponent>(this);
		Solid->SetStaticMesh(Col);
		Solid->SetupAttachment(GetRootComponent());
		Solid->SetMobility(EComponentMobility::Static);
		Solid->SetVisibility(false);
		Solid->SetHiddenInGame(true);
		Solid->SetCastShadow(false);
		Solid->SetCollisionProfileName(TEXT("BlockAll"));
		Solid->SetCanEverAffectNavigation(false);
		Solid->RegisterComponent();
	}
	uint32 Hash = 2166136261u;
	auto HashIn = [&](float V) { const int32 Q = FMath::RoundToInt(V * 10.f); for (int32 b = 0; b < 4; ++b) { Hash ^= uint32((Q >> (8 * b)) & 0xFF); Hash *= 16777619u; } };
	const float ScaleZ = Model->ScaleZ > 0.f ? Model->ScaleZ : Model->Scale;
	int32 Placed = 0;
	for (const JeebOpen::FSite& S : JeebOpen::Sites())
	{
		const float Z = JeebTerrain::HeightAt(S.House.X, S.House.Y);
		const float Yaw = FRotator::NormalizeAxis(S.HouseYaw - Model->DoorYaw);
		const FTransform Xf(FRotator(0.f, Yaw, 0.f), FVector(S.House.X, S.House.Y, Z), FVector(Model->Scale, Model->Scale, ScaleZ));
		if (Mesh)
		{
			UStaticMeshComponent* B = NewObject<UStaticMeshComponent>(this);
			B->SetStaticMesh(Mesh);
			B->SetupAttachment(GetRootComponent());
			B->SetMobility(EComponentMobility::Static);
			B->SetWorldTransform(Xf);
			B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			B->SetCustomPrimitiveDataFloat(0, FMath::Frac(0.37f * (Placed + 1)));
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
		HashIn(S.House.X); HashIn(S.House.Y); HashIn(Z); HashIn(Yaw);
		++Placed;
		UE_LOG(LogTemp, Log, TEXT("JEEBOPEN house for %s at %.0f %.0f %.0f yaw %.0f (%s)"), S.Name, S.House.X, S.House.Y, Z, Yaw, Solid ? TEXT("its collision model") : TEXT("a box over the walls"));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBOPEN houses: %d placeholder houses; hash=%08x%s"), Placed, Hash, bServer ? TEXT(" (server: solids only)") : TEXT(""));
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
	const float Reach = S.Radius * 0.7f + 300.f;   // (over the middle and the green round it; the edge stays open)
	int32 Tries = 0;
	while (Candle.Num() < NumCandles && Tries++ < 2000)
	{
		const float A = R.FRandRange(0.f, 6.2832f), D = Reach * FMath::Sqrt(R.FRand());
		const FVector2D P = S.Pos + FVector2D(FMath::Cos(A), FMath::Sin(A)) * D;
		if (FVector2D::Distance(P, S.House) < 1100.f) continue;   // (the house is about 9 m across; keep clear of it and its eaves)
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
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;   // (this machine's own view: looks only)
	if (!PC || !PC->PlayerCameraManager || !Flakes || Flake.Num() == 0) return;
	Time += DeltaSeconds;
	TickCandles(DeltaSeconds, PC->PlayerCameraManager->GetCameraLocation());
	const AGameStateBase* GS = W->GetGameState();
	const double Clock = GS ? GS->GetServerWorldTimeSeconds() : double(W->GetTimeSeconds());
	const float Force = CVarSnowForce.GetValueOnGameThread();
	SnowNow = Force >= 0.f ? FMath::Min(Force, 1.f) : JeebOpen::Snowing(Clock);
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FVector Fwd = PC->PlayerCameraManager->GetCameraRotation().Vector();
	StormNow = SnowNow * JeebOpen::StormAlt(Cam.Z);   // (the storm where the camera is: ADayNight thickens the fog with it)
	// (2026-11-22, owner: "Why is it snowing in Oak hill? Snow only supposed to fall in snowy areas") the flakes show only where snow lies on
	// the ground round the camera (JeebOpen's snow map, sampled at five spots 25 m apart twice a second), fading in over the snow's edge
	CoverAge += DeltaSeconds;
	if (CoverAge > 0.5f)
	{
		CoverAge = 0.f;
		float Sum = 0.f;
		for (const FVector2D& O : { FVector2D(0.f, 0.f), FVector2D(2500.f, 0.f), FVector2D(-2500.f, 0.f), FVector2D(0.f, 2500.f), FVector2D(0.f, -2500.f) })
			Sum += JeebOpen::SnowOnGround(Cam.X + O.X, Cam.Y + O.Y);
		CoverWant = JeebWorld::Smooth(0.05f, 0.45f, Sum / 5.f);
	}
	CoverNow = FMath::FInterpConstantTo(CoverNow, CoverWant, DeltaSeconds, 0.25f);
	const float Amount = CoverNow * SnowNow * JeebOpen::StormAlt(Cam.Z) * FMath::Clamp(CVarSnowAmount.GetValueOnGameThread(), 0.f, 2.f);
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
