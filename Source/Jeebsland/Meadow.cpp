#include "Meadow.h"
#include "Fill.h"
#include "EngineUtils.h"
#include "Palette.h"
#include "Terrain.h"
#include "GroundMask.h"
#include "Canopy.h"
#include "DayNight.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "Materials/MaterialInterface.h"

// Which grass the meadow grows (2026-11-15, owner: "lean towards realism with the grass"; the packs are the owner's Fab library,
// git-ignored, set up by Tools/BuildGrassPacks.ps1). Changing any of these regrows the grass round you at once (a short hitch).
static TAutoConsoleVariable<int32> CVarGrassPack(TEXT("jeeb.Grass.Pack"), 1,
	TEXT("Meadow grass: 0 our painted cards, 1 Grass Library + Meadow Flowers (realistic, Fab), 2 Open World Foliage (realistic, cheaper, Fab)"));
static TAutoConsoleVariable<float> CVarGrassDensity1(TEXT("jeeb.Grass.Density1"), 6.9412f, TEXT("Grass Library clumps per square metre close by"));
static TAutoConsoleVariable<float> CVarGrassDensity2(TEXT("jeeb.Grass.Density2"), 11.0f, TEXT("Open World Foliage clumps per square metre close by"));
static TAutoConsoleVariable<float> CVarGrassPackFade(TEXT("jeeb.Grass.PackFade"), 5000.f, TEXT("cm: the realistic grass has sunk into the ground by here (it starts 8 m nearer; 50 m since 2026-11-15, owner: twice as far)"));
static TAutoConsoleVariable<int32> CVarGrassFarCards(TEXT("jeeb.Grass.FarCards"), 0, TEXT("1: our painted cards rise where the realistic grass sinks, carrying the meadow to 50 m; 0: painted ground only beyond it"));
static TAutoConsoleVariable<FString> CVarGrassTint1(TEXT("jeeb.Grass.Tint1"), TEXT("0.9261 0.8294 1"), TEXT("Grass Library blades' colour multiplier, \"r g b\" (live; empty: the palette's GrassPack1, Content/Data/Palette.ini)"));
static TAutoConsoleVariable<FString> CVarGrassTint2(TEXT("jeeb.Grass.Tint2"), TEXT(""), TEXT("Open World Foliage blades' colour multiplier, \"r g b\" (live; empty: the palette's GrassPack2)"));
static TAutoConsoleVariable<float> CVarGrassField(TEXT("jeeb.Grass.PackField"), 1.f, TEXT("How much of our field colour (patches, swathes, dry patches) the realistic blades take, 0..1 (live)"));
static TAutoConsoleVariable<float> CVarGrassPackDesat(TEXT("jeeb.Grass.PackDesat"), 0.2983f, TEXT("How far the realistic blades are pulled toward grey (live)"));
static TAutoConsoleVariable<float> CVarGrassGreenDry(TEXT("jeeb.Grass.GreenDry"), 1.f, TEXT("How far the Grass Library's yellow stems and seed heads are pulled to green, 0..1 (live; owner: they stood out)"));
static TAutoConsoleVariable<FString> CVarMoorTawny(TEXT("jeeb.Moor.Tawny"), TEXT("1.2 1.1 0.66"), TEXT("The moor's tawny grass (2026-11-25): its colour as a multiplier on the grass tint, \"r g b\" (live)"));
static TAutoConsoleVariable<float> CVarMoorHeather(TEXT("jeeb.Moor.Heather"), 6.f, TEXT("Heather cushions per square metre in the heart of a heather drift on the moor (read as the grass grows; 0 none)"));
static TAutoConsoleVariable<FString> CVarMoorHeatherCol(TEXT("jeeb.Moor.HeatherColour"), TEXT("0.50 0.27 0.47"), TEXT("The heather's flower colour, \"r g b\" (read as the grass grows)"));
static TAutoConsoleVariable<float> CVarGrassFlowers(TEXT("jeeb.Grass.PackFlowers"), 0.4286f, TEXT("Flower clumps against our single flowers (a clump holds several)"));
static TAutoConsoleVariable<float> CVarGrassFarThin(TEXT("jeeb.Grass.FarThin"), 0.5f, TEXT("The far ring's clumps (past about 32 m) against before 2026-11-16 (1): 0.5 half as many, each a little wider so the ground stays covered (2026-11-16, for speed). Regrows round you. Near grass unchanged."));

namespace
{
	// (the square's size and how far the grass reaches are AMeadow's CellSize and Reach: 15 m and 50 m since 2026-11-14, was 7.5 m and 95 m)
	constexpr float FieldR = 9000.f;
	constexpr float NearDensity = 0.0012f;   // full tufts per square cm, close up
	constexpr float FarDensity = 0.0006f;    // thinner, cheaper tufts further out
	constexpr float NearDist = 1100.f;
	// The steps of detail (2026-11-21): where each begins, the share of a square's spots that grow a realistic clump (times the pack's
	// own share) and a painted card, and how much wider each is so the thinner crop still covers the ground. Every step keeps the
	// spots of the steps beyond it (a spot's own fixed rank decides), so walking toward a square only ever adds clumps to it.
	// Before: three steps at 17 and 32 m, the realistic share 1, 0.47, 0.09 (with jeeb.Grass.FarThin 0.5).
	constexpr int32 NumTiers = 6;
	constexpr float TierEdge[NumTiers - 1] = { 1200.f, 1700.f, 2400.f, 3200.f, 4100.f };
	constexpr float PackKeep[NumTiers] = { 1.f, 0.75f, 0.5f, 0.3f, 0.16f, 0.09f };
	constexpr float CardKeep[NumTiers] = { 1.f, 0.8f, 0.6f, 0.45f, 0.35f, 0.3f };
	inline float PackKeepAt(int32 T, float FarThin) { return T >= 4 ? FMath::Min(1.f, PackKeep[T] * FarThin * 2.f) : PackKeep[T]; }   // (jeeb.Grass.FarThin 0.5 is the table's)
	inline float WideFor(float Keep) { return FMath::Pow(FMath::Max(Keep, 0.02f), -0.45f); }

	// Where the meadow grows (2026-11-12): all over the world's land, so the original square, Oak Hill and the open world are one
	// meadow with no edge (before, only within FieldR of the start and in Oak Hill's rectangle, whose straight side showed).
	// Rock, mud, sand and water thin it out tuft by tuft (GroundMask.h). Frame-rate tests only (-JeebShot): -NoOakGrass leaves
	// out the town's part, -NoWorldGrass everything outside the start's circle and the town.
	bool GrowsHereWorld(float X, float Y, float PadCm = 0.f)
	{
		if (X < JeebWorld::MinX - PadCm || X > JeebWorld::MaxX + PadCm || Y < JeebWorld::MinY - PadCm || Y > JeebWorld::MaxY + PadCm) return false;
		const float E = JeebOakHill::EastM(Y), N = JeebOakHill::NorthM(X), P = PadCm / 100.f;
		const bool bTown = JeebOakHill::Enabled() && E > -60.f - P && E < 86.f + P && N > -118.f - P && N < 120.f + P;   // (JeebOakHill::GrassArea's rectangle)
		static const bool bNoTown = JeebOakHill::TestOff(TEXT("NoOakGrass")), bNoWorld = JeebOakHill::TestOff(TEXT("NoWorldGrass"));
		if (bTown) return !bNoTown || FVector2D(X, Y).Size() <= FieldR + PadCm;
		return !bNoWorld || FVector2D(X, Y).Size() <= FieldR + PadCm;
	}
}

AMeadow::AMeadow()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

// TownLab (the test map only, TownLab.h): over the rebuilt town the roads, buildings and wear are that town's.
static bool InLab(float X, float Y) { return JeebTownLab::NewOn() && JeebTownLab::InPatch(X, Y); }
bool AMeadow::GrowsHere(float X, float Y, float PadCm) const { return GrowsHereWorld(X, Y, PadCm); }
static TAutoConsoleVariable<int32> CVarGrassOldSteps(TEXT("jeeb.Grass.OldSteps"), 0, TEXT("1: the grass fills in by the three steps of before 2026-11-21 (17 and 32 m, a new crop at each), to compare; regrows round you"));
static bool OldSteps() { return CVarGrassOldSteps.GetValueOnGameThread() != 0; }
// (2026-11-21, owner: the grass steps about twice as far in every direction on HIGH, the outer steps very sparse.) The reach, the sinking
// edge and the steps' edges past the first (by Stretch^(step/4): the nearest 12 m step stays, the last edge and the reach go the whole
// way) are stretched; the outermost step's share is thinned by the stretch, so the far ring fills in from very sparse
static TAutoConsoleVariable<float> CVarGrassStretch(TEXT("jeeb.Grass.Stretch"), 1.f, TEXT("How much farther the grass's steps reach (1: 50 m; 2: 100 m, the outer steps very sparse). Set by the graphics setting (LOW 1); regrows round you"));
static float Stretch() { return OldSteps() ? 1.f : FMath::Clamp(CVarGrassStretch.GetValueOnGameThread(), 1.f, 3.f); }
int32 AMeadow::TierFor(float Dist) const
{
	if (OldSteps()) return Dist < 1700.f ? 0 : (Dist < 3200.f ? 1 : 2);
	const float S = Stretch();
	int32 T = 0; while (T < NumTiers - 1 && Dist >= TierEdge[T] * FMath::Pow(S, T / 4.f)) ++T; return T; }
