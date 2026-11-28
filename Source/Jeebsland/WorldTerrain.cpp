#include "WorldTerrain.h"
namespace JeebRoadSlope { void Report(); }
#include "Terrain.h"
#include "Caves.h"
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
	TAutoConsoleVariable<float> SkyFillVar(TEXT("jeeb.Ground.SkyFill"), 0.f, TEXT("Light added to flat ground (M_Ground's grass) at the low sun, times ADayNight::Dusk: the grass blades are lit through and stay bright, flat ground would go dark where they stop (TownLab's WorldFill, 2026-11-14)"));
	// (2026-11-15, owner: "when it's dark and there's shade, the bottom layer on the ground is brighter than the grass and seems to glow".
	// The fill is emissive, which no shadow dims: among the blades it now gives way to a darker floor; Tools/grass_field.py)
	TAutoConsoleVariable<float> UnderFillVar(TEXT("jeeb.Ground.UnderFill"), 0.f, TEXT("How much of the low sun's fill (jeeb.Ground.SkyFill) the ground keeps among the grass blades: 0 none (2026-11-15), 1 all of it (the old look, which glowed in shade at dusk)"));
	TAutoConsoleVariable<float> FloorVar(TEXT("jeeb.Ground.Floor"), 0.9f, TEXT("The ground among the grass blades at the lowest sun, times this (1 at noon, eased in with ADayNight::Dusk): the meadow's shaded floor, a touch darker than the blades. 1 = unchanged"));
	TAutoConsoleVariable<float> FillFromVar(TEXT("jeeb.Ground.FillFrom"), 0.5f, TEXT("Share of the grass's reach (jeeb.Grass.PackFade) where the ground's low-sun fill and its usual brightness start to come back (all of it at the reach)"));
	// (2026-11-17, owner: the ground past the grass "looks brighter than the tree trunks and up close grass". Its multiplier there only, M_Ground and
	// M_TL_Ground's FarTint, grass_field.under_grass_floor; SkyFill's default also went 0.2 to 0.1, the low sun's glow on that same far ground)
	TAutoConsoleVariable<FString> FarTintVar(TEXT("jeeb.Ground.FarTint"), TEXT("0.6857 0.7197 0.6214"), TEXT("The flat ground's colour past the grass blades, \"r g b\" multiplier (live; 1 1 1 the old look)"));
	// (2026-11-26, the cliff lip view's frame cost: the sea cliffs' 2 m ground chunks as shadow casters; live)
	TAutoConsoleVariable<int32> CVarCliffWallShadows(TEXT("jeeb.Cliff.WallShadows"), 1, TEXT("1: the sea cliffs' ground chunks cast shadows (onto the sea, the beaches and the ledges); 0: they cast none (live)"));
	void CliffWallShadowsChanged()
	{
		static int32 Was = -1;
		const int32 Now = CVarCliffWallShadows.GetValueOnGameThread();
		if (Now == Was) return;
		Was = Now;
		for (TObjectIterator<UProceduralMeshComponent> It; It; ++It) if (It->ComponentHasTag(FName(TEXT("JeebCliffWall"))) && It->CastShadow != (Now != 0)) It->SetCastShadow(Now != 0);
	}
	FAutoConsoleVariableSink GCliffWallShadowsSink(FConsoleCommandDelegate::CreateStatic(&CliffWallShadowsChanged));
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
		FLinearColor C(0.6857f, 0.7197f, 0.6214f);
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

// The ground's underside (seen only by a camera that dips under a slope): no collision, no shadow
void AWorldTerrain::AddUnderside(const TArray<FVector>& V, const TArray<int32>& Back, const TArray<FVector>& N, const TArray<FVector2D>& UV, const TArray<FVector2D>& UV1, const TArray<FLinearColor>& Col, UMaterialInterface* Mat)
{
	UProceduralMeshComponent* U = NewObject<UProceduralMeshComponent>(this);
	U->SetupAttachment(GetRootComponent());
	U->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	U->SetCastShadow(false);
	U->bAffectDistanceFieldLighting = false;
	U->SetCanEverAffectNavigation(false);
	U->RegisterComponent();
	U->CreateMeshSection_LinearColor(0, V, Back, N, UV, UV1, TArray<FVector2D>(), TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), false);
	if (Mat) U->SetMaterial(0, Mat);
	Under.Add(U);
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
			TArray<FVector> V; TArray<FVector> N; TArray<FLinearColor> Col; TArray<FVector2D> UV, UV1; TArray<int32> Tri;
			const int32 W = J1 - CJ + 1;
			for (int32 i = CI; i <= I1; ++i)
				for (int32 j = CJ; j <= J1; ++j)
				{
					const float X = MinX + i * Step, Y = MinY + j * Step, Z = At(i, j);
					V.Add(FVector(X, Y, Z));
					const FVector Nrm = FVector(At(i - 1, j) - At(i + 1, j), At(i, j - 1) - At(i, j + 1), 2.f * Step).GetSafeNormal();
					N.Add(Nrm);
					UV.Add(FVector2D(X / 1000.f, Y / 1000.f));
					if (bServer) { Col.Add(FLinearColor::White); UV1.Add(FVector2D::ZeroVector); continue; }
					// what the ground is here (rock, mud, sand, wear: GroundMask.h, the same function as the square's and Oak Hill's
					// ground, so they meet without a seam); M_Ground paints it. The slope over 16 m comes from this grid's neighbours.
					const float Up = JeebGround::Upness(At(i + 1, j), At(i - 1, j), At(i, j + 1), At(i, j - 1));
					const JeebGround::FMask Mask = JeebGround::At(X, Y, Z, Up);
					Col.Add(Mask.Color());
					// (2026-11-22: how much snow lies here goes in the second UV's U, from real noise worked out per point, JeebOpen::Snow: the
					// material's own tiled noise pictures showed as a repeating weave along the snow's edge. Other grounds leave it 0: no snow)
					UV1.Add(FVector2D(JeebOpen::SnowRaw(X, Y, Z, Up) + 1.f, JeebRework::MoorTint(X, Y)));   // (V, 2026-11-25: the moor's heather and tawny grass, MapRework.h)   // (+ 1: a ground that gives no second UV reads 0, which must mean no snow at all)
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
				if (JeebTerrain::InFineCell(X0, Y0, Step)) continue;   // (Steele Creek, the lake, the split: their own 2 m ground, BuildFine)
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
			PM->CreateMeshSection_LinearColor(0, V, Tri, N, UV, UV1, TArray<FVector2D>(), TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), true);
			// the underside too (no collision), so a camera that dips below a slope never sees through the world
			TArray<int32> Back;
			for (int32 t = 0; t + 2 < Tri.Num(); t += 3) Back.Append({ Tri[t], Tri[t + 2], Tri[t + 1] });
			// (2026-11-22, owner: "a triangle of strobing snow") the underside is its own component that casts no shadow: as a second section
			// of the same mesh it cast one from exactly the ground's own place, and every 8 m triangle turned away from a low sun shadowed
			// itself in flickering stripes (shadow acne)
			if (!bServer) AddUnderside(V, Back, N, UV, UV1, Col, Mat);
			PM->SetCollisionProfileName(TEXT("BlockAll"));
			PM->ComponentTags.Add(FName(TEXT("JeebClimb")));   // (hills and rock: climbable where too steep to walk, KnightMovement.h)
			if (!bServer)
			{
				if (Mat) PM->SetMaterial(0, Mat);
				else if (UMaterialInstanceDynamic* M = PM->CreateDynamicMaterialInstance(0, Fallback)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 0.32f, 0.12f));
			}
			else PM->SetVisibility(false);
			Chunks.Add(PM);
		}
	if (!bServer && Mat) BuildBeyond(Mat);
	BuildFine();
	BuildArch();
	JeebCaves::BuildShells(this, bServer ? nullptr : SharedGround(GetWorld()), bServer);   // (the caves' rock shells, 2026-11-26: Caves.h)
	JeebRoadSlope::Report();   // (-RoadSlope: the steepest ground on every road, 2026-11-20)
}

