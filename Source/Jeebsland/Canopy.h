#pragma once

#include "CoreMinimal.h"

// Where the broad-leaf tree canopies are. The forest records each one as it plants it; the meadow thins its grass
// underneath and the autumn decor lays a carpet of fallen leaves in the tree's own colour. Header only.
namespace JeebCanopy
{
	struct FSpot { FVector2D P; float R; int32 Color; };   // Color: index into the leaf palette

	constexpr float GridCell = 1000.f;

	inline TArray<FSpot>& Spots() { static TArray<FSpot> S; return S; }
	inline TMap<FIntPoint, TArray<int32>>& Grid() { static TMap<FIntPoint, TArray<int32>> G; return G; }

	inline void Add(const FVector2D& P, float R, int32 Color)
	{
		const int32 Idx = Spots().Add(FSpot{ P, R, Color });
		const float Reach = R * 1.7f;
		for (int32 y = FMath::FloorToInt((P.Y - Reach) / GridCell); y <= FMath::FloorToInt((P.Y + Reach) / GridCell); ++y)
			for (int32 x = FMath::FloorToInt((P.X - Reach) / GridCell); x <= FMath::FloorToInt((P.X + Reach) / GridCell); ++x)
				Grid().FindOrAdd(FIntPoint(x, y)).Add(Idx);
	}

	// 0 in the open, rising to 1 well under a canopy
	inline float Cover(float X, float Y)
	{
		const TArray<int32>* Near = Grid().Find(FIntPoint(FMath::FloorToInt(X / GridCell), FMath::FloorToInt(Y / GridCell)));
		if (!Near) return 0.f;
		float Best = 0.f;
		for (int32 i : *Near)
		{
			const FSpot& S = Spots()[i];
			const float D = FVector2D::Distance(FVector2D(X, Y), S.P);
			Best = FMath::Max(Best, 1.f - FMath::SmoothStep(0.35f * S.R, 1.6f * S.R, D));   // a long, gentle taper out from the trunk
		}
		return Best;
	}
}
