#include "Bushes.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "OakHill.h"
#include "OakForest.h"
#include "SteeleCreek.h"
#include "Canopy.h"
#include "Fill.h"
#include "TreeLook.h"
#include "JeebWater.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"

// The bushes (Bushes.h). Where, how many and which: all here.
namespace
{
	TAutoConsoleVariable<float> CVarBushDensity(TEXT("jeeb.Bush.Density"), 1.f,
		TEXT("How many of the wild bushes grow (forest edges, clearings, river banks and lake shores): 0 none, 1 as placed (2026-11-18), 2 twice as many. Rebuilds them live. The towns' bushes are the world fill's (FillUses.csv)."), ECVF_Default);
	TAutoConsoleVariable<float> CVarBushReach(TEXT("jeeb.Bush.Reach"), 1.f,
		TEXT("Draw distance of the wild bushes, times their own (45-70 m). Live."), ECVF_Default);
	TAutoConsoleVariable<float> CVarBushShade(TEXT("jeeb.Bush.Shade"), 1.0f,
		TEXT("The bushes' leaf colour: the trees' green (jeeb.Trees.DzGreen) times this (1 = exactly the trees' colour). Reaches the fill's town bushes too. Live."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarBushShadows(TEXT("jeeb.Bush.Shadows"), 0,
		TEXT("Shadows from the wild bushes: 0 none (cheap, as the old bushes), 1 on. Live."), ECVF_Default);

	TWeakObjectPtr<ABushes> GBushes;

	enum EBush : uint8 { B_Full, B_Small, B_Fine, B_Low, B_Flower1, B_Flower2, B_Reed1, B_Reed2, B_Pad, B_PadFlower, B_PadTwin, B_Num };
	struct FBushDef { const TCHAR* Path; float Reach; float Foot; const TCHAR* Title; };
	const FBushDef Defs[B_Num] = {
		{ TEXT("/Game/Bushes/SM_BushFull.SM_BushFull"), 7000.f, 100.f, TEXT("full") },
		{ TEXT("/Game/Bushes/SM_BushSmall.SM_BushSmall"), 5000.f, 60.f, TEXT("small") },
		{ TEXT("/Game/Bushes/SM_BushFine.SM_BushFine"), 7000.f, 85.f, TEXT("fine") },
		{ TEXT("/Game/Bushes/SM_BushLow.SM_BushLow"), 4500.f, 70.f, TEXT("low") },
		// (the flowering bush: a small bush with the meadow's own flowers, the realistic grass pack's, through it)
		{ TEXT("/Game/PN_MeadowFlowers/Meshes/flowerMesh/flower_14_06.flower_14_06"), 3500.f, 0.f, TEXT("flower a") },
		{ TEXT("/Game/PN_MeadowFlowers/Meshes/flowerMesh/flower_17_05.flower_17_05"), 3500.f, 0.f, TEXT("flower b") },
		// (2026-11-21, owner: reeds where the bushes stood in the water) the realistic grass pack's two tallest, narrowest clumps (0.9 m), drawn
		// 1.4-1.9 times as tall in the shallows and the wet edge of the rivers, lakes and the sea's inlets; their own wind, as the meadow's grass
		{ TEXT("/Game/PN_GrassLibrary/Meshes/grassMesh/grass_03_01_mesh.grass_03_01_mesh"), 4500.f, 0.f, TEXT("reed a") },
		{ TEXT("/Game/PN_GrassLibrary/Meshes/grassMesh/grass_12_11_mesh.grass_12_11_mesh"), 4500.f, 0.f, TEXT("reed b") },
		// lily pads on the calmer, deeper water near the banks (2026-11-22, owner: real ones, from Meshy; Tools/BuildLilyPads.ps1): a notched pad, a pad with a
		// pink and white flower, and two pads with a bud. Each 100 cm across, its underside at the origin.
		{ TEXT("/Game/LilyPads/SM_LilyPad.SM_LilyPad"), 4500.f, 0.f, TEXT("lily pad") },
		{ TEXT("/Game/LilyPads/SM_LilyFlower.SM_LilyFlower"), 4500.f, 0.f, TEXT("lily pad flower") },
		{ TEXT("/Game/LilyPads/SM_LilyTwin.SM_LilyTwin"), 4500.f, 0.f, TEXT("lily pads twin") },
	};

	// Where to look: round Oak Hill's forest, Blackrock's woods round the square, Steele Creek, and the land between Oak Hill and Steele
	// Creek (its lake). Cheap tests only.
	bool InRegion(const FVector2D& P)
	{
		const FVector2D Oak(JeebOakHill::PosX, JeebOakHill::PosY), Steele(JeebSteele::PosX, JeebSteele::PosY);
		if (FVector2D::DistSquared(P, Oak) < FMath::Square(JeebOakForest::Reach + 1500.f)) return true;
		if (P.SizeSquared() < FMath::Square(32000.f)) return true;
		if (FVector2D::DistSquared(P, Steele) < FMath::Square(26000.f)) return true;
		const FVector2D AB = Steele - Oak;
		const float T = FMath::Clamp(FVector2D::DotProduct(P - Oak, AB) / AB.SizeSquared(), 0.f, 1.f);
		return FVector2D::DistSquared(P, Oak + AB * T) < FMath::Square(18000.f);
	}

	FLinearColor LeafTint()
	{
		FLinearColor Green(0.2017f, 0.2723f, 0.07f);   // (jeeb.Trees.DzGreen's default, OakForest.cpp)
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.Trees.DzGreen"))) Green = JeebTreeLook::ParseRGB(V->GetString(), Green);
		const float S = FMath::Max(0.f, CVarBushShade.GetValueOnGameThread());
		return JeebTreeLook::Saturate(FLinearColor(Green.R * S, Green.G * S, Green.B * S, 1.f));
	}
}

ABushes::ABushes()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);
}

