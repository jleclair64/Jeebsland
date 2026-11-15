#include "Meadow.h"
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

// Which grass the meadow grows (2026-11-15, owner: "lean towards realism with the grass"; the packs are the owner's Fab library,
// git-ignored, set up by Tools/BuildGrassPacks.ps1). Changing any of these regrows the grass round you at once (a short hitch).
static TAutoConsoleVariable<int32> CVarGrassPack(TEXT("jeeb.Grass.Pack"), 1,
	TEXT("Meadow grass: 0 our painted cards, 1 Grass Library + Meadow Flowers (realistic, Fab), 2 Open World Foliage (realistic, cheaper, Fab)"));
static TAutoConsoleVariable<float> CVarGrassDensity1(TEXT("jeeb.Grass.Density1"), 12.0f, TEXT("Grass Library clumps per square metre close by"));
static TAutoConsoleVariable<float> CVarGrassDensity2(TEXT("jeeb.Grass.Density2"), 11.0f, TEXT("Open World Foliage clumps per square metre close by"));
static TAutoConsoleVariable<float> CVarGrassPackFade(TEXT("jeeb.Grass.PackFade"), 5000.f, TEXT("cm: the realistic grass has sunk into the ground by here (it starts 8 m nearer; 50 m since 2026-11-15, owner: twice as far)"));
static TAutoConsoleVariable<int32> CVarGrassFarCards(TEXT("jeeb.Grass.FarCards"), 0, TEXT("1: our painted cards rise where the realistic grass sinks, carrying the meadow to 50 m; 0: painted ground only beyond it"));
static TAutoConsoleVariable<FString> CVarGrassTint1(TEXT("jeeb.Grass.Tint1"), TEXT(""), TEXT("Grass Library blades' colour multiplier, \"r g b\" (live; empty: the palette's GrassPack1, Content/Data/Palette.ini)"));
static TAutoConsoleVariable<FString> CVarGrassTint2(TEXT("jeeb.Grass.Tint2"), TEXT(""), TEXT("Open World Foliage blades' colour multiplier, \"r g b\" (live; empty: the palette's GrassPack2)"));
static TAutoConsoleVariable<float> CVarGrassField(TEXT("jeeb.Grass.PackField"), 1.f, TEXT("How much of our field colour (patches, swathes, dry patches) the realistic blades take, 0..1 (live)"));
static TAutoConsoleVariable<float> CVarGrassPackDesat(TEXT("jeeb.Grass.PackDesat"), 0.3f, TEXT("How far the realistic blades are pulled toward grey (live)"));
static TAutoConsoleVariable<float> CVarGrassGreenDry(TEXT("jeeb.Grass.GreenDry"), 1.f, TEXT("How far the Grass Library's yellow stems and seed heads are pulled to green, 0..1 (live; owner: they stood out)"));
static TAutoConsoleVariable<float> CVarGrassFlowers(TEXT("jeeb.Grass.PackFlowers"), 0.45f, TEXT("Flower clumps against our single flowers (a clump holds several)"));

