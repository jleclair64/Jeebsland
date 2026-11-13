#include "OakHillTown.h"
#include "OakHill.h"
#include "Terrain.h"
#include "GroundMask.h"
#include "GroundDecor.h"
#include "Forest.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// The asset for each model: the reduced file's name (Art/Buildings/<Name>_300k.glb, the well _150k)
	FString AssetName(const TCHAR* Name) { return FString(Name) + (FCString::Strcmp(Name, TEXT("OakHillWell")) == 0 ? TEXT("_150k") : TEXT("_300k")); }

	void HashIn(uint32& H, float V)
	{
		const int32 Q = FMath::RoundToInt(V * 10.f);   // millimetres
		for (int32 b = 0; b < 4; ++b) { H ^= uint32((Q >> (8 * b)) & 0xFF); H *= 16777619u; }
	}
}

AOakHillTown::AOakHillTown()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AOakHillTown::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);
	if (!JeebOakHill::Enabled()) return;
	const double T0 = FPlatformTime::Seconds();
	BuildGround();
	const double T1 = FPlatformTime::Seconds();
	BuildBuildings();
	if (!JeebOakHill::TestOff(TEXT("NoOakMeshes"))) JeebDecor::DressFoundations(this, Foundations, 8128);   // (stones and shrubs at the walls: visual only)
	UE_LOG(LogTemp, Log, TEXT("JEEBOAK built ground %.0f ms, buildings %.0f ms, hash=%08x"), (T1 - T0) * 1000.0, (FPlatformTime::Seconds() - T1) * 1000.0, Hash);
	LogChecks();
}

