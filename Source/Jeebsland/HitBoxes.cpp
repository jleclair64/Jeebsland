#include "HitBoxes.h"
#include "HealthComponent.h"
#include "KnightDesign.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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

	bool InMoveLab(const UWorld* World)
	{
		return World && World->GetMapName().Contains(TEXT("MoveLab"));
	}

	bool ShowBoxes(const UWorld* World)
	{
		if (GShow >= 0) return GShow > 0;
		static const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("JeebShot")) && !FParse::Param(FCommandLine::Get(), TEXT("ShotBoxes"));   // (test pictures: off unless -ShotBoxes)
		if (bShot) return false;
		return InMoveLab(World);
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

	float HelmTop(const USceneComponent* Head)
	{
		using namespace KnightDesign;
		if (!Head) return -1.0e9f;
		return float(Head->GetComponentTransform().TransformPosition(FVector(3, 0, 50) - PivotPos(P_Head)).Z) + 17.f;   // (the helm capsule's top: KnightHurt)
	}

	// The top's height over a flat point (relative to the apex): seen from the front an arch, the helm's top in the middle,
	// curving down (a half ellipse) to ArchFrom at the left and right ends
	static float WedgeTop(const FVector2D& Q, const FShieldWedge& W)
	{
		const float Ha = FMath::DegreesToRadians(W.HalfAngle);
		const float Ang = FMath::Min(FMath::Atan2(FMath::Abs(Q.Y), FMath::Max(Q.X, 0.f)), Ha);
		const float U = FMath::Sin(Ang) / FMath::Sin(Ha);   // 0 in the middle, 1 at either end (how far across, seen from the front)
		return W.ArchFrom + (W.ZHigh - W.ArchFrom) * FMath::Sqrt(FMath::Max(0.f, 1.f - U * U));
	}

	// How far a point is from the shield (0 inside), worked out flat (the curved band) and upright (its floor and arched top)
	static float WedgeDist(const FVector& P, const FShieldWedge& W)
	{
		const FVector Right = FVector::UpVector ^ W.Fwd;   // (his right, with X forward and Z up)
		const FVector D = P - W.Apex;
		const FVector2D Q(FVector::DotProduct(D, W.Fwd), FVector::DotProduct(D, Right));
		const FVector2D Qa(Q.X, FMath::Abs(Q.Y));   // (the two halves are mirror images)
		const float Len = Q.Size();
		const float Ang = FMath::RadiansToDegrees(FMath::Atan2(Qa.Y, Qa.X));
		const float Ha = FMath::DegreesToRadians(W.HalfAngle);
		float Flat = 0.f;
		if (Ang <= W.HalfAngle) Flat = FMath::Max3(0.f, W.Inner - Len, Len - W.Radius);   // in front: in, behind or beyond the band
		else
		{
			// round the side: the band's end, along the foul line
			const FVector2D E(FMath::Cos(Ha), FMath::Sin(Ha));
			const float T = FMath::Clamp(FVector2D::DotProduct(Qa, E), W.Inner, W.Radius);
			Flat = (Qa - E * T).Size();
		}
		const float Z = float(D.Z);
		const float Up = FMath::Max3(0.f, W.ZLow - Z, Z - WedgeTop(Q, W));
		return FMath::Sqrt(Flat * Flat + Up * Up);
	}

	// (points along the hit box's sweep, close enough together that it can't slip past a wall or a limb between two of them)
	static int32 SweepSteps(const FCapsule& Hit)
	{
		return FMath::Clamp(FMath::CeilToInt(FVector::Dist(Hit.A, Hit.B) / FMath::Max(Hit.R * 0.5f, 1.f)), 1, 32);
	}

	float FirstTouchWedge(const FCapsule& Hit, const FShieldWedge& W)
	{
		const int32 N = SweepSteps(Hit);
		for (int32 i = 0; i <= N; ++i)
			if (WedgeDist(FMath::Lerp(Hit.A, Hit.B, (float)i / N), W) <= Hit.R) return (float)i / N;
		return 2.f;
	}

	float FirstTouch(const FCapsule& Hit, const TArray<FCapsule>& Hurt)
	{
		const int32 N = SweepSteps(Hit);
		for (int32 i = 0; i <= N; ++i)
		{
			const FVector P = FMath::Lerp(Hit.A, Hit.B, (float)i / N);
			for (const FCapsule& C : Hurt)
				if (FMath::PointDistToSegment(P, C.A, C.B) <= Hit.R + C.R) return (float)i / N;
		}
		return 2.f;
	}

	bool TouchWedge(const FCapsule& Hit, const FShieldWedge& W)
	{
		return FirstTouchWedge(Hit, W) <= 1.f;
	}

	bool ShieldOf(const UHealthComponent* H, FShieldWedge& Out)
	{
		return H && H->GatherShield && H->GatherShield(Out);
	}

	FVector LaunchVelocity(const FVector& Away, const FVector& DI)
	{
		const FVector Base = Away.GetSafeNormal2D() * LaunchBack + FVector(0.f, 0.f, LaunchUp);
		const FVector Dir = Base.GetSafeNormal();
		const FVector D = FVector(DI.X, DI.Y, 0.f).GetClampedToMaxSize(1.f);
		const FVector Square = D - Dir * FVector::DotProduct(D, Dir);   // (the part of the DI square to the launch)
		if (Square.SizeSquared() < 1.0e-4f) return Base;
		const float A = FMath::DegreesToRadians(DIAngle) * FMath::Min(Square.Size(), 1.f);
		return (Dir * FMath::Cos(A) + Square.GetSafeNormal() * FMath::Sin(A)) * Base.Size();
	}

	// The view: the band as a closed see-through solid (its arched top, floor, outer and inner faces and the two ends), laid
	// out on a grid round the arc (across) and from its inner face to its outer face (out)
	void AddWedgeMesh(const FShieldWedge& W, const FLinearColor& Col, TArray<FVector>& V, TArray<FVector>& N, TArray<int32>& I, TArray<FLinearColor>& Colors)
	{
		constexpr int32 NA = 48, NO = 2;   // across (fine, for the arch's steep ends), out
		const FVector Right = FVector::UpVector ^ W.Fwd;
		const float Ha = FMath::DegreesToRadians(W.HalfAngle);
		auto Flat = [&](int32 a, int32 o)   // a grid point, flat, in the wedge's frame
		{
			const float Th = FMath::Lerp(-Ha, Ha, (float)a / NA);
			const float Rho = FMath::Lerp(W.Inner, W.Radius, (float)o / NO);
			return FVector2D(FMath::Cos(Th) * Rho, FMath::Sin(Th) * Rho);
		};
		auto World = [&](const FVector2D& Q, float Z) { return W.Apex + W.Fwd * Q.X + Right * Q.Y + FVector(0, 0, Z); };
		const int32 Top = V.Num();
		for (int32 a = 0; a <= NA; ++a)
			for (int32 o = 0; o <= NO; ++o) { const FVector2D Q = Flat(a, o); V.Add(World(Q, WedgeTop(Q, W))); N.Add(FVector::UpVector); Colors.Add(Col); }
		const int32 Bot = V.Num();
		for (int32 a = 0; a <= NA; ++a)
			for (int32 o = 0; o <= NO; ++o) { V.Add(World(Flat(a, o), W.ZLow)); N.Add(-FVector::UpVector); Colors.Add(Col); }
		auto Id = [&](int32 Base, int32 a, int32 o) { return Base + a * (NO + 1) + o; };
		auto Quad = [&](int32 A, int32 B, int32 C, int32 D) { I.Add(A); I.Add(B); I.Add(C); I.Add(A); I.Add(C); I.Add(D); };
		for (int32 a = 0; a < NA; ++a)
			for (int32 o = 0; o < NO; ++o)
			{
				Quad(Id(Top, a, o), Id(Top, a, o + 1), Id(Top, a + 1, o + 1), Id(Top, a + 1, o));   // the top
				Quad(Id(Bot, a, o), Id(Bot, a + 1, o), Id(Bot, a + 1, o + 1), Id(Bot, a, o + 1));   // the floor
			}
		for (int32 a = 0; a < NA; ++a)   // the outer and inner faces
		{
			Quad(Id(Bot, a, NO), Id(Bot, a + 1, NO), Id(Top, a + 1, NO), Id(Top, a, NO));
			Quad(Id(Bot, a, 0), Id(Top, a, 0), Id(Top, a + 1, 0), Id(Bot, a + 1, 0));
		}
		for (int32 o = 0; o < NO; ++o)   // the two ends, along the foul lines
		{
			Quad(Id(Bot, 0, o), Id(Top, 0, o), Id(Top, 0, o + 1), Id(Bot, 0, o + 1));
			Quad(Id(Bot, NA, o), Id(Bot, NA, o + 1), Id(Top, NA, o + 1), Id(Top, NA, o));
		}
	}
}
