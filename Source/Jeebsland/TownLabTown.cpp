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
#include "Materials/Material.h"
#include "Components/PointLightComponent.h"
#include "TownLabDecor.h"
#include "DayNight.h"

namespace
{
	TAutoConsoleVariable<float> GroundFill(TEXT("jeeb.TownLab.GroundFill"), 0.15f, TEXT("TownLab: light added to the rebuilt town's ground at the low sun (times ADayNight::Dusk), so it stays as bright as the grass blades"));
	FString AssetName(const TCHAR* Name) { return FString(Name) + (FCString::Strcmp(Name, TEXT("OakHillWell")) == 0 ? TEXT("_150k") : TEXT("_300k")); }
	// WHICH MODEL EACH BUILDING OF THE NEW TOWN USES. Any of the town's models can be swapped here for a new one (a Fab pack's, a
	// Meshy remake) without touching anything else: Mesh is the new static mesh's path (its solid shape is Mesh + "_Col" when that
	// exists, else the old model's), Scale replaces OakHill.h's FModel Scale when above 0, YawAdd turns it (its door must face +Y as
	// the old models' do, or say so here), Lift raises it in cm. A missing mesh falls back to the old model, so a row can wait for
	// its asset. PlotSwaps does the same for single plots (the plot's index in JeebOakHill::Plots(), from the JEEBLAB PLOT lines).
	struct FLabSwap { const TCHAR* Model; const TCHAR* Mesh; float Scale; float YawAdd; float Lift; };
	const FLabSwap ModelSwaps[] = {
		{ TEXT("OakHillHouse"), TEXT("/Game/TownLab/Buildings/SM_TL_House"), 0.f, 0.f, 0.f },   // townbuild's rebuilt house
		{ TEXT("OakHillStore"), TEXT("/Game/TownLab/Buildings/SM_TL_Store"), 0.f, 0.f, 0.f },   // townbuild's rebuilt store
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
	bool InstancingOk(UStaticMesh* Mesh)
	{
		for (const FStaticMaterial& M : Mesh->GetStaticMaterials())
			if (M.MaterialInterface && M.MaterialInterface->GetMaterial() && !M.MaterialInterface->GetMaterial()->bUsedWithInstancedStaticMeshes) return false;
		return true;
	}
	UStaticMesh* LoadMesh(const FString& Path)
	{
		const FString Name = FPaths::GetBaseFilename(Path);
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *Path, *Name));
	}
}

ATownLabTown::ATownLabTown()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
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

// The windows follow the sun as the real town's do (AOakHillLife::UpdateGlow: its GlowAtNoon 0.9 and GlowAtDusk 1.35)
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
	if (Mat) { GroundMid = UMaterialInstanceDynamic::Create(Mat, this); Ground->SetMaterial(0, GroundMid); }
	++DrawSets;
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB new ground %d x %d cells, %d triangles, lift %.0f cm"), NI, NJ, Tri.Num() / 3, Lift());
}

