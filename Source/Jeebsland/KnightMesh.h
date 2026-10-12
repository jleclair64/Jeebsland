#pragma once

#include "CoreMinimal.h"

// A tiny toolkit for building smooth 3D shapes in code (spheres, rounded boxes, lathed armour plates, tapered
// blades, swept plumes, tubes). Every builder appends triangles to an FMD in the shape's own coordinates;
// ComputeNormals() then gives smooth lighting. No Unreal assets are needed.
namespace KM
{
	struct FMD
	{
		TArray<FVector> V;
		TArray<int32> I;
	};
	using FRing = TArray<FVector>;

	inline double DMax(double A, double B) { return A > B ? A : B; }
	inline double DMin(double A, double B) { return A < B ? A : B; }
	inline double DClamp(double X, double A, double B) { return X < A ? A : (X > B ? B : X); }
	inline double SPow(double X, double E) { return X < 0.0 ? -FMath::Pow(-X, E) : FMath::Pow(X, E); }

	// Where a shape goes: rotate (degrees about X, then Y, then Z; right-handed), then move
	struct FXf
	{
		FVector Loc = FVector(0.0, 0.0, 0.0);
		FVector Rot = FVector(0.0, 0.0, 0.0);
	};
	inline FXf Xf(const FVector& Loc, const FVector& Rot = FVector(0.0, 0.0, 0.0)) { FXf X; X.Loc = Loc; X.Rot = Rot; return X; }

	inline FVector RotateEuler(const FVector& P, const FVector& E)
	{
		const double ax = FMath::DegreesToRadians(E.X), ay = FMath::DegreesToRadians(E.Y), az = FMath::DegreesToRadians(E.Z);
		const double cx = FMath::Cos(ax), sx = FMath::Sin(ax), cy = FMath::Cos(ay), sy = FMath::Sin(ay), cz = FMath::Cos(az), sz = FMath::Sin(az);
		const double y1 = P.Y * cx - P.Z * sx, z1 = P.Y * sx + P.Z * cx, x1 = P.X;
		const double x2 = x1 * cy + z1 * sy, z2 = -x1 * sy + z1 * cy, y2 = y1;
		return FVector(x2 * cz - y2 * sz, x2 * sz + y2 * cz, z2);
	}

	inline void Append(FMD& Dst, const FMD& Src, const FXf& X, bool bFlat)
	{
		if (bFlat)
		{
			// One private copy of the corners per triangle: sharp edges
			for (int32 t = 0; t + 2 < Src.I.Num(); t += 3)
			{
				const int32 Base = Dst.V.Num();
				for (int32 k = 0; k < 3; ++k)
				{
					Dst.V.Add(RotateEuler(Src.V[Src.I[t + k]], X.Rot) + X.Loc);
					Dst.I.Add(Base + k);
				}
			}
			return;
		}
		const int32 Base = Dst.V.Num();
		for (int32 i = 0; i < Src.V.Num(); ++i) Dst.V.Add(RotateEuler(Src.V[i], X.Rot) + X.Loc);
		for (int32 i = 0; i < Src.I.Num(); ++i) Dst.I.Add(Base + Src.I[i]);
	}

	// Smooth vertex normals (area weighted)
	inline void ComputeNormals(const FMD& M, TArray<FVector>& N)
	{
		N.SetNum(M.V.Num());
		for (int32 i = 0; i < N.Num(); ++i) N[i] = FVector(0.0, 0.0, 0.0);
		for (int32 t = 0; t + 2 < M.I.Num(); t += 3)
		{
			const FVector& A = M.V[M.I[t]];
			const FVector& B = M.V[M.I[t + 1]];
			const FVector& C = M.V[M.I[t + 2]];
			const FVector Fn = FVector::CrossProduct(B - A, C - A);
			N[M.I[t]] += Fn; N[M.I[t + 1]] += Fn; N[M.I[t + 2]] += Fn;
		}
		for (int32 i = 0; i < N.Num(); ++i)
		{
			const FVector Nn = N[i].GetSafeNormal();
			N[i] = (Nn.SizeSquared() > 0.5) ? Nn : FVector(0.0, 0.0, 1.0);
		}
	}