// The ground east of the original square, on the square's own 2 m grid (so the two meet vertex for vertex at Y 16000) and with
// its other three edges following the world's 8 m mesh exactly (each edge vertex takes the straight line between the coarse
// grid's points, as the coarse triangles do), so there is no step or gap anywhere along the seam.
void AOakHillTown::BuildGround()
{
	using namespace JeebOakHill;
	const float Step = JeebTerrain::GroundStep, Coarse = JeebWorld::WorldStep;
	const int32 NI = FMath::RoundToInt((PatchX1 - PatchX0) / Step), NJ = FMath::RoundToInt((PatchY1 - PatchY0) / Step);
	auto CoarseLerp = [&](float X, float Y, bool bAlongY)
	{
		const float V = bAlongY ? Y : X;
		const float V0 = FMath::FloorToFloat(V / Coarse) * Coarse, T = (V - V0) / Coarse;
		if (T < 1.e-4f) return JeebTerrain::HeightAt(X, Y);
		return bAlongY ? FMath::Lerp(JeebTerrain::HeightAt(X, V0), JeebTerrain::HeightAt(X, V0 + Coarse), T)
		               : FMath::Lerp(JeebTerrain::HeightAt(V0, Y), JeebTerrain::HeightAt(V0 + Coarse, Y), T);
	};
	auto CoarseMask = [&](float X, float Y, bool bAlongY)
	{
		auto MaskAt = [](float PX, float PY) { return JeebGround::At(PX, PY, JeebTerrain::HeightAt(PX, PY), JeebGround::Upness(PX, PY)); };
		const float V = bAlongY ? Y : X;
		const float V0 = FMath::FloorToFloat(V / Coarse) * Coarse, T = (V - V0) / Coarse;
		if (T < 1.e-4f) return MaskAt(X, Y);
		return bAlongY ? JeebGround::FMask::Lerp(MaskAt(X, V0), MaskAt(X, V0 + Coarse), T) : JeebGround::FMask::Lerp(MaskAt(V0, Y), MaskAt(V0 + Coarse, Y), T);
	};
	TArray<FVector> V, N; TArray<FVector2D> UV; TArray<FLinearColor> Col; TArray<int32> Tri, Back;
	const bool bServer = IsNetMode(NM_DedicatedServer);
	for (int32 i = 0; i <= NI; ++i)
		for (int32 j = 0; j <= NJ; ++j)
		{
			const float X = PatchX0 + i * Step, Y = PatchY0 + j * Step;
			float Z;
			if (i == 0 || i == NI) Z = CoarseLerp(X, Y, true);        // the west and east ends (X): along Y
			else if (j == NJ) Z = CoarseLerp(X, Y, false);           // the far edge (Y): along X
			else Z = JeebTerrain::HeightAt(X, Y);                    // inside, and the edge shared with the square's own mesh
			V.Add(FVector(X, Y, Z));
			const float DX = (JeebTerrain::HeightAt(X + Step, Y) - JeebTerrain::HeightAt(X - Step, Y)) / (2.f * Step);
			const float DY = (JeebTerrain::HeightAt(X, Y + Step) - JeebTerrain::HeightAt(X, Y - Step)) / (2.f * Step);
			N.Add(FVector(-DX, -DY, 1.f).GetSafeNormal());
			UV.Add(FVector2D(X / 1000.f, Y / 1000.f));
			// what the ground is here (GroundMask.h: worn by roads and doorways, rock, mud, sand), painted by M_Ground like the square
			// and the world. Along the three edges shared with the world's 8 m mesh it takes the straight line between that mesh's
			// own corners, as the heights do, so the colours meet it exactly.
			if (!bServer)
			{
				if (i == 0 || i == NI) Col.Add(CoarseMask(X, Y, true).Color());
				else if (j == NJ) Col.Add(CoarseMask(X, Y, false).Color());
				else Col.Add(JeebGround::At(X, Y, Z, JeebGround::Upness(X, Y)).Color());
			}
			else Col.Add(FLinearColor::Black);
			HashIn(Hash, Z);
		}
	const int32 W = NJ + 1;
	for (int32 i = 0; i < NI; ++i)
		for (int32 j = 0; j < NJ; ++j)
		{
			// the same split as the square's mesh and JeebTerrain::GroundMeshHeight: A (X0,Y0), B (+X), C (+Y), D (+X+Y), along B-C
			const int32 A = i * W + j, B = A + W, C = A + 1, D = B + 1;
			Tri.Append({ A, C, B, B, C, D });
			Back.Append({ A, B, C, B, D, C });
		}
	{
		const FVector Fn = FVector::CrossProduct(V[Tri[1]] - V[Tri[0]], V[Tri[2]] - V[Tri[0]]);
		if (Fn.Z > 0.f) Swap(Tri, Back);   // (wound to face up as Unreal counts it, like GrassField: the cross product points DOWN. The other way round the collision faced down, and a knight landing on it was never on walkable floor and stayed falling, 2026-11-12)
	}
	Ground = NewObject<UProceduralMeshComponent>(this, TEXT("OakHillGround"));
	Ground->SetupAttachment(GetRootComponent());
	Ground->bUseAsyncCooking = false;              // the server must have it solid before anyone walks on it
	Ground->bUseComplexAsSimpleCollision = true;
	Ground->RegisterComponent();
	Ground->CreateMeshSection_LinearColor(0, V, Tri, N, UV, Col, TArray<FProcMeshTangent>(), true);
	Ground->CreateMeshSection_LinearColor(1, V, Back, N, UV, Col, TArray<FProcMeshTangent>(), false);
	Ground->SetCollisionProfileName(TEXT("BlockAll"));
	if (bServer) { Ground->SetVisibility(false); return; }
	UMaterialInterface* GroundMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Ground.M_Ground"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (int32 s = 0; s < 2; ++s)
	{
		if (GroundMat) Ground->SetMaterial(s, GroundMat);
		else if (UMaterialInstanceDynamic* M = Ground->CreateDynamicMaterialInstance(s, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.085f, 0.13f, 0.035f));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBOAK ground %d x %d cells, %d vertices, %d triangles"), NI, NJ, V.Num(), Tri.Num() / 3);
}

void AOakHillTown::BuildBuildings()
{
	using namespace JeebOakHill;
	const bool bServer = IsNetMode(NM_DedicatedServer);
	int32 Placed = 0, Missing = 0;
	for (const FPlot& P : Plots())
	{
		if (P.Kind != EPlot::Building || !P.Model) continue;
		const FModel* Mdl = FindModel(P.Model);
		const float Scale = Mdl ? Mdl->Scale : 1.f, DoorYaw = Mdl ? Mdl->DoorYaw : 90.f;
		const FVector2D C = FromPx(P.Px, P.Py);
		// The model's long side along the plot's long side, its door toward the road (shared with the grass's wall test, OakHill.h)
		const float Yaw = BuildingYaw(P, Mdl);
		const FVector At(C.X, C.Y, JeebTerrain::HeightAt(C.X, C.Y));   // (the plot is level: JeebOakHill::TownM)
		const FString Asset = AssetName(P.Model);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Buildings/SM_%s.SM_%s"), *Asset, *Asset));
		if (!Mesh) { ++Missing; UE_LOG(LogTemp, Warning, TEXT("JEEBOAK missing model /Game/Buildings/SM_%s (run Tools/BuildBuildings.ps1)"), *Asset); }
		UStaticMeshComponent* M = nullptr;
		if (Mesh && !bServer && !TestOff(TEXT("NoOakMeshes")))
		{
			M = NewObject<UStaticMeshComponent>(this);
			M->SetStaticMesh(Mesh);
			M->SetupAttachment(GetRootComponent());
			M->SetWorldLocationAndRotation(At, FRotator(0.f, Yaw, 0.f));
			M->SetWorldScale3D(FVector(Scale));
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			M->SetMobility(EComponentMobility::Static);
			M->RegisterComponent();
		}
		// Solid walls: a box over the model's footprint, a little inside its outline so porches, eaves and awnings can be walked under
		FVector Lo, Hi;
		if (Mesh) { const FBox Bx = Mesh->GetBoundingBox(); Lo = Bx.Min * Scale; Hi = Bx.Max * Scale; }
		// (the walls follow the foundation measured at foot height, not the eaves, porches and chimney: the full bounds stay for the clutter)
		FVector WLo = Lo * 0.86f, WHi = Hi * 0.86f;
		// (Unreal mirrors Blender's Y on import: a model's +Y in Blender is its -Y here)
		if (Mdl && Mdl->WX1 > Mdl->WX0) { WLo = FVector(Mdl->WX0, -Mdl->WY1, 0.f) * Scale; WHi = FVector(Mdl->WX1, -Mdl->WY0, 0.f) * Scale; }
		else { const float L = FMath::Max(P.W, P.H) * UnitCm * 0.5f, S = FMath::Min(P.W, P.H) * UnitCm * 0.5f; Lo = FVector(-L, -S, 0.f); Hi = FVector(L, S, 600.f); }
		const FVector Mid((WLo.X + WHi.X) * 0.5f, (WLo.Y + WHi.Y) * 0.5f, 0.f);
		const FVector Ext((WHi.X - WLo.X) * 0.5f, (WHi.Y - WLo.Y) * 0.5f, FMath::Min(Hi.Z, 600.f) * 0.5f);
		UBoxComponent* Walls = NewObject<UBoxComponent>(this);
		Walls->SetupAttachment(GetRootComponent());
		Walls->SetBoxExtent(Ext);
		Walls->SetWorldLocationAndRotation(At + FRotator(0.f, Yaw, 0.f).RotateVector(Mid) + FVector(0.f, 0.f, Ext.Z), FRotator(0.f, Yaw, 0.f));
		Walls->SetCollisionProfileName(TEXT("BlockAll"));
		Walls->SetMobility(EComponentMobility::Static);
		Walls->SetCanEverAffectNavigation(false);
		Walls->RegisterComponent();
		HashIn(Hash, At.X); HashIn(Hash, At.Y); HashIn(Hash, At.Z); HashIn(Hash, Yaw); HashIn(Hash, Ext.X); HashIn(Hash, Ext.Y); HashIn(Hash, Ext.Z);
		++Placed;
		// (its walls and door for the stones and shrubs at its foundation, GroundDecor.h; not the market stalls, which have their own
		// clutter round them, or the well on the lawn)
		if (FCString::Strcmp(P.Model, TEXT("GeneralStore")) != 0 && FCString::Strcmp(P.Model, TEXT("PotionStall")) != 0 && FCString::Strcmp(P.Model, TEXT("BankStall")) != 0 && FCString::Strcmp(P.Model, TEXT("OakHillWell")) != 0)
		{
			const FVector WallMid = At + FRotator(0.f, Yaw, 0.f).RotateVector(Mid);
			JeebDecor::FFootprint Fp;
			Fp.Centre = FVector2D(WallMid.X, WallMid.Y); Fp.Yaw = Yaw; Fp.Half = FVector2D(Ext.X, Ext.Y); Fp.DoorYaw = Yaw + DoorYaw;
			Foundations.Add(Fp);
		}
		// the market stalls get barrels, crates and sacks round them (the bank stall has no yard behind: only beside it)
		if (FCString::Strcmp(P.Model, TEXT("GeneralStore")) == 0 || FCString::Strcmp(P.Model, TEXT("PotionStall")) == 0 || FCString::Strcmp(P.Model, TEXT("BankStall")) == 0)
			BuildStoreClutter(At, Yaw + DoorYaw, (Hi.X - Lo.X) * 0.5f, (Hi.Y - Lo.Y) * 0.5f, FCString::Strcmp(P.Model, TEXT("BankStall")) != 0);
		UE_LOG(LogTemp, Log, TEXT("JEEBOAK %s (%s) sketch %.0f,%.0f at %.0f %.0f %.0f yaw %.0f, walls %.0f x %.0f x %.0f cm, road clearance %.1f m"), P.Name, *Asset, P.Px, P.Py,
			At.X, At.Y, At.Z, Yaw, Ext.X * 2.f, Ext.Y * 2.f, Ext.Z * 2.f, RoadClearanceM(EastM(At.Y), NorthM(At.X)) - FMath::Min(P.W, P.H) * 0.5f * Unit);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBOAK %d buildings placed, %d models missing"), Placed, Missing);
}

void AOakHillTown::BuildStoreClutter(const FVector& At, float FrontYaw, float HalfWidthCm, float HalfDepthCm, bool bBehind)
{
	const bool bServer = IsNetMode(NM_DedicatedServer);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	// One instanced set per shape and colour (rich clean colours, no dirt: the art direction)
	TMap<FString, UInstancedStaticMeshComponent*> Sets;
	auto Set = [&](UStaticMesh* Mesh, const TCHAR* Key, const FLinearColor& C) -> UInstancedStaticMeshComponent*
	{
		if (bServer || !Mesh) return nullptr;
		if (UInstancedStaticMeshComponent** Got = Sets.Find(Key)) return *Got;
		UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
		I->SetStaticMesh(Mesh);
		I->SetupAttachment(GetRootComponent());
		I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		I->SetMobility(EComponentMobility::Static);
		I->RegisterComponent();
		if (Base) if (UMaterialInstanceDynamic* M = I->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), C);
		Sets.Add(Key, I);
		return I;
	};
	const FLinearColor Wood(0.20f, 0.10f, 0.045f), WoodDark(0.12f, 0.06f, 0.028f), Plank(0.30f, 0.17f, 0.08f), Iron(0.035f, 0.035f, 0.04f),
		Burlap(0.42f, 0.32f, 0.17f), Rope(0.50f, 0.38f, 0.20f), Apple(0.45f, 0.05f, 0.03f);
	const FRotator Face(0.f, FrontYaw, 0.f);
	// Local spot: F along the open front (negative is behind the stall), R to the right as you face the front, Z up (cm)
	auto Spot = [&](float F, float R, float Z)
	{
		const FVector P = At + Face.RotateVector(FVector(F, R, 0.f));
		return FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) + Z);
	};
	int32 Pieces = 0;
	auto Put = [&](UStaticMesh* Mesh, const TCHAR* Key, const FLinearColor& C, const FVector& P, const FRotator& Rot, const FVector& S)
	{
		if (UInstancedStaticMeshComponent* I = Set(Mesh, Key, C)) I->AddInstance(FTransform(Rot, P, S), true);
		++Pieces;
	};
	auto Solid = [&](float F, float R, const FVector& HalfCm)
	{
		UBoxComponent* Bx = NewObject<UBoxComponent>(this);
		Bx->SetupAttachment(GetRootComponent());
		Bx->SetBoxExtent(HalfCm);
		Bx->SetWorldLocationAndRotation(Spot(F, R, HalfCm.Z), Face);
		Bx->SetCollisionProfileName(TEXT("BlockAll"));
		Bx->SetMobility(EComponentMobility::Static);
		Bx->SetCanEverAffectNavigation(false);
		Bx->RegisterComponent();
		HashIn(Hash, Bx->GetComponentLocation().X); HashIn(Hash, Bx->GetComponentLocation().Y);
	};
	auto Barrel = [&](float F, float R, float Turn)
	{
		const FRotator Rt(0.f, FrontYaw + Turn, 0.f);
		Put(Cyl, TEXT("barrel"), Wood, Spot(F, R, 50.f), Rt, FVector(0.66f, 0.66f, 1.0f));
		Put(Cyl, TEXT("barrelmid"), Plank, Spot(F, R, 50.f), Rt, FVector(0.72f, 0.72f, 0.5f));   // (the bulge)
		for (float Z : { 12.f, 88.f }) Put(Cyl, TEXT("hoop"), Iron, Spot(F, R, Z), Rt, FVector(0.69f, 0.69f, 0.05f));
		Put(Cyl, TEXT("lid"), WoodDark, Spot(F, R, 100.5f), Rt, FVector(0.6f, 0.6f, 0.02f));
	};
	auto Crate = [&](float F, float R, float Z, float Size, float Turn)
	{
		const FRotator Rt(0.f, FrontYaw + Turn, 0.f);
		const float S = Size / 100.f;
		const FVector C = Spot(F, R, Z + Size * 0.5f);
		Put(Cube, TEXT("crate"), Plank, C, Rt, FVector(S, S, S));
		// dark corner posts and a middle band, so it reads as boards
		for (float A : { -1.f, 1.f }) for (float B : { -1.f, 1.f })
			Put(Cube, TEXT("cratepost"), WoodDark, C + Rt.RotateVector(FVector(A, B, 0.f) * (Size * 0.5f - 2.f)), Rt, FVector(0.06f, 0.06f, S * 1.01f));
		Put(Cube, TEXT("cratepost"), WoodDark, C, Rt, FVector(S * 1.02f, S * 1.02f, 0.06f));
	};
	auto Sack = [&](float F, float R, float Lean)
	{
		Put(Ball, TEXT("sack"), Burlap, Spot(F, R, 30.f), FRotator(Lean, FrontYaw + Lean * 3.f, 0.f), FVector(0.5f, 0.45f, 0.62f));
		Put(Ball, TEXT("sack"), Burlap, Spot(F, R, 58.f), FRotator(Lean, FrontYaw, 0.f), FVector(0.22f, 0.22f, 0.16f));   // the tied neck
	};
	const float Back = -HalfDepthCm - 110.f, W = HalfWidthCm;
	if (bBehind)
	{
	// Behind, left: a stack of crates (two below, one on top) and three barrels
	Crate(Back, -W + 80.f, 0.f, 80.f, 5.f); Crate(Back, -W + 170.f, 0.f, 75.f, -8.f); Crate(Back - 5.f, -W + 120.f, 80.f, 65.f, 20.f);
	Solid(Back, -W + 125.f, FVector(45.f, 90.f, 70.f));
	for (int32 k = 0; k < 3; ++k) Barrel(Back - 10.f, -W + 280.f + k * 75.f, k * 37.f);
	Solid(Back - 10.f, -W + 355.f, FVector(36.f, 115.f, 50.f));
	// Behind, middle: sacks against the back wall, a rope coil and a crate of apples
	for (int32 k = 0; k < 4; ++k) Sack(-HalfDepthCm - 35.f, -60.f + k * 48.f, (k % 2 ? 8.f : -6.f));
	Put(Cyl, TEXT("rope"), Rope, Spot(Back + 20.f, 170.f, 6.f), FRotator::ZeroRotator, FVector(0.55f, 0.55f, 0.12f));
	Put(Cyl, TEXT("ropehole"), WoodDark, Spot(Back + 20.f, 170.f, 7.f), FRotator::ZeroRotator, FVector(0.25f, 0.25f, 0.12f));
	Crate(Back - 20.f, 60.f, 0.f, 60.f, 12.f);
	for (int32 k = 0; k < 6; ++k) Put(Ball, TEXT("apple"), Apple, Spot(Back - 20.f + (k % 3 - 1) * 15.f, 60.f + (k / 3 - 0.5f) * 18.f, 64.f), FRotator::ZeroRotator, FVector(0.12f));
	// Behind, right: a hand cart resting on its handles, with a barrel and a sack on it
	{
		const float CF = Back - 40.f, CR = W - 170.f;
		const FRotator Tip(-6.f, FrontYaw + 90.f, 0.f);   // (its long side across the back of the stall)
		Put(Cube, TEXT("cart"), Plank, Spot(CF, CR, 62.f), Tip, FVector(2.1f, 1.2f, 0.12f));
		for (float Side : { -1.f, 1.f })
		{
			Put(Cube, TEXT("cartside"), WoodDark, Spot(CF + Side * 58.f, CR, 80.f), Tip, FVector(2.1f, 0.06f, 0.3f));
			Put(Cyl, TEXT("wheel"), WoodDark, Spot(CF + Side * 70.f, CR + 30.f, 48.f), FRotator(0.f, FrontYaw, 90.f), FVector(0.96f, 0.96f, 0.08f));
			Put(Cyl, TEXT("hub"), Iron, Spot(CF + Side * 76.f, CR + 30.f, 48.f), FRotator(0.f, FrontYaw, 90.f), FVector(0.18f, 0.18f, 0.1f));
			Put(Cube, TEXT("cartpost"), WoodDark, Spot(CF + Side * 45.f, CR - 165.f, 30.f), FRotator(-14.f, FrontYaw + 90.f, 0.f), FVector(0.9f, 0.06f, 0.06f));
		}
		Barrel(CF, CR + 50.f, 0.f);
		Sack(CF + 10.f, CR - 40.f, 10.f);
		Solid(CF, CR - 30.f, FVector(65.f, 140.f, 55.f));
	}
	}
	// Each side of the stall: a barrel and a crate by the corner, set back from the front
	for (float Side : { -1.f, 1.f })
	{
		Barrel(-HalfDepthCm * 0.3f, Side * (W + 60.f), 20.f * Side);
		Crate(-HalfDepthCm * 0.3f - 85.f, Side * (W + 55.f), 0.f, 70.f, 15.f * Side);
		Solid(-HalfDepthCm * 0.3f - 40.f, Side * (W + 58.f), FVector(85.f, 38.f, 50.f));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBOAK store clutter %d pieces in %d instanced sets"), Pieces, Sets.Num());
}

