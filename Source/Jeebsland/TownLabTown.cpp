#include "TownLabTown.h"
#include "TownLab.h"
#include "OakHill.h"
#include "Terrain.h"
#include "GroundMask.h"
#include "GroundDecor.h"
#include "Forest.h"
#include "WorldTerrain.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Components/PointLightComponent.h"
#include "TownLabDecor.h"
#include "DayNight.h"

namespace
{
	TAutoConsoleVariable<float> GroundFill(TEXT("jeeb.TownLab.GroundFill"), 0.15f, TEXT("Light added to the rebuilt town's ground at the low sun (times ADayNight::Dusk), so it stays as bright as the grass blades"));
	FString AssetName(const TCHAR* Name) { return FString(Name) + (FCString::Strcmp(Name, TEXT("OakHillWell")) == 0 ? TEXT("_150k") : TEXT("_300k")); }
	// WHICH MODEL EACH BUILDING OF THE NEW TOWN USES. Any of the town's models can be swapped here for a new one (a Fab pack's, a
	// Meshy remake) without touching anything else: Mesh is the new static mesh's path (its solid shape is Mesh + "_Col" when that
	// exists, else the old model's), Scale replaces OakHill.h's FModel Scale when above 0, YawAdd turns it (its door must face +Y as
	// the old models' do, or say so here), Lift raises it in cm. A missing mesh falls back to the old model, so a row can wait for
	// its asset. PlotSwaps does the same for single plots (the plot's index in JeebOakHill::Plots(), from the PLOT log lines).
	// Keep the old model's frame and Scale 0 where possible: the shop counters (OakHill.h Counters), the grass's footprints
	// (OakHillFootprints.h) and the chimneys (OakHillLife.cpp) are all measured on the old models.
	struct FLabSwap { const TCHAR* Model; const TCHAR* Mesh; float Scale; float YawAdd; float Lift; };
	const FLabSwap ModelSwaps[] = {
		{ TEXT("OakHillHouse"), TEXT("/Game/TownLab/Buildings/SM_TL_House"), 0.f, 0.f, 0.f },   // townbuild's rebuilt house
		{ TEXT("OakHillStore"), TEXT("/Game/TownLab/Buildings/SM_TL_Store"), 0.f, 0.f, 0.f },   // townbuild's rebuilt store
		// build-b's four (townbuild2, 2026-11-14)
		{ TEXT("OakHillTownHall"), TEXT("/Game/TownLab/Buildings/SM_TL_TownHall"), 0.f, 0.f, 0.f },
		{ TEXT("OakHillChurch"), TEXT("/Game/TownLab/Buildings/SM_TL_Church"), 0.f, 0.f, 0.f },
		{ TEXT("OakHillWell"), TEXT("/Game/TownLab/Buildings/SM_TL_Well"), 0.f, 0.f, 0.f },
		{ TEXT("OakHillPotionShop"), TEXT("/Game/TownLab/Buildings/SM_TL_PotionShop"), 0.f, 0.f, 0.f },
		{ TEXT("OakHillBank"), TEXT("/Game/TownLab/Buildings/SM_TL_Bank"), 0.f, 0.f, 0.f },   // build-a: the bank
		{ TEXT("BankStall"), TEXT("/Game/TownLab/Buildings/SM_TL_BankStall"), 0.f, 0.f, 0.f },   // build-a: the bank stall
		{ TEXT("OakHillSmith"), TEXT("/Game/TownLab/Buildings/SM_TL_Smith"), 0.f, 0.f, 0.f },   // build-a: the smithy (its forge burns all day)
		{ TEXT("OakHillCornerHouse"), TEXT("/Game/TownLab/Buildings/SM_TL_CornerHouse"), 0.f, 0.f, 0.f },   // build-a: the corner house
		{ TEXT("GeneralStore"), TEXT("/Game/TownLab/Buildings/SM_TL_MarketStall"), 0.f, 0.f, 0.f },   // build-c's rebuilt market stall
		{ TEXT("PotionStall"), TEXT("/Game/TownLab/Buildings/SM_TL_PotionStall"), 0.f, 0.f, 0.f },    // build-c's rebuilt potion stall
	};
	struct FPlotSwap { int32 Plot; const TCHAR* Mesh; float Scale; float YawAdd; float Lift; };
	const FPlotSwap PlotSwaps[] = {
		{ -1, nullptr, 0.f, 0.f, 0.f },   // (none yet)
	};
	struct FLabPick { FString Mesh; float Scale = 0.f, YawAdd = 0.f, Lift = 0.f; };
	FLabPick LabAsset(int32 PlotIndex, const TCHAR* Model)
	{
		FLabPick Pick;
		for (const FPlotSwap& S : PlotSwaps) if (S.Plot == PlotIndex && S.Mesh) { Pick.Mesh = S.Mesh; Pick.Scale = S.Scale; Pick.YawAdd = S.YawAdd; Pick.Lift = S.Lift; return Pick; }
		for (const FLabSwap& S : ModelSwaps) if (FCString::Strcmp(Model, S.Model) == 0) { Pick.Mesh = S.Mesh; Pick.Scale = S.Scale; Pick.YawAdd = S.YawAdd; Pick.Lift = S.Lift; return Pick; }
		return Pick;
	}
	float Hash01(uint32 N) { N ^= N >> 16; N *= 0x7feb352dU; N ^= N >> 15; N *= 0x846ca68bU; N ^= N >> 16; return float(N & 0xffffff) / float(0x1000000); }
	void HashIn(uint32& H, float V)
	{
		const int32 Q = FMath::RoundToInt(V * 10.f);   // millimetres
		for (int32 b = 0; b < 4; ++b) { H ^= uint32((Q >> (8 * b)) & 0xFF); H *= 16777619u; }
	}
	bool InstancingOk(UStaticMesh* Mesh)
	{
		for (const FStaticMaterial& M : Mesh->GetStaticMaterials())
			if (M.MaterialInterface && M.MaterialInterface->GetMaterial() && !M.MaterialInterface->GetMaterial()->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes)) return false;
		return true;
	}
	UStaticMesh* LoadMesh(const FString& Path)
	{
		// (asked many times per model. Weak pointers: the cache outlives a world, and a client builds the town twice, in the login
		// screen's world and again after travelling to the server, so a raw pointer here dangled once the first world's meshes were
		// collected and crashed the second build, 2026-11-14; a miss is asked again)
		static TMap<FString, TWeakObjectPtr<UStaticMesh>> Cache;
		if (const TWeakObjectPtr<UStaticMesh>* Got = Cache.Find(Path)) if (Got->IsValid()) return Got->Get();
		const FString Name = FPaths::GetBaseFilename(Path);
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *Path, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
		Cache.Add(Path, M);
		return M;
	}
	// A decor piece's mesh. The library props were made in /Game/Filler (the owner's filler lineup, never cooked: 700 MB of options);
	// the ones the town uses are copied with their materials to /Game/TownDecor (Tools/town_decor_copy.py), which ships
	UStaticMesh* LoadDecor(const TCHAR* FullPath)
	{
		FString P(FullPath);
		P = P.Left(P.Find(TEXT(".")) >= 0 ? P.Find(TEXT(".")) : P.Len());
		if (P.StartsWith(TEXT("/Game/Filler/")))
			if (UStaticMesh* M = LoadMesh(P.Replace(TEXT("/Game/Filler/"), TEXT("/Game/TownDecor/")))) return M;
		return LoadMesh(P);
	}
}

