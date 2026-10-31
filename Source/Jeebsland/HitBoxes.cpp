#include "HitBoxes.h"
#include "HealthComponent.h"
#include "KnightDesign.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

namespace JeebHit
{
	float SegmentDistance(const FVector& P0, const FVector& P1, const FVector& Q0, const FVector& Q1)
	{
		FVector A, B;
		FMath::SegmentDistToSegmentSafe(P0, P1, Q0, Q1, A, B);
		return FVector::Dist(A, B);
	}

	void KnightHurt(const FKnightJoints& J, TArray<FCapsule>& Out)
	{
		using namespace KnightDesign;
		// a point given in capsule space (where the body is modelled), carried by the joint it hangs off
		auto At = [](const USceneComponent* C, int32 Pivot, const FVector& P) { return C->GetComponentTransform().TransformPosition(P - PivotPos(Pivot)); };
		auto Add = [&](const FVector& A, const FVector& B, float R) { FCapsule C; C.A = A; C.B = B; C.R = R; Out.Add(C); };
		if (J.Head)  Add(At(J.Head, P_Head, FVector(3, 0, 36)), At(J.Head, P_Head, FVector(3, 0, 50)), 17.f);      // the helm
		if (J.Upper) Add(At(J.Upper, P_Upper, FVector(0, 0, -34)), At(J.Upper, P_Upper, FVector(0, 0, 4)), 22.f); // chest down to the hips
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const USceneComponent* Arm = sd < 0 ? J.ArmL : J.ArmR;
			const USceneComponent* Elb = sd < 0 ? J.ElbowL : J.ElbowR;
			const USceneComponent* Hip = sd < 0 ? J.HipL : J.HipR;
			const USceneComponent* Knee = sd < 0 ? J.KneeL : J.KneeR;
			const USceneComponent* Ankle = sd < 0 ? J.AnkleL : J.AnkleR;
			if (Arm && Elb) Add(Arm->GetComponentLocation(), Elb->GetComponentLocation(), 9.f);                     // upper arm
			if (Elb)                                                                                                // forearm to the fist
			{
				const FVector Fist = (sd > 0 && J.WristR) ? At(J.WristR, P_WristR, FistPos(1)) : At(Elb, sd < 0 ? P_ElbowL : P_ElbowR, FistPos(sd));
				Add(Elb->GetComponentLocation(), Fist, 8.f);
			}
			if (Hip && Knee) Add(Hip->GetComponentLocation(), Knee->GetComponentLocation(), 10.f);                  // thigh
			if (Knee && Ankle) Add(Knee->GetComponentLocation(), At(Ankle, sd < 0 ? P_AnkleL : P_AnkleR, FVector(2, sd * 13, -90)), 8.f);   // shin, down to the sole
		}
	}

	void HurtOf(const UHealthComponent* H, TArray<FCapsule>& Out)
	{
		if (!H) return;
		if (H->GatherHurt) { H->GatherHurt(Out); return; }
		const FVector C = H->HitCentre();
		FCapsule Cap; Cap.A = C - FVector(0, 0, 50); Cap.B = C + FVector(0, 0, 50); Cap.R = 35.f;
		Out.Add(Cap);
	}

	static int32 GShow = -1;   // -1: not chosen yet (on in MoveLab only)

	bool ShowBoxes(const UWorld* World)
	{
		if (GShow >= 0) return GShow > 0;
		return World && World->GetMapName().Contains(TEXT("MoveLab"));
	}

	void ToggleShow(const UWorld* World)
	{
		GShow = ShowBoxes(World) ? 0 : 1;
	}

	void AddCapsuleMesh(const FCapsule& C, const FLinearColor& Col, TArray<FVector>& V, TArray<FVector>& N, TArray<int32>& I, TArray<FLinearColor>& Colors)
	{
		constexpr int32 Seg = 16, Lat = 5;   // around, and rings per half sphere
		FVector Ax = C.B - C.A;
		const FVector Z = Ax.IsNearlyZero() ? FVector::UpVector : Ax.GetSafeNormal();
		const FVector X = FMath::Abs(Z.Z) < 0.9f ? (FVector::UpVector ^ Z).GetSafeNormal() : (FVector::ForwardVector ^ Z).GetSafeNormal();
		const FVector Y = Z ^ X;
		const int32 Base = V.Num();
		int32 Rings = 0;
		for (int32 h = 0; h < 2; ++h)   // the top half sphere round B, then the bottom one round A (the band between is the tube)
			for (int32 k = 0; k <= Lat; ++k)
			{
				const float Phi = PI * 0.5f * (h == 0 ? (float)k / Lat : 1.f + (float)k / Lat);   // from the top pole down
				const FVector Ctr = (h == 0) ? C.B : C.A;
				for (int32 s = 0; s < Seg; ++s)
				{
					const float Th = 2.f * PI * s / Seg;
					const FVector Dir = Z * FMath::Cos(Phi) + (X * FMath::Cos(Th) + Y * FMath::Sin(Th)) * FMath::Sin(Phi);
					V.Add(Ctr + Dir * C.R);
					N.Add(Dir);
					Colors.Add(Col);
				}
				++Rings;
			}
		for (int32 r = 0; r + 1 < Rings; ++r)
			for (int32 s = 0; s < Seg; ++s)
			{
				const int32 A0 = Base + r * Seg + s, A1 = Base + r * Seg + (s + 1) % Seg;
				const int32 B0 = A0 + Seg, B1 = A1 + Seg;
				I.Add(A0); I.Add(B0); I.Add(A1);
				I.Add(A1); I.Add(B0); I.Add(B1);
			}
	}

	// How far a point is from the wedge (0 inside), worked out flat (the 90-degree sector) and upright (its floor and top)
	static float WedgeDist(const FVector& P, const FShieldWedge& W)
	{
		const FVector Right = FVector::UpVector ^ W.Fwd;   // (his right, with X forward and Z up)
		const FVector D = P - W.Apex;
		const FVector2D Q(FVector::DotProduct(D, W.Fwd), FVector::DotProduct(D, Right));
		const float Len = Q.Size();
		const float Ang = FMath::RadiansToDegrees(FMath::Atan2(Q.Y, Q.X));
		float Flat = 0.f;
		if (FMath::Abs(Ang) > W.HalfAngle || Len > W.Radius)
		{
			const float Ha = FMath::DegreesToRadians(W.HalfAngle);
			auto ToLine = [&](float Side)   // the foul line on that side, from home plate to the fence
			{
				const FVector2D E(FMath::Cos(Ha) * W.Radius, Side * FMath::Sin(Ha) * W.Radius);
				const float T = FMath::Clamp(FVector2D::DotProduct(Q, E) / E.SizeSquared(), 0.f, 1.f);
				return (Q - E * T).Size();
			};
			Flat = FMath::Min(ToLine(1.f), ToLine(-1.f));
			if (FMath::Abs(Ang) <= W.HalfAngle) Flat = FMath::Min(Flat, Len - W.Radius);   // beyond the fence
		}
		const float Z = float(D.Z);
		const float Up = FMath::Max3(0.f, W.ZLow - Z, Z - W.ZHigh);
		return FMath::Sqrt(Flat * Flat + Up * Up);
	}

	bool TouchWedge(const FCapsule& Hit, const FShieldWedge& W)
	{
		// (points along the hit box's sweep, close enough together that it can't slip past a wall between two of them)
		const float Len = FVector::Dist(Hit.A, Hit.B);
		const int32 N = FMath::Clamp(FMath::CeilToInt(Len / FMath::Max(Hit.R * 0.5f, 1.f)), 1, 32);
		for (int32 i = 0; i <= N; ++i)
			if (WedgeDist(FMath::Lerp(Hit.A, Hit.B, (float)i / N), W) <= Hit.R) return true;
		return false;
	}

	bool ShieldOf(const UHealthComponent* H, FShieldWedge& Out)
	{
		return H && H->GatherShield && H->GatherShield(Out);
	}

	// The view: the two foul-line walls and the curved outfield wall, see-through
	void AddWedgeMesh(const FShieldWedge& W, const FLinearColor& Col, TArray<FVector>& V, TArray<FVector>& N, TArray<int32>& I, TArray<FLinearColor>& Colors)
	{
		const FVector Right = FVector::UpVector ^ W.Fwd;
		const float Ha = FMath::DegreesToRadians(W.HalfAngle);
		auto Dir = [&](float A) { return W.Fwd * FMath::Cos(A) + Right * FMath::Sin(A); };
		auto Wall = [&](const FVector& P0, const FVector& P1, const FVector& Nrm)   // an upright quad from P0 to P1 (flat points)
		{
			const int32 B = V.Num();
			V.Add(P0 + FVector(0, 0, W.ZLow)); V.Add(P1 + FVector(0, 0, W.ZLow)); V.Add(P1 + FVector(0, 0, W.ZHigh)); V.Add(P0 + FVector(0, 0, W.ZHigh));
			for (int32 k = 0; k < 4; ++k) { N.Add(Nrm); Colors.Add(Col); }
			I.Add(B); I.Add(B + 1); I.Add(B + 2); I.Add(B); I.Add(B + 2); I.Add(B + 3);
		};
		for (int32 sd = -1; sd <= 1; sd += 2)   // the foul lines
		{
			const FVector E = Dir(sd * Ha);
			Wall(W.Apex, W.Apex + E * W.Radius, (FVector::UpVector ^ E) * float(sd));
		}
		constexpr int32 Seg = 16;   // the outfield fence
		for (int32 s = 0; s < Seg; ++s)
		{
			const float A0 = FMath::Lerp(-Ha, Ha, (float)s / Seg), A1 = FMath::Lerp(-Ha, Ha, (float)(s + 1) / Seg);
			Wall(W.Apex + Dir(A0) * W.Radius, W.Apex + Dir(A1) * W.Radius, Dir(0.5f * (A0 + A1)));
		}
	}
}