	// ------------------------------------------------------------------------------------------------
	// Loft: join a stack of rings (each the same number of points) into a surface. Rings go along the shape;
	// points in a ring go around it. bWrapRings joins the last ring back to the first (torus-like shapes).
	// Caps close the first and last ring with a flat disc. The result faces outward.
	// bCrisp: each strip between two neighbouring points of a ring keeps its own corners, so the strips meet at sharp edges
	// (the faces of a plate), while each strip still shades smoothly along the shape. That keeps plate armour crisp without
	// breaking it into lots of little mirror-like facets.
	// ------------------------------------------------------------------------------------------------
	inline void Solid(FMD& Dst, const TArray<FRing>& R, bool bWrapRings, bool bCapStart, bool bCapEnd, const FXf& X, bool bCrisp = false)
	{
		const int32 NR = R.Num();
		if (NR < 2) return;
		const int32 NP = R[0].Num();
		FMD M;
		TArray<int32> VRing;   // which ring each vertex came from
		if (bCrisp)
		{
			for (int32 j = 0; j < NP; ++j)
				for (int32 i = 0; i < NR; ++i) { M.V.Add(R[i][j]); M.V.Add(R[i][(j + 1) % NP]); VRing.Add(i); VRing.Add(i); }
		}
		else
		{
			for (int32 i = 0; i < NR; ++i)
				for (int32 j = 0; j < NP; ++j) { M.V.Add(R[i][j]); VRing.Add(i); }
		}
		// the corner on ring i at point j (bRight: the strip's right-hand corner, for strip j)
		auto Idx = [&](int32 i, int32 j, bool bRight) { return bCrisp ? (j * NR + i) * 2 + (bRight ? 1 : 0) : i * NP + ((j + (bRight ? 1 : 0)) % NP); };
		const int32 Segs = bWrapRings ? NR : NR - 1;
		for (int32 i = 0; i < Segs; ++i)
		{
			const int32 i2 = (i + 1) % NR;
			for (int32 j = 0; j < NP; ++j)
			{
				const int32 a = Idx(i, j, false), b = Idx(i, j, true), c = Idx(i2, j, false), d = Idx(i2, j, true);
				M.I.Add(a); M.I.Add(b); M.I.Add(c);
				M.I.Add(b); M.I.Add(d); M.I.Add(c);
			}
		}

		// Make the sides face away from the middle of their ring
		double Score = 0.0;
		for (int32 t = 0; t + 2 < M.I.Num(); t += 3)
		{
			const FVector& A = M.V[M.I[t]];
			const FVector& B = M.V[M.I[t + 1]];
			const FVector& C = M.V[M.I[t + 2]];
			const FVector Fn = FVector::CrossProduct(B - A, C - A);
			const int32 Ring = VRing[M.I[t]];
			FVector Cen(0.0, 0.0, 0.0);
			for (int32 j = 0; j < NP; ++j) Cen += R[Ring][j];
			Cen = Cen / double(NP);
			Score += FVector::DotProduct(Fn, (A + B + C) / 3.0 - Cen);
		}
		if (Score < 0.0)
		{
			for (int32 t = 0; t + 2 < M.I.Num(); t += 3)
			{
				const int32 Tmp = M.I[t + 1]; M.I[t + 1] = M.I[t + 2]; M.I[t + 2] = Tmp;
			}
		}

		// Flat caps (own vertices, so the edge stays crisp)
		auto Cap = [&](int32 Ring, const FVector& Outward)
		{
			FVector Cen(0.0, 0.0, 0.0);
			for (int32 j = 0; j < NP; ++j) Cen += R[Ring][j];
			Cen = Cen / double(NP);
			const int32 Base = M.V.Num();
			M.V.Add(Cen);
			for (int32 j = 0; j < NP; ++j) M.V.Add(R[Ring][j]);
			for (int32 j = 0; j < NP; ++j)
			{
				int32 p = Base + 1 + j, q = Base + 1 + ((j + 1) % NP);
				const FVector Fn = FVector::CrossProduct(M.V[p] - Cen, M.V[q] - Cen);
				if (FVector::DotProduct(Fn, Outward) < 0.0) { const int32 T = p; p = q; q = T; }
				M.I.Add(Base); M.I.Add(p); M.I.Add(q);
			}
		};
		auto Centroid = [&](int32 Ring)
		{
			FVector Cen(0.0, 0.0, 0.0);
			for (int32 j = 0; j < NP; ++j) Cen += R[Ring][j];
			return Cen / double(NP);
		};
		// each cap faces away from its neighbouring ring (right even when the shape curves round, like a band round a helm)
		if (bCapStart) Cap(0, (Centroid(0) - Centroid(1)).GetSafeNormal());
		if (bCapEnd) Cap(NR - 1, (Centroid(NR - 1) - Centroid(NR - 2)).GetSafeNormal());
		Append(Dst, M, X, false);
	}