void ABushes::BeginPlay()
{
	Super::BeginPlay();
	if (!GetActorLocation().IsNearlyZero()) SetActorLocation(FVector::ZeroVector);
	GBushes = this;
	static bool bHooked = false;
	if (!bHooked)
	{
		bHooked = true;
		CVarBushDensity->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { if (GBushes.IsValid()) GBushes->Rebuild(); }));
		for (IConsoleVariable* V : { CVarBushReach.AsVariable(), CVarBushShade.AsVariable(), CVarBushShadows.AsVariable() })
			V->OnChangedDelegate().AddLambda([](IConsoleVariable*) { if (GBushes.IsValid()) GBushes->ApplyLook(); });
		if (IConsoleVariable* G = IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.Trees.DzGreen")))
			G->OnChangedDelegate().AddLambda([](IConsoleVariable*) { if (GBushes.IsValid()) GBushes->ApplyLook(); });
		JeebTreeLook::OnChanged().AddLambda([]() { if (GBushes.IsValid()) GBushes->ApplyLook(); });
	}
	Rebuild();
}

void ABushes::Rebuild()
{
	const double T0 = FPlatformTime::Seconds();
	for (UHierarchicalInstancedStaticMeshComponent* C : Sets) if (C) C->DestroyComponent();
	Sets.Reset(); SetReach.Reset();
	UWorld* World = GetWorld();
	if (!World || !JeebOakHill::Enabled()) return;

	UStaticMesh* Meshes[B_Num] = {};
	for (int32 k = 0; k < B_Num; ++k)
	{
		Meshes[k] = LoadObject<UStaticMesh>(nullptr, Defs[k].Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
		if (!Meshes[k]) UE_LOG(LogTemp, Warning, TEXT("JEEBBUSH missing %s (run Tools/BuildBushes.ps1): left out"), Defs[k].Path);
	}
	TArray<FTransform> Xf[B_Num];
	const float Density = FMath::Clamp(CVarBushDensity.GetValueOnGameThread(), 0.f, 4.f);
	int32 Where[3] = {}, Tried = 0, Blocked = 0;

	// A jittered 5 m lattice; each cell draws its own numbers from its own seed, so a skip never moves another cell's bush
	const float Cell = 500.f;
	const FVector2D Lo(JeebSteele::PosX - 27000.f, -JeebOakForest::Reach + JeebOakHill::PosY - 1500.f);
	const FVector2D Hi(JeebOakHill::PosX + JeebOakForest::Reach + 1500.f, JeebOakHill::PosY + JeebOakForest::Reach + 1500.f);
	const int32 NI = FMath::CeilToInt((Hi.X - Lo.X) / Cell), NJ = FMath::CeilToInt((Hi.Y - Lo.Y) / Cell);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebBush), false);
	const FVector2D Ring6[6] = { {1.f, 0.f}, {0.5f, 0.866f}, {-0.5f, 0.866f}, {-1.f, 0.f}, {-0.5f, -0.866f}, {0.5f, -0.866f} };
	for (int32 i = 0; i < NI && Density > 0.f; ++i)
		for (int32 j = 0; j < NJ; ++j)
		{
			FRandomStream R(int32(HashCombine(GetTypeHash(i * 7919 + 17), GetTypeHash(j * 104729 + 3))) ^ 0x5B05);
			const float Jx = R.FRand(), Jy = R.FRand(), Roll = R.FRand(), Pick = R.FRand(), Sc = R.FRand(), Yaw = R.FRandRange(0.f, 360.f);
			const FVector2D P(Lo.X + (i + 0.1f + 0.8f * Jx) * Cell, Lo.Y + (j + 0.1f + 0.8f * Jy) * Cell);
			if (!InRegion(P)) continue;
			if (FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y)) < JeebTerrain::GroundHalf + 500.f) continue;   // (the original square: the fill's)
			if (JeebSteele::InBox(P.X, P.Y, -4000.f)) continue;   // (Steele Creek's middle)
			// (2026-11-21, owner: "Why are there bushes in the water?") A bush is 2-4 m across and only its centre had to be dry, so bank bushes
			// stood half in the river. Now no water within 3.5 m of the centre; the water's edge itself gets reeds instead (below)
			if (JeebWorld::InWater(P.X, P.Y) || JeebWorld::NearRiver(P.X, P.Y, 350.f)) continue;
			{
				bool bWet = false;
				for (int32 k = 0; k < 6 && !bWet; ++k) bWet = JeebWorld::InWater(P.X + Ring6[k].X * 350.f, P.Y + Ring6[k].Y * 350.f);
				if (bWet) continue;
			}
			// on a bank: water within 6 m
			bool bShore = false;
			for (int32 k = 0; k < 6 && !bShore; k += 2) bShore = JeebWorld::InWater(P.X + Ring6[k].X * 600.f, P.Y + Ring6[k].Y * 600.f);
			if (!bShore) bShore = JeebWorld::InWater(P.X - 600.f, P.Y);
			// at a forest edge: some open ground and some canopy within 7 m; under the trees: canopy all round
			const float C0 = JeebCanopy::Cover(P.X, P.Y);
			int32 Open = 0, Shade = 0;
			for (const FVector2D& D : Ring6)
			{
				const float C = JeebCanopy::Cover(P.X + D.X * 700.f, P.Y + D.Y * 700.f);
				Open += C < 0.12f; Shade += C > 0.45f;
			}
			const bool bEdge = Open >= 2 && Shade >= 2 && C0 < 0.75f, bUnder = Open == 0 && C0 > 0.35f;
			const bool bTown = JeebOakHill::InBox(P.X, P.Y, 1500.f);
			if (bTown && !bShore) continue;   // (the town's bushes are the fill's, in its plots)
			const int32 Kind3 = bShore ? 0 : bEdge ? 1 : bUnder ? 2 : -1;
			if (Kind3 < 0) continue;   // (none out in the open meadow)
			const float Want = (Kind3 == 0 ? 0.26f : Kind3 == 1 ? 0.2f : 0.025f) * Density;
			if (Roll >= Want) continue;
			++Tried;
			if (JeebTerrain::PathClearance(P.X, P.Y) < (bShore ? 150.f : 180.f)) continue;
			if (JeebTerrain::NoGroundCover(P.X, P.Y) || JeebFill::Covered(P.X, P.Y)) continue;
			if (bTown && JeebOakHill::OnBuilding(P.X, P.Y, 4.f)) continue;
			const float Z = JeebTerrain::DrawnHeight(P.X, P.Y);
			const float Slope = FVector2D(JeebTerrain::DrawnHeight(P.X + 60.f, P.Y) - Z, JeebTerrain::DrawnHeight(P.X, P.Y + 60.f) - Z).Size() / 60.f;
			if (Slope > 0.6f) continue;
			int32 Kind; bool bFlowers = false;
			if (Kind3 == 0) Kind = Pick < 0.35f ? B_Low : Pick < 0.6f ? B_Fine : Pick < 0.85f ? B_Full : B_Small;
			else if (Kind3 == 1)
			{
				Kind = Pick < 0.28f ? B_Full : Pick < 0.5f ? B_Small : Pick < 0.68f ? B_Fine : Pick < 0.82f ? B_Low : B_Small;
				bFlowers = Pick >= 0.82f && C0 < 0.35f;   // (a flowering bush on the sunny side of the edge)
			}
			else Kind = Pick < 0.6f ? B_Low : B_Small;
			if (!Meshes[Kind]) continue;
			const float Scale = Kind == B_Low ? FMath::Lerp(0.8f, 1.3f, Sc) : FMath::Lerp(0.75f, 1.2f, Sc);
			const float Foot = Defs[Kind].Foot * Scale;
			// never in a solid: trunks, logs, stumps, rocks, ruins, walls, fill pieces, camps (an overlap above the ground with the world's collision)
			const float Rad = FMath::Min(0.5f * Foot, 50.f);
			if (World->OverlapAnyTestByChannel(FVector(P.X, P.Y, Z + 115.f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Rad, 60.f), Q)) { ++Blocked; continue; }
			const float Sink = 4.f + Slope * 0.5f * FMath::Min(Foot, 100.f);
			Xf[Kind].Add(FTransform(FRotator(0.f, Yaw, 0.f), FVector(P.X, P.Y, Z - Sink), FVector(Scale, Scale, Scale * FMath::Lerp(0.9f, 1.1f, Jx))));
			++Where[Kind3];
			if (bFlowers)
				for (int32 f = 0, Nf = 3 + int32(Sc * 2.99f); f < Nf; ++f)
				{
					const float A = Yaw + f * (360.f / Nf) + R.FRandRange(-25.f, 25.f), Dd = R.FRandRange(0.85f, 1.2f) * (Kind == B_Small ? 80.f : 110.f) * Scale;   // (round the leafy rim)
					const FVector2D Fp = P + FVector2D(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A))) * Dd;
					const int32 Fk = R.FRand() < 0.5f ? B_Flower1 : B_Flower2;
					const float Fs = R.FRandRange(1.0f, 1.5f);
					Xf[Fk].Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f), FVector(Fp.X, Fp.Y, JeebTerrain::DrawnHeight(Fp.X, Fp.Y) - 2.f), FVector(Fs)));
				}
		}

	// Reeds and lily pads (2026-11-21) along the water's edge: a jittered 1.6 m lattice near the water, each cell its own seed. Reeds in clumps
	// where the ground lies from 30 cm under the surface to 15 cm above it; a few pads where it is 50-140 cm deep within 4 m of the bank.
	int32 NReed = 0, NPad = 0;
	if (Density > 0.f)
	{
		const float RC = 160.f;
		const int32 RI = FMath::CeilToInt((Hi.X - Lo.X) / RC), RJ = FMath::CeilToInt((Hi.Y - Lo.Y) / RC);
		for (int32 i = 0; i < RI; ++i)
			for (int32 j = 0; j < RJ; ++j)
			{
				const FVector2D P0(Lo.X + (i + 0.5f) * RC, Lo.Y + (j + 0.5f) * RC);
				if (!JeebWorld::NearRiver(P0.X, P0.Y, 450.f) && P0.Y - JeebWorld::CoastY(P0.X) < -200.f) continue;   // (cheap: far from any water)
				if (!InRegion(P0) || JeebSteele::InBox(P0.X, P0.Y, -4000.f)) continue;
				FRandomStream R(int32(HashCombine(GetTypeHash(i * 6151 + 29), GetTypeHash(j * 92821 + 7))) ^ 0x7EED);
				const FVector2D P(P0.X + R.FRandRange(-0.45f, 0.45f) * RC, P0.Y + R.FRandRange(-0.45f, 0.45f) * RC);
				float S;
				if (!JeebWater::SurfaceAt(P.X, P.Y, S)) continue;
				const float G = JeebTerrain::DrawnHeight(P.X, P.Y), Under = S - G;
				const float Roll = R.FRand();
				if (Under > -15.f && Under < 30.f)
				{
					if (Roll >= 0.3f * Density) continue;
					if (JeebTerrain::PathClearance(P.X, P.Y) < 200.f || JeebFill::Covered(P.X, P.Y)) continue;
					if (JeebOakHill::InBox(P.X, P.Y, 0.f) && JeebOakHill::OnBuilding(P.X, P.Y, 4.f)) continue;
					for (int32 n = 0, Nn = 7 + R.RandRange(0, 6); n < Nn; ++n)
					{
						const FVector2D Qp = P + FVector2D(R.FRandRange(-55.f, 55.f), R.FRandRange(-55.f, 55.f));
						const float H = R.FRandRange(1.3f, 1.9f), W = R.FRandRange(1.1f, 1.5f);
						Xf[R.FRand() < 0.55f ? B_Reed1 : B_Reed2].Add(FTransform(FRotator(R.FRandRange(-5.f, 5.f), R.FRandRange(0.f, 360.f), R.FRandRange(-5.f, 5.f)),
							FVector(Qp.X, Qp.Y, JeebTerrain::DrawnHeight(Qp.X, Qp.Y) - 5.f), FVector(W, W, H)));
						++NReed;
					}
				}
				else if (Under > 50.f && Under < 140.f && Roll < 0.10f * Density)
				{
					bool bBank = false;   // (within 4 m of the bank: some ring point is dry)
					for (int32 k = 0; k < 6 && !bBank; ++k)
					{
						const float Bx = P.X + Ring6[k].X * 400.f, By = P.Y + Ring6[k].Y * 400.f;
						float Sb;
						bBank = !JeebWater::SurfaceAt(Bx, By, Sb) || JeebTerrain::DrawnHeight(Bx, By) > Sb;
					}
					if (!bBank) continue;
					for (int32 n = 0, Nn = 2 + R.RandRange(0, 2); n < Nn; ++n)
					{
						const FVector2D Qp = P + FVector2D(R.FRandRange(-160.f, 160.f), R.FRandRange(-160.f, 160.f));
						// (deterministic: the cell's own stream, the same on the server and every client) which of the three, and its own size and yaw
						const float Pick = R.FRand();
						const int32 Kind = Pick < 0.5f ? B_Pad : (Pick < 0.8f ? B_PadFlower : B_PadTwin);
						const float Sz = Kind == B_PadTwin ? R.FRandRange(0.28f, 0.45f) : R.FRandRange(0.32f, 0.6f);
						Xf[Kind].Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f), FVector(Qp.X, Qp.Y, S - 1.f), FVector(Sz)));
						++NPad;
					}
				}
			}
	}

