#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

namespace JeebGem
{
	// Fills a procedural mesh with a six-sided crystal: two pyramids joined base to base,
	// made of 12 flat triangles. About 100 wide and 120 tall; its pivot is the middle of the ring.
	inline void Build(UProceduralMeshComponent* Mesh)
	{
		const int32 Sides = 6;
		const float Radius = 50.f;
		const FVector Top(0.f, 0.f, 85.f);
		const FVector Bottom(0.f, 0.f, -35.f);
		const FVector Center(0.f, 0.f, 15.f);

		TArray<FVector> Ring;
		for (int32 k = 0; k < Sides; ++k)
		{
			const float A = 2.f * PI * float(k) / float(Sides);
			Ring.Add(FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, 0.f));
		}

		TArray<FVector> Verts;
		TArray<FVector> Normals;
		TArray<int32> Tris;

		auto AddTri = [&](FVector A, FVector B, FVector C)
		{
			FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
			const FVector Centroid = (A + B + C) / 3.f;
			if (FVector::DotProduct(Normal, Centroid - Center) < 0.f)
			{
				Swap(B, C); // wind the triangle so it faces outward
				Normal = -Normal;
			}
			const int32 Base = Verts.Num();
			Verts.Add(A); Verts.Add(B); Verts.Add(C);
			Normals.Add(Normal); Normals.Add(Normal); Normals.Add(Normal);
			// Both windings, so the outside shows whichever way the engine treats as 'front'
			Tris.Add(Base); Tris.Add(Base + 1); Tris.Add(Base + 2);
			Tris.Add(Base); Tris.Add(Base + 2); Tris.Add(Base + 1);
		};

		for (int32 k = 0; k < Sides; ++k)
		{
			const FVector& P = Ring[k];
			const FVector& Q = Ring[(k + 1) % Sides];
			AddTri(Top, P, Q);
			AddTri(Bottom, Q, P);
		}

		TArray<FVector2D> UV;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UV, Colors, Tangents, false);
	}
}
