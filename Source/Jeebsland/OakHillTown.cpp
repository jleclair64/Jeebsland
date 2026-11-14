#include "OakHillTown.h"
#include "OakHill.h"
#include "Terrain.h"
#include "GroundMask.h"
#include "GroundDecor.h"
#include "Forest.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
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
	BuildPlazaOak();
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
			M->SetWorldScale3D(FVector(Scale, Scale, Mdl && Mdl->ScaleZ > 0.f ? Mdl->ScaleZ : Scale));   // (ScaleZ: the potion stall keeps its counter's height)
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
		// Solid shape (2026-11-13, owner: "still being cut off by an invisible wall outside these houses", "go up on the stairs",
		// "go under the awning"): SM_<Asset>_Col, a reduced copy of the model whose own triangles are its collision (building_clean.py,
		// building_import.py), placed exactly like the model (same spot, turn and scale), never drawn, on every machine: the server
		// decides where he can walk. He walks up the steps (under 45 cm each), onto porches, under awnings and the forge's roof, and
		// stops at the stone. Not tagged JeebClimb: buildings can't be climbed. Without it (an old import): the old box over the walls.
		UStaticMesh* ColMesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Buildings/SM_%s_Col.SM_%s_Col"), *Asset, *Asset));
		if (ColMesh)
		{
			UStaticMeshComponent* Solid = NewObject<UStaticMeshComponent>(this);
			Solid->SetStaticMesh(ColMesh);
			Solid->SetupAttachment(GetRootComponent());
			Solid->SetWorldLocationAndRotation(At, FRotator(0.f, Yaw, 0.f));
			Solid->SetWorldScale3D(FVector(Scale, Scale, Mdl && Mdl->ScaleZ > 0.f ? Mdl->ScaleZ : Scale));   // (as the model's, above)
			Solid->SetVisibility(false);
			Solid->SetHiddenInGame(true);
			Solid->SetCastShadow(false);
			Solid->SetCollisionProfileName(TEXT("BlockAll"));
			Solid->SetMobility(EComponentMobility::Static);
			Solid->SetCanEverAffectNavigation(false);
			Solid->RegisterComponent();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("JEEBOAK no collision model /Game/Buildings/SM_%s_Col: a box instead (run Tools/BuildBuildings.ps1)"), *Asset);
			UBoxComponent* Walls = NewObject<UBoxComponent>(this);
			Walls->SetupAttachment(GetRootComponent());
			Walls->SetBoxExtent(Ext);
			Walls->SetWorldLocationAndRotation(At + FRotator(0.f, Yaw, 0.f).RotateVector(Mid) + FVector(0.f, 0.f, Ext.Z), FRotator(0.f, Yaw, 0.f));
			Walls->SetCollisionProfileName(TEXT("BlockAll"));
			Walls->SetMobility(EComponentMobility::Static);
			Walls->SetCanEverAffectNavigation(false);
			Walls->RegisterComponent();
		}
		HashIn(Hash, ColMesh ? 1.f : 0.f);
		HashIn(Hash, At.X); HashIn(Hash, At.Y); HashIn(Hash, At.Z); HashIn(Hash, Yaw); HashIn(Hash, Ext.X); HashIn(Hash, Ext.Y); HashIn(Hash, Ext.Z);
		++Placed;
		// (its walls and door for the stones and shrubs at its foundation, GroundDecor.h; not the open market stalls, whose fronts and
		// counters stay clear, or the well on the lawn)
		if (FCString::Strcmp(P.Model, TEXT("GeneralStore")) != 0 && FCString::Strcmp(P.Model, TEXT("PotionStall")) != 0 && FCString::Strcmp(P.Model, TEXT("BankStall")) != 0 && FCString::Strcmp(P.Model, TEXT("OakHillWell")) != 0)
		{
			const FVector WallMid = At + FRotator(0.f, Yaw, 0.f).RotateVector(Mid);
			JeebDecor::FFootprint Fp;
			Fp.Centre = FVector2D(WallMid.X, WallMid.Y); Fp.Yaw = Yaw; Fp.Half = FVector2D(Ext.X, Ext.Y); Fp.DoorYaw = Yaw + DoorYaw;
			Foundations.Add(Fp);
		}
		// (the barrels, crates and sacks that stood round the stalls were removed, owner 2026-11-13: "cut them out and remove
		// them ... We'll add our own")
		UE_LOG(LogTemp, Log, TEXT("JEEBOAK %s (%s) sketch %.0f,%.0f at %.0f %.0f %.0f yaw %.0f, walls %.0f x %.0f x %.0f cm, road clearance %.1f m"), P.Name, *Asset, P.Px, P.Py,
			At.X, At.Y, At.Z, Yaw, Ext.X * 2.f, Ext.Y * 2.f, Ext.Z * 2.f, RoadClearanceM(EastM(At.Y), NorthM(At.X)) - FMath::Min(P.W, P.H) * 0.5f * Unit);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBOAK %d buildings placed, %d models missing"), Placed, Missing);
}

