#include "KnightHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "TextureResource.h"
#include "HAL/IConsoleManager.h"

// ===================================================================================================
// The menu look (owner 2026-11-18: "This liquid glass menu we made looks outdated ... make it fit the game better").
//
// A small fantasy UI kit drawn with the canvas only (no assets to cook): dark walnut panels with a faint painted grain and a soft
// darker edge, framed by a thin dark-steel line, a brass band (lit along the top, darker along the bottom) and small brass corner
// fittings with a rivet; buttons are raised wooden plates with a brass edge (brighter on hover, an oxblood plate with a lit edge
// when active, flat and dull when disabled); slots, list rows and text boxes are inset wells (a brighter edge on hover, a lit brass
// edge and a warm glow when picked); bars are dark wells in a brass rim with a matte fill. The two small textures (the grain and
// the corner fitting) are made here once, at first use, from code (EnsureKitTex).
//
// jeeb.UI.Style: 0 the old liquid glass, 1 walnut and brass (default), 2 midnight blue and gold (the same kit, another palette).
// The layout, words and behaviour of every screen are unchanged: only Panel, Tile, Gauge, Btn, CloseBtn (KnightHUDGlass.cpp)
// branch here, plus the tick boxes, sliders and the map's frame.
// ===================================================================================================

static TAutoConsoleVariable<int32> CVarUIStyle(TEXT("jeeb.UI.Style"), 1,
	TEXT("The menus' look: 0 the old liquid glass, 1 walnut and brass (default), 2 midnight blue and gold."));

int32 AKnightHUD::UIStyle()
{
	return FMath::Clamp(CVarUIStyle.GetValueOnGameThread(), 0, 2);
}

namespace
{
	// A colour given as 0-255 sRGB (as a painter would pick it), turned into the linear colour the canvas takes
	FLinearColor SC(uint8 R, uint8 G, uint8 B, float A = 1.f)
	{
		FLinearColor C(FColor(R, G, B, 255));
		C.A = A;
		return C;
	}
	FLinearColor WithA(const FLinearColor& C, float A) { return FLinearColor(C.R, C.G, C.B, A); }

	struct FKit
	{
		FLinearColor Edge, TrimTop, TrimSide, TrimBot, Inner, BodyTop, BodyBot;
		float Grain;
		FLinearColor PlateTop, PlateBot, HotTop, HotBot, OnTop, OnBot, OffTop, OffBot;
		FLinearColor PlateEdge, HotEdge, OnEdge, OffEdge;
		FLinearColor TextIdle, TextHot, TextOn, TextOff;
		FLinearColor WellTop, WellBot, WellEdge, WellHot, WellOn, Glow;
		FLinearColor Fitting, Fog, GaugeWell;
	};

	FKit MakeWalnut()
	{
		FKit K;
		K.Edge = SC(16, 10, 6);
		K.TrimTop = SC(222, 186, 116); K.TrimSide = SC(166, 128, 70); K.TrimBot = SC(104, 76, 40);
		K.Inner = SC(10, 6, 3, 0.85f);
		K.BodyTop = SC(64, 45, 31); K.BodyBot = SC(37, 25, 16);
		K.Grain = 0.32f;
		K.PlateTop = SC(88, 63, 43); K.PlateBot = SC(54, 37, 24);
		K.HotTop = SC(112, 81, 54); K.HotBot = SC(68, 47, 30);
		K.OnTop = SC(128, 44, 30); K.OnBot = SC(80, 24, 16);              // oxblood: the one accent
		K.OffTop = SC(50, 42, 35); K.OffBot = SC(40, 33, 27);
		K.PlateEdge = SC(132, 101, 58); K.HotEdge = SC(226, 190, 116); K.OnEdge = SC(244, 210, 132); K.OffEdge = SC(74, 63, 50);
		K.TextIdle = SC(238, 226, 200); K.TextHot = SC(255, 248, 228); K.TextOn = SC(255, 222, 142); K.TextOff = SC(138, 126, 112);
		K.WellTop = SC(20, 13, 8); K.WellBot = SC(34, 24, 15);
		K.WellEdge = SC(98, 76, 46); K.WellHot = SC(196, 158, 94); K.WellOn = SC(244, 210, 132); K.Glow = SC(255, 186, 84);
		K.Fitting = SC(214, 174, 104);
		K.Fog = SC(236, 220, 186);
		K.GaugeWell = SC(18, 12, 7);
		return K;
	}