ATownLabTown::ATownLabTown()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);   // (its meshes are static: a movable root refused them, "cannot attach", 2026-11-14; the lantern lights are movable children, which is allowed)
	SetRootComponent(Root);
}

const JeebTownLab::FSite& ATownLabTown::Site() const { return bRealSite ? JeebTownLab::RealSite() : JeebTownLab::LabSite(); }

FString ATownLabTown::OldMeshName(const FString& MeshName)
{
	for (const FLabSwap& S : ModelSwaps)
		if (FPaths::GetBaseFilename(S.Mesh) == MeshName) return TEXT("SM_") + AssetName(S.Model);
	return MeshName;
}

void ATownLabTown::BeginPlay()
{
	Super::BeginPlay();
	if (!GetActorLocation().IsNearlyZero()) SetActorLocation(FVector::ZeroVector);   // (spawned at the origin; a static root must not be moved)
	const bool bServer = IsNetMode(NM_DedicatedServer);
	if (bServer) SetActorTickEnabled(false);
	if (bRealSite ? !JeebTownLab::RealTown() : !JeebTownLab::NewOn()) return;
	const double T0 = FPlatformTime::Seconds();
	BuildGround();
	const double T1 = FPlatformTime::Seconds();
	BuildBuildings();
	BuildPlazaOak();
	if (!JeebOakHill::TestOff(TEXT("NoOakMeshes")))
		PlaceBoulders(this, JeebOakHill::PosX, JeebOakHill::PosY + Site().OffY, bRealSite ? &Site() : nullptr);
	UE_LOG(LogTemp, Log, TEXT("%s built (rebuilt town%s): ground %.0f ms, the rest %.0f ms, %d drawn sets, hash=%08x"), Tag(), bRealSite ? TEXT(", the real Oak Hill") : TEXT(", TownLab"),
		(T1 - T0) * 1000.0, (FPlatformTime::Seconds() - T1) * 1000.0, DrawSets, Hash);
}

