#pragma once

#include "CoreMinimal.h"
#include "KnightMesh.h"
#include "KnightDesign.h"
#include "Inventory.h"

// Every weapon and tool the knight can hold, modelled in code.
// A weapon is built around the hand's grip (the origin) with its business end pointing along -Z, so it points
// forward when the elbow is bent and down when the arm hangs. Place moves the whole thing into capsule space.
namespace WeaponDesign
{
	using namespace KM;
	using KnightDesign::V3;
	using KnightDesign::Off;

	// One piece of a weapon. Role follows EArmorRole (Plate = the blade or head, Trim = guard and fittings,
	// Leather = grip). bFixed pieces ignore the tier and use Fixed (wooden shafts, gems).
	struct FWPart
	{
		FMD Mesh;
		int32 Role = 0;
		bool bFixed = false;
		FLinearColor Fixed = FLinearColor::White;
		float Glow = 1.f;
	};

	struct FSec { double Z, W, T, Xo; };

	// bAlongGuard: the blade's flat lies in line with the crossguard (across Y), as on a real sword; otherwise across X (the katana)
	inline void BladeSolid(FMD& M, std::initializer_list<FSec> L, const FXf& Pl, bool bAlongGuard = true)
	{
		TArray<FRing> Rings;
		for (const FSec& S : L)
		{
			FRing Rg;
			Rg.Add(V3(S.Xo + S.W, 0, S.Z)); Rg.Add(V3(S.Xo + S.W * 0.55, S.T, S.Z)); Rg.Add(V3(S.Xo - S.W * 0.55, S.T, S.Z));
			Rg.Add(V3(S.Xo - S.W, 0, S.Z)); Rg.Add(V3(S.Xo - S.W * 0.55, -S.T, S.Z)); Rg.Add(V3(S.Xo + S.W * 0.55, -S.T, S.Z));
			if (bAlongGuard) for (FVector& P : Rg) Swap(P.X, P.Y);
			Rings.Add(Rg);
		}
		Solid(M, Rings, false, true, false, Pl, true);
	}

	// How far the elbow is bent when the weapon is just held (long weapons are carried pointing forward)
	inline float IdleElbow(int32 Kind)
	{
		switch (Kind)
		{
		case JeebItems::K_Staff: return 35.f;   // held upright, the arm a little out
		case JeebItems::K_Spear: case JeebItems::K_Wand: case JeebItems::K_Bow: case JeebItems::K_Rod: return 62.f;
		default: return 25.f;
		}
	}

