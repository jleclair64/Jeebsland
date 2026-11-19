#include "Bushes.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "OakHill.h"
#include "OakForest.h"
#include "SteeleCreek.h"
#include "Canopy.h"
#include "Fill.h"
#include "TreeLook.h"
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
	TAutoConsoleVariable<float> CVarBushShade(TEXT("jeeb.Bush.Shade"), 0.85f,
		TEXT("The bushes' leaf colour: the trees' green (jeeb.Trees.DzGreen) times this (they grow in the trees' shade). Reaches the fill's town bushes too. Live."), ECVF_Default);
	TAutoConsoleVariable<int32> CVarBushShadows(TEXT("jeeb.Bush.Shadows"), 0,
		TEXT("Shadows from the wild bushes: 0 none (cheap, as the old bushes), 1 on. Live."), ECVF_Default);

	TWeakObjectPtr<ABushes> GBushes;

	enum EBush : uint8 { B_Full, B_Small, B_Fine, B_Low, B_Flower1, B_Flower2, B_Num };
	struct FBushDef { const TCHAR* Path; float Reach; float Foot; const TCHAR* Title; };
	const FBushDef Defs[B_Num] = {
		{ TEXT("/Game/Bushes/SM_BushFull.SM_BushFull"), 7000.f, 100.f, TEXT("full") },
		{ TEXT("/Game/Bushes/SM_BushSmall.SM_BushSmall"), 5000.f, 60.f, TEXT("small") },
		{ TEXT("/Game/Bushes/SM_BushFine.SM_BushFine"), 7000.f, 85.f, TEXT("fine") },
		{ TEXT("/Game/Bushes/SM_BushLow.SM_BushLow"), 4500.f, 70.f, TEXT("low") },
		// (the flowering bush: a small bush with the meadow's own flowers, the realistic grass pack's, through it)
		{ TEXT("/Game/PN_MeadowFlowers/Meshes/flowerMesh/flower_14_06.flower_14_06"), 3500.f, 0.f, TEXT("flower a") },
		{ TEXT("/Game/PN_MeadowFlowers/Meshes/flowerMesh/flower_17_05.flower_17_05"), 3500.f, 0.f, TEXT("flower b") },
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
			if (JeebWorld::InWater(P.X, P.Y)) continue;
			// on a bank: water within 4.5 m
			bool bShore = false;
			for (int32 k = 0; k < 6 && !bShore; k += 2) bShore = JeebWorld::InWater(P.X + Ring6[k].X * 450.f, P.Y + Ring6[k].Y * 450.f);
			if (!bShore) bShore = JeebWorld::InWater(P.X - 450.f, P.Y);
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
					const float A = Yaw + f * (360.f / Nf) + R.FRandRange(-25.f, 25.f), Dd = R.FRandRange(0.35f, 0.7f) * Foot;
					const FVector2D Fp = P + FVector2D(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A))) * Dd;
					const int32 Fk = R.FRand() < 0.5f ? B_Flower1 : B_Flower2;
					const float Fs = R.FRandRange(1.0f, 1.5f);
					Xf[Fk].Add(FTransform(FRotator(0.f, R.FRandRange(0.f, 360.f), 0.f), FVector(Fp.X, Fp.Y, JeebTerrain::DrawnHeight(Fp.X, Fp.Y) - 2.f), FVector(Fs)));
				}
		}

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
		C->AddInstances(Xf[k], false, true, false);
		Sets.Add(C); SetReach.Add(Defs[k].Reach);
		Total += Xf[k].Num();
		List += FString::Printf(TEXT(" %s %d"), Defs[k].Title, Xf[k].Num());
	}
	ApplyLook();
	UE_LOG(LogTemp, Log, TEXT("JEEBBUSH wild bushes: %d instances (%d river and lake banks, %d forest edges, %d under the trees; %d tried, %d in a solid),%s; density %.2f, %.0f ms"),
		Total, Where[0], Where[1], Where[2], Tried, Blocked, *List, Density, (FPlatformTime::Seconds() - T0) * 1000.0);
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
