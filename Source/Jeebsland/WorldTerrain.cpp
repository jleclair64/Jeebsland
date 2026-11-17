#include "WorldTerrain.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "GroundMask.h"
#include "WaterLook.h"
#include "DayNight.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	constexpr int32 ChunkCells = 40;   // cells per chunk side
	TAutoConsoleVariable<float> SkyFillVar(TEXT("jeeb.Ground.SkyFill"), 0.1f, TEXT("Light added to flat ground (M_Ground's grass) at the low sun, times ADayNight::Dusk: the grass blades are lit through and stay bright, flat ground would go dark where they stop (TownLab's WorldFill, 2026-11-14)"));
	// (2026-11-15, owner: "when it's dark and there's shade, the bottom layer on the ground is brighter than the grass and seems to glow".
	// The fill is emissive, which no shadow dims: among the blades it now gives way to a darker floor; Tools/grass_field.py)
	TAutoConsoleVariable<float> UnderFillVar(TEXT("jeeb.Ground.UnderFill"), 0.f, TEXT("How much of the low sun's fill (jeeb.Ground.SkyFill) the ground keeps among the grass blades: 0 none (2026-11-15), 1 all of it (the old look, which glowed in shade at dusk)"));
	TAutoConsoleVariable<float> FloorVar(TEXT("jeeb.Ground.Floor"), 0.9f, TEXT("The ground among the grass blades at the lowest sun, times this (1 at noon, eased in with ADayNight::Dusk): the meadow's shaded floor, a touch darker than the blades. 1 = unchanged"));
	TAutoConsoleVariable<float> FillFromVar(TEXT("jeeb.Ground.FillFrom"), 0.5f, TEXT("Share of the grass's reach (jeeb.Grass.PackFade) where the ground's low-sun fill and its usual brightness start to come back (all of it at the reach)"));
	// (2026-11-17, owner: the ground past the grass "looks brighter than the tree trunks and up close grass". Its multiplier there only, M_Ground and
	// M_TL_Ground's FarTint, grass_field.under_grass_floor; SkyFill's default also went 0.2 to 0.1, the low sun's glow on that same far ground)
	TAutoConsoleVariable<FString> FarTintVar(TEXT("jeeb.Ground.FarTint"), TEXT("0.68 0.72 0.62"), TEXT("The flat ground's colour past the grass blades, \"r g b\" multiplier (live; 1 1 1 the old look)"));
	TWeakObjectPtr<UMaterialInstanceDynamic> GSharedGround;
	TWeakObjectPtr<UWorld> GSharedWorld;
	struct FUnderGrass { float Fill = -1.f, From = 0.f, To = 0.f, Under = 0.f, Floor = 0.f; FString Far; };
	TMap<TWeakObjectPtr<UMaterialInstanceDynamic>, FUnderGrass> GUnderGrassSet;   // (what each ground copy was last given)
}

float AWorldTerrain::LowSunFill()
{
	return FMath::Max(0.f, SkyFillVar.GetValueOnGameThread()) * ADayNight::Dusk;
}

bool AWorldTerrain::SetUnderGrass(UMaterialInstanceDynamic* Mid, float Fill)
{
	if (!Mid) return false;
	static IConsoleVariable* Reach = IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.Grass.PackFade"));
	const float To = FMath::Max(600.f, Reach ? Reach->GetFloat() : 5000.f);
	const float From = To * FMath::Clamp(FillFromVar.GetValueOnGameThread(), 0.f, 0.98f);
	const float Under = FMath::Clamp(UnderFillVar.GetValueOnGameThread(), 0.f, 1.f);
	const float Floor = FMath::Lerp(1.f, FMath::Clamp(FloorVar.GetValueOnGameThread(), 0.2f, 1.5f), ADayNight::Dusk);
	const FString FarS = FarTintVar.GetValueOnGameThread();
	if (const FUnderGrass* W = GUnderGrassSet.Find(Mid))
		if (FMath::Abs(W->Fill - Fill) < 0.002f && FMath::Abs(W->From - From) < 1.f && FMath::Abs(W->To - To) < 1.f && FMath::Abs(W->Under - Under) < 0.002f
			&& FMath::Abs(W->Floor - Floor) < 0.002f && W->Far == FarS) return false;
	GUnderGrassSet.Add(Mid, FUnderGrass{ Fill, From, To, Under, Floor, FarS });
	{
		TArray<FString> P; FarS.ParseIntoArrayWS(P);
		FLinearColor C(0.68f, 0.72f, 0.62f);
		if (P.Num() >= 3) C = FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
		Mid->SetVectorParameterValue(TEXT("FarTint"), C);
	}
	Mid->SetScalarParameterValue(TEXT("SkyFill"), Fill);
	Mid->SetScalarParameterValue(TEXT("FillFrom"), From);
	Mid->SetScalarParameterValue(TEXT("FillTo"), To);
	Mid->SetScalarParameterValue(TEXT("UnderFill"), Under);
	Mid->SetScalarParameterValue(TEXT("Floor"), Floor);
	return true;
}