	FKit MakeMidnight()
	{
		FKit K;
		K.Edge = SC(6, 8, 14);
		K.TrimTop = SC(228, 198, 124); K.TrimSide = SC(170, 140, 78); K.TrimBot = SC(104, 84, 46);
		K.Inner = SC(3, 4, 9, 0.85f);
		K.BodyTop = SC(36, 46, 68); K.BodyBot = SC(19, 25, 40);
		K.Grain = 0.22f;
		K.PlateTop = SC(50, 63, 90); K.PlateBot = SC(30, 38, 57);
		K.HotTop = SC(66, 82, 116); K.HotBot = SC(38, 48, 72);
		K.OnTop = SC(30, 98, 102); K.OnBot = SC(16, 60, 64);              // deep teal: the one accent
		K.OffTop = SC(36, 40, 50); K.OffBot = SC(29, 32, 41);
		K.PlateEdge = SC(140, 116, 66); K.HotEdge = SC(232, 200, 126); K.OnEdge = SC(246, 214, 140); K.OffEdge = SC(64, 68, 80);
		K.TextIdle = SC(226, 230, 240); K.TextHot = SC(255, 255, 255); K.TextOn = SC(255, 224, 150); K.TextOff = SC(122, 128, 142);
		K.WellTop = SC(11, 14, 23); K.WellBot = SC(22, 28, 42);
		K.WellEdge = SC(74, 86, 112); K.WellHot = SC(196, 168, 104); K.WellOn = SC(246, 214, 140); K.Glow = SC(255, 204, 112);
		K.Fitting = SC(220, 186, 112);
		K.Fog = SC(206, 214, 228);
		K.GaugeWell = SC(9, 11, 18);
		return K;
	}

	const FKit& Kit()
	{
		static const FKit Walnut = MakeWalnut(), Midnight = MakeMidnight();
		return FMath::Clamp(CVarUIStyle.GetValueOnGameThread(), 0, 2) == 2 ? Midnight : Walnut;
	}

	// Small helpers for the made textures
	float Hash2(int32 X, int32 Y) { uint32 H = uint32(X) * 374761393u + uint32(Y) * 668265263u; H = (H ^ (H >> 13)) * 1274126177u; return float(H & 0xFFFF) / 65535.f; }
}

