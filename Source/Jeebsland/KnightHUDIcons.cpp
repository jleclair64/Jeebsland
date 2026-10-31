#include "KnightHUD.h"
#include "SkillComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "KnightCharacter.h"
#include "Pig.h"
#include "FoeKnight.h"
#include "Loot.h"
#include "Npc.h"
#include "HealthComponent.h"
#include "Combat.h"
#include "Lore.h"
#include "Horse.h"
#include "MiningRock.h"
#include "Terrain.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Inventory.h"
#include "CanvasItem.h"
#include "Store.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "TextureResource.h"
#include "RenderUtils.h"
#include "Messages.h"

using namespace JeebItems;

// ===================================================================================================
// Drawing helpers and icons
// ===================================================================================================
void AKnightHUD::Poly(const TArray<FVector2D>& Src, const FLinearColor& C)
{
	// A smooth (anti-aliased) fill, for the item and skill pictures. Each pixel row is sampled at three heights; the middle of
	// the shape is one solid bar, and the edge pixels get a share of the colour in proportion to how much of them the shape
	// covers. Works for any shape whose rows are one solid run.
	if (Src.Num() < 3) return;
	TArray<FVector2D> P = Src;
	if (!FMath::IsNearlyZero(IconTiltDeg))   // lean the whole picture about its centre
	{
		const float Cs = FMath::Cos(FMath::DegreesToRadians(IconTiltDeg)), Sn = FMath::Sin(FMath::DegreesToRadians(IconTiltDeg));
		for (FVector2D& V : P)
		{
			const FVector2D D = V - IconPivot;
			V = IconPivot + FVector2D(D.X * Cs - D.Y * Sn, D.X * Sn + D.Y * Cs);
		}
	}
	float MinY = P[0].Y, MaxY = P[0].Y;
	for (const FVector2D& V : P) { MinY = FMath::Min(MinY, V.Y); MaxY = FMath::Max(MaxY, V.Y); }
	constexpr int32 Subs = 3;
	for (int32 Row = FMath::FloorToInt(MinY); Row < FMath::CeilToInt(MaxY); ++Row)
	{
		float A[Subs], B[Subs];
		bool Has[Subs];
		bool bAll = true, bAny = false;
		float MinA = 1e9f, MaxB = -1e9f, CoreL = -1e9f, CoreR = 1e9f;
		for (int32 k = 0; k < Subs; ++k)
		{
			const float Y = Row + (k + 0.5f) / Subs;
			float X0 = 1e9f, X1 = -1e9f;
			for (int32 i = 0; i < P.Num(); ++i)
			{
				const FVector2D& Pa = P[i];
				const FVector2D& Pb = P[(i + 1) % P.Num()];
				if ((Pa.Y <= Y && Pb.Y > Y) || (Pb.Y <= Y && Pa.Y > Y))
				{
					const float X = Pa.X + (Y - Pa.Y) / (Pb.Y - Pa.Y) * (Pb.X - Pa.X);
					X0 = FMath::Min(X0, X);
					X1 = FMath::Max(X1, X);
				}
			}
			Has[k] = X1 > X0;
			A[k] = X0; B[k] = X1;
			if (Has[k]) { bAny = true; MinA = FMath::Min(MinA, X0); MaxB = FMath::Max(MaxB, X1); CoreL = FMath::Max(CoreL, X0); CoreR = FMath::Min(CoreR, X1); }
			else bAll = false;
		}
		if (!bAny) continue;
		auto Cover = [&](int32 Col)   // how much of the pixel (Col, Row) the shape covers, 0..1
		{
			float S = 0.f;
			for (int32 k = 0; k < Subs; ++k)
				if (Has[k]) S += FMath::Max(0.f, FMath::Min(float(Col + 1), B[k]) - FMath::Max(float(Col), A[k]));
			return S / Subs;
		};
		auto Run = [&](int32 Col0, int32 Col1)   // pixels Col0 .. Col1-1: equal coverage is drawn as one bar
		{
			int32 Start = Col0;
			float Cur = Cover(Col0);
			for (int32 Col = Col0 + 1; Col <= Col1; ++Col)
			{
				const float Cv = (Col < Col1) ? Cover(Col) : -1.f;
				if (Col == Col1 || FMath::Abs(Cv - Cur) > 0.03f)
				{
					if (Cur > 0.02f) DrawRect(FLinearColor(C.R, C.G, C.B, C.A * FMath::Min(Cur, 1.f)), float(Start), float(Row), float(Col - Start), 1.f);
					Start = Col; Cur = Cv;
				}
			}
		};
		const int32 C0 = FMath::FloorToInt(MinA), C1 = FMath::CeilToInt(MaxB);
		if (bAll && CoreR - CoreL > 1.f)
		{
			const int32 SL = FMath::CeilToInt(CoreL), SR = FMath::FloorToInt(CoreR);
			if (SR > SL)
			{
				if (SL > C0) Run(C0, SL);
				DrawRect(C, float(SL), float(Row), float(SR - SL), 1.f);
				if (C1 > SR) Run(SR, C1);
				continue;
			}
		}
		Run(C0, C1);
	}
}