UMaterialInterface* AWorldTerrain::SharedGround(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer) return nullptr;
	if (GSharedGround.IsValid() && GSharedWorld.Get() == World) return GSharedGround.Get();
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Ground.M_Ground"));
	if (!Base) return nullptr;
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, World);
	SetUnderGrass(Mid, LowSunFill());
	GSharedGround = Mid; GSharedWorld = World;
	return Mid;
}

AWorldTerrain::AWorldTerrain()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;   // (the sun moves slowly: twice a second is plenty for the ground's fill)
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AWorldTerrain::BeginPlay()
{
	Super::BeginPlay();
	BuildLand();
	if (!IsNetMode(NM_DedicatedServer)) BuildWater();
	else SetActorTickEnabled(false);
}

void AWorldTerrain::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(SharedGround(GetWorld()));
	if (!Mid) return;
	SetUnderGrass(Mid, LowSunFill());   // (sets only what changed: the sun, the reach, jeeb.Ground.*)
}

void AWorldTerrain::BuildLand()
{
	using namespace JeebWorld;
	const float Step = WorldStep;
	const int32 NX = FMath::RoundToInt((MaxX - MinX) / Step), NY = FMath::RoundToInt((MaxY - MinY) / Step);
	const float Hole = JeebTerrain::GroundHalf;   // the original square has its own ground mesh (GrassField)

	// Every grid point's height once (one row and column of border so every normal has neighbours)
	const int32 GX = NX + 3, GY = NY + 3;
	TArray<float> H;
	H.SetNumUninitialized(GX * GY);
	auto PX = [&](int32 i) { return MinX + (i - 1) * Step; };
	auto PY = [&](int32 j) { return MinY + (j - 1) * Step; };
	for (int32 i = 0; i < GX; ++i)
		for (int32 j = 0; j < GY; ++j)
		{
			const float X = PX(i), Y = PY(j);
			float Z = JeebTerrain::HeightAt(X, Y);
			// just inside the original square's edge the big mesh tucks a little under the square's own (finer) ground, so the
			// two never fight over which is drawn
			if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) < Hole - 1.f) Z -= 15.f;
			H[i * GY + j] = Z;
		}
	auto At = [&](int32 i, int32 j) { return H[(i + 1) * GY + (j + 1)]; };   // grid point i, j (0 .. NX, 0 .. NY)

	UMaterialInterface* Mat = SharedGround(GetWorld());   // (the one ground material, shared with the square and Oak Hill: 2026-11-12; a dynamic copy for the low sun's fill)
	UMaterialInterface* Fallback = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	const bool bServer = IsNetMode(NM_DedicatedServer);

	for (int32 CI = 0; CI < NX; CI += ChunkCells)
		for (int32 CJ = 0; CJ < NY; CJ += ChunkCells)
		{
			const int32 I1 = FMath::Min(CI + ChunkCells, NX), J1 = FMath::Min(CJ + ChunkCells, NY);
			TArray<FVector> V; TArray<FVector> N; TArray<FLinearColor> Col; TArray<FVector2D> UV; TArray<int32> Tri;
			const int32 W = J1 - CJ + 1;
			for (int32 i = CI; i <= I1; ++i)
				for (int32 j = CJ; j <= J1; ++j)
				{
					const float X = MinX + i * Step, Y = MinY + j * Step, Z = At(i, j);
					V.Add(FVector(X, Y, Z));
					const FVector Nrm = FVector(At(i - 1, j) - At(i + 1, j), At(i, j - 1) - At(i, j + 1), 2.f * Step).GetSafeNormal();
					N.Add(Nrm);
					UV.Add(FVector2D(X / 1000.f, Y / 1000.f));
					if (bServer) { Col.Add(FLinearColor::White); continue; }
					// what the ground is here (rock, mud, sand, wear: GroundMask.h, the same function as the square's and Oak Hill's
					// ground, so they meet without a seam); M_Ground paints it. The slope over 16 m comes from this grid's neighbours.
					const float Up = JeebGround::Upness(At(i + 1, j), At(i - 1, j), At(i, j + 1), At(i, j - 1));
					Col.Add(JeebGround::At(X, Y, Z, Up).Color());
				}
			for (int32 i = CI; i < I1; ++i)
				for (int32 j = CJ; j < J1; ++j)
				{
					// skip the cells inside the original square (its own ground covers them)
					const float X0 = MinX + i * Step, Y0 = MinY + j * Step;
					if (X0 >= -Hole && X0 + Step <= Hole && Y0 >= -Hole && Y0 + Step <= Hole) continue;
				// and Oak Hill's own finer ground east of the square (AOakHillTown, 2026-11-11)
				if (JeebOakHill::Enabled() && JeebOakHill::InPatchCell(X0, Y0, Step)) continue;   // (the rebuilt town covers the same cells and the square's part of its box: JeebTownLab::RealSite)
				if (JeebTownLab::NewOn() && JeebTownLab::InPatchCell(X0, Y0, Step)) continue;   // (TownLab only: the rebuilt town's own 1 m ground)
					const int32 A = (i - CI) * W + (j - CJ), B = A + W, C = A + 1, D = B + 1;
					Tri.Append({ A, C, B, B, C, D });
				}
			if (Tri.Num() == 0) continue;
			// (wound to face up: checked against the normals so the ground is solid from above whichever way the engine counts)
			{
				const FVector Fn = FVector::CrossProduct(V[Tri[1]] - V[Tri[0]], V[Tri[2]] - V[Tri[0]]);
				// (face up as Unreal counts it, like GrassField: the cross product points down; see AOakHillTown::BuildGround)
				if (Fn.Z > 0.f) for (int32 t = 0; t + 2 < Tri.Num(); t += 3) Swap(Tri[t + 1], Tri[t + 2]);
			}
			UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
			PM->SetupAttachment(GetRootComponent());
			PM->bUseAsyncCooking = true;
			PM->RegisterComponent();
			PM->CreateMeshSection_LinearColor(0, V, Tri, N, UV, Col, TArray<FProcMeshTangent>(), true);
			// the underside too (no collision), so a camera that dips below a slope never sees through the world
			TArray<int32> Back;
			for (int32 t = 0; t + 2 < Tri.Num(); t += 3) Back.Append({ Tri[t], Tri[t + 2], Tri[t + 1] });
			PM->CreateMeshSection_LinearColor(1, V, Back, N, UV, Col, TArray<FProcMeshTangent>(), false);
			PM->SetCollisionProfileName(TEXT("BlockAll"));
			PM->ComponentTags.Add(FName(TEXT("JeebClimb")));   // (hills and rock: climbable where too steep to walk, KnightMovement.h)
			if (!bServer)
			{
				for (int32 s = 0; s < 2; ++s)
				{
					if (Mat) PM->SetMaterial(s, Mat);
					else if (UMaterialInstanceDynamic* M = PM->CreateDynamicMaterialInstance(s, Fallback)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 0.32f, 0.12f));
				}
			}
			else PM->SetVisibility(false);
			Chunks.Add(PM);
		}
}