// ---------------------------------------------------------------------------------------------------
// The two made textures: the painted grain (tiles both ways; streaks run left to right like boards) and a brass corner fitting
// (an L-shaped plate along the top and left edges with a rivet; grey, shaded, tinted when drawn and flipped into each corner)
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::EnsureKitTex()
{
	if (GrainTex && FittingTex) return;
	if (!GrainTex)
	{
		const int32 W = 256, H = 128;
		GrainTex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (GrainTex)
		{
			GrainTex->SRGB = false;
			GrainTex->Filter = TF_Bilinear;
			GrainTex->AddressX = TA_Wrap;
			GrainTex->AddressY = TA_Wrap;
			FColor* Px = static_cast<FColor*>(GrainTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
			const float Tau = 2.f * PI;
			for (int32 y = 0; y < H; ++y)
				for (int32 x = 0; x < W; ++x)
				{
					const float U = float(x) / W, V = float(y) / H;
					// long streaks running left to right (whole numbers of waves across and down, so it tiles): a few broad soft bands
					// that wander a little, and fine lines, each row its own shade, fading in and out along their length like brushed paint
					const float Warp = 0.10f * FMath::Sin(Tau * (U * 1.f + 0.13f)) + 0.04f * FMath::Sin(Tau * (U * 3.f + V * 1.f + 0.4f));
					const float Bands = 0.5f + 0.5f * FMath::Sin(Tau * (V * 5.f + Warp));
					const int32 Row = ((y + FMath::RoundToInt(Warp * H * 0.6f)) % H + H) % H;
					const float RowN = (Hash2(0, (Row + H - 1) % H) + 2.f * Hash2(0, Row) + Hash2(0, (Row + 1) % H)) * 0.25f;
					const float Along = 0.5f + 0.5f * FMath::Sin(Tau * (U * 2.f + Hash2(1, Row) * 4.f));
					const float Fleck = Hash2(x, y);
					const float A = 0.22f * Bands * Bands + 0.55f * RowN * RowN * (0.35f + 0.65f * Along) + 0.06f * Fleck;
					Px[y * W + x] = FColor(0, 0, 0, uint8(FMath::Clamp(A, 0.f, 1.f) * 255.f));
				}
			GrainTex->GetPlatformData()->Mips[0].BulkData.Unlock();
			GrainTex->UpdateResource();
		}
	}
	if (!FittingTex)
	{
		const int32 N = 64;
		FittingTex = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
		if (FittingTex)
		{
			FittingTex->SRGB = false;
			FittingTex->Filter = TF_Bilinear;
			FittingTex->AddressX = TA_Clamp;
			FittingTex->AddressY = TA_Clamp;
			FColor* Px = static_cast<FColor*>(FittingTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
			// In units of the texture (0..64): arms 13 wide along the top and left, 64 long with a pointed end; a square boss 0..26 at the
			// corner with its inner corner cut; a domed rivet at (12, 12)
			auto Inside = [](float X, float Y, float& Shade) -> bool
			{
				const float Arm = 13.f;
				const float RX = X - 12.f, RY = Y - 12.f;
				if (RX * RX + RY * RY < 6.2f * 6.2f)   // the rivet: lit from the upper left
				{
					Shade = FMath::Clamp(1.05f - 0.06f * (RX + RY) - 0.012f * (RX * RX + RY * RY), 0.35f, 1.f);
					return true;
				}
				if (RX * RX + RY * RY < 8.f * 8.f) { Shade = 0.32f; return true; }   // its dark ring
				auto ArmOk = [&](float Along, float Across)   // a pointed end from 46 on
				{
					const float Lim = Along < 46.f ? Arm : Arm * FMath::Max(0.f, (64.f - Along) / 18.f);
					return Across < Lim;
				};
				const bool bBoss = X < 26.f && Y < 26.f && X + Y < 44.f;
				const bool bTop = ArmOk(X, Y), bLeft = ArmOk(Y, X);
				if (!(bBoss || bTop || bLeft)) return false;
				// a bevel: lighter near the outer (top/left) edges, darker on the inner edges
				const float DOut = FMath::Min(X, Y);
				float DIn = 1e9f;
				if (bTop && !bBoss) DIn = FMath::Min(DIn, (X < 46.f ? Arm : Arm * (64.f - X) / 18.f) - Y);
				if (bLeft && !bBoss) DIn = FMath::Min(DIn, (Y < 46.f ? Arm : Arm * (64.f - Y) / 18.f) - X);
				if (bBoss) DIn = FMath::Min(DIn, FMath::Min(26.f - FMath::Max(X, Y), (44.f - X - Y) * 0.7071f));
				Shade = 0.72f;
				if (DOut < 2.5f) Shade = 1.f;
				else if (DIn < 2.5f) Shade = 0.42f;
				return true;
			};
			for (int32 y = 0; y < N; ++y)
				for (int32 x = 0; x < N; ++x)
				{
					float Cov = 0.f, Sh = 0.f;
					for (int32 sy = 0; sy < 4; ++sy)
						for (int32 sx = 0; sx < 4; ++sx)
						{
							float S = 0.f;
							if (Inside(x + (sx + 0.5f) / 4.f, y + (sy + 0.5f) / 4.f, S)) { Cov += 1.f; Sh += S; }
						}
					const float Shade = Cov > 0.f ? Sh / Cov : 0.f;
					const uint8 G = uint8(FMath::Clamp(Shade, 0.f, 1.f) * 255.f);
					Px[y * N + x] = FColor(G, G, G, uint8(Cov / 16.f * 255.f));
				}
			FittingTex->GetPlatformData()->Mips[0].BulkData.Unlock();
			FittingTex->UpdateResource();
		}
	}
}

// A square frame T thick (no overlaps, so a see-through colour stays even), its top, sides and bottom in their own colours
void AKnightHUD::KitFrame(float X, float Y, float W, float H, float T, const FLinearColor& Top, const FLinearColor& Side, const FLinearColor& Bottom)
{
	X = FMath::RoundToFloat(X); Y = FMath::RoundToFloat(Y); W = FMath::RoundToFloat(W); H = FMath::RoundToFloat(H);
	if (W < 2.f * T || H < 2.f * T) return;
	DrawRect(Top, X, Y, W, T);
	DrawRect(Bottom, X, Y + H - T, W, T);
	DrawRect(Side, X, Y + T, T, H - 2.f * T);
	DrawRect(Side, X + W - T, Y + T, T, H - 2.f * T);
}

void AKnightHUD::KitFittings(float X, float Y, float W, float H, float S, const FLinearColor& Col)
{
	EnsureKitTex();
	if (!FittingTex) return;
	S = FMath::RoundToFloat(S);
	X = FMath::RoundToFloat(X); Y = FMath::RoundToFloat(Y); W = FMath::RoundToFloat(W); H = FMath::RoundToFloat(H);
	for (int32 c = 0; c < 4; ++c) Corner(FittingTex, (c & 1) ? X + W - S : X, (c & 2) ? Y + H - S : Y, S, c, Col);
}

void AKnightHUD::KitGrain(float X, float Y, float W, float H, float Alpha)
{
	EnsureKitTex();
	if (!GrainTex || Alpha <= 0.f || W < 2.f || H < 2.f) return;
	// (a different stretch of the grain for each panel, from where it sits; drawn at about 1.4 screen pixels per texel)
	const float U0 = FMath::Frac(X * 0.0071f + Y * 0.0013f), V0 = FMath::Frac(Y * 0.0057f);
	DrawTexture(GrainTex, X, Y, W, H, U0, V0, W / 360.f, H / 180.f, FLinearColor(1.f, 1.f, 1.f, Alpha), BLEND_Translucent);
}

// ---------------------------------------------------------------------------------------------------
// Panels: a window, a tooltip, a bar slot
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::KitPanel(float X, float Y, float W, float H, float Alpha, bool bGlow)
{
	const FKit& K = Kit();
	if (Alpha <= 0.003f) return;
	X = FMath::RoundToFloat(X); Y = FMath::RoundToFloat(Y); W = FMath::RoundToFloat(W); H = FMath::RoundToFloat(H);
	const float Small = FMath::Min(W, H);
	const bool bBig = Small >= 110.f;
	const float Te = bBig ? 2.f : 1.f;                 // the dark outer line
	const float Tt = bBig ? 3.f : 2.f;                 // the brass band
	const float Op = (bGreyGlass ? 0.86f : 0.97f) * Alpha;
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Pulse = bGlow ? 0.5f + 0.5f * FMath::Sin(T * 4.f) : 0.f;

	// a soft shadow under it
	for (int32 k = 1; k <= 3; ++k)
		FillRRx(X - k * 1.5f, Y + 1.f + k * 1.5f, W + k * 3.f, H + k * 2.f, 3.f + k * 2.f, [&](float) { return FLinearColor(0.f, 0.f, 0.f, 0.09f * Alpha); });

	KitFrame(X, Y, W, H, Te, WithA(K.Edge, 0.95f * Alpha), WithA(K.Edge, 0.95f * Alpha), WithA(K.Edge, 0.95f * Alpha));
	FLinearColor TT = K.TrimTop, TS = K.TrimSide, TB = K.TrimBot;
	if (bGlow) { TT = Lift(TT, 1.f + 0.25f * Pulse); TS = Lift(TS, 1.15f + 0.3f * Pulse); TB = Lift(TB, 1.25f + 0.3f * Pulse); }
	KitFrame(X + Te, Y + Te, W - 2.f * Te, H - 2.f * Te, Tt, WithA(TT, Alpha), WithA(TS, Alpha), WithA(TB, Alpha));
	const float I = Te + Tt;
	KitFrame(X + I, Y + I, W - 2.f * I, H - 2.f * I, 1.f, WithA(K.Inner, K.Inner.A * Alpha), WithA(K.Inner, K.Inner.A * Alpha), WithA(K.Inner, K.Inner.A * Alpha));
	const float BX = X + I + 1.f, BY = Y + I + 1.f, BW = W - 2.f * I - 2.f, BH = H - 2.f * I - 2.f;
	if (BW < 1.f || BH < 1.f) return;
	FillRRx(BX, BY, BW, BH, 0.f, [&](float t) { return WithA(K.BodyTop + (K.BodyBot - K.BodyTop) * t, Op); });
	KitGrain(BX, BY, BW, BH, K.Grain * Op);
	// a darker edge inside (depth), and a faint lit line under the top
	const float Depth = FMath::Clamp(Small * 0.12f, 4.f, 22.f);
	for (int32 s = 0; s < 4; ++s)
	{
		const float D = FMath::RoundToFloat(Depth * (4 - s) / 4.f);
		const FLinearColor Sh(0.f, 0.f, 0.f, 0.05f * Op);
		DrawRect(Sh, BX, BY, BW, D);
		DrawRect(Sh, BX, BY + BH - D, BW, D);
		DrawRect(Sh, BX, BY + D, D, BH - 2.f * D);
		DrawRect(Sh, BX + BW - D, BY + D, D, BH - 2.f * D);
	}
	DrawRect(FLinearColor(1.f, 0.92f, 0.75f, 0.07f * Op), BX, BY, BW, 1.f);
	if (bGlow) FillRRx(BX, BY, BW, BH, 0.f, [&](float t) { return WithA(K.Glow, (0.05f + 0.07f * Pulse) * (0.3f + t) * Alpha); });
	// the brass corner pieces
	if (Small >= 44.f)
	{
		const float FS = FMath::Clamp(Small * 0.11f, 12.f, 30.f);
		KitFittings(X - 1.f, Y - 1.f, W + 2.f, H + 2.f, FS, WithA(bGlow ? Lift(K.Fitting, 1.15f) : K.Fitting, Alpha));
	}
}

// ---------------------------------------------------------------------------------------------------
// Tiles: slots, list rows, text boxes and label backings. The old glass tiles said what they were by their tint (a cool blue for a
// plain cell, a brighter blue under the mouse, gold when picked, near-black for a label's backing) and how strong they were; the
// tint is read the same way here so no screen needed changing.
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::KitTile(float X, float Y, float W, float H, const FLinearColor& Tint, float Alpha, bool bRim)
{
	const FKit& K = Kit();
	if (Alpha <= 0.003f) return;
	X = FMath::RoundToFloat(X); Y = FMath::RoundToFloat(Y); W = FMath::RoundToFloat(W); H = FMath::RoundToFloat(H);
	const float Lum = 0.3f * Tint.R + 0.59f * Tint.G + 0.11f * Tint.B;
	const float Warm = Tint.R - Tint.B;
	const float R = FMath::Min(W, H) >= 20.f ? 3.f : 2.f;
	if (Lum < 0.25f)   // a dark backing (a label, a text panel)
	{
		const float Op = FMath::Clamp(Alpha * 1.7f, 0.f, 0.94f);
		FillRRx(X, Y, W, H, R, [&](float t) { return WithA((K.WellTop + (K.WellBot - K.WellTop) * t) * 0.9f, Op); });
		if (bRim) KitFrame(X, Y, W, H, 1.f, WithA(K.WellEdge, 0.8f * Op), WithA(K.WellEdge, 0.7f * Op), WithA(K.WellEdge, 0.55f * Op));
		return;
	}
	const bool bOn = Warm > 0.25f && Tint.R > 0.85f;
	const bool bHot = !bOn && Lum > 0.84f;
	float Op = FMath::Clamp(Alpha * 3.2f, 0.f, 1.f);
	if (bOn) Op = FMath::Max(Op, 0.92f);
	// the well: darker at the top (it is sunk into the panel). A strongly coloured tint (the gamble row's violet) keeps a little of its colour.
	const float Sat = Tint.GetMax() - Tint.GetMin();
	const bool bColoured = !bOn && Sat > 0.3f && Warm < 0.f && Tint.R > Tint.G;
	FillRRx(X, Y, W, H, R, [&](float t)
	{
		FLinearColor C = K.WellTop + (K.WellBot - K.WellTop) * t;
		if (bColoured) C = C + (Tint * 0.22f - C) * 0.35f;
		return WithA(C, Op * 0.92f);
	});
	if (W > 6.f && H > 6.f)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.32f * Op), X + 1.f, Y + 1.f, W - 2.f, 2.f);                 // the shadow under its top edge
		DrawRect(FLinearColor(1.f, 0.9f, 0.7f, 0.06f * Op), X + 1.f, Y + H - 2.f, W - 2.f, 1.f);          // a faint light along its bottom
	}
	if (bOn)
	{
		FillRRx(X + 2.f, Y + 2.f, W - 4.f, H - 4.f, R, [&](float t) { return WithA(K.Glow, (0.08f + 0.12f * t) * Op); });
		KitFrame(X, Y, W, H, 2.f, WithA(K.WellOn, Op), WithA(K.WellOn, 0.95f * Op), WithA(Lift(K.WellOn, 0.8f), 0.95f * Op));
		return;
	}
	if (bHot) FillRRx(X + 1.f, Y + 1.f, W - 2.f, H - 2.f, R, [&](float) { return FLinearColor(1.f, 0.92f, 0.8f, 0.05f * Op); });
	const FLinearColor E = bHot ? K.WellHot : K.WellEdge;
	const float EA = (bRim ? 1.f : 0.65f) * Op;
	KitFrame(X, Y, W, H, 1.f, WithA(Lift(E, 0.75f), EA), WithA(E, EA), WithA(Lift(E, 1.2f), EA));
}