	inline void Build(int32 Kind, TArray<FWPart>& Out, const FXf& Pl)
	{
		using namespace JeebItems;
		auto Add = [&](int32 Role) -> FMD& { FWPart P; P.Role = Role; Out.Add(P); return Out.Last().Mesh; };
		auto AddFixed = [&](const FLinearColor& C, float Glow = 1.f) -> FMD& { FWPart P; P.bFixed = true; P.Fixed = C; P.Glow = Glow; Out.Add(P); return Out.Last().Mesh; };
		auto At = [&](double X, double Y, double Z) { return Xf(Off(Pl.Loc, Pl.Rot, V3(X, Y, Z)), Pl.Rot); };
		const int32 Plate = 0, Trim = 2, Leather = 5;
		const FLinearColor Wood(0.34f, 0.21f, 0.09f, 1.f);

		switch (Kind)
		{
		case K_Sword:
		{
			Lathe(Add(Leather), KnightDesign::Prof({ 0, 10, 2.1, 9.6, 2.4, 5, 2.1, 0, 2.4, -5, 2.1, -9.6, 0, -10 }), 14, 1.0, 1.0, Pl);
			{ FMD& M = Add(Trim);
			  Blob(M, 3.4, 3.4, 3.4, At(0, 0, 12.5));
			  RBox(M, 1.8, 11.0, 1.7, 0.5, At(0, 0, -12.5));
			  Blob(M, 2.5, 2.5, 2.5, At(0, 11.4, -12.5)); Blob(M, 2.5, 2.5, 2.5, At(0, -11.4, -12.5)); }
			BladeSolid(Add(Plate), { {-13.5, 4.8, 1.4, 0}, {-55, 4.3, 1.2, 0}, {-65, 3.1, 0.9, 0}, {-72, 1.4, 0.6, 0}, {-76, 0, 0, 0} }, Pl);
			break;
		}
		case K_Broadsword:
		{
			Lathe(Add(Leather), KnightDesign::Prof({ 0, 16, 2.3, 15.5, 2.6, 8, 2.3, 0, 2.6, -8, 2.3, -15.5, 0, -16 }), 14, 1.0, 1.0, Pl);
			{ FMD& M = Add(Trim);
			  Blob(M, 4.2, 4.2, 4.2, At(0, 0, 19));
			  RBox(M, 2.2, 15.0, 2.0, 0.5, At(0, 0, -18));
			  Blob(M, 3.0, 3.0, 3.0, At(0, 15.5, -18)); Blob(M, 3.0, 3.0, 3.0, At(0, -15.5, -18)); }
			BladeSolid(Add(Plate), { {-19, 7.0, 1.9, 0}, {-80, 6.3, 1.7, 0}, {-95, 4.6, 1.3, 0}, {-107, 2.0, 0.8, 0}, {-112, 0, 0, 0} }, Pl);
			break;
		}
		case K_Katana:
		{
			Lathe(Add(Leather), KnightDesign::Prof({ 0, 9, 1.9, 8.6, 2.1, 4, 1.9, -2, 2.1, -8, 1.9, -12, 0, -13 }), 12, 1.0, 1.0, Pl);
			{ FMD& M = Add(Trim);
			  Lathe(M, KnightDesign::Prof({ 0, 0.9, 5.6, 0.9, 5.8, 0, 5.6, -0.9, 0, -0.9 }), 20, 1.0, 0.8, At(0, 0, -14));
			  Blob(M, 2.4, 2.4, 2.4, At(0, 0, 10.5)); }
			const double C = 0.0020;
			auto Xo = [&](double Z) { return -C * (Z + 14.0) * (Z + 14.0); };
			BladeSolid(Add(Plate), { {-15, 3.0, 0.9, Xo(-15)}, {-45, 3.0, 0.85, Xo(-45)}, {-70, 2.8, 0.8, Xo(-70)}, {-88, 2.0, 0.6, Xo(-88)}, {-98, 0.8, 0.3, Xo(-98)}, {-101, 0, 0, Xo(-101)} }, Pl, false);
			break;
		}
		case K_Axe:
		{
			Lathe(AddFixed(Wood), KnightDesign::Prof({ 0, 10, 2.1, 9.5, 2.3, 0, 2.3, -60, 2.0, -70, 0, -71 }), 12, 1.0, 1.0, Pl);
			{ FMD& M = Add(Trim);
			  RBox(M, 3.6, 2.4, 6.0, 0.6, At(-1, 0, -62)); }                // socket on the haft
			{ FMD& M = Add(Plate);
			  RBox(M, 7.5, 1.5, 9.0, 0.7, At(7.0, 0, -64));                    // head
			  Ellipsoid(M, 3.0, 1.0, 13.0, 1.0, 1.0, 14, 10, At(14.0, 0, -64)); } // curved cutting edge
			break;
		}
		case K_BattleAxe:
		{
			Lathe(AddFixed(Wood), KnightDesign::Prof({ 0, 16, 2.3, 15, 2.5, 0, 2.5, -88, 2.2, -102, 0, -103 }), 12, 1.0, 1.0, Pl);
			{ FMD& M = Add(Trim);
			  RBox(M, 4.4, 3.0, 7.0, 0.6, At(0, 0, -88));
			  Blob(M, 2.6, 2.6, 2.6, At(0, 0, -104)); }
			{ FMD& M = Add(Plate);
			  for (int32 sd = -1; sd <= 1; sd += 2)
			  {
				  RBox(M, 8.5, 1.6, 10.0, 0.7, At(sd * 8.5, 0, -88));
				  Ellipsoid(M, 3.0, 1.1, 15.0, 1.0, 1.0, 14, 10, At(sd * 17.0, 0, -88));
			  } }
			break;
		}
		case K_Pickaxe:
		{
			Lathe(AddFixed(Wood), KnightDesign::Prof({ 0, 10, 2.1, 9.5, 2.3, 0, 2.3, -60, 2.0, -70, 0, -71 }), 12, 1.0, 1.0, Pl);
			{ FMD& M = Add(Trim);
			  RBox(M, 3.0, 5.0, 3.2, 0.6, At(0, 0, -66)); }
			{ // curved pick head, tips swept forward
				TArray<FVector> Path; TArray<FVector2D> AB;
				for (int32 i = -6; i <= 6; ++i)
				{
					const double Y = i * 3.6;
					Path.Add(V3(0, Y, -66.0 - 0.020 * Y * Y));
					AB.Add(FVector2D(2.2 * (1.0 - 0.7 * FMath::Abs(i) / 6.0) + 0.35, 2.4 * (1.0 - 0.8 * FMath::Abs(i) / 6.0) + 0.3));
				}
				Sweep(Add(Plate), Path, AB, V3(1, 0, 0), 8, 0.8, Pl);
			}
			break;
		}
		case K_Spear:
		{
			Lathe(AddFixed(Wood), KnightDesign::Prof({ 0, 38, 1.9, 37, 2.1, 20, 2.1, -100, 1.8, -112, 0, -113 }), 12, 1.0, 1.0, Pl);
			{ FMD& M = Add(Trim);
			  Lathe(M, KnightDesign::Prof({ 0, -108, 2.6, -108, 3.4, -112, 3.4, -118, 2.4, -122, 0, -122 }), 12, 1.0, 1.0, Pl);
			  Blob(M, 2.2, 2.2, 2.2, At(0, 0, 40)); }
			BladeSolid(Add(Plate), { {-118, 2.4, 1.0, 0}, {-126, 4.6, 1.2, 0}, {-138, 3.6, 1.0, 0}, {-150, 1.4, 0.6, 0}, {-155, 0, 0, 0} }, Pl);
			break;
		}
		case K_Wand:
		{
			Lathe(AddFixed(Wood), KnightDesign::Prof({ 0, 9, 1.5, 8.5, 1.6, 0, 1.2, -30, 0.9, -42, 0, -43 }), 10, 1.0, 1.0, Pl);
			{ FMD& M = Add(Trim);
			  Torus(M, 1.8, 0.8, 12, 6, 1.0, 1.0, At(0, 0, -34));
			  Blob(M, 1.8, 1.8, 1.8, At(0, 0, 10)); }
			Blob(AddFixed(FLinearColor(0.7f, 0.5f, 1.0f), 2.2f), 3.4, 3.4, 3.8, At(0, 0, -47));
			break;
		}
		case K_Staff:
		{
			Lathe(AddFixed(Wood), KnightDesign::Prof({ 0, 34, 2.2, 33, 2.3, 0, 2.3, -100, 2.0, -112, 0, -113 }), 12, 1.0, 1.0, Pl);
			{ FMD& M = Add(Trim);
			  Torus(M, 4.6, 1.1, 16, 6, 1.0, 1.0, At(0, 0, -112));
			  Torus(M, 5.6, 1.0, 16, 6, 1.0, 1.0, At(0, 0, -127));
			  Torus(M, 3.0, 1.0, 12, 6, 1.0, 1.0, At(0, 0, -62));
			  Blob(M, 2.4, 2.4, 2.4, At(0, 0, 36)); }
			Blob(AddFixed(FLinearColor(0.4f, 0.8f, 1.0f), 2.2f), 6.4, 6.4, 6.4, At(0, 0, -120));
			break;
		}
		case K_Bow:
		{
			TArray<FVector> Path; TArray<FVector2D> AB;
			const int32 N = 14;
			for (int32 i = 0; i <= N; ++i)
			{
				const double X = -48.0 + 96.0 * double(i) / double(N);
				Path.Add(V3(X, 0, 0.0125 * X * X));
				AB.Add(FVector2D(1.5 + 1.2 * (1.0 - FMath::Abs(X) / 48.0), 1.5 + 1.1 * (1.0 - FMath::Abs(X) / 48.0)));
			}
			Sweep(Add(Plate), Path, AB, V3(0, 1, 0), 8, 1.0, Pl);
			{ FMD& M = Add(Leather);
			  RBox(M, 7.5, 2.4, 2.8, 0.6, At(0, 0, 0)); }
			{ FMD& M = Add(Trim);
			  Blob(M, 1.4, 1.4, 1.4, At(-48, 0, 28.8)); Blob(M, 1.4, 1.4, 1.4, At(48, 0, 28.8)); }
			{ TArray<FVector> SP; TArray<FVector2D> SA; // string
			  for (int32 i = 0; i <= 2; ++i) { SP.Add(V3(-48.0 + 48.0 * i, 0, 28.8)); SA.Add(FVector2D(0.28, 0.28)); }
			  Sweep(AddFixed(FLinearColor(0.9f, 0.88f, 0.78f)), SP, SA, V3(0, 1, 0), 5, 1.0, Pl); }
			break;
		}
		case K_Rod:
		{
			{ TArray<FVector> P; TArray<FVector2D> A;
			  for (int32 i = 0; i <= 10; ++i)
			  {
				  const double Z = 22.0 - 150.0 * double(i) / 10.0;
				  P.Add(V3(0, 0, Z)); A.Add(FVector2D(1.9 - 1.4 * double(i) / 10.0, 1.9 - 1.4 * double(i) / 10.0));
			  }
			  Sweep(AddFixed(Wood), P, A, V3(0, 1, 0), 8, 1.0, Pl); }
			{ FMD& M = Add(Trim);
			  Lathe(M, KnightDesign::Prof({ 0, 3, 3.6, 3, 4.0, 0, 3.6, -3, 0, -3 }), 14, 1.0, 1.0, At(3.5, 0, -14));
			  Blob(M, 1.6, 1.6, 1.6, At(0, 0, -124)); }
			Lathe(Add(Leather), KnightDesign::Prof({ 0, 18, 2.4, 17, 2.6, 4, 2.6, -8, 0, -9 }), 10, 1.0, 1.0, Pl);
			break;
		}
		default: break;
		}
	}
}
