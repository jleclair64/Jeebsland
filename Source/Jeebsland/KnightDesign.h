#pragma once

#include "CoreMinimal.h"
#include "KnightMesh.h"
#include <initializer_list>

// The knight's look: every armour plate, rivet and trim, modelled in code.
// All coordinates are "capsule space" in cm: feet at Z = -96, forward = +X, right = +Y.
// A part hangs off one pivot of the skeleton so it moves with that body part.
//
// The style (owner's sketches, 2026-10-12): a broad, stocky knight in crisp, faceted plate. Bronze plate with gold edges,
// a round great helm with a gold crest, brow and nose guard (closed cheeks, small vents), compact shoulder caps of
// overlapping lames, fan-winged elbow and knee cops, flared pointed gauntlet cuffs, hip lames and tassets over a mail
// skirt, and long layered sabatons. Most plates are eight-sided and flat-shaded so every face catches the light on its
// own. Each limb piece hangs off its own joint (shoulder, elbow, hip, knee) and the joints are covered by cops and lames,
// so the arms and legs can bend without opening gaps.

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
		case P_ArmL:   return FVector(0, -28, 6);      // shoulders: broad
		case P_ArmR:   return FVector(0, 28, 6);
		case P_ElbowL: return FVector(0, -32, -13);    // the arms hang a little out from the body
		case P_ElbowR: return FVector(0, 32, -13);
		case P_HipL:   return FVector(0, -11.5, -40);
		case P_HipR:   return FVector(0, 11.5, -40);
		case P_KneeL:  return FVector(0, -12, -63);
		case P_KneeR:  return FVector(0, 12, -63);
		case P_AnkleL: return FVector(1, -12.5, -86.5);   // the feet turn here, so they can stay flat while the knees bend
		case P_AnkleR: return FVector(1, 12.5, -86.5);
		}
		return FVector(0, 0, 0);
	}
	// The middle of each fist, and where a held weapon's grip sits in it
	inline FVector FistPos(int32 Side) { return FVector(7, Side * 35.0, -30); }
	inline FVector GripPos(bool bLeft) { return FVector(8, bLeft ? -35.0 : 35.0, -26); }

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
		// HELMET: a round great helm (sixteen faces, tapering in to the neck) with a gold crest over the top, a gold brow
		// dipping to a point over the nose, a raised nose guard, gold frames under the eyes, gold cheek sweeps and rim.
		// The cheeks meet below the nose guard: only the eyes and a few small vents are open.
		// =========================================================================================
		const double HSX = 1.12;
		const TArray<FVector2D> HelmP = Prof({ 0, 69, 5.5, 68.6, 9.5, 67.2, 12.8, 64.8, 15.0, 61.5, 16.3, 57.5, 16.6, 52, 16.5, 44, 16.0, 37, 15.0, 31.5, 14.0, 28.5, 13.6, 27, 0, 27 });
		const FOct Helm(HelmP, HSX, O0, 32);
		auto HS = [&](double Th, double Z, double H) { return Helm.At(Th, Z, H); };
		// the brow's lower edge (it dips to a point over the nose) and the bottom of the eye openings
		auto BrowBot = [](double Th) { const double A = FMath::Abs(Th); return A >= 30.0 ? 53.0 : (A >= 8.0 ? 50.5 + 2.5 * (A - 8.0) / 22.0 : 48.8 + 1.7 * A / 8.0); };
		auto EyeBot = [](double Th) { const double A = FMath::Abs(Th); return A <= 8.0 ? 45.5 : 45.5 + 2.0 * FMath::Min(1.0, (A - 8.0) / 42.0); };
		// The line of the crest: from the brow, over the top, down the back (evenly spaced points), with the outward direction
		// and the crest's height at each point (tallest on top)
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
			H = 0.8 + 1.6 * DClamp((P.Z - 40.0) / 29.0, 0.0, 1.0);
		};
		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Plate);
			Lathe(D.Mesh, HelmP, 32, HSX, 1.0, Xf(O0));   // a smooth, polished dome
		}
		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Trim);
			// brow band, round the front and both sides
			Ribbon(D.Mesh, 84, [&](double T) { const double Th = -105.0 + 210.0 * T, Zb = BrowBot(Th);
				return Quad(HS(Th, 57.5, -0.3), HS(Th, 57.5, 0.9), HS(Th, Zb, 0.9), HS(Th, Zb, -0.3)); });
			// nose guard, with a ridge down its middle, ending in a point
			{
				const TArray<FVector2D> NW = Prof({ 36.5, 0.25, 38.0, 0.8, 42.0, 1.8, 48.0, 2.6, 57.5, 2.0 });   // (height, half width)
				const TArray<FVector2D> NH = Prof({ 36.5, 0.9, 46.0, 1.5, 57.5, 1.1 });                           // (height, how proud)
				Ribbon(D.Mesh, 18, [&](double T) {
					const double Z = 57.5 - 21.0 * T, W = FMath::RadiansToDegrees(Lerp1(NW, Z) / Helm.R(Z)), H = Lerp1(NH, Z);
					return FRing({ HS(-W, Z, -0.3), HS(-W, Z, H), HS(0, Z, H + 0.7), HS(W, Z, H), HS(W, Z, -0.3) }); });
			}
			// frame under the eyes, meeting under the nose guard
			Ribbon(D.Mesh, 40, [&](double T) { const double Th = -50.0 + 100.0 * T, Zt = EyeBot(Th);
				return Quad(HS(Th, Zt, -0.3), HS(Th, Zt, 0.8), HS(Th, Zt - 1.7, 0.8), HS(Th, Zt - 1.7, -0.3)); });
			// cheek sweeps: from the corners of the brow down and forward to the jaw
			for (int32 sd = -1; sd <= 1; sd += 2)
			{
				static const double PT[5] = { 80, 76, 68, 57, 49 }, PZ[5] = { 52.5, 46, 38.5, 31.8, 27.6 };
				Ribbon(D.Mesh, 16, [&](double T) {
					const double F = T * 4.0; const int32 i = FMath::Min(int32(F), 3); const double U = F - i;
					const double Th = sd * (PT[i] + (PT[i + 1] - PT[i]) * U), Z = PZ[i] + (PZ[i + 1] - PZ[i]) * U;
					const double Dw = FMath::RadiansToDegrees(1.1 / Helm.R(Z));
					return Quad(HS(Th - Dw, Z, -0.3), HS(Th - Dw, Z, 0.8), HS(Th + Dw, Z, 0.8), HS(Th + Dw, Z, -0.3)); });
			}
			// a seam down the chin, under the nose guard
			Ribbon(D.Mesh, 6, [&](double T) { const double Z = 36.8 - 7.6 * T, Dw = FMath::RadiansToDegrees(0.4 / Helm.R(Z));
				return Quad(HS(-Dw, Z, -0.3), HS(-Dw, Z, 0.55), HS(Dw, Z, 0.55), HS(Dw, Z, -0.3)); });
			// the rim round the bottom
			Lathe(D.Mesh, Prof({ Helm.R(27.0) - 0.4, 27.0, Helm.R(27.0) + 0.9, 27.0, Helm.R(29.2) + 0.9, 29.2, Helm.R(29.2) - 0.4, 29.2 }), 32, HSX, 1.0, Xf(O0), true);
			// the crest: a raised ridge from the brow, over the top, down the back, tallest on top
			Ribbon(D.Mesh, 44, [&](double T) {
				FVector P, N; double H;
				CrestAt(T, P, N, H);
				const double W = 1.2 + 0.6 * FMath::Sin(PI * T);
				const FVector Y(0, 1, 0);
				return FRing({ P - Y * W - N * 0.3, P - Y * W + N * H, P + N * (H + 0.6), P + Y * W + N * H, P + Y * W - N * 0.3 }); });
			// rivets on the sides
			for (int32 sd = -1; sd <= 1; sd += 2) { Bead(D.Mesh, 0.8, HS(sd * 100.0, 46.0, 0.3)); Bead(D.Mesh, 0.8, HS(sd * 100.0, 39.0, 0.3)); }
		}
		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Under);   // the dark openings: eyes and breathing vents (always black)
			D.bFixedColor = true; D.Fixed = FLinearColor(0.006f, 0.006f, 0.008f);
			for (int32 sd = -1; sd <= 1; sd += 2)
			{
				Ribbon(D.Mesh, 16, [&](double T) { const double Th = sd * (7.0 + 43.0 * T), Zt = BrowBot(Th) + 0.4, Zb = EyeBot(Th) - 0.3;
					return Quad(HS(Th, Zt, -0.3), HS(Th, Zt, 0.3), HS(Th, Zb, 0.3), HS(Th, Zb, -0.3)); });
				for (int32 k = 0; k < 3; ++k)   // three upright breathing slits on each cheek
					Ribbon(D.Mesh, 6, [&](double T) { const double Th0 = sd * (14.0 + 5.5 * k), Z = 40.5 - 6.0 * T + 0.6 * k, Dw = FMath::RadiansToDegrees(0.4 / Helm.R(Z));
						return Quad(HS(Th0 - Dw, Z, -0.3), HS(Th0 - Dw, Z, 0.25), HS(Th0 + Dw, Z, 0.25), HS(Th0 + Dw, Z, -0.3)); });
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

		// =========================================================================================
		// GORGET: two lames round the neck, gold edged (the helm tapers in above it, so the knight has a neck)
		// =========================================================================================
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Plate);
			Hoop(D.Mesh, O0, 11.0, 11.8, 29.5, 13.0, 12.2, 24.0, 8, 0.92);
			Hoop(D.Mesh, O0, 12.6, 13.4, 25.0, 17.6, 16.8, 19.4, 8, 0.92);
		}
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Trim);
			Edge(D.Mesh, O0, 13.0, 24.0, 8, 0.92);
			Edge(D.Mesh, O0, 17.6, 19.4, 8, 0.92);
		}

		// =========================================================================================
		// BODY: a broad eight-sided breastplate (a crisp ridge down the middle), a raised plackart over the belly with a
		// gold V edge, two hip lames, tassets over the hips and a short mail skirt
		// =========================================================================================
		const double TSX = 0.85;
		const FVector TC(1, 0, 0);
		const TArray<FVector2D> TorsoP = Prof({ 13.5, 21, 19.5, 18.5, 22.4, 14, 23.0, 7, 22.6, 0, 21.4, -7, 19.6, -13, 18.2, -18, 18.0, -21.5, 0, -21.5 });
		const FOct Torso(TorsoP, TSX, TC, 8);
		auto PlackTop = [](double Th) { return -4.0 - 7.0 * FMath::Min(FMath::Abs(Th), 60.0) / 60.0; };
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Plate);
			Lathe(D.Mesh, TorsoP, 8, TSX, 1.0, Xf(TC), false, true);
			Ribbon(D.Mesh, 90, [&](double T) { const double Th = -112.5 + 225.0 * T, Zt = PlackTop(Th);    // the plackart
				return Quad(Torso.At(Th, Zt, -0.2), Torso.At(Th, Zt, 0.9), Torso.At(Th, -21.3, 0.9), Torso.At(Th, -21.3, -0.2)); });
			Hoop(D.Mesh, TC, 18.4, 19.2, -20.0, 20.8, 20.0, -26.0, 8, TSX);                                    // hip lames
			Hoop(D.Mesh, TC, 19.6, 20.4, -25.2, 22.0, 21.2, -31.0, 8, TSX);
		}
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Trim);
			// the ridge down the chest
			Ribbon(D.Mesh, 12, [&](double T) { const double Z = 17.0 - 20.6 * T, A = FMath::RadiansToDegrees(0.75 / Torso.R(Z));
				return Quad(Torso.At(-A, Z, -0.2), Torso.At(-A, Z, 0.55), Torso.At(A, Z, 0.55), Torso.At(A, Z, -0.2)); });
			// the plackart's gold V edge
			Ribbon(D.Mesh, 90, [&](double T) { const double Th = -112.5 + 225.0 * T, Zt = PlackTop(Th);
				return Quad(Torso.At(Th, Zt + 0.2, 0.7), Torso.At(Th, Zt + 0.2, 1.45), Torso.At(Th, Zt - 1.1, 1.45), Torso.At(Th, Zt - 1.1, 0.7)); });
			for (int32 sd = -1; sd <= 1; sd += 2) Bead(D.Mesh, 0.85, Torso.At(sd * 40.0, -15.0, 1.1));
			Edge(D.Mesh, TC, 20.8, -26.0, 8, TSX);
			Edge(D.Mesh, TC, 22.0, -31.0, 8, TSX);
		}
		{
			FDef& D = Add(P_Body, SlotBody, EArmorRole::Under);
			RBox(D.Mesh, 13.0, 16.0, 8.0, 0.6, Xf(V3(0, 0, -35)));                                              // pelvis
			Lathe(D.Mesh, Prof({ 20.2, -27, 22.0, -35, 22.8, -43.5, 22.2, -43.5, 21.4, -35, 19.6, -27 }), 16, TSX, 1.0, Xf(V3(0.5, 0, 0)), true);   // mail skirt
		}
		// Tassets: a plate over each hip, flaring out, hung from under the hip lames
		auto Tasset = [&](int32 sd, double Th0, double Th1, double Zt, double Zb0, double Zb1, double H0, double H1, double Inset)
		{
			return [=](double T) {
				const double Th = sd * (Th0 + (Th1 - Th0) * T), Zb = Zb0 + (Zb1 - Zb0) * T, A = FMath::DegreesToRadians(Th);
				auto P = [&](double Z, double H) { const double R = 20.3 + 2.7 * (-29.5 - Z) / 11.5 + H; return V3(R * FMath::Cos(A) * 0.9 + 0.5, R * FMath::Sin(A), Z); };
				return FRing({ P(Zt, H0), P(Zt, H1), P(Zb + Inset, H1), P(Zb + Inset, H0) });
			};
		};
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			{ FDef& D = Add(P_Body, SlotBody, EArmorRole::Plate); Ribbon(D.Mesh, 12, Tasset(sd, 58, 112, -29.5, -40.0, -42.0, 0.0, 1.0, 0.0)); }
			{ FDef& D = Add(P_Body, SlotBody, EArmorRole::Trim);
			  Ribbon(D.Mesh, 12, [&](double T) {   // gold along the bottom edge
				const double Th = sd * (58.0 + 54.0 * T), Zb = -40.0 - 2.0 * T, A = FMath::DegreesToRadians(Th);
				auto P = [&](double Z, double H) { const double R = 20.3 + 2.7 * (-29.5 - Z) / 11.5 + H; return V3(R * FMath::Cos(A) * 0.9 + 0.5, R * FMath::Sin(A), Z); };
				return FRing({ P(Zb + 1.2, 0.8), P(Zb + 1.2, 1.5), P(Zb - 0.2, 1.5), P(Zb - 0.2, 0.8) }); });
			  const double A = FMath::DegreesToRadians(sd * 85.0), R = 20.3 + 2.7 * 4.0 / 11.5 + 1.2;
			  Bead(D.Mesh, 0.8, V3(R * FMath::Cos(A) * 0.9 + 0.5, R * FMath::Sin(A), -33.5)); }
		}

		// =========================================================================================
		// SHOULDERS: a compact faceted cap on each shoulder (gold rim; a rivet on bronze and iron, a spike on steel and
		// better), and three narrow lames that hug the top of the arm and swing with it
		// =========================================================================================
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const int32 Arm = (sd < 0) ? P_ArmL : P_ArmR;
			const FVector J = PivotPos(Arm);
			const FVector Up(0, sd * FMath::Sin(FMath::DegreesToRadians(25.0)), FMath::Cos(FMath::DegreesToRadians(25.0)));
			const FVector C(0, sd * 28.0, 8.5);
			const FXf CapX = Xf(C, DownTo(Up));          // the cap is built pointing down (-Z) and turned to face up and out
			const FVector Dn = (PivotPos((sd < 0) ? P_ElbowL : P_ElbowR) - J).GetSafeNormal();
			const FXf LameX = Xf(J, DownTo(Dn));
			{
				FDef& D = Add(P_Upper, SlotShoulders, EArmorRole::Plate);
				Lathe(D.Mesh, Prof({ 0, -9.5, 5.0, -9.0, 9.0, -7.0, 11.5, -4.2, 12.6, -1.0, 12.8, 0.4, 0, 0.4 }), 8, 1.0, 1.0, CapX, false, true);
			}
			{
				FDef& D = Add(P_Upper, SlotShoulders, EArmorRole::Trim);
				Lathe(D.Mesh, Prof({ 12.3, 0.5, 13.4, 0.5, 13.4, -1.4, 12.3, -1.4 }), 8, 1.0, 1.0, CapX, true, true);
			}
			{ FDef& D = Add(P_Upper, SlotShoulders, EArmorRole::Trim, 1, 2); Bead(D.Mesh, 1.3, C + Up * 9.8); }
			{ FDef& D = Add(P_Upper, SlotShoulders, EArmorRole::Trim, 3, 9); Lathe(D.Mesh, Prof({ 0, -6.5, 1.7, 0, 0, 0 }), 6, 1.0, 1.0, Xf(C + Up * 9.0, DownTo(Up)), false, true); }
			{
				FDef& D = Add(Arm, SlotShoulders, EArmorRole::Plate);
				Lathe(D.Mesh, Prof({ 11.6, -0.5, 12.4, -0.5, 12.6, -5.0, 11.8, -5.0 }), 8, 1.0, 1.0, LameX, true, true);
				Lathe(D.Mesh, Prof({ 10.9, -4.3, 11.7, -4.3, 11.9, -8.8, 11.1, -8.8 }), 8, 1.0, 1.0, LameX, true, true);
				Lathe(D.Mesh, Prof({ 10.2, -8.1, 11.0, -8.1, 11.2, -12.4, 10.4, -12.4 }), 8, 1.0, 1.0, LameX, true, true);
			}
			{
				FDef& D = Add(Arm, SlotShoulders, EArmorRole::Trim);
				Lathe(D.Mesh, Prof({ 12.4, -4.0, 13.15, -4.0, 13.15, -5.2, 12.4, -5.2 }), 8, 1.0, 1.0, LameX, true, true);
				Lathe(D.Mesh, Prof({ 11.7, -7.8, 12.45, -7.8, 12.45, -9.0, 11.7, -9.0 }), 8, 1.0, 1.0, LameX, true, true);
				Lathe(D.Mesh, Prof({ 11.0, -11.4, 11.75, -11.4, 11.75, -12.6, 11.0, -12.6 }), 8, 1.0, 1.0, LameX, true, true);
			}
		}

		// =========================================================================================
		// ARMS: thick upper arms, an angular elbow cop with a fan on the outside, vambraces, and gauntlets with a flared cuff
		// rising to points, layered knuckle plates and a big fist
		// =========================================================================================
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const int32 Arm = (sd < 0) ? P_ArmL : P_ArmR, Elb = (sd < 0) ? P_ElbowL : P_ElbowR;
			const FVector J = PivotPos(Arm), E = PivotPos(Elb), F = FistPos(sd);
			const FVector W(6.0, sd * 34.6, -24.5);                 // the wrist
			const FXf UpperX = Xf(J, DownTo(E - J)), ForeX = Xf(E, DownTo(W - E));
			const FVector Cd = V3(-0.55, sd * 0.8, -0.1).GetSafeNormal();   // the point of the elbow: back and out
			{
				FDef& D = Add(Arm, SlotBody, EArmorRole::Plate);   // upper arm, thickest at the biceps
				Lathe(D.Mesh, Prof({ 0, 1.0, 7.4, 1.0, 8.4, -3.5, 8.6, -8.5, 8.0, -13.5, 7.2, -17.5, 6.6, -19.0, 0, -19.0 }), 8, 1.0, 1.0, UpperX, false, true);
			}
			{
				FDef& D = Add(Elb, SlotBody, EArmorRole::Plate);
				Lathe(D.Mesh, Prof({ 0, -5.5, 4.5, -5.0, 7.5, -3.2, 8.8, -0.8, 9.0, 0.6, 0, 0.6 }), 6, 1.0, 1.0, Xf(E, DownTo(Cd)), false, true);   // elbow cop
				TArray<FVector> Fan;   // the fan on the outside of the elbow
				const double Fy = E.Y + sd * 7.6;
				Fan.Add(V3(-1.0, Fy, E.Z + 5.5)); Fan.Add(V3(-9.5, Fy, E.Z + 0.5)); Fan.Add(V3(-4.0, Fy, E.Z - 8.0)); Fan.Add(V3(3.0, Fy, E.Z - 2.5));
				Prism(D.Mesh, Fan, V3(0, sd * 1.2, 0));
				Lathe(D.Mesh, Prof({ 0, 0.5, 6.9, 0.5, 7.2, -3.0, 6.7, -8.5, 6.0, -12.5, 0, -12.5 }), 8, 1.0, 1.0, ForeX, false, true);   // vambrace
			}
			{
				FDef& D = Add(Elb, SlotBody, EArmorRole::Trim);
				Bead(D.Mesh, 1.0, E + Cd * 5.7);
				Bead(D.Mesh, 0.9, V3(-2.6, E.Y + sd * 9.0, E.Z - 0.8));
			}
			// Gauntlet: the flared cuff, its top edge rising to four points
			{
				FDef& D = Add(Elb, SlotGloves, EArmorRole::Plate);
				const double Zb = -14.8;
				auto Top = [](int32 k) { return (k % 2 == 0) ? -5.0 : -8.5; };
				TArray<FRing> R;
				for (int32 Ring = 0; Ring < 4; ++Ring)
				{
					FRing Rg;
					for (int32 k = 0; k < 8; ++k)
					{
						const double A = 2.0 * PI * k / 8.0;
						const double Rad = (Ring == 0) ? 6.6 : (Ring == 1) ? 7.4 : (Ring == 2) ? 9.6 : 8.8;
						const double Z = (Ring <= 1) ? Top(k) : Zb;
						Rg.Add(V3(Rad * FMath::Cos(A), Rad * FMath::Sin(A), Z));
					}
					R.Add(Rg);
				}
				Solid(D.Mesh, R, true, false, false, ForeX, true);
				// the fist, with two knuckle plates across the front and a thumb
				RBox(D.Mesh, 5.4, 4.6, 5.6, 0.35, Xf(F));
				RBox(D.Mesh, 1.1, 4.9, 1.6, 0.3, Xf(F + V3(5.0, 0, 2.6)));
				RBox(D.Mesh, 1.1, 4.9, 1.6, 0.3, Xf(F + V3(5.3, 0, 0.0)));
				RBox(D.Mesh, 2.4, 1.8, 3.2, 0.4, Xf(F + V3(3.5, -sd * 4.6, 1.0)));
				D.Shade = 0.85f;
			}
			{
				FDef& D = Add(Elb, SlotGloves, EArmorRole::Trim);
				// gold along the cuff's pointed top edge and round its bottom
				TArray<FRing> R;
				for (int32 Ring = 0; Ring < 4; ++Ring)
				{
					FRing Rg;
					for (int32 k = 0; k < 8; ++k)
					{
						const double A = 2.0 * PI * k / 8.0, Zt = (k % 2 == 0) ? -5.0 : -8.5;
						const double Fr = 1.3 / (Zt + 14.8), Ro = 7.4 + 2.2 * Fr;   // the cuff's radius 1.3 cm below its edge
						const double Rad = (Ring == 0) ? 7.25 : (Ring == 1) ? 7.95 : (Ring == 2) ? Ro + 0.55 : Ro - 0.15;
						const double Z = (Ring <= 1) ? Zt + 0.1 : Zt - 1.3;
						Rg.Add(V3(Rad * FMath::Cos(A), Rad * FMath::Sin(A), Z));
					}
					R.Add(Rg);
				}
				Solid(D.Mesh, R, true, false, false, ForeX, true);
				Lathe(D.Mesh, Prof({ 8.9, -13.9, 10.0, -13.9, 10.0, -15.2, 8.9, -15.2 }), 8, 1.0, 1.0, ForeX, true, true);
				RBox(D.Mesh, 0.5, 5.0, 0.45, 0.4, Xf(F + V3(6.1, 0, 3.9)));   // gold on the top knuckle plate
			}
			{
				FDef& D = Add(Elb, SlotGloves, EArmorRole::Under);   // finger grooves
				for (int32 j = -1; j <= 1; ++j) RBox(D.Mesh, 0.35, 0.3, 2.2, 0.5, Xf(F + V3(5.35, j * 1.9, -3.0)));
			}
			{
				FDef& D = Add(Elb, SlotGloves, EArmorRole::Under); D.bBare = true; D.Fixed = FLinearColor(0.80f, 0.58f, 0.42f);   // bare hand
				Blob(D.Mesh, 5.0, 4.4, 5.4, Xf(F));
			}
		}

		// =========================================================================================
		// LEGS and BOOTS: thick cuisses, an angular knee cop with a fan and a lame above and below, greaves with a gold ridge,
		// two flaring ankle rings, and long sabatons of overlapping plates
		// =========================================================================================
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const int32 Hip = (sd < 0) ? P_HipL : P_HipR, Knee = (sd < 0) ? P_KneeL : P_KneeR, Ankle = (sd < 0) ? P_AnkleL : P_AnkleR;
			const FVector LC(0.5, sd * 12.0, 0);
			const TArray<FVector2D> GreaveP = Prof({ 0, -64, 7.0, -64, 7.7, -68, 7.9, -73, 7.1, -78.5, 5.6, -83.5, 0, -83.5 });
			const FOct Greave(GreaveP, 1.1, LC, 8);
			const FXf KneeX = Xf(V3(5.0, sd * 12.0, -63.5), DownTo(V3(1, 0, 0)));
			{
				FDef& D = Add(Hip, SlotBody, EArmorRole::Plate);
				// cuisse: broad at the top (its inner side shifted in so the thighs meet under the mail, no gap between them),
				// tapering to the knee
				{
					struct FT { double Z, Cy, R; };
					static const FT Th[6] = { { -30.5, 10.6, 11.2 }, { -36.0, 10.8, 11.3 }, { -44.0, 11.2, 11.0 }, { -51.0, 11.6, 10.0 }, { -57.0, 12.0, 8.6 }, { -61.0, 12.0, 7.8 } };
					TArray<FRing> R;
					for (const FT& T : Th)
					{
						FRing Rg;
						for (int32 k = 0; k < 8; ++k)
						{
							const double A = 2.0 * PI * k / 8.0;
							Rg.Add(V3(0.5 + T.R * FMath::Cos(A) * 0.95, sd * T.Cy + T.R * FMath::Sin(A), T.Z));
						}
						R.Add(Rg);
					}
					Solid(D.Mesh, R, false, true, true, FXf(), true);
				}
				Hoop(D.Mesh, LC, 8.1, 8.9, -56.5, 9.1, 8.3, -59.0, 8, 1.05);                                                                          // lame above the knee
			}
			{ FDef& D = Add(Hip, SlotBody, EArmorRole::Trim); Edge(D.Mesh, LC, 9.1, -59.0, 8, 1.05); }
			{
				FDef& D = Add(Knee, SlotBody, EArmorRole::Plate);
				Lathe(D.Mesh, Prof({ 0, -4.6, 4.0, -4.1, 6.4, -2.3, 7.3, -0.4, 7.4, 0.8, 0, 0.8 }), 6, 1.0, 1.0, KneeX, false, true);   // knee cop
				TArray<FVector> Fan;   // the fan on the outside of the knee
				const double Fy = sd * 20.2;
				Fan.Add(V3(2.5, Fy, -57.5)); Fan.Add(V3(-4.8, Fy, -61.0)); Fan.Add(V3(-2.0, Fy, -69.5)); Fan.Add(V3(4.2, Fy, -65.5));
				Prism(D.Mesh, Fan, V3(0, sd * 1.2, 0));
				Hoop(D.Mesh, LC, 7.5, 8.3, -65.5, 8.5, 7.7, -67.8, 8, 1.1);                                                 // lame below the knee
				Lathe(D.Mesh, GreaveP, 8, 1.1, 1.0, Xf(LC), false, true);                                                   // greave
			}
			{
				FDef& D = Add(Knee, SlotBody, EArmorRole::Trim);
				Lathe(D.Mesh, Prof({ 7.0, 1.0, 7.9, 1.0, 7.9, -0.6, 7.0, -0.6 }), 6, 1.0, 1.0, KneeX, true, true);         // knee cop rim
				Bead(D.Mesh, 1.0, V3(9.9, sd * 12.0, -63.5));
				Bead(D.Mesh, 0.9, V3(0.0, sd * 21.4, -63.2));
				Edge(D.Mesh, LC, 8.5, -67.8, 8, 1.1);
				Ribbon(D.Mesh, 10, [&](double T) { const double Z = -68.5 - 14.5 * T, A = FMath::RadiansToDegrees(0.7 / Greave.R(Z));   // shin ridge
					return Quad(Greave.At(-A, Z, -0.2), Greave.At(-A, Z, 0.55), Greave.At(A, Z, 0.55), Greave.At(A, Z, -0.2)); });
			}
			// Ankle rings
			{
				FDef& D = Add(Knee, SlotBoots, EArmorRole::Plate);
				Hoop(D.Mesh, LC, 5.9, 6.7, -82.3, 7.4, 6.6, -85.2, 8, 1.1);
				Hoop(D.Mesh, LC, 6.4, 7.2, -84.5, 7.9, 7.1, -87.4, 8, 1.1);
			}
			{
				FDef& D = Add(Knee, SlotBoots, EArmorRole::Trim);
				Edge(D.Mesh, LC, 7.4, -85.2, 8, 1.1);
				Edge(D.Mesh, LC, 7.9, -87.4, 8, 1.1);
			}
			// The sabaton: a long, faceted foot (always there) with overlapping plates over the top, each gold edged
			struct FSt { double X, W, Top, Bot; };
			static const FSt St[7] = { { -7.0, 4.4, -88.0, -96.0 }, { -3.5, 5.6, -86.4, -96.0 }, { 2.5, 6.2, -86.6, -96.0 }, { 8.5, 6.1, -89.2, -96.0 },
			                           { 13.5, 5.0, -91.8, -96.0 }, { 17.5, 3.0, -93.6, -96.0 }, { 20.0, 0.9, -94.8, -95.6 } };
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
				Ribbon(D.Mesh, 18, [&](double T) { return Arch(-7.0 + 27.0 * T, 1.0, 0.0, true); });
			}
			struct FLame { double X0, X1, K, Dz; };
			static const FLame Lames[4] = { { 1.0, 6.2, 1.10, 0.75 }, { 5.6, 10.8, 1.085, 0.6 }, { 10.2, 15.2, 1.07, 0.45 }, { 14.6, 19.8, 1.055, 0.3 } };
			{
				FDef& D = Add(Ankle, SlotBoots, EArmorRole::Plate);
				for (const FLame& L : Lames)
					Ribbon(D.Mesh, 4, [&](double T) { return Shell(L.X0 + (L.X1 - L.X0) * T, L.K, L.Dz, 0.97, -0.5); });
			}
			{
				FDef& D = Add(Ankle, SlotBoots, EArmorRole::Trim);
				for (int32 i = 0; i < 3; ++i)
				{
					const FLame& L = Lames[i];
					Ribbon(D.Mesh, 1, [&](double T) { return Shell(L.X1 - 0.8 + 0.85 * T, L.K + 0.03, L.Dz + 0.35, L.K - 0.02, L.Dz - 0.1); });
				}
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
		{
			FDef& D = Add(P_Upper, SlotAmulet, EArmorRole::Trim); D.bFixedColor = true; D.Fixed = FLinearColor(1.0f, 0.72f, 0.22f);
			for (int32 sd = -1; sd <= 1; sd += 2)
			{
				TArray<FVector> Path; TArray<FVector2D> AB;
				Path.Add(V3(13.0, sd * 10.0, 19.5)); Path.Add(V3(18.5, sd * 6.0, 15.5)); Path.Add(V3(21.2, sd * 1.5, 12.8));
				for (int32 i = 0; i < 3; ++i) AB.Add(FVector2D(0.4, 0.4));
				Sweep(D.Mesh, Path, AB, V3(0, 0, 1), 6, 1.0, Xf(O0));
			}
			Ellipsoid(D.Mesh, 0.8, 3.0, 3.6, 1.0, 1.0, 16, 10, Xf(V3(21.6, 0, 9.5)));
		}
		{ FDef& D = Add(P_Upper, SlotAmulet, EArmorRole::Accent); D.bFixedColor = true; D.Fixed = FLinearColor(0.1f, 0.35f, 0.9f);
		  Bead(D.Mesh, 1.4, V3(22.5, 0, 9.5)); }

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
