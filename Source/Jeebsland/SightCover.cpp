#include "SightCover.h"

namespace
{
	struct FCoverBox { float X, Y, Z, H, Hx, Hy, C, S; };   // C, S: cos and sin of the yaw
	constexpr float CellCm = 1000.f;
	TArray<FCoverBox> Boxes[JeebSightCover::NumSources];
	TMap<FIntPoint, TArray<int32>> Grid[JeebSightCover::NumSources];
	inline FIntPoint CellOf(float X, float Y) { return FIntPoint(FMath::FloorToInt(X / CellCm), FMath::FloorToInt(Y / CellCm)); }

	// slab test in the box's own frame; true if the segment enters it below its top and above its floor
	bool Hits(const FCoverBox& B, const FVector& A, const FVector& D)
	{
		const float Ax = A.X - B.X, Ay = A.Y - B.Y;
		const float Lx = Ax * B.C + Ay * B.S, Ly = -Ax * B.S + Ay * B.C;
		const float Dx = D.X * B.C + D.Y * B.S, Dy = -D.X * B.S + D.Y * B.C;
		float T0 = 0.f, T1 = 1.f;
		const float O[3] = { Lx, Ly, A.Z - B.Z }, V[3] = { Dx, Dy, D.Z }, Hf[3] = { B.Hx, B.Hy, 0.f };
		for (int32 i = 0; i < 3; ++i)
		{
			const float Lo = i == 2 ? 0.f : -Hf[i], Hi = i == 2 ? B.H : Hf[i];
			if (FMath::Abs(V[i]) < 1.0e-4f) { if (O[i] < Lo || O[i] > Hi) return false; continue; }
			float Ta = (Lo - O[i]) / V[i], Tb = (Hi - O[i]) / V[i];
			if (Ta > Tb) Swap(Ta, Tb);
			T0 = FMath::Max(T0, Ta); T1 = FMath::Min(T1, Tb);
			if (T0 > T1) return false;
		}
		return true;
	}
}

void JeebSightCover::Reset(ESource Src) { Boxes[Src].Reset(); Grid[Src].Reset(); }

void JeebSightCover::Add(ESource Src, float X, float Y, float Z, float Height, float HalfX, float HalfY, float YawDeg)
{
	const float R = FMath::DegreesToRadians(YawDeg);
	const int32 Id = Boxes[Src].Add({ X, Y, Z, Height, HalfX, HalfY, FMath::Cos(R), FMath::Sin(R) });
	const float Reach = FMath::Max(HalfX, HalfY) * 1.42f;
	const FIntPoint Lo = CellOf(X - Reach, Y - Reach), Hi = CellOf(X + Reach, Y + Reach);
	for (int32 gx = Lo.X; gx <= Hi.X; ++gx) for (int32 gy = Lo.Y; gy <= Hi.Y; ++gy) Grid[Src].FindOrAdd(FIntPoint(gx, gy)).Add(Id);
}

int32 JeebSightCover::Count(ESource Src) { return Boxes[Src].Num(); }

bool JeebSightCover::Blocked(const FVector& A, const FVector& B)
{
	const FVector D = B - A;
	const FIntPoint Lo = CellOf(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), Hi = CellOf(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
	for (int32 s = 0; s < NumSources; ++s)
	{
		if (Boxes[s].Num() == 0) continue;
		for (int32 gx = Lo.X; gx <= Hi.X; ++gx)
			for (int32 gy = Lo.Y; gy <= Hi.Y; ++gy)
				if (const TArray<int32>* C = Grid[s].Find(FIntPoint(gx, gy)))
					for (int32 Id : *C) if (Hits(Boxes[s][Id], A, D)) return true;
	}
	return false;
}
