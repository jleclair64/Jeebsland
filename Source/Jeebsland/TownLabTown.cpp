#include "TownLabTown.h"
#include "TownLab.h"
#include "OakHill.h"
#include "Terrain.h"
#include "GroundMask.h"
#include "GroundDecor.h"
#include "Forest.h"
#include "ProceduralMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	FString AssetName(const TCHAR* Name) { return FString(Name) + (FCString::Strcmp(Name, TEXT("OakHillWell")) == 0 ? TEXT("_150k") : TEXT("_300k")); }
	// The rebuilt house and store from the townbuild agent, when they exist (/Game/TownLab/Buildings), else the town's own models
	FString LabAsset(const TCHAR* Model)
	{
		if (FCString::Strcmp(Model, TEXT("OakHillHouse")) == 0) return TEXT("/Game/TownLab/Buildings/SM_TL_House");
		if (FCString::Strcmp(Model, TEXT("OakHillStore")) == 0) return TEXT("/Game/TownLab/Buildings/SM_TL_Store");
		return FString();
	}
	UStaticMesh* LoadMesh(const FString& Path)
	{
		const FString Name = FPaths::GetBaseFilename(Path);
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *Path, *Name));
	}
}

ATownLabTown::ATownLabTown()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ATownLabTown::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);
	if (!JeebTownLab::NewOn()) return;
	const double T0 = FPlatformTime::Seconds();
	BuildGround();
	const double T1 = FPlatformTime::Seconds();
	BuildBuildings();
	BuildPlazaOak();
	if (!IsNetMode(NM_DedicatedServer))
	{
		PlaceBoulders(this, JeebTownLab::PlazaX, JeebTownLab::PlazaY);
		TArray<UInstancedStaticMeshComponent*> Before; GetComponents(Before);
		UE_LOG(LogTemp, Log, TEXT("JEEBLAB new town built: ground %.0f ms, the rest %.0f ms, %d drawn sets"), (T1 - T0) * 1000.0, (FPlatformTime::Seconds() - T1) * 1000.0, DrawSets);
	}
}