#if !UE_BUILD_SHIPPING
	// (test pictures: -BushLineupX=X -BushLineupY=Y puts one of each kind in a row 6 m apart along +X from there, plus a hedge, for a close look)
	float X0 = 0.f, Y0 = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("BushLineupX="), X0) && FParse::Value(FCommandLine::Get(), TEXT("BushLineupY="), Y0))
	{
		{
			for (int32 k = 0; k < B_Flower1; ++k)
			{
				const float X = X0 + k * 600.f;
				Xf[k].Add(FTransform(FRotator::ZeroRotator, FVector(X, Y0, JeebTerrain::DrawnHeight(X, Y0) - 4.f)));
			}
			if (UStaticMesh* H = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Bushes/SM_BushHedge.SM_BushHedge"), nullptr, LOAD_Quiet | LOAD_NoWarn))
			{
				UHierarchicalInstancedStaticMeshComponent* L = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, TEXT("BushLineupHedge"));
				L->SetStaticMesh(H);
				L->SetupAttachment(GetRootComponent());
				L->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				L->bAffectDistanceFieldLighting = false;
				L->bAffectDynamicIndirectLighting = false;
				L->RegisterComponent();
				const float X = X0 + B_Flower1 * 600.f + 100.f;
				L->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X, Y0, JeebTerrain::DrawnHeight(X, Y0) - 4.f)));
				Sets.Add(L); SetReach.Add(7000.f);
			}
			UE_LOG(LogTemp, Log, TEXT("JEEBBUSH lineup at %.0f %.0f, ground %.0f"), X0, Y0, JeebTerrain::DrawnHeight(X0, Y0));
		}
	}