// The ground as drawn at a spot (JeebTerrain::DrawnHeight), its meshes' grid points cached for the square being built
float AMeadow::GroundAt(float X, float Y)
{
	return JeebTerrain::DrawnHeightWith(X, Y, [this](float A, float B)
	{
		const uint64 K = (uint64(uint32(FMath::RoundToInt(A))) << 32) | uint64(uint32(FMath::RoundToInt(B)));
		if (const float* H = JobGround.Find(K)) return *H;
		return JobGround.Add(K, JeebTerrain::HeightAt(A, B));
	});
}
float AMeadow::Clearance(float X, float Y) const { return InLab(X, Y) ? JeebTownLab::RoadClearance(X, Y) : JeebTerrain::PathClearance(X, Y); }
float AMeadow::WearAt(float X, float Y, float PathClearanceCm) const
{
	if (!InLab(X, Y)) return JeebGround::BuildingWear(X, Y, PathClearanceCm);
	const FVector2D O = JeebTownLab::ToOld(X, Y);
	return JeebGround::BuildingWear(O.X, O.Y, PathClearanceCm);
}
bool AMeadow::NoCover(float X, float Y) const { return InLab(X, Y) ? JeebTownLab::OnBuilding(X, Y) : JeebTerrain::NoGroundCover(X, Y); }
float AMeadow::Nearness(float X, float Y) const { return InLab(X, Y) ? 1.f - FMath::SmoothStep(0.f, 700.f, JeebTownLab::BuildingDistance(X, Y)) : JeebTerrain::BuildingNearness(X, Y); }
// Sporadic patches of longer grass (owner, 2026-11-14: "mix short and longer grassy patches like in the art pictures ... spread it
// around sporadically to make it look natural"; Breath of the Wild's and Hogwarts Legacy's meadows): soft, irregular shapes of
// different sizes from two octaves of world-space noise, never a grid. 0 = the short field, 1 = the heart of a long patch.
static float LongGrassNoise(float X, float Y)
{
	const float N = 0.62f * FMath::PerlinNoise2D(FVector2D(X / 2600.f + 17.31f, Y / 2600.f - 4.17f))
	              + 0.38f * FMath::PerlinNoise2D(FVector2D(X / 820.f - 9.73f, Y / 820.f + 31.29f));
	return JeebWorld::Smooth(0.02f, 0.26f, N);
}

// Where long grass may grow: never on or beside a road (full room 4.5 m from its edge), at a building (the shorter-grass rule
// there wins), under a tree's crown, on trampled ground, or on a town's plaza
static float LongGrassRoom(float X, float Y, float Pc, float Near, float Cov, float Wear)
{
	float R = JeebWorld::Smooth(150.f, 450.f, Pc) * (1.f - Near) * (1.f - Cov) * (1.f - JeebWorld::Smooth(0.05f, 0.3f, Wear));
	R *= JeebWorld::Smooth(4500.f, 7000.f, FVector2D::Distance(FVector2D(X, Y), FVector2D(JeebOakHill::PosX, JeebOakHill::PosY)));
	if (JeebTownLab::NewOn()) R *= JeebWorld::Smooth(4500.f, 7000.f, FVector2D::Distance(FVector2D(X, Y), FVector2D(JeebTownLab::PlazaX, JeebTownLab::PlazaY)));
	return R;
}

// The meadow's flowers (2026-11-14, "a few flowers"): small heads on thin stems among the tufts, in clumps of one colour, as the
// references' meadows have them. Colours (linear): cream white, butter yellow, lilac, soft blue, rose.
static const FLinearColor FlowerColours[5] = {
	FLinearColor(0.95f, 0.93f, 0.82f), FLinearColor(1.0f, 0.82f, 0.25f), FLinearColor(0.82f, 0.66f, 1.0f),
	FLinearColor(0.62f, 0.75f, 1.0f), FLinearColor(1.0f, 0.60f, 0.70f) };   // (lighter since the first review: the blue read navy)
static float FlowerChance(float X, float Y)
{
	const float N = FMath::PerlinNoise2D(FVector2D(X / 1100.f + 3.7f, Y / 1100.f + 11.9f));
	return 0.01f + 0.16f * JeebWorld::Smooth(0.15f, 0.42f, N);   // (a few anywhere, clumps where the noise is high)
}
static int32 FlowerKind(float X, float Y)
{
	const float N = FMath::PerlinNoise2D(FVector2D(X / 1700.f - 21.3f, Y / 1700.f + 5.1f)) * 0.5f + 0.5f;   // (one colour per clump, mostly)
	return FMath::Clamp(FMath::FloorToInt(N * 5.f), 0, 4);
}

// A flower: a thin crossed stem and a five-petal head 30 cm up (vertex red 0 stem, 1 head: M_GrassFlower). Its V runs as a tuft's
// card (0 at the head, 1 at the ground): placed with its head at the blades' tips, it sways as they do.
static UStaticMesh* MakeFlowerMesh(UObject* Outer, UMaterialInterface* Mat)
{
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Flower")));
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Sign = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> Cols = Attr.GetVertexInstanceColors();
	TPolygonGroupAttributesRef<FName> GroupSlot = Attr.GetPolygonGroupMaterialSlotNames();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	GroupSlot[Group] = FName("Flower");
	auto Tri = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& N, float Head)
	{
		const FVector Pts[3] = { A, B, C };
		TArray<FVertexInstanceID> Inst;
		for (int32 k = 0; k < 3; ++k)
		{
			const FVertexID V = MD.CreateVertex();
			Pos[V] = FVector3f(Pts[k]);
			const FVertexInstanceID I = MD.CreateVertexInstance(V);
			Nrm[I] = FVector3f(N);
			Tan[I] = FVector3f(1.f, 0.f, 0.f);
			Sign[I] = 1.f;
			UVs[I] = FVector2f(0.5f, FMath::Max(0.f, 1.f - float(Pts[k].Z) / 30.f));
			Cols[I] = FVector4f(Head, 0.f, 0.f, 1.f);
			Inst.Add(I);
		}
		MD.CreatePolygon(Group, Inst);
	};
	const float H = 30.f, StemW = 0.7f;
	for (int32 c = 0; c < 2; ++c)   // the stem: two crossed thin strips
	{
		const FVector S = c == 0 ? FVector(StemW, 0.f, 0.f) : FVector(0.f, StemW, 0.f);
		const FVector N = c == 0 ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f);
		Tri(-S, S, S + FVector(0, 0, H), N, 0.f);
		Tri(-S, S + FVector(0, 0, H), -S + FVector(0, 0, H), N, 0.f);
	}
	const FVector Ctr(0.f, 0.f, H + 0.4f);   // the head: five rounded petals (a 20-point outline with five soft lobes), cupped a little
	for (int32 k = 0; k < 20; ++k)
	{
		auto P = [&](int32 i) { const float A = i * PI / 10.f; const float L = FMath::Abs(FMath::Cos(A * 2.5f)); const float R = 3.5f + 4.3f * L; return FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, H + 0.9f * L); };
		Tri(Ctr, P(k), P(k + 1), FVector(0.f, 0.f, 1.f), 1.f);
	}
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	SM->BuildFromMeshDescriptions(Descs, Params);
	return SM;
}

// A cushion of heather (2026-11-25, the moor: MapRework.h). Second pass: the first model, seven stems with a flower spike each, read as
// scattered purple flowers on bare ground. Heather is a low, dense mat: so this is a bumpy dome about 54 cm across and 17 cm high
// (a rim, a shoulder ring, a crown) with seven short sprigs standing out of it for a ragged outline; many of them overlap into one
// carpet. Vertex red 0 is the plant's green (the rim), 1 the instance's colour (the flowering top), mottled between; V runs as a
// tuft's card, so it stirs with the grass. 34 triangles.
static UStaticMesh* MakeHeatherMesh(UObject* Outer, UMaterialInterface* Mat)
{
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Flower")));
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Sign = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> Cols = Attr.GetVertexInstanceColors();
	TPolygonGroupAttributesRef<FName> GroupSlot = Attr.GetPolygonGroupMaterialSlotNames();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	GroupSlot[Group] = FName("Flower");
	auto Tri = [&](const FVector& A, const FVector& B, const FVector& C, float HeadA, float HeadB, float HeadC)
	{
		const FVector Pts[3] = { A, B, C };
		const float Heads[3] = { HeadA, HeadB, HeadC };
		TArray<FVertexInstanceID> Inst;
		for (int32 k = 0; k < 3; ++k)
		{
			const FVertexID V = MD.CreateVertex();
			Pos[V] = FVector3f(Pts[k]);
			const FVertexInstanceID I = MD.CreateVertexInstance(V);
			Nrm[I] = FVector3f(0.f, 0.f, 1.f);   // (lit as the ground is: a soft, even drift, no dark undersides)
			Tan[I] = FVector3f(1.f, 0.f, 0.f);
			Sign[I] = 1.f;
			UVs[I] = FVector2f(0.5f, FMath::Max(0.f, 1.f - float(Pts[k].Z) / 30.f));
			Cols[I] = FVector4f(Heads[k], 0.f, 0.f, 1.f);
			Inst.Add(I);
		}
		MD.CreatePolygon(Group, Inst);
	};
	FRandomStream R(7711);
	constexpr int32 Seg = 9;
	FVector Rim[Seg], Sh[Seg]; float RimH[Seg], ShH[Seg];
	for (int32 s = 0; s < Seg; ++s)
	{
		const float A = s * 2.f * PI / Seg + R.FRandRange(-0.2f, 0.2f), Rr = R.FRandRange(23.f, 31.f), In = R.FRandRange(0.5f, 0.68f);
		Rim[s] = FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, -1.f);
		Sh[s] = FVector(FMath::Cos(A + 0.2f) * Rr * In, FMath::Sin(A + 0.2f) * Rr * In, R.FRandRange(9.f, 15.f));
		RimH[s] = R.FRandRange(0.3f, 0.6f); ShH[s] = R.FRandRange(0.6f, 1.f);
	}
	const FVector Crown(R.FRandRange(-3.f, 3.f), R.FRandRange(-3.f, 3.f), 17.f);
	for (int32 s = 0; s < Seg; ++s)
	{
		const int32 n = (s + 1) % Seg;
		Tri(Rim[s], Rim[n], Sh[n], RimH[s], RimH[n], ShH[n]);
		Tri(Rim[s], Sh[n], Sh[s], RimH[s], ShH[n], ShH[s]);
		Tri(Sh[s], Sh[n], Crown, ShH[s], ShH[n], 0.8f);
	}
	for (int32 s = 0; s < 7; ++s)
	{
		const float A = R.FRandRange(0.f, 2.f * PI), Rad = R.FRandRange(4.f, 21.f);
		const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.f), Side(-Out.Y, Out.X, 0.f);
		const FVector Base = Out * Rad + FVector(0.f, 0.f, 8.f), Tip = Base + Out * R.FRandRange(1.f, 6.f) + FVector(0.f, 0.f, R.FRandRange(10.f, 16.f));
		Tri(Base - Side * 2.4f, Base + Side * 2.4f, Tip, 0.55f, 0.55f, 1.f);
	}
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	SM->BuildFromMeshDescriptions(Descs, Params);
	return SM;
}