// The plaza's big oak (2026-11-13, owner: "Here's an oak tree for the middle of oak hill. See if you can color it autumn colors"):
// Meshy's library "Tree" by Tansy, cleaned and repainted in autumn by Tools/plaza_oak_clean.py (built at its real size, 16.6 m tall,
// its crown about 9.2 m in radius over the 8.4 m lawn, its roots cut 5 m out), /Game/Trees/SM_PlazaOak. Drawn on clients (Nanite, its crown swaying gently
// in M_PlazaOak; the sway stops beyond PlazaOakSwayCm); the trunk's capsule blocks on every machine, the server too.
static constexpr float PlazaOakYaw = 35.f;            // (fixed: the same on every machine)
static constexpr float PlazaOakTrunkR = 110.f, PlazaOakTrunkHalfH = 150.f;   // (the trunk measured 1.45 m in radius at 1 m with its root flare, 0.9 m at 2 m)
static constexpr float PlazaOakSwayCm = 6000.f;
static constexpr float PlazaOakFill = 12000.f, PlazaOakFillUp = 650.f, PlazaOakFillCone = 54.f;   // (the fill light under the crown: tan 54 x 6.5 m = 8.9 m on the lawn)
UStaticMesh* AOakHillTown::LoadPlazaOak()
{
	// (-NoPlazaOak, test pictures only: the woods' own oak stands in, to measure what the Meshy oak costs)
	static UStaticMesh* Mesh = JeebOakHill::TestOff(TEXT("NoPlazaOak")) ? nullptr : LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Trees/SM_PlazaOak.SM_PlazaOak"));
	return Mesh;
}

