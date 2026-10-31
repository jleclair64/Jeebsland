#pragma once

#include "CoreMinimal.h"
#include "KnightMesh.h"
#include <initializer_list>

// The knight's look: every armour plate, rivet and trim, modelled in code.
// All coordinates are "capsule space" in cm: feet at Z = -96, forward = +X, right = +Y.
// A part hangs off one pivot of the skeleton so it moves with that body part.
//
// The style (owner's ChatGPT concept sheet, 2026-10-29): a stocky, cartoony knight in plain bevelled plate over dark padded
// cloth. A round armet with a pointed visor (one eye slit, breathing slits, round ear rosettes), a tall collar, a broad
// breastplate with a raised V at the neck, a leather belt with a round buckle, a pointed plate over the front, big
// faceted pauldrons, big vambraces, dark gloves, large knee cops, broad greaves and sabatons. No gold trim: the plate
// takes the tier colour. Each limb piece hangs off its own joint (shoulder, elbow, hip, knee) so it bends cleanly.

enum class EArmorRole : uint8 { Plate, Under, Trim, Cloth, Accent, Leather, BootBody };

namespace KnightDesign
{
	using namespace KM;

	enum EPivot : int32
	{
		P_Body, P_Upper, P_Head, P_Plume, P_CapeU, P_CapeL, P_ArmL, P_ArmR, P_ElbowL, P_ElbowR, P_HipL, P_HipR, P_KneeL, P_KneeR, P_AnkleL, P_AnkleR, P_Count
	};
	enum ESlot : int32 { SlotHelmet = 0, SlotBody = 1, SlotShoulders = 2, SlotBoots = 3, SlotCape = 4, SlotGloves = 5, SlotShield = 6, SlotBrush = 7, SlotRingR = 8, SlotRingL = 9, SlotAmulet = 10, SlotNone = -1 };   // SlotBrush: the Red Helm Brush (worn in a misc slot)
	constexpr int32 NumSlots = 11;

	// Where each pivot sits (capsule space). The animation turns parts about these points.
	inline FVector PivotPos(int32 P)
	{
		switch (P)
		{
		case P_Body:   return FVector(0, 0, 0);
		case P_Upper:  return FVector(0, 0, -41);
		case P_Head:   return FVector(0, 0, 23);
		case P_Plume:  return FVector(-2, 0, 71);
		case P_CapeU:  return FVector(-26, 0, 13);
		case P_CapeL:  return FVector(-26, 0, -19);
		// (moved 2026-10-29 to the joints of the Meshy model, see KnightModel.h and Tools\knight_blender_export.py)
		case P_ArmL:   return FVector(-3, -29, 14);    // shoulders: broad, under the big pauldrons
		case P_ArmR:   return FVector(-3, 29, 14);
		case P_ElbowL: return FVector(-5, -34.5, -6);  // the arms hang a little out from the body
		case P_ElbowR: return FVector(-5, 34.5, -6);
		case P_HipL:   return FVector(0, -13, -26);
		case P_HipR:   return FVector(0, 13, -26);
		case P_KneeL:  return FVector(-1, -13, -51);   // (the model's legs are straightened to hang under the hips)
		case P_KneeR:  return FVector(-1, 13, -51);
		case P_AnkleL: return FVector(-4, -13, -82);   // the feet turn here, so they can stay flat while the knees bend
		case P_AnkleR: return FVector(-4, 13, -82);
		}
		return FVector(0, 0, 0);
	}
	// The middle of each fist, and where a held weapon's grip sits in it
	// (measured on the model's fists, 2026-10-29, after they were tipped 20 degrees forward to line up with a weapon's grip)
	inline FVector FistPos(int32 Side) { return FVector(3.6, Side * 36.5, -37.0); }
	// The sword's grip: through the curl of the fingers, which close front to back (palm to the thigh, back of the hand out,
	// thumb over the front). Lined up in Blender renders of the model's fist, 2026-10-30.
	inline FVector GripPos(bool bLeft) { return FVector(-3.9, bLeft ? -38.7 : 38.7, -36.9); }   // (slid 3 cm back along the blade so the guard clears the thumb)
	// Which way a held blade points out of the fist (Side +1 right hand): out past the thumb, forward and a little down
	inline FVector BladeDir(int32 Side) { return FVector(0.97, -Side * 0.06, -0.21).GetSafeNormal(); }
	inline FVector WristPos(int32 Side) { return FVector(-2.0, Side * 37.0, -29.0); }
	// Lengths of the thigh and the shin (for keeping the feet on the ground as the knees bend)
	inline double ThighLen() { return FVector::Dist(PivotPos(P_HipR), PivotPos(P_KneeR)); }
	inline double ShinLen() { return FVector::Dist(PivotPos(P_KneeR), PivotPos(P_AnkleR)); }
	// The feet are drawn narrower than the model made them (owner, 2026-10-30: 30% narrower, full length). Everything hanging
	// off an ankle is shrunk in width towards the ankle, easing in over the first 8 cm under it so it still meets the greave;
	// heights are kept, so the soles stay on the ground. Used wherever a piece's mesh is made (player, pen knight, model).
	constexpr double FootScale = 0.7;
	inline bool IsFootPivot(int32 P) { return P == P_AnkleL || P == P_AnkleR; }
	inline double FootShrink(int32 P, double Z)
	{
		if (!IsFootPivot(P)) return 1.0;
		const double W = FMath::SmoothStep(0.0, 1.0, FMath::Clamp((PivotPos(P).Z + 2.0 - Z) / 8.0, 0.0, 1.0));
		return FMath::Lerp(1.0, FootScale, W);
	}
	inline FVector FootVertex(int32 P, const FVector& V)
	{
		const double S = FootShrink(P, V.Z);
		const FVector A = PivotPos(P);
		return FVector(V.X, A.Y + (V.Y - A.Y) * S, V.Z);   // (width only: the owner kept the full length, 2026-10-30)
	}
	inline FVector FootNormal(int32 P, const FVector& V, const FVector& N)
	{
		const double S = FootShrink(P, V.Z);
		return FVector(N.X, N.Y / S, N.Z).GetSafeNormal();
	}

	struct FDef
	{
		int32 Pivot = P_Body;
		int32 Slot = SlotNone;       // which armour set it belongs to (-1 = fixed gear)
		EArmorRole Role = EArmorRole::Plate;
		int32 MinTier = 0, MaxTier = 9;
		float Shade = 1.f;           // darkens the palette colour (the fists)
		FLinearColor Fixed = FLinearColor(1.f, 1.f, 1.f, 1.f);
		bool bFixedMetal = false;
		bool bBare = false;          // skin and hair: shown only while nothing is worn in this slot (uses Fixed)
		bool bFixedColor = false;    // always painted Fixed (matte), whatever the tier: the black eye openings, the red brush
		int32 Style = 0;             // shields: which of the shield styles this piece belongs to (only the worn one shows)
		FMD Mesh;
	};