// Every copy of a model is one instance in that model's set: one draw per model (and per material section), however many copies
void ATownLabTown::BuildBuildings()
{
	using namespace JeebOakHill;
	const bool bServer = IsNetMode(NM_DedicatedServer);
	TMap<FString, UInstancedStaticMeshComponent*> Sets, Solids;
	TMap<FString, UInstancedStaticMeshComponent*> DecorSets;
	TMap<UMaterialInterface*, int32> GlowOf;
	int32 DecorPieces = 0, DecorLights = 0, LanternCount = 0, DecorPlain = 0;
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
		const FVector2D C = FromPx(P.Px, P.Py) + FVector2D(0.f, JeebTownLab::OffY);
		const float Yaw = BuildingYaw(P, Mdl) + (bSwap ? Pick.YawAdd : 0.f);
		const FVector At(C.X, C.Y, JeebTerrain::HeightAt(C.X, C.Y) + (bSwap ? Pick.Lift : 0.f));
		const FString Asset = AssetName(P.Model);
		FString Path = FString::Printf(TEXT("/Game/Buildings/SM_%s"), *Asset), ColPath = Path + TEXT("_Col");
		if (bSwap) { Path = Pick.Mesh; ColPath = LoadMesh(Pick.Mesh + TEXT("_Col")) ? Pick.Mesh + TEXT("_Col") : ColPath; ++Rebuilt; }
		UE_LOG(LogTemp, Log, TEXT("JEEBLAB PLOT %d %s -> %s at %.0f %.0f"), PlotIndex, P.Model, *FPaths::GetBaseFilename(Path), At.X, At.Y);
		const FTransform Xf(FRotator(0.f, Yaw, 0.f), At, FVector(Scale, Scale, ScaleZ));
		if (!bServer)
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
				Sets.FindOrAdd(Path);
				++DrawSets;
				// The rebuilt models' own decorations (townbuild: ivy, moss, stones, lanterns), one instanced set per kind for the town
				const FString Kind = Path.EndsWith(TEXT("SM_TL_House")) ? TEXT("TL_House") : Path.EndsWith(TEXT("SM_TL_Store")) ? TEXT("TL_Store") : FString();
				if (const JeebTownLabDecor::FBuildingDecor* D = Kind.IsEmpty() ? nullptr : JeebTownLabDecor::For(*Kind))
					for (int32 i = 0; i < D->Num; ++i)
					{
						const JeebTownLabDecor::FDecor& Pc = D->Pieces[i];
						const FString Key = FString::Printf(TEXT("%s|%d"), Pc.Mesh, Pc.bCastShadow ? 1 : 0);
						UStaticMesh* PM = LoadObject<UStaticMesh>(nullptr, Pc.Mesh);
						if (!PM) { UE_LOG(LogTemp, Warning, TEXT("JEEBLAB missing decor %s"), Pc.Mesh); continue; }
						const FTransform PieceXf = JeebTownLabDecor::Place(Xf, Pc);
						// An instanced set only when every material of the piece is flagged for instanced meshes (else it draws as the
						// engine's grey default): the Filler and townbuild materials aren't yet, so those go in as plain components
						if (!InstancingOk(PM))
						{
							UStaticMeshComponent* S = NewObject<UStaticMeshComponent>(this);
							S->SetStaticMesh(PM);
							S->SetupAttachment(GetRootComponent());
							S->SetMobility(EComponentMobility::Static);
							S->SetWorldTransform(PieceXf);
							S->SetCastShadow(Pc.bCastShadow);
							S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
							S->SetCachedMaxDrawDistance(9000.f);
							S->LDMaxDrawDistance = 9000.f;
							S->RegisterComponent();
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
						if (Pc.bLight && ++LanternCount > 0)
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
			FVector WLo = FVector(Mdl->WX0, -Mdl->WY1, 0.f) * Scale, WHi = FVector(Mdl->WX1, -Mdl->WY0, 0.f) * Scale;
			// a swapped model: its own walls, from its solid shape (else the whole model), a little inside the eaves
			if (bSwap)
				if (UStaticMesh* Shape = LoadMesh(ColPath) ? LoadMesh(ColPath) : LoadMesh(Path))
				{
					const FBox B = Shape->GetBoundingBox();
					const FVector BC = B.GetCenter(), H = B.GetExtent() * 0.92f;
					WLo = FVector(BC.X - H.X, BC.Y - H.Y, 0.f) * Scale; WHi = FVector(BC.X + H.X, BC.Y + H.Y, 0.f) * Scale;
					UE_LOG(LogTemp, Log, TEXT("JEEBLAB walls of %s: %.0f x %.0f cm (the old model said %.0f x %.0f)"), *FPaths::GetBaseFilename(Path), WHi.X - WLo.X, WHi.Y - WLo.Y, (Mdl->WX1 - Mdl->WX0) * Scale, (Mdl->WY1 - Mdl->WY0) * Scale);
				}
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
	UE_LOG(LogTemp, Log, TEXT("JEEBLAB new town: %d buildings of %d models (%d from the rebuilt house/store), %d glow materials, decor %d pieces in %d sets plus %d single (materials not flagged for instancing), %d lantern lights"), Placed, Sets.Num(), Rebuilt, GlowMids.Num(), DecorPieces, DecorSets.Num(), DecorPlain, DecorLights);
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