void AOakHillTown::BuildPlazaOak()
{
	using namespace JeebOakHill;
	if (TestOff(TEXT("NoOakTrees"))) return;
	UStaticMesh* Mesh = LoadPlazaOak();
	if (!Mesh) { UE_LOG(LogTemp, Warning, TEXT("JEEBOAK missing /Game/Trees/SM_PlazaOak (run Tools/BuildPlazaOak.ps1): the woods' oak stands in")); return; }
	const FVector2D C = FromPx(255.f, 410.f);
	const FVector At(C.X, C.Y, JeebTerrain::HeightAt(C.X, C.Y));
	if (!IsNetMode(NM_DedicatedServer) && !TestOff(TEXT("NoOakMeshes")))
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
		M->SetStaticMesh(Mesh);
		M->SetupAttachment(GetRootComponent());
		M->SetWorldLocationAndRotation(At, FRotator(0.f, PlazaOakYaw, 0.f));
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetMobility(EComponentMobility::Static);
		M->SetWorldPositionOffsetDisableDistance(FMath::RoundToInt(PlazaOakSwayCm));   // (world units, cm)
		M->RegisterComponent();
		// The light the leaves let through (visual review 2026-11-13: under the dense crown the lawn and the trunk went near-black):
		// one soft amber spot from inside the crown down onto the lawn, no shadows, reaching about the crown's edge only, so the
		// shop fronts beyond it keep their own light. -OakFill=<intensity> overrides it in test pictures.
		float Fill = PlazaOakFill;
		FParse::Value(FCommandLine::Get(), TEXT("OakFill="), Fill);
		if (Fill > 0.f)
		{
			USpotLightComponent* L = NewObject<USpotLightComponent>(this);
			L->SetupAttachment(GetRootComponent());
			L->SetWorldLocationAndRotation(At + FVector(0.f, 0.f, PlazaOakFillUp), FRotator(-90.f, 0.f, 0.f));
			L->SetMobility(EComponentMobility::Movable);
			L->SetIntensity(Fill);
			L->SetLightColor(FLinearColor(1.0f, 0.70f, 0.40f));
			L->SetAttenuationRadius(PlazaOakFillUp * 1.9f);
			L->SetInnerConeAngle(30.f);
			L->SetOuterConeAngle(PlazaOakFillCone);
			L->SetCastShadows(false);
			L->RegisterComponent();
		}
	}
	UCapsuleComponent* Trunk = NewObject<UCapsuleComponent>(this);
	Trunk->SetupAttachment(GetRootComponent());
	Trunk->SetCapsuleSize(PlazaOakTrunkR, PlazaOakTrunkHalfH);
	Trunk->SetWorldLocation(At + FVector(0.f, 0.f, PlazaOakTrunkHalfH - 50.f));
	Trunk->SetCollisionProfileName(TEXT("BlockAll"));
	Trunk->SetMobility(EComponentMobility::Static);
	Trunk->SetCanEverAffectNavigation(false);
	Trunk->RegisterComponent();
	HashIn(Hash, At.X); HashIn(Hash, At.Y); HashIn(Hash, At.Z); HashIn(Hash, PlazaOakTrunkR);
	const FBox B = Mesh->GetBoundingBox();
	UE_LOG(LogTemp, Log, TEXT("JEEBOAK plaza oak at %.0f %.0f %.0f, %.0f x %.0f x %.0f cm, trunk capsule r %.0f"), At.X, At.Y, At.Z, B.GetSize().X, B.GetSize().Y, B.GetSize().Z, PlazaOakTrunkR);
}

void AOakHillTown::PlantTrees(AForest* Forest)
{
	using namespace JeebOakHill;
	if (!Forest || !Enabled() || TestOff(TEXT("NoOakTrees"))) return;
	FRandomStream R(5150);   // fixed: the same trees on every machine
	// The big oak in the middle of the plaza's lawn: the Meshy oak (BuildPlazaOak) when it is there, else the woods' own oak
	if (!LoadPlazaOak())
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
	// (the plan's heights from the offline map of the 2026-11-13 layout, twice as hilly; 0 where it gave none)
	const FSpot Spots[] = { { TEXT("plaza"), 255.f, 410.f, 6.9f }, { TEXT("castle knoll"), 240.f, 95.f, 9.5f }, { TEXT("church rise"), 495.f, 140.f, 6.3f },
		{ TEXT("saddle"), 235.f, 240.f, 0.f }, { TEXT("boxing ring"), 255.f, 610.f, 0.f }, { TEXT("west road end"), -110.f, 410.f, 0.f } };
	for (const FSpot& S : Spots)
	{
		const FVector2D P = FromPx(S.Px, S.Py);
		UE_LOG(LogTemp, Log, TEXT("JEEBOAK height %s %.2f m (plan %.1f)"), S.Name, (JeebTerrain::HeightAt(P.X, P.Y) - JeebWorld::RiverLevel) / 100.f, S.Want);
	}
	// The shop counters (E there opens the shop's window)
	for (const FCounter& C : Counters())
		UE_LOG(LogTemp, Log, TEXT("JEEBOAK counter %s (%s) at %.0f %.0f %.0f, facing %.0f, %.1f m from the oak"), C.Name, C.Shop == EShop::Smith ? TEXT("smithy") : C.Shop == EShop::Bank ? TEXT("bank") : TEXT("store"),
			C.At.X, C.At.Y, JeebTerrain::HeightAt(C.At.X, C.At.Y), C.Facing, FVector2D::Distance(C.At, FVector2D(PosX, PosY)) / 100.f);
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