// ---------------------------------------------------------------------------------------------------
// Bars: a dark well in a thin brass rim, a matte fill with a lit top line
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::KitGauge(float X, float Y, float W, float H, float F, const FLinearColor& Fill, bool bPulse)
{
	const FKit& K = Kit();
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	F = FMath::Clamp(F, 0.f, 1.f);
	X = FMath::RoundToFloat(X); Y = FMath::RoundToFloat(Y); W = FMath::RoundToFloat(W); H = FMath::RoundToFloat(H);
	FillRRx(X - 3.f, Y - 1.f, W + 6.f, H + 5.f, 3.f, [&](float) { return FLinearColor(0.f, 0.f, 0.f, 0.22f); });   // shadow
	KitFrame(X - 2.f, Y - 2.f, W + 4.f, H + 4.f, 1.f, WithA(K.Edge, 0.95f), WithA(K.Edge, 0.95f), WithA(K.Edge, 0.95f));
	KitFrame(X - 1.f, Y - 1.f, W + 2.f, H + 2.f, 1.f, K.TrimTop, K.TrimSide, K.TrimBot);
	DrawRect(WithA(K.GaugeWell, 0.92f), X, Y, W, H);
	const float Glow = bPulse ? 0.75f + 0.25f * FMath::Sin(T * 7.f) : 1.f;
	const float FW = FMath::RoundToFloat(W * F);
	if (FW >= 1.f)
	{
		FillRRx(X, Y, FW, H, 0.f, [&](float t)
		{
			const float L = FMath::Lerp(1.05f, 0.62f, t) * Glow;
			return FLinearColor(FMath::Min(Fill.R * L, 1.f), FMath::Min(Fill.G * L, 1.f), FMath::Min(Fill.B * L, 1.f), 0.97f);
		});
		if (H >= 5.f) DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.30f), X, Y, FW, 1.f);                   // the lit top line
		if (FW > 2.f && F < 0.999f) DrawRect(FLinearColor(1.f, 0.96f, 0.85f, 0.45f), X + FW - 1.f, Y, 1.f, H);   // its leading edge
	}
	if (W > 60.f) for (int32 k = 1; k < 4; ++k) DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), FMath::RoundToFloat(X + W * 0.25f * k), Y, 1.f, H);   // quarter marks
}