// The windows follow the sun (the old town's AOakHillLife::UpdateGlow: GlowAtNoon 0.9 and GlowAtDusk 1.35), the lanterns brighten
void ATownLabTown::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float D = ADayNight::Dusk;
	if (FMath::Abs(D - LastDusk) < 0.005f) return;
	LastDusk = D;
	if (GroundMid) GroundMid->SetScalarParameterValue(TEXT("SkyFill"), GroundFill.GetValueOnGameThread() * D);
	const float F = FMath::Lerp(0.9f, 1.35f, D);
	for (int32 i = 0; i < GlowMids.Num(); ++i)
	{
		if (GlowBase[i] > 0.f) GlowMids[i]->SetScalarParameterValue(TEXT("GlowStrength"), GlowBase[i] * F);
		GlowMids[i]->SetScalarParameterValue(TEXT("Night"), D);
	}
	for (UPointLightComponent* L : LanternLights) if (L) L->SetIntensity(70.f * FMath::Lerp(0.15f, 1.f, D));   // (lit with the windows at dusk)
}

// The ground: a 1 m grid over the whole site (the town's streets are 3.5 m wide: a 2 m grid blurred their edges). Each of its four
// edges takes the straight line between the points of the ground beyond it, so there is no step or gap: the world's 8 m mesh, or,
// where the edge runs inside the original square (the real town's west part), the square's own 2 m mesh (GrassField).
// Vertex colours tell M_TL_Ground what each spot is: R the street (1 down its middle, about 0.4 at its edge, 0 a metre beyond),
// G worn earth (doorways, drip lines, the street's fringe), B how close a wall is (moss and damp at the foot of walls), A unused.
// At the real site the cells on river banks, mud or rock (JeebGround's mask) go in a second section drawn with the world's ground
// material and its own mask colours, so the banks and the river bed look as they always did.
void ATownLabTown::BuildGround()
{
	using namespace JeebTownLab;
	const FSite& S = Site();
	const float Step = 100.f;
	const int32 NI = FMath::RoundToInt((S.X1 - S.X0) / Step), NJ = FMath::RoundToInt((S.Y1 - S.Y0) / Step);
	// (the spacing of the ground beyond a point just outside an edge: the square's 2 m inside it, else the world's 8 m)
	auto BeyondStep = [](float X, float Y) { return FMath::Abs(X) < JeebTerrain::GroundHalf && FMath::Abs(Y) < JeebTerrain::GroundHalf ? JeebTerrain::GroundStep : JeebWorld::WorldStep; };
	auto EdgeLerp = [](float X, float Y, bool bAlongY, float Coarse)
	{
		const float V = bAlongY ? Y : X;
		const float V0 = FMath::FloorToFloat(V / Coarse) * Coarse, T = (V - V0) / Coarse;   // (both grids are aligned to 0: MinX, MinY and -16000 are multiples of 8 m)
		if (T < 1.e-4f) return JeebTerrain::HeightAt(X, Y);
		return bAlongY ? FMath::Lerp(JeebTerrain::HeightAt(X, V0), JeebTerrain::HeightAt(X, V0 + Coarse), T)
		               : FMath::Lerp(JeebTerrain::HeightAt(V0, Y), JeebTerrain::HeightAt(V0 + Coarse, Y), T);
	};
	const bool bServer = IsNetMode(NM_DedicatedServer);
	const bool bBanks = bRealSite && !bServer;
	TArray<FVector> V, N; TArray<FVector2D> UV; TArray<FLinearColor> Col, BankCol; TArray<float> BankAmt;
	const int32 W = NJ + 1;
	// (the ground's heights once, with a ring of cells beyond, so each vertex and its normal cost one HeightAt instead of five: the
	// build took 1.2-1.7 s, 2026-11-14)
	const int32 HW = NJ + 3;
	TArray<float> H; H.SetNumUninitialized((NI + 3) * HW);
	for (int32 i = -1; i <= NI + 1; ++i)
		for (int32 j = -1; j <= NJ + 1; ++j)
			H[(i + 1) * HW + (j + 1)] = JeebTerrain::HeightAt(S.X0 + i * Step, S.Y0 + j * Step);
	auto Ht = [&](int32 i, int32 j) { return H[(i + 1) * HW + (j + 1)]; };
	V.Reserve((NI + 1) * W); N.Reserve((NI + 1) * W); UV.Reserve((NI + 1) * W); Col.Reserve((NI + 1) * W);
	for (int32 i = 0; i <= NI; ++i)
		for (int32 j = 0; j <= NJ; ++j)
		{
			const float X = S.X0 + i * Step, Y = S.Y0 + j * Step;
			float Z;
			if (i == 0) Z = EdgeLerp(X, Y, true, BeyondStep(X - 1.f, Y));
			else if (i == NI) Z = EdgeLerp(X, Y, true, BeyondStep(X + 1.f, Y));
			else if (j == 0) Z = EdgeLerp(X, Y, false, BeyondStep(X, Y - 1.f));
			else if (j == NJ) Z = EdgeLerp(X, Y, false, BeyondStep(X, Y + 1.f));
			else Z = Ht(i, j);
			V.Add(FVector(X, Y, Z));
			HashIn(Hash, Z);
			const float DX = (Ht(i + 1, j) - Ht(i - 1, j)) / (2.f * Step);
			const float DY = (Ht(i, j + 1) - Ht(i, j - 1)) / (2.f * Step);
			N.Add(FVector(-DX, -DY, 1.f).GetSafeNormal());
			UV.Add(FVector2D(X / 1000.f, Y / 1000.f));
			if (bServer) { Col.Add(FLinearColor::Black); continue; }
			const float Clear = RoadClearance(S, X, Y);
			const float Street = FMath::Clamp((120.f - Clear) / 295.f, 0.f, 1.f);
			const FVector2D O = ToPlan(S, X, Y);
			float Worn = JeebGround::BuildingWear(O.X, O.Y, Clear);
			Worn = FMath::Max(Worn, 0.55f * (1.f - JeebWorld::Smooth(-20.f, 140.f, Clear)));   // (a fringe of packed earth along each street)
			const float Wall = 1.f - JeebWorld::Smooth(0.f, 260.f, JeebOakHill::BuildingDistance(O.X, O.Y));
			Col.Add(FLinearColor(Street, FMath::Clamp(Worn, 0.f, 1.f), Wall, 0.f));
			if (bBanks)
			{
				const JeebGround::FMask M = JeebGround::At(X, Y, Z, JeebGround::Upness(X, Y));
				BankCol.Add(M.Color());
				BankAmt.Add(FMath::Max3(M.Rock, M.Mud, M.Sand));
			}
		}
	TArray<int32> Tri, BankTri;
	auto Quad = [&](TArray<int32>& Out, int32 A, int32 B, int32 C, int32 D, bool bFlip)
	{
		if (!bFlip) Out.Append({ A, C, B, B, C, D });
		else Out.Append({ A, B, C, B, D, C });
	};
	bool bFlip = false;
	{
		// (wound to face up as Unreal counts it: the cross product points DOWN; see AOakHillTown::BuildGround)
		const FVector Fn = FVector::CrossProduct(V[1] - V[0], V[W] - V[0]);   // (A, C, B of the first cell)
		bFlip = Fn.Z > 0.f;
	}
	int32 BankCells = 0;
	for (int32 i = 0; i < NI; ++i)
		for (int32 j = 0; j < NJ; ++j)
		{
			const int32 A = i * W + j, B = A + W, C = A + 1, D = B + 1;
			const bool bBank = bBanks && FMath::Max(FMath::Max(BankAmt[A], BankAmt[B]), FMath::Max(BankAmt[C], BankAmt[D])) > 0.03f;
			Quad(bBank ? BankTri : Tri, A, B, C, D, bFlip);
			BankCells += bBank ? 1 : 0;
		}
	Ground = NewObject<UProceduralMeshComponent>(this, TEXT("TownGround"));
	Ground->SetupAttachment(GetRootComponent());
	Ground->bUseAsyncCooking = false;   // (the server must have it solid before anyone walks on it)
	Ground->bUseComplexAsSimpleCollision = true;
	Ground->RegisterComponent();
	Ground->CreateMeshSection_LinearColor(0, V, Tri, N, UV, Col, TArray<FProcMeshTangent>(), true);
	if (BankTri.Num()) Ground->CreateMeshSection_LinearColor(1, V, BankTri, N, UV, BankCol, TArray<FProcMeshTangent>(), true);
	Ground->SetCollisionProfileName(TEXT("BlockAll"));
	Ground->SetCastShadow(false);   // (flat ground shades nothing worth a shadow-map draw; it still receives shadows)
	if (bServer) { Ground->SetVisibility(false); return; }
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TownLab/M_TL_Ground.M_TL_Ground"));
	if (!Mat) { UE_LOG(LogTemp, Warning, TEXT("%s missing /Game/TownLab/M_TL_Ground (run Tools/BuildTownLab.ps1): M_Ground instead"), Tag()); Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Ground.M_Ground")); }
	if (Mat) { GroundMid = UMaterialInstanceDynamic::Create(Mat, this); Ground->SetMaterial(0, GroundMid); }
	if (BankTri.Num()) if (UMaterialInterface* WorldMat = AWorldTerrain::SharedGround(GetWorld())) Ground->SetMaterial(1, WorldMat);
	++DrawSets;
	UE_LOG(LogTemp, Log, TEXT("%s new ground %d x %d cells (%d on banks, the world's ground), %d triangles"), Tag(), NI, NJ, BankCells, (Tri.Num() + BankTri.Num()) / 3);
}

// One plain component per building, its solid shape, its decor; the stones and shrubs at the foundations
void ATownLabTown::BuildBuildings()
{
	using namespace JeebOakHill;
	const JeebTownLab::FSite& St = Site();
	const bool bServer = IsNetMode(NM_DedicatedServer);
	const bool bNoMeshes = TestOff(TEXT("NoOakMeshes"));
	TSet<FString> Sets;
	TMap<FString, UInstancedStaticMeshComponent*> Solids;
	TMap<FString, UInstancedStaticMeshComponent*> DecorSets;
	TMap<UMaterialInterface*, int32> GlowOf;
	int32 DecorPieces = 0, DecorLights = 0, DecorPlain = 0, DecorMissing = 0;
	TArray<JeebDecor::FFootprint> Foundations;
	int32 Placed = 0, Rebuilt = 0;
	int32 PlotIndex = -1;
	for (const FPlot& P : Plots())
	{
		++PlotIndex;
		if (P.Kind != EPlot::Building || !P.Model) continue;
		const FModel* Mdl = FindModel(P.Model);
		const FLabPick Pick = LabAsset(PlotIndex, P.Model);
		const bool bSwap = !Pick.Mesh.IsEmpty() && LoadMesh(Pick.Mesh);
		const float Scale = bSwap && Pick.Scale > 0.f ? Pick.Scale : Mdl ? Mdl->Scale : 1.f;
		const float ScaleZ = bSwap && Pick.Scale > 0.f ? Pick.Scale : Mdl && Mdl->ScaleZ > 0.f ? Mdl->ScaleZ : Scale;
		if (bSwap && Pick.Scale > 0.f && bRealSite) UE_LOG(LogTemp, Warning, TEXT("JEEBOAK %s drawn at its own scale %.2f: its shop counter, grass footprint and chimney are measured on the old model's"), P.Model, Pick.Scale);
		const FVector2D C = FromPx(P.Px, P.Py) + FVector2D(0.f, St.OffY);
		const float Yaw = BuildingYaw(P, Mdl) + (bSwap ? Pick.YawAdd : 0.f);
		const FVector At(C.X, C.Y, JeebTerrain::HeightAt(C.X, C.Y) + (bSwap ? Pick.Lift : 0.f));
		const FString Asset = AssetName(P.Model);
		FString Path = FString::Printf(TEXT("/Game/Buildings/SM_%s"), *Asset), ColPath = Path + TEXT("_Col");
		if (bSwap) { Path = Pick.Mesh; ColPath = LoadMesh(Pick.Mesh + TEXT("_Col")) ? Pick.Mesh + TEXT("_Col") : ColPath; ++Rebuilt; }
		UE_LOG(LogTemp, Log, TEXT("%s PLOT %d %s (%s) -> %s at %.0f %.0f %.0f yaw %.0f"), Tag(), PlotIndex, P.Name, P.Model, *FPaths::GetBaseFilename(Path), At.X, At.Y, At.Z, Yaw);
		const FTransform Xf(FRotator(0.f, Yaw, 0.f), At, FVector(Scale, Scale, ScaleZ));
		if (!bServer && !bNoMeshes)
			if (UStaticMesh* Mesh = LoadMesh(Path))
			{
				// One plain component per building: Nanite draws every Nanite mesh in one pass whatever the count, so instancing buys
				// nothing here, and the town's /Game/Buildings materials aren't flagged for instanced meshes (they fell back to grey).
				UStaticMeshComponent* B = NewObject<UStaticMeshComponent>(this);
				B->SetStaticMesh(Mesh);
				B->SetupAttachment(GetRootComponent());
				B->SetMobility(EComponentMobility::Static);
				B->SetWorldTransform(Xf);
				B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				// (when this building's windows come on at dusk: M_Building's HouseOffset, the same on every machine from where it stands)
				B->SetCustomPrimitiveDataFloat(0, Hash01(uint32(FMath::RoundToInt(At.X * 0.1f)) * 83492791u ^ uint32(FMath::RoundToInt(At.Y * 0.1f)) * 2654435761u));
				for (int32 i = 0; i < B->GetNumMaterials(); ++i)
					if (UMaterialInterface* M = B->GetMaterial(i))
					{
						int32* Got = GlowOf.Find(M);
						if (!Got)
						{
							UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(M, this);
							GlowMids.Add(Mid);
							GlowBase.Add(Mid->K2_GetScalarParameterValue(TEXT("GlowStrength")));
							Got = &GlowOf.Add(M, GlowMids.Num() - 1);
						}
						B->SetMaterial(i, GlowMids[*Got]);
					}
				B->RegisterComponent();
				Sets.Add(Path);
				++DrawSets;
				// The rebuilt models' own decorations (townbuild: ivy, moss, stones, lanterns), one instanced set per kind for the town
				// (any rebuilt model SM_TL_<Name> finds its decor under "TL_<Name>"; build-c 2026-11-14: not only the house and store)
				const FString MeshName = FPaths::GetBaseFilename(Path);
				const FString Kind = MeshName.StartsWith(TEXT("SM_TL_")) ? MeshName.Mid(3) : FString();
				if (const JeebTownLabDecor::FBuildingDecor* D = Kind.IsEmpty() ? nullptr : JeebTownLabDecor::For(*Kind))
					for (int32 i = 0; i < D->Num; ++i)
					{
						const JeebTownLabDecor::FDecor& Pc = D->Pieces[i];
						UStaticMesh* PM = LoadDecor(Pc.Mesh);
						if (!PM) { if (DecorMissing++ < 10) UE_LOG(LogTemp, Warning, TEXT("%s missing decor %s"), Tag(), Pc.Mesh); continue; }
						const FString Key = FString::Printf(TEXT("%s|%d"), *PM->GetPathName(), Pc.bCastShadow ? 1 : 0);
						const FTransform PieceXf = JeebTownLabDecor::Place(Xf, Pc);
						// An instanced set only when every material of the piece is flagged for instanced meshes (else it draws as the
						// engine's grey default); the others go in as plain components
						if (!InstancingOk(PM))
						{
							UStaticMeshComponent* Sm = NewObject<UStaticMeshComponent>(this);
							Sm->SetStaticMesh(PM);
							Sm->SetupAttachment(GetRootComponent());
							Sm->SetMobility(EComponentMobility::Static);
							Sm->SetWorldTransform(PieceXf);
							Sm->SetCastShadow(Pc.bCastShadow);
							Sm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
							Sm->SetCachedMaxDrawDistance(9000.f);
							Sm->LDMaxDrawDistance = 9000.f;
							Sm->RegisterComponent();
							++DrawSets; ++DecorPlain;
						}
						else
						{
							UInstancedStaticMeshComponent*& Ism = DecorSets.FindOrAdd(Key);
							if (!Ism)
							{
								Ism = NewObject<UInstancedStaticMeshComponent>(this);
								Ism->SetStaticMesh(PM);
								Ism->SetupAttachment(GetRootComponent());
								Ism->SetMobility(EComponentMobility::Static);
								Ism->SetCastShadow(Pc.bCastShadow);
								Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
								Ism->SetCullDistances(0, 9000);
								Ism->RegisterComponent();
								++DrawSets;
							}
							Ism->AddInstance(PieceXf, true);
						}
						++DecorPieces;
						// a real (shadowless) light in every lantern, sized to the lantern (townbuild: 70 lm, 3 m, warm), dimmed by day in Tick
						if (Pc.bLight)
						{
							UPointLightComponent* L = NewObject<UPointLightComponent>(this);
							L->SetupAttachment(GetRootComponent());
							L->SetWorldLocation(PieceXf.GetLocation() + FVector(0.f, 0.f, 25.f));
							L->SetIntensityUnits(ELightUnits::Lumens);
							L->SetIntensity(70.f);
							L->SetAttenuationRadius(300.f);
							L->SetLightColor(FLinearColor(1.f, 0.62f, 0.3f));
							L->SetCastShadows(false);
							L->RegisterComponent();
							LanternLights.Add(L);
							++DecorLights;
						}
					}
			}
		// Solid shape, on every machine (the server decides where he can walk): the model's own reduced copy, never drawn (steps,
		// porches and awnings walkable); without one the old box over the foundation walls (AOakHillTown's fallback)
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
		if (Solid) { Solid->AddInstance(Xf, true); HashIn(Hash, 1.f); }
		else if (Mdl && Mdl->WX1 > Mdl->WX0)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s no collision model %s: a box over the walls instead"), Tag(), *ColPath);
			const FVector WLo = FVector(Mdl->WX0, -Mdl->WY1, 0.f) * Scale, WHi = FVector(Mdl->WX1, -Mdl->WY0, 0.f) * Scale;
			const FVector Ext((WHi.X - WLo.X) * 0.5f, (WHi.Y - WLo.Y) * 0.5f, 300.f);
			UBoxComponent* Walls = NewObject<UBoxComponent>(this);
			Walls->SetupAttachment(GetRootComponent());
			Walls->SetBoxExtent(Ext);
			Walls->SetWorldLocationAndRotation(At + FRotator(0.f, Yaw, 0.f).RotateVector((WLo + WHi) * 0.5f) + FVector(0.f, 0.f, Ext.Z), FRotator(0.f, Yaw, 0.f));
			Walls->SetCollisionProfileName(TEXT("BlockAll"));
			Walls->SetMobility(EComponentMobility::Static);
			Walls->SetCanEverAffectNavigation(false);
			Walls->RegisterComponent();
			HashIn(Hash, Ext.X); HashIn(Hash, Ext.Y);
		}
		HashIn(Hash, At.X); HashIn(Hash, At.Y); HashIn(Hash, At.Z); HashIn(Hash, Yaw); HashIn(Hash, Scale); HashIn(Hash, ScaleZ);
		++Placed;
		// (the stones and shrubs at the foundations, as the old town had them: not the open stalls or the well)
		if (Mdl && Mdl->WX1 > Mdl->WX0 && !IsStall(P.Model) && FCString::Strcmp(P.Model, TEXT("OakHillWell")) != 0)
		{
			FVector WLo = FVector(Mdl->WX0, -Mdl->WY1, 0.f) * Scale, WHi = FVector(Mdl->WX1, -Mdl->WY0, 0.f) * Scale;
			// a swapped model: its own walls, from its solid shape (else the whole model), a little inside the eaves
			if (bSwap)
				if (UStaticMesh* Shape = LoadMesh(ColPath) ? LoadMesh(ColPath) : LoadMesh(Path))
				{
					const FBox Bx = Shape->GetBoundingBox();
					const FVector BC = Bx.GetCenter(), H = Bx.GetExtent() * 0.92f;
					WLo = FVector(BC.X - H.X, BC.Y - H.Y, 0.f) * Scale; WHi = FVector(BC.X + H.X, BC.Y + H.Y, 0.f) * Scale;
				}
			const FVector Mid((WLo.X + WHi.X) * 0.5f, (WLo.Y + WHi.Y) * 0.5f, 0.f);
			const FVector WallMid = At + FRotator(0.f, Yaw, 0.f).RotateVector(Mid);
			JeebDecor::FFootprint Fp;
			Fp.Centre = FVector2D(WallMid.X, WallMid.Y); Fp.Yaw = Yaw; Fp.Half = FVector2D((WHi.X - WLo.X) * 0.5f, (WHi.Y - WLo.Y) * 0.5f); Fp.DoorYaw = Yaw + Mdl->DoorYaw;
			Foundations.Add(Fp);
		}
	}
	if (!bServer && !bNoMeshes)
	{
		TArray<UInstancedStaticMeshComponent*> Before; GetComponents(Before);
		JeebDecor::DressFoundations(this, Foundations, 8128);
		TArray<UInstancedStaticMeshComponent*> After; GetComponents(After);
		DrawSets += After.Num() - Before.Num();
	}
	UE_LOG(LogTemp, Log, TEXT("%s new town: %d buildings of %d models (%d rebuilt models), %d glow materials, decor %d pieces in %d sets plus %d single (materials not flagged for instancing), %d missing, %d lantern lights"),
		Tag(), Placed, Sets.Num(), Rebuilt, GlowMids.Num(), DecorPieces, DecorSets.Num(), DecorPlain, DecorMissing, DecorLights);
}