// The ground: a 1 m grid over the whole patch (the town's streets are 3.5 m wide: a 2 m grid blurred their edges), its four edges
// on the world's 8 m mesh exactly (each edge vertex takes the straight line between that mesh's own points), so no step or gap.
// Vertex colours tell M_TL_Ground what each spot is: R the street (1 down its middle, about 0.4 at its edge, 0 a metre beyond),
// G worn earth (doorways, drip lines, the street's fringe), B how close a wall is (moss and damp at the foot of walls), A unused.
void ATownLabTown::BuildGround()
{
	using namespace JeebTownLab;
	const float Step = 100.f, Coarse = JeebWorld::WorldStep;
	const int32 NI = FMath::RoundToInt((PatchX1 - PatchX0) / Step), NJ = FMath::RoundToInt((PatchY1 - PatchY0) / Step);
	auto CoarseLerp = [&](float X, float Y, bool bAlongY)
	{
		const float V = bAlongY ? Y : X;
		const float Org = bAlongY ? JeebWorld::MinY : JeebWorld::MinX;
		const float V0 = Org + FMath::FloorToFloat((V - Org) / Coarse) * Coarse, T = (V - V0) / Coarse;
		if (T < 1.e-4f) return JeebTerrain::HeightAt(X, Y);
		return bAlongY ? FMath::Lerp(JeebTerrain::HeightAt(X, V0), JeebTerrain::HeightAt(X, V0 + Coarse), T)
		               : FMath::Lerp(JeebTerrain::HeightAt(V0, Y), JeebTerrain::HeightAt(V0 + Coarse, Y), T);
	};
	const bool bServer = IsNetMode(NM_DedicatedServer);
	TArray<FVector> V, N; TArray<FVector2D> UV; TArray<FLinearColor> Col; TArray<int32> Tri, Back;
	const int32 W = NJ + 1;
	V.Reserve((NI + 1) * W); N.Reserve((NI + 1) * W); UV.Reserve((NI + 1) * W); Col.Reserve((NI + 1) * W);
	for (int32 i = 0; i <= NI; ++i)
		for (int32 j = 0; j <= NJ; ++j)
		{
			const float X = PatchX0 + i * Step, Y = PatchY0 + j * Step;
			float Z;
			if (i == 0 || i == NI) Z = CoarseLerp(X, Y, true);
			else if (j == 0 || j == NJ) Z = CoarseLerp(X, Y, false);
			else Z = JeebTerrain::HeightAt(X, Y);
			V.Add(FVector(X, Y, Z));
			const float DX = (JeebTerrain::HeightAt(X + Step, Y) - JeebTerrain::HeightAt(X - Step, Y)) / (2.f * Step);
			const float DY = (JeebTerrain::HeightAt(X, Y + Step) - JeebTerrain::HeightAt(X, Y - Step)) / (2.f * Step);
			N.Add(FVector(-DX, -DY, 1.f).GetSafeNormal());
			UV.Add(FVector2D(X / 1000.f, Y / 1000.f));
			if (bServer) { Col.Add(FLinearColor::Black); continue; }
			const float Clear = RoadClearance(X, Y);
			const float Street = FMath::Clamp((120.f - Clear) / 295.f, 0.f, 1.f);
			const FVector2D O = ToOld(X, Y);
			float Worn = JeebGround::BuildingWear(O.X, O.Y, Clear);
			Worn = FMath::Max(Worn, 0.55f * (1.f - JeebWorld::Smooth(-20.f, 140.f, Clear)));   // (a fringe of packed earth along each street)
			const float Wall = 1.f - JeebWorld::Smooth(0.f, 260.f, BuildingDistance(X, Y));
			Col.Add(FLinearColor(Street, FMath::Clamp(Worn, 0.f, 1.f), Wall, 0.f));
		}
	for (int32 i = 0; i < NI; ++i)
		for (int32 j = 0; j < NJ; ++j)
		{
			const int32 A = i * W + j, B = A + W, C = A + 1, D = B + 1;
			Tri.Append({ A, C, B, B, C, D });
			Back.Append({ A, B, C, B, D, C });
		}
	{
		const FVector Fn = FVector::CrossProduct(V[Tri[1]] - V[Tri[0]], V[Tri[2]] - V[Tri[0]]);
		if (Fn.Z > 0.f) Swap(Tri, Back);   // (wound to face up as Unreal counts it: see AOakHillTown::BuildGround)
	}
	Ground = NewObject<UProceduralMeshComponent>(this, TEXT("TownLabGround"));
	Ground->SetupAttachment(GetRootComponent());
	Ground->bUseAsyncCooking = false;
	Ground->bUseComplexAsSimpleCollision = true;
	Ground->RegisterComponent();
	Ground->CreateMeshSection_LinearColor(0, V, Tri, N, UV, Col, TArray<FProcMeshTangent>(), true);
	Ground->SetCollisionProfileName(TEXT("BlockAll"));
	Ground->SetCastShadow(false);   // (flat ground shades nothing worth a shadow-map draw; it still receives shadows)
	if (bServer) { Ground->SetVisibility(false); return; }
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TownLab/M_TL_Ground.M_TL_Ground"));
	if (!Mat) { UE_LOG(LogTemp, Warning, TEXT("JEEBLAB missing /Game/TownLab/M_TL_Ground (run Tools/BuildTownLab.ps1): M_Ground instead")); Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Ground.M_Ground")); }
	if (Mat) Ground->SetMaterial(0, Mat);
	++DrawSets;
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB new ground %d x %d cells, %d triangles, lift %.0f cm"), NI, NJ, Tri.Num() / 3, Lift());
}

// Every copy of a model is one instance in that model's set: one draw per model (and per material section), however many copies
void ATownLabTown::BuildBuildings()
{
	using namespace JeebOakHill;
	const bool bServer = IsNetMode(NM_DedicatedServer);
	TMap<FString, UInstancedStaticMeshComponent*> Sets, Solids;
	TArray<JeebDecor::FFootprint> Foundations;
	int32 Placed = 0, Rebuilt = 0;
	for (const FPlot& P : Plots())
	{
		if (P.Kind != EPlot::Building || !P.Model) continue;
		const FModel* Mdl = FindModel(P.Model);
		const float Scale = Mdl ? Mdl->Scale : 1.f;
		const FVector2D C = FromPx(P.Px, P.Py) + FVector2D(0.f, JeebTownLab::OffY);
		const float Yaw = BuildingYaw(P, Mdl);
		const FVector At(C.X, C.Y, JeebTerrain::HeightAt(C.X, C.Y));
		const FString Asset = AssetName(P.Model);
		FString Path = FString::Printf(TEXT("/Game/Buildings/SM_%s"), *Asset), ColPath = Path + TEXT("_Col");
		const FString Lab = LabAsset(P.Model);
		if (!Lab.IsEmpty() && LoadMesh(Lab)) { Path = Lab; ColPath = LoadMesh(Lab + TEXT("_Col")) ? Lab + TEXT("_Col") : ColPath; ++Rebuilt; }
		const FTransform Xf(FRotator(0.f, Yaw, 0.f), At, FVector(Scale, Scale, Mdl && Mdl->ScaleZ > 0.f ? Mdl->ScaleZ : Scale));
		if (!bServer)
		{
			UInstancedStaticMeshComponent*& Set = Sets.FindOrAdd(Path);
			if (!Set)
				if (UStaticMesh* Mesh = LoadMesh(Path))
				{
					Set = NewObject<UInstancedStaticMeshComponent>(this);
					Set->SetStaticMesh(Mesh);
					Set->SetupAttachment(GetRootComponent());
					Set->SetMobility(EComponentMobility::Static);
					Set->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Set->RegisterComponent();
					++DrawSets;
				}
			if (Set) Set->AddInstance(Xf, true);
		}
		UInstancedStaticMeshComponent*& Solid = Solids.FindOrAdd(ColPath);
		if (!Solid)
			if (UStaticMesh* ColMesh = LoadMesh(ColPath))
			{
				Solid = NewObject<UInstancedStaticMeshComponent>(this);
				Solid->SetStaticMesh(ColMesh);
				Solid->SetupAttachment(GetRootComponent());
				Solid->SetMobility(EComponentMobility::Static);
				Solid->SetVisibility(false);
				Solid->SetHiddenInGame(true);
				Solid->SetCastShadow(false);
				Solid->SetCollisionProfileName(TEXT("BlockAll"));
				Solid->SetCanEverAffectNavigation(false);
				Solid->RegisterComponent();
			}
		if (Solid) Solid->AddInstance(Xf, true);
		++Placed;
		// (the stones and shrubs at the foundations, as the real town has them: not the open stalls or the well)
		if (Mdl && Mdl->WX1 > Mdl->WX0 && !IsStall(P.Model) && FCString::Strcmp(P.Model, TEXT("OakHillWell")) != 0)
		{
			const FVector WLo = FVector(Mdl->WX0, -Mdl->WY1, 0.f) * Scale, WHi = FVector(Mdl->WX1, -Mdl->WY0, 0.f) * Scale;
			const FVector Mid((WLo.X + WHi.X) * 0.5f, (WLo.Y + WHi.Y) * 0.5f, 0.f);
			const FVector WallMid = At + FRotator(0.f, Yaw, 0.f).RotateVector(Mid);
			JeebDecor::FFootprint Fp;
			Fp.Centre = FVector2D(WallMid.X, WallMid.Y); Fp.Yaw = Yaw; Fp.Half = FVector2D((WHi.X - WLo.X) * 0.5f, (WHi.Y - WLo.Y) * 0.5f); Fp.DoorYaw = Yaw + Mdl->DoorYaw;
			Foundations.Add(Fp);
		}
	}
	if (!bServer)
	{
		TArray<UInstancedStaticMeshComponent*> Before; GetComponents(Before);
		JeebDecor::DressFoundations(this, Foundations, 8128);
		TArray<UInstancedStaticMeshComponent*> After; GetComponents(After);
		DrawSets += After.Num() - Before.Num();
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB new town: %d buildings in %d instanced sets (%d from the rebuilt house/store models)"), Placed, Sets.Num(), Rebuilt);
}

// The plaza's big oak, as the real town's (OakHillTown.cpp): the Meshy oak, its dappled shadow stand-in, the warm light under the crown
void ATownLabTown::BuildPlazaOak()
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Trees/SM_PlazaOak.SM_PlazaOak"));
	const FVector2D C = JeebOakHill::FromPx(255.f, 410.f) + FVector2D(0.f, JeebTownLab::OffY);
	const FVector At(C.X, C.Y, JeebTerrain::HeightAt(C.X, C.Y));
	if (Mesh && !IsNetMode(NM_DedicatedServer))
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
		M->SetStaticMesh(Mesh);
		M->SetupAttachment(GetRootComponent());
		M->SetWorldLocationAndRotation(At, FRotator(0.f, 35.f, 0.f));
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetMobility(EComponentMobility::Static);
		M->SetWorldPositionOffsetDisableDistance(6000);
		if (UStaticMesh* ShadowMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Trees/SM_PlazaOakShadow.SM_PlazaOakShadow")))
		{
			M->SetCastShadow(false);
			UStaticMeshComponent* S = NewObject<UStaticMeshComponent>(this);
			S->SetStaticMesh(ShadowMesh);
			S->SetupAttachment(GetRootComponent());
			S->SetWorldLocationAndRotation(At, FRotator(0.f, 35.f, 0.f));
			S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			S->SetMobility(EComponentMobility::Static);
			S->SetHiddenInGame(true);
			S->SetCastShadow(true);
			S->bCastHiddenShadow = true;
			S->SetWorldPositionOffsetDisableDistance(6000);
			S->RegisterComponent();
		}
		M->RegisterComponent();
		DrawSets += 2;
		USpotLightComponent* L = NewObject<USpotLightComponent>(this);
		L->SetupAttachment(GetRootComponent());
		L->SetWorldLocationAndRotation(At + FVector(0.f, 0.f, 650.f), FRotator(-90.f, 0.f, 0.f));
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensity(12000.f);
		L->SetLightColor(FLinearColor(1.0f, 0.70f, 0.40f));
		L->SetAttenuationRadius(650.f * 1.9f);
		L->SetInnerConeAngle(30.f);
		L->SetOuterConeAngle(54.f);
		L->SetCastShadows(false);
		L->RegisterComponent();
	}
	UCapsuleComponent* Trunk = NewObject<UCapsuleComponent>(this);
	Trunk->SetupAttachment(GetRootComponent());
	Trunk->SetCapsuleSize(110.f, 150.f);
	Trunk->SetWorldLocation(At + FVector(0.f, 0.f, 100.f));
	Trunk->SetCollisionProfileName(TEXT("BlockAll"));
	Trunk->SetMobility(EComponentMobility::Static);
	Trunk->RegisterComponent();
}

// Trees in the gaps between the plots, as the real town's (AOakHillTown::PlantTrees), and a thicker ring of autumn oaks and dark
// pines round the edge (the style sheet: gold and orange crowns against dark pines)
void ATownLabTown::PlantTrees(AForest* Forest)
{
	using namespace JeebOakHill;
	if (!Forest || !JeebTownLab::NewOn()) return;
	FRandomStream R(5150);
	TArray<FVector2D> Got;
	int32 Tries = 0;
	while (Got.Num() < 170 && Tries < 5000)
	{
		++Tries;
		const float E = R.FRandRange(-58.f, 92.f), N = R.FRandRange(-118.f, 124.f);
		const float Roll = R.FRand(), Kind = R.FRand(), S = R.FRandRange(0.85f, 1.25f);
		const float FromPlaza = FVector2D(E, N).Size();
		const float Want = FMath::Lerp(0.12f, 0.75f, JeebWorld::Smooth(32.f, 66.f, FromPlaza));
		if (Roll > Want) continue;
		const float X = JeebTownLab::PlazaX + N * 100.f, Y = JeebTownLab::PlazaY + E * 100.f;
		if (JeebTownLab::RoadClearance(X, Y) < 450.f) continue;
		bool bClear = true;
		for (const FPlot& P : Plots()) if (PlotSD(P, E, N) < (P.Kind == EPlot::Building ? 6.f : 3.f)) { bClear = false; break; }
		if (!bClear) continue;
		for (const FVector2D& G : Got) if (FVector2D::DistSquared(G, FVector2D(X, Y)) < 700.f * 700.f) { bClear = false; break; }
		if (!bClear) continue;
		Got.Add(FVector2D(X, Y));
		const ETreeKind K = (FromPlaza > 50.f && Kind < 0.4f) ? ETreeKind::Pine : ETreeKind::Oak;
		Forest->AddTree(K, FVector(X, Y, JeebTerrain::HeightAt(X, Y) - 20.f * S), S, R);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB new town trees %d"), Got.Num());
}

// Rounded boulders, half sunk, in small groups round the town's edge (the same spots relative to each plaza): squashed spheres in
// the painted stone (M_Stone), one instanced set, casting shadows (they are big)
void ATownLabTown::PlaceBoulders(AActor* Owner, float PlazaX, float PlazaY)
{
	if (!Owner || Owner->IsNetMode(NM_DedicatedServer)) return;
	UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/TownLab/SM_TL_Boulder.SM_TL_Boulder"));
	const bool bMeshy = Ball != nullptr;
	if (!Ball) Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Stone = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Stone.M_Stone"));
	if (!Ball) return;
	UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(Owner);
	I->SetStaticMesh(Ball);
	if (Stone && !bMeshy) I->SetMaterial(0, Stone);
	I->SetupAttachment(Owner->GetRootComponent());
	I->SetMobility(EComponentMobility::Static);
	I->SetCollisionProfileName(TEXT("BlockAll"));
	I->SetCullDistances(0, 30000);
	I->RegisterComponent();
	FRandomStream R(4242);
	int32 Count = 0;
	for (int32 g = 0; g < 14; ++g)
	{
		// (a ring 75-110 m out, away from the roads: the town's own fringe, outside its plots)
		const float A = R.FRandRange(0.f, 2.f * PI), D = R.FRandRange(7500.f, 11000.f);
		const float GX = PlazaX + D * FMath::Cos(A), GY = PlazaY + D * FMath::Sin(A) * 0.75f;
		const int32 N = R.RandRange(1, 4);
		for (int32 k = 0; k < N; ++k)
		{
			const float X = GX + R.FRandRange(-500.f, 500.f), Y = GY + R.FRandRange(-500.f, 500.f);
			const float S = R.FRandRange(0.9f, 2.6f) * (k == 0 ? 1.3f : 0.8f);
			const float Sx = S * R.FRandRange(0.9f, 1.4f), Sy = S * R.FRandRange(0.8f, 1.2f), Sz = S * R.FRandRange(0.55f, 0.8f);
			const FRotator Rot(R.FRandRange(-12.f, 12.f), R.FRandRange(0.f, 360.f), R.FRandRange(-12.f, 12.f));   // (every random number drawn before any skip, so both towns get the same set)
			const float Size = bMeshy ? 100.f : 100.f;   // (the engine sphere is 1 m across; the Meshy boulder is made 1 m across too)
			if (JeebWorld::InWater(X, Y) || JeebTerrain::PathClearance(X, Y) < 300.f || JeebTownLab::RoadClearance(X, Y) < 300.f) continue;
			I->AddInstance(FTransform(Rot,
				FVector(X, Y, JeebTerrain::HeightAt(X, Y) - 0.18f * Sz * Size), FVector(Sx, Sy, Sz)), true);
			++Count;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB %d boulders round the plaza at %.0f %.0f (%s)"), Count, PlazaX, PlazaY, bMeshy ? TEXT("Meshy boulder") : TEXT("rounded engine spheres"));
}
