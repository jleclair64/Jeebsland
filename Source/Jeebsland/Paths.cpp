#include "Paths.h"
#include "Terrain.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

APaths::APaths()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Roads"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
}

void APaths::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);

	const TArray<JeebTerrain::FPathDef>& Defs = JeebTerrain::PathDefs();
	TArray<FVector> Verts, Normals;
	TArray<int32> Tris, TrisBack;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Colors;
	// Oak Hill's streets are cobbled (2026-11-12): red 1 on them (M_Dirt lays cobbles down the middle, fraying into packed earth at
	// the sides and then into the grass); Blackrock's paths and the forest's roads stay packed earth (red 0).
	int32 FirstCobbled = Defs.Num(), FirstForest = Defs.Num();   // (Oak Hill's streets, then the forest's roads round it: OakForest.cpp, 2026-11-16)
	for (int32 d = Defs.Num() - 1; d >= 0; --d) { if (Defs[d].Kind == 1) FirstCobbled = d; if (Defs[d].Kind == 2) FirstForest = d; }

	// Each road is resampled every 50 cm along its length and 5 points across, and every point takes its height from
	// the ground mesh itself, so the road follows every fold of the ground instead of cutting through humps
	constexpr int32 Cols = 9;
	constexpr float StepLen = 50.f;
	for (int32 d = 0; d < Defs.Num(); ++d)
	{
		const JeebTerrain::FPathDef& Def = Defs[d];
		const int32 N = Def.Pts.Num();
		if (N < 2) continue;
		if (Def.Kind == 4) continue;   // (the road over the stone bridge: its deck is the road, 2026-11-18)
		if (Def.Kind == 1 && JeebTownLab::RealTown()) continue;   // (the rebuilt Oak Hill paints its own streets into its ground, 2026-11-14: no ribbons)
		const float Lift = Def.Kind == 3 ? 2.5f : Def.Kind == 2 ? 4.f + 0.12f * ((d - FirstForest) % 24) : 1.5f + 0.12f * (Def.Kind == 1 ? d - FirstCobbled : d);   // a hair above the ground he walks on (the ribbons have no collision); each road a touch higher so crossings never flicker

		// Points along the centre line, with the road's half-width on each side
		TArray<FVector2D> Line;
		TArray<float> HL, HR;
		const bool bShaped = Def.HalfL.Num() == N && Def.HalfR.Num() == N;
		const int32 Segs = Def.bClosed ? N : N - 1;
		for (int32 i = 0; i < Segs; ++i)
		{
			const int32 j = (i + 1) % N;
			const FVector2D A = Def.Pts[i], B = Def.Pts[j];
			const int32 Sub = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(A, B) / StepLen));
			for (int32 s = 0; s < Sub; ++s)
			{
				const float K = float(s) / Sub;
				Line.Add(FMath::Lerp(A, B, K));
				HL.Add(bShaped ? FMath::Lerp(Def.HalfL[i], Def.HalfL[j], K) : Def.Width * 0.5f);
				HR.Add(bShaped ? FMath::Lerp(Def.HalfR[i], Def.HalfR[j], K) : Def.Width * 0.5f);
			}
		}
		if (!Def.bClosed) { Line.Add(Def.Pts.Last()); HL.Add(bShaped ? Def.HalfL.Last() : Def.Width * 0.5f); HR.Add(bShaped ? Def.HalfR.Last() : Def.Width * 0.5f); }
		const int32 L = Line.Num();

		const int32 First = Verts.Num();
		float Along = 0.f;
		for (int32 i = 0; i < L; ++i)
		{
			const FVector2D Prev = Line[Def.bClosed ? (i + L - 1) % L : FMath::Max(i - 1, 0)];
			const FVector2D Next = Line[Def.bClosed ? (i + 1) % L : FMath::Min(i + 1, L - 1)];
			const FVector2D Tan = (Next - Prev).GetSafeNormal();
			const FVector2D Nrm(-Tan.Y, Tan.X);
			if (i > 0) Along += FVector2D::Distance(Line[i], Line[i - 1]);
			for (int32 c = 0; c < Cols; ++c)
			{
				const float S = float(c) / (Cols - 1) - 0.5f;   // -0.5 .. 0.5 across the road
				const float Half = (S < 0.f ? HR[i] : HL[i]) * 1.4f;   // wider than the road: the material frays the edge back in over a broad band
				const FVector2D P = Line[i] + Nrm * (2.f * Half * S);
				Verts.Add(FVector(P.X, P.Y, (Def.Kind == 2 ? JeebTerrain::DrawnHeight(P.X, P.Y) : JeebTerrain::GroundMeshHeight(P.X, P.Y)) + Lift));   // (the forest roads: on whichever ground is drawn there, mostly the world's 8 m mesh)
				Normals.Add(FVector::UpVector);
				UV.Add(FVector2D(S + 0.5f, Along / Def.Width));
				Colors.Add(FLinearColor(Def.Kind == 1 ? 1.f : 0.f, 0.f, 0.f, 1.f));
			}
		}
		const int32 Rows = Def.bClosed ? L : L - 1;
		for (int32 i = 0; i < Rows; ++i)
		{
			for (int32 c = 0; c < Cols - 1; ++c)
			{
				const int32 A = First + i * Cols + c, B = A + 1;
				const int32 C = First + ((i + 1) % L) * Cols + c, D = C + 1;
				Tris.Append({ A, C, B, B, C, D });
				TrisBack.Append({ A, B, C, B, D, C });
			}
		}
	}

	TArray<FProcMeshTangent> Tangents;
	UE_LOG(LogTemp, Log, TEXT("JEEBPATHS %d ribbons drawn: %d points, %d triangles (forest roads from def %d of %d)"), Defs.Num(), Verts.Num(), Tris.Num() / 3, FirstForest, Defs.Num());
	Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UV, Colors, Tangents, false);
	Mesh->CreateMeshSection_LinearColor(1, Verts, TrisBack, Normals, UV, Colors, Tangents, false);

	// Packed dirt (and Oak Hill's cobbles): painted textures laid in world space (Tools/world_material.py), or a flat colour if it is missing
	UMaterialInterface* Dirt = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Dirt.M_Dirt"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
	{
		if (Dirt) Mesh->SetMaterial(i, Dirt);
		else if (UMaterialInstanceDynamic* M = Mesh->CreateDynamicMaterialInstance(i, Base)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.24f, 0.16f, 0.08f));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBPATHS material %s, bounds %s, visible %d"), Mesh->GetMaterial(0) ? *Mesh->GetMaterial(0)->GetPathName() : TEXT("none"), *Mesh->Bounds.GetBox().ToString(), Mesh->IsVisible() ? 1 : 0);
#if !UE_BUILD_SHIPPING
	// (test pictures: the console command jeeb.Test.PathsRed paints the ribbons flat red with the engine's plain material, to tell the mesh from the material)
	static FAutoConsoleCommandWithWorld PathsRed(TEXT("jeeb.Test.PathsRed"), TEXT("Paints the road ribbons flat red (test pictures)"), FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		UMaterialInterface* Plain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		for (TActorIterator<APaths> It(W); It && Plain; ++It)
			for (int32 i = 0; i < It->Mesh->GetNumMaterials(); ++i)
				if (UMaterialInstanceDynamic* M = It->Mesh->CreateDynamicMaterialInstance(i, Plain)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.f, 0.f));
	}));
#endif
}