// A grass tuft as three crossed flat cards (6 triangles). The blades are painted on a see-through texture (M_Grass).
static UStaticMesh* MakeCardMesh(UObject* Outer, UMaterialInterface* Mat, float Width, float Height)
{
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Grass")));

	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Sign = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> GroupSlot = Attr.GetPolygonGroupMaterialSlotNames();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	GroupSlot[Group] = FName("Grass");

	auto Tri = [&](const FVector& A, const FVector& B, const FVector& C, const FVector2D& UA, const FVector2D& UB, const FVector2D& UC)
	{
		const FVector Pts[3] = { A, B, C };
		const FVector2D Uv[3] = { UA, UB, UC };
		TArray<FVertexInstanceID> Inst;
		for (int32 k = 0; k < 3; ++k)
		{
			const FVertexID V = MD.CreateVertex();
			Pos[V] = FVector3f(Pts[k]);
			const FVertexInstanceID I = MD.CreateVertexInstance(V);
			Nrm[I] = FVector3f(0.f, 0.f, 1.f);
			Tan[I] = FVector3f(1.f, 0.f, 0.f);
			Sign[I] = 1.f;
			UVs[I] = FVector2f(float(Uv[k].X), float(Uv[k].Y));
			Inst.Add(I);
		}
		MD.CreatePolygon(Group, Inst);
	};

	for (int32 c = 0; c < 3; ++c)
	{
		const float Yaw = c * PI / 3.f;
		const FVector Along(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f);
		const FVector BL = -Along * (Width * 0.5f), BR = Along * (Width * 0.5f);
		const FVector Up(0.f, 0.f, Height);
		Tri(BL, BR, BR + Up, FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0));
		Tri(BL, BR + Up, BL + Up, FVector2D(0, 1), FVector2D(1, 0), FVector2D(0, 0));
	}

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&MD);
	SM->BuildFromMeshDescriptions(Descs, Params);
	return SM;
}

void AMeadow::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);

	UMaterialInterface* GrassMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Grass.M_Grass"));
	if (GrassMat)
	{
		GrassMid = UMaterialInstanceDynamic::Create(GrassMat, this);
		NearMesh = MakeCardMesh(this, GrassMid, 75.f, 48.f);
		FarMesh = NearMesh;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("M_Grass is missing: run MakeGrassCards.bat once. No grass tufts will grow."));
	}
	if (UMaterialInterface* FlowerMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_GrassFlower.M_GrassFlower")))
	{
		FlowerMesh = MakeFlowerMesh(this, FlowerMat);
		HeatherMesh = MakeHeatherMesh(this, FlowerMat);
	}
	else
		UE_LOG(LogTemp, Warning, TEXT("M_GrassFlower is missing: run Tools/BuildMaterials.ps1. No meadow flowers."));
	{
		int32 P = -1;   // (-GrassPack=N: the grass for test runs and Play, as jeeb.Grass.Pack)
		if (FParse::Value(FCommandLine::Get(), TEXT("GrassPack="), P) && P >= 0) CVarGrassPack->Set(P, ECVF_SetByCommandline);
	}
	{
		// (log JEEBGRASS: how much of the land the long patches cover, from a fixed sample, to tune LongGrassNoise without pictures)
		FRandomStream R(99);
		int32 N = 0; float Long = 0.f, Heart = 0.f, Fl = 0.f;
		for (int32 i = 0; i < 20000; ++i)
		{
			const float X = R.FRandRange(JeebWorld::MinX, JeebWorld::MaxX), Y = R.FRandRange(JeebWorld::MinY, JeebWorld::MaxY);
			const float L = LongGrassNoise(X, Y);
			++N; Long += L; Heart += L > 0.7f ? 1.f : 0.f; Fl += FlowerChance(X, Y);
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBGRASS long grass: mean %.2f, heart of a patch on %.0f%% of the land; flowers %.2f per 100 tufts; reach %.0f m in %.0f m squares"),
			Long / N, 100.f * Heart / N, 100.f * Fl / N, Reach / 100.f, CellSize / 100.f);
		// (test pictures: the nearest open spot to -GrassNear=X_Y with a long patch's heart, and with a clump of flowers)
		FString Near;
		if (FParse::Value(FCommandLine::Get(), TEXT("GrassNear="), Near, false))
		{
			TArray<FString> P; Near.ParseIntoArray(P, TEXT("_"));
			const FVector2D O(P.Num() > 0 ? FCString::Atof(*P[0]) : 0.f, P.Num() > 1 ? FCString::Atof(*P[1]) : 0.f);
			FVector2D BestL(O), BestF(O); float DL = 1e9f, DF = 1e9f;
			for (int32 j = -60; j <= 60; ++j)
				for (int32 i = -60; i <= 60; ++i)
				{
					const FVector2D Q = O + FVector2D(i, j) * 200.f;
					const float D = FVector2D::Distance(Q, O);
					if (!GrowsHere(Q.X, Q.Y) || JeebTerrain::PathClearance(Q.X, Q.Y) < 800.f || JeebCanopy::Cover(Q.X, Q.Y) > 0.f || JeebTerrain::BuildingNearness(Q.X, Q.Y) > 0.f) continue;
					if (D < DL && LongGrassNoise(Q.X, Q.Y) > 0.9f) { DL = D; BestL = Q; }
					if (D < DF && FlowerChance(Q.X, Q.Y) > 0.07f) { DF = D; BestF = Q; }
				}
			UE_LOG(LogTemp, Log, TEXT("JEEBGRASS near %.0f %.0f: long patch at %.0f %.0f, flowers at %.0f %.0f"), O.X, O.Y, BestL.X, BestL.Y, BestF.X, BestF.Y);
		}
	}

}

void AMeadow::Prewarm()
{
	// Grow the grass round the start right away (a moment longer to load, but nothing pops in when you first run off)
	if (NearMesh)
	{
		for (int32 j = -6; j <= 6; ++j)
			for (int32 i = -6; i <= 6; ++i)
			{
				const FVector2D Ctr((i + 0.5f) * CellSize, (j + 0.5f) * CellSize);
				const float D = Ctr.Size();
				if (D > 4200.f || D > FieldR) continue;
				StartJob(FIntPoint(i, j), TierFor(D));
				StepJob(1.0e6);
			}
	}
}

void AMeadow::DropCell(const FIntPoint& Id)
{
	if (FCell* C = Cells.Find(Id))
	{
		if (C->Comp) C->Comp->DestroyComponent();
		if (C->Flowers) C->Flowers->DestroyComponent();
		for (UInstancedStaticMeshComponent* P : C->Packed) if (P) P->DestroyComponent();
		Cells.Remove(Id);
	}
}

static FLinearColor ParseRgb(const FString& S)
{
	TArray<FString> P; S.ParseIntoArrayWS(P);
	return FLinearColor(P.Num() > 0 ? FCString::Atof(*P[0]) : 1.f, P.Num() > 1 ? FCString::Atof(*P[1]) : 1.f, P.Num() > 2 ? FCString::Atof(*P[2]) : 1.f);
}

// The pack's meshes from Content/Data/GrassPacks.csv (Pack, Role short|long|flower, Mesh, Weight, Height, Width), each with its own
// material instance (the pack's, whose parent Tools/grass_packs.py made from the pack's master with our field colour and sinking)
void AMeadow::LoadPack(int32 P)
{
	Kinds.Reset();
	KeepAlive.Reset();
	if (P <= 0) return;
	FString Text;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/GrassPacks.csv");
	if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogTemp, Warning, TEXT("JEEBGRASS pack %d: no %s, our own grass instead"), P, *Path); return; }
	TArray<FString> Lines; Text.ParseIntoArrayLines(Lines);
	for (const FString& L : Lines)
	{
		TArray<FString> C; L.ParseIntoArray(C, TEXT(","), false);
		if (C.Num() < 6 || !C[0].IsNumeric() || FCString::Atoi(*C[0]) != P) continue;
		const FString RoleName = C[1].TrimStartAndEnd().ToLower();
		FKind K;
		K.Role = RoleName == TEXT("long") ? 1 : (RoleName == TEXT("flower") ? 2 : (RoleName == TEXT("moor") ? 3 : 0));   // (moor, 2026-11-25: the tawny grass of the moor, MapRework.h)
		const FString MeshPath = C[2].TrimStartAndEnd();
		K.Mesh = LoadObject<UStaticMesh>(nullptr, *(MeshPath + TEXT(".") + FPaths::GetBaseFilename(MeshPath)));
		if (!K.Mesh) { UE_LOG(LogTemp, Warning, TEXT("JEEBGRASS pack %d: %s is missing (are the Fab packs in Content?)"), P, *MeshPath); continue; }
		K.Weight = FCString::Atof(*C[3]); K.Height = FCString::Atof(*C[4]); K.Width = FCString::Atof(*C[5]);
		K.NaturalH = FMath::Max(10.f, float(K.Mesh->GetBoundingBox().Max.Z));
		if (UMaterialInterface* M = K.Mesh->GetMaterial(0)) K.Mid = UMaterialInstanceDynamic::Create(M, this);
		JeebPalette::BindGrade(K.Mid);   // (the palette's knobs: these materials take the grade as parameters, not from the collection)
		UE_LOG(LogTemp, Log, TEXT("JEEBGRASS kind %s role %d: %.0f..%.0f cm tall, %d material slots, %d LODs"), *FPaths::GetBaseFilename(MeshPath), K.Role, K.Mesh->GetBoundingBox().Min.Z, K.Mesh->GetBoundingBox().Max.Z, K.Mesh->GetStaticMaterials().Num(), K.Mesh->GetNumLODs());
		KeepAlive.Add(K.Mesh);
		if (K.Mid) KeepAlive.Add(K.Mid);
		Kinds.Add(K);
	}
	if (!Kinds.ContainsByPredicate([](const FKind& K) { return K.Role == 0; }))
	{
		UE_LOG(LogTemp, Warning, TEXT("JEEBGRASS pack %d: no short grass loaded, our own grass instead"), P);
		Kinds.Reset(); KeepAlive.Reset();
		return;
	}
	PackFadeEnd = FMath::Max(600.f, CVarGrassPackFade.GetValueOnGameThread() * Stretch());
	PackFadeStart = FMath::Max(200.f, PackFadeEnd - 800.f * Stretch());
	bFarCards = CVarGrassFarCards.GetValueOnGameThread() != 0;
	const float Density = P == 1 ? CVarGrassDensity1.GetValueOnGameThread() : CVarGrassDensity2.GetValueOnGameThread();
	PackChance = FMath::Clamp(Density / (NearDensity * 10000.f), 0.f, 1.f);
	for (FKind& K : Kinds)
		if (K.Mid) { K.Mid->SetScalarParameterValue(TEXT("FadeStart"), PackFadeStart); K.Mid->SetScalarParameterValue(TEXT("FadeEnd"), PackFadeEnd); }
	UE_LOG(LogTemp, Log, TEXT("JEEBGRASS pack %d: %d kinds, %.1f clumps per m2 (%.0f%% of the spots), sinking %.0f..%.0f m, far cards %d"),
		P, Kinds.Num(), Density, PackChance * 100.f, PackFadeStart / 100.f, PackFadeEnd / 100.f, bFarCards ? 1 : 0);
}