// ---------------------------------------------------------------------------------------------------
// Buttons (and tabs: an active tab is a lit button)
// ---------------------------------------------------------------------------------------------------
bool AKnightHUD::KitBtn(float X, float Y, float W, float H, const FString& Label, bool bEnabled, bool bActive, float Scale)
{
	const FKit& K = Kit();
	UFont* Font = GEngine->GetMediumFont();
	const bool bOver = bEnabled && Over(X, Y, W, H);
	const bool bDown = bOver && bUIWasDown;
	X = FMath::RoundToFloat(X); Y = FMath::RoundToFloat(Y); W = FMath::RoundToFloat(W); H = FMath::RoundToFloat(H);
	FLinearColor Top = K.PlateTop, Bot = K.PlateBot, Edge = K.PlateEdge, Txt = K.TextIdle;
	if (!bEnabled) { Top = K.OffTop; Bot = K.OffBot; Edge = K.OffEdge; Txt = K.TextOff; }
	else if (bActive) { Top = K.OnTop; Bot = K.OnBot; Edge = K.OnEdge; Txt = K.TextOn; if (bOver) { Top = Lift(Top, 1.12f); Bot = Lift(Bot, 1.12f); } }
	else if (bOver) { Top = K.HotTop; Bot = K.HotBot; Edge = K.HotEdge; Txt = K.TextHot; }
	const float Off = bDown ? 1.f : 0.f;   // pressed: it sinks a pixel
	// its drop shadow, the dark outline, the brass edge, the plate
	if (bEnabled && !bDown) DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), X + 1.f, Y + H, W, 2.f);
	KitFrame(X, Y + Off, W, H, 1.f, WithA(K.Edge, 0.9f), WithA(K.Edge, 0.9f), WithA(K.Edge, 0.9f));
	const float Et = bActive && bEnabled ? 2.f : 1.f;
	KitFrame(X + 1.f, Y + 1.f + Off, W - 2.f, H - 2.f, Et, Lift(Edge, 1.1f), Edge, Lift(Edge, 0.7f));
	const float PX = X + 1.f + Et, PY = Y + 1.f + Et + Off, PW = W - 2.f - 2.f * Et, PH = H - 2.f - 2.f * Et;
	FillRRx(PX, PY, PW, PH, 0.f, [&](float t) { return bDown ? Bot + (Top - Bot) * t : Top + (Bot - Top) * t; });
	if (bEnabled && PW > 6.f && PH > 6.f) KitGrain(PX, PY, PW, PH, Kit().Grain * 0.6f);
	if (PH > 4.f && !bDown) DrawRect(FLinearColor(1.f, 0.95f, 0.85f, bEnabled ? 0.14f : 0.05f), PX, PY, PW, 1.f);   // a lit bevel along the top
	if (PH > 4.f) DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.25f), PX, PY + PH - 1.f, PW, 1.f);                         // a dark one along the bottom
	if (bActive && bEnabled && PW > 30.f)   // a small lit diamond each side of an active button's word (a tab reads as chosen)
	{
		float TW0 = 0.f, TH0 = 0.f;
		HDTextSize(Label, TW0, TH0, Font, Scale);
		const float Cy = Y + H * 0.5f + Off, Gap = FMath::Min(14.f, (PW - TW0) * 0.25f);
		if (Gap >= 7.f)
			for (int32 s = -1; s <= 1; s += 2)
			{
				const float Cx = X + W * 0.5f + s * (TW0 * 0.5f + Gap * 0.6f);
				Poly({ FVector2D(Cx, Cy - 3.5f), FVector2D(Cx + 3.5f, Cy), FVector2D(Cx, Cy + 3.5f), FVector2D(Cx - 3.5f, Cy) }, K.OnEdge);
			}
	}
	float TW = 0.f, TH = 0.f;
	HDTextSize(Label, TW, TH, Font, Scale);
	const float TX = FMath::RoundToFloat(X + (W - TW) * 0.5f), TY = FMath::RoundToFloat(Y + (H - TH) * 0.5f + Off);
	HDText(Label, FLinearColor(0.f, 0.f, 0.f, bEnabled ? 0.6f : 0.3f), TX + 1.f, TY + 1.f, Font, Scale);
	HDText(Label, Txt, TX, TY, Font, Scale);
	return bOver && bUIClick;
}

