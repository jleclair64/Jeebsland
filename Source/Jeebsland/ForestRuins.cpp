#include "ForestRuins.h"
#include "OakForest.h"
#include "OakHill.h"
#include "TownLab.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"

namespace
{
	constexpr float RuinHalfX = 245.f, RuinHalfY = 370.f;   // the model's footprint (481 x 730 cm, origin at the bottom centre)
	constexpr float RuinR = 440.f;                          // its corners' reach from the centre
	constexpr float Clear = 800.f;                          // kept off roads, water and river banks (cm, from the footprint's edge)
	constexpr float MaxSpread = 110.f;                      // how uneven the ground under it may be (cm, highest sample to lowest)
	constexpr float Sink = 15.f;                            // set this far below the lowest ground sample, so no edge floats

	float BoxGap(float X, float Y)   // how far outside Oak Hill's box (cm; 0 inside); the same rule as OakForest.cpp's
	{
		const float Dx = FMath::Max3(JeebOakHill::BoxX0 - X, 0.f, X - JeebOakHill::BoxX1), Dy = FMath::Max3(JeebOakHill::BoxY0 - Y, 0.f, Y - JeebOakHill::BoxY1);
		return FMath::Sqrt(Dx * Dx + Dy * Dy);
	}
	bool NearForestRoad(float X, float Y, float Pad)   // a quick box test before PathClearance
	{
		for (const JeebTerrain::FPathDef& P : JeebTerrain::PathDefs())
			if (P.Bounds.ExpandBy(P.MaxHalf + Pad).IsInside(FVector2D(X, Y))) return true;
		return false;
	}
	FIntPoint CellOf(float X, float Y) { return FIntPoint(FMath::FloorToInt(X / 1000.f), FMath::FloorToInt(Y / 1000.f)); }
}

AForestRuins::AForestRuins()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
}

