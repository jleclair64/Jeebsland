#pragma once

#include "CoreMinimal.h"
#include "KnightMesh.h"
#include "KnightDesign.h"
#include "WeaponDesign.h"
#include "Inventory.h"

// What every item looks like lying on the ground, modelled in code. Weapons use the same models the knight holds; armour,
// shields, rings, amulets and the helm brush use the knight's own pieces; materials, fish, arrows, potions and ingots have
// their own small models here. Build() gives the pieces resting on the ground (lowest point at Z = 0), centred on the origin.
namespace JeebItemModel
{
	using namespace KM;
	using KnightDesign::V3;
	using KnightDesign::Prof;

	struct FPart
	{
		FMD Mesh;
		FLinearColor Color = FLinearColor::White;
		bool bMetal = false;
	};

	inline void Turn(TArray<FPart>& Parts, const FVector& Rot)
	{
		for (FPart& P : Parts) for (FVector& V : P.Mesh.V) V = RotateEuler(V, Rot);
	}
	inline void Scale(TArray<FPart>& Parts, double S)
	{
		for (FPart& P : Parts) for (FVector& V : P.Mesh.V) V *= S;
	}
	// Rest on the ground, centred
	inline void Settle(TArray<FPart>& Parts)
	{
		FBox B(ForceInit);
		for (const FPart& P : Parts) for (const FVector& V : P.Mesh.V) B += V;
		if (!B.IsValid) return;
		const FVector Off(-0.5 * (B.Min.X + B.Max.X), -0.5 * (B.Min.Y + B.Max.Y), -B.Min.Z);
		for (FPart& P : Parts) for (FVector& V : P.Mesh.V) V += Off;
	}

	// The knight's own pieces of one slot (optionally only one pivot), in the colours of that tier
	inline void FromKnight(TArray<FPart>& Out, int32 Slot, int32 Tier, int32 OnlyPivot = -1, int32 Style = -1)
	{
		TArray<KnightDesign::FDef> Defs;
		KnightDesign::Build(Defs);
		for (const KnightDesign::FDef& D : Defs)
		{
			if (D.Slot != Slot || D.bBare || D.Mesh.V.Num() == 0) continue;
			if (OnlyPivot >= 0 && D.Pivot != OnlyPivot) continue;
			if (Style >= 0 && D.Style != Style) continue;
			if (Tier < D.MinTier || Tier > D.MaxTier) continue;
			FPart P;
			P.Mesh = D.Mesh;
			if (D.bFixedColor) P.Color = D.Fixed;
			else
			{
				P.Color = JeebItems::Palette(Tier, static_cast<int32>(D.Role));
				P.Color.R *= D.Shade; P.Color.G *= D.Shade; P.Color.B *= D.Shade;
				P.bMetal = Tier >= 1 && (D.Role == EArmorRole::Plate || D.Role == EArmorRole::Trim || D.Role == EArmorRole::BootBody);
			}
			Out.Add(P);
		}
	}

	inline FPart& NewPart(TArray<FPart>& Out, const FLinearColor& C, bool bMetal = false)
	{
		FPart P; P.Color = C; P.bMetal = bMetal;
		Out.Add(P);
		return Out.Last();
	}

	// A small faceted crystal pointing up (diamonds), like the ones on the rocks
	inline void Crystal(FMD& M, double R, double H, const FVector& At, const FVector& Rot)
	{
		Lathe(M, Prof({ 0, -0.3 * H, R, 0.15 * H, 0.8 * R, 0.7 * H, 0, H }), 6, 1.0, 1.0, Xf(At, Rot), false, true);
	}