bool AKnightHUD::KitCloseBtn(float BX, float BY, float S)
{
	const FKit& K = Kit();
	const bool bOver = Over(BX, BY, S, S);
	BX = FMath::RoundToFloat(BX); BY = FMath::RoundToFloat(BY);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), BX + 1.f, BY + S, S, 2.f);
	KitFrame(BX, BY, S, S, 1.f, WithA(K.Edge, 0.9f), WithA(K.Edge, 0.9f), WithA(K.Edge, 0.9f));
	const FLinearColor Edge = bOver ? K.OnEdge : K.PlateEdge;
	KitFrame(BX + 1.f, BY + 1.f, S - 2.f, S - 2.f, 1.f, Lift(Edge, 1.1f), Edge, Lift(Edge, 0.7f));
	const FLinearColor Top = bOver ? K.OnTop : K.PlateTop, Bot = bOver ? K.OnBot : K.PlateBot;
	FillRRx(BX + 2.f, BY + 2.f, S - 4.f, S - 4.f, 0.f, [&](float t) { return Top + (Bot - Top) * t; });
	// the cross: two bars
	const FLinearColor C = bOver ? K.TextHot : K.TextIdle;
	const float Cx = BX + S * 0.5f, Cy = BY + S * 0.5f, L = S * 0.25f, Wd = FMath::Max(1.2f, S * 0.065f);
	Poly({ FVector2D(Cx - L - Wd, Cy - L + Wd), FVector2D(Cx - L + Wd, Cy - L - Wd), FVector2D(Cx + L + Wd, Cy + L - Wd), FVector2D(Cx + L - Wd, Cy + L + Wd) }, C);
	Poly({ FVector2D(Cx + L - Wd, Cy - L - Wd), FVector2D(Cx + L + Wd, Cy - L + Wd), FVector2D(Cx - L + Wd, Cy + L + Wd), FVector2D(Cx - L - Wd, Cy + L - Wd) }, C);
	return bOver && bUIClick;
}

