#include "WorldTerrain.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

namespace
{
	constexpr int32 ChunkCells = 40;   // cells per chunk side
}

AWorldTerrain::AWorldTerrain()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AWorldTerrain::BeginPlay()
{
	Super::BeginPlay();
	BuildLand();
	if (!IsNetMode(NM_DedicatedServer)) BuildWater();
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

	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_WorldGround.M_WorldGround"));
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
					// colour: green lowland, browner hills, grey rock on steep or high ground, snow up top, sand by the sea,
					// darker mud in the river beds
					const float Slope = 1.f - Nrm.Z;   // 0 flat .. 1 vertical
					FLinearColor C = FMath::Lerp(FLinearColor(0.16f, 0.27f, 0.09f), FLinearColor(0.30f, 0.30f, 0.14f), JeebWorld::Smooth(300.f, 2000.f, Z));
					C = FMath::Lerp(C, FLinearColor(0.30f, 0.29f, 0.27f), FMath::Max(JeebWorld::Smooth(0.25f, 0.45f, Slope), JeebWorld::Smooth(2200.f, 3800.f, Z)));
					C = FMath::Lerp(C, FLinearColor(0.85f, 0.87f, 0.90f), JeebWorld::Smooth(4300.f, 5000.f, Z) * (1.f - JeebWorld::Smooth(0.45f, 0.65f, Slope)));
					C = FMath::Lerp(C, FLinearColor(0.38f, 0.34f, 0.28f), RockMask(FVector2D(X, Y)) * 0.6f * (1.f - JeebWorld::Smooth(2200.f, 3800.f, Z)));
					const float Shore = JeebWorld::Smooth(SeaLevel + 160.f, SeaLevel + 40.f, Z) * JeebWorld::Smooth(-4000.f, -500.f, Y - CoastY(X));
					C = FMath::Lerp(C, FLinearColor(0.62f, 0.56f, 0.40f), Shore);
					float Half;
					const float Dr = RiverDistance(X, Y, Half);
					C = FMath::Lerp(C, FLinearColor(0.20f, 0.18f, 0.13f), JeebWorld::Smooth(Half + 300.f, Half - 100.f, Dr));
					Col.Add(C);
				}
			for (int32 i = CI; i < I1; ++i)
				for (int32 j = CJ; j < J1; ++j)
				{
					// skip the cells inside the original square (its own ground covers them)
					const float X0 = MinX + i * Step, Y0 = MinY + j * Step;
					if (X0 >= -Hole && X0 + Step <= Hole && Y0 >= -Hole && Y0 + Step <= Hole) continue;
					const int32 A = (i - CI) * W + (j - CJ), B = A + W, C = A + 1, D = B + 1;
					Tri.Append({ A, C, B, B, C, D });
				}
			if (Tri.Num() == 0) continue;
			// (wound to face up: checked against the normals so the ground is solid from above whichever way the engine counts)
			{
				const FVector Fn = FVector::CrossProduct(V[Tri[1]] - V[Tri[0]], V[Tri[2]] - V[Tri[0]]);
				if (Fn.Z < 0.f) for (int32 t = 0; t + 2 < Tri.Num(); t += 3) Swap(Tri[t + 1], Tri[t + 2]);
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
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	const FLinearColor WaterCol(0.06f, 0.20f, 0.30f);
	auto Sheet = [&](const TArray<FVector>& V, const TArray<int32>& T)
	{
		UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
		PM->SetupAttachment(GetRootComponent());
		PM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PM->RegisterComponent();
		TArray<FVector> N; N.Init(FVector::UpVector, V.Num());
		TArray<int32> Both = T;
		for (int32 t = 0; t + 2 < T.Num(); t += 3) Both.Append({ T[t], T[t + 2], T[t + 1] });   // both windings: seen from either side
		PM->CreateMeshSection_LinearColor(0, V, Both, N, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		if (UMaterialInstanceDynamic* M = PM->CreateDynamicMaterialInstance(0, Base)) M->SetVectorParameterValue(TEXT("Color"), WaterCol);
		PM->SetCastShadow(false);
		Water.Add(PM);
	};
	// The ocean: one big sheet from just inside the coast out past the world's eastern edge
	{
		float WestMost = 1.0e9f;
		for (float X = MinX; X <= MaxX; X += 2000.f) WestMost = FMath::Min(WestMost, CoastY(X));
		const float Y0 = WestMost - 3000.f, Y1 = MaxY + 60000.f, X0 = MinX - 60000.f, X1 = MaxX + 60000.f;
		Sheet({ FVector(X0, Y0, SeaLevel), FVector(X1, Y0, SeaLevel), FVector(X0, Y1, SeaLevel), FVector(X1, Y1, SeaLevel) }, { 0, 2, 1, 1, 2, 3 });
	}
	// The rivers: a ribbon along each, a little wider than its channel
	for (const FRiver& Rv : Rivers())
	{
		TArray<FVector> V; TArray<int32> T;
		for (int32 i = 0; i < Rv.Pts.Num(); ++i)
		{
			const FVector2D A = Rv.Pts[FMath::Max(i - 1, 0)], B = Rv.Pts[FMath::Min(i + 1, Rv.Pts.Num() - 1)];
			const FVector2D Dir = (B - A).GetSafeNormal(), Side(-Dir.Y, Dir.X);
			const float W = Rv.HalfWidth + 250.f;
			const FVector2D L = Rv.Pts[i] + Side * W, R = Rv.Pts[i] - Side * W;
			V.Add(FVector(L.X, L.Y, RiverLevel)); V.Add(FVector(R.X, R.Y, RiverLevel));
			if (i > 0) { const int32 k = (i - 1) * 2; T.Append({ k, k + 2, k + 1, k + 1, k + 2, k + 3 }); }
		}
		Sheet(V, T);
	}
}