void AOakHillTown::PlantTrees(AForest* Forest)
{
	using namespace JeebOakHill;
	if (!Forest || !Enabled() || TestOff(TEXT("NoOakTrees"))) return;
	FRandomStream R(5150);   // fixed: the same trees on every machine
	// The big oak in the middle of the plaza's lawn
	{
		const FVector2D C = FromPx(255.f, 410.f);
		const float S = 1.9f;
		Forest->AddTree(ETreeKind::Oak, FVector(C.X, C.Y, JeebTerrain::HeightAt(C.X, C.Y) - 20.f * S), S, R);
	}
	// Trees in the gaps between the plots (few in the middle of town, more toward its edges), never on a road, a plot, the lawn
	// or a river bank, and at least 7 m apart
	TArray<FVector2D> Got;
	int32 Tries = 0;
	while (Got.Num() < 150 && Tries < 4000)
	{
		++Tries;
		const float E = R.FRandRange(-50.f, 84.f), N = R.FRandRange(-110.f, 116.f);
		const float Roll = R.FRand(), Kind = R.FRand(), S = R.FRandRange(0.85f, 1.2f);
		const float FromPlaza = FVector2D(E, N).Size();
		const float Want = FMath::Lerp(0.12f, 0.7f, JeebWorld::Smooth(32.f, 66.f, FromPlaza));
		if (Roll > Want) continue;
		const float X = PosX + N * 100.f, Y = PosY + E * 100.f;
		float Half;
		if (JeebWorld::RiverDistance(X, Y, Half) < Half + 600.f) continue;
		if (JeebTerrain::PathClearance(X, Y) < 450.f) continue;
		bool bClear = true;
		for (const FPlot& P : Plots()) if (PlotSD(P, E, N) < (P.Kind == EPlot::Building ? 6.f : 3.f)) { bClear = false; break; }
		if (!bClear) continue;
		for (const FVector2D& G : Got) if (FVector2D::DistSquared(G, FVector2D(X, Y)) < 700.f * 700.f) { bClear = false; break; }
		if (!bClear) continue;
		Got.Add(FVector2D(X, Y));
		const ETreeKind K = (FromPlaza > 50.f && Kind < 0.25f) ? ETreeKind::Pine : ETreeKind::Oak;
		Forest->AddTree(K, FVector(X, Y, JeebTerrain::HeightAt(X, Y) - 20.f * S), S, R);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBOAK trees %d (+ the plaza oak)"), Got.Num());
}

// Measured once per world build, so a wrong number shows in the log before any picture: heights at the named spots against
// the plan, each road's steepest grade, and the steepest ground away from the river banks.
void AOakHillTown::LogChecks()
{
	using namespace JeebOakHill;
	struct FSpot { const TCHAR* Name; float Px, Py, Want; };
	const FSpot Spots[] = { { TEXT("plaza"), 255.f, 410.f, 6.4f }, { TEXT("castle knoll"), 240.f, 95.f, 9.f }, { TEXT("church rise"), 495.f, 140.f, 6.8f },
		{ TEXT("saddle"), 235.f, 240.f, 5.f }, { TEXT("boxing ring"), 255.f, 610.f, 4.5f }, { TEXT("west road end"), -31.f, 410.f, 2.5f } };
	for (const FSpot& S : Spots)
	{
		const FVector2D P = FromPx(S.Px, S.Py);
		UE_LOG(LogTemp, Log, TEXT("JEEBOAK height %s %.2f m (plan %.1f)"), S.Name, (JeebTerrain::HeightAt(P.X, P.Y) - JeebWorld::RiverLevel) / 100.f, S.Want);
	}
	for (int32 r = 0; r < Roads().Num(); ++r)
	{
		const TArray<FVector2D>& Pts = Roads()[r].Pts;
		float Worst = 0.f; FVector2D WorstAt(0.f, 0.f);
		for (int32 i = 8; i < Pts.Num(); i += 8)
		{
			const float D = FVector2D::Distance(Pts[i], Pts[i - 8]);
			if (D < 1.f) continue;
			const float G = FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(JeebTerrain::HeightAt(Pts[i].X, Pts[i].Y) - JeebTerrain::HeightAt(Pts[i - 8].X, Pts[i - 8].Y)) / D));
			if (G > Worst) { Worst = G; WorstAt = Pts[i]; }
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBOAK road %d: %d points, steepest %.1f degrees at e %.0f n %.0f m"), r, Pts.Num(), Worst, EastM(WorstAt.Y), NorthM(WorstAt.X));
	}
	// The closest two buildings, wall to wall (the plots' boxes, sampled round each outline)
	{
		float Closest = 1.0e9f; const FPlot* CA = nullptr; const FPlot* CB = nullptr;
		const TArray<FPlot>& Ps = Plots();
		for (int32 i = 0; i < Ps.Num(); ++i)
			for (int32 j = i + 1; j < Ps.Num(); ++j)
			{
				if (Ps[i].Kind != EPlot::Building || Ps[j].Kind != EPlot::Building) continue;
				for (int32 k = 0; k < 64; ++k)
				{
					const float A0 = 2.f * PI * k / 64.f;
					// (a point on plot i's outline: walk out from its centre until the box's edge)
					const FVector2D Dir(FMath::Cos(A0), FMath::Sin(A0));
					float Lo = 0.f, Hi = Ps[i].Reach;
					for (int32 s = 0; s < 20; ++s) { const float Mid = 0.5f * (Lo + Hi); (PlotSD(Ps[i], Ps[i].Ce + Dir.X * Mid, Ps[i].Cn + Dir.Y * Mid) < 0.f ? Lo : Hi) = Mid; }
					const float G = PlotSD(Ps[j], Ps[i].Ce + Dir.X * Lo, Ps[i].Cn + Dir.Y * Lo);
					if (G < Closest) { Closest = G; CA = &Ps[i]; CB = &Ps[j]; }
				}
			}
		if (CA) UE_LOG(LogTemp, Log, TEXT("JEEBOAK closest buildings %.1f m apart: %s (sketch %.0f,%.0f) and %s (sketch %.0f,%.0f)"), Closest, CA->Name, CA->Px, CA->Py, CB->Name, CB->Px, CB->Py);
	}
	float Steep = 0.f; FVector2D SteepAt(0.f, 0.f);
	for (float E = -45.f; E <= 80.f; E += 2.f)
		for (float N = -95.f; N <= 115.f; N += 2.f)
		{
			const float X = PosX + N * 100.f, Y = PosY + E * 100.f;
			float Half;
			if (JeebWorld::RiverDistance(X, Y, Half) < Half + 1200.f) continue;
			const float G = FVector2D(JeebTerrain::HeightAt(X + 100.f, Y) - JeebTerrain::HeightAt(X - 100.f, Y), JeebTerrain::HeightAt(X, Y + 100.f) - JeebTerrain::HeightAt(X, Y - 100.f)).Size() / 200.f;
			if (G > Steep) { Steep = G; SteepAt = FVector2D(E, N); }
		}
	UE_LOG(LogTemp, Log, TEXT("JEEBOAK steepest ground away from the rivers %.1f degrees at e %.0f n %.0f m"), FMath::RadiansToDegrees(FMath::Atan(Steep)), SteepAt.X, SteepAt.Y);
	for (const FPlot& P : Plots())
		if (PlotSD(P, SteepAt.X, SteepAt.Y) < PadFlat + PadBlend + 2.f)
			UE_LOG(LogTemp, Log, TEXT("JEEBOAK   near it: %s (sketch %.0f,%.0f), %.1f m from its outline, level %d"), P.Name, P.Px, P.Py, PlotSD(P, SteepAt.X, SteepAt.Y), P.bLevel ? 1 : 0);
	for (float D = -6.f; D <= 6.f; D += 2.f)
		UE_LOG(LogTemp, Log, TEXT("JEEBOAK   across it: e %+.0f: %.2f m, n %+.0f: %.2f m"), D, (JeebTerrain::HeightAt(PosX + SteepAt.Y * 100.f, PosY + (SteepAt.X + D) * 100.f) - JeebWorld::RiverLevel) / 100.f,
			D, (JeebTerrain::HeightAt(PosX + (SteepAt.Y + D) * 100.f, PosY + SteepAt.X * 100.f) - JeebWorld::RiverLevel) / 100.f);
}