// Every square in reach grown at once round a spot (the start, a changed grass setting, or a test still's jump across the map)
void AMeadow::RegrowAround(const FVector2D& Me)
{
	TArray<FIntPoint> All; Cells.GetKeys(All);
	for (const FIntPoint& Id : All) DropCell(Id);
	bJob = false;
	const int32 Cx = FMath::FloorToInt(Me.X / CellSize), Cy = FMath::FloorToInt(Me.Y / CellSize);
	const int32 Span = FMath::CeilToInt(Reach * Stretch() / CellSize) + 1;
	for (int32 j = Cy - Span; j <= Cy + Span; ++j)
		for (int32 i = Cx - Span; i <= Cx + Span; ++i)
		{
			const FVector2D Ctr((i + 0.5f) * CellSize, (j + 0.5f) * CellSize);
			const float D = FVector2D::Distance(Ctr, Me);
			if (D > Reach * Stretch() || !GrowsHere(Ctr.X, Ctr.Y, CellSize * 0.75f)) continue;
			StartJob(FIntPoint(i, j), TierFor(D));
			StepJob(1.0e6);
		}
}

// (2026-11-21) -GlowLog: every loaded mesh with a glowing material slot (Glow, Flame, Fire, Candle, Emiss), the box of that slot's
// vertices and of the whole mesh in its own frame, so each light can be put inside its glass or flame (see Fill.cpp's JeebGlowAt)
static void JeebGlowLog()
{
#if WITH_EDITORONLY_DATA
	for (TObjectIterator<UStaticMesh> It; It; ++It)
	{
		UStaticMesh* M = *It;
		const TArray<FStaticMaterial>& Mats = M->GetStaticMaterials();
		bool bAny = false;
		for (const FStaticMaterial& S : Mats) { const FString N = S.MaterialSlotName.ToString() + (S.MaterialInterface ? S.MaterialInterface->GetName() : FString()); if (N.Contains(TEXT("Glow")) || N.Contains(TEXT("Flame")) || N.Contains(TEXT("Fire")) || N.Contains(TEXT("Candle")) || N.Contains(TEXT("Emiss"))) bAny = true; }
		if (!bAny || !M->IsMeshDescriptionValid(0)) continue;
		const FMeshDescription* Md = M->GetMeshDescription(0);
		if (!Md) continue;
		FStaticMeshConstAttributes A(*Md);
		TVertexAttributesConstRef<FVector3f> Pos = A.GetVertexPositions();
		TPolygonGroupAttributesConstRef<FName> Slot = A.GetPolygonGroupMaterialSlotNames();
		FBox All(ForceInit);
		for (const FVertexID V : Md->Vertices().GetElementIDs()) All += FVector(Pos[V]);
		for (const FPolygonGroupID G : Md->PolygonGroups().GetElementIDs())
		{
			const FString N = Slot[G].ToString();
			FString MatName; for (const FStaticMaterial& S : Mats) if (S.MaterialSlotName == Slot[G] && S.MaterialInterface) MatName = S.MaterialInterface->GetName();
			const FString Both = N + TEXT(" ") + MatName;
			if (!(Both.Contains(TEXT("Glow")) || Both.Contains(TEXT("Flame")) || Both.Contains(TEXT("Fire")) || Both.Contains(TEXT("Candle")) || Both.Contains(TEXT("Emiss")))) continue;
			FBox B(ForceInit);
			for (const FTriangleID T : Md->GetPolygonGroupTriangles(G)) for (const FVertexInstanceID Vi : Md->GetTriangleVertexInstances(T)) B += FVector(Pos[Md->GetVertexInstanceVertex(Vi)]);
			UE_LOG(LogTemp, Display, TEXT("JEEBGLOW %s slot %s mat %s glow %.0f %.0f %.0f size %.0f %.0f %.0f mesh %.0f..%.0f %.0f..%.0f %.0f..%.0f"), *M->GetPathName(), *N, *MatName,
				B.GetCenter().X, B.GetCenter().Y, B.GetCenter().Z, B.GetSize().X, B.GetSize().Y, B.GetSize().Z, All.Min.X, All.Max.X, All.Min.Y, All.Max.Y, All.Min.Z, All.Max.Z);
		}
	}
#endif
}
#if !UE_BUILD_SHIPPING
// (-FloatScan, 2026-11-21, owner: "Why are the grass and knight floating here?" beside a dirt road by the river) Along every road and
// river, across a band 7 m either side, the ground he stands on (a trace down onto the world's solid ground) against the height the
// grass is placed at and the height of the ground as drawn (JeebTerrain::DrawnHeight). Logs JEEBFLOAT: how many spots and how far.
static void JeebFloatScan(UWorld* W)
{
	struct FBad { FVector2D P; float Grass, Drawn, Hit; FString What; };
	TArray<FBad> Bad, DrawnBad;
	int32 N = 0, NGrass = 0, NDrawn = 0, NMiss = 0, NOak = 0, NOakBad = 0;
	double SumG = 0.0, SumOak = 0.0;
	float MaxG = 0.f, MaxD = 0.f;
	TMap<FString, int32> ByWhat;
	auto Probe = [&](float X, float Y)
	{
		const float G = JeebTerrain::HeightAt(X, Y), D = JeebTerrain::DrawnHeight(X, Y);
		FHitResult Hit;
		FCollisionQueryParams Q(FName(TEXT("JeebFloat")), true);
		FCollisionObjectQueryParams O(ECC_WorldStatic);
		if (!W->LineTraceSingleByObjectType(Hit, FVector(X, Y, G + 1500.f), FVector(X, Y, G - 1500.f), O, Q)) { ++NMiss; return; }
		++N;
		const float H = Hit.ImpactPoint.Z;
		const FString What = Hit.GetComponent() ? Hit.GetComponent()->GetOwner()->GetClass()->GetName() + TEXT(".") + Hit.GetComponent()->GetName() : TEXT("?");
		if (H > D + 25.f) return;   // (standing on something: a bridge, a rock, a building)
		const float Dg = G - H, Dd = D - H;
		if (FMath::Abs(Dg) > 8.f) { ++NGrass; SumG += FMath::Abs(Dg); ByWhat.FindOrAdd(What)++; }
		if (FMath::Abs(Dd) > 4.f) ++NDrawn;
		MaxG = FMath::Max(MaxG, FMath::Abs(Dg)); MaxD = FMath::Max(MaxD, FMath::Abs(Dd));
		if (FVector2D::Distance(FVector2D(X, Y), FVector2D(JeebOakHill::PosX, JeebOakHill::PosY)) < 50000.f) { ++NOak; if (FMath::Abs(Dg) > 8.f) { ++NOakBad; SumOak += FMath::Abs(Dg); } }
		if (FMath::Abs(Dd) > 4.f && DrawnBad.Num() < 25) DrawnBad.Add({ FVector2D(X, Y), G, D, H, What });
		if (FMath::Abs(Dg) > 20.f || FMath::Abs(Dd) > 8.f) Bad.Add({ FVector2D(X, Y), G, D, H, What });
	};
	auto Along = [&](const TArray<FVector2D>& Pts, bool bClosed)
	{
		float Next = 0.f, S = 0.f;
		const int32 Segs = bClosed ? Pts.Num() : Pts.Num() - 1;
		for (int32 i = 0; i < Segs; ++i)
		{
			const FVector2D A = Pts[i], B = Pts[(i + 1) % Pts.Num()];
			const float L = FVector2D::Distance(A, B);
			if (L < 1.f) continue;
			const FVector2D Dir = (B - A) / L, Side(-Dir.Y, Dir.X);
			while (Next <= S + L)
			{
				const FVector2D C = A + Dir * (Next - S);
				for (float o = -700.f; o <= 700.f; o += 140.f) Probe(C.X + Side.X * o, C.Y + Side.Y * o);
				Next += 300.f;
			}
			S += L;
		}
	};
	for (const JeebTerrain::FPathDef& P : JeebTerrain::PathDefs()) Along(P.Pts, P.bClosed);
	for (const JeebWorld::FRiver& R : JeebWorld::Rivers())
	{
		// (the banks: the river's line pushed out to each side by its half width, where the grass meets the water)
		TArray<FVector2D> L, Rr;
		for (int32 i = 0; i < R.Pts.Num(); ++i)
		{
			const FVector2D Dir = (R.Pts[FMath::Min(i + 1, R.Pts.Num() - 1)] - R.Pts[FMath::Max(i - 1, 0)]).GetSafeNormal();
			const float Hw = R.Halfs.IsValidIndex(i) ? R.Halfs[i] : R.HalfWidth;
			L.Add(R.Pts[i] + FVector2D(-Dir.Y, Dir.X) * (Hw + 500.f)); Rr.Add(R.Pts[i] - FVector2D(-Dir.Y, Dir.X) * (Hw + 500.f));
		}
		Along(L, false); Along(Rr, false);
	}
	Bad.Sort([](const FBad& A, const FBad& B) { return FMath::Abs(A.Grass - A.Hit) > FMath::Abs(B.Grass - B.Hit); });
	UE_LOG(LogTemp, Display, TEXT("JEEBFLOAT %d spots on the ground (%d missed): grass off the ground by over 8 cm at %d (mean %.1f cm, worst %.0f); drawn vs solid over 4 cm at %d (worst %.0f)"),
		N, NMiss, NGrass, NGrass ? SumG / NGrass : 0.0, MaxG, NDrawn, MaxD);
	UE_LOG(LogTemp, Display, TEXT("JEEBFLOAT within 500 m of Oak Hill's plaza: %d spots, HeightAt off by over 8 cm at %d (mean %.1f cm)"), NOak, NOakBad, NOakBad ? SumOak / NOakBad : 0.0);
	for (const FBad& B : DrawnBad) UE_LOG(LogTemp, Display, TEXT("JEEBFLOAT drawn-vs-solid %.0f %.0f drawn %.0f solid %.0f %s"), B.P.X, B.P.Y, B.Drawn, B.Hit, *B.What);
	for (int32 i = 0; i < FMath::Min(40, Bad.Num()); ++i)
		UE_LOG(LogTemp, Display, TEXT("JEEBFLOAT bad %.0f %.0f grass %.0f drawn %.0f solid %.0f (%+.0f) %s"), Bad[i].P.X, Bad[i].P.Y, Bad[i].Grass, Bad[i].Drawn, Bad[i].Hit, Bad[i].Grass - Bad[i].Hit, *Bad[i].What);

	// (2026-11-21) the other placers: every instance of the forest, the autumn decor and the fill, traced down past itself. Each
	// mesh keeps its own sink (its median height over the solid ground); an instance more than 8 cm off that floats or sinks.
	// "HeightAt" counts how many would be off had they been placed at HeightAt (the old way), for comparing.
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		const FString Cls = It->GetClass()->GetName();
		if (!Cls.Contains(TEXT("OakForest")) && !Cls.Contains(TEXT("AutumnDecor")) && !Cls.Contains(TEXT("WorldFill"))) continue;
		int32 NI = 0, NOff = 0, NOld = 0; float Worst = 0.f;
		TArray<UInstancedStaticMeshComponent*> Comps; It->GetComponents(Comps);
		for (UInstancedStaticMeshComponent* C : Comps)
		{
			const int32 Cnt = FMath::Min(C->GetInstanceCount(), 4000);
			TArray<float> Rs, Ro; Rs.Reserve(Cnt); Ro.Reserve(Cnt);
			FCollisionQueryParams Q(FName(TEXT("JeebFloatI")), true); Q.AddIgnoredComponent(C);
			FCollisionObjectQueryParams O(ECC_WorldStatic);
			for (int32 i = 0; i < Cnt; ++i)
			{
				FTransform T; C->GetInstanceTransform(i, T, true);
				const FVector P = T.GetLocation();
				const float G = JeebTerrain::HeightAt(P.X, P.Y);
				if (FMath::Abs(P.Z - G) > 300.f) continue;   // (hung, on a roof, in the air: not on the ground)
				FHitResult Hit;
				if (!W->LineTraceSingleByObjectType(Hit, FVector(P.X, P.Y, G + 400.f), FVector(P.X, P.Y, G - 400.f), O, Q)) continue;
				if (Hit.ImpactPoint.Z > JeebTerrain::DrawnHeight(P.X, P.Y) + 25.f) continue;   // (on a bridge or rock)
				Rs.Add(P.Z - Hit.ImpactPoint.Z); Ro.Add(G - Hit.ImpactPoint.Z);
			}
			if (Rs.Num() == 0) continue;
			TArray<float> S = Rs; S.Sort(); const float Med = S[S.Num() / 2];
			for (int32 i = 0; i < Rs.Num(); ++i)
			{
				++NI; const float E = FMath::Abs(Rs[i] - Med); Worst = FMath::Max(Worst, E);
				if (E > 8.f) ++NOff;
				if (FMath::Abs(Ro[i]) > 8.f) ++NOld;
			}
		}
		// (2026-11-21) buried or lifted ends: each ground piece's four bottom corners against the drawn ground (the fill only)
		if (Cls.Contains(TEXT("WorldFill")))
		{
			TMap<FString, FVector4> WorstAt; TMap<FString, FIntPoint> Ends;   // mesh -> (pieces, pieces with a corner over 10 cm under or over the ground)
			for (UInstancedStaticMeshComponent* C : Comps)
			{
				if (!C->GetStaticMesh()) continue;
				const FBox B = C->GetStaticMesh()->GetBoundingBox();
				for (int32 i = 0; i < C->GetInstanceCount(); ++i)
				{
					FTransform T; C->GetInstanceTransform(i, T, true);
					const FVector P = T.GetLocation();
					if (FMath::Abs(P.Z - JeebTerrain::DrawnHeight(P.X, P.Y)) > 30.f) continue;   // (stacked, hung, on a deck)
					float WorstC = 0.f;
					for (int32 c = 0; c < 4; ++c)
					{
						const FVector Q = T.TransformPosition(FVector(c & 1 ? B.Max.X : B.Min.X, c & 2 ? B.Max.Y : B.Min.Y, B.Min.Z));
						WorstC = FMath::Max(WorstC, FMath::Abs(Q.Z - JeebTerrain::DrawnHeight(Q.X, Q.Y)));
					}
					FIntPoint& E = Ends.FindOrAdd(C->GetStaticMesh()->GetName()); ++E.X; if (WorstC > 10.f) ++E.Y;
					FVector4& Wp = WorstAt.FindOrAdd(C->GetStaticMesh()->GetName(), FVector4(0, 0, 0, -1)); if (WorstC > Wp.W) Wp = FVector4(P.X, P.Y, P.Z, WorstC);
				}
			}
			for (const TPair<FString, FIntPoint>& E : Ends) if (E.Value.Y > 0) { const FVector4 Wp = WorstAt[E.Key]; UE_LOG(LogTemp, Display, TEXT("JEEBFLOAT ends %s: %d of %d pieces have a corner over 10 cm off the ground (worst %.0f cm at %.0f %.0f %.0f)"), *E.Key, E.Value.Y, E.Value.X, Wp.W, Wp.X, Wp.Y, Wp.Z); }
		}
		UE_LOG(LogTemp, Display, TEXT("JEEBFLOAT placed %s: %d on the ground, off by over 8 cm at %d (worst %.0f); at HeightAt it would be %d"), *Cls, NI, NOff, Worst, NOld);
	}
}
#endif