#endif
	int32 Total = 0;
	FString List;
	for (int32 k = 0; k < B_Num; ++k)
	{
		if (!Meshes[k] || Xf[k].Num() == 0) continue;
		UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("Bushes_%d"), k));
		C->SetStaticMesh(Meshes[k]);
		C->SetupAttachment(GetRootComponent());
		C->SetMobility(EComponentMobility::Static);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		C->RegisterComponent();
		if (k >= B_Pad) C->SetCastShadow(false);   // (flat on the water: no shadows)
		C->AddInstances(Xf[k], false, true, false);
		Sets.Add(C); SetReach.Add(Defs[k].Reach);
		Total += Xf[k].Num();
		List += FString::Printf(TEXT(" %s %d"), Defs[k].Title, Xf[k].Num());
	}
	ApplyLook();
	UE_LOG(LogTemp, Log, TEXT("JEEBBUSH wild bushes: %d instances (%d river and lake banks, %d forest edges, %d under the trees; %d tried, %d in a solid),%s; density %.2f, %.0f ms"),
		Total, Where[0], Where[1], Where[2], Tried, Blocked, *List, Density, (FPlatformTime::Seconds() - T0) * 1000.0);
	UE_LOG(LogTemp, Log, TEXT("JEEBREED reeds %d, lily pads %d"), NReed, NPad);
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebShot")))
		for (int32 k : { (int32)B_Reed1, (int32)B_Pad, (int32)B_PadFlower, (int32)B_PadTwin })
			for (int32 n = 0; n < FMath::Min(6, Xf[k].Num()); ++n)
			{
				const FVector L = Xf[k][(n * 7919 + 13) % Xf[k].Num()].GetLocation();
				UE_LOG(LogTemp, Log, TEXT("JEEBREED spot %s %.0f %.0f %.0f (from the plaza %.0f m)"), Defs[k].Title, L.X, L.Y, L.Z, FVector2D::Distance(FVector2D(L), FVector2D(JeebOakHill::PosX, JeebOakHill::PosY)) / 100.f);
			}