void AWorldTerrain::BuildWater()
{
	using namespace JeebWorld;
	// (2026-11-13) M_Water on every sheet: see-through and tinted by depth, reflecting, rippling; the rivers carry their
	// downstream direction in the vertex colours so the ripples drift with them (WaterLook.h). The surfaces stay exactly at
	// SeaLevel and RiverLevel, with no collision (swimming reads those levels).
	auto Sheet = [&](const TArray<FVector>& V, const TArray<int32>& T, const TArray<FLinearColor>& Col)
	{
		UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
		PM->SetupAttachment(GetRootComponent());
		PM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PM->RegisterComponent();
		TArray<FVector> N; N.Init(FVector::UpVector, V.Num());
		TArray<int32> Both = T;
		for (int32 t = 0; t + 2 < T.Num(); t += 3) Both.Append({ T[t], T[t + 2], T[t + 1] });   // both windings: seen from either side
		PM->CreateMeshSection_LinearColor(0, V, Both, N, TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), false);
		JeebWaterLook::Apply(PM, 0, FLinearColor(0.06f, 0.20f, 0.30f));
		PM->SetCastShadow(false);
		PM->bAffectDistanceFieldLighting = false;
		Water.Add(PM);
	};
	// The ocean: one big sheet from just inside the coast out past the world's eastern edge (still water: white)
	{
		float WestMost = 1.0e9f;
		for (float X = MinX; X <= MaxX; X += 2000.f) WestMost = FMath::Min(WestMost, CoastY(X));
		const float Y0 = WestMost - 3000.f, Y1 = MaxY + 60000.f, X0 = MinX - 60000.f, X1 = MaxX + 60000.f;
		TArray<FLinearColor> White; White.Init(FLinearColor::White, 4);
		Sheet({ FVector(X0, Y0, SeaLevel), FVector(X1, Y0, SeaLevel), FVector(X0, Y1, SeaLevel), FVector(X1, Y1, SeaLevel) }, { 0, 2, 1, 1, 2, 3 }, White);
	}
	// Where rivers meet (the fork south of Blackrock: the main river's end and both branches' starts), their sheets overlap at
	// the same height; there the flow fades to still water on all of them, so the overlapping sheets draw exactly alike
	TArray<FVector2D> Joins;
	{
		const TArray<FRiver>& Rs = Rivers();
		for (int32 a = 0; a < Rs.Num(); ++a)
			for (const FVector2D& E : { Rs[a].Pts[0], Rs[a].Pts.Last() })
				for (int32 b = 0; b < Rs.Num(); ++b)
					if (b != a && (FVector2D::Distance(E, Rs[b].Pts[0]) < 100.f || FVector2D::Distance(E, Rs[b].Pts.Last()) < 100.f)) { Joins.AddUnique(E); break; }
	}
	// The rivers: a ribbon along each, a little wider than its channel; the points run downstream (WorldMap.h: spring to fork,
	// fork to the sea)
	for (const FRiver& Rv : Rivers())
	{
		TArray<FVector> V; TArray<int32> T; TArray<FLinearColor> Col;
		for (int32 i = 0; i < Rv.Pts.Num(); ++i)
		{
			const FVector2D A = Rv.Pts[FMath::Max(i - 1, 0)], B = Rv.Pts[FMath::Min(i + 1, Rv.Pts.Num() - 1)];
			const FVector2D Dir = (B - A).GetSafeNormal(), Side(-Dir.Y, Dir.X);
			const float W = Rv.HalfWidth + 250.f;
			const FVector2D L = Rv.Pts[i] + Side * W, R = Rv.Pts[i] - Side * W;
			V.Add(FVector(L.X, L.Y, RiverLevel)); V.Add(FVector(R.X, R.Y, RiverLevel));
			float NearJoin = 1.0e9f;
			for (const FVector2D& J : Joins) NearJoin = FMath::Min(NearJoin, float(FVector2D::Distance(Rv.Pts[i], J)));
			const float Flowing = FMath::SmoothStep(3500.f, 7000.f, NearJoin);
			const FLinearColor F = JeebWaterLook::FlowColor(Dir);
			const FLinearColor Here(FMath::Lerp(0.5f, F.R, Flowing), FMath::Lerp(0.5f, F.G, Flowing), 0.f, 1.f);
			Col.Add(Here); Col.Add(Here);
			if (i > 0) { const int32 k = (i - 1) * 2; T.Append({ k, k + 2, k + 1, k + 1, k + 2, k + 3 }); }
		}
		Sheet(V, T, Col);
	}
}