void AMeadow::Tick(float Dt)
{
	Super::Tick(Dt);
#if !UE_BUILD_SHIPPING
	{
		static double Born = FPlatformTime::Seconds();
		static bool bScanned = false, bGlowLogged = false;
		if (!bScanned && FPlatformTime::Seconds() - Born > 8.0 && FParse::Param(FCommandLine::Get(), TEXT("FloatScan"))) { bScanned = true; JeebFloatScan(GetWorld()); }
		if (!bGlowLogged && FPlatformTime::Seconds() - Born > 8.0 && FParse::Param(FCommandLine::Get(), TEXT("GlowLog"))) { bGlowLogged = true; JeebGlowLog(); }
	}
#endif
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !NearMesh) return;
	if (GrassMid) GrassMid->SetScalarParameterValue(TEXT("Dusk"), ADayNight::Dusk);
	const FVector2D Me(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y);
	{
		// the grass setting (jeeb.Grass.*): a change regrows everything round you; the first tick grows the start (the woods are
		// planted by now, so grass can thin under them)
		static FString Was;
		const FString Now = FString::Printf(TEXT("%d %.2f %.2f %.0f %d %.2f %d %.2f"), CVarGrassPack.GetValueOnGameThread(), CVarGrassDensity1.GetValueOnGameThread(),
			CVarGrassDensity2.GetValueOnGameThread(), CVarGrassPackFade.GetValueOnGameThread(), CVarGrassFarCards.GetValueOnGameThread(), CVarGrassFarThin.GetValueOnGameThread(), CVarGrassOldSteps.GetValueOnGameThread(), CVarGrassStretch.GetValueOnGameThread());
		bool bRegrow = !bPrewarmed || Now != Was;
#if !UE_BUILD_SHIPPING
		static const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("JeebShot"));
		if (bShot && bPrewarmed && FVector2D::Distance(Me, LastMe) > 3000.f) bRegrow = true;   // (test stills jump across the map)
#endif
		if (bRegrow)
		{
			if (Now != Was || !bPrewarmed)
			{
				const int32 Want = CVarGrassPack.GetValueOnGameThread();
				LoadPack(Want);
				Pack = Kinds.Num() > 0 ? Want : 0;
				if (GrassMid)   // (our cards rise where the realistic grass sinks, M_Grass NearStart..NearEnd; off for our own grass)
				{
					const bool bRise = Pack != 0 && bFarCards;
					GrassMid->SetScalarParameterValue(TEXT("NearStart"), bRise ? PackFadeStart : -2.f);
					GrassMid->SetScalarParameterValue(TEXT("NearEnd"), bRise ? PackFadeEnd : -1.f);
				}
			}
			Was = Now;
			bPrewarmed = true;
			RegrowAround(Me);
		}
		LastMe = Me;
		if (Pack != 0)
		{
			const FString TintS = Pack == 1 ? CVarGrassTint1.GetValueOnGameThread() : CVarGrassTint2.GetValueOnGameThread();
			const FLinearColor Tint = TintS.TrimStartAndEnd().IsEmpty() ? JeebPalette::Raw(Pack == 1 ? FName(TEXT("GrassPack1")) : FName(TEXT("GrassPack2"))) : ParseRgb(TintS);   // (the material grades it with the palette's knobs)
			for (FKind& K : Kinds)
				if (K.Mid)
				{
					K.Mid->SetScalarParameterValue(TEXT("Dusk"), ADayNight::Dusk);
					const bool bFlower = K.Role == 2;   // (petals keep their own colours; only a little of the field's light and shade)
					K.Mid->SetVectorParameterValue(TEXT("JeebTint"), bFlower ? FLinearColor::White : (K.Role == 3 ? Tint * ParseRgb(CVarMoorTawny.GetValueOnGameThread()) : Tint));
					K.Mid->SetScalarParameterValue(TEXT("JeebField"), CVarGrassField.GetValueOnGameThread() * (bFlower ? 0.3f : 1.f));
					K.Mid->SetScalarParameterValue(TEXT("JeebDesat"), bFlower ? 0.f : CVarGrassPackDesat.GetValueOnGameThread());
					K.Mid->SetScalarParameterValue(TEXT("JeebGreenDry"), K.Role == 3 ? 0.45f * CVarGrassGreenDry.GetValueOnGameThread() : CVarGrassGreenDry.GetValueOnGameThread());   // (the moor's tawny grass keeps some of its dry stems; all of them read too yellow)
				}
		}
	}

	TArray<FIntPoint> Far;
	for (const TPair<FIntPoint, FCell>& P : Cells)
	{
		const FVector2D Ctr((P.Key.X + 0.5f) * CellSize, (P.Key.Y + 0.5f) * CellSize);
		if (FVector2D::Distance(Ctr, Me) > Reach * Stretch() + CellSize) Far.Add(P.Key);
	}
	for (const FIntPoint& Id : Far) DropCell(Id);

	// Build toward where you are heading: distances are measured from a point a second and a half ahead of you
	const FVector Vel = Pawn->GetVelocity();
	const FVector2D Ahead = Me + FVector2D(Vel.X, Vel.Y) * 1.5f;

	struct FWant { FIntPoint Id; int32 Tier; float D; };
	TArray<FWant> Want;
	const int32 Cx = FMath::FloorToInt(Me.X / CellSize), Cy = FMath::FloorToInt(Me.Y / CellSize);
	const int32 Span = FMath::CeilToInt(Reach * Stretch() / CellSize) + 1;
	for (int32 j = Cy - Span; j <= Cy + Span; ++j)
		for (int32 i = Cx - Span; i <= Cx + Span; ++i)
		{
			const FVector2D Ctr((i + 0.5f) * CellSize, (j + 0.5f) * CellSize);
			const float D = FVector2D::Distance(Ctr, Me);
			if (D > Reach * Stretch() || !GrowsHere(Ctr.X, Ctr.Y, CellSize * 0.75f)) continue;   // (the whole world's land, 2026-11-12)
			const FCell* Have = Cells.Find(FIntPoint(i, j));
			// Pick the detail from the nearer of now and a moment ahead, and only coarsen a square once it is well behind (no flicker at the boundary)
			const float DA = FMath::Min(D, float(FVector2D::Distance(Ctr, Ahead)));
			int32 T = TierFor(DA);
			if (Have && T > Have->Tier && TierFor(FMath::Max(D - 400.f, 0.f)) <= Have->Tier) T = Have->Tier;
			if (!Have || Have->Tier != T) Want.Add({ FIntPoint(i, j), T, DA + (Have ? 600.f : 0.f) });
		}
	Want.Sort([](const FWant& A, const FWant& B) { return A.D < B.D; });
	if (!bJob && Want.Num() > 0) StartJob(Want[0].Id, Want[0].Tier);
	if (bJob) StepJob(0.006);   // about 6 ms of grass work per frame
	if (!bJob && Want.Num() > 1) { StartJob(Want[1].Id, Want[1].Tier); StepJob(0.002); }   // a quick second square when there is time
}