#endif
	// the towns' bushes, from the fill (its pieces with a /Game/Bushes mesh)
	TMap<FString, int32> Town;
	for (TActorIterator<AWorldFill> It(World); It; ++It)
	{
		TArray<UInstancedStaticMeshComponent*> Comps;
		It->GetComponents(Comps);
		for (UInstancedStaticMeshComponent* C : Comps)
			if (C && C->GetStaticMesh() && C->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Game/Bushes/")) && C->IsVisible()) Town.FindOrAdd(C->GetStaticMesh()->GetName()) += C->GetInstanceCount();
	}
	FString TownList; int32 TownTotal = 0;
	for (const TPair<FString, int32>& T : Town) { TownList += FString::Printf(TEXT(" %s %d"), *T.Key, T.Value); TownTotal += T.Value; }
	UE_LOG(LogTemp, Log, TEXT("JEEBBUSH the fill's bushes (towns, yards, the pond):%s; all bushes %d"), *TownList, Total + TownTotal - Xf[B_Flower1].Num() - Xf[B_Flower2].Num());
#if !UE_BUILD_SHIPPING
	// (test pictures: where to look)
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebShot")))
	{
		for (int32 k = 0; k < B_Flower1; ++k)
			for (int32 n = 0; n < FMath::Min(4, Xf[k].Num()); ++n)
			{
				const FVector L = Xf[k][(n * 7919) % Xf[k].Num()].GetLocation();
				UE_LOG(LogTemp, Log, TEXT("JEEBBUSH spot %s %.0f %.0f (canopy %.2f, from the plaza %.0f m)"), Defs[k].Title, L.X, L.Y, JeebCanopy::Cover(L.X, L.Y), FVector2D::Distance(FVector2D(L), FVector2D(JeebOakHill::PosX, JeebOakHill::PosY)) / 100.f);
			}
		for (const JeebOakHill::FPlot& P : JeebOakHill::Plots())
			if (P.Kind == JeebOakHill::EPlot::Building)
				UE_LOG(LogTemp, Log, TEXT("JEEBBUSH plot %s at %.0f %.0f (cos %.2f sin %.2f, %.0f x %.0f)"), P.Name, JeebOakHill::PosX + P.Cn * 100.f, JeebOakHill::PosY + P.Ce * 100.f, P.Cs, P.Sn, P.W * JeebOakHill::Unit, P.H * JeebOakHill::Unit);
	}
#endif
}