	// Surface of revolution around Z. Profile points are (radius, height); a radius of 0 at an end closes it to a point.
	// sx / sy squash the cross-section. bClosedProfile joins the last profile point to the first (rings, tori).
	inline void Lathe(FMD& Dst, const TArray<FVector2D>& Prof, int32 Seg, double sx, double sy, const FXf& X, bool bClosedProfile = false, bool bFlat = false)
	{
		TArray<FRing> Rings;
		for (int32 k = 0; k < Prof.Num(); ++k)
		{
			FRing Rg;
			for (int32 s = 0; s < Seg; ++s)
			{
				const double A = 2.0 * PI * double(s) / double(Seg);
				Rg.Add(FVector(Prof[k].X * FMath::Cos(A) * sx, Prof[k].X * FMath::Sin(A) * sy, Prof[k].Y));
			}
			Rings.Add(Rg);
		}
		Solid(Dst, Rings, bClosedProfile, false, false, X, bFlat);
	}

	// A circle of profile points for ring-shaped tubes
	inline void Torus(FMD& Dst, double Rad, double Thick, int32 Seg, int32 SegTube, double sx, double sy, const FXf& X, bool bFlat = false)
	{
		TArray<FVector2D> P;
		for (int32 k = 0; k < SegTube; ++k)
		{
			const double T = 2.0 * PI * double(k) / double(SegTube);
			P.Add(FVector2D(Rad + Thick * FMath::Cos(T), Thick * FMath::Sin(T)));
		}
		Lathe(Dst, P, Seg, sx, sy, X, true, bFlat);
	}

	// Rounded blob or box: e = 1 is an ellipsoid, smaller e gives squarer corners
	inline void Ellipsoid(FMD& Dst, double rx, double ry, double rz, double e1, double e2, int32 SegU, int32 SegV, const FXf& X, bool bFlat = false)
	{
		TArray<FRing> Rings;
		for (int32 v = 0; v <= SegV; ++v)
		{
			const double Vv = -0.5 * PI + PI * double(v) / double(SegV);
			const double cv = SPow(FMath::Cos(Vv), e1), sv = SPow(FMath::Sin(Vv), e1);
			FRing Rg;
			for (int32 u = 0; u < SegU; ++u)
			{
				const double Uu = 2.0 * PI * double(u) / double(SegU);
				Rg.Add(FVector(rx * cv * SPow(FMath::Cos(Uu), e2), ry * cv * SPow(FMath::Sin(Uu), e2), rz * sv));
			}
			Rings.Add(Rg);
		}
		Solid(Dst, Rings, false, false, false, X, bFlat);
	}
	inline void Blob(FMD& Dst, double rx, double ry, double rz, const FXf& X) { Ellipsoid(Dst, rx, ry, rz, 1.0, 1.0, 18, 12, X); }
	inline void RBox(FMD& Dst, double hx, double hy, double hz, double Round, const FXf& X, bool bFlat = false) { Ellipsoid(Dst, hx, hy, hz, Round, Round, 14, 10, X, bFlat); }
	// A small faceted bead (rivets)
	inline void Bead(FMD& Dst, double R, const FVector& At) { Ellipsoid(Dst, R, R, R, 1.0, 1.0, 10, 6, FXf{ At, FVector(0.0, 0.0, 0.0) }); }

	// The rotation (degrees about X, then Y) that turns a shape built pointing down (-Z) to point along Dir
	inline FVector DownTo(const FVector& Dir)
	{
		const FVector D = Dir.GetSafeNormal();
		const double A = FMath::Asin(DClamp(D.Y, -1.0, 1.0));
		const double B = FMath::Atan2(-D.X, -D.Z);
		return FVector(FMath::RadiansToDegrees(A), FMath::RadiansToDegrees(B), 0.0);
	}