void AMeadow::StartJob(const FIntPoint& Id, int32 Tier)
{
	bJob = true; JobId = Id; JobTier = Tier; JobDone = 0;
	// (the same spots at every step: the seed is the square's alone, and each spot draws from its own stream, StepJob)
	JobRand.Initialize(int32(uint32(Id.X) * 73856093u ^ uint32(Id.Y) * 19349663u));
	JobCount = FMath::RoundToInt(CellSize * CellSize * NearDensity);
	JobGround.Reset();
	// Far from every road, no tuft in this square can be near one: skip the (slow) road check for each tuft
	const FVector2D Ctr((Id.X + 0.5f) * CellSize, (Id.Y + 0.5f) * CellSize);
	bJobNearRoad = Clearance(Ctr.X, Ctr.Y) < CellSize * 0.75f + 200.f;
	// Near a building, each tuft asks for the doorway and drip-line wear itself: those patches are a metre or two across, finer
	// than the grid below, which used to blur them away so the grass stood untouched right up to doors and the well
	bJobExactWear = InLab(Ctr.X, Ctr.Y) || JeebOakHill::InBox(Ctr.X, Ctr.Y, CellSize);
	if (!bJobExactWear && FMath::Abs(Ctr.X) < 9800.f && FMath::Abs(Ctr.Y) < 9800.f)
		for (const JeebGround::FHutSpot& H : JeebGround::HutSpots())
			if (FVector2D::Distance(Ctr, H.Pos) < 900.f + CellSize) { bJobExactWear = true; break; }
	// the ground's mask on a 5 x 5 grid over the square (the roads are left out: each tuft checks those itself, below)
	for (int32 b = 0; b < 5; ++b)
		for (int32 a = 0; a < 5; ++a)
		{
			const float X = (Id.X + a * 0.25f) * CellSize, Y = (Id.Y + b * 0.25f) * CellSize;
			const JeebGround::FMask M = JeebGround::At(X, Y, false);
			JobAmount[b * 5 + a] = JeebGround::GrassAmount(M);
			JobWear[b * 5 + a] = WearAt(X, Y, bJobNearRoad ? Clearance(X, Y) : 1.0e6f);
		}
	JobXf.Reset(); JobTall.Reset(); JobFlowerXf.Reset(); JobFlowerCol.Reset();
	JobXf.Reserve(JobCount); JobTall.Reserve(JobCount);
	JobKindXf.SetNum(Kinds.Num());
	for (TArray<FTransform>& K : JobKindXf) K.Reset();
}

// A realistic clump's kind at a spot: each kind grows in its own drifts (a species-patch noise picks the main one there), mixed
static int32 PickKind(const TArray<int32>& Of, const TArray<float>& W, float X, float Y, FRandomStream& R)
{
	if (Of.Num() == 0) return -1;
	if (Of.Num() > 1 && R.FRand() < 0.55f)
	{
		const float N = FMath::PerlinNoise2D(FVector2D(X / 900.f + 41.7f, Y / 900.f - 12.3f)) * 0.5f + 0.5f;
		return Of[FMath::Clamp(FMath::FloorToInt(FMath::Clamp(N * 1.4f - 0.2f, 0.f, 0.999f) * Of.Num()), 0, Of.Num() - 1)];
	}
	float Sum = 0.f; for (float V : W) Sum += V;
	float Pick = R.FRand() * Sum;
	for (int32 i = 0; i < Of.Num(); ++i) { Pick -= W[i]; if (Pick <= 0.f) return Of[i]; }
	return Of.Last();
}