void AKnightHUD::Disc(float Cx, float Cy, float Rx, float Ry, const FLinearColor& C)
{
	TArray<FVector2D> P;
	const int32 N = 32;
	for (int32 i = 0; i < N; ++i)
	{
		const float A = i * 2.f * PI / N;
		P.Add(FVector2D(Cx + FMath::Cos(A) * Rx, Cy + FMath::Sin(A) * Ry));
	}
	Poly(P, C);
}

void AKnightHUD::DrawIngot(float X, float Y, float S)
{
	PolyStep = 1.f;
	// A gold bar: a trapezoid top face and a front face
	const float U = S / 32.f;
	Poly({ FVector2D(X + 6 * U, Y + 8 * U), FVector2D(X + 26 * U, Y + 8 * U), FVector2D(X + 31 * U, Y + 16 * U), FVector2D(X + 1 * U, Y + 16 * U) }, FLinearColor(1.f, 0.88f, 0.35f));
	Poly({ FVector2D(X + 1 * U, Y + 16 * U), FVector2D(X + 31 * U, Y + 16 * U), FVector2D(X + 31 * U, Y + 26 * U), FVector2D(X + 1 * U, Y + 26 * U) }, FLinearColor(0.85f, 0.60f, 0.08f));
	Poly({ FVector2D(X + 8 * U, Y + 11 * U), FVector2D(X + 18 * U, Y + 11 * U), FVector2D(X + 16 * U, Y + 14 * U), FVector2D(X + 6 * U, Y + 14 * U) }, FLinearColor(1.f, 0.97f, 0.7f));
}

// Every weapon icon leans the same way wherever it is drawn (pack, bar, drag): 35 degrees, the fishing rod a little less because
// its picture already leans
void AKnightHUD::DrawIcon(int32 Id, float X, float Y, float S)
{
	const float SavedTilt = IconTiltDeg;
	const FVector2D SavedPivot = IconPivot;
	if (FMath::IsNearlyZero(SavedTilt) && IsGear(Id) && IsWeaponKind(KindOf(Id)))
	{
		IconTiltDeg = (KindOf(Id) == K_Rod) ? -9.f : 35.f;
		IconPivot = FVector2D(X + S * 0.5f, Y + S * 0.5f);
	}
	DrawIconBody(Id, X, Y, S);
	IconTiltDeg = SavedTilt;
	IconPivot = SavedPivot;
}

