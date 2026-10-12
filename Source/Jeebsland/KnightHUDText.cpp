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

// ---------------------------------------------------------------------------------------------------
// Sharp text (see KnightHUD.h)
// ---------------------------------------------------------------------------------------------------
UFont* AKnightHUD::PickFont(UFont* Font, float& Scale) const
{
	if (!Font || !HDFonts[3] || Font != HDFonts[0]) return Font;
	const int32 K = FMath::Clamp(FMath::CeilToInt(Scale - 0.05f), 1, 4);   // the smallest raster at least as big as needed
	Scale /= float(K);
	return HDFonts[K - 1];
}

void AKnightHUD::HDText(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale)
{
	float S = Scale;
	UFont* F = PickFont(Font, S);
	DrawText(Text, Color, X, Y, F, S);
}

void AKnightHUD::HDTextSize(const FString& Text, float& OutW, float& OutH, UFont* Font, float Scale) const
{
	float S = Scale;
	UFont* F = PickFont(Font, S);
	GetTextSize(Text, OutW, OutH, F, S);
}



// A tooltip-style panel centred above a point on screen (the description over a dropped thing)
void AKnightHUD::DrawLinesAt(const TArray<FString>& Lines, float CX, float BottomY, float Alpha)
{
	if (Lines.Num() == 0 || Alpha <= 0.01f) return;
	UFont* Font = GEngine->GetMediumFont();
	float W = 0.f, H = 0.f;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		float TW = 0.f, TH = 0.f;
		HDTextSize(Lines[i], TW, TH, Font, i == 0 ? 1.15f : 0.95f);
		W = FMath::Max(W, TW); H += TH + 3.f;
	}
	const float X = FMath::Clamp(CX - W * 0.5f, 12.f, Canvas->ClipX - W - 28.f), Y = FMath::Clamp(BottomY - H, 12.f, Canvas->ClipY - H - 20.f);
	Panel(X - 8.f, Y - 8.f, W + 24.f, H + 16.f, Alpha);
	float Cy = Y;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const float Sc = i == 0 ? 1.15f : 0.95f;
		float TW = 0.f, TH = 0.f;
		HDTextSize(Lines[i], TW, TH, Font, Sc);
		FLinearColor C = i == 0 ? FLinearColor(1.f, 0.9f, 0.5f) : FLinearColor(0.88f, 0.88f, 0.88f);
		C.A = Alpha;
		HDText(Lines[i], C, X + 2.f, Cy, Font, Sc);
		Cy += TH + 3.f;
	}
}