namespace
{
	// (the square's size and how far the grass reaches are AMeadow's CellSize and Reach: 15 m and 50 m since 2026-11-14, was 7.5 m and 95 m)
	constexpr float FieldR = 9000.f;
	constexpr float NearDensity = 0.0012f;   // full tufts per square cm, close up
	constexpr float FarDensity = 0.0006f;    // thinner, cheaper tufts further out
	constexpr float NearDist = 1100.f;
	inline float TierDensity(int32 T) { return T == 0 ? 1.f : (T == 1 ? 0.55f : 0.3f); }
	inline float TierSize(int32 T) { return T == 0 ? 1.f : (T == 1 ? 1.25f : 1.6f); }

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
int32 AMeadow::TierFor(float Dist) const { return Dist < Tier0 ? 0 : (Dist < Tier1 ? 1 : 2); }
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

// A bush as a dome of leafy cards (the leaf picture is painted on them by MakeGrassCards.bat)
static UStaticMesh* MakeBushMesh(UObject* Outer, UMaterialInterface* Mat)
{
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Mat, FName("Leaf")));

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
	GroupSlot[Group] = FName("Leaf");

	auto Tri = [&](const FVector& A, const FVector& B, const FVector& C, const FVector2D& UA, const FVector2D& UB, const FVector2D& UC, const FVector& N)
	{
		const FVector Pts[3] = { A, B, C };
		const FVector2D Uv[3] = { UA, UB, UC };
		TArray<FVertexInstanceID> Inst;
		for (int32 k = 0; k < 3; ++k)
		{
			const FVertexID V = MD.CreateVertex();
			Pos[V] = FVector3f(Pts[k]);
			const FVertexInstanceID I = MD.CreateVertexInstance(V);
			Nrm[I] = FVector3f(N);
			Tan[I] = FVector3f(1.f, 0.f, 0.f);
			Sign[I] = 1.f;
			UVs[I] = FVector2f(float(Uv[k].X), float(Uv[k].Y));
			Inst.Add(I);
		}
		MD.CreatePolygon(Group, Inst);
	};

	FRandomStream Rand(77);
	const int32 Cards = 30;
	for (int32 c = 0; c < Cards; ++c)
	{
		const float Yaw = (c + Rand.FRandRange(-0.4f, 0.4f)) / Cards * 2.f * PI * 2.f;      // wraps round twice, so cards fan out in all directions
		const FVector D(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f);
		const FVector Side(-D.Y, D.X, 0.f);
		const float Lean = Rand.FRandRange(0.35f, 1.15f);                                      // radians from vertical
		const FVector Up = (D * FMath::Sin(Lean) + FVector(0.f, 0.f, FMath::Cos(Lean))).GetSafeNormal();
		const float Out = (c % 3 == 0) ? Rand.FRandRange(0.f, 12.f) : Rand.FRandRange(10.f, 42.f);
		const FVector Base = D * Out + FVector(0.f, 0.f, Rand.FRandRange(0.f, 28.f));
		const float W = Rand.FRandRange(85.f, 115.f), H = Rand.FRandRange(75.f, 105.f);
		const FVector N = (D * 0.55f + FVector(0.f, 0.f, 1.f)).GetSafeNormal();
		const FVector BL = Base - Side * (W * 0.5f), BR = Base + Side * (W * 0.5f);
		Tri(BL, BR, BR + Up * H, FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), N);
		Tri(BL, BR + Up * H, BL + Up * H, FVector2D(0, 1), FVector2D(1, 0), FVector2D(0, 0), N);
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
		FlowerMesh = MakeFlowerMesh(this, FlowerMat);
	else
		UE_LOG(LogTemp, Warning, TEXT("M_GrassFlower is missing: run Tools/BuildMaterials.ps1. No meadow flowers."));
	PlaceBushesAndFlowers();
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
		K.Role = RoleName == TEXT("long") ? 1 : (RoleName == TEXT("flower") ? 2 : 0);
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
	PackFadeEnd = FMath::Max(600.f, CVarGrassPackFade.GetValueOnGameThread());
	PackFadeStart = FMath::Max(200.f, PackFadeEnd - 800.f);
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
	const int32 Span = FMath::CeilToInt(Reach / CellSize) + 1;
	for (int32 j = Cy - Span; j <= Cy + Span; ++j)
		for (int32 i = Cx - Span; i <= Cx + Span; ++i)
		{
			const FVector2D Ctr((i + 0.5f) * CellSize, (j + 0.5f) * CellSize);
			const float D = FVector2D::Distance(Ctr, Me);
			if (D > Reach || !GrowsHere(Ctr.X, Ctr.Y, CellSize * 0.75f)) continue;
			StartJob(FIntPoint(i, j), TierFor(D));
			StepJob(1.0e6);
		}
}