void AKnightHUD::DrawIconBody(int32 Id, float X, float Y, float S)
{
	PolyStep = FMath::Clamp(S / 44.f, 1.f, 2.5f);
	const float U = S / 64.f;
	auto P = [&](float A, float B) { return FVector2D(X + A * U, Y + B * U); };
	auto Quad = [&](float x0, float y0, float x1, float y1, const FLinearColor& C) { Poly({ P(x0, y0), P(x1, y0), P(x1, y1), P(x0, y1) }, C); };
	auto DiscP = [&](float Cx, float Cy, float Rx, float Ry, const FLinearColor& C) { Disc(X + Cx * U, Y + Cy * U, Rx * U, Ry * U, C); };

	// ---------------- potions: a round flask with a cork, filled with its colour ----------------
	if (IsPotion(Id))
	{
		const FLinearColor F = PotionRow(PotionType(Id)).Color;   // (Potions.csv)
		const FLinearColor Glass(0.75f, 0.85f, 0.95f, 0.55f), Cork(0.55f, 0.36f, 0.18f);
		DiscP(32, 42, 18, 18, Glass);                                      // the bulb
		DiscP(32, 46, 15, 11, F);                                          // the potion inside, filling the lower part
		Quad(27, 12, 37, 27, Glass);                                       // the neck
		Quad(26, 6, 38, 13, Cork);                                         // the cork
		DiscP(25, 36, 4, 6, FLinearColor(1.f, 1.f, 1.f, 0.55f));           // a highlight on the glass
		return;
	}
	// ---------------- arrows: a bundle of three ----------------
	if (IsArrow(Id))
	{
		const int32 T = ArrowTier(Id);
		const bool bFire = IsFireArrow(Id);   // (fire arrows: a flame-orange head and red fletching)
		const FLinearColor Head = bFire ? FLinearColor(1.f, 0.45f, 0.08f) : Lift(Palette(T, 0), 1.3f), Shaft(0.50f, 0.34f, 0.17f), Fl = bFire ? FLinearColor(0.85f, 0.18f, 0.08f) : FLinearColor(0.92f, 0.92f, 0.88f);
		for (int32 k = 0; k < 3; ++k)
		{
			const float Ox = (k - 1) * 11.f;
			Quad(30.f + Ox, 12.f, 33.f + Ox, 56.f, Shaft);
			Poly({ P(31.5f + Ox, 2.f), P(37.f + Ox, 14.f), P(31.5f + Ox, 11.f), P(26.f + Ox, 14.f) }, Head);
			Poly({ P(31.5f + Ox, 52.f), P(37.f + Ox, 62.f), P(31.5f + Ox, 58.f), P(26.f + Ox, 62.f) }, Fl);
		}
		return;
	}

	// ---------------- gear ----------------
	if (IsGear(Id))
	{
		const int32 K = KindOf(Id), V = VariantOf(Id);

		if (K == K_Crest)
		{
			// a red horsehair brush fanning up out of a gold socket
			const FLinearColor Red(0.82f, 0.10f, 0.08f), Deep(0.55f, 0.05f, 0.04f), Gold(0.95f, 0.75f, 0.25f);
			Poly({ P(10, 40), P(13, 24), P(22, 13), P(32, 9), P(42, 13), P(51, 24), P(54, 40), P(44, 35), P(32, 33), P(20, 35) }, Red);
			Poly({ P(20, 35), P(24, 20), P(32, 15), P(30, 33) }, Lift(Red, 1.25f));
			for (int32 s = -2; s <= 2; ++s)   // strands
				Poly({ P(32.f + s * 3.f, 33.f), P(32.f + s * 8.5f + 1.2f, 15.f + FMath::Abs(s) * 3.f), P(32.f + s * 8.5f + 2.6f, 16.f + FMath::Abs(s) * 3.f), P(32.f + s * 3.f + 1.4f, 33.f) }, Deep);
			Poly({ P(24, 36), P(40, 36), P(38, 48), P(26, 48) }, Gold);
			Quad(29, 48, 35, 58, Lift(Gold, 0.75f));
			return;
		}
		if (K == K_Ring || K == K_Amulet)
		{
			const FAccessoryRow& Acc = AccessoryRow(K, V);   // (Accessories.csv: Band and Gem colours)
			if (K == K_Ring)
			{
				DiscP(32, 38, 19, 19, Acc.Band);
				DiscP(32, 38, 12, 12, FLinearColor(0.10f, 0.09f, 0.08f));
				Poly({ P(32, 8), P(42, 17), P(32, 27), P(22, 17) }, Acc.Gem);
				Poly({ P(32, 8), P(42, 17), P(32, 17) }, Lift(Acc.Gem, 1.3f));
			}
			else
			{
				const FLinearColor Chain(0.85f, 0.85f, 0.9f);
				for (int32 k = 0; k < 9; ++k)
				{
					const float T = k / 8.f;
					DiscP(8.f + 48.f * T, 6.f + 30.f * (1.f - FMath::Abs(2.f * T - 1.f)) * 1.f, 2.4f, 2.4f, Chain);
				}
				Poly({ P(32, 30), P(46, 42), P(32, 60), P(18, 42) }, Acc.Gem);
				Poly({ P(32, 30), P(46, 42), P(32, 42) }, Lift(Acc.Gem, 1.35f));
				Poly({ P(32, 30), P(36, 28), P(32, 24), P(28, 28) }, Chain);
			}
			return;
		}

		if (K == K_Cape)
		{
			const FLinearColor Ac = CapeColor(V);
			const FLinearColor Lining(Ac.R * 0.7f, Ac.G * 0.7f, Ac.B * 0.7f, 1.f);
			Poly({ P(16, 10), P(48, 10), P(58, 58), P(6, 58) }, Ac);
			Poly({ P(34, 10), P(48, 10), P(58, 58), P(40, 58) }, Lining);
			Quad(12, 6, 52, 13, FLinearColor(0.85f, 0.7f, 0.25f));
			return;
		}

		const FLinearColor Pl = Lift(Palette(V, 0), 1.25f), Dk = Palette(V, 1), Tr = Lift(Palette(V, 2), 1.1f), Ac = Palette(V, 4), Lt = Palette(V, 5);
		const FLinearColor Wd(0.45f, 0.28f, 0.12f), Gem = GemColor(V);
		const FLinearColor Edge = Lift(Palette(V, 3), 1.0f);
		switch (K)
		{
		case K_Sword:
			Poly({ P(30, 2), P(34, 2), P(35, 40), P(32, 44), P(29, 40) }, Pl);
			Quad(31.5f, 4, 32.5f, 40, Lift(Pl, 0.7f));
			Quad(20, 40, 44, 46, Tr);
			Quad(29, 46, 35, 56, Lt);
			DiscP(32, 58, 5, 4, Tr);
			break;
		case K_Broadsword:
			Poly({ P(27, 0), P(37, 0), P(39, 38), P(32, 44), P(25, 38) }, Pl);
			Quad(31, 3, 33, 38, Lift(Pl, 0.7f));
			Quad(16, 38, 48, 45, Tr);
			Quad(29, 45, 35, 58, Lt);
			DiscP(32, 60, 5, 4, Tr);
			break;
		case K_Axe:
			Quad(29, 8, 34, 62, Wd);
			Poly({ P(34, 8), P(54, 4), P(58, 18), P(54, 32), P(34, 26) }, Pl);
			Poly({ P(54, 4), P(58, 18), P(54, 32), P(50, 30), P(53, 18), P(50, 6) }, Edge);
			Quad(24, 10, 34, 24, Tr);
			break;
		case K_BattleAxe:
			Quad(30, 4, 34, 62, Wd);
			Poly({ P(34, 8), P(54, 2), P(60, 18), P(54, 34), P(34, 28) }, Pl);
			Poly({ P(30, 8), P(10, 2), P(4, 18), P(10, 34), P(30, 28) }, Pl);
			Quad(26, 14, 38, 22, Tr);
			Poly({ P(32, 0), P(36, 6), P(28, 6) }, Tr);
			break;
		case K_Pickaxe:
			Quad(30, 12, 34, 62, Wd);
			Poly({ P(4, 24), P(14, 10), P(32, 6), P(50, 10), P(60, 24), P(50, 17), P(32, 13), P(14, 17) }, Pl);
			Quad(27, 10, 37, 18, Tr);
			break;
		case K_Spear:
			Quad(30, 18, 34, 62, Wd);
			Poly({ P(32, 0), P(24, 18), P(32, 24), P(40, 18) }, Pl);
			Poly({ P(32, 0), P(40, 18), P(32, 24) }, Lift(Pl, 0.75f));
			Quad(28, 22, 36, 27, Tr);
			break;
		case K_Katana:
			Poly({ P(42, 2), P(46, 5), P(38, 28), P(27, 46), P(22, 44), P(31, 26) }, Pl);
			Poly({ P(42, 2), P(46, 5), P(44, 9) }, Edge);
			DiscP(24, 46, 6, 3, Tr);
			Poly({ P(22, 46), P(27, 48), P(18, 62), P(13, 60) }, Lt);
			break;
		case K_Wand:
			Poly({ P(12, 54), P(16, 58), P(44, 24), P(40, 20) }, Wd);
			DiscP(46, 18, 8, 8, Gem);
			DiscP(44, 16, 3, 3, Lift(Gem, 1.6f));
			DiscP(36, 28, 4, 3, Tr);
			break;
		case K_Staff:
			Quad(30, 16, 34, 62, Wd);
			DiscP(32, 11, 9, 9, Gem);
			DiscP(29, 8, 3, 3, Lift(Gem, 1.6f));
			Quad(21, 19, 43, 22, Tr);
			break;
		case K_Bow:
			Poly({ P(44, 2), P(54, 16), P(57, 32), P(54, 48), P(44, 62), P(40, 59), P(49, 47), P(52, 32), P(49, 17), P(40, 5) }, Pl);
			Quad(40.5f, 4, 42.5f, 60, Edge);
			Quad(38, 28, 47, 36, Lt);
			break;
		case K_Rod:
			Poly({ P(8, 58), P(12, 62), P(56, 8), P(52, 4) }, Wd);
			DiscP(20, 48, 6, 6, Tr);
			Quad(56, 6, 57, 40, FLinearColor(0.9f, 0.9f, 0.9f));
			DiscP(56.5f, 44, 3, 3, FLinearColor(0.9f, 0.2f, 0.15f));
			break;
		case K_Shield:
			if (StyleOf(Id) == 1)   // round: rim, face, a boss in the middle and studs round the edge
			{
				DiscP(32, 32, 27, 27, Tr);
				DiscP(32, 32, 23, 23, Pl);
				DiscP(32, 32, 7, 7, Ac);
				for (int32 s = 0; s < 8; ++s) { const float A = s * PI / 4.f; DiscP(32 + 17.f * FMath::Cos(A), 32 + 17.f * FMath::Sin(A), 2.2f, 2.2f, Ac); }
			}
			else if (StyleOf(Id) == 2)   // tower: tall, with a band down the middle and two straps
			{
				Poly({ P(14, 2), P(50, 2), P(52, 6), P(52, 58), P(48, 62), P(16, 62), P(12, 58), P(12, 6) }, Tr);
				Poly({ P(16, 6), P(48, 6), P(48, 58), P(16, 58) }, Pl);
				Quad(29, 6, 35, 58, Ac);
				Quad(16, 16, 48, 19, Ac);
				Quad(16, 46, 48, 49, Ac);
			}
			else   // kite, with its cross
			{
				Poly({ P(10, 6), P(54, 6), P(54, 28), P(32, 60), P(10, 28) }, Tr);
				Poly({ P(14, 10), P(50, 10), P(50, 27), P(32, 54), P(14, 27) }, Pl);
				Quad(29, 12, 35, 46, Ac);
				Quad(19, 21, 45, 27, Ac);
			}
			break;
		case K_Helmet:
			Poly({ P(32, 0), P(36, 6), P(28, 6) }, Ac);
			Poly({ P(14, 14), P(32, 3), P(50, 14), P(32, 11) }, Ac);
			DiscP(32, 28, 20, 18, Pl);
			Quad(12, 28, 52, 52, Pl);
			Quad(17, 31, 28, 38, Dk);
			Quad(36, 31, 47, 38, Dk);
			Quad(30, 28, 34, 54, Tr);
			Quad(17, 41, 25, 52, Dk);
			Quad(39, 41, 47, 52, Dk);
			Quad(12, 50, 52, 54, Tr);
			break;
		case K_Chest:
			Poly({ P(14, 10), P(50, 10), P(56, 26), P(47, 54), P(17, 54), P(8, 26) }, Pl);
			Poly({ P(25, 10), P(39, 10), P(32, 20) }, Dk);
			Quad(30, 20, 34, 54, Tr);
			Quad(16, 42, 48, 48, Lt);
			Quad(29, 41, 35, 49, Tr);
			break;
		case K_Shoulders:
			DiscP(19, 36, 16, 13, Pl);
			DiscP(45, 36, 16, 13, Pl);
			Quad(5, 38, 33, 41, Tr);
			Quad(31, 38, 59, 41, Tr);
			DiscP(19, 26, 4, 4, Tr);
			DiscP(45, 26, 4, 4, Tr);
			break;
		case K_Gloves:
			for (int32 g = 0; g < 2; ++g)
			{
				const float Ox = g * 30.f;
				Quad(8 + Ox, 30, 26 + Ox, 50, Pl);
				for (int32 f = 0; f < 4; ++f) Quad(8 + Ox + f * 4.6f, 12 + (f == 1 ? -3 : (f == 3 ? 3 : 0)), 12 + Ox + f * 4.6f, 32, Pl);
				Poly({ P(26 + Ox, 36), P(34 + Ox, 30), P(34 + Ox, 36), P(26 + Ox, 44) }, Pl);
				Quad(6 + Ox, 48, 28 + Ox, 58, Tr);
			}
			break;
		case K_Boots:
			for (int32 b = 0; b < 2; ++b)
			{
				const float Ox = b * 28.f;
				Poly({ P(8 + Ox, 10), P(24 + Ox, 10), P(24 + Ox, 40), P(36 + Ox, 50), P(36 + Ox, 56), P(8 + Ox, 56) }, Pl);
				Quad(8 + Ox, 10, 24 + Ox, 15, Tr);
				Quad(8 + Ox, 52, 36 + Ox, 56, Lt);
			}
			break;
		default: break;
		}
		return;
	}

	// ---------------- materials and fish ----------------
	if (!IsMaterial(Id)) return;
	switch (MaterialType(Id))
	{
	case EItemType::CopperOre:
	case EItemType::Coal:
	case EItemType::TinOre:
	case EItemType::IronOre:
	case EItemType::SilverOre:
	case EItemType::GoldOre:
	{
		const bool bCoal = MaterialType(Id) == EItemType::Coal;
		const FLinearColor Body = bCoal ? FLinearColor(0.10f, 0.10f, 0.12f) : FLinearColor(0.38f, 0.34f, 0.32f);
		const FLinearColor Lit = bCoal ? FLinearColor(0.26f, 0.26f, 0.30f) : FLinearColor(0.55f, 0.50f, 0.46f);
		Poly({ P(8, 46), P(12, 26), P(26, 12), P(46, 16), P(57, 34), P(52, 52), P(30, 56) }, Body);
		Poly({ P(12, 26), P(26, 12), P(46, 16), P(34, 28), P(20, 34) }, Lit);
		if (bCoal)
		{
			Poly({ P(30, 34), P(44, 30), P(50, 44), P(36, 50) }, FLinearColor(0.02f, 0.02f, 0.03f));
			Poly({ P(18, 40), P(26, 36), P(28, 46), P(20, 48) }, FLinearColor(0.04f, 0.04f, 0.05f));
		}
		else
		{
			const FLinearColor Cu = OreColor(MaterialType(Id)) * 1.08f;   // the ore showing through the rock (copper, tin, iron, silver, gold)
			Poly({ P(22, 28), P(32, 25), P(34, 34), P(24, 37) }, Cu);
			Poly({ P(38, 36), P(48, 36), P(46, 46), P(37, 45) }, Cu);
			Poly({ P(18, 42), P(26, 41), P(26, 49), P(18, 49) }, OreColor(MaterialType(Id)) * 0.82f);
		}
		break;
	}
	case EItemType::Diamond:
	case EItemType::Onyx:
	case EItemType::Emerald:
	{
		// a cut gem in its own colour (diamond icy blue, onyx glossy black, emerald green)
		const FLinearColor G = OreColor(MaterialType(Id));
		auto Sh = [&](float K) { return FLinearColor(FMath::Min(G.R * K + (K > 1.f ? 0.12f : 0.f), 1.f), FMath::Min(G.G * K + (K > 1.f ? 0.12f : 0.f), 1.f), FMath::Min(G.B * K + (K > 1.f ? 0.12f : 0.f), 1.f)); };
		Poly({ P(20, 12), P(44, 12), P(56, 28), P(32, 58), P(8, 28) }, Sh(0.78f));
		Poly({ P(8, 28), P(20, 12), P(32, 28) }, Sh(1.35f));
		Poly({ P(56, 28), P(44, 12), P(32, 28) }, Sh(1.15f));
		Poly({ P(20, 12), P(44, 12), P(32, 28) }, Sh(1.6f));
		Poly({ P(8, 28), P(32, 58), P(32, 28) }, Sh(0.5f));
		break;
	}
	case EItemType::Logs:
		Quad(6, 22, 48, 46, FLinearColor(0.50f, 0.30f, 0.14f));
		Quad(6, 28, 48, 30, FLinearColor(0.38f, 0.22f, 0.10f));
		Quad(6, 38, 48, 40, FLinearColor(0.38f, 0.22f, 0.10f));
		DiscP(48, 34, 8, 12, FLinearColor(0.82f, 0.62f, 0.36f));
		DiscP(48, 34, 5, 8, FLinearColor(0.62f, 0.42f, 0.22f));
		DiscP(48, 34, 2, 3, FLinearColor(0.82f, 0.62f, 0.36f));
		break;
	case EItemType::PineLogs:
		Quad(6, 26, 48, 50, FLinearColor(0.36f, 0.25f, 0.14f));
		Quad(6, 32, 48, 34, FLinearColor(0.26f, 0.18f, 0.09f));
		DiscP(48, 38, 8, 12, FLinearColor(0.85f, 0.70f, 0.45f));
		DiscP(48, 38, 4, 7, FLinearColor(0.70f, 0.52f, 0.30f));
		Poly({ P(26, 6), P(15, 26), P(37, 26) }, FLinearColor(0.12f, 0.45f, 0.18f));
		Poly({ P(26, 14), P(18, 28), P(34, 28) }, FLinearColor(0.16f, 0.55f, 0.22f));
		break;
	case EItemType::RedwoodLogs:
		Quad(4, 8, 44, 30, FLinearColor(0.52f, 0.18f, 0.09f));
		DiscP(44, 19, 8, 11, FLinearColor(0.85f, 0.45f, 0.26f));
		DiscP(44, 19, 4, 6, FLinearColor(0.65f, 0.28f, 0.14f));
		Quad(14, 34, 54, 58, FLinearColor(0.58f, 0.21f, 0.10f));
		Quad(14, 42, 54, 44, FLinearColor(0.40f, 0.13f, 0.06f));
		DiscP(54, 46, 7, 12, FLinearColor(0.88f, 0.48f, 0.28f));
		DiscP(54, 46, 4, 7, FLinearColor(0.68f, 0.30f, 0.15f));
		break;
	case EItemType::Bacon:
		Poly({ P(6, 40), P(40, 12), P(58, 20), P(24, 52) }, FLinearColor(0.88f, 0.40f, 0.36f));
		Poly({ P(14, 41), P(40, 20), P(46, 22), P(20, 46) }, FLinearColor(0.97f, 0.86f, 0.76f));
		Poly({ P(24, 49), P(46, 30), P(50, 32), P(28, 52) }, FLinearColor(0.97f, 0.86f, 0.76f));
		break;
		case EItemType::Hide:
		Poly({ P(8, 16), P(30, 8), P(52, 16), P(58, 34), P(50, 52), P(30, 58), P(10, 50), P(4, 32) }, FLinearColor(0.42f, 0.27f, 0.14f));
		Poly({ P(16, 24), P(30, 18), P(46, 24), P(50, 36), P(44, 46), P(30, 50), P(16, 44), P(12, 34) }, FLinearColor(0.52f, 0.35f, 0.19f));
		break;
	case EItemType::Bones:
		Poly({ P(12, 44), P(46, 14), P(52, 20), P(18, 50) }, FLinearColor(0.93f, 0.90f, 0.80f));
		DiscP(12, 48, 7, 7, FLinearColor(0.93f, 0.90f, 0.80f));
		DiscP(20, 54, 7, 7, FLinearColor(0.93f, 0.90f, 0.80f));
		DiscP(48, 14, 7, 7, FLinearColor(0.93f, 0.90f, 0.80f));
		DiscP(56, 22, 7, 7, FLinearColor(0.93f, 0.90f, 0.80f));
		break;
	case EItemType::Perch:
	case EItemType::Trout:
	case EItemType::GoldenCarp:
	{
		const EItemType T = MaterialType(Id);
		FLinearColor Body(0.35f, 0.55f, 0.50f), Belly(0.80f, 0.85f, 0.70f), Fin(0.75f, 0.35f, 0.15f);
		if (T == EItemType::Trout) { Body = FLinearColor(0.62f, 0.45f, 0.40f); Belly = FLinearColor(0.95f, 0.78f, 0.72f); Fin = FLinearColor(0.50f, 0.30f, 0.25f); }
		if (T == EItemType::GoldenCarp) { Body = FLinearColor(1.f, 0.78f, 0.12f); Belly = FLinearColor(1.f, 0.95f, 0.55f); Fin = FLinearColor(1.f, 0.55f, 0.1f); }
		Poly({ P(46, 32), P(62, 18), P(60, 32), P(62, 46) }, Fin);                     // tail
		DiscP(28, 32, 24, 13, Body);
		DiscP(28, 38, 20, 7, Belly);
		Poly({ P(18, 20), P(30, 8), P(40, 22) }, Fin);                                 // back fin
		DiscP(13, 29, 3.2f, 3.2f, FLinearColor(0.02f, 0.02f, 0.02f));                  // eye
		if (T == EItemType::Perch) { for (int32 b = 0; b < 3; ++b) Quad(22.f + b * 8.f, 22, 24.f + b * 8.f, 40, FLinearColor(0.15f, 0.30f, 0.28f)); }
		if (T == EItemType::GoldenCarp) DiscP(24, 30, 3, 3, FLinearColor(1.f, 1.f, 0.8f));
		break;
	}
	}
}


