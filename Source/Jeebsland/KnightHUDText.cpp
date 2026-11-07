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

// The title lettering (owner, 2026-11-06, after how Skyrim letters its place names and its title): no drawn letters, only the game's own font in widely spaced capitals,
// warm ivory with a soft shadow so it reads on grass or sky. Its capitals' top at Y, H tall; at X, or centred if X < 0.
float AKnightHUD::TitleTextAt(const FString& TextIn, float X, float Y, float H, float Alpha)
{
	if (Alpha <= 0.01f || !HDFonts[0]) return 0.f;
	if (!TitleFonts[0] && HDFonts[0]->FontCacheType == EFontCacheType::Runtime)
	{
		static const int32 Sizes[3] = { 20, 40, 80 };
		for (int32 i = 0; i < 3; ++i)
		{
			TitleFonts[i] = DuplicateObject<UFont>(HDFonts[0], this);
			TitleFonts[i]->LegacyFontSize = Sizes[i];
		}
	}
	const FString Text = TextIn.ToUpper();
	// (the smallest raster at least as big as it is shown; a capital is about 0.61 of the font's line)
	UFont* F = HDFonts[0];
	for (int32 i = 0; i < 3; ++i)
	{
		if (!TitleFonts[i]) continue;
		F = TitleFonts[i];
		float W0 = 0.f, L0 = 0.f;
		GetTextSize(TEXT("H"), W0, L0, F, 1.f);
		if (L0 * 0.61f >= H * 0.95f) break;
	}
	float W0 = 0.f, Line = 0.f;
	GetTextSize(TEXT("H"), W0, Line, F, 1.f);
	const float S = H / FMath::Max(Line * 0.61f, 1.f);
	const float Spacing = H * (H >= 24.f ? 0.26f : 0.13f);   // (big titles breathe; a window's title stays compact)
	TArray<float> Adv;
	float Total = 0.f;
	for (int32 i = 0; i < Text.Len(); ++i)
	{
		float CW = 0.f, CH = 0.f;
		GetTextSize(Text.Mid(i, 1), CW, CH, F, S);
		if (Text[i] == ' ') CW = H * 0.42f;
		Adv.Add(CW + Spacing);
		Total += CW + Spacing;
	}
	Total -= Spacing;
	const float X0 = X >= 0.f ? X : (Canvas->ClipX - Total) * 0.5f;
	const float Top = Y - Line * S * 0.185f;   // (the line's empty top)
	const FLinearColor Ivory = H >= 24.f ? FLinearColor(0.98f, 0.93f, 0.80f, Alpha) : FLinearColor(1.f, 0.90f, 0.66f, Alpha);
	const float Sh = FMath::Max(1.f, H * 0.045f);
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		float Cx = X0;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const FString Ch = Text.Mid(i, 1);
			if (Pass == 0) DrawText(Ch, FLinearColor(0.f, 0.f, 0.f, 0.22f * Alpha), Cx + Sh * 1.6f, Top + Sh * 1.6f, F, S);
			else if (Pass == 1) DrawText(Ch, FLinearColor(0.f, 0.f, 0.f, 0.5f * Alpha), Cx + Sh * 0.7f, Top + Sh * 0.7f, F, S);
			else DrawText(Ch, Ivory, Cx, Top, F, S);
			Cx += Adv[i];
		}
	}
	return Total;
}

void AKnightHUD::TitleBanner(const FString& Text, float Y, float H, float Alpha)
{
	const float W = TitleTextAt(Text, -1.f, Y, H, Alpha);
	if (Alpha <= 0.01f || W <= 0.f) return;
	// a thin rule either side, fading away from the name
	const float Mid = Canvas->ClipX * 0.5f, RuleY = Y + H * 0.5f, Thick = FMath::Max(1.f, H * 0.035f), Long = H * 3.2f, From = W * 0.5f + H * 0.55f;
	constexpr int32 Steps = 24;
	for (int32 i = 0; i < Steps; ++i)
	{
		const float U = float(i) / Steps, Len = Long / Steps;
		const FLinearColor C(0.98f, 0.86f, 0.58f, 0.75f * Alpha * FMath::Square(1.f - U));
		DrawRect(C, Mid + From + U * Long, RuleY - Thick * 0.5f, Len + 0.5f, Thick);
		DrawRect(C, Mid - From - U * Long - Len, RuleY - Thick * 0.5f, Len + 0.5f, Thick);
	}
}