void AMeadow::Tick(float Dt)
{
	Super::Tick(Dt);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !NearMesh) return;
	if (GrassMid) GrassMid->SetScalarParameterValue(TEXT("Dusk"), ADayNight::Dusk);
	const FVector2D Me(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y);
	{
		// the grass setting (jeeb.Grass.*): a change regrows everything round you; the first tick grows the start (the woods are
		// planted by now, so grass can thin under them)
		static FString Was;
		const FString Now = FString::Printf(TEXT("%d %.2f %.2f %.0f %d"), CVarGrassPack.GetValueOnGameThread(), CVarGrassDensity1.GetValueOnGameThread(),
			CVarGrassDensity2.GetValueOnGameThread(), CVarGrassPackFade.GetValueOnGameThread(), CVarGrassFarCards.GetValueOnGameThread());
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
					K.Mid->SetVectorParameterValue(TEXT("JeebTint"), bFlower ? FLinearColor::White : Tint);
					K.Mid->SetScalarParameterValue(TEXT("JeebField"), CVarGrassField.GetValueOnGameThread() * (bFlower ? 0.3f : 1.f));
					K.Mid->SetScalarParameterValue(TEXT("JeebDesat"), bFlower ? 0.f : CVarGrassPackDesat.GetValueOnGameThread());
					K.Mid->SetScalarParameterValue(TEXT("JeebGreenDry"), CVarGrassGreenDry.GetValueOnGameThread());
				}
		}
	}

	TArray<FIntPoint> Far;
	for (const TPair<FIntPoint, FCell>& P : Cells)
	{
		const FVector2D Ctr((P.Key.X + 0.5f) * CellSize, (P.Key.Y + 0.5f) * CellSize);
		if (FVector2D::Distance(Ctr, Me) > Reach + CellSize) Far.Add(P.Key);
	}
	for (const FIntPoint& Id : Far) DropCell(Id);

	// Build toward where you are heading: distances are measured from a point a second and a half ahead of you
	const FVector Vel = Pawn->GetVelocity();
	const FVector2D Ahead = Me + FVector2D(Vel.X, Vel.Y) * 1.5f;

	struct FWant { FIntPoint Id; int32 Tier; float D; };
	TArray<FWant> Want;
	const int32 Cx = FMath::FloorToInt(Me.X / CellSize), Cy = FMath::FloorToInt(Me.Y / CellSize);
	const int32 Span = FMath::CeilToInt(Reach / CellSize) + 1;
	for (int32 j = Cy - Span; j <= Cy + Span; ++j)
		for (int32 i = Cx - Span; i <= Cx + Span; ++i)
		{
			const FVector2D Ctr((i + 0.5f) * CellSize, (j + 0.5f) * CellSize);
			const float D = FVector2D::Distance(Ctr, Me);
			if (D > Reach || !GrowsHere(Ctr.X, Ctr.Y, CellSize * 0.75f)) continue;   // (the whole world's land, 2026-11-12)
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
	JobRand.Initialize(int32(uint32(Id.X) * 73856093u ^ uint32(Id.Y) * 19349663u ^ uint32(Tier) * 83492791u));
	JobCount = FMath::RoundToInt(CellSize * CellSize * NearDensity * TierDensity(Tier));
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
	TArray<int32> Of[3]; TArray<float> OfW[3];   // (the pack's kinds by role: short, long, flower)
	for (int32 k = 0; k < Kinds.Num(); ++k) { Of[Kinds[k].Role].Add(k); OfW[Kinds[k].Role].Add(Kinds[k].Weight); }
	const float FlowerShare = CVarGrassFlowers.GetValueOnGameThread();
	static const float TierChance[3] = { 1.f, 0.85f, 0.6f }, TierWide[3] = { 1.f, 1.1f, 1.3f };
	while (JobDone < JobCount)
	{
		if ((JobDone & 15) == 15 && FPlatformTime::Seconds() > End) return;   // (checked first, so a tuft skipped below never misses it; the next call picks up here)
		++JobDone;
		const float X = (JobId.X + JobRand.FRand()) * CellSize, Y = (JobId.Y + JobRand.FRand()) * CellSize;
		const float Sxy = JobRand.FRandRange(0.85f, 1.3f) * TierSize(JobTier);
		const float Yaw = JobRand.FRandRange(0.f, 360.f), Sz = JobRand.FRandRange(0.75f, 1.3f);
		float Amount, Wear;
		SampleJob(X, Y, Amount, Wear);
		const float Keep = JobRand.FRand();
		if (Keep >= Amount || !GrowsHere(X, Y)) continue;   // thinning out over rock, mud and sand
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
				const float Tall = LongGrassNoise(X, Y) * LongGrassRoom(X, Y, Pc, Near, Cov, Wear);
				Short *= 0.82f * (1.f + 1.3f * Tall * JobRand.FRandRange(0.8f, 1.15f));
				const float Wide = Sxy * FMath::Lerp(0.75f, 1.f, FMath::Min(Short, 1.f)) * (1.f + 0.35f * Near) * (1.f + 0.12f * Tall);   // and wider by buildings, so the tufts overlap into one mat and the individual cards disappear
				if (bThin) continue;
				const float Gz = JeebTerrain::HeightAt(X, Y);
				if (Pack == 0 || bFarCards)
				{
					JobXf.Add(FTransform(FRotator(0.f, Yaw, 0.f), FVector(X, Y, Gz + 4.f), FVector(Wide, Wide, Sz * (1.f + 0.1f * JobTier) * Short)));
					JobTall.Add(Tall);
				}
				if (Pack != 0)
				{
					// a realistic clump at about this spot's card height (48 cm card x its scale), keeping its own proportions: long-patch kinds
					// where the long grass is (more often toward a patch's heart), the short field kinds elsewhere
					// (2026-11-15, for speed: past about 17 m fewer, wider clumps of fewer kinds, so the far ring costs less but still reads as grass)
					if (JobRand.FRand() < PackChance * TierChance[FMath::Min(JobTier, 2)])
					{
						const int32 Kr = (Of[1].Num() > 0 && Tall > 0.2f && JobRand.FRand() < Tall * 1.25f) ? 1 : 0;
						const int32 KindsHere = JobTier == 0 ? Of[Kr].Num() : FMath::Min(Of[Kr].Num(), JobTier == 1 ? 2 : 1);
						TArray<int32> FarOf(Of[Kr].GetData(), KindsHere); TArray<float> FarW(OfW[Kr].GetData(), KindsHere);
						const int32 K = PickKind(FarOf, FarW, X, Y, JobRand);
						const FKind& Kd = Kinds[K];
						const float TargetH = 48.f * Sz * (1.f + 0.1f * JobTier) * Short * Kd.Height;
						const float Xy = FMath::Clamp(TargetH / Kd.NaturalH, 0.75f, 1.3f) * Kd.Width * JobRand.FRandRange(0.85f, 1.15f) * (1.f + 0.25f * Near) * TierWide[FMath::Min(JobTier, 2)];
						const float Zs = FMath::Clamp(TargetH / Kd.NaturalH, Xy * 0.55f, Xy * 1.6f);   // (never squashed or stretched far from its own shape)
						JobKindXf[K].Add(FTransform(FRotator(JobRand.FRandRange(-4.f, 4.f), JobRand.FRandRange(0.f, 360.f), JobRand.FRandRange(-4.f, 4.f)), FVector(X, Y, Gz), FVector(Xy, Xy, Zs)));
					}
					// a flower clump now and then (a clump holds several, so fewer than our single flowers), one kind per drift
					if (Of[2].Num() > 0 && JobTier <= 1 && Pc > 60.f && Wear < 0.3f && JobRand.FRand() < FlowerChance(X, Y) * (1.f - Cov) * FlowerShare)
					{
						const float Fn = FMath::Clamp((FMath::PerlinNoise2D(FVector2D(X / 1700.f - 21.3f, Y / 1700.f + 5.1f)) * 0.5f + 0.5f) * 1.5f - 0.25f, 0.f, 0.999f);
						const int32 K = JobRand.FRand() < 0.85f ? Of[2][FMath::FloorToInt(Fn * Of[2].Num())] : Of[2][JobRand.RandRange(0, Of[2].Num() - 1)];
						const float S = JobRand.FRandRange(0.85f, 1.15f) * Kinds[K].Width;
						const float Fx = X + JobRand.FRandRange(-20.f, 20.f), Fy = Y + JobRand.FRandRange(-20.f, 20.f);
						JobKindXf[K].Add(FTransform(FRotator(0.f, JobRand.FRandRange(0.f, 360.f), 0.f), FVector(Fx, Fy, JeebTerrain::HeightAt(Fx, Fy)), FVector(S, S, S * Kinds[K].Height)));
					}
					continue;
				}
				// a flower beside this tuft now and then, its head just above the blades round it (tiers 0 and 1: within about 27 m;
				// none on paths, trampled ground or under trees)
				if (FlowerMesh && JobTier <= 1 && Pc > 60.f && Wear < 0.3f && JobRand.FRand() < FlowerChance(X, Y) * (1.f - Cov))
				{
					const float Fx = X + JobRand.FRandRange(-20.f, 20.f), Fy = Y + JobRand.FRandRange(-20.f, 20.f);
					const float Up = FMath::Clamp(Sz * Short * 1.75f * JobRand.FRandRange(0.9f, 1.1f), 0.7f, 2.4f);   // (the head at the blades' tips, 30 cm x Up against a 48 cm card)
					const float S = JobRand.FRandRange(0.85f, 1.2f);
					JobFlowerXf.Add(FTransform(FRotator(JobRand.FRandRange(-8.f, 8.f), JobRand.FRandRange(0.f, 360.f), JobRand.FRandRange(-8.f, 8.f)), FVector(Fx, Fy, JeebTerrain::HeightAt(Fx, Fy) + 1.f), FVector(S, S, Up)));
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
	DropCell(JobId);
	FCell Cell; Cell.Tier = JobTier;
	if (JobXf.Num() > 0)
	{
		UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this);
		Comp->SetStaticMesh(JobTier == 0 ? NearMesh : FarMesh);
		Comp->SetupAttachment(RootComponent);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->bAffectDynamicIndirectLighting = false;
		Comp->bAffectDistanceFieldLighting = false;
		Comp->SetCullDistances(0, FMath::RoundToInt(Reach + 500.f));
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
		if (JobTier >= 2 && Kinds[k].Mesh->GetNumLODs() > 1) { P->bOverrideMinLOD = true; P->MinLOD = 1; }   // (the far ring: the pack's simpler model)
		P->RegisterComponent();
		P->AddInstances(JobKindXf[k], false, true);
		Cell.Packed.Add(P);
		JobKindXf[k].Reset();
	}
	Cells.Add(JobId, Cell);
	JobXf.Reset(); JobTall.Reset(); JobFlowerXf.Reset(); JobFlowerCol.Reset();
}

void AMeadow::PlaceBushesAndFlowers()
{
	FRandomStream Rand(2024);
	auto Spot = [&](float& X, float& Y)
	{
		const float A = Rand.FRandRange(0.f, 2.f * PI), D = FMath::Sqrt(Rand.FRand()) * FieldR;
		X = FMath::Cos(A) * D; Y = FMath::Sin(A) * D;
	};

	// bushes: leafy cards in a dome, lit and swaying like the grass
	UMaterialInterface* BushMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Bush.M_Bush"));
	if (!BushMat) UE_LOG(LogTemp, Warning, TEXT("M_Bush is missing: run MakeGrassCards.bat once. No bushes will grow."));
	UInstancedStaticMeshComponent* Bushes = nullptr;
	if (BushMat)
	{
		Bushes = NewObject<UInstancedStaticMeshComponent>(this);
		Bushes->SetStaticMesh(MakeBushMesh(this, BushMat));
		Bushes->SetupAttachment(RootComponent);
		Bushes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Bushes->SetCastShadow(false);
		Bushes->bAffectDynamicIndirectLighting = false;
		Bushes->bAffectDistanceFieldLighting = false;
		Bushes->SetCullDistances(0, 10000);
		Bushes->RegisterComponent();
	}
	const int32 BushTries = 960;
	for (int32 n = 0; n < BushTries && Bushes; ++n)
	{
		float X, Y; Spot(X, Y);
		if (JeebTerrain::PathClearance(X, Y) < 140.f || JeebTerrain::NoGroundCover(X, Y) || JeebTerrain::TreeBlocked(X, Y)) continue;
		const float Size = Rand.FRandRange(0.8f, 1.3f);
		Bushes->AddInstance(FTransform(FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f), FVector(X, Y, JeebTerrain::HeightAt(X, Y) - 2.f), FVector(Size, Size, Size * Rand.FRandRange(0.85f, 1.15f))));
	}

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