// The 2 m patches (JeebTerrain::FinePatches): the same ground, material and collision as the 8 m mesh, in chunks of 60 x 60 cells; each
// patch's edge points lie on the 8 m mesh's edges (FineVertexZ), so the two meet without a crack
void AWorldTerrain::BuildFine()
{
	const double T0 = FPlatformTime::Seconds();
	UMaterialInterface* Mat = SharedGround(GetWorld());
	const bool bServer = IsNetMode(NM_DedicatedServer);
	const float S = JeebTerrain::FineStep;
	int32 Cells = 0, WallPoints = 0, CaveCut = 0;
	for (const JeebTerrain::FFinePatch& P : JeebTerrain::FinePatches())
	{
		const int32 NX = FMath::RoundToInt((P.X1 - P.X0) / S), NY = FMath::RoundToInt((P.Y1 - P.Y0) / S);
		// every grid point once, with a ring beyond for the normals
		const int32 GX = NX + 3, GY = NY + 3;
		TArray<float> H; H.SetNumUninitialized(GX * GY);
		for (int32 i = 0; i < GX; ++i)
			for (int32 j = 0; j < GY; ++j)
			{
				const float X = P.X0 + (i - 1) * S, Y = P.Y0 + (j - 1) * S;
				H[i * GY + j] = (i >= 1 && j >= 1 && i <= NX + 1 && j <= NY + 1) ? JeebTerrain::FineVertexZ(P, X, Y) : JeebTerrain::HeightAt(X, Y);
			}
		auto At = [&](int32 i, int32 j) { return H[(i + 1) * GY + (j + 1)]; };
		// (Second pass on the sea cliffs, 2026-11-25; owner: "fewer obvious straight lines that make it look zig zaggy".) A wall is a
		// drop in the height itself, and on a plain grid a drop can only run along grid lines: every wall was a staircase of 2 m
		// straight cuts. So in a walled patch the points beside a drop are moved sideways onto it: the point on the high side to the
		// drop's true line (found by halving along the grid edge), the point on the low side to a little beyond it (the face leans
		// back by 0.4 to 2.3 m, never the same). The wall then follows the line the height function draws, at any angle and curve.
		// Points on the patch's border never move (they meet the 8 m mesh).
		const int32 VW = NY + 1;
		TArray<FVector2D> Mv;
		int32 Snapped = 0, Eased = 0;
		if (P.bWalls)
		{
			Mv.SetNumZeroed((NX + 1) * VW);
			constexpr float Jump = 250.f;
			for (int32 i = 1; i < NX; ++i)
				for (int32 j = 1; j < NY; ++j)
				{
					const float H0 = At(i, j), X0 = P.X0 + i * S, Y0 = P.Y0 + j * S;
					for (int32 Axis = 0; Axis < 2; ++Axis)
					{
						int32 Dir = 0; float Best = 0.f, BestDh = 0.f;
						for (int32 Dd = -1; Dd <= 1; Dd += 2)
						{
							const float Dh = (Axis == 0 ? At(i + Dd, j) : At(i, j + Dd)) - H0;
							if (FMath::Abs(Dh) < Jump) continue;
							const float Score = FMath::Abs(Dh) + (Dh < 0.f ? 1.0e6f : 0.f);   // (a point at a drop's top stays its top)
							if (Score > Best) { Best = Score; Dir = Dd; BestDh = Dh; }
						}
						if (Dir == 0) continue;
						const float X1 = X0 + (Axis == 0 ? Dir * S : 0.f), Y1 = Y0 + (Axis == 1 ? Dir * S : 0.f), MidH = H0 + 0.5f * BestDh;
						float Lo = 0.f, Hi = 1.f;
						for (int32 k = 0; k < 7; ++k)
						{
							const float M = 0.5f * (Lo + Hi);
							if ((JeebTerrain::HeightAt(FMath::Lerp(X0, X1, M), FMath::Lerp(Y0, Y1, M)) > MidH) == (BestDh < 0.f)) Lo = M; else Hi = M;
						}
						float D;
						if (BestDh < 0.f) D = FMath::Min(Lo * S, 0.95f * S);
						else
						{
							const float Lean = FMath::Min(40.f + 190.f * FMath::Clamp(0.5f + FMath::PerlinNoise2D(FVector2D(X0 / 900.f + 5.3f, Y0 / 900.f - 2.2f)), 0.f, 1.f), 0.3f * BestDh);
							D = FMath::Clamp(Hi * S - Lean, -0.45f * S, 0.95f * S);
						}
						(Axis == 0 ? Mv[i * VW + j].X : Mv[i * VW + j].Y) = Dir * D;
						++Snapped;
					}
				}
			// (no cell may fold over or shrink to a sliver: where one would, its points go half way back)
			auto Pt = [&](int32 i, int32 j) { return FVector2D(P.X0 + i * S, P.Y0 + j * S) + Mv[i * VW + j]; };
			for (int32 Pass = 0; Pass < 4; ++Pass)
			{
				int32 Bad = 0;
				for (int32 i = 0; i < NX; ++i)
					for (int32 j = 0; j < NY; ++j)
					{
						const FVector2D A = Pt(i, j), B = Pt(i + 1, j), C = Pt(i, j + 1), D = Pt(i + 1, j + 1);
						if (FVector2D::CrossProduct(B - A, C - A) > 0.05f * S * S && FVector2D::CrossProduct(D - B, C - B) > 0.05f * S * S) continue;
						Mv[i * VW + j] *= 0.5f; Mv[(i + 1) * VW + j] *= 0.5f; Mv[i * VW + j + 1] *= 0.5f; Mv[(i + 1) * VW + j + 1] *= 0.5f;
						++Bad;
					}
				Eased += Bad;
				if (Bad == 0) break;
			}
			UE_LOG(LogTemp, Log, TEXT("JEEBREWORK walls in patch X %.0f..%.0f: %d grid points moved onto a drop's line, %d cells eased back"), P.X0, P.X1, Snapped, Eased);
		}
		constexpr int32 Chunk = 60;
		for (int32 CI = 0; CI < NX; CI += Chunk)
			for (int32 CJ = 0; CJ < NY; CJ += Chunk)
			{
				const int32 I1 = FMath::Min(CI + Chunk, NX), J1 = FMath::Min(CJ + Chunk, NY);
				TArray<FVector> V; TArray<FVector> N; TArray<FLinearColor> Col; TArray<FVector2D> UV, UV1; TArray<int32> Tri;
				const int32 W = J1 - CJ + 1;
				for (int32 i = CI; i <= I1; ++i)
					for (int32 j = CJ; j <= J1; ++j)
					{
						const float X = P.X0 + i * S + (P.bWalls ? Mv[i * VW + j].X : 0.f), Y = P.Y0 + j * S + (P.bWalls ? Mv[i * VW + j].Y : 0.f), Z = At(i, j);
						V.Add(FVector(X, Y, Z));
						N.Add(FVector(At(i - 1, j) - At(i + 1, j), At(i, j - 1) - At(i, j + 1), 2.f * S).GetSafeNormal());
						UV.Add(FVector2D(X / 1000.f, Y / 1000.f));
						if (bServer) { Col.Add(FLinearColor::White); UV1.Add(FVector2D::ZeroVector); continue; }
						// (the slope over 16 m, as the 8 m mesh measures it, so the two paint alike)
						const float Up = JeebGround::Upness(JeebTerrain::HeightAt(X + 800.f, Y), JeebTerrain::HeightAt(X - 800.f, Y), JeebTerrain::HeightAt(X, Y + 800.f), JeebTerrain::HeightAt(X, Y - 800.f));
						Col.Add(JeebGround::At(X, Y, Z, Up).Color());
						UV1.Add(FVector2D(JeebOpen::SnowRaw(X, Y, Z, Up) + 1.f, JeebRework::MoorTint(X, Y)));   // (V, 2026-11-25: the moor's heather and tawny grass, MapRework.h)   // (2026-11-25: the snow, as the 8 m mesh gives it: the sea cliffs' patches reach into the north)
					}
				for (int32 i = CI; i < I1; ++i)
					for (int32 j = CJ; j < J1; ++j)
					{
						// (a cell an earlier patch already draws, or the square's own ground: drawn once, 2026-11-20)
						const float Cx = P.X0 + i * S, Cy = P.Y0 + j * S;
						bool bTaken = FMath::Abs(Cx + S * 0.5f) < JeebTerrain::GroundHalf && FMath::Abs(Cy + S * 0.5f) < JeebTerrain::GroundHalf;
						for (const JeebTerrain::FFinePatch& Q : JeebTerrain::FinePatches())
						{
							if (&Q == &P) break;
							if (Cx >= Q.X0 - 1.f && Cx + S <= Q.X1 + 1.f && Cy >= Q.Y0 - 1.f && Cy + S <= Q.Y1 + 1.f) { bTaken = true; break; }
						}
						if (bTaken) continue;
						if (!P.bWalls && JeebCaves::HoleCell(Cx, Cy)) { ++CaveCut; continue; }   // (a cave's mouth: its rock frame stands here, Caves.h)
						const int32 A = (i - CI) * W + (j - CJ), B = A + W, C = A + 1, D = B + 1;
						Tri.Append({ A, C, B, B, C, D });
						++Cells;
					}
				if (Tri.Num() == 0) continue;
				{
					const FVector Fn = FVector::CrossProduct(V[Tri[1]] - V[Tri[0]], V[Tri[2]] - V[Tri[0]]);
					if (Fn.Z > 0.f) for (int32 t = 0; t + 2 < Tri.Num(); t += 3) Swap(Tri[t + 1], Tri[t + 2]);
				}
				// (2026-11-25, the sea cliffs and stacks: MapRework.h) A triangle steeper than 60 degrees is a wall's face. It gets its own
				// points (so neither the grass of the top nor its light runs down the wall, and the top is lit as level ground right to its
				// lip), plain stone for its colour, and (second pass) it is cut into bands 2 m high whose points stand in or out by
				// JeebRework::WallRough: beds, bulges and hollows, an overhang here and there, shaded round instead of in flat boards.
				// Cut points lie on the triangles' edges at fixed heights, so neighbouring faces share them and no crack opens.
				if (P.bWalls)
				{
					const int32 Shared = V.Num();
					TArray<FVector> Acc; Acc.SetNumZeroed(Shared);
					TArray<uint8> Touch; Touch.SetNumZeroed(Shared);
					TArray<int32> Kept; Kept.Reserve(Tri.Num());
					TMap<FIntVector, int32> Weld;
					constexpr float Band = 200.f;
					auto OutOf = [&](int32 Idx) { const int32 i = CI + Idx / W, j = CJ + Idx % W; return FVector2D(At(i - 1, j) - At(i + 1, j), At(i, j - 1) - At(i, j + 1)).GetSafeNormal(); };
					// a wall's point on the edge between two of the chunk's points (Lo the lower) at height Z
					auto WallPoint = [&](int32 Lo, int32 Hi, float Z) -> int32
					{
						const FVector A = V[Lo], B = V[Hi];
						const float T = (B.Z - A.Z) > 1.f ? FMath::Clamp((Z - A.Z) / (B.Z - A.Z), 0.f, 1.f) : 0.f;
						const FVector Q = A + (B - A) * T;
						const FIntVector Key(FMath::RoundToInt32(Q.X * 2.f), FMath::RoundToInt32(Q.Y * 2.f), FMath::RoundToInt32(Q.Z * 2.f));
						if (const int32* Found = Weld.Find(Key)) return *Found;
						FVector Pd = Q;
						if (T > 0.f && T < 1.f)
						{
							const FVector2D O = FMath::Lerp(OutOf(Lo), OutOf(Hi), T).GetSafeNormal();
							// (2026-11-26: a face never gains more than WallLean outward per cm of height, measured up this edge from its foot and
							// down from its top, so no bulge hangs over a climber or a swimmer at its foot: he could not climb out of the sea at
							// X 56,000, stuck under a bulge, nor at X 60,000, under a rock lip)
							auto Raw = [&](float Zq) { return JeebRework::WallRough(A + (B - A) * ((Zq - A.Z) / (B.Z - A.Z))) * JeebRework::Ramp(0.f, 220.f, FMath::Min(Zq - A.Z, B.Z - Zq)); };
							float Push = Raw(Z);
							for (int32 k = 1; k <= 24; ++k)
							{
								const float Zq = Z - 50.f * k;
								if (Zq <= A.Z) { Push = FMath::Min(Push, JeebRework::WallLean * (Z - A.Z)); break; }
								Push = FMath::Min(Push, Raw(Zq) + JeebRework::WallLean * 50.f * k);
							}
							Push = FMath::Max(Push, -JeebRework::WallLean * (B.Z - Z));
							Pd += FVector(O.X, O.Y, 0.f) * Push;
						}
						const int32 New = V.Add(Pd);
						N.Add(FVector::ZeroVector);
						UV.Add(FVector2D(Pd.X / 1000.f, Pd.Y / 1000.f));
						UV1.Add(FVector2D(JeebOpen::SilverMountW(Q.X, Q.Y) > 0.f ? FMath::Min(FMath::Lerp(UV1[Lo].X, UV1[Hi].X, T), 0.9f) : FMath::Lerp(UV1[Lo].X, UV1[Hi].X, T), 1.f + JeebRework::PaleAt(Q.X, Q.Y)));   // (V from 1: a wall, and how pale its stone is: the Pale Cove, the Needles)
						Col.Add(FLinearColor(0.f, 1.f, 0.f, 0.f));
						Weld.Add(Key, New);
						++WallPoints;
						return New;
					};
					auto Cuts = [&](int32 Lo, int32 Hi, TArray<int32, TInlineAllocator<48>>& Out)
					{
						const float Z0 = V[Lo].Z, Z1 = V[Hi].Z;
						for (float Z = (FMath::FloorToFloat((Z0 + 20.f) / Band) + 1.f) * Band; Z < Z1 - 20.f; Z += Band) Out.Add(WallPoint(Lo, Hi, Z));
					};
					for (int32 t = 0; t + 2 < Tri.Num(); t += 3)
					{
						const FVector C0 = FVector::CrossProduct(V[Tri[t + 1]] - V[Tri[t]], V[Tri[t + 2]] - V[Tri[t]]);
						FVector Fn = C0.GetSafeNormal();
						if (Fn.Z < 0.f) Fn = -Fn;
						if (Fn.Z >= 0.5f && JeebCaves::CutsAt((V[Tri[t]] + V[Tri[t + 1]] + V[Tri[t + 2]]) / 3.f)) { ++CaveCut; continue; }
						if (Fn.Z >= 0.5f || !JeebOpen::SilverWallsAt(float(V[Tri[t]].X), float(V[Tri[t]].Y))) { for (int32 k = 0; k < 3; ++k) { Acc[Tri[t + k]] += Fn; Kept.Add(Tri[t + k]); } continue; }   // (2026-11-28: no stone walls on Silver Point's mountain away from its cliff)
						int32 Ix[3] = { Tri[t], Tri[t + 1], Tri[t + 2] };
						for (int32 k = 0; k < 3; ++k) Touch[Ix[k]] = 1;
						auto Lower = [&](int32 A, int32 B) { return V[A].Z < V[B].Z || (V[A].Z == V[B].Z && A < B); };
						if (Lower(Ix[1], Ix[0])) Swap(Ix[0], Ix[1]);
						if (Lower(Ix[2], Ix[1])) Swap(Ix[1], Ix[2]);
						if (Lower(Ix[1], Ix[0])) Swap(Ix[0], Ix[1]);
						const bool bSame = FVector::DotProduct(FVector::CrossProduct(V[Ix[1]] - V[Ix[0]], V[Ix[2]] - V[Ix[0]]), C0) > 0.f;
						TArray<int32, TInlineAllocator<48>> Lc, Rc;
						const int32 Wa = WallPoint(Ix[0], Ix[0], V[Ix[0]].Z), Wb = WallPoint(Ix[1], Ix[1], V[Ix[1]].Z), Wc = WallPoint(Ix[2], Ix[2], V[Ix[2]].Z);
						Lc.Add(Wa); Cuts(Ix[0], Ix[2], Lc); Lc.Add(Wc);
						Rc.Add(Wa); Cuts(Ix[0], Ix[1], Rc); Rc.Add(Wb); Cuts(Ix[1], Ix[2], Rc); Rc.Add(Wc);
						int32 Li = 0, Ri = 0;
						while (Li < Lc.Num() - 1 || Ri < Rc.Num() - 1)
						{
							const bool bLeft = Ri == Rc.Num() - 1 || (Li < Lc.Num() - 1 && V[Lc[Li + 1]].Z <= V[Rc[Ri + 1]].Z);
							const int32 Q0 = Lc[Li], Q1 = Rc[Ri], Q2 = bLeft ? Lc[Li + 1] : Rc[Ri + 1];
							if (bLeft) ++Li; else ++Ri;
							if (Q0 == Q1 || Q1 == Q2 || Q0 == Q2) continue;
							const int32 R1 = bSame ? Q1 : Q2, R2 = bSame ? Q2 : Q1;
							if (JeebCaves::CutsAt((V[Q0] + V[R1] + V[R2]) / 3.f)) { ++CaveCut; continue; }
							Kept.Append({ Q0, R1, R2 });
							const FVector Wn = -FVector::CrossProduct(V[R1] - V[Q0], V[R2] - V[Q0]);   // (the ground's triangles are wound with their cross product down)
							N[Q0] += Wn; N[R1] += Wn; N[R2] += Wn;
						}
					}
					for (int32 v = 0; v < Shared; ++v) if (Touch[v] && !Acc[v].IsNearlyZero()) N[v] = Acc[v].GetSafeNormal();
					for (int32 v = Shared; v < V.Num(); ++v) N[v] = N[v].IsNearlyZero() ? FVector::UpVector : N[v].GetSafeNormal();
					Tri = MoveTemp(Kept);
				}
				UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
				PM->SetupAttachment(GetRootComponent());
				PM->bUseAsyncCooking = true;
				PM->RegisterComponent();
				PM->CreateMeshSection_LinearColor(0, V, Tri, N, UV, UV1, TArray<FVector2D>(), TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), true);
				TArray<int32> Back;
				for (int32 t = 0; t + 2 < Tri.Num(); t += 3) Back.Append({ Tri[t], Tri[t + 2], Tri[t + 1] });
				if (!bServer) AddUnderside(V, Back, N, UV, UV1, Col, Mat);   // (casts no shadow: see BuildLand)
				PM->SetCollisionProfileName(TEXT("BlockAll"));
				PM->ComponentTags.Add(FName(TEXT("JeebClimb")));
				if (P.bWalls) { PM->ComponentTags.Add(FName(TEXT("JeebCliffWall"))); PM->SetCastShadow(CVarCliffWallShadows.GetValueOnGameThread() != 0); }
				if (!bServer) { if (Mat) PM->SetMaterial(0, Mat); }
				else PM->SetVisibility(false);
				Chunks.Add(PM);
			}
	}
	for (const JeebTerrain::FFinePatch& Fp : JeebTerrain::FinePatches()) UE_LOG(LogTemp, Log, TEXT("JEEBWATER fine patch X %.0f..%.0f Y %.0f..%.0f"), Fp.X0, Fp.X1, Fp.Y0, Fp.Y1);
	UE_LOG(LogTemp, Log, TEXT("JEEBWATER fine ground: %d patches, %d cells of 2 m (%d points on the sea cliffs' walls), built in %.0f ms; %d cells left out at cave mouths"), JeebTerrain::FinePatches().Num(), Cells, WallPoints, (FPlatformTime::Seconds() - T0) * 1000.0, CaveCut);
	// (2026-11-25, the map rework: where the sea cliffs and Anchorage's bay lie, for placing test views and for comparing machines)
	{
		uint32 Hash = 0;
		for (float X = 24000.f; X <= 100000.f; X += 4000.f)
		{
			const JeebRework::FSeaCliff C = JeebRework::SeaCliffAt(X);
			const float Cy = JeebWorld::CoastY(X), Top = JeebTerrain::HeightAt(X, Cy - C.Run - 600.f);
			Hash = FCrc::MemCrc32(&Cy, sizeof(float), Hash); Hash = FCrc::MemCrc32(&Top, sizeof(float), Hash);
			float LipY = 0.f;   // (where the top ends, walking in from the sea: for placing test views)
			for (float D = -2500.f; D <= 9000.f; D += 50.f) if (JeebTerrain::HeightAt(X, Cy - D) > JeebWorld::SeaLevel + 0.8f * C.Top * C.A) { LipY = Cy - D; break; }
			UE_LOG(LogTemp, Log, TEXT("JEEBREWORK cliff at X %.0f: coast Y %.0f, wall %.1f m (%s%s), ground at its lip %.1f m, lip at Y %.0f"), X, Cy, C.Top * C.A / 100.f, C.Ledge > 0.5f ? TEXT("stepped") : TEXT("sheer"), C.Beach > 0.3f ? TEXT(", beach at its foot") : TEXT(""), Top / 100.f, LipY);
		}
		for (float X = -36000.f; X <= -6000.f; X += 3000.f) { const float Cy = JeebWorld::CoastY(X); Hash = FCrc::MemCrc32(&Cy, sizeof(float), Hash); UE_LOG(LogTemp, Log, TEXT("JEEBREWORK bay at X %.0f: shore Y %.0f"), X, Cy); }
		const TArray<FVector2D>& East = JeebWorld::Rivers()[1].Pts;
		UE_LOG(LogTemp, Log, TEXT("JEEBREWORK the east branch ends in the bay at %.0f %.0f (%d points); hash=%08x"), East.Last().X, East.Last().Y, East.Num(), Hash);
	}

	// Checks (every machine: compare the hash lines between a server and a client)
	{
		uint32 Hash = 0;
		for (const JeebWorld::FRiver& Rv : JeebWorld::Rivers())
		{
			Hash = FCrc::MemCrc32(Rv.Pts.GetData(), Rv.Pts.Num() * sizeof(FVector2D), Hash);
			Hash = FCrc::MemCrc32(Rv.Halfs.GetData(), Rv.Halfs.Num() * sizeof(float), Hash);
		}
		const JeebWorld::FLake& L = JeebWorld::Lake();
		Hash = FCrc::MemCrc32(L.SD.GetData(), L.SD.Num() * sizeof(float), Hash);
		float LakeM2 = 0.f;
		for (float D : L.SD) if (D < 100.f) LakeM2 += JeebWorld::LakeGridStep * JeebWorld::LakeGridStep / 10000.f;
		float RoadDry = 1.0e9f; FVector2D RoadAt = FVector2D::ZeroVector;
		for (const JeebSteele::FStreet& St : JeebSteele::Streets())
			if (St.Pts.Num() > 800)
				for (const FVector2D& Q : St.Pts) { const float E = JeebWorld::WaterEdge(Q.X, Q.Y) - 100.f - St.Width * 0.5f; if (E < RoadDry) { RoadDry = E; RoadAt = Q; } }
		const FVector2D Isle = JeebWorld::IsleAt();
		UE_LOG(LogTemp, Log, TEXT("JEEBWATER shape hash=%08x; rivers %d (split %d pts), lake about %.0f m2 of water, its island top %.1f m over the water; Steele Creek's road keeps %.1f m from the water at its nearest (%.0f %.0f)"),
			Hash, JeebWorld::Rivers().Num(), JeebWorld::Rivers().Num() > 3 ? JeebWorld::Rivers()[3].Pts.Num() : 0, LakeM2, (JeebTerrain::HeightAt(Isle.X, Isle.Y) - JeebWorld::RiverLevel) / 100.f, RoadDry / 100.f, RoadAt.X, RoadAt.Y);
		{
			float Hw = 0.f; const float D = JeebWorld::RiverDistance(-3550.f, 12110.f, Hw);
			UE_LOG(LogTemp, Log, TEXT("JEEBWATER bridge reach: %.0f cm from the line's middle, half width %.0f (was 700), ground there %.0f"), D, Hw, JeebTerrain::HeightAt(-3550.f, 12110.f));
		}
		// every building's pad: its level, the drawn ground under its walls and at its doorstep against it, the steepest ground round it
		uint32 PadHash = 0; int32 Bad = 0;
		for (const JeebTerrain::FBuildingPad& P : JeebTerrain::BuildingPads())
		{
			auto World = [&](const FVector2D& Lc) { return P.Mid + FVector2D(Lc.X * P.Cs - Lc.Y * P.Sn, Lc.X * P.Sn + Lc.Y * P.Cs); };
			float Off = 0.f;
			for (int32 a = 0; a <= 4; ++a) for (int32 b = 0; b <= 4; ++b)
			{
				const FVector2D W = World(FVector2D(FMath::Lerp(-P.Half.X, P.Half.X, a / 4.f), FMath::Lerp(-P.Half.Y, P.Half.Y, b / 4.f)));
				Off = FMath::Max(Off, FMath::Abs(JeebTerrain::DrawnHeight(W.X, W.Y) - P.Level));
			}
			const FVector2D Door = World(P.ApronMid);
			const float DoorOff = JeebTerrain::DrawnHeight(Door.X, Door.Y) - P.Level;
			float Steep = 0.f, Was = 0.f, Wet = 1.0e9f;
			for (int32 k = 0; k < 48; ++k)
			{
				const FVector2D Dir(FMath::Cos(2.f * PI * k / 48.f), FMath::Sin(2.f * PI * k / 48.f));
				for (float R = P.Half.Size() + 300.f; R < P.Half.Size() + 2400.f; R += 200.f)
				{
					const FVector2D Q = P.Mid + Dir * R, Q2 = P.Mid + Dir * (R + 200.f);
					if (JeebWorld::WaterEdge(Q.X, Q.Y) < 600.f) continue;
					Steep = FMath::Max(Steep, FMath::Abs(JeebTerrain::HeightAt(Q2.X, Q2.Y) - JeebTerrain::HeightAt(Q.X, Q.Y)) / 200.f);
					Was = FMath::Max(Was, FMath::Abs(JeebTerrain::HeightAt(Q2.X, Q2.Y, true, 2, false) - JeebTerrain::HeightAt(Q.X, Q.Y, true, 2, false)) / 200.f);
				}
			}
			for (int32 a = 0; a <= 4; ++a) for (int32 b = 0; b <= 4; ++b)
			{
				const FVector2D W = World(FVector2D(FMath::Lerp(-P.Half.X, P.Half.X, a / 4.f), FMath::Lerp(-P.Half.Y, P.Half.Y, b / 4.f)));
				Wet = FMath::Min(Wet, JeebWorld::WaterEdge(W.X, W.Y));
			}
			if (Off > 15.f || FMath::Abs(DoorOff) > 15.f || Steep > FMath::Max(0.6f, Was + 0.05f)) ++Bad;
			PadHash = FCrc::MemCrc32(&P.Level, sizeof(float), PadHash);
			UE_LOG(LogTemp, Log, TEXT("JEEBWATER pad %s%s at %.0f %.0f level %.0f: walls off by up to %.0f cm, doorstep %+.0f cm, steepest round it %.2f (without the pads %.2f), walls %.0f cm from the water"),
				P.bSteele ? TEXT("Steele ") : TEXT(""), P.Name ? P.Name : TEXT("?"), P.Mid.X, P.Mid.Y, P.Level, Off, DoorOff, Steep, Was, Wet);
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBWATER pads: %d buildings, %d with ground off by over 15 cm or a slope over 0.6 round them steeper than without the pads; hash=%08x"), JeebTerrain::BuildingPads().Num(), Bad, PadHash);
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
		// (2026-11-21, owner: "black line on the horizon") the sheet ended 600 m past the world's edge, and past it the sky atmosphere's
		// nearly black planet ground showed as a dark band between the sea and the sky. It now runs 100 km out: from any height in the
		// game its far edge sits a small fraction of a pixel under the true horizon, and the haze covers the far water anyway
		constexpr float Far = 10000000.f;
		const float Y0 = WestMost - 3000.f, Y1 = MaxY + Far, X0 = MinX - Far, X1 = MaxX + Far;
		TArray<FLinearColor> White; White.Init(FLinearColor::White, 4);
		Sheet({ FVector(X0, Y0, SeaLevel), FVector(X1, Y0, SeaLevel), FVector(X0, Y1, SeaLevel), FVector(X1, Y1, SeaLevel) }, { 0, 2, 1, 1, 2, 3 }, White);
	}
	// The rivers, the split and the lake (2026-11-18): one sheet over a 4 m grid of every cell that holds water (within the drawn reach,
	// half width + 250 cm, of a river's or the split's line, or of the lake's shore line: JeebWater::SurfaceAt asks the same), cut into
	// chunks of 100 x 100 cells. Every shape the water takes is covered once, with no overlapping sheets where waters meet (the old
	// ribbons overlapped at the fork). The edge cells reach past the water into the banks, which stand over them. The vertex colours
	// carry each spot's downstream direction, fading to still water in the lake and where two channels meet (WorldMap.h FlowAt).
	{
		constexpr float Cell = 400.f;
		constexpr int32 ChunkN = 100;
		const int32 NI = FMath::CeilToInt((MaxX - MinX) / Cell), NJ = FMath::CeilToInt((MaxY - MinY) / Cell);
		TSet<int64> Wet;
		auto Try = [&](int32 i, int32 j)
		{
			if (i < 0 || j < 0 || i >= NI || j >= NJ) return;
			const int64 Key = int64(i) * NJ + j;
			if (Wet.Contains(Key)) return;
			const float Cx = MinX + (i + 0.5f) * Cell, Cy = MinY + (j + 0.5f) * Cell;
			if (WaterEdge(Cx, Cy) < 250.f + Cell * 0.71f) Wet.Add(Key);
		};
		TSet<int64> Tried;
		auto Visit = [&](int32 i, int32 j) { const int64 K = int64(i) * NJ + j; if (!Tried.Contains(K)) { Tried.Add(K); Try(i, j); } };
		for (const FRiver& Rv : Rivers())
			for (int32 s = 0; s + 1 < Rv.Pts.Num(); ++s)
			{
				const float Pad = FMath::Max(Rv.Halfs[s], Rv.Halfs[s + 1]) + 250.f + Cell;
				const FVector2D A = Rv.Pts[s], B = Rv.Pts[s + 1];
				const int32 I0 = FMath::FloorToInt((FMath::Min(A.X, B.X) - Pad - MinX) / Cell), I1 = FMath::FloorToInt((FMath::Max(A.X, B.X) + Pad - MinX) / Cell);
				const int32 J0 = FMath::FloorToInt((FMath::Min(A.Y, B.Y) - Pad - MinY) / Cell), J1 = FMath::FloorToInt((FMath::Max(A.Y, B.Y) + Pad - MinY) / Cell);
				for (int32 i = I0; i <= I1; ++i) for (int32 j = J0; j <= J1; ++j) Visit(i, j);
			}
		{
			const FBox2D L = FBox2D(Lake().Shore).ExpandBy(250.f + Cell);
			for (int32 i = FMath::FloorToInt((L.Min.X - MinX) / Cell); i <= FMath::FloorToInt((L.Max.X - MinX) / Cell); ++i)
				for (int32 j = FMath::FloorToInt((L.Min.Y - MinY) / Cell); j <= FMath::FloorToInt((L.Max.Y - MinY) / Cell); ++j) Visit(i, j);
		}
		// by chunk
		TMap<int64, TArray<FIntPoint>> ByChunk;
		for (const int64 K : Wet) { const int32 i = int32(K / NJ), j = int32(K % NJ); ByChunk.FindOrAdd(int64(i / ChunkN) * 100000 + j / ChunkN).Add(FIntPoint(i, j)); }
		TArray<int64> Keys; ByChunk.GetKeys(Keys); Keys.Sort();   // (the same order every run)
		int32 Cells = 0;
		for (const int64 CK : Keys)
		{
			TArray<FIntPoint>& Cs = ByChunk[CK];
			Cs.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
			TMap<int64, int32> VertOf;
			TArray<FVector> V; TArray<int32> T; TArray<FLinearColor> Col;
			auto Vert = [&](int32 i, int32 j)
			{
				const int64 K = int64(i) * (NJ + 1) + j;
				if (const int32* F = VertOf.Find(K)) return *F;
				const float X = MinX + i * Cell, Y = MinY + j * Cell;
				float Still = 0.f;
				const FVector2D Dir = FlowAt(X, Y, Still);
				const FLinearColor F = JeebWaterLook::FlowColor(Dir.IsNearlyZero() ? FVector2D(1.f, 0.f) : Dir);
				Col.Add(FLinearColor(FMath::Lerp(F.R, 0.5f, Still), FMath::Lerp(F.G, 0.5f, Still), 0.f, 1.f));
				V.Add(FVector(X, Y, JeebRework::On() ? FMath::Lerp(RiverLevel, SeaLevel + 2.f, Smooth(-2500.f, 300.f, Y - CoastY(X))) : RiverLevel));   // (2026-11-25: a river eases down to the sea over its last 25 m, so its sheet no longer ends in a 40 cm step in Anchorage's bay)
				return VertOf.Add(K, V.Num() - 1);
			};
			for (const FIntPoint& C : Cs)
			{
				const int32 A = Vert(C.X, C.Y), B = Vert(C.X + 1, C.Y), Cc = Vert(C.X, C.Y + 1), D = Vert(C.X + 1, C.Y + 1);
				T.Append({ A, Cc, B, B, Cc, D });
				++Cells;
			}
			Sheet(V, T, Col);
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBWATER rivers and lake: %d cells of 4 m in %d sheets"), Cells, Keys.Num());
	}
}

// The land beyond the map's edges (2026-11-22). From the tall northern range a player sees over the map's north, west and south
// edges, where there was nothing (the old 48 m edge ranges hid it from the ground). A coarse apron (64 m cells, 1.3 km out, no
// collision, drawn on players' machines only) carries the ground on from the edge: it starts on the edge line, never above the real
// ground's own edge, and eases out to far rolling land (more snowy range to the north, seabed off the coast), which the haze takes.
// Looks only: nobody can walk there (AWorldEdge's walls).
void AWorldTerrain::BuildBeyond(UMaterialInterface* Mat)
{
	using namespace JeebWorld;
	constexpr float Cs = 6400.f;
	constexpr int32 Out = 20;
	// (2026-11-22, owner: "the land that is unreachable is a sharp drop down. Could you make the cutoff not so apparent") The apron
	// used to start 60 cm under the lowest real ground within 64 m along the edge, on 64 m cells: under every high stretch of the edge the
	// real ground ended in a wall or a fin over it. Now the apron's lines along the edges are the world mesh's own 8 m points, so its first
	// row is exactly the real edge (no gap, nothing to hide), and its rings start fine (8, 24, 48 m) and widen to 64 m; the edge's
	// heights are smoothed more the further out, then eased to the far land over 1.2 km.
	auto Lines = [&](float Lo, float Hi, bool bPastHi)
	{
		static const float Near[] = { 800.f, 2400.f, 4800.f };
		TArray<float> Rings;
		for (float R : Near) Rings.Add(R);
		// (16 m rings out to about 320 m, where a player on the edge sees the ground's shape against the sky: 64 m cells drew straight
		// ridge lines there, 2026-11-22 check pictures; then 64 m)
		for (float R = 6400.f; R < 32500.f; R += 1600.f) Rings.Add(R);
		for (int32 k = 6; k <= Out; ++k) Rings.Add(k * Cs + 2400.f);
		for (int32 k = 1; k <= 7; ++k) Rings.Add(Out * Cs + 2400.f + k * 10.f * Cs);   // (then seven wide cells out to about 5.8 km, for the horizon)
		TArray<float> L;
		for (int32 k = Rings.Num() - 1; k >= 0; --k) L.Add(Lo - Rings[k]);
		const int32 In = FMath::Max(1, FMath::RoundToInt((Hi - Lo) / WorldStep));
		for (int32 k = 0; k <= In; ++k) L.Add(Lo + (Hi - Lo) * k / In);
		if (bPastHi) for (float R : Rings) L.Add(Hi + R);
		return L;
	};
	const TArray<float> Xs = Lines(MinX, MaxX, true), Ys = Lines(MinY, MaxY, false);
	const int32 NXv = Xs.Num(), NYv = Ys.Num();
	// the real edge's height near an edge point, averaged along the edge over R either way (R 0: the point itself)
	auto EdgeAvg = [&](float X, float Y, float R)
	{
		if (R < WorldStep) return JeebTerrain::HeightAt(X, Y);
		const bool bAlongY = X <= MinX || X >= MaxX;   // (an edge that runs along Y; corners take both)
		const bool bAlongX = Y <= MinY;
		float Sum = 0.f, W = 0.f;
		for (int32 k = -6; k <= 6; ++k)
		{
			const float D = R * k / 6.f, Wk = 1.f - FMath::Abs(k) / 7.f;
			if (bAlongY) { Sum += Wk * JeebTerrain::HeightAt(X, FMath::Clamp(Y + D, MinY, MaxY)); W += Wk; }
			if (bAlongX) { Sum += Wk * JeebTerrain::HeightAt(FMath::Clamp(X + D, MinX, MaxX), Y); W += Wk; }
		}
		return W > 0.f ? Sum / W : JeebTerrain::HeightAt(X, Y);
	};
	TArray<float> H; H.SetNumUninitialized(NXv * NYv);
	for (int32 i = 0; i < NXv; ++i)
		for (int32 j = 0; j < NYv; ++j)
		{
			const float X = Xs[i], Y = Ys[j];
			const float Cx = FMath::Clamp(X, MinX, MaxX), Cy = FMath::Clamp(Y, MinY, MaxY);
			const float D = FVector2D(X - Cx, Y - Cy).Size();
			if (D <= 1.f) { H[i * NYv + j] = JeebTerrain::HeightAt(X, Y); continue; }   // (on the edge: the world mesh's own point; inside: only for normals)
			const float Edge = EdgeAvg(Cx, Cy, FMath::Min(D * 1.5f, 40000.f));
			const FVector2D P(X, Y);
			float Far = 300.f + 1800.f * (0.5f + 0.5f * FMath::PerlinNoise2D(P / 47000.f + FVector2D(3.3f, 7.1f)));
			if (X > MaxX) Far = 3000.f + 9000.f * FMath::Square(0.5f + 0.5f * FMath::PerlinNoise2D(P / 33000.f + FVector2D(-2.6f, 1.9f))) + 1500.f * FMath::PerlinNoise2D(P / 14000.f + FVector2D(5.1f, 0.4f));   // the range carries on north
			if (Cy - CoastY(Cx) > -3000.f) Far = SeaLevel - 800.f;                                                               // off the coast: seabed
			H[i * NYv + j] = FMath::Lerp(Edge, Far, Smooth(0.f, 120000.f, D)) + 500.f * Smooth(2000.f, 30000.f, D) * FMath::PerlinNoise2D(P / 15000.f + FVector2D(9.2f, -4.4f));
		}
	auto At = [&](int32 i, int32 j) { return H[FMath::Clamp(i, 0, NXv - 1) * NYv + FMath::Clamp(j, 0, NYv - 1)]; };
	TArray<FVector> V; TArray<FVector> N; TArray<FLinearColor> Col; TArray<FVector2D> UV, UV1; TArray<int32> Tri;
	for (int32 i = 0; i < NXv; ++i)
		for (int32 j = 0; j < NYv; ++j)
		{
			const float X = Xs[i], Y = Ys[j], Z = At(i, j);
			V.Add(FVector(X, Y, Z));
			const bool bOnEdge = X >= MinX - 1.f && X <= MaxX + 1.f && Y >= MinY - 1.f;   // (a point of the world mesh's own edge: the same normal and paint)
			const FVector Nrm = bOnEdge
				? FVector(JeebTerrain::HeightAt(X - WorldStep, Y) - JeebTerrain::HeightAt(X + WorldStep, Y), JeebTerrain::HeightAt(X, Y - WorldStep) - JeebTerrain::HeightAt(X, Y + WorldStep), 2.f * WorldStep).GetSafeNormal()
				: FVector((At(i - 1, j) - At(i + 1, j)) / FMath::Max(Xs[FMath::Min(i + 1, NXv - 1)] - Xs[FMath::Max(i - 1, 0)], 1.f), (At(i, j - 1) - At(i, j + 1)) / FMath::Max(Ys[FMath::Min(j + 1, NYv - 1)] - Ys[FMath::Max(j - 1, 0)], 1.f), 1.f).GetSafeNormal();
			N.Add(Nrm);
			UV.Add(FVector2D(X / 1000.f, Y / 1000.f));
			if (bOnEdge) { const FVector2D Cl(FMath::Clamp(X, MinX, MaxX), FMath::Clamp(Y, MinY, MaxY)); Col.Add(JeebGround::At(Cl.X, Cl.Y, Z, JeebGround::Upness(Cl.X, Cl.Y)).Color()); }
			else Col.Add(FLinearColor(0.f, 0.f, 0.f, 0.f));
			UV1.Add(FVector2D(JeebOpen::SnowRaw(X, Y, Z, Nrm.Z) + 1.f, 0.f));
		}
	for (int32 i = 0; i + 1 < NXv; ++i)
		for (int32 j = 0; j + 1 < NYv; ++j)
		{
			const float Mx = 0.5f * (Xs[i] + Xs[i + 1]), My = 0.5f * (Ys[j] + Ys[j + 1]);
			if (Mx > MinX && Mx < MaxX && My > MinY) continue;   // (a cell of the map itself)
			if (Xs[i + 1] - Xs[i] < 1.f || Ys[j + 1] - Ys[j] < 1.f) continue;
			const int32 A = i * NYv + j, B = A + NYv, C = A + 1, D = B + 1;
			Tri.Append({ A, C, B, B, C, D });
		}
	if (Tri.Num() == 0) return;
	const FVector Fn = FVector::CrossProduct(V[Tri[1]] - V[Tri[0]], V[Tri[2]] - V[Tri[0]]);
	if (Fn.Z > 0.f) for (int32 t = 0; t + 2 < Tri.Num(); t += 3) Swap(Tri[t + 1], Tri[t + 2]);
	UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
	PM->SetupAttachment(GetRootComponent());
	PM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PM->RegisterComponent();
	PM->CreateMeshSection_LinearColor(0, V, Tri, N, UV, UV1, TArray<FVector2D>(), TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), false);
	PM->SetMaterial(0, Mat);
	PM->SetCastShadow(false);
	PM->bAffectDistanceFieldLighting = false;
	Chunks.Add(PM);
	UE_LOG(LogTemp, Log, TEXT("JEEBOPEN land beyond the edges: %d cells (8 m along the edges, rings from 8 m to 64 m) out to %.0f m past the north, west and south edges (drawn only, no collision)"), Tri.Num() / 6, Out * Cs / 100.f);
}