void AForestRuins::BeginPlay()
{
	Super::BeginPlay();
	const double T0 = FPlatformTime::Seconds();
	const bool bServer = IsNetMode(NM_DedicatedServer);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/WorldProps/SM_Ruin.SM_Ruin"));
	if (!Mesh) { UE_LOG(LogTemp, Warning, TEXT("JEEBRUINS /Game/WorldProps/SM_Ruin missing: no ruins")); return; }

	// The forest's solids (trunks, logs, stumps, rocks, boulders: every machine has them) by 10 m cell, so no ruin stands on one
	AOakForest* Forest = nullptr;
	for (TActorIterator<AOakForest> It(GetWorld()); It; ++It) { Forest = *It; break; }
	TMap<FIntPoint, TArray<FVector2D>> Solid;
	TArray<UInstancedStaticMeshComponent*> Sets;
	if (Forest) Forest->GetComponents<UInstancedStaticMeshComponent>(Sets);
	for (UInstancedStaticMeshComponent* C : Sets)
	{
		if (!C || !C->IsCollisionEnabled()) continue;
		for (int32 i = 0; i < C->GetInstanceCount(); ++i)
		{
			FTransform Xf; C->GetInstanceTransform(i, Xf, true);
			const FVector L = Xf.GetLocation();
			Solid.FindOrAdd(CellOf(L.X, L.Y)).Add(FVector2D(L.X, L.Y));
		}
	}
	auto OnSolid = [&](const FVector2D& P) -> bool
	{
		const FIntPoint C = CellOf(P.X, P.Y);
		for (int32 dx = -1; dx <= 1; ++dx) for (int32 dy = -1; dy <= 1; ++dy)
			if (const TArray<FVector2D>* L = Solid.Find(C + FIntPoint(dx, dy)))
				for (const FVector2D& S : *L) if (FVector2D::DistSquared(S, P) < FMath::Square(RuinR + 120.f)) return true;
		return false;
	};

	// The spots: tried in a fixed order from a fixed seed; each one's checks read only the land and the forest's solids
	const FVector2D Plaza(JeebOakHill::PosX, JeebOakHill::PosY);
	const bool bLab = JeebTownLab::Active();
	FRandomStream R(47471);
	int32 Tries = 0;
	for (; Tries < 6000 && Placed.Num() < JeebForestRuins::Count; ++Tries)
	{
		const float Ang = R.FRandRange(0.f, 2.f * PI), Rad = JeebOakForest::Reach * FMath::Sqrt(R.FRandRange(0.04f, 0.88f)), Yaw = R.FRandRange(0.f, 360.f);
		const FVector2D P = Plaza + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Rad;
		if (BoxGap(P.X, P.Y) < 2500.f + RuinR) continue;
		if (FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y)) < JeebTerrain::GroundHalf + 1200.f + RuinR) continue;
		if (bLab && JeebTownLab::InSite(JeebTownLab::LabSite(), P.X, P.Y, 2500.f + RuinR)) continue;
		bool bFar = true;
		for (const FVector4& Q : Placed) if (FVector2D::Distance(FVector2D(Q.X, Q.Y), P) < JeebForestRuins::MinApart) { bFar = false; break; }
		if (!bFar) continue;
		float Half; const float Rd = JeebWorld::RiverDistance(P.X, P.Y, Half) - Half;
		if (Rd < Clear + RuinR) continue;
		// the footprint: its corners, edges' middles and centre all dry and close in height
		const FQuat Q(FVector::UpVector, FMath::DegreesToRadians(Yaw));
		float Lo = 1.e9f, Hi = -1.e9f; bool bWet = false;
		for (int32 ix = -2; ix <= 2 && !bWet; ++ix)
			for (int32 iy = -2; iy <= 2; ++iy)
			{
				const FVector W = FVector(P, 0.f) + Q.RotateVector(FVector(RuinHalfX * ix * 0.5f, RuinHalfY * iy * 0.5f, 0.f));
				if (JeebWorld::InWater(W.X, W.Y)) { bWet = true; break; }
				const float H = JeebTerrain::DrawnHeight(W.X, W.Y);
				Lo = FMath::Min(Lo, H); Hi = FMath::Max(Hi, H);
			}
		if (bWet || Hi - Lo > MaxSpread) continue;
		if (NearForestRoad(P.X, P.Y, Clear + RuinR) && JeebTerrain::PathClearance(P.X, P.Y) < Clear + RuinR) continue;
		if (OnSolid(P)) continue;
		Placed.Add(FVector4(P.X, P.Y, Lo - Sink, Yaw));
	}

	uint32 Hash = 2166136261u;
	auto HashIn = [&](float V) { const int32 Qv = FMath::RoundToInt(V * 10.f); for (int32 b = 0; b < 4; ++b) { Hash ^= uint32((Qv >> (8 * b)) & 0xFF); Hash *= 16777619u; } };
	TArray<FTransform> Xs;
	for (const FVector4& S : Placed)
	{
		Xs.Add(FTransform(FRotator(0.f, S.W, 0.f), FVector(S.X, S.Y, S.Z)));
		HashIn(S.X); HashIn(S.Y); HashIn(S.Z); HashIn(S.W);
	}

	// One instanced set: drawn and shadowed on clients, hidden (collision only) on a dedicated server
	Ruins = NewObject<UInstancedStaticMeshComponent>(this, TEXT("ForestRuins"));
	Ruins->SetStaticMesh(Mesh);
	Ruins->SetupAttachment(RootComponent);
	Ruins->SetMobility(EComponentMobility::Static);
	Ruins->SetCollisionProfileName(TEXT("BlockAll"));
	Ruins->SetCanEverAffectNavigation(false);
	Ruins->SetCastShadow(true);
	Ruins->bCastFarShadow = false;
	Ruins->SetCullDistances(25000, 30000);
	if (bServer) { Ruins->SetVisibility(false); Ruins->SetHiddenInGame(true); Ruins->SetCastShadow(false); }
	Ruins->RegisterComponent();
	if (Xs.Num()) Ruins->AddInstances(Xs, false, true, false);

	// Clients: the forest's bushes, plants and seedlings under a ruin go (its trees can't be there: their trunks are solids, kept clear above).
	// A tree set has a hidden twin with the same mesh that draws its shadows from its own list, so its seedlings are shrunk away instead.
	int32 Cleared = 0;
	if (!bServer && Forest && Placed.Num())
	{
		TSet<UStaticMesh*> TreeMeshes;
		for (UInstancedStaticMeshComponent* C : Sets) if (C && C->bCastHiddenShadow) TreeMeshes.Add(C->GetStaticMesh());
		for (UInstancedStaticMeshComponent* C : Sets)
		{
			if (!C || C->IsCollisionEnabled() || C->bCastHiddenShadow || !C->IsVisible()) continue;
			const bool bTree = TreeMeshes.Contains(C->GetStaticMesh());
			TArray<int32> Gone;
			for (int32 i = 0; i < C->GetInstanceCount(); ++i)
			{
				FTransform Xf; C->GetInstanceTransform(i, Xf, true);
				const FVector L = Xf.GetLocation();
				for (const FTransform& Rx : Xs)
				{
					const FVector Lc = Rx.InverseTransformPosition(L);
					if (FMath::Abs(Lc.X) < RuinHalfX + 80.f && FMath::Abs(Lc.Y) < RuinHalfY + 80.f) { Gone.Add(i); break; }
				}
			}
			if (!Gone.Num()) continue;
			if (bTree)
				for (int32 i : Gone) { FTransform Xf; C->GetInstanceTransform(i, Xf, true); Xf.SetScale3D(FVector(0.001f)); C->UpdateInstanceTransform(i, Xf, true, false, true); }
			else C->RemoveInstances(Gone);
			C->MarkRenderStateDirty();
			Cleared += Gone.Num();
		}
	}

	for (const FVector4& S : Placed)
		UE_LOG(LogTemp, Log, TEXT("JEEBRUINS at %.0f %.0f %.0f yaw %.0f (%.0f m from the plaza)"), S.X, S.Y, S.Z, S.W, FVector2D::Distance(FVector2D(S.X, S.Y), Plaza) / 100.f);
	UE_LOG(LogTemp, Log, TEXT("JEEBRUINS %d ruins of %d wanted (%d spots tried, %d cells of forest solids, %d plants cleared) in %.1f ms, hash %08x%s"),
		Placed.Num(), JeebForestRuins::Count, Tries, Solid.Num(), Cleared, (FPlatformTime::Seconds() - T0) * 1000.0, Hash, bServer ? TEXT(" (server: collision only)") : TEXT(""));
}