// The leaf colour, draw distance and shadows, live. The fill's town bushes (its pieces with a /Game/Bushes mesh) share the same leaf colour.
void ABushes::ApplyLook()
{
	const FLinearColor Tint = LeafTint();
	auto Shared = [&](UMaterialInterface* M) -> UMaterialInterface*
	{
		if (!M || !M->GetPathName().StartsWith(TEXT("/Game/Bushes/"))) return M;
		for (UMaterialInstanceDynamic* D : Mids) if (D && D->Parent == M) return D;
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(M, this);
		Mids.Add(D);
		return D;
	};
	auto Paint = [&](UInstancedStaticMeshComponent* C)
	{
		const UStaticMesh* SM = C ? C->GetStaticMesh() : nullptr;
		if (!SM || !SM->GetPathName().StartsWith(TEXT("/Game/Bushes/"))) return;
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			UMaterialInterface* M = C->GetMaterial(i);
			if (UMaterialInstanceDynamic* D = Cast<UMaterialInstanceDynamic>(M)) { if (Mids.Contains(D)) continue; M = D->Parent; }
			C->SetMaterial(i, Shared(M));
		}
	};
	for (int32 s = 0; s < Sets.Num(); ++s)
	{
		UHierarchicalInstancedStaticMeshComponent* C = Sets[s];
		if (!C) continue;
		Paint(C);
		const float End = SetReach[s] * FMath::Max(0.1f, CVarBushReach.GetValueOnGameThread());
		C->SetCullDistances(int32(End * 0.85f), int32(End));
		C->SetCastShadow(CVarBushShadows.GetValueOnGameThread() != 0);
	}
	if (UWorld* World = GetWorld())
		for (TActorIterator<AWorldFill> It(World); It; ++It)
		{
			TArray<UInstancedStaticMeshComponent*> Comps;
			It->GetComponents(Comps);
			for (UInstancedStaticMeshComponent* C : Comps) Paint(C);
		}
	for (UMaterialInstanceDynamic* D : Mids) if (D) D->SetVectorParameterValue(TEXT("Fall Color Tint"), Tint);
}