// The Pale Cove's rock arch (2026-11-25, MapRework.h; the owner's picture of a pale rock arch over light water). A height field cannot
// have a hole under it, so this one piece is its own mesh: a slab of rock from the cliff top out to the top of the nearest stack, with
// an opening under it to swim or wade through. Both ends are buried in the cliff and the stack, which are ordinary ground. Built on
// every machine with collision (the server too), climbable like the cliffs; flat-shaded faces as theirs, pale stone on the sides and
// underside, grass on top, with the one ground material.
void AWorldTerrain::BuildArch()
{
	using namespace JeebWorld;
	if (!JeebRework::On() || JeebRework::Stacks().Num() <= JeebRework::ArchStack) return;
	const JeebRework::FStack& St = JeebRework::Stacks()[JeebRework::ArchStack];
	const FVector2D B(St.X, StackY(St));
	// (Second pass, owner: "too perfectly round on the bottom, please look at pictures of real land bridges". Durdle Door, the Azure
	// Window, Arch Rock: the opening is lopsided, steep on one side and leaning on the other, its highest point off the middle; the
	// seaward leg is a thick pier; the top is thick and humped; the rock narrows unevenly over the opening and swells at its feet;
	// nothing is flat or straight. So: a tube of rock with a rounded, uneven cross-section, roughened as the cliffs' faces.)
	float LipIn = -1.0e9f;   // (cm inland of the coast line where the cliff's top begins, found by walking in from the sea)
	for (float D = -900.f; D <= 4500.f; D += 50.f) if (JeebTerrain::HeightAt(St.X, CoastY(St.X) - D) > SeaLevel + 900.f) { LipIn = D; break; }
	const FVector2D A(St.X, CoastY(St.X) - LipIn - 700.f);   // (7 m in from the lip: on the cliff top)
	const float ZA = LipIn > -1.0e8f ? JeebTerrain::HeightAt(A.X, A.Y) : 0.f, ZB = JeebTerrain::HeightAt(B.X, B.Y);
	const float L = FVector2D::Distance(A, B);
	if (LipIn < -1.0e8f || L < 2500.f || ZA < SeaLevel + 900.f || ZB < SeaLevel + 900.f) { UE_LOG(LogTemp, Warning, TEXT("JEEBREWORK no arch: the cliff top (%.0f) or the stack (%.0f) is not there"), ZA, ZB); return; }
	const FVector2D Dir = (B - A) / L, Side(-Dir.Y, Dir.X);
	const float O1 = 700.f + 130.f, O2 = FMath::Min(O1 + 1750.f, L - 0.75f * St.R);   // the opening, cm along: from the wall to a thick seaward pier
	constexpr int32 Nn = 44, Mm = 12;
	TArray<FVector> V; TArray<FVector> N; TArray<FLinearColor> Col; TArray<FVector2D> UV, UV1; TArray<int32> Tri;
	TArray<FVector> Ctr;
	for (int32 i = 0; i <= Nn; ++i)
	{
		const float S = float(i) / Nn, U = (S * L - O1) / (O2 - O1);
		float Bot = SeaLevel - 450.f, Over = 0.f;
		if (U > 0.f && U < 1.f)
		{
			// (highest 40% of the way out, the land side the steeper; its outline wavers)
			const float Vv = U < 0.4f ? U / 0.4f * 0.5f : 0.5f + (U - 0.4f) / 0.6f * 0.5f;
			Over = FMath::Pow(FMath::Sin(PI * Vv), U < 0.4f ? 0.5f : 0.8f);
			Bot += (JeebRework::ArchHole + 450.f) * Over * (1.f + 0.07f * FMath::Sin(U * 11.f + 0.8f));
		}
		float Top = FMath::Lerp(ZA, ZB, S) + 60.f + 230.f * FMath::Square(FMath::Sin(PI * FMath::Clamp((S - 0.1f) / 0.75f, 0.f, 1.f))) + 70.f * FMath::PerlinNoise1D(S * 4.3f + 3.1f);
		Top -= 160.f * (1.f - JeebRework::Ramp(0.f, 0.07f, S)) + 160.f * (1.f - JeebRework::Ramp(1.f, 0.93f, S));   // (both ends sink into the cliff top and the stack)
		Bot = FMath::Min(Bot, Top - 380.f);
		const float Half = 0.5f * JeebRework::ArchWidth * (1.3f - 0.5f * Over + 0.22f * FMath::PerlinNoise1D(S * 5.7f + 9.2f));
		const FVector2D Mid = A + Dir * (S * L) + Side * (110.f * FMath::PerlinNoise1D(S * 3.1f + 0.6f));
		Ctr.Add(FVector(Mid.X, Mid.Y, 0.5f * (Bot + Top)));
		for (int32 k = 0; k < Mm; ++k)
		{
			const float Th = 2.f * PI * k / Mm, Cy = FMath::Cos(Th), Sz = FMath::Sin(Th);
			const float Sy = FMath::Sign(Cy) * FMath::Pow(FMath::Abs(Cy), 0.65f), Szz = FMath::Sign(Sz) * FMath::Pow(FMath::Abs(Sz), 0.5f);
			const float Lat = Half * Sy * (1.f - 0.2f * FMath::Max(Szz, 0.f)) * (1.f + 0.3f * FMath::Max(-Szz, 0.f) * (1.f - Over));   // (rounded shoulders; the feet swell)
			FVector Q(Mid.X + Side.X * Lat, Mid.Y + Side.Y * Lat, FMath::Lerp(Bot, Top, 0.5f + 0.5f * Szz));
			const bool bGrass = Szz > 0.8f;
			const float Rough = 0.8f * JeebRework::WallRough(Q) + 30.f * FMath::PerlinNoise3D(Q / 240.f + FVector(5.1f, 2.2f, -6.3f));
			Q += FVector(Side.X * Sy, Side.Y * Sy, 0.45f * Szz) * (Rough * (bGrass ? 0.25f : 1.f));
			V.Add(Q);
			N.Add(FVector::ZeroVector);
			UV.Add(FVector2D(Q.X / 1000.f, Q.Y / 1000.f));
			UV1.Add(FVector2D(0.f, bGrass ? 0.f : 2.f));   // (U 0: no snow; V 2: a wall of pale stone)
			Col.Add(bGrass ? FLinearColor(0.f, 0.f, 0.f, 0.f) : FLinearColor(0.f, 1.f, 0.f, 0.f));
		}
	}
	// (wound as the ground's: a triangle's cross product points into the rock)
	auto Face = [&](int32 I0, int32 I1, int32 I2, const FVector& Out)
	{
		FVector C = FVector::CrossProduct(V[I1] - V[I0], V[I2] - V[I0]);
		if (FVector::DotProduct(C, Out) > 0.f) { Swap(I1, I2); C = -C; }
		Tri.Append({ I0, I1, I2 });
		N[I0] -= C; N[I1] -= C; N[I2] -= C;
	};
	for (int32 i = 0; i < Nn; ++i)
		for (int32 k = 0; k < Mm; ++k)
		{
			const int32 K1 = (k + 1) % Mm, P0 = i * Mm + k, P1 = i * Mm + K1, P2 = (i + 1) * Mm + K1, P3 = (i + 1) * Mm + k;
			const FVector Out = 0.25f * (V[P0] + V[P1] + V[P2] + V[P3]) - 0.5f * (Ctr[i] + Ctr[i + 1]);
			Face(P0, P1, P2, Out); Face(P0, P2, P3, Out);
		}
	for (FVector& Nv : N) Nv = Nv.IsNearlyZero() ? FVector::UpVector : Nv.GetSafeNormal();
	const bool bServer = IsNetMode(NM_DedicatedServer);
	UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
	PM->SetupAttachment(GetRootComponent());
	PM->bUseAsyncCooking = true;
	PM->RegisterComponent();
	PM->CreateMeshSection_LinearColor(0, V, Tri, N, UV, UV1, TArray<FVector2D>(), TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), true);
	PM->SetCollisionProfileName(TEXT("BlockAll"));
	PM->ComponentTags.Add(FName(TEXT("JeebClimb")));
	if (!bServer) { if (UMaterialInterface* Mat = SharedGround(GetWorld())) PM->SetMaterial(0, Mat); }
	else PM->SetVisibility(false);
	Chunks.Add(PM);
	const uint32 Hash = FCrc::MemCrc32(V.GetData(), V.Num() * sizeof(FVector));
	UE_LOG(LogTemp, Log, TEXT("JEEBREWORK arch: from the cliff top at %.0f %.0f (%.0f) to the stack at %.0f %.0f (%.0f), %.0f m long, opening %.0f m wide and %.1f m high over the sea; %d triangles; hash=%08x"),
		A.X, A.Y, ZA, B.X, B.Y, ZB, L / 100.f, (O2 - O1) / 100.f, JeebRework::ArchHole / 100.f, Tri.Num() / 3, Hash);
}