// ---------------------------------------------------------------------------------------------------
// Tick boxes and sliders (the settings and LOOK tabs)
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::CheckBox(float X, float Y, float S, bool bOn)
{
	if (UIStyle() == 0)
	{
		DrawRect(FLinearColor::White, X, Y, S, S);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 1.f), X + 2.f, Y + 2.f, S - 4.f, S - 4.f);
		if (bOn) DrawRect(FLinearColor(0.9f, 0.55f, 0.15f), X + 5.f, Y + 5.f, S - 10.f, S - 10.f);
		return;
	}
	const FKit& K = Kit();
	KitFrame(X, Y, S, S, 1.f, WithA(K.Edge, 0.95f), WithA(K.Edge, 0.95f), WithA(K.Edge, 0.95f));
	KitFrame(X + 1.f, Y + 1.f, S - 2.f, S - 2.f, 1.f, K.TrimBot, K.TrimSide, K.TrimTop);   // (lit along the bottom: sunk in)
	FillRRx(X + 2.f, Y + 2.f, S - 4.f, S - 4.f, 0.f, [&](float t) { return K.WellTop + (K.WellBot - K.WellTop) * t; });
	if (bOn)   // a brass tick
	{
		const FLinearColor C = K.OnEdge;
		const float U = S / 26.f;
		Poly({ FVector2D(X + 5.f * U, Y + 13.f * U), FVector2D(X + 8.f * U, Y + 10.f * U), FVector2D(X + 11.5f * U, Y + 14.f * U), FVector2D(X + 11.5f * U, Y + 20.f * U) }, C);
		Poly({ FVector2D(X + 11.5f * U, Y + 20.f * U), FVector2D(X + 11.5f * U, Y + 14.f * U), FVector2D(X + 19.f * U, Y + 5.f * U), FVector2D(X + 22.f * U, Y + 8.f * U) }, C);
	}
}