	// ---- small helpers ----
	inline FVector V3(double x, double y, double z) { return FVector(x, y, z); }
	inline TArray<FVector2D> Prof(std::initializer_list<double> L)
	{
		TArray<FVector2D> A;
		const double* It = L.begin();
		while (It != L.end()) { const double r = *It++; const double z = *It++; A.Add(FVector2D(r, z)); }
		return A;
	}
	inline FVector Off(const FVector& Loc, const FVector& Rot, const FVector& Local) { return Loc + RotateEuler(Local, Rot); }
	inline double Lerp1(const TArray<FVector2D>& P, double X) // piecewise linear y(x), x ascending
	{
		if (X <= P[0].X) return P[0].Y;
		for (int32 i = 0; i + 1 < P.Num(); ++i)
			if (X <= P[i + 1].X)
			{
				const double T = (X - P[i].X) / DMax(P[i + 1].X - P[i].X, 1e-6);
				return P[i].Y + (P[i + 1].Y - P[i].Y) * T;
			}
		return P[P.Num() - 1].Y;
	}

	// A point on (or H cm off) a many-sided lathe with one corner straight ahead: angle Th in degrees (0 ahead, + to the
	// right), height Z. Prof is the lathe's profile (radius, height), Sx squashes it front to back, C is where its axis is.
	// Bands and ridges laid on with this sit flat on the faces of the plate underneath.
	struct FOct
	{
		TArray<FVector2D> ZR;
		double Sx = 1.0;
		FVector C = FVector::ZeroVector;
		int32 Sides = 8;
		FOct(const TArray<FVector2D>& Pr, double InSx, const FVector& InC, int32 InSides) : Sx(InSx), C(InC), Sides(InSides)
		{
			for (const FVector2D& P : Pr) if (P.X > 0.0) ZR.Add(FVector2D(P.Y, P.X));
			ZR.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
		}
		double R(double Z) const { return Lerp1(ZR, Z); }
		FVector At(double Th, double Z, double H) const
		{
			const double Step = 360.0 / Sides;
			double M = FMath::Fmod(Th, Step);
			if (M < 0.0) M += Step;
			const double K = FMath::Cos(FMath::DegreesToRadians(0.5 * Step)) / FMath::Cos(FMath::DegreesToRadians(M - 0.5 * Step));
			const double Rr = (R(Z) + H) * K, A = FMath::DegreesToRadians(Th);
			return C + FVector(Rr * FMath::Cos(A) * Sx, Rr * FMath::Sin(A), Z);
		}
	};

	// Even spacing along a polyline (N + 1 points)
	inline TArray<FVector> Resample(const TArray<FVector>& P, int32 N)
	{
		TArray<double> L;
		L.Add(0.0);
		for (int32 i = 1; i < P.Num(); ++i) L.Add(L.Last() + FVector::Distance(P[i], P[i - 1]));
		TArray<FVector> Res;
		for (int32 k = 0; k <= N; ++k)
		{
			const double S = L.Last() * double(k) / double(N);
			int32 i = 1;
			while (i < P.Num() - 1 && L[i] < S) ++i;
			const double T = (S - L[i - 1]) / DMax(L[i] - L[i - 1], 1e-6);
			Res.Add(P[i - 1] + (P[i] - P[i - 1]) * T);
		}
		return Res;
	}

