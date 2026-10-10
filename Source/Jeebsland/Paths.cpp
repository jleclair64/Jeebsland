#include "Paths.h"
#include "Terrain.h"
#include "Materials/MaterialInstanceDynamic.h"

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

	for (int32 d = 0; d < Defs.Num(); ++d)
	{
		const JeebTerrain::FPathDef& Def = Defs[d];
		const int32 N = Def.Pts.Num();
		const float Lift = 9.f + d * 2.f; // each road a hair higher than the last so overlaps never flicker
		const int32 First = Verts.Num();

		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D Prev = Def.Pts[Def.bClosed ? (i + N - 1) % N : FMath::Max(i - 1, 0)];
			const FVector2D Next = Def.Pts[Def.bClosed ? (i + 1) % N : FMath::Min(i + 1, N - 1)];
			const FVector2D Tan = (Next - Prev).GetSafeNormal();
			const FVector2D Nrm(-Tan.Y, Tan.X);
			const FVector2D L = Def.Pts[i] + Nrm * (Def.Width * 0.5f);
			const FVector2D R = Def.Pts[i] - Nrm * (Def.Width * 0.5f);
			Verts.Add(FVector(L.X, L.Y, JeebTerrain::HeightAt(L.X, L.Y) + Lift));
			Verts.Add(FVector(R.X, R.Y, JeebTerrain::HeightAt(R.X, R.Y) + Lift));
			Normals.Add(FVector::UpVector);
			Normals.Add(FVector::UpVector);
			UV.Add(FVector2D(0.f, i));
			UV.Add(FVector2D(1.f, i));
		}

		const int32 Segs = Def.bClosed ? N : N - 1;
		for (int32 i = 0; i < Segs; ++i)
		{
			const int32 A = First + 2 * i, B = A + 1;
			const int32 C = First + 2 * ((i + 1) % N), D = C + 1;
			Tris.Add(A); Tris.Add(C); Tris.Add(B);
			Tris.Add(B); Tris.Add(C); Tris.Add(D);
			TrisBack.Add(A); TrisBack.Add(B); TrisBack.Add(C);
			TrisBack.Add(B); TrisBack.Add(D); TrisBack.Add(C);
		}
	}

	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UV, Colors, Tangents, false);
	Mesh->CreateMeshSection_LinearColor(1, Verts, TrisBack, Normals, UV, Colors, Tangents, false);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
	{
		if (UMaterialInstanceDynamic* M = Mesh->CreateDynamicMaterialInstance(i, Base))
		{
			M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.33f, 0.22f, 0.11f)); // packed dirt
		}
	}
}