	// A slab that follows any path: RingAt(t) gives its cross-section for t from 0 to 1. Capped at both ends, crisp edges.
	inline void Ribbon(FMD& Dst, int32 N, TFunctionRef<FRing(double)> RingAt, bool bFlat = true)
	{
		TArray<FRing> R;
		for (int32 k = 0; k <= N; ++k) R.Add(RingAt(double(k) / double(N)));
		Solid(Dst, R, false, true, true, FXf(), bFlat);
	}

	// A flat plate: a convex outline (all in one plane) given thickness along Thick. Crisp edges.
	inline void Prism(FMD& Dst, const TArray<FVector>& Outline, const FVector& Thick)
	{
		TArray<FRing> R;
		R.Add(Outline);
		FRing B;
		for (const FVector& P : Outline) B.Add(P + Thick);
		R.Add(B);
		Solid(Dst, R, false, true, true, FXf(), true);
	}

	// A tube along a path whose cross-section (a wide, b thick) changes along the way; Exp < 1 squares the section.
	inline void Sweep(FMD& Dst, const TArray<FVector>& Path, const TArray<FVector2D>& AB, const FVector& Side, int32 Seg, double Exp, const FXf& X, bool bFlat = false)
	{
		TArray<FRing> Rings;
		const int32 N = Path.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector T = (Path[i + 1 < N ? i + 1 : i] - Path[i > 0 ? i - 1 : i]).GetSafeNormal();
			const FVector S = (Side - T * FVector::DotProduct(Side, T)).GetSafeNormal();
			const FVector U = FVector::CrossProduct(T, S).GetSafeNormal();
			FRing Rg;
			for (int32 s = 0; s < Seg; ++s)
			{
				const double A = 2.0 * PI * double(s) / double(Seg);
				Rg.Add(Path[i] + S * (AB[i].X * SPow(FMath::Cos(A), Exp)) + U * (AB[i].Y * SPow(FMath::Sin(A), Exp)));
			}
			Rings.Add(Rg);
		}
		Solid(Dst, Rings, false, true, true, X, bFlat);
	}

	// A tube that follows a closed outline (a shield rim)
	inline void TubeLoop(FMD& Dst, const TArray<FVector>& Loop, const FVector& Axis, double a, double b, int32 Seg, const FXf& X)
	{
		TArray<FRing> Rings;
		const int32 N = Loop.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector T = (Loop[(i + 1) % N] - Loop[(i + N - 1) % N]).GetSafeNormal();
			const FVector Nm = FVector::CrossProduct(T, Axis).GetSafeNormal();
			FRing Rg;
			for (int32 s = 0; s < Seg; ++s)
			{
				const double A = 2.0 * PI * double(s) / double(Seg);
				Rg.Add(Loop[i] + Nm * (a * FMath::Cos(A)) + Axis * (b * FMath::Sin(A)));
			}
			Rings.Add(Rg);
		}
		Solid(Dst, Rings, true, false, false, X);
	}

	// An open surface (a flat face or ribbon): the triangles are turned to face Dir
	inline void Patch(FMD& Dst, const TArray<FRing>& R, const FVector& Dir, const FXf& X)
	{
		const int32 NR = R.Num();
		if (NR < 2) return;
		const int32 NP = R[0].Num();
		FMD M;
		for (int32 i = 0; i < NR; ++i)
			for (int32 j = 0; j < NP; ++j) M.V.Add(R[i][j]);
		for (int32 i = 0; i + 1 < NR; ++i)
			for (int32 j = 0; j + 1 < NP; ++j)
			{
				const int32 a = i * NP + j, b = i * NP + j + 1, c = (i + 1) * NP + j, d = (i + 1) * NP + j + 1;
				M.I.Add(a); M.I.Add(b); M.I.Add(c);
				M.I.Add(b); M.I.Add(d); M.I.Add(c);
			}
		double Score = 0.0;
		for (int32 t = 0; t + 2 < M.I.Num(); t += 3)
			Score += FVector::DotProduct(FVector::CrossProduct(M.V[M.I[t + 1]] - M.V[M.I[t]], M.V[M.I[t + 2]] - M.V[M.I[t]]), Dir);
		if (Score < 0.0)
			for (int32 t = 0; t + 2 < M.I.Num(); t += 3) { const int32 T = M.I[t + 1]; M.I[t + 1] = M.I[t + 2]; M.I[t + 2] = T; }
		Append(Dst, M, X, false);
	}
}