void AKnightHUD::SliderBar(float SX, float SY, float SW, float T, const FLinearColor& Fill)
{
	T = FMath::Clamp(T, 0.f, 1.f);
	if (UIStyle() == 0)
	{
		DrawRect(FLinearColor(0.25f, 0.25f, 0.25f, 1.f), SX, SY + 4.f, SW, 8.f);
		DrawRect(Fill, SX, SY + 4.f, SW * T, 8.f);
		DrawRect(FLinearColor::White, SX + SW * T - 8.f, SY - 6.f, 16.f, 28.f);
		return;
	}
	const FKit& K = Kit();
	KitGauge(SX, SY + 4.f, SW, 8.f, T, Fill, false);
	// the knob: a small brass-edged plate with a groove down its middle
	const float KX = FMath::RoundToFloat(SX + SW * T - 7.f), KY = FMath::RoundToFloat(SY - 5.f), KW = 14.f, KH = 26.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), KX + 1.f, KY + KH, KW, 2.f);
	KitFrame(KX, KY, KW, KH, 1.f, WithA(K.Edge, 0.95f), WithA(K.Edge, 0.95f), WithA(K.Edge, 0.95f));
	KitFrame(KX + 1.f, KY + 1.f, KW - 2.f, KH - 2.f, 1.f, K.TrimTop, K.TrimSide, K.TrimBot);
	FillRRx(KX + 2.f, KY + 2.f, KW - 4.f, KH - 4.f, 0.f, [&](float t) { return K.HotTop + (K.HotBot - K.HotTop) * t; });
	DrawRect(WithA(K.TrimTop, 0.9f), KX + KW * 0.5f - 0.5f, KY + 6.f, 1.f, KH - 12.f);
}

// ---------------------------------------------------------------------------------------------------
// The world map's frame: framed like a chart (a dark wooden border with a brass inner line and corner pieces), and its fog is
// tinted like old paper
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::MapFrame(float X, float Y, float S)
{
	if (UIStyle() == 0) { Tile(X - 6.f, Y - 6.f, S + 12.f, S + 12.f, FLinearColor(0.8f, 0.7f, 0.5f), 0.3f); return; }
	const FKit& K = Kit();
	const float B = 9.f;   // the wooden border
	const float OX = FMath::RoundToFloat(X - B - 3.f), OY = FMath::RoundToFloat(Y - B - 3.f), OS = FMath::RoundToFloat(S + 2.f * (B + 3.f));
	KitFrame(OX, OY, OS, OS, 1.f, WithA(K.Edge, 0.95f), WithA(K.Edge, 0.95f), WithA(K.Edge, 0.95f));
	FillRRx(OX + 1.f, OY + 1.f, OS - 2.f, OS - 2.f, 0.f, [&](float t) { return Lift(K.PlateTop + (K.PlateBot - K.PlateTop) * t, 0.85f); });
	KitGrain(OX + 1.f, OY + 1.f, OS - 2.f, OS - 2.f, K.Grain * 1.2f);
	DrawRect(FLinearColor(1.f, 0.92f, 0.75f, 0.10f), OX + 1.f, OY + 1.f, OS - 2.f, 1.f);
	// the brass line round the paper, then a dark hairline
	KitFrame(X - 3.f, Y - 3.f, S + 6.f, S + 6.f, 2.f, K.TrimBot, K.TrimSide, K.TrimTop);
	KitFrame(X - 1.f, Y - 1.f, S + 2.f, S + 2.f, 1.f, WithA(K.Edge, 0.9f), WithA(K.Edge, 0.9f), WithA(K.Edge, 0.9f));
	KitFittings(OX - 1.f, OY - 1.f, OS + 2.f, OS + 2.f, 24.f, K.Fitting);
}

FLinearColor AKnightHUD::MapFogTint() const
{
	return UIStyle() == 0 ? FLinearColor::White : Kit().Fog;
}

// A tooltip line's colour: the first line (the name) gold, a class line (\x02 and the class number first) in the class's colour,
// the rest a soft white. OutText is the line without its marker.
FLinearColor AKnightHUD::TipLineColor(const FString& Line, bool bFirst, FString& OutText) const
{
	if (Line.Len() > 1 && Line[0] == TCHAR(2))
	{
		OutText = Line.Mid(2);
		const FLinearColor C = ClassColor(Line[1] - TCHAR('0'));
		if (Line[1] == TCHAR('1')) return SC(140, 196, 255);   // (knight: a clear steel blue, so it stands apart from the soft white lines; the icon outline stays grey)
		return Lift(C, 1.25f);
	}
	OutText = Line;
	if (bFirst) return FLinearColor(1.f, 0.9f, 0.5f);
	return UIStyle() == 0 ? FLinearColor(0.88f, 0.88f, 0.88f) : SC(232, 222, 202);
}