// The plaza's big oak, as the old town's (OakHillTown.cpp): the Meshy oak, its dappled shadow stand-in, the warm light under the
// crown; the trunk's capsule on every machine. Without the Meshy oak, AOakHillTown::PlantTrees puts the woods' own oak there.
void ATownLabTown::BuildPlazaOak()
{
	using namespace JeebOakHill;
	if (TestOff(TEXT("NoOakTrees"))) return;
	UStaticMesh* Mesh = TestOff(TEXT("NoPlazaOak")) ? nullptr : LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Trees/SM_PlazaOak.SM_PlazaOak"));
	if (!Mesh) return;
	const FVector2D C = FromPx(255.f, 410.f) + FVector2D(0.f, Site().OffY);
	const FVector At(C.X, C.Y, JeebTerrain::HeightAt(C.X, C.Y));
	if (!IsNetMode(NM_DedicatedServer) && !TestOff(TEXT("NoOakMeshes")))
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
		float Fill = 12000.f;
#if !UE_BUILD_SHIPPING
		FParse::Value(FCommandLine::Get(), TEXT("OakFill="), Fill);   // (a test switch)
#endif
		if (Fill > 0.f)
		{
			USpotLightComponent* L = NewObject<USpotLightComponent>(this);
			L->SetupAttachment(GetRootComponent());
			L->SetWorldLocationAndRotation(At + FVector(0.f, 0.f, 650.f), FRotator(-90.f, 0.f, 0.f));
			L->SetMobility(EComponentMobility::Movable);
			L->SetIntensity(Fill);
			L->SetLightColor(FLinearColor(1.0f, 0.70f, 0.40f));
			L->SetAttenuationRadius(650.f * 1.9f);
			L->SetInnerConeAngle(30.f);
			L->SetOuterConeAngle(54.f);
			L->SetCastShadows(false);
			L->RegisterComponent();
		}
	}
	UCapsuleComponent* Trunk = NewObject<UCapsuleComponent>(this);
	Trunk->SetupAttachment(GetRootComponent());
	Trunk->SetCapsuleSize(110.f, 150.f);
	Trunk->SetWorldLocation(At + FVector(0.f, 0.f, 100.f));
	Trunk->SetCollisionProfileName(TEXT("BlockAll"));
	Trunk->SetMobility(EComponentMobility::Static);
	Trunk->SetCanEverAffectNavigation(false);
	Trunk->RegisterComponent();
	HashIn(Hash, At.X); HashIn(Hash, At.Y); HashIn(Hash, At.Z);
}