	inline void Build(TArray<FDef>& Out)
	{
		auto Add = [&](int32 Pivot, int32 Slot, EArmorRole Role, int32 MinTier = 0, int32 MaxTier = 9) -> FDef&
		{
			FDef D; D.Pivot = Pivot; D.Slot = Slot; D.Role = Role; D.MinTier = MinTier; D.MaxTier = MaxTier;
			Out.Add(D);
			return Out.Last();
		};
		// A plate hoop round an axis at C: inner/outer radius at the top (Zt), outer/inner at the bottom (Zb). Faceted.
		auto Hoop = [&](FMD& M, const FVector& C, double Ti, double To, double Zt, double Bo, double Bi, double Zb, int32 Seg, double Sx)
		{
			Lathe(M, Prof({ Ti, Zt, To, Zt, Bo, Zb, Bi, Zb }), Seg, Sx, 1.0, Xf(C), true, true);
		};
		// A gold edge round the bottom of a hoop (outer radius R at height Z)
		auto Edge = [&](FMD& M, const FVector& C, double R, double Z, int32 Seg, double Sx)
		{
			Lathe(M, Prof({ R - 0.3, Z + 1.1, R + 0.5, Z + 1.1, R + 0.6, Z - 0.2, R - 0.2, Z - 0.2 }), Seg, Sx, 1.0, Xf(C), true, true);
		};
		// A rectangular cross-section between two surface points (A above B), from depth H0 to height H1 off the surface
		auto Quad = [](const FVector& A0, const FVector& A1, const FVector& B1, const FVector& B0) { return FRing({ A0, A1, B1, B0 }); };
		const FVector O0(0, 0, 0);

		// =========================================================================================
		// HELMET: a round armet (smooth dome with a low ridge from front to back) and a visor that juts forward to a crease
		// down the middle, its top edge peaking over the brow. One eye slit across it, four upright breathing slits below,
		// a raised edge round the visor, and a round rosette over each ear where the visor pivots.
		// =========================================================================================
		const double HSX = 1.12;
		const TArray<FVector2D> HelmP = Prof({ 0, 69, 5.5, 68.6, 9.5, 67.2, 12.8, 64.8, 15.0, 61.5, 16.3, 57.5, 16.6, 52, 16.5, 44, 16.0, 37, 15.0, 31.5, 14.0, 28.5, 13.6, 27, 0, 27 });
		const FOct Helm(HelmP, HSX, O0, 32);
		auto HS = [&](double Th, double Z, double H) { return Helm.At(Th, Z, H); };
		// The line of the crest: from the brow, over the top, down the back (evenly spaced points), with the outward direction
		// and the crest's height at each point (tallest on top). The Red Helm Brush stands on it.
		TArray<FVector> CP;
		{
			TArray<FVector> Mer;
			Mer.Add(V3(Helm.R(56.5) * HSX, 0, 56.5));
			for (int32 i = HelmP.Num() - 2; i >= 0; --i) if (HelmP[i].Y > 56.5) Mer.Add(V3(HelmP[i].X * HSX, 0, HelmP[i].Y));
			for (int32 i = 1; i < HelmP.Num() - 1; ++i) if (HelmP[i].Y > 40.0) Mer.Add(V3(-HelmP[i].X * HSX, 0, HelmP[i].Y));
			Mer.Add(V3(-Helm.R(40.0) * HSX, 0, 40.0));
			CP = Resample(Mer, 44);
		}
		auto CrestAt = [&](double T, FVector& P, FVector& N, double& H)
		{
			const int32 i = FMath::Clamp(FMath::RoundToInt(T * 44.0), 0, 44);
			P = CP[i];
			const FVector Tg = CP[FMath::Min(i + 1, 44)] - CP[FMath::Max(i - 1, 0)];
			N = FVector(Tg.Z, 0.0, -Tg.X);
			N.Normalize();
			if (FVector::DotProduct(N, P - V3(0, 0, 48)) < 0.0) N = -N;
			H = 0.6 + 0.6 * DClamp((P.Z - 40.0) / 29.0, 0.0, 1.0);
		};
		// The visor: its top edge (peaking over the brow), how far it stands off the dome (most at the middle: the prow),
		// and that height at any point of its face (it leans back a little towards the chin)
		const double VZb = 29.0, VTh = 78.0;
		auto VTop = [](double Th) { return 58.5 - 6.0 * FMath::Min(FMath::Abs(Th), 78.0) / 78.0; };
		auto VH = [](double Th) { return 1.0 + 3.2 * (1.0 - FMath::Min(FMath::Abs(Th), 78.0) / 78.0); };
		auto VHAt = [&](double Th, double Z) { return VH(Th) * (0.75 + 0.25 * DClamp((Z - VZb) / (VTop(Th) - VZb), 0.0, 1.0)); };
		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Plate);
			Lathe(D.Mesh, HelmP, 32, HSX, 1.0, Xf(O0));   // a smooth, polished dome
			for (int32 sd = -1; sd <= 1; sd += 2)
			{
				// each half of the visor (built in halves so the crease down the middle stays sharp)
				Ribbon(D.Mesh, 30, [&](double T) { const double Th = sd * VTh * T, Zt = VTop(Th);
					return Quad(HS(Th, Zt, -0.3), HS(Th, Zt, VHAt(Th, Zt)), HS(Th, VZb, VHAt(Th, VZb)), HS(Th, VZb, -0.3)); });
				// the raised edge along its top
				Ribbon(D.Mesh, 30, [&](double T) { const double Th = sd * VTh * T, Zt = VTop(Th);
					return Quad(HS(Th, Zt, VHAt(Th, Zt) - 0.2), HS(Th, Zt, VHAt(Th, Zt) + 0.7), HS(Th, Zt - 1.6, VHAt(Th, Zt - 1.6) + 0.7), HS(Th, Zt - 1.6, VHAt(Th, Zt - 1.6) - 0.2)); });
				// a step across the visor just under the eye slit
				Ribbon(D.Mesh, 24, [&](double T) { const double Th = sd * 60.0 * T;
					return Quad(HS(Th, 46.6, VHAt(Th, 46.6) - 0.2), HS(Th, 46.6, VHAt(Th, 46.6) + 0.55), HS(Th, 45.2, VHAt(Th, 45.2) + 0.55), HS(Th, 45.2, VHAt(Th, 45.2) - 0.2)); });
				// the rosette over the ear: a round disc with a low cone in the middle
				Lathe(D.Mesh, Prof({ 0, -0.5, 3.8, -0.5, 3.8, 0.7, 2.9, 1.3, 1.2, 2.6, 0, 3.0 }), 12, 1.0, 1.0, Xf(HS(sd * 92.0, 47.0, -0.2), V3(-90.0 * sd, 0, 0)), false, true);
			}
			// a low ridge from the brow, over the top and down the back
			Ribbon(D.Mesh, 44, [&](double T) {
				FVector P, N; double H;
				CrestAt(T, P, N, H);
				const FVector Y(0, 1, 0);
				return FRing({ P - Y * 1.0 - N * 0.3, P - Y * 1.0 + N * H, P + N * (H + 0.35), P + Y * 1.0 + N * H, P + Y * 1.0 - N * 0.3 }); });
			// the rim round the bottom
			Lathe(D.Mesh, Prof({ Helm.R(27.0) - 0.4, 27.0, Helm.R(27.0) + 1.0, 27.0, Helm.R(29.4) + 1.0, 29.4, Helm.R(29.4) - 0.4, 29.4 }), 32, HSX, 1.0, Xf(O0), true);
		}
		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Under);   // the dark openings: the eye slit and breathing slits (always black)
			D.bFixedColor = true; D.Fixed = FLinearColor(0.006f, 0.006f, 0.008f);
			for (int32 sd = -1; sd <= 1; sd += 2)
			{
				Ribbon(D.Mesh, 20, [&](double T) { const double Th = sd * 56.0 * T;
					return Quad(HS(Th, 50.6, VHAt(Th, 50.6) - 0.3), HS(Th, 50.6, VHAt(Th, 50.6) + 0.12), HS(Th, 47.2, VHAt(Th, 47.2) + 0.12), HS(Th, 47.2, VHAt(Th, 47.2) - 0.3)); });
				for (int32 k = 0; k < 2; ++k)   // two upright breathing slits each side of the crease
				{
					const double Th0 = sd * (6.5 + 9.0 * k);
					Ribbon(D.Mesh, 6, [&](double T) { const double Z = 42.6 - 8.0 * T, Dw = FMath::RadiansToDegrees(0.75 / Helm.R(Z));
						return Quad(HS(Th0 - Dw, Z, VHAt(Th0, Z) - 0.3), HS(Th0 - Dw, Z, VHAt(Th0 - Dw, Z) + 0.12), HS(Th0 + Dw, Z, VHAt(Th0 + Dw, Z) + 0.12), HS(Th0 + Dw, Z, VHAt(Th0, Z) - 0.3)); });
				}
			}
		}

		// The Red Helm Brush (shown while one is worn in a miscellaneous slot): a broad, square-cut red horsehair brush
		// standing along the crest, made of a solid core with rows of bristle tufts on both faces and along the top
		{
			const double T0 = 0.04, T1 = 0.90;
			// its outline along the crest: almost full height everywhere, rounding off only at the two ends
			auto Height = [](double U) { const double E = FMath::Min(U, 1.0 - U) / 0.12; return 11.0 * FMath::Sqrt(DClamp(E, 0.0, 1.0)) + 1.5; };
			const double Wd = 3.2;   // half its thickness
			{
				FDef& D = Add(P_Head, SlotBrush, EArmorRole::Accent);
				D.bFixedColor = true; D.Fixed = FLinearColor(0.55f, 0.03f, 0.02f);
				Ribbon(D.Mesh, 40, [&](double U) {
					FVector P, N; double H;
					CrestAt(T0 + (T1 - T0) * U, P, N, H);
					const double B0 = H + 0.4, Bh = Height(U);
					const FVector Y(0, 1, 0);
					return FRing({ P + N * B0 - Y * Wd, P + N * (B0 + Bh) - Y * Wd, P + N * (B0 + Bh) + Y * Wd, P + N * B0 + Y * Wd }); });
			}
			// bristles: thin tufts standing a little proud of the core, of uneven length, in two shades of red
			for (int32 Shade = 0; Shade < 2; ++Shade)
			{
				FDef& D = Add(P_Head, SlotBrush, EArmorRole::Accent);
				D.bFixedColor = true; D.Fixed = Shade == 0 ? FLinearColor(0.78f, 0.06f, 0.04f) : FLinearColor(0.36f, 0.015f, 0.01f);
				FRandomStream R(77 + Shade);
				for (int32 k = 0; k < 70; ++k)
				{
					const double U = R.FRandRange(0.02, 0.98);
					FVector P, N; double H;
					CrestAt(T0 + (T1 - T0) * U, P, N, H);
					const double B0 = H + 0.4, Bh = Height(U);
					const int32 Face = R.RandRange(0, 2);   // left face, right face, or the top
					const FVector Y(0, 1, 0);
					const double Bw = R.FRandRange(0.25, 0.45);
					FVector A, B;
					if (Face < 2)
					{
						const double S = Face == 0 ? -1.0 : 1.0, Z0 = R.FRandRange(0.0, 0.5) * Bh, Z1 = FMath::Min(Bh + R.FRandRange(0.2, 1.4), Z0 + R.FRandRange(0.4, 0.9) * Bh);
						A = P + N * (B0 + Z0) + Y * (S * (Wd + 0.15)); B = P + N * (B0 + Z1) + Y * (S * (Wd + 0.15));
						RBox(D.Mesh, Bw, 0.2, 0.5 * (Z1 - Z0), 0.6, Xf((A + B) * 0.5, V3(0, FMath::RadiansToDegrees(FMath::Atan2(N.X, N.Z)), 0)));
					}
					else
					{
						const double Ys = R.FRandRange(-Wd + 0.3, Wd - 0.3), Up = R.FRandRange(0.4, 1.6);
						A = P + N * (B0 + Bh - 0.5) + Y * Ys; B = A + N * (Up + 0.5);
						RBox(D.Mesh, Bw, 0.3, 0.5 * (Up + 0.5), 0.6, Xf((A + B) * 0.5, V3(0, FMath::RadiansToDegrees(FMath::Atan2(N.X, N.Z)), 0)));
					}
				}
			}
		}

		// Bare head (shown while no helmet is worn): neck, head, hair, eyes (built a size smaller about the neck)
		const int32 FirstBare = Out.Num();
		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Under); D.bBare = true; D.Fixed = FLinearColor(0.80f, 0.58f, 0.42f);
			Lathe(D.Mesh, Prof({ 0, 12, 7.5, 12, 8.0, 24, 0, 28 }), 16, 1.0, 1.0, Xf(V3(0, 0, 0)));                       // neck
			Ellipsoid(D.Mesh, 17.0, 15.5, 21.0, 1.0, 1.0, 24, 16, Xf(V3(0, 0, 46.0)));                                      // head
			Blob(D.Mesh, 3.0, 2.6, 3.6, Xf(V3(16.5, 0, 44.0)));                                                              // nose
			Blob(D.Mesh, 2.2, 3.0, 3.4, Xf(V3(0, 16.0, 45.0))); Blob(D.Mesh, 2.2, 3.0, 3.4, Xf(V3(0, -16.0, 45.0)));          // ears
		}
		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Under); D.bBare = true; D.Fixed = FLinearColor(0.20f, 0.12f, 0.06f);   // hair
			Ellipsoid(D.Mesh, 18.0, 16.4, 13.0, 1.0, 1.0, 24, 12, Xf(V3(-1.0, 0, 55.0)));
			Blob(D.Mesh, 13.0, 15.0, 15.0, Xf(V3(-7.0, 0, 48.0)));
		}
		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Under); D.bBare = true; D.Fixed = FLinearColor(0.03f, 0.03f, 0.04f);   // eyes
			Blob(D.Mesh, 1.6, 2.2, 2.4, Xf(V3(15.2, 6.2, 49.0))); Blob(D.Mesh, 1.6, 2.2, 2.4, Xf(V3(15.2, -6.2, 49.0)));
		}
		for (int32 i = FirstBare; i < Out.Num(); ++i)
			for (FVector& V : Out[i].Mesh.V) V = V3(0, 0, 23) + (V - V3(0, 0, 23)) * 0.85;
		// The whole head sits a little lower (a shorter neck) and a little forward of the shoulders, the way people carry it
		for (FDef& D : Out)
			if (D.Pivot == P_Head)
				for (FVector& V : D.Mesh.V) { V.X += 3.5; V.Z -= 2.5; }

		// The dark padded cloth under the plate (sleeves, trousers, the skirt under the belt): always there, always charcoal
		const FLinearColor ClothCol(0.035f, 0.035f, 0.04f);
		auto Cloth = [&](int32 Pivot, int32 Slot) -> FDef& { FDef& D = Add(Pivot, Slot, EArmorRole::Under); D.bFixedColor = true; D.Fixed = ClothCol; return D; };

		// =========================================================================================
		// GORGET: a tall collar of two lames round the neck, flaring onto the chest
		// =========================================================================================
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Plate);
			Hoop(D.Mesh, O0, 11.0, 11.8, 30.5, 14.2, 13.4, 23.5, 8, 0.92);
			Hoop(D.Mesh, O0, 13.4, 14.2, 24.5, 18.6, 17.8, 18.6, 8, 0.92);
		}

		// =========================================================================================
		// BODY: a broad eight-sided breastplate with a raised V round the neck, a ridge down the middle and a V-shaped
		// plackart edge under the chest; a brown leather belt with a round steel buckle; a fauld band below it, a pointed
		// plate hanging over the front, square tassets on the hips and two plates behind, over a dark cloth skirt
		// =========================================================================================
		const double TSX = 0.85;
		const FVector TC(1, 0, 0);
		const TArray<FVector2D> TorsoP = Prof({ 13.5, 21, 19.5, 18.5, 22.4, 14, 23.0, 7, 22.6, 0, 21.4, -7, 19.6, -13, 18.2, -18, 18.0, -21.5, 0, -21.5 });
		const FOct Torso(TorsoP, TSX, TC, 8);
		auto PlackTop = [](double Th) { return -2.0 - 8.0 * FMath::Min(FMath::Abs(Th), 70.0) / 70.0; };
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Plate);
			Lathe(D.Mesh, TorsoP, 8, TSX, 1.0, Xf(TC), false, true);
			Ribbon(D.Mesh, 90, [&](double T) { const double Th = -112.5 + 225.0 * T, Zt = PlackTop(Th);    // the plackart
				return Quad(Torso.At(Th, Zt, -0.2), Torso.At(Th, Zt, 0.9), Torso.At(Th, -19.0, 0.9), Torso.At(Th, -19.0, -0.2)); });
			for (int32 sd = -1; sd <= 1; sd += 2)   // the raised V round the neck, meeting at the breastbone
				Ribbon(D.Mesh, 14, [&](double T) { const double Th = sd * 58.0 * T, Z = 8.0 + 11.0 * T;
					return Quad(Torso.At(Th, Z + 0.9, -0.2), Torso.At(Th, Z + 0.9, 0.9), Torso.At(Th, Z - 0.9, 0.9), Torso.At(Th, Z - 0.9, -0.2)); });
			Ribbon(D.Mesh, 12, [&](double T) { const double Z = 7.5 - 13.0 * T, A = FMath::RadiansToDegrees(0.75 / Torso.R(Z));   // the ridge down the middle
				return Quad(Torso.At(-A, Z, -0.2), Torso.At(-A, Z, 0.55), Torso.At(A, Z, 0.55), Torso.At(A, Z, -0.2)); });
			Hoop(D.Mesh, TC, 19.4, 20.6, -21.6, 21.6, 20.4, -25.8, 8, TSX);   // fauld band under the belt
		}
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Leather);   // the belt (always worn)
			Hoop(D.Mesh, TC, 19.0, 20.5, -16.6, 20.5, 19.0, -22.0, 16, TSX);
		}
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Plate);   // the round buckle
			Lathe(D.Mesh, Prof({ 0, 0, 3.5, 0, 3.5, 1.0, 2.5, 1.6, 0, 2.3 }), 12, 1.0, 1.0, Xf(V3(TC.X + 20.5 * TSX - 0.2, 0, -19.3), V3(0, 90, 0)), false, true);
		}
		{
			FDef& D = Cloth(P_Body, SlotBody);
			RBox(D.Mesh, 13.0, 16.0, 8.0, 0.6, Xf(V3(0, 0, -35)));                                              // pelvis
			Lathe(D.Mesh, Prof({ 19.6, -22, 21.0, -30, 21.6, -40, 21.0, -40, 20.2, -30, 18.6, -22 }), 16, TSX, 1.0, Xf(V3(0.5, 0, 0)), true);   // cloth skirt
		}
		{
			FDef& D = Add(P_Body, SlotBody, EArmorRole::Plate);
			// the pointed plate over the front, with a raised panel on it (both flat, leaning out a little at the bottom)
			auto Tab = [](double S, double Out) { const double K = 0.135;
				return TArray<FVector>({ V3(19.4 + Out, -9.0 * S, -24.5 - 1.0 * (1.0 - S)), V3(19.4 + Out, 9.0 * S, -24.5 - 1.0 * (1.0 - S)),
					V3(19.4 + Out + K * 10.5, 9.0 * S, -35.0 + 0.6 * (1.0 - S)), V3(19.4 + Out + K * 18.0 - 0.3 * (1.0 - S), 0.0, -42.5 + 3.0 * (1.0 - S)), V3(19.4 + Out + K * 10.5, -9.0 * S, -35.0 + 0.6 * (1.0 - S)) }); };
			Prism(D.Mesh, Tab(1.0, 0.0), V3(-1.3, 0, 0));
			Prism(D.Mesh, Tab(0.8, 0.6), V3(-0.8, 0, 0));
			// square tassets on the hips, flaring out, and two plates behind
			auto HipPlate = [&](int32 sd, double Th0, double Th1, double Zt, double Zb)
			{
				return [=](double T) {
					const double Th = sd * (Th0 + (Th1 - Th0) * T), A = FMath::DegreesToRadians(Th);
					auto P = [&](double Z, double H) { const double R = 20.9 + 0.22 * (-25.0 - Z) + H; return V3(R * FMath::Cos(A) * 0.9 + 0.5, R * FMath::Sin(A), Z); };
					return FRing({ P(Zt, 0.0), P(Zt, 1.2), P(Zb, 1.2), P(Zb, 0.0) });
				};
			};
			for (int32 sd = -1; sd <= 1; sd += 2)
			{
				Ribbon(D.Mesh, 10, HipPlate(sd, 38, 100, -25.0, -38.5));
				Ribbon(D.Mesh, 10, HipPlate(sd, 118, 176, -25.0, -35.0));
			}
		}

		// =========================================================================================
		// SHOULDERS: a big faceted pauldron on each shoulder with a raised rim, and three broad lames over the top of the
		// arm that swing with it. Below them the dark sleeve shows.
		// =========================================================================================
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const int32 Arm = (sd < 0) ? P_ArmL : P_ArmR;
			const FVector J = PivotPos(Arm);
			const FVector Up(0, sd * FMath::Sin(FMath::DegreesToRadians(30.0)), FMath::Cos(FMath::DegreesToRadians(30.0)));
			const FVector C(0, sd * 29.0, 8.5);
			const FXf CapX = Xf(C, DownTo(Up));          // the cap is built pointing down (-Z) and turned to face up and out
			const FVector Dn = (PivotPos((sd < 0) ? P_ElbowL : P_ElbowR) - J).GetSafeNormal();
			const FXf LameX = Xf(J, DownTo(Dn));
			{
				FDef& D = Add(P_Upper, SlotShoulders, EArmorRole::Plate);
				Lathe(D.Mesh, Prof({ 0, -11.0, 6.0, -10.5, 11.0, -8.0, 14.0, -4.6, 15.2, -1.2, 15.4, 0.6, 0, 0.6 }), 8, 1.0, 1.0, CapX, false, true);
				Lathe(D.Mesh, Prof({ 14.7, 0.7, 16.0, 0.7, 16.0, -1.3, 14.7, -1.3 }), 8, 1.0, 1.0, CapX, true, true);   // raised rim
				Lathe(D.Mesh, Prof({ 9.8, -6.6, 11.1, -6.6, 11.1, -8.2, 9.8, -8.2 }), 8, 1.0, 1.0, CapX, true, true);    // a second step near the top
			}
			{
				FDef& D = Add(Arm, SlotShoulders, EArmorRole::Plate);
				Lathe(D.Mesh, Prof({ 12.4, -0.5, 13.4, -0.5, 13.7, -5.4, 12.7, -5.4 }), 8, 1.0, 1.0, LameX, true, true);
				Lathe(D.Mesh, Prof({ 11.6, -4.6, 12.6, -4.6, 12.9, -9.4, 11.9, -9.4 }), 8, 1.0, 1.0, LameX, true, true);
				Lathe(D.Mesh, Prof({ 10.8, -8.6, 11.8, -8.6, 12.1, -12.8, 11.1, -12.8 }), 8, 1.0, 1.0, LameX, true, true);
			}
		}

		// =========================================================================================
		// ARMS: dark cloth sleeves, an angular elbow cop with a fan on the outside, big flaring vambraces with a raised band
		// at the top, a wide cuff, and dark gloved fists with a steel plate over the knuckles and the back of the hand
		// =========================================================================================
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const int32 Arm = (sd < 0) ? P_ArmL : P_ArmR, Elb = (sd < 0) ? P_ElbowL : P_ElbowR;
			const FVector J = PivotPos(Arm), E = PivotPos(Elb), F = FistPos(sd);
			const FVector W = WristPos(sd);
			const FXf UpperX = Xf(J, DownTo(E - J)), ForeX = Xf(E, DownTo(W - E));
			const FVector Cd = V3(-0.55, sd * 0.8, -0.1).GetSafeNormal();   // the point of the elbow: back and out
			{
				FDef& D = Cloth(Arm, SlotBody);   // the sleeve
				Lathe(D.Mesh, Prof({ 0, 1.0, 7.0, 1.0, 7.8, -3.5, 8.0, -8.5, 7.5, -13.5, 6.9, -17.5, 6.4, -19.0, 0, -19.0 }), 12, 1.0, 1.0, UpperX);
			}
			{
				FDef& D = Add(Elb, SlotBody, EArmorRole::Plate);
				Lathe(D.Mesh, Prof({ 0, -6.5, 5.0, -6.0, 8.5, -3.8, 9.8, -1.0, 10.0, 0.6, 0, 0.6 }), 6, 1.0, 1.0, Xf(E, DownTo(Cd)), false, true);   // elbow cop
				TArray<FVector> Fan;   // the fan on the outside of the elbow
				const double Fy = E.Y + sd * 8.4;
				Fan.Add(V3(-1.0, Fy, E.Z + 6.0)); Fan.Add(V3(-10.0, Fy, E.Z + 0.5)); Fan.Add(V3(-4.5, Fy, E.Z - 8.5)); Fan.Add(V3(3.0, Fy, E.Z - 2.5));
				Prism(D.Mesh, Fan, V3(0, sd * 1.3, 0));
				Lathe(D.Mesh, Prof({ 0, 0.5, 8.0, 0.5, 8.8, -3.0, 9.3, -8.0, 9.7, -12.5, 0, -12.5 }), 8, 1.0, 1.0, ForeX, false, true);   // vambrace
				Lathe(D.Mesh, Prof({ 8.2, 0.6, 9.3, 0.6, 9.5, -1.8, 8.5, -1.8 }), 8, 1.0, 1.0, ForeX, true, true);                         // its raised top band
			}
			{
				FDef& D = Add(Elb, SlotGloves, EArmorRole::Plate);
				Lathe(D.Mesh, Prof({ 9.2, -11.6, 10.4, -11.6, 11.0, -15.6, 9.6, -15.6 }), 8, 1.0, 1.0, ForeX, true, true);   // the cuff
				RBox(D.Mesh, 1.3, 5.2, 1.9, 0.3, Xf(F + V3(5.0, 0, 2.6)));          // knuckle plate
				RBox(D.Mesh, 4.4, 1.2, 4.2, 0.35, Xf(F + V3(0.5, sd * 4.7, 0.8)));   // plate over the back of the hand
			}
			{
				FDef& D = Add(Elb, SlotGloves, EArmorRole::Under);   // the dark gloved fist and thumb
				D.bFixedColor = true; D.Fixed = FLinearColor(0.05f, 0.05f, 0.055f);
				RBox(D.Mesh, 5.6, 4.7, 5.8, 0.4, Xf(F));
				RBox(D.Mesh, 2.5, 1.9, 3.3, 0.5, Xf(F + V3(3.5, -sd * 4.7, 1.0)));
				for (int32 j = -1; j <= 2; ++j) RBox(D.Mesh, 1.0, 1.15, 1.2, 0.6, Xf(F + V3(5.5, (j - 0.5) * 2.3, -1.8)));   // finger knuckles
			}
			{
				FDef& D = Add(Elb, SlotGloves, EArmorRole::Under); D.bBare = true; D.Fixed = FLinearColor(0.80f, 0.58f, 0.42f);   // bare hand
				Blob(D.Mesh, 5.0, 4.4, 5.4, Xf(F));
			}
		}

		// =========================================================================================
		// LEGS and BOOTS: dark cloth trousers, a lame above the knee, a big faceted knee cop with a fan, broad greaves with a
		// raised band at the top and a ridge down the shin, a wide ankle ring, and broad sabatons of overlapping plates
		// =========================================================================================
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const int32 Hip = (sd < 0) ? P_HipL : P_HipR, Knee = (sd < 0) ? P_KneeL : P_KneeR, Ankle = (sd < 0) ? P_AnkleL : P_AnkleR;
			const FVector LC(0.5, sd * 12.0, 0);
			const TArray<FVector2D> GreaveP = Prof({ 0, -64, 7.6, -64, 8.4, -68, 8.7, -73, 8.3, -79, 7.8, -84, 0, -84 });
			const FOct Greave(GreaveP, 1.1, LC, 8);
			const FXf KneeX = Xf(V3(5.6, sd * 12.0, -63.5), DownTo(V3(1, 0, 0)));
			{
				FDef& D = Cloth(Hip, SlotBody);
				// the thigh: broad at the top (its inner side shifted in so the thighs meet under the skirt), tapering to the knee
				struct FT { double Z, Cy, R; };
				static const FT Th[6] = { { -30.5, 10.6, 10.6 }, { -36.0, 10.8, 10.7 }, { -44.0, 11.2, 10.4 }, { -51.0, 11.6, 9.4 }, { -57.0, 12.0, 8.2 }, { -61.0, 12.0, 7.4 } };
				TArray<FRing> R;
				for (const FT& T : Th)
				{
					FRing Rg;
					for (int32 k = 0; k < 12; ++k)
					{
						const double A = 2.0 * PI * k / 12.0;
						Rg.Add(V3(0.5 + T.R * FMath::Cos(A) * 0.95, sd * T.Cy + T.R * FMath::Sin(A), T.Z));
					}
					R.Add(Rg);
				}
				Solid(D.Mesh, R, false, true, true, FXf());
			}
			{ FDef& D = Add(Hip, SlotBody, EArmorRole::Plate); Hoop(D.Mesh, LC, 8.1, 9.1, -55.8, 9.4, 8.4, -59.0, 8, 1.05); }   // lame above the knee
			{
				FDef& D = Add(Knee, SlotBody, EArmorRole::Plate);
				Lathe(D.Mesh, Prof({ 0, -5.6, 4.8, -5.0, 7.6, -2.8, 8.7, -0.4, 8.8, 0.8, 0, 0.8 }), 6, 1.0, 1.0, KneeX, false, true);   // knee cop
				Lathe(D.Mesh, Prof({ 8.3, 1.0, 9.3, 1.0, 9.3, -0.8, 8.3, -0.8 }), 6, 1.0, 1.0, KneeX, true, true);                       // its rim
				TArray<FVector> Fan;   // the fan on the outside of the knee
				const double Fy = sd * 20.6;
				Fan.Add(V3(3.0, Fy, -57.0)); Fan.Add(V3(-5.2, Fy, -61.0)); Fan.Add(V3(-2.2, Fy, -70.0)); Fan.Add(V3(4.6, Fy, -66.0));
				Prism(D.Mesh, Fan, V3(0, sd * 1.3, 0));
				Lathe(D.Mesh, GreaveP, 8, 1.1, 1.0, Xf(LC), false, true);                                                    // greave
				Hoop(D.Mesh, LC, 8.2, 9.3, -65.6, 9.5, 8.4, -68.4, 8, 1.1);                                                   // band at its top
				Ribbon(D.Mesh, 10, [&](double T) { const double Z = -69.0 - 13.5 * T, A = FMath::RadiansToDegrees(0.7 / Greave.R(Z));   // shin ridge
					return Quad(Greave.At(-A, Z, -0.2), Greave.At(-A, Z, 0.55), Greave.At(A, Z, 0.55), Greave.At(A, Z, -0.2)); });
			}
			// Ankle ring
			{
				FDef& D = Add(Knee, SlotBoots, EArmorRole::Plate);
				Hoop(D.Mesh, LC, 7.3, 8.3, -82.2, 9.2, 8.2, -86.4, 8, 1.1);
			}
			// The sabaton: a broad, faceted foot (always there) with overlapping plates over the top
			struct FSt { double X, W, Top, Bot; };
			static const FSt St[7] = { { -7.5, 5.4, -87.8, -96.0 }, { -3.5, 6.8, -86.2, -96.0 }, { 2.5, 7.4, -86.4, -96.0 }, { 8.5, 7.3, -88.8, -96.0 },
			                           { 13.5, 6.4, -91.4, -96.0 }, { 17.5, 4.6, -93.4, -96.0 }, { 20.0, 2.0, -94.8, -95.8 } };
			auto StAt = [&](double X) -> FSt
			{
				if (X <= St[0].X) return St[0];
				for (int32 i = 0; i < 6; ++i)
					if (X <= St[i + 1].X)
					{
						const double T = (X - St[i].X) / (St[i + 1].X - St[i].X);
						return { X, St[i].W + (St[i + 1].W - St[i].W) * T, St[i].Top + (St[i + 1].Top - St[i].Top) * T, St[i].Bot + (St[i + 1].Bot - St[i].Bot) * T };
					}
				return St[6];
			};
			const double FY = sd * 12.5;
			// the arch over the foot at X (wider by K, raised by Dz); bFull adds the sole
			auto Arch = [&](double X, double K, double Dz, bool bFull) -> FRing
			{
				const FSt S = StAt(X);
				const double Mid = S.Bot + 0.42 * (S.Top - S.Bot), Wd = S.W * K;
				FRing R;
				if (bFull) { R.Add(V3(X, FY - Wd, S.Bot)); R.Add(V3(X, FY + Wd, S.Bot)); }
				R.Add(V3(X, FY + 1.08 * Wd, Mid)); R.Add(V3(X, FY + 0.78 * Wd, S.Top + Dz)); R.Add(V3(X, FY, S.Top + 0.7 + Dz));
				R.Add(V3(X, FY - 0.78 * Wd, S.Top + Dz)); R.Add(V3(X, FY - 1.08 * Wd, Mid));
				return R;
			};
			// a plate lying over the foot from X0 to X1: outer arch, then the inner arch back again
			auto Shell = [&](double X, double Ko, double Dzo, double Ki, double Dzi) -> FRing
			{
				FRing R = Arch(X, Ko, Dzo, false);
				const FRing In = Arch(X, Ki, Dzi, false);
				for (int32 i = In.Num() - 1; i >= 0; --i) R.Add(In[i]);
				return R;
			};
			{
				FDef& D = Add(Ankle, SlotBoots, EArmorRole::BootBody);
				Ribbon(D.Mesh, 18, [&](double T) { return Arch(-7.5 + 27.5 * T, 1.0, 0.0, true); });
			}
			struct FLame { double X0, X1, K, Dz; };
			static const FLame Lames[3] = { { 0.5, 7.5, 1.10, 0.8 }, { 6.8, 13.2, 1.085, 0.6 }, { 12.5, 19.6, 1.07, 0.4 } };
			{
				FDef& D = Add(Ankle, SlotBoots, EArmorRole::Plate);
				for (const FLame& L : Lames)
					Ribbon(D.Mesh, 4, [&](double T) { return Shell(L.X0 + (L.X1 - L.X0) * T, L.K, L.Dz, 0.97, -0.5); });
			}
		}

		// RINGS and the AMULET (worn in the miscellaneous slots): a gold band with a red stone over the knuckles of each
		// fist (first ring right hand, second left), and a gold pendant hung on a chain over the chest
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const int32 Elb = (sd < 0) ? P_ElbowL : P_ElbowR, Slot = (sd < 0) ? SlotRingL : SlotRingR;
			const FVector F = FistPos(sd);
			{ FDef& D = Add(Elb, Slot, EArmorRole::Trim); D.bFixedColor = true; D.Fixed = FLinearColor(1.0f, 0.72f, 0.22f);
			  Torus(D.Mesh, 2.1, 0.7, 12, 6, 1.0, 1.0, Xf(F + V3(5.6, -sd * 1.6, -1.2), V3(90, 0, 0))); }
			{ FDef& D = Add(Elb, Slot, EArmorRole::Accent); D.bFixedColor = true; D.Fixed = FLinearColor(0.8f, 0.05f, 0.08f);
			  Bead(D.Mesh, 1.1, F + V3(7.9, -sd * 1.6, -1.2)); }
		}
		// The amulet (2026-10-30: bigger, and sitting on the Meshy model's breastplate): a heavy gold chain that goes right round
		// the collar, under the back of the helmet, and down the chest to a large medallion with a blue stone. Measured on the
		// model: the chest stands out to about x 23 at the medallion, the collar about 21 round the back at neck height.
		{
			FDef& D = Add(P_Upper, SlotAmulet, EArmorRole::Trim); D.bFixedColor = true; D.Fixed = FLinearColor(1.0f, 0.72f, 0.22f);
			static const FVector Half[] = { V3(24.6, 1.3, 14.0), V3(23.4, 3.6, 17.2), V3(20.6, 7.2, 21.0), V3(16.8, 14.6, 24.2), V3(6.5, 20.4, 25.8),
			                                V3(-5.0, 21.2, 26.4), V3(-14.8, 17.0, 26.8), V3(-20.6, 9.0, 26.8), V3(-22.0, 0.0, 26.8) };
			TArray<FVector> Path; TArray<FVector2D> AB;
			for (const FVector& P : Half) Path.Add(P);                                                  // right side, round to the back
			for (int32 i = UE_ARRAY_COUNT(Half) - 2; i >= 0; --i) Path.Add(V3(Half[i].X, -Half[i].Y, Half[i].Z));   // and the left side back to the front
			for (int32 i = 0; i < Path.Num(); ++i) AB.Add(FVector2D(0.6, 0.6));
			Sweep(D.Mesh, Path, AB, V3(0, 0, 1), 6, 1.0, Xf(O0));
			Torus(D.Mesh, 1.6, 0.55, 12, 6, 1.0, 1.0, Xf(V3(24.9, 0, 13.6), V3(0, 90, 0)));   // the bail the chain runs through
			// the medallion: a thick gold disc facing forward with a raised rim
			Lathe(D.Mesh, Prof({ 0, -0.8, 5.4, -0.8, 6.0, 0.0, 5.6, 0.9, 4.6, 0.9, 4.4, 0.5, 0, 0.6 }), 24, 1.0, 1.0, Xf(V3(25.0, 0, 8.0), V3(0, 90, 0)), false);
		}
		{ FDef& D = Add(P_Upper, SlotAmulet, EArmorRole::Accent); D.bFixedColor = true; D.Fixed = FLinearColor(0.1f, 0.35f, 0.9f);
		  Ellipsoid(D.Mesh, 1.2, 2.6, 2.6, 1.0, 1.0, 16, 10, Xf(V3(26.0, 0, 8.0))); }   // the blue stone

		// =========================================================================================
		// SHIELDS (left arm; weapons are modelled in WeaponDesign.h). The cape is cloth, simulated by the character
		// (see AKnightCharacter::TickCape).
		// Three styles share one slot; each piece carries its Style so only the worn one shows:
		// 0 the kite (domed face, steel rim, a red cross that follows the dome), 1 the round shield (a deep dome, a boss in the
		// middle and studs round the rim), 2 the tower shield (tall and nearly flat, with a band down the middle and two straps).
		// =========================================================================================
		{
			const double YC = -41.0, BackX = 14.0, FrontX = 17.3;   // in front of the left fist
			// One shield face: Outline (sideways offset, height; the last point repeats the first), its middle C, and a dome that
			// rises Dome cm at the middle and falls to nothing over Ry sideways and Rz up and down
			auto Face = [&](int32 Style, const TArray<FVector2D>& O, const FVector2D& C, double Ry, double Rz, double Dome)
			{
				auto F = [C, Ry, Rz](double Yl, double Z)   // by value: it is handed back and used after Face returns
				{
					const double Q = (Yl / Ry) * (Yl / Ry) + ((Z - C.Y) / Rz) * ((Z - C.Y) / Rz);
					return FMath::Pow(DMax(0.0, 1.0 - Q), 0.8);
				};
				const int32 NO = O.Num();
				TArray<FRing> Front, Back;
				const int32 NT = 8;
				for (int32 k = 0; k <= NT; ++k)
				{
					const double T = double(k) / double(NT);
					FRing Rf, Rb;
					for (int32 i = 0; i <= NO; ++i)
					{
						const FVector2D& P = O[i % NO];
						const double Yl = C.X + T * (P.X - C.X), Z = C.Y + T * (P.Y - C.Y);
						Rf.Add(V3(FrontX + Dome * F(Yl, Z), YC + Yl, Z));
						Rb.Add(V3(BackX, YC + Yl, Z));
					}
					Front.Add(Rf); Back.Add(Rb);
				}
				{ FDef& D = Add(P_ArmL, SlotShield, EArmorRole::Plate); D.Style = Style;
				  Patch(D.Mesh, Front, V3(1, 0, 0), Xf(V3(0, 0, 0)));
				  Patch(D.Mesh, Back, V3(-1, 0, 0), Xf(V3(0, 0, 0))); }
				{ FDef& D = Add(P_ArmL, SlotShield, EArmorRole::Trim); D.Style = Style;
				  TArray<FVector> Loop;
				  for (int32 i = 0; i + 1 < NO; ++i) { Loop.Add(V3(0.5 * (BackX + FrontX), YC + O[i].X, O[i].Y)); }
				  TubeLoop(D.Mesh, Loop, V3(1, 0, 0), 2.2, 3.1, 10, Xf(V3(0, 0, 0))); }
				return F;
			};
			// a band of accent colour lying on the face, from (Y0, Z0) to (Y1, Z1), Wd wide
			auto Band = [&](FDef& D, const TFunction<double(double, double)>& F, double Dome, double Y0, double Z0, double Y1, double Z1, double Wd)
			{
				TArray<FRing> Rs;
				const int32 N = 14;
				const FVector2D Dir = FVector2D(Y1 - Y0, Z1 - Z0).GetSafeNormal(), Side(-Dir.Y, Dir.X);
				for (int32 k = 0; k <= N; ++k)
				{
					const double T = double(k) / N, Yl = Y0 + (Y1 - Y0) * T, Z = Z0 + (Z1 - Z0) * T;
					FRing R;
					for (int32 j = -1; j <= 1; ++j) { const double Ys = Yl + Side.X * Wd * j, Zs = Z + Side.Y * Wd * j; R.Add(V3(FrontX + Dome * F(Ys, Zs) + 0.55, YC + Ys, Zs)); }
					Rs.Add(R);
				}
				Patch(D.Mesh, Rs, V3(1, 0, 0), Xf(V3(0, 0, 0)));
			};

			// ---- 0: the kite ----
			{
				TArray<FVector2D> O;
				for (int32 i = 0; i <= 12; ++i) { const double Yl = -17.0 + 34.0 * double(i) / 12.0; O.Add(FVector2D(Yl, 17.5 + 2.2 * (1.0 - (Yl / 17.0) * (Yl / 17.0)))); }
				TArray<FVector2D> RightSide;
				RightSide.Add(FVector2D(17.2, 12.0)); RightSide.Add(FVector2D(17.4, 4.0)); RightSide.Add(FVector2D(17.3, -4.0)); RightSide.Add(FVector2D(17.2, -9.0));
				for (int32 s = 1; s <= 10; ++s) { const double T = double(s) / 10.0; RightSide.Add(FVector2D(17.2 * (1.0 - FMath::Pow(T, 1.8)), -9.0 - 41.0 * T)); }
				RightSide.Last() = FVector2D(0.0, -50.0);
				for (int32 i = 0; i < RightSide.Num(); ++i) O.Add(RightSide[i]);
				for (int32 i = RightSide.Num() - 2; i >= 0; --i) O.Add(FVector2D(-RightSide[i].X, RightSide[i].Y));
				const double Dome = 4.2;
				auto F = Face(0, O, FVector2D(0.0, -12.0), 21.0, 43.0, Dome);
				FDef& D = Add(P_ArmL, SlotShield, EArmorRole::Accent); D.Style = 0;
				Band(D, F, Dome, 0.0, 15.5, 0.0, -44.0, 4.4);     // the cross: down ...
				Band(D, F, Dome, -13.5, 3.0, 13.5, 3.0, 4.4);     // ... and across
			}
			// ---- 1: the round shield ----
			{
				TArray<FVector2D> O;
				const double R = 19.0, Cz = -6.0;
				for (int32 i = 0; i <= 28; ++i) { const double A = 2.0 * PI * i / 28.0; O.Add(FVector2D(R * FMath::Sin(A), Cz + R * FMath::Cos(A))); }
				const double Dome = 6.0;
				auto F = Face(1, O, FVector2D(0.0, Cz), 21.5, 21.5, Dome);
				FDef& D = Add(P_ArmL, SlotShield, EArmorRole::Accent); D.Style = 1;
				Blob(D.Mesh, 2.8, 5.2, 5.2, Xf(V3(FrontX + Dome + 0.8, YC, Cz)));   // the boss in the middle
				for (int32 k = 0; k < 8; ++k)                                         // studs round the rim
				{
					const double A = 2.0 * PI * k / 8.0, Yl = 15.0 * FMath::Sin(A), Z = Cz + 15.0 * FMath::Cos(A);
					Blob(D.Mesh, 1.2, 1.5, 1.5, Xf(V3(FrontX + Dome * F(Yl, Z) + 0.6, YC + Yl, Z)));
				}
			}
			// ---- 2: the tower shield ----
			{
				TArray<FVector2D> O;
				for (int32 i = 0; i <= 12; ++i) { const double Yl = -17.5 + 35.0 * double(i) / 12.0; O.Add(FVector2D(Yl, 22.0 + 2.5 * (1.0 - (Yl / 17.5) * (Yl / 17.5)))); }
				const FVector2D RightSide[] = { FVector2D(17.8, 14.0), FVector2D(18.0, -5.0), FVector2D(18.0, -25.0), FVector2D(17.8, -44.0), FVector2D(16.5, -51.0), FVector2D(12.0, -54.5), FVector2D(0.0, -56.0) };
				for (const FVector2D& P : RightSide) O.Add(P);
				for (int32 i = UE_ARRAY_COUNT(RightSide) - 2; i >= 0; --i) O.Add(FVector2D(-RightSide[i].X, RightSide[i].Y));
				{ const FVector2D First = O[0]; O.Add(First); }
				const double Dome = 3.0;
				auto F = Face(2, O, FVector2D(0.0, -16.0), 24.0, 52.0, Dome);
				FDef& D = Add(P_ArmL, SlotShield, EArmorRole::Accent); D.Style = 2;
				Band(D, F, Dome, 0.0, 21.0, 0.0, -51.0, 3.6);      // a band down the middle
				Band(D, F, Dome, -15.5, 12.0, 15.5, 12.0, 1.8);    // and two straps across
				Band(D, F, Dome, -15.5, -38.0, 15.5, -38.0, 1.8);
			}
		}
	}
}