// Place names in a Norse hand: every letter built from straight, angular strokes with chisel-cut ends, like carved runes,
// in gold with a dark bronze edge, a soft shadow and a pale sheen. Centred across the screen, its top at Y, H tall.
void AKnightHUD::NorseText(const FString& Text, float Y, float H, float Alpha)
{
	if (Alpha <= 0.01f) return;
	struct FSeg { float X0, Y0, X1, Y1; };
	auto Line = [](TArray<FSeg>& G, std::initializer_list<FVector2f> Pts) { const FVector2f* P = Pts.begin(); for (size_t i = 1; i < Pts.size(); ++i) G.Add({ P[i - 1].X, P[i - 1].Y, P[i].X, P[i].Y }); };
	auto Glyph = [&](TCHAR Ch) -> TArray<FSeg>
	{
		TArray<FSeg> G;
		switch (FChar::ToUpper(Ch))
		{
		case 'A': Line(G, { {0,1}, {0.34f,0}, {0.68f,1} }); Line(G, { {0.13f,0.62f}, {0.55f,0.62f} }); break;
		case 'B': Line(G, { {0,0}, {0,1} }); Line(G, { {0,0}, {0.48f,0}, {0.6f,0.2f}, {0.44f,0.47f}, {0,0.47f} }); Line(G, { {0,0.47f}, {0.52f,0.47f}, {0.68f,0.72f}, {0.54f,1}, {0,1} }); break;
		case 'C': Line(G, { {0.62f,0.14f}, {0.46f,0}, {0.16f,0}, {0,0.26f}, {0,0.74f}, {0.16f,1}, {0.46f,1}, {0.62f,0.86f} }); break;
		case 'D': Line(G, { {0,0}, {0,1} }); Line(G, { {0,0}, {0.4f,0}, {0.64f,0.28f}, {0.64f,0.72f}, {0.4f,1}, {0,1} }); break;
		case 'E': Line(G, { {0.6f,0}, {0,0}, {0,1}, {0.6f,1} }); Line(G, { {0,0.48f}, {0.45f,0.48f} }); break;
		case 'F': Line(G, { {0.6f,0}, {0,0}, {0,1} }); Line(G, { {0,0.48f}, {0.45f,0.48f} }); break;
		case 'G': Line(G, { {0.62f,0.14f}, {0.46f,0}, {0.16f,0}, {0,0.26f}, {0,0.74f}, {0.16f,1}, {0.46f,1}, {0.62f,0.86f}, {0.62f,0.56f}, {0.36f,0.56f} }); break;
		case 'H': Line(G, { {0,0}, {0,1} }); Line(G, { {0.62f,0}, {0.62f,1} }); Line(G, { {0,0.5f}, {0.62f,0.5f} }); break;
		case 'I': Line(G, { {0.2f,0}, {0.2f,1} }); break;
		case 'J': Line(G, { {0.5f,0}, {0.5f,0.78f}, {0.3f,1}, {0.05f,0.85f} }); break;
		case 'K': Line(G, { {0,0}, {0,1} }); Line(G, { {0.62f,0}, {0,0.56f} }); Line(G, { {0.2f,0.4f}, {0.66f,1} }); break;
		case 'L': Line(G, { {0,0}, {0,1}, {0.56f,1}, {0.64f,0.86f} }); break;
		case 'M': Line(G, { {0,1}, {0,0}, {0.36f,0.5f}, {0.72f,0}, {0.72f,1} }); break;
		case 'N': Line(G, { {0,1}, {0,0}, {0.64f,1}, {0.64f,0} }); break;
		case 'O': Line(G, { {0.32f,0}, {0.64f,0.26f}, {0.64f,0.74f}, {0.32f,1}, {0,0.74f}, {0,0.26f}, {0.32f,0} }); break;
		case 'P': Line(G, { {0,1}, {0,0}, {0.5f,0}, {0.62f,0.22f}, {0.5f,0.47f}, {0,0.47f} }); break;
		case 'Q': Line(G, { {0.32f,0}, {0.64f,0.26f}, {0.64f,0.74f}, {0.32f,1}, {0,0.74f}, {0,0.26f}, {0.32f,0} }); Line(G, { {0.38f,0.7f}, {0.72f,1.08f} }); break;
		case 'R': Line(G, { {0,0}, {0,1} }); Line(G, { {0,0}, {0.5f,0}, {0.62f,0.22f}, {0.5f,0.47f}, {0,0.47f} }); Line(G, { {0.26f,0.47f}, {0.66f,1} }); break;
		case 'S': Line(G, { {0.62f,0.12f}, {0.46f,0}, {0.14f,0}, {0,0.2f}, {0.14f,0.44f}, {0.5f,0.56f}, {0.64f,0.8f}, {0.5f,1}, {0.16f,1}, {0,0.88f} }); break;
		case 'T': Line(G, { {0,0}, {0.68f,0} }); Line(G, { {0.34f,0}, {0.34f,1} }); break;
		case 'U': Line(G, { {0,0}, {0,0.78f}, {0.2f,1}, {0.44f,1}, {0.64f,0.78f}, {0.64f,0} }); break;
		case 'V': Line(G, { {0,0}, {0.34f,1}, {0.68f,0} }); break;
		case 'W': Line(G, { {0,0}, {0.18f,1}, {0.38f,0.4f}, {0.58f,1}, {0.76f,0} }); break;
		case 'X': Line(G, { {0,0}, {0.64f,1} }); Line(G, { {0.64f,0}, {0,1} }); break;
		case 'Y': Line(G, { {0,0}, {0.34f,0.5f}, {0.68f,0} }); Line(G, { {0.34f,0.5f}, {0.34f,1} }); break;
		case 'Z': Line(G, { {0,0}, {0.64f,0}, {0,1}, {0.64f,1} }); break;
		default: break;
		}
		return G;
	};
	auto Adv = [&](TCHAR Ch) { const TCHAR U = FChar::ToUpper(Ch); return H * (U == ' ' ? 0.55f : U == 'I' ? 0.5f : (U == 'M' || U == 'W') ? 0.98f : 0.86f); };
	float Total = 0.f;
	for (int32 i = 0; i < Text.Len(); ++i) Total += Adv(Text[i]);
	const float X0 = (Canvas->ClipX - (Total - H * 0.2f)) * 0.5f, Wd = H * 0.12f;
	auto Stroke = [&](const FSeg& S, float Ox, float Oy, float Width, const FLinearColor& C)
	{
		const FVector2D P0(X0 + Ox + S.X0 * H, Y + Oy + S.Y0 * H), P1(X0 + Ox + S.X1 * H, Y + Oy + S.Y1 * H);
		const FVector2D D = (P1 - P0).GetSafeNormal(), N(-D.Y, D.X), E = D * Width * 0.35f;   // chisel-cut ends, overlapping at the joints
		Poly({ P0 - E + N * Width * 0.5f, P1 + E + N * Width * 0.5f, P1 + E - N * Width * 0.5f, P0 - E - N * Width * 0.5f }, C);
	};
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		float Ox = 0.f;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			for (const FSeg& S : Glyph(Text[i]))
			{
				if (Pass == 0) Stroke(S, Ox + H * 0.05f, H * 0.06f, Wd * 1.6f, FLinearColor(0.f, 0.f, 0.f, 0.45f * Alpha));       // shadow
				else if (Pass == 1) Stroke(S, Ox, 0.f, Wd * 1.55f, FLinearColor(0.32f, 0.18f, 0.03f, Alpha));                      // dark edge
				else if (Pass == 2) Stroke(S, Ox, 0.f, Wd, FLinearColor(1.f, 0.74f, 0.2f, Alpha));                                 // gold
				else Stroke(S, Ox - Wd * 0.12f, -Wd * 0.12f, Wd * 0.35f, FLinearColor(1.f, 0.95f, 0.65f, 0.75f * Alpha));       // sheen
			}
			Ox += Adv(Text[i]);
		}
	}
}