// Trees in the gaps between the plots in the TownLab map's rebuilt town (the real town's own set: AOakHillTown::PlantTrees)
void ATownLabTown::PlantTrees(AForest* Forest)
{
	using namespace JeebOakHill;
	if (!Forest || !JeebTownLab::NewOn()) return;
	// The very same trees as the real town (AOakHillTown::PlantTrees: same seed, same draws, same tests made at the real town's spot,
	// river and roads included), so both towns are compared with the same trees and the same shade on the grass
	FRandomStream R(5150);
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
		const float OX = PosX + N * 100.f, OY = PosY + E * 100.f;
		float Half;
		if (JeebWorld::RiverDistance(OX, OY, Half) < Half + 600.f) continue;
		if (JeebTerrain::PathClearance(OX, OY) < 450.f) continue;
		bool bClear = true;
		for (const FPlot& P : Plots()) if (PlotSD(P, E, N) < (P.Kind == EPlot::Building ? 6.f : 3.f)) { bClear = false; break; }
		if (!bClear) continue;
		for (const FVector2D& G : Got) if (FVector2D::DistSquared(G, FVector2D(OX, OY)) < 700.f * 700.f) { bClear = false; break; }
		if (!bClear) continue;
		Got.Add(FVector2D(OX, OY));
		const float X = JeebTownLab::PlazaX + N * 100.f, Y = JeebTownLab::PlazaY + E * 100.f;
		const ETreeKind K = (FromPlaza > 50.f && Kind < 0.25f) ? ETreeKind::Pine : ETreeKind::Oak;
		Forest->AddTree(K, FVector(X, Y, JeebTerrain::HeightAt(X, Y) - 20.f * S), S, R);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB new town trees %d (the real town's set)"), Got.Num());
}

// Rounded boulders, half sunk, in small groups round the town's edge (the same spots relative to each plaza), one instanced set,
// casting shadows (they are big). Solid, so they are built on a dedicated server too (hidden there): the server must stop a player
// at the same stones the player sees. (JeebTownLab::RoadClearance(X, Y): the TownLab map's rebuilt streets; 1e6 elsewhere)
void ATownLabTown::PlaceBoulders(AActor* Owner, float PlazaX, float PlazaY, const JeebTownLab::FSite* Site)
{
	if (!Owner) return;
	const bool bServer = Owner->IsNetMode(NM_DedicatedServer);
	UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/TownLab/SM_TL_Boulder.SM_TL_Boulder"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	const bool bMeshy = Ball != nullptr;
	if (!Ball) Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Stone = bServer ? nullptr : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Stone.M_Stone"));
	if (!Ball) return;
	UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(Owner);
	I->SetStaticMesh(Ball);
	if (Stone && !bMeshy) I->SetMaterial(0, Stone);
	I->SetupAttachment(Owner->GetRootComponent());
	I->SetMobility(EComponentMobility::Static);
	I->SetCollisionProfileName(TEXT("BlockAll"));
	I->SetCullDistances(0, 30000);
	if (bServer) { I->SetVisibility(false); I->SetHiddenInGame(true); }
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
			if (JeebWorld::InWater(X, Y) || JeebTerrain::PathClearance(X, Y) < 300.f || JeebTownLab::RoadClearance(X, Y) < 300.f) continue;
			if (Site)
			{
				// (only inside the town's own ground, never on its streets, plots or the river's banks: at the real town the ring's west
				// side is the river and Blackrock's side of it)
				if (!JeebTownLab::InSite(*Site, X, Y, -800.f) || JeebTownLab::RoadClearance(*Site, X, Y) < 300.f) continue;
				const FVector2D O = JeebTownLab::ToPlan(*Site, X, Y);
				const float Pe = JeebOakHill::EastM(O.Y), Pn = JeebOakHill::NorthM(O.X);
				bool bOnPlot = false;
				for (const JeebOakHill::FPlot& P : JeebOakHill::Plots()) if (JeebOakHill::PlotSD(P, Pe, Pn) < 4.f) { bOnPlot = true; break; }
				float Half;
				if (bOnPlot || JeebWorld::RiverDistance(O.X, O.Y, Half) < Half + 900.f) continue;
			}
			I->AddInstance(FTransform(Rot, FVector(X, Y, JeebTerrain::HeightAt(X, Y) - 0.18f * Sz * 100.f), FVector(Sx, Sy, Sz)), true);
			++Count;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB %d boulders round the plaza at %.0f %.0f (%s)"), Count, PlazaX, PlazaY, bMeshy ? TEXT("Meshy boulder") : TEXT("rounded engine spheres"));
}