	inline void Build(int32 Id, TArray<FPart>& Out)
	{
		using namespace JeebItems;
		Out.Reset();

		// ---------------- materials, food and fish ----------------
		if (IsMaterial(Id))
		{
			switch (MaterialType(Id))
			{
			case EItemType::Logs: case EItemType::PineLogs: case EItemType::RedwoodLogs:
			{
				// a cut log lying on its side: bark round it, pale rings on the cut ends
				const EItemType T = MaterialType(Id);
				const FLinearColor Bark = T == EItemType::Logs ? FLinearColor(0.34f, 0.22f, 0.12f) : T == EItemType::PineLogs ? FLinearColor(0.22f, 0.15f, 0.09f) : FLinearColor(0.46f, 0.16f, 0.09f);
				const FLinearColor Wood = T == EItemType::Logs ? FLinearColor(0.78f, 0.62f, 0.40f) : T == EItemType::PineLogs ? FLinearColor(0.86f, 0.74f, 0.50f) : FLinearColor(0.72f, 0.36f, 0.24f);
				Lathe(NewPart(Out, Bark).Mesh, Prof({ 0, 17, 5.3, 17, 5.7, 11, 5.4, 2, 5.7, -7, 5.4, -17, 0, -17 }), 10, 1.0, 1.0, Xf(FVector::ZeroVector), false, true);
				FMD& E = NewPart(Out, Wood).Mesh;
				Ellipsoid(E, 4.8, 4.8, 0.35, 1.0, 1.0, 14, 4, Xf(V3(0, 0, 17.0)));
				Ellipsoid(E, 4.8, 4.8, 0.35, 1.0, 1.0, 14, 4, Xf(V3(0, 0, -17.0)));
				FMD& Ring = NewPart(Out, Bark * 0.8f).Mesh;   // growth rings on the ends
				Torus(Ring, 2.6, 0.25, 12, 4, 1.0, 1.0, Xf(V3(0, 0, 17.3)));
				Torus(Ring, 2.6, 0.25, 12, 4, 1.0, 1.0, Xf(V3(0, 0, -17.3)));
				Turn(Out, V3(0, 90, 0));
				break;
			}
			case EItemType::CopperOre:
			{
				// a grey chunk of rock with copper showing through
				RBox(NewPart(Out, FLinearColor(0.36f, 0.34f, 0.32f)).Mesh, 6.5, 5.5, 4.5, 0.7, Xf(V3(0, 0, 0), V3(10, -8, 25)), true);
				FMD& C = NewPart(Out, FLinearColor(0.85f, 0.45f, 0.22f), true).Mesh;
				Blob(C, 2.4, 2.0, 1.6, Xf(V3(3.5, 2.5, 3.6)));
				Blob(C, 2.0, 1.8, 1.4, Xf(V3(-3.0, -2.0, 3.8)));
				Blob(C, 1.6, 1.6, 1.4, Xf(V3(1.0, -3.6, 2.6)));
				break;
			}
			case EItemType::Coal:
			{
				FMD& C = NewPart(Out, FLinearColor(0.045f, 0.045f, 0.05f)).Mesh;   // three black, broken lumps
				Ellipsoid(C, 4.5, 3.8, 3.2, 0.8, 0.8, 7, 5, Xf(V3(0, 0, 0), V3(20, 10, 0)), true);
				Ellipsoid(C, 3.2, 3.0, 2.6, 0.8, 0.8, 7, 5, Xf(V3(5.5, 3.0, -0.6), V3(-15, 30, 40)), true);
				Ellipsoid(C, 2.8, 2.4, 2.2, 0.8, 0.8, 7, 5, Xf(V3(-4.5, 4.0, -0.8), V3(5, -20, 70)), true);
				break;
			}
			case EItemType::Diamond:
			{
				FMD& G = NewPart(Out, FLinearColor(0.55f, 1.15f, 1.3f), true).Mesh;   // a cluster of pale blue crystals
				Crystal(G, 2.6, 9.0, V3(0, 0, 0), V3(0, 0, 0));
				Crystal(G, 1.9, 6.5, V3(2.6, 1.2, -0.5), V3(-18, 22, 0));
				Crystal(G, 1.6, 5.0, V3(-2.4, -1.4, -0.5), V3(20, -24, 0));
				break;
			}
			case EItemType::Bacon:
			{
				RBox(NewPart(Out, FLinearColor(0.82f, 0.36f, 0.36f)).Mesh, 9.0, 4.5, 1.4, 0.5, Xf(V3(0, 0, 0)));
				FMD& Fat = NewPart(Out, FLinearColor(0.96f, 0.86f, 0.78f)).Mesh;
				for (int32 k = -1; k <= 1; ++k) RBox(Fat, 9.1, 0.6, 1.45, 0.5, Xf(V3(0, k * 2.6, 0)));
				break;
			}
			case EItemType::Perch: case EItemType::Trout: case EItemType::GoldenCarp:
			{
				// a fish lying on its side: body, tail, a fin on its back, an eye
				const EItemType T = MaterialType(Id);
				const FLinearColor Body = T == EItemType::Perch ? FLinearColor(0.42f, 0.52f, 0.18f) : T == EItemType::Trout ? FLinearColor(0.62f, 0.6f, 0.58f) : FLinearColor(1.0f, 0.68f, 0.12f);
				const double L = T == EItemType::Trout ? 15.0 : 12.0;
				FPart& B = NewPart(Out, Body, T == EItemType::GoldenCarp);
				Ellipsoid(B.Mesh, L, 2.4, 4.6, 1.0, 1.0, 18, 12, Xf(V3(0, 0, 0)));
				TArray<FVector> Tail = { V3(-L + 1.0, 0, 0), V3(-L - 5.5, 0, 4.0), V3(-L - 4.0, 0, 0), V3(-L - 5.5, 0, -4.0) };
				Prism(B.Mesh, Tail, V3(0, 0.6, 0));
				TArray<FVector> Fin = { V3(-3, 0, 3.8), V3(3, 0, 4.4), V3(-2, 0, 6.8) };
				Prism(B.Mesh, Fin, V3(0, 0.5, 0));
				FMD& Eye = NewPart(Out, FLinearColor(0.02f, 0.02f, 0.02f)).Mesh;
				Bead(Eye, 0.8, V3(L - 3.0, 1.9, 1.2));
				Bead(Eye, 0.8, V3(L - 3.0, -1.9, 1.2));
				if (T == EItemType::Trout)   // a pink stripe
					RBox(NewPart(Out, FLinearColor(0.85f, 0.45f, 0.5f)).Mesh, L * 0.75, 2.5, 0.8, 0.6, Xf(V3(0, 0, 0)));
				Turn(Out, V3(90, 0, 0));   // onto its side
				break;
			}
			case EItemType::Hide:
			{
				// a rough brown pelt, flat on the ground, with its four legs and a tail spread out
				FMD& H = NewPart(Out, FLinearColor(0.42f, 0.28f, 0.16f)).Mesh;
				Ellipsoid(H, 15.0, 10.0, 0.7, 1.0, 1.0, 20, 6, Xf(V3(0, 0, 0)));
				for (int32 a = -1; a <= 1; a += 2) for (int32 b = -1; b <= 1; b += 2)
					Ellipsoid(H, 4.0, 2.0, 0.6, 1.0, 1.0, 12, 4, Xf(V3(a * 10.0, b * 10.5, 0), V3(0, 0, a * b * 35.0)));
				Ellipsoid(H, 5.0, 1.5, 0.6, 1.0, 1.0, 12, 4, Xf(V3(-18.0, 0, 0)));
				Ellipsoid(NewPart(Out, FLinearColor(0.58f, 0.42f, 0.26f)).Mesh, 9.0, 5.5, 0.5, 1.0, 1.0, 16, 4, Xf(V3(1.0, 0, 0.4)));
				break;
			}
			case EItemType::Bones:
			{
				FMD& B = NewPart(Out, FLinearColor(0.9f, 0.86f, 0.76f)).Mesh;   // two bones, crossed
				for (int32 k = 0; k < 2; ++k)
				{
					const FVector R(0, 0, k == 0 ? 25.0 : -30.0);
					Lathe(B, Prof({ 0, 9, 1.2, 8, 1.0, 0, 1.2, -8, 0, -9 }), 10, 1.0, 1.0, Xf(V3(0, 0, k * 2.4), V3(90, 0, R.Z)));
					for (int32 e = -1; e <= 1; e += 2)
						for (int32 s = -1; s <= 1; s += 2)
						{
							const FVector End = RotateEuler(V3(s * 1.2, e * 9.0, 0), V3(0, 0, R.Z));
							Blob(B, 1.6, 1.6, 1.6, Xf(End + V3(0, 0, k * 2.4)));
						}
				}
				break;
			}
			default: break;
			}
			Settle(Out);
			return;
		}

		// ---------------- arrows: a bundle of three ----------------
		if (IsArrow(Id))
		{
			const int32 T = ArrowTier(Id);
			for (int32 k = -1; k <= 1; ++k)
			{
				const FVector At(0, k * 1.6, 0);
				Lathe(NewPart(Out, FLinearColor(0.5f, 0.34f, 0.17f)).Mesh, Prof({ 0, 20, 0.45, 19.5, 0.45, -19.5, 0, -20 }), 8, 1.0, 1.0, Xf(At, V3(0, 90, k * 4.0)));
				Lathe(NewPart(Out, Palette(T, 0), T >= 1).Mesh, Prof({ 0, 4.0, 1.1, 0, 0, -0.3 }), 6, 1.0, 1.0, Xf(At + RotateEuler(V3(20, 0, 0), V3(0, 0, k * 4.0)), V3(0, 90, k * 4.0)), false, true);
				FMD& F = NewPart(Out, FLinearColor(0.92f, 0.9f, 0.86f)).Mesh;
				const FVector Back = At + RotateEuler(V3(-17, 0, 0), V3(0, 0, k * 4.0));
				TArray<FVector> Fl = { Back + V3(-2.5, 0, 0), Back + V3(2.5, 0, 0), Back + V3(-3.0, 0, 1.6) };
				Prism(F, Fl, V3(0, 0.2, 0));
			}
			Settle(Out);
			return;
		}

		// ---------------- potions: a round flask with a cork ----------------
		if (IsPotion(Id))
		{
			static const FLinearColor Fill[NumPotions] = { FLinearColor(0.2f, 0.85f, 0.25f), FLinearColor(0.9f, 0.1f, 0.08f), FLinearColor(0.55f, 0.15f, 0.9f) };
			Blob(NewPart(Out, Fill[PotionType(Id)] * 1.2f).Mesh, 4.6, 4.6, 4.6, Xf(V3(0, 0, 4.6)));
			Lathe(NewPart(Out, FLinearColor(0.75f, 0.85f, 0.9f), true).Mesh, Prof({ 0, 8.0, 1.5, 8.0, 1.5, 11.5, 0, 11.5 }), 10, 1.0, 1.0, Xf(FVector::ZeroVector));
			Lathe(NewPart(Out, FLinearColor(0.55f, 0.36f, 0.18f)).Mesh, Prof({ 0, 11.3, 1.7, 11.3, 1.6, 13.3, 0, 13.3 }), 10, 1.0, 1.0, Xf(FVector::ZeroVector));
			Settle(Out);
			return;
		}

		if (!IsGear(Id)) return;
		const int32 K = KindOf(Id), V = VariantOf(Id);

		// ---------------- weapons and tools: the model the knight holds, laid flat ----------------
		if (IsWeaponKind(K))
		{
			TArray<WeaponDesign::FWPart> Parts;
			WeaponDesign::Build(K, Parts, Xf(FVector::ZeroVector));
			for (const WeaponDesign::FWPart& W : Parts)
			{
				if (W.Mesh.V.Num() == 0) continue;
				FPart& P = NewPart(Out, W.bFixed ? W.Fixed * W.Glow : Palette(V, W.Role));
				P.Mesh = W.Mesh;
				P.bMetal = !W.bFixed && V >= 1 && (W.Role == 0 || W.Role == 2 || W.Role == 6);
			}
			Turn(Out, K == K_Katana ? V3(90, 0, 30) : V3(0, 90, 30));   // blade flat on the ground, at a slant
			Settle(Out);
			return;
		}

		// ---------------- worn gear: the knight's own pieces ----------------
		using namespace KnightDesign;
		switch (K)
		{
		case K_Shield:    FromKnight(Out, SlotShield, V, -1, StyleOf(Id)); Turn(Out, V3(0, -90, 0)); break;   // face up
		case K_Helmet:    FromKnight(Out, SlotHelmet, V, P_Head); break;
		case K_Chest:     FromKnight(Out, SlotBody, V, P_Upper); break;
		case K_Shoulders: FromKnight(Out, SlotShoulders, V); break;
		case K_Gloves:    FromKnight(Out, SlotGloves, V, P_ElbowR); FromKnight(Out, SlotGloves, V, P_ElbowL); Turn(Out, V3(0, 0, 0)); break;
		case K_Boots:     FromKnight(Out, SlotBoots, V, P_AnkleR); FromKnight(Out, SlotBoots, V, P_AnkleL); break;
		case K_Ring:      FromKnight(Out, SlotRingR, 0); Scale(Out, 2.5); break;    // bigger than life, so it can be seen in the grass
		case K_Amulet:    FromKnight(Out, SlotAmulet, 0); Turn(Out, V3(0, -80, 0)); Scale(Out, 1.6); break;
		case K_Crest:     FromKnight(Out, SlotBrush, 0); break;
		case K_Cape:
		{
			// a folded cloak: three layers of cloth with the fold showing
			const FLinearColor C = CapeColor(V);
			FMD& M = NewPart(Out, C).Mesh;
			RBox(M, 16.0, 11.0, 1.2, 0.3, Xf(V3(0, 0, 1.2)));
			RBox(M, 15.0, 10.5, 1.2, 0.3, Xf(V3(0.5, 0, 3.5)));
			RBox(M, 14.0, 10.0, 1.2, 0.3, Xf(V3(1.0, 0, 5.8)));
			Lathe(NewPart(Out, C * 0.75f).Mesh, Prof({ 1.2, -10.5, 1.2, 10.5 }), 10, 1.0, 1.0, Xf(V3(-15.0, 0, 3.5), V3(90, 0, 0)));
			break;
		}
		default: break;
		}
		Settle(Out);
	}

	// A small stack of gold ingot bars
	inline void BuildIngots(int32 Count, TArray<FPart>& Out)
	{
		Out.Reset();
		FPart& P = NewPart(Out, FLinearColor(1.0f, 0.72f, 0.2f), true);
		const int32 N = FMath::Clamp(Count / 10 + 1, 1, 5);
		for (int32 i = 0; i < N; ++i)
		{
			const int32 Row = i < 3 ? 0 : 1;
			const double Y = Row == 0 ? (i - 1) * 5.0 : (i - 3.5) * 5.0;
			Ellipsoid(P.Mesh, 6.0, 2.2, 1.5, 0.25, 0.25, 12, 8, Xf(V3(0, Y, 1.5 + Row * 3.0)));
		}
		Settle(Out);
	}
}