void AMeadow::StepJob(double BudgetSeconds)
{
	const double End = FPlatformTime::Seconds() + BudgetSeconds;
	TArray<int32> Of[4]; TArray<float> OfW[4];   // (the pack's kinds by role: short, long, flower, the moor's tawny grass)
	for (int32 k = 0; k < Kinds.Num(); ++k) { Of[Kinds[k].Role].Add(k); OfW[Kinds[k].Role].Add(Kinds[k].Weight); }
	const float FlowerShare = CVarGrassFlowers.GetValueOnGameThread();
	const float HeatherPerM2 = FMath::Clamp(CVarMoorHeather.GetValueOnGameThread(), 0.f, 12.f);
	const FLinearColor HeatherCol = ParseRgb(CVarMoorHeatherCol.GetValueOnGameThread());
	// (jeeb.Grass.FarThin thins the far ring and widens what is left: half as many, about 1.4 times as wide, keeps the cover)
	const float FarThin = FMath::Clamp(CVarGrassFarThin.GetValueOnGameThread(), 0.1f, 1.f);
	float PackHere = PackChance * PackKeepAt(JobTier, FarThin) / (JobTier == NumTiers - 1 ? Stretch() : 1.f), CardHere = CardKeep[JobTier];
	float PackWide = WideFor(PackKeepAt(JobTier, FarThin)), CardSize = FMath::Pow(CardHere, -0.4f), Taller = 1.f;   // (2026-11-22, owner: far grass grew in taller then shrank as you came closer: one height at every step)
	int32 KindsTier = JobTier <= 2 ? 0 : JobTier - 2;   // (0 every kind, 1 two, 2 or more one)
	bool bFlowerTier = JobTier <= 3;
	uint32 Seed = uint32(JobId.X) * 73856093u ^ uint32(JobId.Y) * 19349663u;
	if (OldSteps())   // (jeeb.Grass.OldSteps 1: the three steps of before 2026-11-21, a new crop at each, for comparing)
	{
		const float OldShare[3] = { 1.f, 0.55f * 0.85f, 0.3f * 0.6f * FarThin }, OldCard[3] = { 1.f, 0.55f, 0.3f }, OldSize[3] = { 1.f, 1.25f, 1.6f };
		const float OldWide[3] = { 1.f, 1.1f, 1.3f * FMath::Sqrt(1.f / FarThin) };
		PackHere = PackChance * OldShare[JobTier]; CardHere = OldCard[JobTier];
		PackWide = OldWide[JobTier]; CardSize = OldSize[JobTier]; Taller = 1.f + 0.1f * JobTier;
		KindsTier = JobTier; bFlowerTier = JobTier <= 1;
		Seed ^= uint32(JobTier) * 83492791u;
	}
	const bool bCards = Pack == 0 || bFarCards;
	const float Need = Pack == 0 ? CardHere : (bFarCards ? FMath::Max(CardHere, PackHere) : PackHere);
	while (JobDone < JobCount)
	{
		if ((JobDone & 15) == 15 && FPlatformTime::Seconds() > End) return;   // (checked first, so a tuft skipped below never misses it; the next call picks up here)
		++JobDone;
		// (each spot its own stream: the same place, size and rank at every step, whatever the other spots did)
		{
			uint32 H = Seed ^ (uint32(JobDone) * 2654435761u);   // (a murmur finish, so neighbouring spots' streams share no pattern)
			H ^= H >> 16; H *= 0x85ebca6bu; H ^= H >> 13; H *= 0xc2b2ae35u; H ^= H >> 16;
			JobRand.Initialize(int32(H));
			JobRand.GetUnsignedInt();
		}
		const float X = (JobId.X + JobRand.FRand()) * CellSize, Y = (JobId.Y + JobRand.FRand()) * CellSize;
		const float Rank = JobRand.FRand();
		if (Rank >= Need) continue;   // (not grown at this step: the spots of the farther steps are the lowest ranks)
		const float Sxy = JobRand.FRandRange(0.85f, 1.3f) * CardSize;
		const float Yaw = JobRand.FRandRange(0.f, 360.f), Sz = JobRand.FRandRange(0.75f, 1.3f);
		float Amount, Wear;
		SampleJob(X, Y, Amount, Wear);
		const float Keep = JobRand.FRand();
		if (Keep >= Amount || !GrowsHere(X, Y)) continue;   // thinning out over rock, mud and sand
		if (JeebFill::Covered(X, Y)) continue;   // (under a filler wagon, stall or well: Fill.h)
		const float Pc = bJobNearRoad ? Clearance(X, Y) : 1.0e6f;
		if (bJobExactWear) Wear = WearAt(X, Y, Pc);
		if (Keep < Amount * (1.f - 0.5f * JeebWorld::Smooth(0.3f, 0.7f, Wear)))   // and over trampled ground: sparser and short at walls and doors, never cleared (bare ground in a building's shadow reads black)
		{
			if ((Pc >= -12.f || (Pc >= -40.f && JobRand.FRand() < (Pc + 40.f) / 28.f)) && !NoCover(X, Y))   // a few tufts creep onto the edge of the dirt
			{
				// the grass grows right into the frayed edge of a path, short there and full height about a metre back
				float Short = FMath::Lerp(0.3f, 1.f, FMath::Clamp((Pc + 12.f) / 110.f, 0.f, 1.f));
				// under a tree the grass is short, so the fallen leaves show through it
				const float Cov = JeebCanopy::Cover(X, Y);
				Short *= 1.f - 0.72f * Cov;                                          // about ankle height under the crown
				const bool bThin = Cov > 0.f && JobRand.FRand() < 0.35f * Cov;     // and a little sparser
				// beside every building (huts, stalls, the castle, the village inside its palisade, the forts) the grass is a little shorter,
				// easing back up to full height over about 7 m so it never reads as tall-then-suddenly-short
				const float Near = Nearness(X, Y);
				Short *= 1.f - 0.22f * Near;   // a bit shorter beside buildings (taller than before, so the crossed cards stand up)
				Short *= FMath::Lerp(0.55f, 1.f, Amount) * (1.f - 0.6f * JeebWorld::Smooth(0.1f, 0.6f, Wear));   // short where it thins out, and trampled by doors
				// (2026-11-14) the open field a little shorter than before, so the long patches stand out of it; the long patches up
				// to twice that, a touch wider, never where LongGrassRoom says no (roads, buildings, trees, trampled ground, plazas)
				// (2026-11-25, the moor, MapRework.h: long grass almost everywhere, short among the heather)
				const float Moor = JeebRework::Moor(X, Y), Hea = Moor > 0.f ? JeebRework::Heather(X, Y) : 0.f, Taw = Moor > 0.f ? JeebRework::Tawny(X, Y) : 0.f;
				Short *= 1.f - 0.6f * Hea;
				// (second pass, owner: "making the grass a bit thicker/denser near the edges to mask the edge": within 6 m of a sea cliff's
				// lip the grass stands taller and wider, and more clumps grow between, below)
				const float LipNear = JeebWorld::SeaCliffLipNear(X, Y, 600.f);
				Short *= 1.f + 0.45f * LipNear;
				const float Tall = FMath::Lerp(LongGrassNoise(X, Y), FMath::Max(LongGrassNoise(X, Y), 0.4f + 0.6f * LongGrassNoise(X + 9100.f, Y - 4700.f)), Moor * (1.f - Hea)) * LongGrassRoom(X, Y, Pc, Near, Cov, Wear);
				Short *= 0.82f * (1.f + 1.3f * Tall * JobRand.FRandRange(0.8f, 1.15f));
				const float Wide = Sxy * FMath::Lerp(0.75f, 1.f, FMath::Min(Short, 1.f)) * (1.f + 0.35f * Near) * (1.f + 0.12f * Tall);   // and wider by buildings, so the tufts overlap into one mat and the individual cards disappear
				if (bThin) continue;
				const float Gz = GroundAt(X, Y);   // (the ground as drawn, not HeightAt: 2026-11-21, grass floated over the 8 m mesh's banks and cut roads)
				if (bCards && Rank < CardHere)
				{
					JobXf.Add(FTransform(FRotator(0.f, Yaw, 0.f), FVector(X, Y, Gz + 4.f), FVector(Wide, Wide, Sz * Taller * Short)));
					JobTall.Add(Tall);
				}
				if (Pack != 0)
				{
					// a realistic clump at about this spot's card height (48 cm card x its scale), keeping its own proportions: long-patch kinds
					// where the long grass is (more often toward a patch's heart), the short field kinds elsewhere
					// (2026-11-15, for speed: past about 17 m fewer, wider clumps of fewer kinds, so the far ring costs less but still reads as grass)
					const float KindRoll = JobRand.FRand(), Pick2 = JobRand.FRand(), Jit = JobRand.FRandRange(0.85f, 1.15f);
					const FRotator Tilt(JobRand.FRandRange(-4.f, 4.f), JobRand.FRandRange(0.f, 360.f), JobRand.FRandRange(-4.f, 4.f));
					if (Rank < PackHere)
					{
						const int32 Kr = (Of[3].Num() > 0 && Taw > 0.12f && Pick2 < Taw * 1.15f) ? 3 : (Of[1].Num() > 0 && Tall > 0.2f && KindRoll < Tall * 1.25f) ? 1 : 0;   // (the moor's tawny swathes first)
						// (every kind within 24 m; beyond, two and then one, for fewer draws: a spot keeps its kind's place in the list)
						const int32 KindsHere = KindsTier == 0 ? Of[Kr].Num() : FMath::Min(Of[Kr].Num(), KindsTier == 1 ? 2 : 1);
						int32 K = PickKind(Of[Kr], OfW[Kr], X, Y, JobRand);
						if (KindsHere < Of[Kr].Num() && Of[Kr].IndexOfByKey(K) >= KindsHere) K = Of[Kr][FMath::Min(FMath::FloorToInt(Pick2 * KindsHere), KindsHere - 1)];
						const FKind& Kd = Kinds[K];
						const float TargetH = 48.f * Sz * Taller * Short * Kd.Height;
						const float Xy0 = FMath::Clamp(TargetH / Kd.NaturalH, 0.75f, 1.3f) * Kd.Width * Jit * (1.f + 0.25f * Near), Xy = Xy0 * PackWide;
						// (never squashed or stretched far from its own shape; the floor from the unwidened clump, 2026-11-22: the far ring's
						// wider clumps used to lift their own height too, so they grew in taller and shrank on the swap to a nearer step)
						const float Zs = FMath::Clamp(TargetH / Kd.NaturalH, Xy0 * 0.55f, Xy0 * 1.6f);
						JobKindXf[K].Add(FTransform(Tilt, FVector(X, Y, Gz), FVector(Xy, Xy, Zs)));
						if (LipNear > 0.05f)   // (the sea cliffs' lip: up to three more clumps beside this one, a thick fringe that hides the edge)
							for (int32 e = 0; e < 3; ++e)
							{
								const float Ex = X + JobRand.FRandRange(-45.f, 45.f), Ey = Y + JobRand.FRandRange(-45.f, 45.f), Roll = JobRand.FRand(), Turn = JobRand.FRandRange(0.f, 360.f), Big = JobRand.FRandRange(1.f, 1.3f);
								if (Roll >= LipNear) continue;
								const float Ez = GroundAt(Ex, Ey);
								if (FMath::Abs(Ez - Gz) > 60.f) continue;   // (over the lip)
								JobKindXf[K].Add(FTransform(FRotator(Tilt.Pitch, Turn, Tilt.Roll), FVector(Ex, Ey, Ez), FVector(Xy * Big, Xy * Big, Zs * Big)));
							}
					}
					// a flower clump now and then (a clump holds several, so fewer than our single flowers), one kind per drift; only on a spot
					// grown at this step (so they thin with the grass and never pop on a nearer step's swap), as often near by as before
					if (Of[2].Num() > 0 && bFlowerTier && Rank < PackHere && Pc > 60.f && Wear < 0.3f && JobRand.FRand() < FlowerChance(X, Y) * (1.f - Cov) * FlowerShare / FMath::Max(PackChance, 0.05f))
					{
						const float Fn = FMath::Clamp((FMath::PerlinNoise2D(FVector2D(X / 1700.f - 21.3f, Y / 1700.f + 5.1f)) * 0.5f + 0.5f) * 1.5f - 0.25f, 0.f, 0.999f);
						const int32 K = JobRand.FRand() < 0.85f ? Of[2][FMath::FloorToInt(Fn * Of[2].Num())] : Of[2][JobRand.RandRange(0, Of[2].Num() - 1)];
						const float S = JobRand.FRandRange(0.85f, 1.15f) * Kinds[K].Width;
						const float Fx = X + JobRand.FRandRange(-20.f, 20.f), Fy = Y + JobRand.FRandRange(-20.f, 20.f);
						JobKindXf[K].Add(FTransform(FRotator(0.f, JobRand.FRandRange(0.f, 360.f), 0.f), FVector(Fx, Fy, GroundAt(Fx, Fy)), FVector(S, S, S * Kinds[K].Height)));
					}
					// (2026-11-25, the moor, MapRework.h) heather: clumps in drifts, as many per square metre whatever the grass's own density,
					// thinning with distance as the grass does; none on paths or trampled ground
					if (HeatherMesh && bFlowerTier && Hea > 0.08f && Rank < PackHere && Pc > 60.f && Wear < 0.3f
						&& JobRand.FRand() < JeebRework::Ramp(0.1f, 0.45f, Hea) * HeatherPerM2 / (NearDensity * 10000.f * FMath::Max(PackChance, 0.05f)))   // (second pass: full to a drift's ragged edge, not thinning all the way through it)
					{
						const float S = JobRand.FRandRange(1.5f, 2.4f);   // (cushions 80 to 130 cm across and 15 to 30 cm high that overlap into a carpet: at half that they stood apart as dark molehills)
						const float Hx = X + JobRand.FRandRange(-25.f, 25.f), Hy = Y + JobRand.FRandRange(-25.f, 25.f);
						JobHeatherXf.Add(FTransform(FRotator(0.f, JobRand.FRandRange(0.f, 360.f), 0.f), FVector(Hx, Hy, GroundAt(Hx, Hy) - 1.f), FVector(S, S, S * JobRand.FRandRange(0.5f, 0.85f))));
						// (purple to purple-brown: some cushions in flower, some going over)
						JobHeatherCol.Add(FMath::Lerp(HeatherCol, FLinearColor(0.3f, 0.19f, 0.14f), 0.65f * FMath::Square(JobRand.FRand())) * JobRand.FRandRange(0.75f, 1.2f));
					}
					continue;
				}
				// a flower beside this tuft now and then, its head just above the blades round it (tiers 0 and 1: within about 27 m;
				// none on paths, trampled ground or under trees)
				if (FlowerMesh && bFlowerTier && Rank < CardHere && Pc > 60.f && Wear < 0.3f && JobRand.FRand() < FlowerChance(X, Y) * (1.f - Cov))
				{
					const float Fx = X + JobRand.FRandRange(-20.f, 20.f), Fy = Y + JobRand.FRandRange(-20.f, 20.f);
					const float Up = FMath::Clamp(Sz * Short * 1.75f * JobRand.FRandRange(0.9f, 1.1f), 0.7f, 2.4f);   // (the head at the blades' tips, 30 cm x Up against a 48 cm card)
					const float S = JobRand.FRandRange(0.85f, 1.2f);
					JobFlowerXf.Add(FTransform(FRotator(JobRand.FRandRange(-8.f, 8.f), JobRand.FRandRange(0.f, 360.f), JobRand.FRandRange(-8.f, 8.f)), FVector(Fx, Fy, GroundAt(Fx, Fy) + 1.f), FVector(S, S, Up)));
					const int32 K = JobRand.FRand() < 0.85f ? FlowerKind(X, Y) : JobRand.RandRange(0, 4);
					JobFlowerCol.Add(FlowerColours[K] * JobRand.FRandRange(0.85f, 1.1f));
				}
			}
		}
	}
	FinishJob();
}

