#pragma once

#include "CoreMinimal.h"
#include "KnightMesh.h"
#include <initializer_list>

// The knight's look: every armour plate, strap, blade and plume, modelled in code.
// All coordinates are "capsule space" in cm: feet at Z = -96, forward = +X, right = +Y.
// A part hangs off one pivot of the skeleton so it moves with that body part.

enum class EArmorRole : uint8 { Plate, Under, Trim, Cloth, Accent, Leather, BootBody };

namespace KnightDesign
{
	using namespace KM;

	enum EPivot : int32
	{
		P_Body, P_Upper, P_Head, P_Plume, P_CapeU, P_CapeL, P_ArmL, P_ArmR, P_ElbowL, P_ElbowR, P_HipL, P_HipR, P_KneeL, P_KneeR, P_Count
	};
	enum ESlot : int32 { SlotHelmet = 0, SlotBody = 1, SlotShoulders = 2, SlotBoots = 3, SlotCape = 4, SlotGloves = 5, SlotShield = 6, SlotNone = -1 };
	constexpr int32 NumSlots = 7;

	// The knight is built lean: everything below the helmet is squeezed by these factors at the end of Build()
	constexpr double SlimY = 0.80, SlimX = 0.90;

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
		case P_ArmL:   return FVector(0, -26 * SlimY, 7);
		case P_ArmR:   return FVector(0, 26 * SlimY, 7);
		case P_ElbowL: return FVector(0, -27 * SlimY, -11);
		case P_ElbowR: return FVector(0, 27 * SlimY, -11);
		case P_HipL:   return FVector(0, -13 * SlimY, -47);
		case P_HipR:   return FVector(0, 13 * SlimY, -47);
		case P_KneeL:  return FVector(0, -13 * SlimY, -68);
		case P_KneeR:  return FVector(0, 13 * SlimY, -68);
		}
		return FVector(0, 0, 0);
	}

	struct FDef
	{
		int32 Pivot = P_Body;
		int32 Slot = SlotNone;       // which armour set it belongs to (-1 = fixed gear)
		EArmorRole Role = EArmorRole::Plate;
		int32 MinTier = 0, MaxTier = 9;
		float Shade = 1.f;           // darkens the palette colour (cape lining)
		FLinearColor Fixed = FLinearColor(1.f, 1.f, 1.f, 1.f);
		bool bFixedMetal = false;
		bool bBare = false;          // skin and hair: shown only while nothing is worn in this slot (uses Fixed)
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
	// A rounded dome with a flat underside: radius R, top at height H, 13.5 deep
	inline TArray<FVector2D> DomeProf(double R, double H)
	{
		return Prof({ 0, H, 0.28 * R, H - 0.5, 0.55 * R, H - 1.8, 0.78 * R, H - 4.5, 0.94 * R, H - 8.0, R, H - 12.0, R, H - 13.5, 0, H - 13.5 });
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

	inline void Build(TArray<FDef>& Out)
	{
		auto Add = [&](int32 Pivot, int32 Slot, EArmorRole Role, int32 MinTier = 0, int32 MaxTier = 9) -> FDef&
		{
			FDef D; D.Pivot = Pivot; D.Slot = Slot; D.Role = Role; D.MinTier = MinTier; D.MaxTier = MaxTier;
			Out.Add(D);
			return Out.Last();
		};
		const FVector NoRot(0, 0, 0);

		// =========================================================================================
		// HELMET: a closed, angular war helm in the Westerosi style: beaked faceplate, T-shaped eye slit,
		// vent slits, steel cheek plates and a tall blade crest
		// =========================================================================================
		const double HC = 45.0; // helm centre height
		const double HSX = 1.12, HSY = 0.86;
		const TArray<FVector2D> HelmP = Prof({ 0, -22, 13.5, -22, 15.5, -17, 17.5, -9, 18.8, 0, 18.8, 9, 17.6, 15, 14.6, 20.5, 10, 24, 4.6, 26.2, 0, 26.8 });
		TArray<FVector2D> HZ;
		for (int32 i = 0; i < HelmP.Num(); ++i) HZ.Add(FVector2D(HelmP[i].Y, HelmP[i].X));
		auto HelmRadius = [&](double Zr) { return Lerp1(HZ, Zr); };
		auto HelmX = [&](double Zr, double Y)
		{
			const double R = HelmRadius(Zr);
			return HSX * FMath::Sqrt(DMax(0.0, R * R - (Y / HSY) * (Y / HSY)));
		};
		// The sideways slope of the helm's surface at height Zr, sideways offset Y (as a yaw in degrees)
		auto HelmYaw = [&](double Zr, double Y)
		{
			const double R = HelmRadius(Zr);
			const double Root = FMath::Sqrt(DMax(1.0, R * R - (Y / HSY) * (Y / HSY)));
			return FMath::RadiansToDegrees(FMath::Atan(HSX * (Y / (HSY * HSY)) / Root));
		};

		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Plate);
			TArray<FVector2D> Pr;
			for (int32 i = 0; i < HelmP.Num(); ++i) Pr.Add(FVector2D(HelmP[i].X, HC + HelmP[i].Y));
			Lathe(D.Mesh, Pr, 12, HSX, HSY, Xf(V3(0, 0, 0)));   // 12 flat-ish sides: a rigid, faceted skull
			// The beak: a sharp ridge from the brow to the chin, jutting out most at the nose
			TArray<FVector> Path; TArray<FVector2D> AB;
			const double Zs[] = { 11, 6, 1, -4, -9, -14, -19 };
			const double Ex[] = { 0.4, 1.4, 2.6, 3.0, 2.4, 1.4, 0.5 };
			const double Hw[] = { 2.4, 3.6, 4.4, 4.2, 3.6, 3.0, 2.4 };
			const double Th[] = { 2.0, 2.8, 3.4, 3.6, 3.2, 2.6, 2.0 };
			for (int32 i = 0; i < 7; ++i) { Path.Add(V3(HelmX(Zs[i], 0) + Ex[i], 0, HC + Zs[i])); AB.Add(FVector2D(Hw[i], Th[i])); }
			Sweep(D.Mesh, Path, AB, V3(0, 1, 0), 6, 0.3, Xf(V3(0, 0, 0)));
		}
		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Under); // dark openings: the long eye slit and the vents
			for (int32 sd = -1; sd <= 1; sd += 2)
			{
				const double Ye = sd * 8.4;
				RBox(D.Mesh, 1.4, 5.6, 1.7, 0.5, Xf(V3(HelmX(5.4, Ye) + 0.2, Ye, HC + 5.4), V3(0, 0, HelmYaw(5.4, Ye))));   // eye slit
				for (int32 k = 0; k < 4; ++k)
				{
					const double Zr = -2.0 - 4.2 * k, Y = sd * (10.0 + 0.4 * k);
					RBox(D.Mesh, 1.2, 0.8, 1.6, 0.3, Xf(V3(HelmX(Zr, Y) + 0.2, Y, HC + Zr), V3(0, 0, HelmYaw(Zr, Y))));   // vent slits
				}
			}
			RBox(D.Mesh, 1.4, 1.2, 6.0, 0.4, Xf(V3(HelmX(0.0, 0) + 4.5, 0, HC + 2.0)));   // the stem of the T, down the nose guard
		}
		{
			FDef& D = Add(P_Head, SlotHelmet, EArmorRole::Trim); // brow band, neck rim, angled cheek plates
			Torus(D.Mesh, 19.4, 1.3, 12, 6, HSX, HSY, Xf(V3(0, 0, HC + 9.6)));
			Torus(D.Mesh, 14.4, 1.4, 12, 6, HSX, HSY, Xf(V3(0, 0, HC - 21.0)));
			for (int32 sd = -1; sd <= 1; sd += 2)
			{
				const double Y = sd * 12.6, Zr = -10.0;
				RBox(D.Mesh, 1.0, 3.0, 6.4, 0.3, Xf(V3(HelmX(Zr, Y) + 0.6, Y, HC + Zr), V3(0, 0, HelmYaw(Zr, Y))));
			}
		}
		{
			FDef& D = Add(P_Plume, SlotHelmet, EArmorRole::Accent); // a tall, thin blade crest from brow to nape
			TArray<FVector> Path; TArray<FVector2D> AB;
			const double Zf[] = { 17, 21, 24.5, 26.6 };
			const double Hf[] = { 2.0, 3.4, 5.0, 7.0 };
			for (int32 i = 0; i < 4; ++i) { Path.Add(V3(HelmX(Zf[i], 0) + 0.4, 0, HC + Zf[i] + 1.5)); AB.Add(FVector2D(1.7, Hf[i])); }
			Path.Add(V3(0, 0, HC + 26.8 + 8.0)); AB.Add(FVector2D(1.3, 8.0));
			const double Zb[] = { 26.2, 24, 20, 15, 9, 3 };
			const double Hb[] = { 7.0, 6.0, 5.0, 4.0, 3.0, 2.0 };
			for (int32 i = 0; i < 6; ++i) { Path.Add(V3(-HelmX(Zb[i], 0) - 0.4 - (Zb[i] < 12 ? (12 - Zb[i]) * 0.8 : 0.0), 0, HC + Zb[i] + 1.5)); AB.Add(FVector2D(1.7, Hb[i])); }
			Path.Add(V3(-26, 0, HC - 6)); AB.Add(FVector2D(1.0, 1.4));
			Sweep(D.Mesh, Path, AB, V3(0, 1, 0), 6, 0.6, Xf(V3(0, 0, 0)));
		}

		// Bare head (shown while no helmet is worn): neck, head, hair, eyes
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

		// =========================================================================================
		// BODY: breastplate, back plate, plate lames, belt, tabard with a red cross, gorget, hip guards
		// =========================================================================================
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Plate);
			Lathe(D.Mesh, Prof({ 0, 9, 14, 9, 18.5, 10.5, 21.5, 12.8, 22, 15.5, 20.5, 18.5, 17, 20, 0, 20 }), 14, 1.0, 1.0, Xf(V3(0, 0, 0)));   // gorget
			Ellipsoid(D.Mesh, 21, 20.5, 17.5, 0.75, 0.8, 12, 8, Xf(V3(2, 0, -3)));      // breastplate
			Ellipsoid(D.Mesh, 16, 19.5, 17, 0.75, 0.8, 12, 8, Xf(V3(-8, 0, -4)));       // back plate
			{ TArray<FVector> Kp; TArray<FVector2D> Ka; const double Kz[] = { 14, 6, -2, -10, -17 }, Kx[] = { 16.0, 20.0, 21.4, 20.8, 19.0 };
			  for (int32 i = 0; i < 5; ++i) { Kp.Add(V3(Kx[i], 0, Kz[i])); Ka.Add(FVector2D(1.6, 1.7)); }
			  Sweep(D.Mesh, Kp, Ka, V3(0, 1, 0), 6, 0.3, Xf(V3(0, 0, 0))); }   // angular keel down the chest
			Ellipsoid(D.Mesh, 20.5, 19.5, 3.6, 0.55, 0.9, 12, 8, Xf(V3(1, 0, -15.5)));  // belly lames
			Ellipsoid(D.Mesh, 21, 20, 3.4, 0.55, 0.9, 12, 8, Xf(V3(1, 0, -19.5)));
		}
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Under);
			Ellipsoid(D.Mesh, 17, 17, 16, 0.8, 0.9, 12, 8, Xf(V3(0, 0, -5)));
			Lathe(D.Mesh, Prof({ 0, -34, 17.5, -34, 18.5, -30, 18.5, -20, 0, -20 }), 10, 1.0, 1.0, Xf(V3(0, 0, 0)));
		}
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Trim);
			Torus(D.Mesh, 18.3, 1.3, 10, 6, 1.0, 1.0, Xf(V3(0, 0, 19.2)));                // collar rim
			RBox(D.Mesh, 1.8, 5.2, 5.0, 0.35, Xf(V3(23.4, 0, -26.2)));                     // buckle
			RBox(D.Mesh, 1.6, 14.2, 1.2, 0.35, Xf(V3(25.4, 0, -45.6)));                    // tabard hem
		}
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Leather);
			Lathe(D.Mesh, Prof({ 0, -29, 21.2, -29, 22.4, -28, 22.4, -24.5, 21.2, -23.5, 0, -23.5 }), 14, 1.02, 1.0, Xf(V3(0, 0, 0)));   // belt
			RBox(D.Mesh, 0.9, 2.2, 6.0, 0.4, Xf(V3(23.0, -8, -33.5)));                     // belt tail
			RBox(D.Mesh, 4.2, 3.8, 5.6, 0.5, Xf(V3(3, -23.5, -31)));                       // pouch
		}
		// Tabard: hangs from the chest to below the belt
		const double TabZ[] = { 9, 3, -6, -14, -22, -30, -38, -45 };
		const double TabX[] = { 19.5, 23.8, 24.6, 23.6, 20.8, 20.8, 22.6, 25.0 };
		const double TabA[] = { 11.5, 12, 12.5, 12.5, 12.3, 12.8, 13.3, 13.8 };
		auto TabXAt = [&](double Z)
		{
			for (int32 i = 0; i + 1 < 8; ++i)
				if (Z <= TabZ[i] && Z >= TabZ[i + 1])
				{
					const double T = (TabZ[i] - Z) / (TabZ[i] - TabZ[i + 1]);
					return TabX[i] + (TabX[i + 1] - TabX[i]) * T;
				}
			return TabX[7];
		};
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Cloth);
			TArray<FVector> Path; TArray<FVector2D> AB;
			for (int32 i = 0; i < 8; ++i) { Path.Add(V3(TabX[i], 0, TabZ[i])); AB.Add(FVector2D(TabA[i], 1.3)); }
			Sweep(D.Mesh, Path, AB, V3(0, 1, 0), 12, 0.35, Xf(V3(0, 0, 0)));
			// (no back panel: the cape covers the back, and the pale panel showed under it as a white patch)
		}
		{
			FDef& D = Add(P_Upper, SlotBody, EArmorRole::Accent);     // the red cross
			TArray<FVector> Path; TArray<FVector2D> AB;
			for (double Z = 7.0; Z >= -21.0; Z -= 4.0) { Path.Add(V3(TabXAt(Z) + 1.35, 0, Z)); AB.Add(FVector2D(3.4, 0.5)); }
			Sweep(D.Mesh, Path, AB, V3(0, 1, 0), 8, 0.4, Xf(V3(0, 0, 0)));
			RBox(D.Mesh, 0.5, 10.5, 3.4, 0.3, Xf(V3(TabXAt(0.5) + 1.35, 0, 0.5)));
		}
		{
			FDef& D = Add(P_Body, SlotBody, EArmorRole::Under);
			Ellipsoid(D.Mesh, 14, 16.5, 11, 0.7, 0.8, 12, 8, Xf(V3(0, 0, -37)));        // pelvis
		}
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			FDef& D = Add(P_Body, SlotBody, EArmorRole::Plate);
			Ellipsoid(D.Mesh, 9.5, 3.6, 11, 0.5, 0.7, 12, 8, Xf(V3(1, sd * 20.5, -43), V3(sd * 8.0, 0, 0)));   // hip guards (tassets)
		}

		// =========================================================================================
		// SHOULDERS: three layered, domed pauldrons on each side with steel edge trim; rivets / spikes on higher tiers
		// =========================================================================================
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const FVector R1(-sd * 22.0, 0, 0), R2(-sd * 30.0, 0, 0), R3(-sd * 38.0, 0, 0);
			const FVector P1(0, sd * 27.5, 10.5), P2(0, sd * 31.0, 3.5), P3(0, sd * 33.5, -3.5);
			{
				FDef& D = Add(P_Upper, SlotShoulders, EArmorRole::Plate);
				Lathe(D.Mesh, DomeProf(17.0, 8.0), 14, 1.05, 1.0, Xf(P1, R1));
				Lathe(D.Mesh, DomeProf(18.0, 6.0), 14, 1.05, 1.0, Xf(P2, R2));
			}
			{
				FDef& D = Add(P_Upper, SlotShoulders, EArmorRole::Under);
				Lathe(D.Mesh, DomeProf(16.5, 5.0), 14, 1.05, 1.0, Xf(P3, R3));
			}
			{
				FDef& D = Add(P_Upper, SlotShoulders, EArmorRole::Trim);
				Torus(D.Mesh, 17.0, 0.85, 10, 6, 1.05, 1.0, Xf(Off(P1, R1, V3(0, 0, -4.0)), R1));
				Torus(D.Mesh, 18.0, 0.85, 10, 6, 1.05, 1.0, Xf(Off(P2, R2, V3(0, 0, -6.0)), R2));
			}
			{
				FDef& D = Add(P_Upper, SlotShoulders, EArmorRole::Trim, 1, 1);
				Blob(D.Mesh, 2.2, 2.2, 2.2, Xf(Off(P1, R1, V3(0, 0, 8.3)), R1));
			}
			{
				FDef& D = Add(P_Upper, SlotShoulders, EArmorRole::Trim, 2, 2);
				Lathe(D.Mesh, Prof({ 0, 17, 3.4, 3, 3.4, 0, 0, 0 }), 10, 1.0, 1.0, Xf(Off(P1, R1, V3(0, 0, 7.5)), R1));
			}
		}

		// =========================================================================================
		// ARMS, LEGS and BOOTS
		// =========================================================================================
		for (int32 sd = -1; sd <= 1; sd += 2)
		{
			const int32 Arm = (sd < 0) ? P_ArmL : P_ArmR, Elb = (sd < 0) ? P_ElbowL : P_ElbowR;
			const int32 Hip = (sd < 0) ? P_HipL : P_HipR, Knee = (sd < 0) ? P_KneeL : P_KneeR;
			const double Y = sd * 13.0;

			{ FDef& D = Add(Arm, SlotBody, EArmorRole::Under);
			  Lathe(D.Mesh, Prof({ 0, 9, 6.8, 8.4, 7.2, 0, 6.4, -7, 0, -8.5 }), 10, 1.0, 1.0, Xf(V3(0, sd * 27, -2))); }
			{ FDef& D = Add(Arm, SlotBody, EArmorRole::Plate);
			  Lathe(D.Mesh, Prof({ 0, 5, 8.4, 4.6, 8.9, 0, 8.4, -4.6, 0, -5 }), 10, 1.0, 1.0, Xf(V3(0, sd * 27, -5.5))); }
			{ FDef& D = Add(Arm, SlotBody, EArmorRole::Trim);
			  Torus(D.Mesh, 8.3, 0.9, 10, 6, 1.0, 1.0, Xf(V3(0, sd * 27, -0.6))); }
			{ FDef& D = Add(Elb, SlotBody, EArmorRole::Plate);
			  Ellipsoid(D.Mesh, 7.8, 7.6, 7.4, 0.85, 0.85, 12, 8, Xf(V3(1.5, sd * 27.8, -11)));              // elbow cop
			  Lathe(D.Mesh, Prof({ 0, 8, 6.6, 7.6, 7.2, 2, 7.6, -3, 7.8, -7, 0, -8 }), 10, 1.0, 1.0, Xf(V3(3.5, sd * 28, -22)));   // bracer
			  }
			{ FDef& D = Add(Elb, SlotGloves, EArmorRole::Plate);                                               // gauntlet
			  Ellipsoid(D.Mesh, 6.4, 5.6, 6.6, 0.8, 0.8, 12, 8, Xf(V3(7, sd * 30, -31))); }
			{ FDef& D = Add(Elb, SlotGloves, EArmorRole::Trim);
			  Torus(D.Mesh, 5.6, 0.9, 10, 6, 1.0, 1.0, Xf(V3(5.0, sd * 29.5, -26.5))); }
			{ FDef& D = Add(Elb, SlotGloves, EArmorRole::Under); D.bBare = true; D.Fixed = FLinearColor(0.80f, 0.58f, 0.42f);   // bare hand
			  Blob(D.Mesh, 5.6, 4.8, 6.0, Xf(V3(7, sd * 30, -31))); }
			{ FDef& D = Add(Elb, SlotBody, EArmorRole::Under);
			  Lathe(D.Mesh, Prof({ 0, 10, 5.7, 9.4, 5.9, 0, 5.3, -10, 0, -10.5 }), 10, 1.0, 1.0, Xf(V3(3, sd * 28, -21))); }
			{ FDef& D = Add(Elb, SlotBody, EArmorRole::Trim);
			  Torus(D.Mesh, 7.6, 0.9, 10, 6, 1.0, 1.0, Xf(V3(3.5, sd * 28, -15.8)));
			  Torus(D.Mesh, 7.8, 0.9, 10, 6, 1.0, 1.0, Xf(V3(3.5, sd * 28, -28.6))); }

			// Legs
			{ FDef& D = Add(Hip, SlotBody, EArmorRole::Under);
			  Lathe(D.Mesh, Prof({ 0, 10, 7.6, 9.4, 8.0, 0, 6.9, -9, 0, -10 }), 10, 1.0, 1.0, Xf(V3(0, Y, -57.5))); }
			{ FDef& D = Add(Hip, SlotBody, EArmorRole::Plate);
			  Lathe(D.Mesh, Prof({ 0, 9, 8.6, 8.4, 9.8, 3, 9.6, -3, 8.8, -8.4, 0, -9 }), 10, 1.06, 1.0, Xf(V3(1, Y, -57))); }
			{ FDef& D = Add(Knee, SlotBody, EArmorRole::Plate);
			  Ellipsoid(D.Mesh, 8.6, 8.2, 8.2, 0.9, 0.9, 12, 8, Xf(V3(5.5, Y, -67.5)));                      // knee cop
			  Lathe(D.Mesh, Prof({ 0, 8.5, 7.6, 8, 8.4, 3, 7.9, -3, 7, -8, 0, -8.5 }), 10, 1.1, 1.0, Xf(V3(1.2, Y, -80))); }  // greave
			{ FDef& D = Add(Knee, SlotBody, EArmorRole::Under);
			  Lathe(D.Mesh, Prof({ 0, 10, 6.1, 9.4, 5.8, 0, 5.2, -9, 0, -10 }), 10, 1.0, 1.0, Xf(V3(0, Y, -79))); }
			{ FDef& D = Add(Knee, SlotBody, EArmorRole::Trim);
			  RBox(D.Mesh, 1.0, 1.5, 7.0, 0.4, Xf(V3(9.2, Y, -80)));                                           // shin ridge
			  Torus(D.Mesh, 8.0, 0.9, 10, 6, 1.05, 1.0, Xf(V3(3, Y, -62.5))); }

			// Boots
			{ FDef& D = Add(Knee, SlotBoots, EArmorRole::BootBody);
			  Lathe(D.Mesh, Prof({ 0, 6, 7.8, 5.8, 8.2, 0, 7.8, -5.5, 0, -6 }), 10, 1.0, 1.0, Xf(V3(0, Y, -85.5)));
			  Lathe(D.Mesh, Prof({ 0, -10, 6.2, -9, 8.6, -4, 9, 3, 7.6, 11, 5.6, 18, 3.4, 23, 0, 25.5 }), 10, 0.62, 1.0, Xf(V3(0, Y, -90.3), V3(0, 90, 0))); }
			{ FDef& D = Add(Knee, SlotBoots, EArmorRole::Plate);
			  Lathe(D.Mesh, Prof({ 0, 9.5, 7.6, 10, 8.4, 12, 6.4, 18, 4.2, 23.2, 2.3, 26.3, 0, 27.2 }), 10, 0.62, 1.0, Xf(V3(0, Y, -90.3), V3(0, 90, 0))); }
			{ FDef& D = Add(Knee, SlotBoots, EArmorRole::Trim);
			  Torus(D.Mesh, 8.1, 1.0, 10, 6, 1.0, 1.0, Xf(V3(0, Y, -80.4))); }
			{ FDef& D = Add(Knee, SlotBoots, EArmorRole::Under);
			  Lathe(D.Mesh, Prof({ 0, -10.5, 6.4, -9.5, 8.8, -4, 9.2, 3, 7.8, 11, 5.8, 18, 3.6, 23, 0, 26 }), 10, 0.14, 1.0, Xf(V3(0, Y, -95.1), V3(0, 90, 0))); }
		}

		// =========================================================================================
		// The KITE SHIELD (weapons are modelled in WeaponDesign.h)
		// =========================================================================================
		// (the cape itself is cloth, simulated by the character: see AKnightCharacter::TickCape)

		// Shields (left arm). Three styles share one slot; each piece carries its Style so only the worn one shows:
		// 0 the kite (domed face, steel rim, a red cross that follows the dome), 1 the round shield (a deep dome, a boss in the
		// middle and studs round the rim), 2 the tower shield (tall and nearly flat, with a band down the middle and two straps).
		{
			const double YC = -36.0, BackX = 11.0, FrontX = 14.3;
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
				  for (int32 i = 0; i + 1 < NO; ++i) { Loop.Add(V3(12.65, YC + O[i].X, O[i].Y)); }
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

		// ---- slim everything below the helmet; the shield only slides in with the arm ----
		for (FDef& D : Out)
		{
			if (D.Slot == SlotHelmet) continue;
			for (FVector& V : D.Mesh.V)
			{
				if (D.Slot == SlotShield) { V.Y += 6.0; continue; }
				if (D.Slot != SlotCape) V.X *= SlimX;
				V.Y *= SlimY;
			}
		}
	}
}