void AMeadow::SampleJob(float X, float Y, float& OutAmount, float& OutWear) const
{
	const float U = FMath::Clamp((X / CellSize - JobId.X) * 4.f, 0.f, 3.999f), V = FMath::Clamp((Y / CellSize - JobId.Y) * 4.f, 0.f, 3.999f);
	const int32 A = FMath::FloorToInt(U), B = FMath::FloorToInt(V);
	const float Fu = U - A, Fv = V - B;
	auto Bi = [&](const float* G) { return FMath::Lerp(FMath::Lerp(G[B * 5 + A], G[B * 5 + A + 1], Fu), FMath::Lerp(G[(B + 1) * 5 + A], G[(B + 1) * 5 + A + 1], Fu), Fv); };
	OutAmount = Bi(JobAmount);
	OutWear = Bi(JobWear);
}

void AMeadow::FinishJob()
{
	bJob = false;
#if !UE_BUILD_SHIPPING
	{
		// (test runs: each square that changes its step, when and how far off, to find the moment in a film)
		static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("GrassStepLog"));
		const FCell* Was = Cells.Find(JobId);
		APlayerController* PC = bLog ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (Was && PC && PC->GetPawn())
		{
			const FVector P = PC->GetPawn()->GetActorLocation();
			UE_LOG(LogTemp, Display, TEXT("JEEBGRASS step %d %d: %d -> %d at %.1f m, t %.3f"), JobId.X, JobId.Y, Was->Tier, JobTier,
				FVector2D::Distance(FVector2D((JobId.X + 0.5f) * CellSize, (JobId.Y + 0.5f) * CellSize), FVector2D(P.X, P.Y)) / 100.f, GetWorld()->GetTimeSeconds());
		}
	}
#endif
	DropCell(JobId);
	FCell Cell; Cell.Tier = JobTier;
	if (JobXf.Num() > 0)
	{
		UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this);
		Comp->SetStaticMesh(JobTier <= (OldSteps() ? 0 : 1) ? NearMesh : FarMesh);
		Comp->SetupAttachment(RootComponent);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->bAffectDynamicIndirectLighting = false;
		Comp->bAffectDistanceFieldLighting = false;
		Comp->SetCullDistances(0, FMath::RoundToInt(Reach * Stretch() + 500.f));
		Comp->SetNumCustomDataFloats(1);   // (each tuft's long-grass share: M_Grass's golden tips)
		Comp->RegisterComponent();
		Comp->AddInstances(JobXf, false, true);
		for (int32 i = 0; i < JobTall.Num(); ++i) if (JobTall[i] > 0.f) Comp->SetCustomDataValue(i, 0, JobTall[i], false);
		Comp->MarkRenderStateDirty();
		Cell.Comp = Comp;
	}
	if (JobFlowerXf.Num() > 0)
	{
		UInstancedStaticMeshComponent* F = NewObject<UInstancedStaticMeshComponent>(this);
		F->SetStaticMesh(FlowerMesh);
		F->SetupAttachment(RootComponent);
		F->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		F->SetCastShadow(false);
		F->bAffectDynamicIndirectLighting = false;
		F->bAffectDistanceFieldLighting = false;
		F->SetCullDistances(0, 2800);
		F->SetNumCustomDataFloats(3);   // (the head's colour, M_GrassFlower)
		F->RegisterComponent();
		F->AddInstances(JobFlowerXf, false, true);
		for (int32 i = 0; i < JobFlowerCol.Num(); ++i)
		{
			const float C[3] = { JobFlowerCol[i].R, JobFlowerCol[i].G, JobFlowerCol[i].B };
			F->SetCustomData(i, MakeArrayView(C, 3), false);
		}
		F->MarkRenderStateDirty();
		Cell.Flowers = F;
		static int32 Logged = 0;
		if (Logged++ < 6) UE_LOG(LogTemp, Log, TEXT("JEEBGRASS square %d %d tier %d: %d tufts, %d flowers (first at %s, colour %s)"), JobId.X, JobId.Y, JobTier, JobXf.Num(), JobFlowerXf.Num(), *JobFlowerXf[0].GetLocation().ToString(), *JobFlowerCol[0].ToString());
	}
	// the realistic clumps, one set per kind (drawn only out to where they have sunk away)
	for (int32 k = 0; k < JobKindXf.Num() && k < Kinds.Num(); ++k)
	{
		if (JobKindXf[k].Num() == 0) continue;
		UInstancedStaticMeshComponent* P = NewObject<UInstancedStaticMeshComponent>(this);
		P->SetStaticMesh(Kinds[k].Mesh);
		if (Kinds[k].Mid) P->SetMaterial(0, Kinds[k].Mid);
		P->SetupAttachment(RootComponent);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->SetCastShadow(false);
		P->bAffectDynamicIndirectLighting = false;
		P->bAffectDistanceFieldLighting = false;
		P->SetCullDistances(0, FMath::RoundToInt(PackFadeEnd + 200.f));
		if (JobTier >= (OldSteps() ? 2 : 4) && Kinds[k].Mesh->GetNumLODs() > 1) { P->bOverrideMinLOD = true; P->MinLOD = 1; }   // (the far ring: the pack's simpler model)
		P->RegisterComponent();
		P->AddInstances(JobKindXf[k], false, true);
		Cell.Packed.Add(P);
		JobKindXf[k].Reset();
	}
	// the moor's heather (2026-11-25): one more set, dropped with the square's clumps
	if (JobHeatherXf.Num() > 0 && HeatherMesh)
	{
		UInstancedStaticMeshComponent* F = NewObject<UInstancedStaticMeshComponent>(this);
		F->SetStaticMesh(HeatherMesh);
		F->SetupAttachment(RootComponent);
		F->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		F->SetCastShadow(false);
		F->bAffectDynamicIndirectLighting = false;
		F->bAffectDistanceFieldLighting = false;
		F->SetCullDistances(0, 4200);
		F->SetNumCustomDataFloats(3);   // (the flowers' colour, M_GrassFlower)
		F->RegisterComponent();
		F->AddInstances(JobHeatherXf, false, true);
		for (int32 i = 0; i < JobHeatherCol.Num(); ++i)
		{
			const float C[3] = { JobHeatherCol[i].R, JobHeatherCol[i].G, JobHeatherCol[i].B };
			F->SetCustomData(i, MakeArrayView(C, 3), false);
		}
		F->MarkRenderStateDirty();
		Cell.Packed.Add(F);
	}
	JobHeatherXf.Reset(); JobHeatherCol.Reset();
	Cells.Add(JobId, Cell);
	JobXf.Reset(); JobTall.Reset(); JobFlowerXf.Reset(); JobFlowerCol.Reset();
}

// (test only, -GrassLineup in MoveLab, 2026-11-15: every grass type of the Fab packs in rows a metre apart, for choosing which to grow.
// X -400: Grass Library grass_01..12 (variant 03) then lowGrass_01..10 (variant 02); X -1000: Meadow Flowers 01..20 (the biggest
// clump of each); X -1600: Open World Foliage grass 01_01..02_03 (variants 01 and 04) then flowers 01..06 (variant 02). Y from -1000.)
void JeebGrassLineup(UWorld* W)
{
#if !UE_BUILD_SHIPPING
	if (!W || !FParse::Param(FCommandLine::Get(), TEXT("GrassLineup"))) return;
	const float Z = JeebTerrain::HeightAt(0.f, 0.f);
	auto Row = [&](float X, const TArray<FString>& Paths)
	{
		for (int32 i = 0; i < Paths.Num(); ++i)
		{
			UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *(Paths[i] + TEXT(".") + FPaths::GetBaseFilename(Paths[i])));
			if (!M) { UE_LOG(LogTemp, Warning, TEXT("JEEBGRASS lineup: no %s"), *Paths[i]); continue; }
			if (AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(FVector(X, -1000.f + i * 100.f, Z), FRotator::ZeroRotator))
			{
				A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				A->GetStaticMeshComponent()->SetStaticMesh(M);
			}
		}
	};
	const int32 FlowerVariants[20] = { 4, 4, 5, 4, 5, 4, 4, 4, 6, 7, 4, 4, 7, 6, 5, 5, 6, 4, 5, 3 };
	TArray<FString> A, B, C;
	for (int32 t = 1; t <= 12; ++t) A.Add(FString::Printf(TEXT("/Game/PN_GrassLibrary/Meshes/grassMesh/grass_%02d_03_mesh"), t));
	for (int32 t = 1; t <= 10; ++t) A.Add(FString::Printf(TEXT("/Game/PN_GrassLibrary/Meshes/grassMesh/lowGrass_%02d_02_SM"), t));
	for (int32 t = 1; t <= 20; ++t) B.Add(FString::Printf(TEXT("/Game/PN_MeadowFlowers/Meshes/flowerMesh/flower_%02d_%02d"), t, FlowerVariants[t - 1]));
	for (int32 g = 1; g <= 2; ++g) for (int32 h = 1; h <= 3; ++h) for (int32 v : { 1, 4 }) C.Add(FString::Printf(TEXT("/Game/PN_OpenWorldFoliage/Meshes/openWorld_grass_%02d_%02d_%02d"), g, h, v));
	for (int32 t = 1; t <= 6; ++t) C.Add(FString::Printf(TEXT("/Game/PN_OpenWorldFoliage/Meshes/openWorld_flower_%02d_02"), t));
	Row(-400.f, A); Row(-1000.f, B); Row(-1600.f, C);
	UE_LOG(LogTemp, Log, TEXT("JEEBGRASS lineup: %d + %d + %d meshes"), A.Num(), B.Num(), C.Num());
#endif
}
