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
// Small helpers
// ===================================================================================================
FLinearColor AKnightHUD::Lift(const FLinearColor& C, float F)
{
	return FLinearColor(FMath::Min(C.R * F, 1.f), FMath::Min(C.G * F, 1.f), FMath::Min(C.B * F, 1.f), 1.f);
}

float AKnightHUD::Wrapped(const FString& Text, float X, float Y, float MaxW, float Scale, const FLinearColor& Col)
{
	UFont* Font = GEngine->GetMediumFont();
	TArray<FString> Words;
	Text.ParseIntoArray(Words, TEXT(" "), true);
	FString Line;
	float Cy = Y;
	float LineH = 0.f;
	auto Flush = [&]()
	{
		if (Line.IsEmpty()) return;
		HDText(Line, Col, X, Cy, Font, Scale);
		Cy += LineH + 3.f;
		Line.Reset();
	};
	for (const FString& W : Words)
	{
		const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
		float TW = 0.f, TH = 0.f;
		HDTextSize(Try, TW, TH, Font, Scale);
		LineH = FMath::Max(LineH, TH);
		if (TW > MaxW && !Line.IsEmpty())
		{
			Flush();
			Line = W;
		}
		else Line = Try;
	}
	Flush();
	return Cy - Y;
}

// ---------------------------------------------------------------------------------------------------
// "Liquid glass" look. The canvas cannot blur what is behind a panel, so the glass is faked in layers: a cool,
// translucent body that is a little darker toward the bottom (so text stays readable over anything), a soft white sheen
// down from the top, a bright specular band under the top edge, and a thin rim of light that is strongest on the upper
// left and fades round to the lower right, with a slow glint travelling along the top.
// ---------------------------------------------------------------------------------------------------

// Two small anti-aliased corner pictures (the top-left quarter of a disc, and of a ring), made once. Every rounded corner on the
// glass is one of these scaled to size and flipped, so the curves are smooth instead of stair-stepped.
void AKnightHUD::EnsureGlassTex()
{
	if (QuarterTex && RingTex[0] && RingTex[1]) return;
	const int32 N = 48;
	auto Make = [&](float RingWidth) -> UTexture2D*   // RingWidth 0 = a filled disc
	{
		UTexture2D* T = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
		if (!T) return nullptr;
		T->SRGB = false;
		T->Filter = TF_Bilinear;
		T->AddressX = TA_Clamp;
		T->AddressY = TA_Clamp;
		FColor* Px = static_cast<FColor*>(T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for (int32 y = 0; y < N; ++y)
			for (int32 x = 0; x < N; ++x)
			{
				const float D = FMath::Sqrt(FMath::Square(x + 0.5f - N) + FMath::Square(y + 0.5f - N));   // from the corner the arc curves round
				const float A = RingWidth <= 0.f ? FMath::Clamp(N - D + 0.5f, 0.f, 1.f)
				                                 : FMath::Clamp(RingWidth * 0.5f + 0.5f - FMath::Abs(D - (N - RingWidth * 0.5f)), 0.f, 1.f);
				Px[y * N + x] = FColor(255, 255, 255, uint8(A * 255.f));
			}
		T->GetPlatformData()->Mips[0].BulkData.Unlock();
		T->UpdateResource();
		return T;
	};
	QuarterTex = Make(0.f);
	RingTex[0] = Make(3.5f);   // for big corners
	RingTex[1] = Make(9.f);    // for small corners, so the rim does not vanish
}

// One corner picture, scaled to R x R and flipped into the corner c (0 top left, 1 top right, 2 bottom left, 3 bottom right)
void AKnightHUD::Corner(UTexture2D* Tex, float X, float Y, float R, int32 C, const FLinearColor& Col)
{
	if (!Tex || R < 0.5f) return;
	const float U = (C & 1) ? 1.f : 0.f, V = (C & 2) ? 1.f : 0.f;
	const float Uw = (C & 1) ? -1.f : 1.f, Vh = (C & 2) ? -1.f : 1.f;
	DrawTexture(Tex, X, Y, R, R, U, V, Uw, Vh, Col, BLEND_Translucent);
}

// A rounded rectangle filled with a colour that varies from the top (t = 0) to the bottom (t = 1). The edges are snapped to
// whole pixels and nothing overlaps (an overlap of translucent pieces would show as a bright line).
void AKnightHUD::FillRR(float X, float Y, float W, float H, float R, TFunctionRef<FLinearColor(float)> ColorAt)
{
	EnsureGlassTex();
	X = FMath::RoundToFloat(X); Y = FMath::RoundToFloat(Y); W = FMath::RoundToFloat(W); H = FMath::RoundToFloat(H);
	if (W < 1.f || H < 1.f) return;
	R = FMath::RoundToFloat(FMath::Clamp(R, 0.f, FMath::Min(W, H) * 0.5f));
	if (R >= 1.f)
	{
		Corner(QuarterTex, X, Y, R, 0, ColorAt(R * 0.5f / H));
		Corner(QuarterTex, X + W - R, Y, R, 1, ColorAt(R * 0.5f / H));
		Corner(QuarterTex, X, Y + H - R, R, 2, ColorAt(1.f - R * 0.5f / H));
		Corner(QuarterTex, X + W - R, Y + H - R, R, 3, ColorAt(1.f - R * 0.5f / H));
	}
	// bands: the top and bottom R rows are narrower (between the corners); the middle is the full width
	auto Bands = [&](float Y0, float Y1, float X0, float X1)
	{
		const float L = Y1 - Y0;
		if (L < 1.f || X1 - X0 < 1.f) return;
		const int32 Nb = FMath::Max(1, FMath::CeilToInt(L / 2.f));   // 2 px steps: a smooth fade, no visible bands
		for (int32 b = 0; b < Nb; ++b)
		{
			const float A = FMath::RoundToFloat(Y0 + L * b / Nb), B = FMath::RoundToFloat(Y0 + L * (b + 1) / Nb);
			if (B > A) DrawRect(ColorAt(((A + B) * 0.5f - Y) / H), X0, A, X1 - X0, B - A);
		}
	};
	Bands(Y, Y + R, X + R, X + W - R);
	Bands(Y + R, Y + H - R, X, X + W);
	Bands(Y + H - R, Y + H, X + R, X + W - R);
}

// The rim of light round a rounded rectangle: bright upper left, fading to the lower right
void AKnightHUD::RimLight(float X, float Y, float W, float H, float R, float Alpha, const FLinearColor& Tint)
{
	EnsureGlassTex();
	X = FMath::RoundToFloat(X); Y = FMath::RoundToFloat(Y); W = FMath::RoundToFloat(W); H = FMath::RoundToFloat(H);
	R = FMath::RoundToFloat(FMath::Clamp(R, 0.f, FMath::Min(W, H) * 0.5f));
	const bool bBig = R >= 14.f;
	const float Th = FMath::Max(1.f, FMath::RoundToFloat(R * (bBig ? 3.5f : 9.f) / 48.f));
	auto C = [&](float A) { return FLinearColor(Tint.R, Tint.G, Tint.B, A * Alpha); };
	DrawRect(C(0.62f), X + R, Y, W - 2.f * R, Th);                 // top
	DrawRect(C(0.18f), X + R, Y + H - Th, W - 2.f * R, Th);        // bottom
	DrawRect(C(0.42f), X, Y + R, Th, H - 2.f * R);                 // left
	DrawRect(C(0.14f), X + W - Th, Y + R, Th, H - 2.f * R);        // right
	if (R < 1.f) return;
	UTexture2D* Ring = RingTex[bBig ? 0 : 1];
	Corner(Ring, X, Y, R, 0, C(0.62f));
	Corner(Ring, X + W - R, Y, R, 1, C(0.30f));
	Corner(Ring, X, Y + H - R, R, 2, C(0.30f));
	Corner(Ring, X + W - R, Y + H - R, R, 3, C(0.14f));
}

// A plain outline round a rounded rectangle, the same light all the way round
void AKnightHUD::Outline(float X, float Y, float W, float H, float R, const FLinearColor& Col)
{
	EnsureGlassTex();
	X = FMath::RoundToFloat(X); Y = FMath::RoundToFloat(Y); W = FMath::RoundToFloat(W); H = FMath::RoundToFloat(H);
	R = FMath::RoundToFloat(FMath::Clamp(R, 0.f, FMath::Min(W, H) * 0.5f));
	const float Th = FMath::Max(1.f, FMath::RoundToFloat(R * 9.f / 48.f));
	DrawRect(Col, X + R, Y, W - 2.f * R, Th);
	DrawRect(Col, X + R, Y + H - Th, W - 2.f * R, Th);
	DrawRect(Col, X, Y + R, Th, H - 2.f * R);
	DrawRect(Col, X + W - Th, Y + R, Th, H - 2.f * R);
	for (int32 c = 0; c < 4; ++c) Corner(RingTex[1], (c & 1) ? X + W - R : X, (c & 2) ? Y + H - R : Y, R, c, Col);
}

void AKnightHUD::Panel(float X, float Y, float W, float H, float Alpha, bool bGlow)
{
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float R = FMath::Clamp(FMath::Min(W, H) * 0.22f, 8.f, 30.f);
	const FLinearColor Warm(1.f, 0.82f, 0.45f, 1.f);
	for (int32 k = 3; k >= 1; --k)   // a soft shadow underneath, so the glass lifts off the world
		FillRR(X - k * 2.f, Y + 5.f - k, W + k * 4.f, H + k * 4.f, R + k * 2.f, [&](float) { return FLinearColor(0.f, 0.f, 0.02f, 0.07f * Alpha); });
	FillRR(X, Y, W, H, R, [&](float t)   // the body: cool and clear, deeper toward the bottom
	{
		if (bGreyGlass) return FLinearColor(FMath::Lerp(0.07f, 0.035f, t), FMath::Lerp(0.07f, 0.035f, t), FMath::Lerp(0.075f, 0.04f, t), FMath::Lerp(0.30f, 0.40f, t) * Alpha);   // smoky grey, dim, see-through
		return FLinearColor(FMath::Lerp(0.10f, 0.04f, t), FMath::Lerp(0.15f, 0.07f, t), FMath::Lerp(0.22f, 0.12f, t), FMath::Lerp(0.44f, 0.54f, t) * Alpha);
	});
	FillRR(X + 2.f, Y + 2.f, W - 4.f, H * 0.55f, R - 2.f, [&](float t)   // sheen pouring down from the top edge
	{
		return FLinearColor(1.f, 1.f, 1.f, FMath::Lerp(bGreyGlass ? 0.07f : 0.20f, 0.f, FMath::Clamp(t * 1.2f, 0.f, 1.f)) * Alpha);
	});
	if (bGlow) FillRR(X + 2.f, Y + 2.f, W - 4.f, H - 4.f, R - 2.f, [&](float t) { return FLinearColor(Warm.R, Warm.G, Warm.B, (0.08f + 0.10f * FMath::Sin(T * 4.f)) * (0.4f + t) * Alpha); });
	// specular band just inside the top edge
	if (W > 24.f && H > 24.f) FillRR(X + R * 0.7f, Y + 3.f, W - R * 1.4f, FMath::Max(3.f, H * 0.045f), 2.f, [&](float) { return FLinearColor(1.f, 1.f, 1.f, (bGreyGlass ? 0.06f : 0.16f) * Alpha); });
	RimLight(X, Y, W, H, R, Alpha, bGlow ? Warm : FLinearColor(1.f, 1.f, 1.f, 1.f));
}

// A small glass tile: buttons, slots, rows. Tint colours it (a hover or an active state); Alpha is its strength.
void AKnightHUD::Tile(float X, float Y, float W, float H, const FLinearColor& Tint, float Alpha, bool bRim)
{
	const float R = FMath::Clamp(FMath::Min(W, H) * 0.24f, 4.f, 14.f);
	FillRR(X, Y, W, H, R, [&](float t)
	{
		return FLinearColor(FMath::Lerp(Tint.R, Tint.R * 0.5f, t), FMath::Lerp(Tint.G, Tint.G * 0.5f, t), FMath::Lerp(Tint.B, Tint.B * 0.5f, t), Alpha * FMath::Lerp(1.f, 0.8f, t));
	});
	FillRR(X + 1.5f, Y + 1.5f, W - 3.f, H * 0.42f, R - 1.f, [&](float t) { return FLinearColor(1.f, 1.f, 1.f, FMath::Lerp(bGreyGlass ? 0.06f : 0.15f, 0.f, FMath::Clamp(t * 1.4f, 0.f, 1.f))); });
	if (bRim) RimLight(X, Y, W, H, R, 0.8f, FLinearColor(1.f, 1.f, 1.f, 1.f));
}

// A glass gauge: a pill-shaped well, and a pill of lit colour inside it with a gloss on top and a slow sheen along it.
void AKnightHUD::Gauge(float X, float Y, float W, float H, float F, const FLinearColor& Fill, bool bPulse)
{
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	F = FMath::Clamp(F, 0.f, 1.f);
	const float R = H * 0.5f;
	FillRR(X - 3.f, Y - 2.f, W + 6.f, H + 6.f, R + 3.f, [&](float) { return FLinearColor(0.f, 0.f, 0.02f, 0.18f); });   // shadow
	FillRR(X - 2.f, Y - 2.f, W + 4.f, H + 4.f, R + 2.f, [&](float t) { return FLinearColor(0.05f, 0.08f, 0.13f, FMath::Lerp(0.55f, 0.7f, t)); });
	const float Glow = bPulse ? 0.78f + 0.22f * FMath::Sin(T * 7.f) : 1.f;
	const float FW = W * F;
	if (FW > 2.f)
	{
		const float FR = FMath::Min(R, FW * 0.5f);
		FillRR(X, Y, FW, H, FR, [&](float t)
		{
			const float L = FMath::Lerp(1.0f, 0.6f, t) * Glow;   // soft, not bright
			return FLinearColor(FMath::Min(Fill.R * L, 1.f), FMath::Min(Fill.G * L, 1.f), FMath::Min(Fill.B * L, 1.f), 0.95f);
		});
		FillRR(X + 1.f, Y + 1.f, FW - 2.f, H * 0.42f, FMath::Max(0.f, FR - 1.f), [&](float t) { return FLinearColor(1.f, 1.f, 1.f, FMath::Lerp(0.26f, 0.04f, t)); });   // gloss
		const float Sw = FMath::Fmod(T * 0.35f, 1.5f);
		if (Sw < 1.f && FW > 40.f)   // a soft sheen gliding along the fill: stacked, narrowing layers fade it out at both ends (no hard-edged block)
		{
			const float Mid = X + 32.f + (FW - 64.f) * Sw;
			const float Env = FMath::Sin(Sw * PI);
			for (int32 k = 0; k < 8; ++k)
			{
				const float Wk = 64.f - k * 7.f, Lo = FMath::Max(X, Mid - Wk * 0.5f), Hi = FMath::Min(X + FW, Mid + Wk * 0.5f);
				if (Hi - Lo > 2.f) FillRR(Lo, Y, Hi - Lo, H, FMath::Min(R, (Hi - Lo) * 0.5f), [&](float) { return FLinearColor(1.f, 1.f, 1.f, 0.03f * Env); });
			}
		}
	}
	RimLight(X - 2.f, Y - 2.f, W + 4.f, H + 4.f, R + 2.f, 0.9f, FLinearColor(1.f, 1.f, 1.f, 1.f));
	for (int32 k = 1; k < 4; ++k) DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.14f), X + W * 0.25f * k - 0.5f, Y + 2.f, 1.f, H - 4.f);   // quarter marks
}

bool AKnightHUD::Btn(float X, float Y, float W, float H, const FString& Label, bool bEnabled, bool bActive, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const bool bOver = bEnabled && Over(X, Y, W, H);
	FLinearColor Fill = FLinearColor(0.18f, 0.18f, 0.18f);
	if (!bEnabled) Fill = FLinearColor(0.14f, 0.14f, 0.14f);
	else if (bActive) Fill = FLinearColor(0.55f, 0.32f, 0.08f);
	else if (bOver) Fill = FLinearColor(0.34f, 0.26f, 0.12f);
	Tile(X, Y, W, H, bActive ? FLinearColor(0.95f, 0.65f, 0.25f) : (bOver ? FLinearColor(0.8f, 0.9f, 1.f) : FLinearColor(0.55f, 0.68f, 0.85f)), !bEnabled ? 0.10f : (bActive ? 0.55f : (bOver ? 0.42f : 0.24f)));
	float TW = 0.f, TH = 0.f;
	HDTextSize(Label, TW, TH, Font, Scale);
	const FLinearColor Txt = !bEnabled ? FLinearColor(0.45f, 0.45f, 0.45f) : (bActive ? FLinearColor(1.f, 0.9f, 0.5f) : FLinearColor(0.85f, 0.85f, 0.85f));
	HDText(Label, Txt, X + (W - TW) * 0.5f, Y + (H - TH) * 0.5f, Font, Scale);
	return bOver && bUIClick;
}

bool AKnightHUD::CloseBtn(float PX, float PY, float PW)
{
	UFont* Font = GEngine->GetMediumFont();
	const float BX = PX + PW - 34.f, BY = PY + 6.f;
	const bool bOver = Over(BX, BY, 26.f, 26.f);
	Tile(BX, BY, 26.f, 26.f, bOver ? FLinearColor(1.f, 0.35f, 0.3f) : FLinearColor(0.6f, 0.7f, 0.85f), bOver ? 0.6f : 0.26f);
	HDText(TEXT("X"), FLinearColor::White, BX + 7.f, BY + 3.f, Font, 1.0f);
	return bOver && bUIClick;
}

void AKnightHUD::DrawTip()
{
	if (TipLines.Num() == 0 || !bUIHaveMouse) return;
	UFont* Font = GEngine->GetMediumFont();
	float W = 0.f, H = 0.f;
	for (int32 i = 0; i < TipLines.Num(); ++i)
	{
		float TW = 0.f, TH = 0.f;
		HDTextSize(TipLines[i], TW, TH, Font, i == 0 ? 1.15f : 0.95f);
		W = FMath::Max(W, TW); H += TH + 3.f;
	}
	float X = UIMouseX + 16.f, Y = UIMouseY + 12.f;
	if (X + W + 20.f > Canvas->ClipX) X = UIMouseX - W - 30.f;
	if (Y + H + 14.f > Canvas->ClipY) Y = Canvas->ClipY - H - 16.f;
	Panel(X - 8.f, Y - 8.f, W + 24.f, H + 16.f, 1.f);
	float Cy = Y;
	for (int32 i = 0; i < TipLines.Num(); ++i)
	{
		const float Sc = i == 0 ? 1.15f : 0.95f;
		float TW = 0.f, TH = 0.f;
		HDTextSize(TipLines[i], TW, TH, Font, Sc);
		HDText(TipLines[i], i == 0 ? FLinearColor(1.f, 0.9f, 0.5f) : FLinearColor(0.88f, 0.88f, 0.88f), X + 2.f, Cy, Font, Sc);
		Cy += TH + 3.f;
	}
}

// Tooltip for any item id
void AKnightHUD::ItemTip(int32 Id)
{
	TArray<FString> L;
	if (IsMaterial(Id))
	{
				const EItemType T = MaterialType(Id);
		L.Add(MaterialName(T));
		L.Add(FString::Printf(TEXT("Sells for %d ingots"), SellValue(T)));
		SetTip(L);
		return;
	}
	if (IsPotion(Id))
	{
		L.Add(ItemName(Id));
		L.Add(PotionText(PotionType(Id)));
		L.Add(TEXT("Put it in the potion slot and press Q to drink"));
		SetTip(L);
		return;
	}
	if (IsArrow(Id))
	{
		L.Add(ItemName(Id));
		int32 A, B; WeaponDamage(K_Bow, ArrowTier(Id), A, B, ArrowTier(Id));
		L.Add(FString::Printf(TEXT("Fired from a bow. Bow damage with these: %d-%d"), A, B));
		SetTip(L);
		return;
	}
	if (!IsGear(Id)) return;
	const int32 K = KindOf(Id), V = VariantOf(Id);
	L.Add(ItemName(Id));
	if (IsWeaponKind(K))
	{
		int32 A, B; WeaponDamage(K, V, A, B, V);
		L.Add(FString::Printf(TEXT("Damage %d-%d%s"), A, B, K == K_Bow ? TEXT(" with same-tier arrows") : TEXT("")));
		if (K == K_Pickaxe || K == K_Axe || K == K_Rod) L.Add(FString::Printf(TEXT("Gathers %d%% quicker than wood"), FMath::RoundToInt((1.f - ToolTimeMult(V)) * 100.f)));
		if (!AllowsShield(K)) L.Add(TEXT("Two-handed: cannot be used with a shield"));
	}
	else if (K == K_Shield)
	{
		L.Add(FString::Printf(TEXT("Guard %d: stops that much damage while raised, then half until it recovers"), ShieldGuard(Id)));
		static const TCHAR* StyleNote[NumShieldStyles] = { TEXT("Kite: the all-rounder"), TEXT("Round: a smaller guard, but it recovers twice as soon and you move 15% quicker behind it"), TEXT("Tower: a guard half as big again, but you move 20% slower behind it") };
		L.Add(StyleNote[StyleOf(Id)]);
	}
	else if (IsArmorKind(K))
	{
		L.Add(FString::Printf(TEXT("Defence +%d"), DefenseOf(K, V)));
		if (K == K_Boots) L.Add(FString::Printf(TEXT("Stamina x%.2f"), BootStaminaMult(V)));
	}
	else if (IsAccessoryKind(K)) L.Add(AccessoryText(K, V));
	L.Add(FString::Printf(TEXT("Worth %d ingots"), GearSellPrice(Id)));
	SetTip(L);
}

// ===================================================================================================
// The screen
// ===================================================================================================
void AKnightHUD::DrawHUD()
{
	Super::DrawHUD();

	APawn* Pawn = GetOwningPawn();
	if (!Pawn || !Canvas || !GEngine) return;
	AKnightCharacter* Knight = Cast<AKnightCharacter>(Pawn);
	USkillComponent* Skills = Pawn->FindComponentByClass<USkillComponent>();
	if (!Skills || !Knight) return;

	UFont* Font = GEngine->GetMediumFont();
	// Make the sharp copies of the font once (runtime fonts only: their size can simply be raised)
	if (!HDFonts[0] && Font)
	{
		HDFonts[0] = Font;
		if (Font->FontCacheType == EFontCacheType::Runtime)
		{
			for (int32 k = 2; k <= 4; ++k)
			{
				UFont* F = DuplicateObject<UFont>(Font, this);
				F->LegacyFontSize = Font->LegacyFontSize * k;
				HDFonts[k - 1] = F;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("HUD font: %s, runtime %d, size %d"), *Font->GetName(), Font->FontCacheType == EFontCacheType::Runtime ? 1 : 0, Font->LegacyFontSize);
	}

	APlayerController* PC = GetOwningPlayerController();
	bUIHaveMouse = PC && PC->GetMousePosition(UIMouseX, UIMouseY);
	const bool bDownNow = PC && PC->IsInputKeyDown(EKeys::LeftMouseButton);
	bUIClick = bDownNow && !bUIWasDown;
	bUIRelease = bUIWasDown && !bDownNow;
	bUIWasDown = bDownNow;
	const bool bRightNow = PC && PC->IsInputKeyDown(EKeys::RightMouseButton);
	bUIRightClick = bRightNow && !bRightWasDown;
	bRightWasDown = bRightNow;
	InvCellCount = 0; bHotCells = false; RectCount = 0; WornCellCount = 0; LootCellCount = 0; bPotionCell = false; bDefCell = false;
	TipLines.Reset();

	// Name tags over villagers, only when you are within a few steps (fading in over the last metre)
	if (PC && !Knight->IsUIOpen())
	{
		for (TActorIterator<ANpc> It(GetWorld()); It; ++It)
		{
			const FVector Head = It->GetActorLocation() + FVector(0.f, 0.f, 120.f);
			const float D = FVector::Dist2D(Head, Pawn->GetActorLocation());
			const float A = FMath::Clamp((350.f - D) / 100.f, 0.f, 1.f);   // full at 2.5 m, gone past 3.5 m
			if (A <= 0.f) continue;
			FVector2D SP;
			if (!PC->ProjectWorldLocationToScreen(Head, SP)) continue;
			const FString Nm = It->GetNpcName();
			float TW = 0.f, TH = 0.f;
			HDTextSize(Nm, TW, TH, Font, 2.f);
			HDText(Nm, FLinearColor(0.f, 0.f, 0.f, 0.8f * A), SP.X - TW * 0.5f + 2.f, SP.Y - TH + 2.f, Font, 2.f);
			HDText(Nm, FLinearColor(1.f, 0.85f, 0.45f, 0.95f * A), SP.X - TW * 0.5f, SP.Y - TH, Font, 2.f);
		}
	}

	// Realm title that fades in and out when the game starts
	{
		const float T = GetWorld()->GetTimeSeconds();
		const float Alpha = FMath::Clamp(FMath::Min(T / 1.2f, (7.f - T) / 1.5f), 0.f, 1.f);
		if (Alpha > 0.f)
		{
			const FString Title = TEXT("THE AUTUMN REALM");
			float TW = 0.f, TH = 0.f;
			HDTextSize(Title, TW, TH, Font, 3.f);
			HDText(Title, FLinearColor(1.f, 0.62f, 0.2f, Alpha), (Canvas->ClipX - TW) * 0.5f, Canvas->ClipY * 0.14f, Font, 3.f);
		}
	}

	// "BLACKROCK" fades in for a few seconds each time you walk in through the walls
	{
		const bool bIn = JeebTerrain::RectSD(FVector2D(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y), JeebTerrain::VillageCenter(), FVector2D(JeebTerrain::VillageHalf(), JeebTerrain::VillageHalf())) < 0.f;
		const float T = GetWorld()->GetTimeSeconds();
		if (bIn && !bInTown && T > 8.f) TownBannerT = T;
		bInTown = bIn;
		const float Age = T - TownBannerT;
		if (TownBannerT > 0.f && Age < 4.5f)
		{
			const float A = FMath::Clamp(FMath::Min(Age / 0.8f, (4.5f - Age) / 1.2f), 0.f, 1.f);
			const FString Name = TEXT("BLACKROCK");
			float TW = 0.f, TH = 0.f;
			HDTextSize(Name, TW, TH, Font, 3.f);
			HDText(Name, FLinearColor(0.f, 0.f, 0.f, 0.6f * A), (Canvas->ClipX - TW) * 0.5f + 3.f, Canvas->ClipY * 0.16f + 3.f, Font, 3.f);
			HDText(Name, FLinearColor(1.f, 0.88f, 0.55f, A), (Canvas->ClipX - TW) * 0.5f, Canvas->ClipY * 0.16f, Font, 3.f);
		}
	}


	DrawMinimap(Pawn);
	DrawWeaponBar(Knight);
	if (!Knight->IsUIOpen() && (Knight->GetPanelAlways() || Knight->IsPanelCursor())) DrawSidePanel(Knight);
	if (!Knight->IsUIOpen()) DrawSkillPopup(Skills);

	// Game messages, newest at the bottom, fading out
	{
		TArray<JeebMsg::FMsg>& Msgs = JeebMsg::All();
		const double NowS = FPlatformTime::Seconds();
		Msgs.RemoveAll([&](const JeebMsg::FMsg& M) { return NowS - M.Born > M.Life; });
		float Y = Canvas->ClipY - 190.f;
		for (int32 i = Msgs.Num() - 1; i >= 0 && i >= Msgs.Num() - 4; --i)
		{
			const JeebMsg::FMsg& M = Msgs[i];
			const float Age = float(NowS - M.Born);
			const float A = FMath::Clamp(FMath::Min(Age / 0.15f, (M.Life - Age) / 0.6f), 0.f, 1.f);
			float TW = 0.f, TH = 0.f;
			HDTextSize(M.Text, TW, TH, Font, 1.15f);
			const float X = (Canvas->ClipX - TW) * 0.5f;
			Panel(X - 10.f, Y - 3.f, TW + 20.f, TH + 6.f, 0.8f * A);
			HDText(M.Text, FLinearColor(M.Color.R, M.Color.G, M.Color.B, A), X, Y, Font, 1.15f);
			Y -= TH + 10.f;
		}
	}

	// The stamina bar is hidden (stamina still works, it just is not drawn)

	// Health bars: thin, long, soft in colour and low on the screen. Yours is the lowest; the horse's sits above it while you ride.
	const float BarW = 480.f, BarH = 6.f;
	const float BarX = (Canvas->ClipX - BarW) * 0.5f;
	{
		const float BY = Canvas->ClipY - 28.f;
		const float F = FMath::Clamp(Knight->GetHealth() / (float)FMath::Max(1, Knight->GetMaxHealth()), 0.f, 1.f);
		const FLinearColor HpCol = (F < 0.18f) ? FLinearColor(0.72f, 0.14f, 0.12f) : (F < 0.4f ? FLinearColor(0.82f, 0.68f, 0.18f) : FLinearColor(0.28f, 0.62f, 0.30f));
		Gauge(BarX, BY, BarW, BarH, F, HpCol, F < 0.18f);   // throbs when nearly dead
	}

	// Enemy health bars (15 s after a hit, or under half health) and floating damage numbers
	if (PC)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		for (UHealthComponent* H : UHealthComponent::All())
		{
			if (!H || !H->ShowBar(Now) || !H->GetOwner() || H->GetOwner()->IsHidden()) continue;
			const FVector Pt = H->GetOwner()->GetActorLocation() + FVector(0.f, 0.f, H->BarHeight);
			if (FVector::Dist(Pt, Pawn->GetActorLocation()) > 2800.f) continue;
			FVector2D SP;
			if (!PC->ProjectWorldLocationToScreen(Pt, SP)) continue;
			const float BW = 76.f, BH = 8.f;
			const float F = FMath::Clamp((float)H->Cur / (float)FMath::Max(1, H->Max), 0.f, 1.f);
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.8f), SP.X - BW * 0.5f - 2.f, SP.Y - 2.f, BW + 4.f, BH + 4.f);
			DrawRect(FLinearColor(0.2f, 0.f, 0.f, 1.f), SP.X - BW * 0.5f, SP.Y, BW, BH);
			DrawRect(FLinearColor(0.85f, 0.1f, 0.08f), SP.X - BW * 0.5f, SP.Y, BW * F, BH);
		}
		TArray<JeebCombat::FNumber>& Nums = JeebCombat::Numbers();
		for (int32 i = Nums.Num() - 1; i >= 0; --i)
		{
			const float Age = Now - Nums[i].Born;
			if (Age < 0.f || Age > 1.2f) { Nums.RemoveAt(i); continue; }
			FVector2D SP;
			if (!PC->ProjectWorldLocationToScreen(Nums[i].Loc + FVector(0.f, 0.f, 70.f * Age), SP)) continue;
			FLinearColor Col(1.f, 0.07f, 0.05f);   // a strong red
			float Sc = 2.9f;   // 1.8x the old size
			if (Nums[i].Type == JeebCombat::N_Blocked) Col = FLinearColor(0.3f, 0.55f, 1.f);
			else if (Nums[i].Type == JeebCombat::N_Crit) { Col = FLinearColor(1.f, 0.82f, 0.1f); Sc = 4.1f; }
			Col.A = FMath::Clamp((1.2f - Age) / 0.4f, 0.f, 1.f);
			const FString Txt = FString::FromInt(Nums[i].Amount);
			float TW = 0.f, TH = 0.f;
			HDTextSize(Txt, TW, TH, Font, Sc);
			HDText(Txt, FLinearColor(0.f, 0.f, 0.f, Col.A), SP.X - TW * 0.5f + 2.f, SP.Y + 2.f, Font, Sc);
			HDText(Txt, Col, SP.X - TW * 0.5f, SP.Y, Font, Sc);
		}
	}

	// Shield guard: a steel bar like the health bar, just above the weapon slots. It appears when the shield comes up and goes
	// once it is lowered, but stays while the guard is below full, until it has recovered.
	// A broken guard throbs until it is full again.
	if (Knight->GetGuardFraction() > 0.f || Knight->IsGuardBroken())
	{
		const float F = Knight->GetGuardFraction();
		if (F < 0.999f || Knight->IsBlocking())
		{
			const float BY = Canvas->ClipY - 46.f - 46.f - 26.f;
			const float GW = BarW, GX = BarX;
			Gauge(GX, BY, GW, BarH, F, FLinearColor(0.64f, 0.68f, 0.75f), Knight->IsGuardBroken());   // steel
		}
	}

	// While riding: the horse's health bar, above the hotbar. (Its gallop energy never runs out, so there is no bar for it.)
	if (Knight->IsMounted())
	{
		const float BY = Canvas->ClipY - 46.f - 46.f - 40.f;   // above the shield bar
		const float F = FMath::Clamp(Knight->GetHorseHealth() / Knight->GetHorseMaxHealth(), 0.f, 1.f);
		const FLinearColor HpCol = (F < 0.18f) ? FLinearColor(0.72f, 0.14f, 0.12f) : (F < 0.4f ? FLinearColor(0.82f, 0.68f, 0.18f) : FLinearColor(0.28f, 0.62f, 0.30f));
		Gauge(BarX, BY, BarW, BarH, F, HpCol, F < 0.18f);
	}

	// The E prompt: an outlined square with an E in it, floating just in front of and above whatever you can use. It fades in
	// as you get close and out as you leave. Things that are not an E action (a lock you cannot pick, a fishing spot) and
	// drops (what is lying there) get a small caption under it instead.
	{
		FString Prompt;
		FVector Anchor;
		bool bE = false;
		const bool bHave = PC && !Knight->IsUIOpen() && Knight->GetInteractPrompt(Prompt) && Knight->GetInteractAnchor(Anchor, bE);
		const float Dt = GetWorld()->GetDeltaSeconds();
		PromptAlpha = FMath::FInterpTo(PromptAlpha, bHave ? 1.f : 0.f, Dt, 12.f);
		if (bHave) { PromptAnchor = Anchor; PromptText = Prompt; bPromptE = bE; }
		FVector2D SP;
		if (PromptAlpha > 0.02f && PC && PC->ProjectWorldLocationToScreen(PromptAnchor, SP))
		{
			const float T = GetWorld()->GetTimeSeconds();
			const float A = PromptAlpha;
			const float S = 36.f;
			const float X = SP.X - S * 0.5f, Y = SP.Y - S * 0.5f - 4.f * FMath::Sin(T * 2.2f) - (1.f - A) * 8.f;   // bobs gently, settles in as it appears
			if (bPromptE)
			{
				FillRR(X, Y, S, S, 9.f, [&](float) { return FLinearColor(0.f, 0.02f, 0.05f, 0.22f * A); });
				Outline(X, Y, S, S, 9.f, FLinearColor(1.f, 1.f, 1.f, 0.9f * A));
				float TW = 0.f, TH = 0.f;
				HDTextSize(TEXT("E"), TW, TH, Font, 1.5f);
				HDText(TEXT("E"), FLinearColor(1.f, 1.f, 1.f, A), X + (S - TW) * 0.5f, Y + (S - TH) * 0.5f, Font, 1.5f);
			}
			if (!bPromptE || !PromptText.StartsWith(TEXT("Press E")))
			{
				float TW = 0.f, TH = 0.f;
				HDTextSize(PromptText, TW, TH, Font, 1.0f);
				const float CY = bPromptE ? Y + S + 6.f : Y;
				Tile(SP.X - TW * 0.5f - 10.f, CY - 2.f, TW + 20.f, TH + 6.f, FLinearColor(0.05f, 0.07f, 0.12f), 0.5f * A, false);
				HDText(PromptText, FLinearColor(1.f, 1.f, 1.f, A), SP.X - TW * 0.5f, CY + 1.f, Font, 1.0f);
			}
		}
	}

	// Screens (only one at a time)
	if (Knight->IsSettingsOpen()) DrawSettingsScreen(Knight);
	else if (Knight->IsInventoryOpen()) DrawInventoryScreen(Knight);
	else if (Knight->IsSkillsOpen()) DrawSkillsScreen(Knight);
	else if (Knight->IsBankOpen()) DrawBankScreen(Knight);
	else if (Knight->IsShopOpen()) DrawShopScreen(Knight);
	else if (Knight->IsSmithOpen()) DrawSmithScreen(Knight);
	else if (Knight->IsDialogueOpen()) DrawDialogue(Knight);

	if (!bDropOpen) ProcessDrag(Knight);
	DrawDropBox(Knight);
	DrawDragGhost();
	DrawTip();
}

// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawSkills(const USkillComponent* Skills)
{
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 1.15f;
	const float W = 300.f, H = 236.f;
	const float X = 24.f;
	const float Y = Canvas->ClipY - H - 24.f;

	Panel(X, Y, W, H);
	HDText(TEXT("SKILLS"), FLinearColor(1.f, 0.85f, 0.3f), X + 16.f, Y + 8.f, Font, 1.3f);

	auto SkillRow = [&](float RowY, const TCHAR* Name, ESkillType Skill, const FLinearColor& BarColor)
	{
		int32 Level, Into, Needed;
		Skills->GetProgress(Skill, Level, Into, Needed);
		HDText(FString::Printf(TEXT("%s   Lv %d"), Name, Level), FLinearColor::White, X + 16.f, RowY, Font, Scale);
		const float BarX = X + 16.f, BarY = RowY + 26.f, BarW = W - 32.f;
		const float Fill = Needed > 0 ? FMath::Clamp(float(Into) / float(Needed), 0.f, 1.f) : 0.f;
		Gauge(BarX, BarY, BarW, 9.f, Fill, BarColor);
		HDText(FString::Printf(TEXT("%d / %d XP"), Into, Needed), FLinearColor(0.8f, 0.8f, 0.8f), BarX + BarW - 100.f, RowY + 2.f, Font, 0.9f);
	};
	SkillRow(Y + 40.f,  TEXT("Mining"),      ESkillType::Mining,      FLinearColor(0.9f, 0.55f, 0.15f));
	SkillRow(Y + 92.f,  TEXT("Woodcutting"), ESkillType::Woodcutting, FLinearColor(0.3f, 0.75f, 0.25f));
	SkillRow(Y + 144.f, TEXT("Smithing"),    ESkillType::Smithing,    FLinearColor(0.8f, 0.3f, 0.15f));
	SkillRow(Y + 196.f - 0.f, TEXT("Fishing"), ESkillType::Fishing,   FLinearColor(0.25f, 0.6f, 0.9f));
}

// ---------------------------------------------------------------------------------------------------
// Bottom-left pop-up: the skill you last trained, its level and progress. Stays 30 s after the last XP, then fades.
void AKnightHUD::DrawSkillPopup(const USkillComponent* Skills)
{
	const double Now = FPlatformTime::Seconds();
	const float Since = float(Now - Skills->LastSkillTime);
	if (Since > 30.f) return;
	const float A = FMath::Clamp(FMath::Min(Since / 0.25f, (30.f - Since) / 1.5f), 0.f, 1.f);
	UFont* Font = GEngine->GetMediumFont();
	const float W = 330.f, H = 92.f;
	const float X = 24.f - (1.f - A) * 40.f, Y = Canvas->ClipY - H - 24.f;

	const float SinceLevel = float(Now - Skills->LevelUpTime);
	const bool bLevelGlow = SinceLevel < 4.f;
	const float Pulse = bLevelGlow ? 0.5f + 0.5f * FMath::Sin(SinceLevel * 9.f) : 0.f;
	Panel(X, Y, W, H, A, bLevelGlow);

	const ESkillType T = Skills->LastSkill;
	DrawSkillIcon(static_cast<int32>(T), X + 10.f, Y + 10.f, 72.f);
	int32 Lv, Into, Need;
	Skills->GetProgress(T, Lv, Into, Need);
	HDText(USkillComponent::SkillName(T), FLinearColor(1.f, 0.9f, 0.6f, A), X + 92.f, Y + 10.f, Font, 1.2f);
	{
		const FString LvS = FString::Printf(TEXT("Lv %d"), Lv);
		float TW = 0.f, TH = 0.f;
		HDTextSize(LvS, TW, TH, Font, 1.35f);
		HDText(LvS, bLevelGlow ? FLinearColor(1.f, 0.85f, 0.3f, A) : FLinearColor(1.f, 1.f, 1.f, A), X + W - TW - 12.f, Y + 8.f, Font, 1.35f);
	}
	const float BX = X + 92.f, BY = Y + 46.f, BW = W - 104.f, BH = 12.f;
	const float F = Need > 0 ? FMath::Clamp(float(Into) / float(Need), 0.f, 1.f) : 0.f;
	Gauge(BX, BY, BW, BH, F, FLinearColor(0.35f, 0.8f, 0.3f));
	HDText(FString::Printf(TEXT("%d / %d XP"), Into, Need), FLinearColor(0.8f, 0.8f, 0.8f, A), BX, BY + 16.f, Font, 0.9f);
	if (Since < 2.5f && Skills->LastXPGain > 0)
	{
		const float GA = A * FMath::Clamp((2.5f - Since) / 0.8f, 0.f, 1.f);
		const FString Gain = FString::Printf(TEXT("+%d"), Skills->LastXPGain);
		float TW = 0.f, TH = 0.f;
		HDTextSize(Gain, TW, TH, Font, 1.0f);
		HDText(Gain, FLinearColor(0.6f, 1.f, 0.5f, GA), BX + BW - TW, BY + 16.f - Since * 4.f, Font, 1.0f);
	}
	if (bLevelGlow) HDText(TEXT("LEVEL UP!"), FLinearColor(1.f, 0.85f, 0.3f, A * (0.6f + 0.4f * Pulse)), X + 92.f, Y - 26.f, Font, 1.2f);
}

// Tabs across the top of the pack window: INVENTORY and SKILLS. Returns true if a click switched tabs.
bool AKnightHUD::DrawPackTabs(AKnightCharacter* Knight, float PX, float PY, bool bSkills)
{
	if (Btn(PX + 24.f, PY + 12.f, 190.f, 40.f, TEXT("INVENTORY"), true, !bSkills, 1.35f) && bSkills) { Knight->ShowPackTab(false); return true; }
	if (Btn(PX + 222.f, PY + 12.f, 190.f, 40.f, TEXT("SKILLS"), true, bSkills, 1.35f) && !bSkills) { Knight->ShowPackTab(true); return true; }
	return false;
}

// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawWeaponBar(AKnightCharacter* Knight)
{
	USkillComponent* Skills = Knight->GetSkills();
	UFont* Font = GEngine->GetMediumFont();
	const float SS = 46.f, Gap = 6.f;   // small slots, low on the screen
	const float TotalW = 5 * SS + 4 * Gap;
	const float X0 = (Canvas->ClipX - TotalW) * 0.5f - 40.f;
	const float Y0 = Canvas->ClipY - 46.f - SS;   // just above the health bar

	int32 Arrows = 0;
	for (int32 T = 0; T < NumTiers; ++T) Arrows += Skills->CountItem(ArrowId(T));

	for (int32 i = 0; i < NumHotbar; ++i)
	{
		const float X = X0 + i * (SS + Gap);
		HotCellX[i] = X; HotCellY[i] = Y0; HotCellS = SS; bHotCells = true;
		const bool bSel = (Skills->Selected == i);
		const int32 Id = Skills->Hotbar[i];
		// The chosen slot stands out: it is lifted and bigger, tinted gold with a bright gold outline, while the others are dimmed
		const float Sx = bSel ? X - 4.f : X, Sy = bSel ? Y0 - 13.f : Y0, Ss = bSel ? SS + 8.f : SS;
		Panel(Sx - 3.f, Sy - 3.f, Ss + 6.f, Ss + 6.f, 1.f, bSel);
		if (bSel)
		{
			FillRR(Sx, Sy, Ss, Ss, 13.f, [&](float t) { return FLinearColor(1.f, 0.72f, 0.2f, FMath::Lerp(0.30f, 0.12f, t)); });
			Outline(Sx - 3.f, Sy - 3.f, Ss + 6.f, Ss + 6.f, 15.f, FLinearColor(1.f, 0.8f, 0.3f, 1.f));
			Outline(Sx - 1.f, Sy - 1.f, Ss + 2.f, Ss + 2.f, 13.f, FLinearColor(1.f, 0.92f, 0.6f, 0.7f));
		}
		HDText(FString::Printf(TEXT("%d"), i + 1), bSel ? FLinearColor(1.f, 0.9f, 0.5f) : FLinearColor(0.7f, 0.7f, 0.7f), Sx + 4.f, Sy + 1.f, Font, bSel ? 0.9f : 0.7f);
		if (Id == 0) { if (!bSel) FillRR(X, Y0, SS, SS, 11.f, [&](float) { return FLinearColor(0.f, 0.f, 0.03f, 0.25f); }); continue; }
		// (every weapon icon leans the same way everywhere: see DrawIcon)
		DrawIcon(Id, Sx - 4.f, Sy - 4.f, Ss + 8.f);
		if (!bSel) FillRR(X, Y0, SS, SS, 11.f, [&](float) { return FLinearColor(0.f, 0.f, 0.03f, 0.30f); });   // dim the ones not in hand
		int32 A, B;
		if (HotbarDamage(Skills, i, A, B))
		{
			const FString Txt = FString::FromInt(FMath::RoundToInt(0.5f * (A + B)));   // one number: the middle of the range
			float TW = 0.f, TH = 0.f;
			HDTextSize(Txt, TW, TH, Font, 1.15f);
			Tile(Sx + Ss - TW - 9.f, Sy + Ss - TH - 5.f, TW + 8.f, TH + 3.f, FLinearColor(0.05f, 0.07f, 0.12f), 0.55f, false);
			HDText(Txt, bSel ? FLinearColor(1.f, 0.7f, 0.55f) : FLinearColor(0.85f, 0.5f, 0.42f), Sx + Ss - TW - 5.f, Sy + Ss - TH - 3.f, Font, 1.15f);
		}
		if (IsGear(Id) && KindOf(Id) == K_Bow)
			HDText(FString::Printf(TEXT("x%d"), Arrows), Arrows > 0 ? FLinearColor(0.9f, 0.9f, 0.6f) : FLinearColor(1.f, 0.3f, 0.3f), Sx + Ss - 26.f, Sy + 1.f, Font, 0.7f);
		if (Knight->IsPanelCursor() && Over(X, Y0, SS, SS)) ItemTip(Id);   // hover with the cursor out (Tab) to read about it
	}

	// The potion slot, between the weapons and the defence slot: Q drinks it. While that kind of potion is cooling down the slot
	// darkens from the top and shows the seconds left.
	{
		const float X = X0 + TotalW + 18.f;
		PotionCellX = X; PotionCellY = Y0; bPotionCell = true;
		Panel(X - 3.f, Y0 - 3.f, SS + 6.f, SS + 6.f);
		HDText(TEXT("Q"), FLinearColor(0.7f, 0.7f, 0.7f), X + 4.f, Y0 + 1.f, Font, 0.7f);
		const FInvSlot& P = Skills->PotionSlot;
		if (P.Id != 0 && IsPotion(P.Id))
		{
			if (!(DragKind == 6 && bDragging)) DrawIcon(P.Id, X + 3.f, Y0 + 3.f, SS - 6.f);
			HDText(FString::FromInt(P.Count), FLinearColor::White, X + SS - 14.f, Y0 + 1.f, Font, 0.8f);
			const int32 T = PotionType(P.Id);
			const float Left = Knight->GetPotionCooldownLeft(T);
			if (Left > 0.f)
			{
				const float F = FMath::Clamp(Left / PotionCooldown(T), 0.f, 1.f);
				FillRR(X, Y0, SS, SS * F, FMath::Min(10.f, SS * F * 0.5f), [&](float) { return FLinearColor(0.f, 0.f, 0.02f, 0.6f); });
				const FString Sec = FString::FromInt(FMath::CeilToInt(Left));
				float SW = 0.f, SH = 0.f;
				HDTextSize(Sec, SW, SH, Font, 1.2f);
				HDText(Sec, FLinearColor(0.f, 0.f, 0.f, 0.8f), X + (SS - SW) * 0.5f + 1.f, Y0 + (SS - SH) * 0.5f + 1.f, Font, 1.2f);
				HDText(Sec, FLinearColor(1.f, 1.f, 1.f), X + (SS - SW) * 0.5f, Y0 + (SS - SH) * 0.5f, Font, 1.2f);
			}
			if (Knight->IsPanelCursor() && Over(X, Y0, SS, SS)) ItemTip(P.Id);
		}
		else if (Knight->IsPanelCursor() && Over(X, Y0, SS, SS)) SetTip({ TEXT("Potion slot"), TEXT("Drag a potion here, then press Q to drink it") });
	}

	// Defence total, to the right of the potion slot
	{
		// a slot like the weapon slots: your shield (or a plain one), leaning the same way, with your defence in blue
		const float X = X0 + TotalW + 18.f + SS + 14.f;
		Panel(X - 3.f, Y0 - 3.f, SS + 6.f, SS + 6.f);
		const int32 Def = TotalDefense(Skills, Knight->ShieldActive());
		// this is the shield slot: drag a shield here to hold it, drag it off to put it away
		DefCellX = X; DefCellY = Y0; bDefCell = true;
		const int32 ShieldId = Skills->Worn[K_Shield];
		if (ShieldId != 0)
		{
			if (!(DragKind == 7 && bDragging)) DrawIcon(ShieldId, X - 1.f, Y0 - 1.f, SS + 2.f);
		}
		else
		{
			float LW = 0.f, LH = 0.f;
			HDTextSize(TEXT("Shield"), LW, LH, Font, 0.7f);
			HDText(TEXT("Shield"), FLinearColor(0.6f, 0.6f, 0.62f), X + (SS - LW) * 0.5f, Y0 + (SS - LH) * 0.5f, Font, 0.7f);
		}
		const FString Txt = FString::FromInt(Def);
		float TW = 0.f, TH = 0.f;
		HDTextSize(Txt, TW, TH, Font, 1.15f);
		Tile(X + SS - TW - 9.f, Y0 + SS - TH - 5.f, TW + 8.f, TH + 3.f, FLinearColor(0.05f, 0.07f, 0.12f), 0.55f, false);
		HDText(Txt, FLinearColor(0.5f, 0.75f, 1.f), X + SS - TW - 5.f, Y0 + SS - TH - 3.f, Font, 1.15f);
		if (Knight->IsPanelCursor() && Over(X, Y0, SS, SS))
		{
			if (ShieldId != 0) ItemTip(ShieldId);
			else SetTip({ TEXT("Shield slot"), FString::Printf(TEXT("Drag a shield here. Defence %d in all, every bonus counted"), Def) });
		}
	}
}

// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawMinimap(APawn* Pawn)
{
	UFont* Font = GEngine->GetMediumFont();
	const float MS = 210.f;                // map size on screen
	const float MX0 = 24.f, MY0 = 24.f;
	const float Range = 6500.f;            // how far the map reaches from the knight (world units)
	const float Half = MS * 0.5f;
	const float Cx = MX0 + Half, Cy = MY0 + Half;
	const float K = Half / Range;
	const FVector PL = Pawn->GetActorLocation();

	// Backdrop: the map itself is a half-see-through grey disc (50%), inside a thin gold ring with a dark edge, so the
	// world shows through it. Everything on top (ring, markers, letters) stays solid.
	DrawCircle(Cx, Cy, Half, FLinearColor(0.12f, 0.13f, 0.12f, 0.5f));
	DrawAnnulus(Cx, Cy, Half + 7.5f, FLinearColor(0.f, 0.f, 0.f, 0.7f));
	DrawAnnulus(Cx, Cy, Half + 5.5f, FLinearColor(0.62f, 0.44f, 0.18f, 1.f));
	DrawAnnulus(Cx, Cy, Half * 0.5f + 1.f, FLinearColor(1.f, 1.f, 1.f, 0.12f));   // a faint inner ring at half range

	// The map turns with the camera: whatever the camera faces is at the top
	float CamYaw = Pawn->GetActorRotation().Yaw;
	if (APlayerController* MPC = GetOwningPlayerController())
		if (MPC->PlayerCameraManager) CamYaw = MPC->PlayerCameraManager->GetCameraRotation().Yaw;
	const FVector2D CamF(FMath::Cos(FMath::DegreesToRadians(CamYaw)), FMath::Sin(FMath::DegreesToRadians(CamYaw)));
	const FVector2D CamR(-CamF.Y, CamF.X);
	auto Rot = [&](const FVector2D& D, float& OutRight, float& OutDown) { OutRight = FVector2D::DotProduct(D, CamR); OutDown = -FVector2D::DotProduct(D, CamF); };

	// World -> map. Returns false if off the map unless clamped to the edge.
	auto ToMap = [&](const FVector2D& W, float R, bool bClamp, float& OutX, float& OutY) -> bool
	{
		float dx, dy;
		Rot((W - FVector2D(PL.X, PL.Y)) * K, dx, dy);
		const float Lim = Half - R - 3.f;
		const float Dist = FMath::Sqrt(dx * dx + dy * dy);
		if (Dist > Lim)
		{
			if (!bClamp) return false;
			dx *= Lim / Dist;
			dy *= Lim / Dist;
		}
		OutX = Cx + dx;
		OutY = Cy + dy;
		return true;
	};
	auto Marker = [&](const FVector2D& W, float R, const FLinearColor& Col, bool bClamp)
	{
		float X, Y;
		if (!ToMap(W, R, bClamp, X, Y)) return;
		DrawCircle(X, Y, R + 1.5f, FLinearColor(0.f, 0.f, 0.f, 0.85f));
		DrawCircle(X, Y, R, Col);
	};

	// Water
	Marker(JeebTerrain::PondCenter(), 6.f, FLinearColor(0.2f, 0.55f, 0.95f), true);

	// Stores: shop, smithy, bank
	Marker(JeebTerrain::ShopPos(), 4.f, FLinearColor(1.f, 0.85f, 0.2f), false);
	Marker(JeebTerrain::SmithyPos(), 4.f, FLinearColor(0.9f, 0.4f, 0.1f), false);
	Marker(JeebTerrain::BankPos(), 4.f, FLinearColor(0.7f, 0.8f, 0.95f), false);

	// People
	for (TActorIterator<ANpc> It(GetWorld()); It; ++It)
	{
		Marker(FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y), 3.f, FLinearColor::White, false);
	}
	// Your horse
	for (TActorIterator<AHorse> It(GetWorld()); It; ++It)
	{
		Marker(FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y), 3.5f, FLinearColor(0.75f, 0.45f, 0.18f), true);
	}
	// The pen knight
	for (TActorIterator<AFoeKnight> It(GetWorld()); It; ++It)
	{
		if (!It->IsHidden()) Marker(FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y), 3.5f, FLinearColor(0.95f, 0.15f, 0.1f), false);
	}
	// Enemies (any other character)
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		if (*It == Pawn) continue;
		Marker(FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y), 3.f, FLinearColor(0.95f, 0.15f, 0.1f), false);
	}

	// You: a filled arrow pointing the way you face (relative to the camera)
	{
		const FVector F = Pawn->GetActorForwardVector();
		float FR, FD;
		Rot(FVector2D(F.X, F.Y), FR, FD);
		const FVector2D Fwd = FVector2D(FR, FD).GetSafeNormal();   // map direction
		const FVector2D Side(-Fwd.Y, Fwd.X);
		const FVector2D C(Cx, Cy);
		const FVector2D Tip = C + Fwd * 10.f, L = C - Fwd * 7.f + Side * 7.f, Rr = C - Fwd * 7.f - Side * 7.f, Notch = C - Fwd * 3.f;
		auto Tri = [&](const FVector2D& A, const FVector2D& B, const FVector2D& D, const FLinearColor& Col)
		{
			if (!CircleTex || !CircleTex->GetResource()) return;
			const FVector2D Mid(0.5f, 0.5f);   // the solid middle of the circle texture: plain white
			FCanvasTriangleItem T(A, B, D, Mid, Mid, Mid, CircleTex->GetResource());
			T.SetColor(Col);
			T.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(T);
		};
		const FVector2D O = FVector2D(1.2f, 1.2f);
		Tri(Tip + O, L + O, Notch + O, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Tri(Tip + O, Notch + O, Rr + O, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Tri(Tip, L, Notch, FLinearColor(1.f, 0.9f, 0.15f));
		Tri(Tip, Notch, Rr, FLinearColor(1.f, 0.78f, 0.1f));
	}

	// Compass letters sitting on the ring
	{
		auto Letter = [&](const TCHAR* T, float WX, float WY, const FLinearColor& Col)   // the world direction the letter marks
		{
			float Dx, Dy;
			Rot(FVector2D(WX, WY), Dx, Dy);
			float TW = 0.f, TH = 0.f;
			HDTextSize(T, TW, TH, Font, 1.7f);
			const float X = Cx + Dx * (Half + 6.f), Y = Cy + Dy * (Half + 6.f);
			DrawCircle(X, Y, 18.f, FLinearColor(0.10f, 0.11f, 0.10f, 0.5f));
			HDText(T, Col, X - TW * 0.5f, Y - TH * 0.5f, Font, 1.7f);
		};
		const FLinearColor CC(1.f, 1.f, 1.f, 0.85f);
		Letter(TEXT("N"), 1.f, 0.f, FLinearColor(1.f, 0.82f, 0.28f, 1.f));   // north is world +X, east +Y
		Letter(TEXT("S"), -1.f, 0.f, CC);
		Letter(TEXT("W"), 0.f, -1.f, CC);
		Letter(TEXT("E"), 0.f, 1.f, CC);
	}
}

// A smooth thin ring (an anti-aliased annulus texture; its thickness is 4% of its radius), so a ring can be drawn without
// a solid disc under it and whatever is inside it stays see-through
void AKnightHUD::DrawAnnulus(float X, float Y, float R, const FLinearColor& Col)
{
	if (!AnnulusTex)
	{
		const int32 N = 256;
		AnnulusTex = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
		if (!AnnulusTex) return;
		AnnulusTex->SRGB = false;
		AnnulusTex->Filter = TF_Bilinear;
		FColor* Px = static_cast<FColor*>(AnnulusTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		const float C = N * 0.5f, Out = N * 0.5f - 1.f, In = Out * 0.96f;
		for (int32 y = 0; y < N; ++y)
			for (int32 x = 0; x < N; ++x)
			{
				const float D = FMath::Sqrt(FMath::Square(x + 0.5f - C) + FMath::Square(y + 0.5f - C));
				const float A = FMath::Clamp(Out - D + 0.5f, 0.f, 1.f) * FMath::Clamp(D - In + 0.5f, 0.f, 1.f);
				Px[y * N + x] = FColor(255, 255, 255, uint8(A * 255.f));
			}
		AnnulusTex->GetPlatformData()->Mips[0].BulkData.Unlock();
		AnnulusTex->UpdateResource();
	}
	DrawTexture(AnnulusTex, X - R, Y - R, 2.f * R, 2.f * R, 0.f, 0.f, 1.f, 1.f, Col, BLEND_Translucent);
}

// A smooth filled circle (an anti-aliased disc texture, so it moves smoothly between pixels)
void AKnightHUD::DrawCircle(float X, float Y, float R, const FLinearColor& Col)
{
	if (!CircleTex)
	{
		const int32 N = 128;
		CircleTex = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
		if (!CircleTex) return;
		CircleTex->SRGB = false;
		CircleTex->Filter = TF_Bilinear;
		FColor* Px = static_cast<FColor*>(CircleTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		const float C = (N - 1) * 0.5f, Rad = N * 0.5f - 1.5f;
		for (int32 y = 0; y < N; ++y)
			for (int32 x = 0; x < N; ++x)
			{
				const float D = FMath::Sqrt(FMath::Square(x - C) + FMath::Square(y - C));
				const float A = FMath::Clamp(Rad - D + 0.5f, 0.f, 1.f);
				Px[y * N + x] = FColor(255, 255, 255, uint8(A * 255.f));
			}
		CircleTex->GetPlatformData()->Mips[0].BulkData.Unlock();
		CircleTex->UpdateResource();
	}
	DrawTexture(CircleTex, X - R, Y - R, 2.f * R, 2.f * R, 0.f, 0.f, 1.f, 1.f, Col, BLEND_Translucent);
}
// ---------------------------------------------------------------------------------------------------
// Settings, controls and the dictionary (press O)
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawSettingsScreen(AKnightCharacter* Knight)
{
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 1.3f;
	const float PW = 980.f, PH = 640.f;
	const float PX = (Canvas->ClipX - PW) * 0.5f;
	const float PY = (Canvas->ClipY - PH) * 0.5f;
	// Solid, not see-through: a dark grey backing under the glass
	FillRR(PX, PY, PW, PH, 30.f, [&](float t) { return FLinearColor(FMath::Lerp(0.10f, 0.05f, t), FMath::Lerp(0.10f, 0.05f, t), FMath::Lerp(0.11f, 0.06f, t), 1.f); });
	Panel(PX, PY, PW, PH);
	if (CloseBtn(PX, PY, PW)) { Knight->ToggleSettings(); return; }

	const float MX = UIMouseX, MY = UIMouseY;
	const bool bHaveMouse = bUIHaveMouse;
	const bool bDown = bUIWasDown;
	const bool bClicked = bUIClick;

	// Tabs
	const float TabW = 170.f, TabH = 32.f, TabY = PY + 10.f;
	const TCHAR* TabNames[4] = { TEXT("SETTINGS"), TEXT("CONTROLS"), TEXT("DICTIONARY"), TEXT("LORE") };
	for (int32 t = 0; t < 4; ++t)
	{
		const float TX = PX + 20.f + t * (TabW + 8.f);
		if (Btn(TX, TabY, TabW, TabH, TabNames[t], true, SettingsTab == t, 1.2f))
		{
			SettingsTab = t;
			DraggingSlider = -1;
		}
	}

	if (SettingsTab == 0)
	{
		const float SX = PX + 40.f, SW = 560.f;
		const float LookY = PY + 130.f;    // slider 0: look sensitivity
		const float ScaleY = PY + 220.f;   // slider 1: render scale
		const float BoxX = PX + 40.f, BoxY = PY + 280.f;

		auto OverSlider = [&](float SY) { return MX >= SX - 12.f && MX <= SX + SW + 12.f && MY >= SY - 22.f && MY <= SY + 30.f; };

		if (bHaveMouse)
		{
			if (bDown)
			{
				if (DraggingSlider < 0 && OverSlider(LookY))  DraggingSlider = 0;
				if (DraggingSlider < 0 && OverSlider(ScaleY)) DraggingSlider = 1;
				const float T = FMath::Clamp((MX - SX) / SW, 0.f, 1.f);
				if (DraggingSlider == 0) Knight->SetLookSensitivity(FMath::Lerp(0.1f, 2.0f, T));
				if (DraggingSlider == 1) Knight->SetRenderScale(FMath::Lerp(0.5f, 1.0f, T));

				// Checkbox: toggles once per click
				if (bClicked && MX >= BoxX - 4.f && MX <= BoxX + 560.f && MY >= BoxY + 46.f && MY <= BoxY + 82.f)
					Knight->SetPanelAlways(!Knight->GetPanelAlways());
				if (bClicked && MX >= BoxX - 4.f && MX <= BoxX + 460.f && MY >= BoxY - 4.f && MY <= BoxY + 32.f)
				{
					Knight->SetFancyLighting(!Knight->GetFancyLighting());
				}
			}
			else
			{
				DraggingSlider = -1;
			}
		}

		// Keyboard backup for the look slider: Left / Right arrows
		if (APlayerController* PC = GetOwningPlayerController())
		{
			const float Dt = GetWorld()->GetDeltaSeconds();
			if (PC->IsInputKeyDown(EKeys::Right)) Knight->SetLookSensitivity(Knight->GetLookSensitivity() + 0.5f * Dt);
			if (PC->IsInputKeyDown(EKeys::Left))  Knight->SetLookSensitivity(Knight->GetLookSensitivity() - 0.5f * Dt);
		}

		auto Slider = [&](float SY, float T, const FLinearColor& Fill)
		{
			DrawRect(FLinearColor(0.25f, 0.25f, 0.25f, 1.f), SX, SY + 4.f, SW, 8.f);
			DrawRect(Fill, SX, SY + 4.f, SW * T, 8.f);
			DrawRect(FLinearColor::White, SX + SW * T - 8.f, SY - 6.f, 16.f, 28.f);
		};

		const float Look = Knight->GetLookSensitivity();
		HDText(FString::Printf(TEXT("Look sensitivity:  %d%%"), FMath::RoundToInt(Look * 100.f)), FLinearColor::White, PX + 40.f, PY + 96.f, Font, Scale);
		Slider(LookY, FMath::Clamp((Look - 0.1f) / 1.9f, 0.f, 1.f), FLinearColor(0.9f, 0.55f, 0.15f));

		const float RS = Knight->GetRenderScale();
		HDText(FString::Printf(TEXT("Render resolution:  %d%%   (lower = faster)"), FMath::RoundToInt(RS * 100.f)), FLinearColor::White, PX + 40.f, PY + 186.f, Font, Scale);
		Slider(ScaleY, FMath::Clamp((RS - 0.5f) / 0.5f, 0.f, 1.f), FLinearColor(0.3f, 0.7f, 0.9f));

		DrawRect(FLinearColor::White, BoxX, BoxY, 26.f, 26.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 1.f), BoxX + 2.f, BoxY + 2.f, 22.f, 22.f);
		if (Knight->GetFancyLighting()) DrawRect(FLinearColor(0.9f, 0.55f, 0.15f), BoxX + 5.f, BoxY + 5.f, 16.f, 16.f);
		HDText(TEXT("Fancy lighting (bounce light, misty air) - slower"), FLinearColor::White, BoxX + 38.f, BoxY + 2.f, Font, 1.1f);
		// Side panel: always on screen, or only while the cursor is out (Tab)
		DrawRect(FLinearColor::White, BoxX, BoxY + 50.f, 26.f, 26.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 1.f), BoxX + 2.f, BoxY + 52.f, 22.f, 22.f);
		if (Knight->GetPanelAlways()) DrawRect(FLinearColor(0.9f, 0.55f, 0.15f), BoxX + 5.f, BoxY + 55.f, 16.f, 16.f);
		HDText(TEXT("Show the inventory panel all the time (off: only while Tab is held open)"), FLinearColor::White, BoxX + 38.f, BoxY + 52.f, Font, 1.1f);

	}
	else if (SettingsTab == 1)
	{
		struct FRow { const TCHAR* Key; const TCHAR* Action; };
		static const FRow Rows[] = {
			{ TEXT("W A S D"),        TEXT("Move") },
			{ TEXT("Mouse"),          TEXT("Look around") },
			{ TEXT("Space"),          TEXT("Jump (also: stand up, let go of a ladder)") },
			{ TEXT("Shift (hold)"),   TEXT("Sprint (uses stamina; better boots give more)") },
			{ TEXT("Ctrl (hold)"),    TEXT("Crouch") },
			{ TEXT("1 - 5"),          TEXT("Choose the weapon or tool on your weapon bar") },
			{ TEXT("Left click"),     TEXT("Chop / mine what is in front of you (the right tool is picked for you), or attack") },
			{ TEXT("Right click (hold)"), TEXT("Raise your shield and duck (needs a shield; not with two-handed weapons)") },
			{ TEXT("C while sprinting"), TEXT("Slide") },
			{ TEXT("V or Alt"),       TEXT("Dodge roll") },
			{ TEXT("Tab"),            TEXT("Inventory: click gear to wear or hold it, click worn gear to take it off") },
			{ TEXT("K"),              TEXT("Skills: your levels, shown as icons") },
			{ TEXT("E at a door"),    TEXT("Open or close it. Locked huts need Lockpicking; shutting one locks it again") },
			{ TEXT("Click (spear)"),  TEXT("Hold to charge, let go to thrust. Swords: click again within 0.3 s for a quick sweeping follow-up") },
			{ TEXT("E"),              TEXT("Talk to the shopkeeper, blacksmith and others; sit on benches") },
			{ TEXT("W / S at a ladder"), TEXT("Climb up / down (walk into it)") },
			{ TEXT("O"),              TEXT("Settings, controls, dictionary and the lore you have collected") },
		};
		float RowY = PY + 70.f;
		for (const FRow& R : Rows)
		{
			HDText(R.Key, FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, RowY, Font, 1.1f);
			HDText(R.Action, FLinearColor::White, PX + 250.f, RowY, Font, 1.05f);
			RowY += 30.f;
		}
		HDText(TEXT("Press O to close."), FLinearColor(0.8f, 0.8f, 0.8f), PX + 30.f, PY + PH - 40.f, Font, 1.0f);
	}
	else if (SettingsTab == 2)
	{
		DrawDictionary(PX, PY, PW, PH);
	}
	else
	{
		DrawLore(PX, PY, PW, PH);
	}
}

// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawDictionary(float PX, float PY, float PW, float PH)
{
	UFont* Font = GEngine->GetMediumFont();

	struct FEntry { const TCHAR* Name; int32 Kind; };   // Kind: -1 = ranks, -2 = arrows, otherwise a JeebItems::EKind
	static const FEntry Entries[] = {
		{ TEXT("Ranks & smithing"), -1 },
		{ TEXT("Sword"), K_Sword }, { TEXT("Axe"), K_Axe }, { TEXT("Pickaxe"), K_Pickaxe }, { TEXT("Spear"), K_Spear },
		{ TEXT("Broadsword"), K_Broadsword }, { TEXT("Battle Axe"), K_BattleAxe }, { TEXT("Katana"), K_Katana },
		{ TEXT("Wand"), K_Wand }, { TEXT("Wizard Staff"), K_Staff }, { TEXT("Bow"), K_Bow }, { TEXT("Arrows"), -2 }, { TEXT("Fishing Pole"), K_Rod },
		{ TEXT("Shield"), K_Shield }, { TEXT("Helmet"), K_Helmet }, { TEXT("Chestplate"), K_Chest }, { TEXT("Shoulders"), K_Shoulders },
		{ TEXT("Gloves"), K_Gloves }, { TEXT("Boots"), K_Boots }, { TEXT("Cape"), K_Cape }, { TEXT("Rings"), K_Ring }, { TEXT("Amulets"), K_Amulet },
	};
	const int32 NumEntries = UE_ARRAY_COUNT(Entries);
	DictEntry = FMath::Clamp(DictEntry, 0, NumEntries - 1);

	// List
	const float LX = PX + 20.f, LY = PY + 60.f, LW = 210.f, RH = 24.f;
	for (int32 i = 0; i < NumEntries; ++i)
	{
		if (Btn(LX, LY + i * RH, LW, RH - 2.f, Entries[i].Name, true, DictEntry == i, 0.95f)) DictEntry = i;
	}

	const float CX = PX + 255.f, CW = PW - 275.f;
	const FEntry& E = Entries[DictEntry];
	float Y = PY + 62.f;
	HDText(E.Name, FLinearColor(1.f, 0.85f, 0.3f), CX, Y, Font, 1.7f);
	Y += 40.f;

	if (E.Kind == -1)
	{
		Y += Wrapped(TEXT("Every weapon, shield and piece of armour comes in five ranks, weakest to strongest. Each rank is a different colour and has more damage or defence than the last."), CX, Y, CW, 1.05f, FLinearColor::White) + 10.f;
		for (int32 t = 0; t < NumTiers; ++t)
		{
			DrawRect(Palette(t, 0), CX, Y + 4.f, 14.f, 14.f);
			Y += Wrapped(TierBlurb(t), CX + 24.f, Y, CW - 24.f, 1.0f, FLinearColor(0.9f, 0.9f, 0.9f)) + 4.f;
		}
		Y += 8.f;
		Y += Wrapped(TEXT("HOW IT WORKS: gather materials (mining gives copper, coal and diamonds; chopping gives logs), then take them to the blacksmith to build gear. Wood uses logs, bronze uses copper, iron uses coal, steel uses coal plus copper, diamond uses diamonds plus coal. Smithing earns Smithing XP, and higher ranks need a higher Smithing level. The store sells wood, bronze and iron gear and sells arrows. Steel and diamond have to be smithed."), CX, Y, CW, 1.0f, FLinearColor(0.85f, 0.95f, 0.85f)) + 8.f;
		Y += Wrapped(TEXT("GATHERING LEVELS: coal needs Mining 6 and diamond Mining 15. Pine needs Woodcutting 6 and redwood Woodcutting 15. Trout needs Fishing 5 and the golden carp Fishing 12. Better pickaxes, axes and fishing poles work faster."), CX, Y, CW, 1.0f, FLinearColor(0.9f, 0.85f, 0.7f)) + 8.f;
		Wrapped(TEXT("DAMAGE: every hit rolls a number between a weapon's lowest and highest damage. Rings and amulets move or reshape that range."), CX, Y, CW, 1.0f, FLinearColor(0.9f, 0.8f, 0.8f));
		return;
	}

	if (E.Kind == -2)
	{
		Y += Wrapped(TEXT("Arrows are limited: every shot uses one, and the bow uses the best arrows you carry. Better arrows hit harder. Buy bundles of ten at the store (wood, bronze and iron) or smith any rank, including steel and diamond."), CX, Y, CW, 1.05f, FLinearColor::White) + 12.f;
		const TCHAR* Heads[] = { TEXT("RANK"), TEXT("BOW DAMAGE"), TEXT("SMITH LV"), TEXT("MATERIALS (10)"), TEXT("STORE (10)") };
		const float Xs[] = { 0.f, 110.f, 250.f, 360.f, 580.f };
		for (int32 c = 0; c < 5; ++c) HDText(Heads[c], FLinearColor(0.7f, 0.7f, 0.7f), CX + Xs[c], Y, Font, 0.95f);
		Y += 28.f;
		for (int32 t = 0; t < NumTiers; ++t)
		{
			int32 A, B; WeaponDamage(K_Bow, t, A, B, t);
			const FRecipe R = SmithRecipe(K_Bow, t, true);
			DrawIcon(ArrowId(t), CX - 4.f, Y - 4.f, 28.f);
			HDText(TierName(t), FLinearColor::White, CX + 30.f, Y, Font, 1.0f);
			HDText(FString::Printf(TEXT("%d-%d"), A, B), FLinearColor(1.f, 0.6f, 0.5f), CX + Xs[1], Y, Font, 1.0f);
			HDText(FString::Printf(TEXT("%d"), R.Level), FLinearColor::White, CX + Xs[2], Y, Font, 1.0f);
			HDText(RecipeText(R), FLinearColor(0.9f, 0.9f, 0.8f), CX + Xs[3], Y, Font, 1.0f);
			HDText(t <= StoreMaxTier ? FString::Printf(TEXT("%d ingots"), ArrowBundlePrice(t)) : FString(TEXT("smith only")), FLinearColor(1.f, 0.85f, 0.3f), CX + Xs[4], Y, Font, 1.0f);
			Y += 34.f;
		}
		return;
	}

	const int32 K = E.Kind;
	Y += Wrapped(KindBlurb(K), CX, Y, CW, 1.05f, FLinearColor::White) + 12.f;

	if (K == K_Cape)
	{
		for (int32 c = 0; c < NumCapeColors; ++c)
		{
			DrawIcon(GearId(K_Cape, c), CX, Y - 4.f, 30.f);
			HDText(CapeColorName(c), FLinearColor::White, CX + 40.f, Y, Font, 1.0f);
			HDText(FString::Printf(TEXT("%d ingots"), BuyPrice(K_Cape, c)), FLinearColor(1.f, 0.85f, 0.3f), CX + 200.f, Y, Font, 1.0f);
			Y += 34.f;
		}
		return;
	}
	if (IsAccessoryKind(K))
	{
		const int32 N = (K == K_Ring) ? NumRings : NumAmulets;
		for (int32 v = 0; v < N; ++v)
		{
			DrawIcon(GearId(K, v), CX, Y - 6.f, 36.f);
			HDText(AccessoryName(K, v), FLinearColor(1.f, 0.9f, 0.6f), CX + 46.f, Y, Font, 1.05f);
			HDText(AccessoryText(K, v), FLinearColor(0.85f, 0.85f, 0.85f), CX + 46.f, Y + 20.f, Font, 0.95f);
			HDText(FString::Printf(TEXT("%d ingots"), AccessoryPrice(K, v)), FLinearColor(1.f, 0.85f, 0.3f), CX + CW - 120.f, Y, Font, 1.0f);
			Y += 50.f;
		}
		Wrapped(TEXT("Rings and amulets are sold at the store. Two rings and one amulet can be worn at once."), CX, Y + 8.f, CW, 0.95f, FLinearColor(0.7f, 0.7f, 0.7f));
		return;
	}

	// Weapons, shield and armour: one row per rank
	const bool bWeapon = IsWeaponKind(K);
	const TCHAR* Heads[] = { TEXT("RANK"), bWeapon ? TEXT("DAMAGE") : TEXT("DEFENCE"), TEXT("SMITH LV"), TEXT("MATERIALS"), TEXT("STORE") };
	const float Xs[] = { 44.f, 150.f, 250.f, 350.f, 580.f };
	for (int32 c = 0; c < 5; ++c) HDText(Heads[c], FLinearColor(0.7f, 0.7f, 0.7f), CX + Xs[c], Y, Font, 0.95f);
	Y += 28.f;
	for (int32 t = 0; t < NumTiers; ++t)
	{
		const FRecipe R = SmithRecipe(K, t);
		DrawIcon(GearId(K, t), CX - 2.f, Y - 6.f, 36.f);
		HDText(TierName(t), FLinearColor::White, CX + Xs[0], Y, Font, 1.0f);
		if (bWeapon)
		{
			int32 A, B; WeaponDamage(K, t, A, B, t);
			HDText(FString::Printf(TEXT("%d-%d"), A, B), FLinearColor(1.f, 0.6f, 0.5f), CX + Xs[1], Y, Font, 1.0f);
		}
		else HDText(FString::Printf(TEXT("%d"), DefenseOf(K, t)), FLinearColor(0.55f, 0.75f, 1.f), CX + Xs[1], Y, Font, 1.0f);
		HDText(FString::Printf(TEXT("%d"), R.Level), FLinearColor::White, CX + Xs[2], Y, Font, 1.0f);
		HDText(RecipeText(R), FLinearColor(0.9f, 0.9f, 0.8f), CX + Xs[3], Y, Font, 1.0f);
		HDText(t <= StoreMaxTier ? FString::Printf(TEXT("%d ingots"), BuyPrice(K, t)) : FString(TEXT("smith only")), FLinearColor(1.f, 0.85f, 0.3f), CX + Xs[4], Y, Font, 1.0f);
		if (K == K_Boots) HDText(FString::Printf(TEXT("stamina x%.2f"), BootStaminaMult(t)), FLinearColor(0.6f, 0.9f, 0.6f), CX + Xs[1], Y + 18.f, Font, 0.85f);
		Y += (K == K_Boots) ? 42.f : 38.f;
	}
	if (K == K_Pickaxe || K == K_Axe || K == K_Rod)
		Wrapped(TEXT("Gathering time compared with wood: bronze takes 14% less, iron 28% less, steel 42% less and diamond 56% less."), CX, Y + 6.f, CW, 0.95f, FLinearColor(0.7f, 0.9f, 0.7f));
}

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
		static const FLinearColor Fill[NumPotions] = { FLinearColor(0.25f, 0.85f, 0.25f), FLinearColor(0.9f, 0.12f, 0.1f), FLinearColor(0.6f, 0.2f, 0.9f) };
		const FLinearColor F = Fill[PotionType(Id)];
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
		const FLinearColor Head = Lift(Palette(T, 0), 1.3f), Shaft(0.50f, 0.34f, 0.17f), Fl(0.92f, 0.92f, 0.88f);
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

		if (K == K_Ring || K == K_Amulet)
		{
			static const FLinearColor RingBand[NumRings] = { {0.80f,0.82f,0.88f,1.f}, {0.95f,0.75f,0.20f,1.f}, {0.72f,0.45f,0.20f,1.f}, {0.80f,0.82f,0.88f,1.f}, {0.95f,0.75f,0.20f,1.f} };
			static const FLinearColor RingGem[NumRings] = { {0.95f,0.95f,1.f,1.f}, {1.f,0.95f,0.6f,1.f}, {1.f,0.55f,0.15f,1.f}, {0.2f,0.45f,1.f,1.f}, {0.95f,0.1f,0.2f,1.f} };
			static const FLinearColor AmGem[NumAmulets] = { {1.f,0.7f,0.1f,1.f}, {0.1f,0.8f,0.35f,1.f}, {0.65f,0.2f,0.95f,1.f}, {0.3f,0.5f,1.f,1.f}, {0.95f,0.2f,0.3f,1.f} };
			if (K == K_Ring)
			{
				DiscP(32, 38, 19, 19, RingBand[V]);
				DiscP(32, 38, 12, 12, FLinearColor(0.10f, 0.09f, 0.08f));
				Poly({ P(32, 8), P(42, 17), P(32, 27), P(22, 17) }, RingGem[V]);
				Poly({ P(32, 8), P(42, 17), P(32, 17) }, Lift(RingGem[V], 1.3f));
			}
			else
			{
				const FLinearColor Chain(0.85f, 0.85f, 0.9f);
				for (int32 k = 0; k < 9; ++k)
				{
					const float T = k / 8.f;
					DiscP(8.f + 48.f * T, 6.f + 30.f * (1.f - FMath::Abs(2.f * T - 1.f)) * 1.f, 2.4f, 2.4f, Chain);
				}
				Poly({ P(32, 30), P(46, 42), P(32, 60), P(18, 42) }, AmGem[V]);
				Poly({ P(32, 30), P(46, 42), P(32, 42) }, Lift(AmGem[V], 1.35f));
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
			const FLinearColor Cu(0.88f, 0.48f, 0.16f);
			Poly({ P(22, 28), P(32, 25), P(34, 34), P(24, 37) }, Cu);
			Poly({ P(38, 36), P(48, 36), P(46, 46), P(37, 45) }, Cu);
			Poly({ P(18, 42), P(26, 41), P(26, 49), P(18, 49) }, FLinearColor(0.72f, 0.38f, 0.12f));
		}
		break;
	}
	case EItemType::Diamond:
		Poly({ P(20, 12), P(44, 12), P(56, 28), P(32, 58), P(8, 28) }, FLinearColor(0.35f, 0.82f, 0.96f));
		Poly({ P(8, 28), P(20, 12), P(32, 28) }, FLinearColor(0.72f, 0.97f, 1.f));
		Poly({ P(56, 28), P(44, 12), P(32, 28) }, FLinearColor(0.55f, 0.92f, 1.f));
		Poly({ P(20, 12), P(44, 12), P(32, 28) }, FLinearColor(0.85f, 1.f, 1.f));
		Poly({ P(8, 28), P(32, 58), P(32, 28) }, FLinearColor(0.20f, 0.62f, 0.85f));
		break;
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

// ===================================================================================================
// Inventory screen (Tab)
// ===================================================================================================
void AKnightHUD::DrawInventoryScreen(AKnightCharacter* Knight)
{
	USkillComponent* Skills = Knight->GetSkills();
	UFont* Font = GEngine->GetMediumFont();
	const float PW = 1060.f, PH = 650.f;
	const float PX = (Canvas->ClipX - PW) * 0.5f, PY = (Canvas->ClipY - PH) * 0.5f;
	Panel(PX, PY, PW, PH);

	if (DrawPackTabs(Knight, PX, PY, false)) return;
	{
		const FString Bal = FString::Printf(TEXT("%d"), Skills->GetIngots());
		float TW = 0.f, TH = 0.f;
		HDTextSize(Bal, TW, TH, Font, 1.8f);
		HDText(Bal, FLinearColor(1.f, 0.85f, 0.3f), PX + PW - 100.f - TW, PY + 14.f, Font, 1.8f);
		DrawIngot(PX + PW - 92.f, PY + 10.f, 40.f);
	}
	if (CloseBtn(PX, PY, PW)) { Knight->ToggleInventory(); return; }

	auto SlotBox = [&](float X, float Y, float S, bool bOver, bool bSel)
	{
		Tile(X, Y, S, S, bSel ? FLinearColor(1.f, 0.78f, 0.35f) : (bOver ? FLinearColor(0.85f, 0.93f, 1.f) : FLinearColor(0.6f, 0.72f, 0.88f)), bSel ? 0.5f : (bOver ? 0.36f : 0.2f));
	};

	// ----- The pack: 16 slots -----
	HDText(TEXT("PACK"), FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, PY + 64.f, Font, 1.2f);
	const float SS = 84.f, Gap = 8.f;
	const float GX = PX + 30.f, GY = PY + 92.f;
	for (int32 i = 0; i < NumSlots; ++i)
	{
		const float SX = GX + (i % 4) * (SS + Gap), SY = GY + (i / 4) * (SS + Gap);
		const bool bOver = Over(SX, SY, SS, SS);
		const FInvSlot S = Skills->Slots[i];
		SlotBox(SX, SY, SS, bOver, false);
		if (S.Id == 0) continue;
		DrawIcon(S.Id, SX + 10.f, SY + 8.f, SS - 20.f);
		if (StackSize(S.Id) > 1)
			HDText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, SX + SS - 30.f, SY + SS - 24.f, Font, 1.1f);
		if (bOver)
		{
			ItemTip(S.Id);
			if (bUIClick && IsGear(S.Id)) { Knight->EquipFromInventory(i); }
		}
	}

	// ----- Weapon bar -----
	const float EX = GX + 4 * (SS + Gap) + 34.f;
	HDText(TEXT("WEAPON BAR  (keys 1-5)"), FLinearColor(1.f, 0.85f, 0.3f), EX, PY + 64.f, Font, 1.2f);
	const float HS = 66.f, HG = 10.f;
	for (int32 h = 0; h < NumHotbar; ++h)
	{
		const float X = EX + h * (HS + HG), Y = PY + 92.f;
		const bool bOver = Over(X, Y, HS, HS);
		SlotBox(X, Y, HS, bOver, Skills->Selected == h);
		HDText(FString::Printf(TEXT("%d"), h + 1), FLinearColor(0.7f, 0.7f, 0.7f), X + 4.f, Y + 2.f, Font, 0.9f);
		const int32 Id = Skills->Hotbar[h];
		if (Id == 0) continue;
		DrawIcon(Id, X + 7.f, Y + 5.f, HS - 14.f);
		int32 A, B;
		if (HotbarDamage(Skills, h, A, B))
			HDText(FString::Printf(TEXT("%d-%d"), A, B), FLinearColor(1.f, 0.55f, 0.45f), X + HS - 44.f, Y + HS - 20.f, Font, 0.9f);
		if (bOver)
		{
			ItemTip(Id);
			if (bUIClick) Knight->UnequipHotbar(h);
		}
	}

	// ----- Worn gear -----
	HDText(TEXT("WORN"), FLinearColor(1.f, 0.85f, 0.3f), EX, PY + 170.f, Font, 1.2f);
	struct FWornSlot { int32 Kind; bool bSecond; const TCHAR* Label; };
	static const FWornSlot Worn[10] = {
		{ K_Shield, false, TEXT("Shield") }, { K_Helmet, false, TEXT("Helmet") }, { K_Chest, false, TEXT("Chest") }, { K_Shoulders, false, TEXT("Shoulders") }, { K_Gloves, false, TEXT("Gloves") },
		{ K_Boots, false, TEXT("Boots") }, { K_Cape, false, TEXT("Cape") }, { K_Ring, false, TEXT("Misc") }, { K_Ring, true, TEXT("Misc") }, { K_Amulet, false, TEXT("Misc") },
	};
	for (int32 w = 0; w < 10; ++w)
	{
		const float X = EX + (w % 5) * (HS + HG), Y = PY + 198.f + (w / 5) * (HS + 34.f);
		const bool bOver = Over(X, Y, HS, HS);
		SlotBox(X, Y, HS, bOver, false);
		HDText(Worn[w].Label, FLinearColor(0.7f, 0.7f, 0.7f), X + 2.f, Y + HS + 4.f, Font, 0.85f);
		const int32 Id = Worn[w].bSecond ? Skills->RingB : Skills->Worn[Worn[w].Kind];
		if (Id == 0) continue;
		DrawIcon(Id, X + 7.f, Y + 5.f, HS - 14.f);
		if (bOver)
		{
			ItemTip(Id);
			if (bUIClick) Knight->UnequipWorn(Worn[w].Kind, Worn[w].bSecond);
		}
	}

	// ----- Numbers -----
	{
		const float Y0 = PY + 198.f + 2 * (HS + 34.f) + 4.f;
		const FMods M = CombinedMods(Skills);
		HDText(FString::Printf(TEXT("Defence: %d%s"), TotalDefense(Skills, Knight->ShieldActive()), Knight->ShieldActive() ? TEXT("  (shield included)") : TEXT("")), FLinearColor(0.6f, 0.78f, 1.f), EX, Y0, Font, 1.1f);
		const int32 Sel = Skills->SelectedItem();
		if (IsGear(Sel) && IsWeaponKind(KindOf(Sel)))
		{
			int32 A, B;
			HotbarDamage(Skills, Skills->Selected, A, B);
			HDText(FString::Printf(TEXT("In hand: %s    Damage %d-%d"), *ItemName(Sel), A, B), FLinearColor(1.f, 0.65f, 0.55f), EX, Y0 + 26.f, Font, 1.1f);
		}
		FString Extra;
		if (M.CritChance > 0.f) Extra += FString::Printf(TEXT("Critical hits %d%% (+50%%).  "), FMath::RoundToInt(M.CritChance * 100.f));
		if (M.bWild) Extra += TEXT("Wild damage x0.5-2.  ");
		if (M.MaxHealthBonus > 0) Extra += FString::Printf(TEXT("+%d health.  "), M.MaxHealthBonus);
		if (M.FloorDelta != 0 || M.CeilDelta != 0) Extra += FString::Printf(TEXT("Damage floor %+d, ceiling %+d.  "), M.FloorDelta, M.CeilDelta);
		if (!FMath::IsNearlyEqual(M.AttackMul, 1.f)) Extra += FString::Printf(TEXT("Attack %+d%%.  "), FMath::RoundToInt((M.AttackMul - 1.f) * 100.f));
		if (!FMath::IsNearlyEqual(M.DefenseMul, 1.f)) Extra += FString::Printf(TEXT("Defence %+d%%.  "), FMath::RoundToInt((M.DefenseMul - 1.f) * 100.f));
		if (Skills->Worn[K_Boots]) Extra += FString::Printf(TEXT("Stamina x%.2f.  "), BootStaminaMult(VariantOf(Skills->Worn[K_Boots])));
		if (!Extra.IsEmpty()) Wrapped(Extra, EX, Y0 + 54.f, PW - (EX - PX) - 30.f, 0.95f, FLinearColor(0.7f, 0.9f, 0.7f));
	}

	HDText(TEXT("Click gear in your pack to hold or wear it.  Click worn gear to take it off.  Tab to close, K for skills."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 30.f, Font, 1.0f);
}

// ===================================================================================================
// The store: sell materials, fish, gear and arrows; buy wood / bronze / iron gear, arrows, rings and amulets
// ===================================================================================================
void AKnightHUD::DrawShopScreen(AKnightCharacter* Knight)
{
	USkillComponent* Skills = Knight->GetSkills();
	UFont* Font = GEngine->GetMediumFont();
	const float PW = 1190.f, PH = 690.f;
	const float PX = (Canvas->ClipX - PW) * 0.5f, PY = (Canvas->ClipY - PH) * 0.5f;
	const float Now = GetWorld()->GetTimeSeconds();
	Panel(PX, PY, PW, PH);
	HDText(TEXT("THE TRADING POST"), FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, PY + 14.f, Font, 1.8f);
	{
		const FString Bal = FString::Printf(TEXT("%d"), Skills->GetIngots());
		float TW = 0.f, TH = 0.f;
		HDTextSize(Bal, TW, TH, Font, 1.8f);
		HDText(Bal, FLinearColor(1.f, 0.85f, 0.3f), PX + PW - 100.f - TW, PY + 14.f, Font, 1.8f);
		DrawIngot(PX + PW - 92.f, PY + 10.f, 40.f);
	}
	if (CloseBtn(PX, PY, PW)) { Knight->CloseShop(); return; }

	// ----- Sell (left) -----
	HDText(TEXT("SELL"), FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, PY + 62.f, Font, 1.3f);
		static const EItemType Sellable[12] = { EItemType::Logs, EItemType::PineLogs, EItemType::RedwoodLogs, EItemType::CopperOre, EItemType::Coal, EItemType::Diamond, EItemType::Bacon, EItemType::Perch, EItemType::Trout, EItemType::GoldenCarp, EItemType::Hide, EItemType::Bones };
	static const TCHAR* SellNames[12] = { TEXT("Logs"), TEXT("Pine logs"), TEXT("Redwood logs"), TEXT("Copper ore"), TEXT("Coal"), TEXT("Diamond"), TEXT("Raw pork"), TEXT("Perch"), TEXT("Trout"), TEXT("Golden carp"), TEXT("Beast hide"), TEXT("Bones") };
	for (int32 r = 0; r < 12; ++r)
	{
		const EItemType T = Sellable[r];
		const float RY = PY + 94.f + r * 38.f;
		const int32 N = Skills->GetItemCount(T);
		Tile(PX + 30.f, RY, 510.f, 34.f, FLinearColor(0.6f, 0.72f, 0.88f), 0.16f, false);
		DrawIcon(MaterialId(T), PX + 34.f, RY + 1.f, 32.f);
		HDText(SellNames[r], FLinearColor::White, PX + 74.f, RY + 8.f, Font, 1.0f);
		HDText(FString::Printf(TEXT("x %d"), N), FLinearColor(0.85f, 0.85f, 0.85f), PX + 210.f, RY + 8.f, Font, 1.05f);
		DrawIngot(PX + 280.f, RY + 4.f, 24.f);
		HDText(FString::Printf(TEXT("%d each"), SellValue(T)), FLinearColor(1.f, 0.85f, 0.3f), PX + 310.f, RY + 8.f, Font, 1.0f);
		if (Btn(PX + 430.f, RY + 2.f, 104.f, 30.f, TEXT("SELL ALL"), N > 0, false, 1.0f))
		{
			const int32 Pay = Skills->SellAll(T);
			ShopMsg = FString::Printf(TEXT("Sold for %d ingots."), Pay);
			ShopMsgUntil = Now + 3.f;
		}
	}
	HDText(TEXT("Click gear or arrows in your pack to sell them:"), FLinearColor(0.75f, 0.75f, 0.75f), PX + 30.f, PY + 94.f + 10 * 38.f + 6.f, Font, 0.95f);
	for (int32 i = 0; i < NumSlots; ++i)
	{
		const float X = PX + 30.f + (i % 8) * 64.f, Y = PY + 94.f + 10 * 38.f + 32.f + (i / 8) * 62.f;
		const FInvSlot S = Skills->Slots[i];
		const bool bSellable = S.Id != 0 && (IsGear(S.Id) || IsArrow(S.Id));
		const bool bOver = Over(X, Y, 58.f, 58.f);
		Tile(X, Y, 58.f, 58.f, bOver && bSellable ? FLinearColor(0.85f, 0.93f, 1.f) : FLinearColor(0.6f, 0.72f, 0.88f), bOver && bSellable ? 0.38f : 0.2f);
		if (S.Id == 0) continue;
		DrawIcon(S.Id, X + 5.f, Y + 3.f, 48.f);
		if (StackSize(S.Id) > 1) HDText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, X + 36.f, Y + 38.f, Font, 0.85f);
		if (bOver && bSellable)
		{
			const int32 Pay = IsArrow(S.Id) ? S.Count : GearSellPrice(S.Id) * S.Count;
			TArray<FString> Tip;
			Tip.Add(ItemName(S.Id));
			Tip.Add(FString::Printf(TEXT("Click to sell for %d ingots"), Pay));
			SetTip(Tip);
			if (bUIClick)
			{
				Skills->AddIngots(Pay);
				ShopMsg = FString::Printf(TEXT("Sold %s for %d ingots."), *ItemName(S.Id), Pay);
				ShopMsgUntil = Now + 3.f;
				Skills->Slots[i].Id = 0; Skills->Slots[i].Count = 0;
			}
		}
	}

	// ----- Buy (right): this hour's random wares, and the mystery satchel -----
	{
		const float RX = PX + 574.f, RW = PW - 574.f - 30.f;
		HDText(TEXT("TODAY'S WARES"), FLinearColor(1.f, 0.85f, 0.3f), RX, PY + 62.f, Font, 1.3f);
		{
			const int32 Mins = JeebStore::MinutesToRestock();
			const FString Re = Mins <= 0 ? FString(TEXT("Restocking any moment")) : FString::Printf(TEXT("New stock in %d min"), Mins + 1);
			float TW = 0.f, TH = 0.f;
			HDTextSize(Re, TW, TH, Font, 1.0f);
			HDText(Re, FLinearColor(0.75f, 0.75f, 0.75f), RX + RW - TW, PY + 66.f, Font, 1.0f);
		}

		TArray<JeebStore::FWare>& Wares = JeebStore::Stock();
		const float CW = (RW - 3 * 8.f) / 4.f, CH = 142.f;
		for (int32 i = 0; i < Wares.Num(); ++i)
		{
			JeebStore::FWare& W = Wares[i];
			const float CX = RX + (i % 4) * (CW + 8.f), CY = PY + 96.f + (i / 4) * (CH + 8.f);
			const bool bSoldOut = W.Stock <= 0;
			const bool bAfford = Skills->GetIngots() >= W.Price;
			const bool bOver = !bSoldOut && Over(CX, CY, CW, CH);
			Tile(CX, CY, CW, CH, bOver ? FLinearColor(0.85f, 0.93f, 1.f) : FLinearColor(0.6f, 0.72f, 0.88f), bOver ? 0.34f : 0.18f);
			if (i >= 8) DrawRect(FLinearColor(0.75f, 0.55f, 0.15f, 0.9f), CX, CY, CW, 3.f);       // the valuable row gets a gold edge
			DrawIcon(W.Id, CX + 6.f, CY + 6.f, 58.f);
			FString Name = W.Count > 1 ? FString::Printf(TEXT("%d %s"), W.Count, *ItemName(W.Id)) : ItemName(W.Id);
			Wrapped(Name, CX + 70.f, CY + 8.f, CW - 76.f, 0.85f, bSoldOut ? FLinearColor(0.45f, 0.45f, 0.45f) : FLinearColor::White);
			FString Stat;
			if (IsGear(W.Id))
			{
				const int32 K = KindOf(W.Id), V = VariantOf(W.Id);
				if (IsWeaponKind(K)) { int32 A, B; WeaponDamage(K, V, A, B, V); Stat = FString::Printf(TEXT("Damage %d-%d"), A, B); }
				else if (K == K_Shield) Stat = FString::Printf(TEXT("Guard %d"), ShieldGuard(W.Id));
				else if (IsArmorKind(K)) Stat = FString::Printf(TEXT("Defence %d"), DefenseOf(K, V));
				else if (IsAccessoryKind(K)) Stat = AccessoryText(K, V);
			}
			else if (IsArrow(W.Id)) { int32 A, B; WeaponDamage(K_Bow, ArrowTier(W.Id), A, B, ArrowTier(W.Id)); Stat = FString::Printf(TEXT("Bow damage %d-%d"), A, B); }
			else if (IsPotion(W.Id)) { static const TCHAR* S[NumPotions] = { TEXT("Speed for 8 s"), TEXT("Heals you fully"), TEXT("+50% health, 1 min") }; Stat = S[PotionType(W.Id)]; }
			if (!Stat.IsEmpty()) Wrapped(Stat, CX + 8.f, CY + 70.f, CW - 16.f, 0.78f, FLinearColor(0.75f, 0.8f, 0.95f));
			DrawIngot(CX + 8.f, CY + CH - 30.f, 22.f);
			HDText(FString::Printf(TEXT("%d"), W.Price), bAfford ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor(0.9f, 0.3f, 0.25f), CX + 36.f, CY + CH - 28.f, Font, 1.1f);
			const FString Left = bSoldOut ? FString(TEXT("Sold out")) : FString::Printf(TEXT("%d left"), W.Stock);
			float TW = 0.f, TH = 0.f;
			HDTextSize(Left, TW, TH, Font, 0.85f);
			HDText(Left, bSoldOut ? FLinearColor(0.9f, 0.35f, 0.3f) : FLinearColor(0.7f, 0.7f, 0.7f), CX + CW - TW - 8.f, CY + CH - 25.f, Font, 0.85f);
			if (bOver)
			{
				ItemTip(W.Id);
				if (bUIClick)
				{
					if (!bAfford) ShopMsg = TEXT("Not enough ingots.");
					else if (!Skills->CanAdd(W.Id, W.Count)) ShopMsg = TEXT("Your pack is full.");
					else
					{
						Skills->SpendIngots(W.Price);
						Skills->AddInvItem(W.Id, W.Count);
						--W.Stock;
						ShopMsg = FString::Printf(TEXT("Bought %s. It is in your pack (Tab)."), *Name);
					}
					ShopMsgUntil = Now + 3.f;
				}
			}
		}

		// The mystery satchel
		{
			const float BY = PY + 96.f + 3 * (CH + 8.f) + 4.f, BH = 74.f;
			const int64 Hour = JeebStore::HourNow();
			const bool bUsed = Skills->LastGambleHour == Hour;
			const bool bAfford = Skills->GetIngots() >= JeebStore::GamblePrice;
			Tile(RX, BY, RW, BH, FLinearColor(0.75f, 0.45f, 0.9f), 0.22f);
			DrawRect(FLinearColor(0.70f, 0.40f, 0.85f, 0.9f), RX, BY, RW, 3.f);
			// a little drawstring sack
			Disc(RX + 38.f, BY + 44.f, 22.f, 20.f, FLinearColor(0.55f, 0.36f, 0.18f));
			Disc(RX + 38.f, BY + 20.f, 9.f, 5.f, FLinearColor(0.45f, 0.28f, 0.14f));
			HDText(TEXT("?"), FLinearColor(1.f, 0.9f, 0.5f), RX + 33.f, BY + 32.f, Font, 1.3f);
			HDText(TEXT("MYSTERY SATCHEL"), FLinearColor(0.95f, 0.8f, 1.f), RX + 76.f, BY + 10.f, Font, 1.15f);
			HDText(bUsed ? FString::Printf(TEXT("Opened this hour. Another in %d min."), JeebStore::MinutesToRestock() + 1)
			               : FString::Printf(TEXT("Once an hour, %d ingots. Hover for the odds."), JeebStore::GamblePrice),
				FLinearColor(0.8f, 0.8f, 0.8f), RX + 76.f, BY + 38.f, Font, 0.9f);
			if (Over(RX, BY, RW - 140.f, BH))
			{
				TArray<FString> Tip;
				Tip.Add(TEXT("What the satchel might hold"));
				for (const JeebStore::FOdds& O : JeebStore::GambleTable())
					Tip.Add(FString::Printf(TEXT("%5.1f%%   %s"), O.Percent, O.Label));
				SetTip(Tip);
			}
			if (Btn(RX + RW - 128.f, BY + 20.f, 116.f, 36.f, TEXT("OPEN"), !bUsed && bAfford, false, 1.1f))
			{
				FRandomStream R(static_cast<int32>(FPlatformTime::Cycles()));
				int32 Id = 0, Count = 0;
				const int32 Row = JeebStore::RollGamble(R, Id, Count);
				if (Id != 0 && !Skills->CanAdd(Id, Count)) ShopMsg = TEXT("Make room in your pack first.");
				else
				{
					Skills->SpendIngots(JeebStore::GamblePrice);
					Skills->LastGambleHour = Hour;
					if (Id == 0) Skills->AddIngots(Count);
					else Skills->AddInvItem(Id, Count);
					const FString Got = Id == 0 ? FString::Printf(TEXT("%d ingots"), Count) : (Count > 1 ? FString::Printf(TEXT("%d %s"), Count, *ItemName(Id)) : ItemName(Id));
					ShopMsg = FString::Printf(TEXT("The satchel holds... %s!  (a %.1f%% chance)"), *Got, JeebStore::GambleTable()[Row].Percent);
				}
				ShopMsgUntil = Now + 6.f;
			}
		}
		if (Now < ShopMsgUntil) HDText(ShopMsg, FLinearColor(0.6f, 1.f, 0.6f), RX, PY + PH - 30.f, Font, 1.05f);
	}
	HDText(TEXT("Press E or X to leave."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 30.f, Font, 1.0f);
}
// ===================================================================================================
// The smithy: turn materials into gear
// ===================================================================================================
void AKnightHUD::DrawSmithScreen(AKnightCharacter* Knight)
{
	USkillComponent* Skills = Knight->GetSkills();
	UFont* Font = GEngine->GetMediumFont();
	const float PW = 1190.f, PH = 690.f;
	const float PX = (Canvas->ClipX - PW) * 0.5f, PY = (Canvas->ClipY - PH) * 0.5f;
	const float Now = GetWorld()->GetTimeSeconds();
	Panel(PX, PY, PW, PH);
	HDText(TEXT("THE SMITHY"), FLinearColor(1.f, 0.6f, 0.25f), PX + 30.f, PY + 14.f, Font, 1.8f);
	if (CloseBtn(PX, PY, PW)) { Knight->CloseSmith(); return; }

	const int32 Logs = Skills->CountWood();
	const int32 Copper = Skills->GetItemCount(EItemType::CopperOre);
	const int32 Coal = Skills->GetItemCount(EItemType::Coal);
	const int32 Gems = Skills->GetItemCount(EItemType::Diamond);

	// Materials you carry, and your smithing level
	{
		struct FMat { EItemType T; const TCHAR* N; int32 Have; };
		const FMat Mats[4] = { { EItemType::Logs, TEXT("Logs"), Logs }, { EItemType::CopperOre, TEXT("Copper"), Copper }, { EItemType::Coal, TEXT("Coal"), Coal }, { EItemType::Diamond, TEXT("Diamond"), Gems } };
		for (int32 m = 0; m < 4; ++m)
		{
			const float X = PX + 30.f + m * 150.f;
			DrawIcon(MaterialId(Mats[m].T), X, PY + 58.f, 34.f);
			HDText(FString::Printf(TEXT("%s  %d"), Mats[m].N, Mats[m].Have), FLinearColor::White, X + 40.f, PY + 66.f, Font, 1.1f);
		}
		int32 Lv, Into, Need;
		Skills->GetProgress(ESkillType::Smithing, Lv, Into, Need);
		HDText(FString::Printf(TEXT("Smithing  Lv %d"), Lv), FLinearColor(1.f, 0.7f, 0.4f), PX + PW - 330.f, PY + 56.f, Font, 1.2f);
		Gauge(PX + PW - 330.f, PY + 86.f, 290.f, 9.f, Need > 0 ? float(Into) / float(Need) : 0.f, FLinearColor(0.9f, 0.4f, 0.2f));
	}

	// Kind list (two columns)
	static const int32 Kinds[18] = { K_Sword, K_Axe, K_Pickaxe, K_Spear, K_Broadsword, K_BattleAxe, K_Katana, K_Wand, K_Staff, K_Bow, K_Rod, K_Shield, K_Helmet, K_Chest, K_Shoulders, K_Gloves, K_Boots, -1 };
	SmithKindIdx = FMath::Clamp(SmithKindIdx, 0, 17);
	for (int32 i = 0; i < 18; ++i)
	{
		const float X = PX + 30.f + (i / 9) * 160.f, Y = PY + 118.f + (i % 9) * 44.f;
		const FString Nm = Kinds[i] < 0 ? FString(TEXT("Arrows")) : FString(KindName(Kinds[i]));
		if (Btn(X, Y, 150.f, 38.f, Nm, true, SmithKindIdx == i, 1.0f)) SmithKindIdx = i;
	}
	const int32 Kind = Kinds[SmithKindIdx];
	const bool bArrows = Kind < 0;
	const int32 BKind = bArrows ? K_Bow : Kind;

	const float CW = 150.f, CH = 500.f, CGap = 10.f;
	const float CX0 = PX + 370.f, CY = PY + 112.f;
	for (int32 t = 0; t < NumTiers; ++t)
	{
		const float CX = CX0 + t * (CW + CGap);
		const FRecipe R = SmithRecipe(BKind, t, bArrows);
		const int32 Id = bArrows ? ArrowId(t) : GearId(Kind, t);
		Tile(CX, CY, CW, CH, FLinearColor(0.6f, 0.72f, 0.88f), 0.18f);
		DrawRect(Palette(t, 0), CX, CY, CW, 5.f);
		DrawIcon(Id, CX + 25.f, CY + 14.f, 100.f);
		float Y = CY + 120.f;
		HDText(bArrows ? FString::Printf(TEXT("%s Arrows"), TierName(t)) : ItemName(Id), FLinearColor(1.f, 0.9f, 0.6f), CX + 8.f, Y, Font, 1.05f);
		Y += 26.f;
		if (bArrows)
		{
			int32 A, B; WeaponDamage(K_Bow, t, A, B, t);
			HDText(FString::Printf(TEXT("Makes %d arrows"), ArrowBundle), FLinearColor(0.9f, 0.9f, 0.9f), CX + 8.f, Y, Font, 0.95f); Y += 20.f;
			HDText(FString::Printf(TEXT("Bow damage %d-%d"), A, B), FLinearColor(1.f, 0.6f, 0.5f), CX + 8.f, Y, Font, 0.95f); Y += 26.f;
		}
		else if (IsWeaponKind(Kind))
		{
			int32 A, B; WeaponDamage(Kind, t, A, B, t);
			HDText(FString::Printf(TEXT("Damage %d-%d"), A, B), FLinearColor(1.f, 0.6f, 0.5f), CX + 8.f, Y, Font, 0.95f); Y += 20.f;
			if (!AllowsShield(Kind)) { HDText(TEXT("Two-handed"), FLinearColor(0.7f, 0.7f, 0.7f), CX + 8.f, Y, Font, 0.85f); }
			Y += 20.f;
		}
		else
		{
			HDText(FString::Printf(TEXT("Defence %d"), DefenseOf(Kind, t)), FLinearColor(0.6f, 0.78f, 1.f), CX + 8.f, Y, Font, 0.95f); Y += 20.f;
			if (Kind == K_Boots) HDText(FString::Printf(TEXT("Stamina x%.2f"), BootStaminaMult(t)), FLinearColor(0.6f, 0.9f, 0.6f), CX + 8.f, Y, Font, 0.85f);
			Y += 20.f;
		}

		// Materials: have / need
		HDText(TEXT("MATERIALS"), FLinearColor(0.65f, 0.65f, 0.65f), CX + 8.f, Y, Font, 0.85f); Y += 20.f;
		bool bHaveAll = true;
		auto MatLine = [&](EItemType Ty, const TCHAR* Name, int32 Have, int32 Need)
		{
			if (Need <= 0) return;
			const bool bOk = Have >= Need;
			if (!bOk) bHaveAll = false;
			DrawIcon(MaterialId(Ty), CX + 6.f, Y - 3.f, 24.f);
			HDText(FString::Printf(TEXT("%s %d/%d"), Name, Have, Need), bOk ? FLinearColor(0.7f, 1.f, 0.7f) : FLinearColor(1.f, 0.45f, 0.4f), CX + 34.f, Y, Font, 0.95f);
			Y += 26.f;
		};
		MatLine(EItemType::Logs, TEXT("Logs"), Logs, R.Logs);
		MatLine(EItemType::CopperOre, TEXT("Copper"), Copper, R.Copper);
		MatLine(EItemType::Coal, TEXT("Coal"), Coal, R.Coal);
		MatLine(EItemType::Diamond, TEXT("Diamond"), Gems, R.Diamonds);

		const int32 Lv = Skills->GetLevel(ESkillType::Smithing);
		const bool bLevelOk = Lv >= R.Level;
		Y += 6.f;
		HDText(FString::Printf(TEXT("Needs Smithing %d"), R.Level), bLevelOk ? FLinearColor(0.7f, 1.f, 0.7f) : FLinearColor(1.f, 0.45f, 0.4f), CX + 8.f, Y, Font, 0.95f); Y += 24.f;
		HDText(FString::Printf(TEXT("+%d XP"), R.XP), FLinearColor(0.5f, 0.85f, 1.f), CX + 8.f, Y, Font, 0.95f);

		const bool bCan = bHaveAll && bLevelOk;
		if (Btn(CX + 10.f, CY + CH - 50.f, CW - 20.f, 38.f, TEXT("SMITH"), bCan, false, 1.1f))
		{
			const int32 Count = bArrows ? ArrowBundle : 1;
			if (!Skills->CanAdd(Id, Count)) ShopMsg = TEXT("Your pack is full.");
			else
			{
				if (R.Logs > 0) Skills->TakeWood(R.Logs);
				if (R.Copper > 0) Skills->RemoveItems(MaterialId(EItemType::CopperOre), R.Copper);
				if (R.Coal > 0) Skills->RemoveItems(MaterialId(EItemType::Coal), R.Coal);
				if (R.Diamonds > 0) Skills->RemoveItems(MaterialId(EItemType::Diamond), R.Diamonds);
				Skills->AddInvItem(Id, Count);
				Skills->AddXP(ESkillType::Smithing, R.XP);
				ShopMsg = FString::Printf(TEXT("Smithed %s. It is in your pack (Tab)."), bArrows ? *FString::Printf(TEXT("%d %s arrows"), ArrowBundle, TierName(t)) : *ItemName(Id));
			}
			ShopMsgUntil = Now + 3.f;
		}
		if (Over(CX, CY, CW, CH - 56.f) && !bArrows) ItemTip(Id);
	}
	if (Now < ShopMsgUntil) HDText(ShopMsg, FLinearColor(0.6f, 1.f, 0.6f), PX + 370.f, PY + PH - 64.f, Font, 1.1f);
	HDText(TEXT("Wood uses logs, bronze copper, iron coal, steel coal and copper, diamond diamonds and coal.   Press E or X to leave."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 30.f, Font, 0.95f);
}

// ===================================================================================================
// Dialogue box
// ===================================================================================================
void AKnightHUD::DrawDialogue(AKnightCharacter* Knight)
{
	FString Name, Line;
	bool bLast = false;
	if (!Knight->GetDialogue(Name, Line, bLast)) return;
	UFont* Font = GEngine->GetMediumFont();
		// Just words along the bottom of the screen, with a dark edge so they read over anything
	const float W = FMath::Min(1300.f, Canvas->ClipX - 80.f), H = 300.f;   // large, easy-to-read conversation text
	const float X = (Canvas->ClipX - W) * 0.5f, Y = Canvas->ClipY - H - 110.f;
	auto Shadowed = [&](const FString& Txt, float Tx, float Ty, float Sc, const FLinearColor& Col)
	{
		HDText(Txt, FLinearColor(0.f, 0.f, 0.f, 0.9f), Tx + 2.f, Ty + 2.f, Font, Sc);
		HDText(Txt, FLinearColor(0.f, 0.f, 0.f, 0.9f), Tx - 1.f, Ty + 1.f, Font, Sc);
		HDText(Txt, Col, Tx, Ty, Font, Sc);
	};
	Shadowed(Name, X + 26.f, Y + 10.f, 2.4f, FLinearColor(1.f, 0.85f, 0.3f));
	// The words start a little left of the name and wrap at about the middle of the screen, so they stay on the left
	const float LX = X + 8.f, LW = FMath::Max(300.f, Canvas->ClipX * 0.49f - LX);
	Wrapped(Line, LX + 2.f, Y + 82.f, LW, 2.0f, FLinearColor(0.f, 0.f, 0.f, 0.9f));
	Wrapped(Line, LX, Y + 80.f, LW, 2.0f, FLinearColor(0.97f, 0.97f, 0.97f));
	if (bUIClick && Over(X, Y, W, H)) Knight->AdvanceDialogue();   // (E or a click moves on; no hint shown)
}


// ===================================================================================================
// Item badges: what a piece adds, in small numbers on its icon. Red (lower left) = offence, blue (lower right) = defence.
// A weapon just shows its damage (the middle of its range, no plus sign); armour and shields show +defence;
// rings and amulets show what they shift (+15% attack, +2 damage, +10% defence, +20 health ...).
// ===================================================================================================
void AKnightHUD::ItemBadges(int32 Id, float X, float Y, float S)
{
	if (!IsGear(Id)) return;
	UFont* Font = GEngine->GetMediumFont();
	const int32 K = KindOf(Id), V = VariantOf(Id);
	FString Off, Def;
	if (IsWeaponKind(K))
	{
		int32 A, B;
		WeaponDamage(K, V, A, B, V);
		Off = FString::FromInt(FMath::RoundToInt(0.5f * (A + B)));
	}
	else if (K == K_Shield || IsArmorKind(K))
	{
		const int32 D = (K == K_Shield) ? ShieldGuard(Id) : DefenseOf(K, V);
		if (D > 0) Def = FString::Printf(TEXT("+%d"), D);
	}
	else if (IsAccessoryKind(K))
	{
		const FMods M = AccessoryMods(K, V);
		const int32 AtkPct = FMath::RoundToInt((M.AttackMul - 1.f) * 100.f), DefPct = FMath::RoundToInt((M.DefenseMul - 1.f) * 100.f);
		if (AtkPct != 0) Off = FString::Printf(TEXT("%+d%%"), AtkPct);
		else if (M.FloorDelta != 0 || M.CeilDelta != 0) Off = FString::Printf(TEXT("%+d"), FMath::RoundToInt(0.5f * (M.FloorDelta + M.CeilDelta)));
		else if (M.CritChance > 0.f) Off = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(M.CritChance * 100.f));
		else if (M.bWild) Off = TEXT("x2");
		if (DefPct != 0) Def = FString::Printf(TEXT("%+d%%"), DefPct);
		else if (M.MaxHealthBonus > 0) Def = FString::Printf(TEXT("+%d"), M.MaxHealthBonus);
	}
	const float Sc = FMath::Clamp(S / 42.f, 0.85f, 1.3f);   // big enough to read, small enough for two on one icon
	auto Badge = [&](const FString& T, bool bRight, const FLinearColor& Col)
	{
		if (T.IsEmpty()) return;
		float W = 0.f, H = 0.f;
		HDTextSize(T, W, H, Font, Sc);
		const float Tx = bRight ? X + S - W - 3.f : X + 3.f, Ty = Y + S - H - 1.f;
		HDText(T, FLinearColor(0.f, 0.f, 0.f, 0.85f), Tx + 1.f, Ty + 1.f, Font, Sc);   // a dark edge so it reads over any icon
		HDText(T, Col, Tx, Ty, Font, Sc);
	};
	Badge(Off, false, FLinearColor(1.f, 0.38f, 0.32f));
	Badge(Def, true, FLinearColor(0.45f, 0.72f, 1.f));
}

// ===================================================================================================
// The side panel (lower right, always there, like RuneScape's): three tabs, Inventory, Armor and Skills.
// Tab frees the mouse cursor to use it; Caps Lock moves to the next tab. Without the cursor it is just a display.
// ===================================================================================================
void AKnightHUD::DrawSidePanel(AKnightCharacter* Knight)
{
	// Three windows. The inventory is the one that can stay on screen all the time (lower right). Tab frees the cursor and brings
	// up the other two: Armor just above the inventory, and Skills down the left side of the screen. With the HUD setting off,
	// nothing is shown until Tab, and then all three appear.
	USkillComponent* Skills = Knight->GetSkills();
	UFont* Font = GEngine->GetMediumFont();
	const bool bCursor = Knight->IsPanelCursor();
	const bool bLive = bCursor && bUIHaveMouse;   // clicks and tooltips only while the cursor is out
	auto Hot = [&](float X, float Y, float W, float H) { return bLive && Over(X, Y, W, H); };
	RectCount = 0;
	auto AddRect = [&](float X, float Y, float W, float H) { if (RectCount < 8) { RectX[RectCount] = X; RectY[RectCount] = Y; RectW[RectCount] = W; RectH[RectCount] = H; ++RectCount; } };

	const float SS = 44.f, Gap = 5.f, Pad = 10.f;
	const float GridW = 5 * SS + 4 * Gap, GridH = 5 * SS + 4 * Gap;   // a 5 x 5 pack
	const float PW = GridW + 2.f * Pad;
	bGreyGlass = true;
	const FLinearColor Idle(0.62f, 0.63f, 0.66f), Hover(0.86f, 0.87f, 0.9f);
	auto SlotTile = [&](float X, float Y, float W, float H, bool bOver)
	{
		Tile(X, Y, W, H, bOver ? Hover : Idle, bOver ? 0.24f : 0.11f);
	};

	// ================= Inventory (lower right) =================
	const float IH = Pad + GridH + Pad;
	const float IX = Canvas->ClipX - PW - 16.f, IY = Canvas->ClipY - IH - 16.f;
	PanelTop = IY;
	{
		Panel(IX, IY, PW, IH, 1.f, false);
		AddRect(IX, IY, PW, IH);
		const float GX = IX + Pad, GY = IY + Pad;
		// the top-left cell always holds your ingots (it cannot be changed); then the 24-slot pack
		InvIngotX = GX; InvIngotY = GY;
		{
			const bool bOver = Hot(GX, GY, SS, SS);
			Tile(GX, GY, SS, SS, FLinearColor(1.f, 0.85f, 0.45f), bOver ? 0.26f : 0.16f);
			DrawIngot(GX + 7.f, GY + 4.f, SS - 14.f);
			const FString N = FString::FromInt(Skills->GetIngots());
			float NW = 0.f, NH = 0.f;
			HDTextSize(N, NW, NH, Font, 1.0f);
			HDText(N, FLinearColor(0.f, 0.f, 0.f, 0.85f), GX + (SS - NW) * 0.5f + 1.f, GY + SS - NH, Font, 1.0f);
			HDText(N, FLinearColor(1.f, 0.88f, 0.4f), GX + (SS - NW) * 0.5f, GY + SS - NH - 1.f, Font, 1.0f);
			if (bOver) SetTip({ TEXT("Ingots"), FString::Printf(TEXT("%d ingots: spend them at the store and the smithy"), Skills->GetIngots()) });
		}
		for (int32 i = 0; i < NumSlots; ++i)
		{
			const int32 Cell = i + 1;
			const float X = GX + (Cell % 5) * (SS + Gap), Y = GY + (Cell / 5) * (SS + Gap);
			InvCellX[i] = X; InvCellY[i] = Y; InvCellS = SS; InvCellCount = i + 1;
			const bool bOver = Hot(X, Y, SS, SS);
			const FInvSlot S = Skills->Slots[i];
			SlotTile(X, Y, SS, SS, bOver);
			if (S.Id == 0) continue;
			if (!(DragKind == 1 && bDragging && DragIdx == i)) DrawIcon(S.Id, X + 4.f, Y + 3.f, SS - 8.f);   // the picked-up item leaves its cell
			if (StackSize(S.Id) > 1) HDText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, X + SS - 22.f, Y + 2.f, Font, 0.75f);
			ItemBadges(S.Id, X, Y, SS);
			if (bOver) ItemTip(S.Id);
		}
	}
	if (!bCursor) { bGreyGlass = false; return; }

	// ================= Armor (just above the inventory) =================
	{
		// Laid out like the knight himself: helmet on top; gloves, chest and shoulders across the middle; boots below,
		// with the cape beside them (the shield lives in the slot at the end of the weapon bar). The three miscellaneous slots run down the right edge.
		struct FWornSlot { int32 Kind; bool bSecond; int32 Col, Row; };
		static const FWornSlot Worn[9] = {
			{ K_Helmet, false, 1, 0 },
			{ K_Gloves, false, 0, 1 }, { K_Chest, false, 1, 1 }, { K_Shoulders, false, 2, 1 },
			{ K_Boots, false, 1, 2 }, { K_Cape, false, 2, 2 },
			{ K_Ring, false, 3, 0 }, { K_Ring, true, 3, 1 }, { K_Amulet, false, 3, 2 },
		};
		const float WS = 50.f, WG = (GridW - 4 * WS) / 3.f, RowH = WS + 8.f, BoxH = 36.f;
		const float AH = Pad + 3 * RowH + 2.f + BoxH + Pad;
		const float AX = IX, AY = IY - AH - 10.f;
		ArmorTopY = AY;
		Panel(AX, AY, PW, AH, 1.f, false);
		AddRect(AX, AY, PW, AH);
		const float GX = AX + Pad, GY = AY + Pad;
		for (const FWornSlot& Ws : Worn)
		{
			const float X = GX + Ws.Col * (WS + WG) + (Ws.Col == 3 ? 0.f : -2.f), Y = GY + Ws.Row * RowH;
			WornCellX[WornCellCount] = X; WornCellY[WornCellCount] = Y; WornCellS = WS; WornKind[WornCellCount] = Ws.Kind; WornSecond[WornCellCount] = Ws.bSecond; ++WornCellCount;
			const bool bOver = Hot(X, Y, WS, WS);
			SlotTile(X, Y, WS, WS, bOver);
			const int32 Id = Ws.bSecond ? Skills->RingB : Skills->Worn[Ws.Kind];
			if (Id == 0) continue;
			if (!(DragKind == 4 && bDragging && DragIdx == WornCellCount - 1)) DrawIcon(Id, X + 4.f, Y + 3.f, WS - 8.f);   // a piece being dragged leaves its slot
			ItemBadges(Id, X, Y, WS);
			if (bOver) ItemTip(Id);
		}
		// Totals: offence is what the weapon in hand deals with every bonus counted (the middle of its range);
		// defence is your armour with every bonus counted, plus your shield's guard when one is ready.
		const float TY = GY + 3 * RowH + 2.f, Half = GridW * 0.5f;
		int32 A = 0, B = 0;
		const bool bWeapon = HotbarDamage(Skills, Skills->Selected, A, B);
		const int32 Offence = bWeapon ? FMath::RoundToInt(0.5f * (A + B)) : 0;
		const int32 Defence = TotalDefense(Skills, false);
		const int32 Guard = (Skills->Worn[K_Shield] && Knight->ShieldActive()) ? ShieldGuard(Skills->Worn[K_Shield]) : 0;
		const int32 Held = Skills->SelectedItem();
		const int32 OffIcon = (Held && IsGear(Held)) ? Held : GearId(K_Sword, 1);
		const int32 DefIcon = Skills->Worn[K_Shield] ? Skills->Worn[K_Shield] : GearId(K_Shield, 1);
		auto Total = [&](float BX, int32 Icon, const FString& Num, const FLinearColor& Col, const FLinearColor& Tint)
		{
			const float BW = Half - 3.f;
			Tile(BX, TY, BW, BoxH, Tint, 0.12f, false);
			float NW = 0.f, NH = 0.f;
			HDTextSize(Num, NW, NH, Font, 1.7f);
			const float IconS = 28.f, GroupW = IconS + 6.f + NW, SX = BX + (BW - GroupW) * 0.5f;
			DrawIcon(Icon, SX, TY + (BoxH - IconS) * 0.5f, IconS);
			HDText(Num, Col, SX + IconS + 6.f, TY + (BoxH - NH) * 0.5f, Font, 1.7f);
		};
		Total(GX, OffIcon, FString::FromInt(Offence), FLinearColor(1.f, 0.42f, 0.36f), FLinearColor(1.f, 0.45f, 0.4f));
		Total(GX + Half + 3.f, DefIcon, Guard > 0 ? FString::Printf(TEXT("%d+%d"), Defence, Guard) : FString::FromInt(Defence), FLinearColor(0.5f, 0.75f, 1.f), FLinearColor(0.45f, 0.65f, 1.f));
		if (Hot(GX, TY, Half, BoxH)) SetTip({ TEXT("Total offence"), FString::Printf(TEXT("Your weapon deals %d - %d, all bonuses counted"), A, B) });
		if (Hot(GX + Half, TY, Half, BoxH)) SetTip({ TEXT("Total defence"), FString::Printf(TEXT("Armour %d, all bonuses counted"), Defence),
			Guard > 0 ? FString::Printf(TEXT("Shield guard %d (stops that much while raised)"), Guard) : FString(TEXT("No shield ready")) });
	}

	// ================= Skills (down the left side of the screen) =================
	{
		// Only the skills you have discovered. The window grows to fit: up to 5 across, then more rows.
		static const ESkillType Order[9] = { ESkillType::Attack, ESkillType::Defense, ESkillType::Archery, ESkillType::Magic,
			ESkillType::Mining, ESkillType::Woodcutting, ESkillType::Fishing, ESkillType::Smithing, ESkillType::Lockpicking };
		TArray<ESkillType> Shown;
		for (ESkillType T : Order) if (Skills->IsKnown(T)) Shown.Add(T);
		const int32 N = FMath::Max(1, Shown.Num());
		const int32 Cols = FMath::Min(N, 5), Rows = (N + 4) / 5;
		const float CS = 54.f;   // a skill tile: icon, level, a thin progress bar
		const float SW = Pad * 2.f + Cols * CS + (Cols - 1) * Gap, SH = Pad * 2.f + Rows * CS + (Rows - 1) * Gap;
		const float SX0 = 16.f, SY0 = FMath::Max(270.f, Canvas->ClipY * 0.5f - SH * 0.5f);   // clear of the map
		Panel(SX0, SY0, SW, SH, 1.f, false);
		AddRect(SX0, SY0, SW, SH);
		for (int32 i = 0; i < Shown.Num(); ++i)
		{
			const ESkillType T = Shown[i];
			const float X = SX0 + Pad + (i % 5) * (CS + Gap), Y = SY0 + Pad + (i / 5) * (CS + Gap);
			const bool bOver = Hot(X, Y, CS, CS);
			SlotTile(X, Y, CS, CS, bOver);
			int32 Lv, Into, Need;
			Skills->GetProgress(T, Lv, Into, Need);
			DrawSkillIcon(static_cast<int32>(T), X + 4.f, Y + 3.f, CS - 22.f);
			float LW = 0.f, LH = 0.f;
			const FString LvS = FString::FromInt(Lv);
			HDTextSize(LvS, LW, LH, Font, 1.2f);
			HDText(LvS, FLinearColor(1.f, 0.9f, 0.5f), X + CS - LW - 4.f, Y + CS - LH - 9.f, Font, 1.2f);
			const float F = Need > 0 ? FMath::Clamp((float)Into / (float)Need, 0.f, 1.f) : 0.f;
			Gauge(X + 6.f, Y + CS - 7.f, CS - 12.f, 3.f, F, FLinearColor(0.35f, 0.8f, 0.3f));
			if (bOver)
				SetTip({ USkillComponent::SkillName(T), FString::Printf(TEXT("Level %d"), Lv), FString::Printf(TEXT("%d / %d XP  (%d to level %d)"), Into, Need, FMath::Max(0, Need - Into), Lv + 1) });
		}
	}
	if (Knight->GetLootTarget()) DrawLootWindows(Knight, IX, IY, PW);   // an open sack, to the left of your windows
	bGreyGlass = false;
}

// ---------------------------------------------------------------------------------------------------
// Skills screen (K): one icon per skill in a grid, the level on top of it, a thin bar for progress
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawSkillIcon(int32 Idx, float X, float Y, float S)
{
	PolyStep = FMath::Clamp(S / 44.f, 1.f, 2.5f);
	const float U = S / 64.f;
	auto P = [&](float A, float B) { return FVector2D(X + A * U, Y + B * U); };
	auto Quad = [&](float x0, float y0, float x1, float y1, const FLinearColor& C) { Poly({ P(x0, y0), P(x1, y0), P(x1, y1), P(x0, y1) }, C); };
	auto D = [&](float Cx, float Cy, float Rx, float Ry, const FLinearColor& C) { Disc(X + Cx * U, Y + Cy * U, Rx * U, Ry * U, C); };
	auto Ln = [&](float x0, float y0, float x1, float y1, const FLinearColor& C, float W) { DrawLine(X + x0 * U, Y + y0 * U, X + x1 * U, Y + y1 * U, C, W * U); };

	switch (Idx)
	{
	case 0:   // Mining: a diamond
	{
		const FLinearColor Lt(0.65f, 0.95f, 1.f), Md(0.25f, 0.70f, 0.95f), Dk(0.12f, 0.42f, 0.75f);
		Poly({ P(18, 8), P(46, 8), P(58, 24), P(6, 24) }, Lt);
		Poly({ P(6, 24), P(58, 24), P(32, 58) }, Md);
		Poly({ P(6, 24), P(24, 24), P(32, 58) }, Dk);
		Poly({ P(24, 24), P(40, 24), P(32, 58) }, FLinearColor(0.4f, 0.82f, 1.f));
		Poly({ P(18, 8), P(26, 8), P(22, 24), P(14, 24) }, FLinearColor(0.9f, 1.f, 1.f));
		break;
	}
	case 1:   // Woodcutting: a log
	{
		const FLinearColor Bark(0.42f, 0.25f, 0.10f), Cut(0.80f, 0.62f, 0.36f);
		Quad(6, 22, 46, 46, Bark);
		Quad(6, 22, 46, 27, FLinearColor(0.52f, 0.32f, 0.14f));
		D(46, 34, 10, 12, Bark);
		D(48, 34, 9, 11, Cut);
		D(48, 34, 6, 7.5f, FLinearColor(0.68f, 0.48f, 0.24f));
		D(48, 34, 3, 4, Cut);
		Quad(14, 30, 36, 31.5f, FLinearColor(0.30f, 0.17f, 0.07f));
		break;
	}
	case 2:   // Smithing: an anvil
	{
		const FLinearColor Iron(0.45f, 0.46f, 0.52f), Dark(0.25f, 0.26f, 0.30f);
		Poly({ P(4, 20), P(60, 20), P(52, 34), P(14, 34) }, Iron);
		Poly({ P(4, 20), P(60, 20), P(60, 24), P(4, 24) }, FLinearColor(0.7f, 0.72f, 0.78f));
		Quad(22, 34, 42, 46, Dark);
		Poly({ P(14, 46), P(50, 46), P(56, 58), P(8, 58) }, Iron);
		Poly({ P(56, 20), P(64, 20), P(56, 28) }, Iron);
		D(46, 12, 2.5f, 2.5f, FLinearColor(1.f, 0.7f, 0.2f));
		D(38, 8, 2, 2, FLinearColor(1.f, 0.85f, 0.4f));
		break;
	}
	case 3:   // Fishing: a fish
	{
		D(28, 32, 22, 13, FLinearColor(0.30f, 0.62f, 0.90f));
		D(28, 38, 18, 7, FLinearColor(0.75f, 0.88f, 1.f));
		Poly({ P(46, 32), P(62, 18), P(58, 32), P(62, 46) }, FLinearColor(0.20f, 0.45f, 0.78f));
		Poly({ P(22, 20), P(32, 8), P(36, 22) }, FLinearColor(0.20f, 0.45f, 0.78f));
		D(15, 29, 3.2f, 3.2f, FLinearColor::White);
		D(14.5f, 29, 1.6f, 1.6f, FLinearColor(0.02f, 0.02f, 0.05f));
		Ln(24, 26, 24, 38, FLinearColor(0.15f, 0.35f, 0.65f), 1.5f);
		break;
	}
	case 4:   // Attack: a sword
	{
		Poly({ P(32, 2), P(38, 14), P(36, 42), P(28, 42), P(26, 14) }, FLinearColor(0.82f, 0.85f, 0.92f));
		Quad(31, 4, 33, 42, FLinearColor(0.55f, 0.58f, 0.66f));
		Quad(14, 42, 50, 48, FLinearColor(0.95f, 0.72f, 0.15f));
		Quad(29, 48, 35, 58, FLinearColor(0.40f, 0.22f, 0.08f));
		D(32, 61, 4.5f, 3.5f, FLinearColor(0.95f, 0.72f, 0.15f));
		break;
	}
	case 5:   // Defense: a shield
	{
		Poly({ P(8, 8), P(56, 8), P(56, 34), P(32, 60), P(8, 34) }, FLinearColor(0.60f, 0.64f, 0.74f));
		Poly({ P(13, 13), P(51, 13), P(51, 33), P(32, 54), P(13, 33) }, FLinearColor(0.20f, 0.32f, 0.72f));
		Quad(29, 15, 35, 46, FLinearColor(0.95f, 0.85f, 0.3f));
		Quad(17, 24, 47, 30, FLinearColor(0.95f, 0.85f, 0.3f));
		break;
	}
	case 6:   // Archery: a bow with an arrow
	{
		const FLinearColor Wd(0.55f, 0.34f, 0.14f);
		for (int32 k = 0; k < 8; ++k)
		{
			const float a0 = -1.2f + k * 0.3f, a1 = a0 + 0.3f;
			Ln(22 + 24 * FMath::Cos(a0), 32 + 28 * FMath::Sin(a0), 22 + 24 * FMath::Cos(a1), 32 + 28 * FMath::Sin(a1), Wd, 4.f);
		}
		Ln(22 + 24 * FMath::Cos(-1.2f), 32 + 28 * FMath::Sin(-1.2f), 22 + 24 * FMath::Cos(1.2f), 32 + 28 * FMath::Sin(1.2f), FLinearColor(0.9f, 0.9f, 0.85f), 1.5f);
		Ln(14, 32, 58, 32, FLinearColor(0.75f, 0.60f, 0.40f), 2.5f);
		Poly({ P(62, 32), P(52, 27), P(52, 37) }, FLinearColor(0.82f, 0.85f, 0.92f));
		Poly({ P(10, 32), P(18, 26), P(20, 32), P(18, 38) }, FLinearColor(0.9f, 0.2f, 0.2f));
		break;
	}
	case 7:   // Magic: a wand with a spark
	{
		Ln(8, 58, 40, 26, FLinearColor(0.38f, 0.22f, 0.10f), 5.f);
		Ln(8, 58, 14, 52, FLinearColor(0.95f, 0.8f, 0.3f), 5.f);
		const FLinearColor Sp(0.75f, 0.45f, 1.f);
		Poly({ P(46, 4), P(51, 18), P(46, 32), P(41, 18) }, Sp);
		Poly({ P(32, 18), P(46, 13), P(60, 18), P(46, 23) }, Sp);
		D(46, 18, 4, 4, FLinearColor(1.f, 0.95f, 1.f));
		D(56, 36, 2.5f, 2.5f, FLinearColor(0.6f, 0.8f, 1.f));
		D(24, 12, 2.5f, 2.5f, FLinearColor(1.f, 0.8f, 1.f));
		break;
	}
	default:  // Lockpicking: a padlock
	{
		const FLinearColor Gold(0.95f, 0.72f, 0.15f), Dk(0.12f, 0.10f, 0.08f);
		D(32, 26, 15, 17, FLinearColor(0.7f, 0.72f, 0.78f));
		D(32, 27, 9, 12, Dk);
		Quad(14, 28, 50, 58, Gold);
		Quad(14, 28, 50, 33, FLinearColor(1.f, 0.88f, 0.4f));
		D(32, 41, 4.5f, 4.5f, Dk);
		Quad(30, 41, 34, 52, Dk);
		break;
	}
	}
}

void AKnightHUD::DrawSkillsScreen(AKnightCharacter* Knight)
{
	USkillComponent* Sk = Knight->GetSkills();
	UFont* Font = GEngine->GetMediumFont();
	const float PW = 1060.f, PH = 650.f;   // the same window as the inventory, on its other tab
	const float PX = (Canvas->ClipX - PW) * 0.5f, PY = (Canvas->ClipY - PH) * 0.5f;
	Panel(PX, PY, PW, PH);
	if (DrawPackTabs(Knight, PX, PY, true)) return;
	if (CloseBtn(PX, PY, PW)) { Knight->ToggleSkills(); return; }

	// Order: fighting skills first, then gathering and crafting
	static const ESkillType Order[9] = { ESkillType::Attack, ESkillType::Defense, ESkillType::Archery, ESkillType::Magic,
		ESkillType::Mining, ESkillType::Woodcutting, ESkillType::Fishing, ESkillType::Smithing, ESkillType::Lockpicking };
	int32 Total = 0;
	for (ESkillType T : Order) Total += Sk->GetLevel(T);
	{
		const FString Tot = FString::Printf(TEXT("Total level  %d"), Total);
		float TW = 0.f, TH = 0.f;
		HDTextSize(Tot, TW, TH, Font, 1.3f);
		HDText(Tot, FLinearColor(1.f, 0.85f, 0.3f), PX + PW - 60.f - TW, PY + 20.f, Font, 1.3f);
	}

	// A 3 x 3 board of skills: icon, name, big level number, and progress to the next level
	const float Gap = 14.f;
	const float BW = (PW - 60.f - 2 * Gap) / 3.f, BH = 160.f;
	const float GX = PX + 30.f, GY = PY + 70.f;
	for (int32 i = 0; i < 9; ++i)
	{
		const ESkillType T = Order[i];
		const float X = GX + (i % 3) * (BW + Gap), Y = GY + (i / 3) * (BH + Gap);
		const bool bOver = Over(X, Y, BW, BH);
		Tile(X, Y, BW, BH, bOver ? FLinearColor(0.85f, 0.93f, 1.f) : FLinearColor(0.6f, 0.72f, 0.88f), bOver ? 0.36f : 0.2f);
		DrawSkillIcon(static_cast<int32>(T), X + 16.f, Y + 16.f, 96.f);

		int32 Lv, Into, Need;
		Sk->GetProgress(T, Lv, Into, Need);
		HDText(USkillComponent::SkillName(T), FLinearColor(0.95f, 0.9f, 0.8f), X + 126.f, Y + 18.f, Font, 1.2f);
		const FString LvS = FString::FromInt(Lv);
		float TW = 0.f, TH = 0.f;
		HDTextSize(LvS, TW, TH, Font, 2.8f);
		HDText(LvS, FLinearColor(1.f, 0.9f, 0.45f), X + BW - 22.f - TW, Y + 48.f, Font, 2.8f);
		const float F = Need > 0 ? FMath::Clamp((float)Into / (float)Need, 0.f, 1.f) : 0.f;
		Gauge(X + 16.f, Y + BH - 24.f, BW - 32.f, 10.f, F, FLinearColor(0.35f, 0.8f, 0.3f));
		HDText(FString::Printf(TEXT("%d / %d XP"), Into, Need), FLinearColor(0.7f, 0.7f, 0.7f), X + 126.f, Y + BH - 46.f, Font, 0.9f);
		if (bOver)
		{
			SetTip({ USkillComponent::SkillName(T), FString::Printf(TEXT("Level %d"), Lv), FString::Printf(TEXT("%d XP to level %d"), FMath::Max(0, Need - Into), Lv + 1) });
		}
	}
	HDText(TEXT("K or Tab to close."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 30.f, Font, 1.0f);
}
// ---------------------------------------------------------------------------------------------------
// Lore tab (in the O screen): every fragment you have heard, sorted by kind
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawLore(float PX, float PY, float PW, float PH)
{
	AKnightCharacter* Knight = Cast<AKnightCharacter>(GetOwningPawn());
	USkillComponent* Sk = Knight ? Knight->GetSkills() : nullptr;
	if (!Sk) return;
	UFont* Font = GEngine->GetMediumFont();
	const TArray<JeebLore::FEntry>& E = JeebLore::Entries();

	HDText(FString::Printf(TEXT("Fragments heard:  %d / %d      Talk to the people of Jeebsland. They remember things."), Sk->LoreFound.Num(), E.Num()),
		FLinearColor(0.8f, 0.78f, 0.7f), PX + 30.f, PY + 56.f, Font, 1.0f);

	const float CW = 150.f;
	for (int32 c = 0; c < JeebLore::NumCats; ++c)
	{
		if (Btn(PX + 30.f + c * (CW + 8.f), PY + 86.f, CW, 30.f, JeebLore::CatName(c), true, LoreCat == c, 1.1f)) { LoreCat = c; LoreSel = -1; }
	}

	// List
	const float LX = PX + 30.f, LY = PY + 130.f, LW = 380.f, RowH = 32.f;
	int32 Row = 0;
	for (int32 i = 0; i < E.Num(); ++i)
	{
		if (E[i].Cat != LoreCat) continue;
		const float Y = LY + Row * RowH;
		++Row;
		const bool bKnown = Sk->LoreFound.Contains(i);
		const bool bOver = bKnown && Over(LX, Y, LW, RowH - 4.f);
		Tile(LX, Y, LW, RowH - 4.f, LoreSel == i ? FLinearColor(1.f, 0.78f, 0.35f) : FLinearColor(0.6f, 0.72f, 0.88f), LoreSel == i ? 0.5f : (bOver ? 0.34f : 0.16f));
		HDText(bKnown ? FString(E[i].Title) : FString(TEXT("???")), bKnown ? FLinearColor(1.f, 0.92f, 0.65f) : FLinearColor(0.4f, 0.4f, 0.4f), LX + 10.f, Y + 5.f, Font, 1.05f);
		if (bOver && bUIClick) LoreSel = i;
	}

	// Reading pane
	const float RX = PX + 440.f, RY = PY + 130.f, RW = PW - 440.f - 30.f;
	Tile(RX - 10.f, RY - 6.f, RW + 20.f, PH - 130.f - 60.f, FLinearColor(0.1f, 0.14f, 0.22f), 0.45f, false);
	if (LoreSel >= 0 && E.IsValidIndex(LoreSel) && Sk->LoreFound.Contains(LoreSel))
	{
		const JeebLore::FEntry& En = E[LoreSel];
		HDText(En.Title, FLinearColor(1.f, 0.85f, 0.3f), RX, RY, Font, 1.5f);
		HDText(FString::Printf(TEXT("as told by %s"), JeebLore::SpeakerName(En.Speaker)), FLinearColor(0.65f, 0.65f, 0.7f), RX, RY + 34.f, Font, 0.95f);
		TArray<FString> Parts;
		FString(En.Text).ParseIntoArray(Parts, TEXT("|"), true);
		float Y = RY + 72.f;
		for (const FString& P : Parts) Y += Wrapped(P, RX, Y, RW, 1.1f, FLinearColor(0.95f, 0.93f, 0.88f)) + 14.f;
	}
	else
	{
		HDText(TEXT("Choose a fragment to read it."), FLinearColor(0.5f, 0.5f, 0.5f), RX, RY, Font, 1.1f);
	}
	HDText(TEXT("Press O to close."), FLinearColor(0.8f, 0.8f, 0.8f), PX + 30.f, PY + PH - 40.f, Font, 1.0f);
}


// ---------------------------------------------------------------------------------------------------
// The bank: your pack on the left, 300 bank slots (5 pages) on the right. Click to move a stack across.
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawBankScreen(AKnightCharacter* Knight)
{
	USkillComponent* Sk = Knight->GetSkills();
	UFont* Font = GEngine->GetMediumFont();
	const float PW = 1090.f, PH = 640.f;
	const float PX = (Canvas->ClipX - PW) * 0.5f, PY = (Canvas->ClipY - PH) * 0.5f;
	Panel(PX, PY, PW, PH);
	HDText(TEXT("THE VAULT"), FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, PY + 14.f, Font, 1.8f);
	if (CloseBtn(PX, PY, PW)) { Knight->CloseBank(); return; }

	auto SlotBox = [&](float X, float Y, float S, bool bOver)
	{
		Tile(X, Y, S, S, bOver ? FLinearColor(0.85f, 0.93f, 1.f) : FLinearColor(0.6f, 0.72f, 0.88f), bOver ? 0.36f : 0.2f);
	};

	// ----- Your pack -----
	HDText(TEXT("YOUR PACK  (click to deposit)"), FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, PY + 62.f, Font, 1.1f);
	const float SS = 74.f, Gap = 6.f;
	const float GX = PX + 30.f, GY = PY + 94.f;
	for (int32 i = 0; i < NumSlots; ++i)
	{
		const float X = GX + (i % 4) * (SS + Gap), Y = GY + (i / 4) * (SS + Gap);
		const bool bOver = Over(X, Y, SS, SS);
		SlotBox(X, Y, SS, bOver);
		const FInvSlot S = Sk->Slots[i];
		if (S.Id == 0) continue;
		DrawIcon(S.Id, X + 8.f, Y + 6.f, SS - 16.f);
		if (StackSize(S.Id) > 1) HDText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, X + SS - 28.f, Y + SS - 22.f, Font, 1.0f);
		if (bOver)
		{
			ItemTip(S.Id);
			if (bUIClick) Sk->BankDeposit(i);
		}
	}
	if (Btn(GX, GY + 4 * (SS + Gap) + 8.f, 4 * (SS + Gap) - Gap, 34.f, TEXT("DEPOSIT EVERYTHING"), true, false, 1.0f))
	{
		for (int32 i = 0; i < NumSlots; ++i) Sk->BankDeposit(i);
	}

	// ----- The bank: 10 x 6 per page -----
	const int32 Cols = 10, Rows = 6, PerPage = Cols * Rows;
	const int32 Pages = (USkillComponent::NumBankSlots + PerPage - 1) / PerPage;
	BankPage = FMath::Clamp(BankPage, 0, Pages - 1);
	if (APlayerController* PC = GetOwningPlayerController())
	{
		if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) BankPage = FMath::Min(Pages - 1, BankPage + 1);
		if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) BankPage = FMath::Max(0, BankPage - 1);
	}
	const float BX0 = PX + 400.f, BS = 58.f, BG = 6.f;
	HDText(FString::Printf(TEXT("BANK  %d / %d slots used   (click to take out)"), Sk->BankUsed(), USkillComponent::NumBankSlots), FLinearColor(1.f, 0.85f, 0.3f), BX0, PY + 62.f, Font, 1.1f);
	for (int32 k = 0; k < PerPage; ++k)
	{
		const int32 Idx = BankPage * PerPage + k;
		const float X = BX0 + (k % Cols) * (BS + BG), Y = GY + (k / Cols) * (BS + BG);
		const bool bOver = Over(X, Y, BS, BS);
		SlotBox(X, Y, BS, bOver);
		if (!Sk->Bank.IsValidIndex(Idx)) continue;
		const FInvSlot S = Sk->Bank[Idx];
		if (S.Id == 0) continue;
		DrawIcon(S.Id, X + 6.f, Y + 4.f, BS - 12.f);
		if (StackSize(S.Id) > 1) HDText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, X + BS - 26.f, Y + BS - 20.f, Font, 0.85f);
		if (bOver)
		{
			ItemTip(S.Id);
			if (bUIClick) Sk->BankWithdraw(Idx);
		}
	}
	const float PgY = GY + Rows * (BS + BG) + 10.f;
	if (Btn(BX0, PgY, 90.f, 32.f, TEXT("< PREV"), BankPage > 0, false, 1.0f)) BankPage = FMath::Max(0, BankPage - 1);
	if (Btn(BX0 + 100.f, PgY, 90.f, 32.f, TEXT("NEXT >"), BankPage < Pages - 1, false, 1.0f)) BankPage = FMath::Min(Pages - 1, BankPage + 1);
	HDText(FString::Printf(TEXT("Page %d / %d   (mouse wheel also turns pages)"), BankPage + 1, Pages), FLinearColor(0.8f, 0.8f, 0.8f), BX0 + 210.f, PgY + 6.f, Font, 1.0f);
	HDText(TEXT("Gear you are wearing or holding stays on you. Press E to close."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 34.f, Font, 1.0f);
}

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

// ---------------------------------------------------------------------------------------------------
// Drag and drop: pick an item up from the pack or the weapon bar, drop it on a pack cell or a bar slot. The two trade places
// (or it moves into an empty cell). A click without dragging does what it always did (hold or wear the gear).
// Clicking anywhere else on the screen (not the panel, not the weapon bar) puts the cursor away again.
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::ProcessDrag(AKnightCharacter* Knight)
{
	if (!Knight->IsPanelCursor() || !bUIHaveMouse) { DragKind = 0; bDragging = false; return; }
	USkillComponent* Sk = Knight->GetSkills();
	const FVector2D M(UIMouseX, UIMouseY);
	auto OverCell = [&](float X, float Y, float S) { return M.X >= X && M.X <= X + S && M.Y >= Y && M.Y <= Y + S; };
	auto OverWindows = [&]() { for (int32 r = 0; r < RectCount; ++r) if (M.X >= RectX[r] - 6.f && M.X <= RectX[r] + RectW[r] + 6.f && M.Y >= RectY[r] - 6.f && M.Y <= RectY[r] + RectH[r] + 6.f) return true; return false; };
	auto OverBar = [&]() { return bHotCells && M.X >= HotCellX[0] - 12.f && M.X <= HotCellX[4] + HotCellS + 200.f && M.Y >= HotCellY[0] - 24.f && M.Y <= HotCellY[0] + HotCellS + 20.f; };

	// right-click a stack (or the ingots) for "drop how many?"
	if (bUIRightClick && DragKind == 0)
	{
		for (int32 i = 0; i < InvCellCount; ++i)
			if (OverCell(InvCellX[i], InvCellY[i], InvCellS) && Sk->Slots[i].Id != 0 && Sk->Slots[i].Count > 1) { OpenDropBox(i, Sk->Slots[i].Id, Sk->Slots[i].Count); return; }
		if (InvCellCount > 0 && Sk->GetIngots() > 0 && OverCell(InvIngotX, InvIngotY, InvCellS)) { OpenDropBox(-1, 0, Sk->GetIngots()); return; }
	}

	// pick up
	if (bUIClick && DragKind == 0)
	{
		for (int32 i = 0; i < InvCellCount && DragKind == 0; ++i)
			if (OverCell(InvCellX[i], InvCellY[i], InvCellS) && Sk->Slots[i].Id != 0) { DragKind = 1; DragIdx = i; DragId = Sk->Slots[i].Id; }
		if (bHotCells)
			for (int32 h = 0; h < 5 && DragKind == 0; ++h)
				if (OverCell(HotCellX[h], HotCellY[h], HotCellS) && Sk->Hotbar[h] != 0) { DragKind = 2; DragIdx = h; DragId = Sk->Hotbar[h]; }
		if (DragKind == 0 && InvCellCount > 0 && Sk->GetIngots() > 0 && OverCell(InvIngotX, InvIngotY, InvCellS)) { DragKind = 3; DragIdx = -1; DragId = 0; }
		if (DragKind == 0)
			for (int32 w = 0; w < WornCellCount && DragKind == 0; ++w)
			{
				int32* Wp = Sk->WornSlotPtr(WornKind[w], WornSecond[w]);
				if (OverCell(WornCellX[w], WornCellY[w], WornCellS) && Wp && *Wp != 0) { DragKind = 4; DragIdx = w; DragId = *Wp; }
			}
		if (DragKind == 0)
			for (int32 l = 0; l < LootCellCount && DragKind == 0; ++l)
			{
				const int32 Idx = LootCellIdx[l];
				ALootDrop* Sack = Knight->GetLootTarget();
				if (!Sack || !OverCell(LootCellX[l], LootCellY[l], LootCellS)) continue;
				if (Idx < 0 ? Sack->GetIngots() > 0 : (Sack->GetItems().IsValidIndex(Idx) && Sack->GetItems()[Idx].Id != 0)) { DragKind = 5; DragIdx = Idx; DragId = Idx < 0 ? 0 : Sack->GetItems()[Idx].Id; }
			}
		if (DragKind == 0 && bPotionCell && Sk->PotionSlot.Id != 0 && OverCell(PotionCellX, PotionCellY, HotCellS)) { DragKind = 6; DragIdx = 0; DragId = Sk->PotionSlot.Id; }
		if (DragKind != 0) { DragStart = M; bDragging = false; }
		else if (!OverWindows() && !OverBar()) Knight->SetPanelCursor(false);   // a click on nothing: leave pointer mode
	}
	if (DragKind == 0) return;
	if (!bDragging && FVector2D::Distance(M, DragStart) > 6.f) bDragging = true;

	// drop
	if (bUIRelease)
	{
		bool bChanged = false;
		if (!bDragging)
		{
			if (DragKind == 1 && IsGear(DragId)) Knight->EquipFromInventory(DragIdx);   // a plain click: hold or wear it
			else if (DragKind == 4) Knight->UnequipWorn(WornKind[DragIdx], WornSecond[DragIdx]);   // a plain click on a worn piece takes it off
			else if (DragKind == 5 && Knight->GetLootTarget()) { if (DragIdx < 0) Knight->GetLootTarget()->TakeIngots(Knight); else Knight->GetLootTarget()->TakeOne(DragIdx, Knight); }   // a click takes it
			else if (DragKind == 6) Knight->DrinkPotion();   // a click on the potion slot drinks
			else if (DragKind == 7) Knight->UnequipWorn(K_Shield);   // a click on the shield slot puts the shield away
		}
		else
		{
			int32 ToInv = -1, ToHot = -1, ToWorn = -1;
			for (int32 i = 0; i < InvCellCount; ++i) if (OverCell(InvCellX[i], InvCellY[i], InvCellS)) ToInv = i;
			if (bHotCells) for (int32 h = 0; h < 5; ++h) if (OverCell(HotCellX[h], HotCellY[h], HotCellS)) ToHot = h;
			for (int32 w = 0; w < WornCellCount; ++w) if (OverCell(WornCellX[w], WornCellY[w], WornCellS)) ToWorn = w;
			const bool bOnDef = bDefCell && OverCell(DefCellX, DefCellY, HotCellS);
			if (DragKind == 1)
			{
				const bool bOnPotion = bPotionCell && OverCell(PotionCellX, PotionCellY, HotCellS);
				if (bOnPotion) bChanged = Sk->SwapInvPotion(DragIdx);   // onto the potion slot (potions only)
				else if (bOnDef) bChanged = Sk->SwapInvWorn(DragIdx, K_Shield, false);   // onto the shield slot (shields only)
				else if (ToWorn >= 0) bChanged = Sk->SwapInvWorn(DragIdx, WornKind[ToWorn], WornSecond[ToWorn]);   // onto a worn slot: swap
				else if (ToHot >= 0) bChanged = Sk->SwapInvHotbar(DragIdx, ToHot);
				else if (ToInv >= 0) bChanged = Sk->SwapInv(DragIdx, ToInv);
				else if (!OverWindows() && !OverBar())
				{
					// let go out in the world: it drops to the ground (a stack asks how many)
					if (Sk->Slots[DragIdx].Count > 1) OpenDropBox(DragIdx, Sk->Slots[DragIdx].Id, Sk->Slots[DragIdx].Count);
					else Knight->DropFromPack(DragIdx, 1);
				}
			}
			else if (DragKind == 2)
			{
				if (ToInv >= 0) bChanged = Sk->SwapInvHotbar(ToInv, DragIdx);
				else if (ToHot >= 0) bChanged = Sk->SwapHotbar(DragIdx, ToHot);
				else if (!OverWindows() && !OverBar()) Knight->DropFromHotbar(DragIdx);
			}
			else if (DragKind == 4)
			{
				// a worn piece back to the pack: onto an empty cell it comes off (armour cannot be taken off, only swapped); onto a piece that belongs in that slot they swap
				if (ToInv >= 0)
				{
					if (Sk->Slots[ToInv].Id == 0) Knight->UnequipWorn(WornKind[DragIdx], WornSecond[DragIdx]);
					else bChanged = Sk->SwapInvWorn(ToInv, WornKind[DragIdx], WornSecond[DragIdx]);
				}
			}
			else if (DragKind == 7)
			{
				// the shield back to the pack: onto an empty cell it comes off; onto another shield they swap
				if (ToInv >= 0)
				{
					if (Sk->Slots[ToInv].Id == 0) Knight->UnequipWorn(K_Shield);
					else bChanged = Sk->SwapInvWorn(ToInv, K_Shield, false);
				}
			}
			else if (DragKind == 3 && !OverWindows() && !OverBar()) OpenDropBox(-1, 0, Sk->GetIngots());
			else if (DragKind == 5)
			{
				// from the sack straight onto the weapon bar, the shield slot or an armor slot (the old piece goes to your pack, or
				// into the sack if your pack is full); anywhere else on your inventory or armor window just takes it
				ALootDrop* Sack = Knight->GetLootTarget();
				if (Sack && DragIdx >= 0 && ToHot >= 0) bChanged = Sack->EquipTo(DragIdx, Knight, ToHot, -1, false);
				else if (Sack && DragIdx >= 0 && bOnDef) bChanged = Sack->EquipTo(DragIdx, Knight, -1, K_Shield, false);
				else if (Sack && DragIdx >= 0 && ToWorn >= 0) bChanged = Sack->EquipTo(DragIdx, Knight, -1, WornKind[ToWorn], WornSecond[ToWorn]);
				if (bChanged) { DragKind = 0; bDragging = false; DragIdx = -1; Knight->RefreshEquipment(); return; }
				const bool bMine = (RectCount > 0 && M.X >= RectX[0] && M.X <= RectX[0] + RectW[0] && M.Y >= RectY[0] && M.Y <= RectY[0] + RectH[0])
					|| (RectCount > 1 && M.X >= RectX[1] && M.X <= RectX[1] + RectW[1] && M.Y >= RectY[1] && M.Y <= RectY[1] + RectH[1]);
				if (bMine && Knight->GetLootTarget()) { if (DragIdx < 0) Knight->GetLootTarget()->TakeIngots(Knight); else Knight->GetLootTarget()->TakeOne(DragIdx, Knight); }
			}
			else if (DragKind == 6 && ToInv >= 0) bChanged = Sk->SwapInvPotion(ToInv);   // the potion slot back into the pack
			if (bChanged) Knight->RefreshEquipment();   // the weapon in hand may have changed
		}
		DragKind = 0; bDragging = false; DragIdx = -1;
	}
}

// the item follows the cursor while it is being dragged
void AKnightHUD::DrawDragGhost()
{
	if (DragKind == 0 || !bDragging || !bUIHaveMouse) return;
	const float S = 56.f;
	if (DragKind == 3 || (DragKind == 5 && DragIdx < 0)) DrawIngot(UIMouseX - S * 0.5f, UIMouseY - S * 0.5f, S);
	else DrawIcon(DragId, UIMouseX - S * 0.5f, UIMouseY - S * 0.5f, S);
}

void AKnightHUD::OpenDropBox(int32 Slot, int32 Id, int32 Max)
{
	bDropOpen = true; DropSlot = Slot; DropId = Id; DropMax = Max;
	DropText = FString::FromInt(Max);   // starts as the whole stack: type a smaller number
	DragKind = 0; bDragging = false;
}

// "Drop how many?": type a number, Enter (or Drop) to drop it, Esc (or Cancel) to keep everything
void AKnightHUD::DrawDropBox(AKnightCharacter* Knight)
{
	if (!bDropOpen) return;
	UFont* Font = GEngine->GetMediumFont();
	APlayerController* PC = GetOwningPlayerController();
	if (PC)
	{
		static const FKey Digits[10] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		static const FKey Pad[10] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
		for (int32 d = 0; d < 10; ++d)
			if ((PC->WasInputKeyJustPressed(Digits[d]) || PC->WasInputKeyJustPressed(Pad[d])) && DropText.Len() < 7) DropText += FString::FromInt(d);
		if (PC->WasInputKeyJustPressed(EKeys::BackSpace) && DropText.Len() > 0) DropText.LeftChopInline(1);
	}
	int32 Amount = FCString::Atoi(*DropText);
	if (Amount > DropMax) { Amount = DropMax; DropText = FString::FromInt(Amount); }
	const float W = 440.f, H = 230.f, X = (Canvas->ClipX - W) * 0.5f, Y = (Canvas->ClipY - H) * 0.5f;
	bGreyGlass = true;
	FillRR(X, Y, W, H, 26.f, [&](float) { return FLinearColor(0.05f, 0.05f, 0.055f, 0.96f); });
	Panel(X, Y, W, H);
	bGreyGlass = false;
	const FString Name = DropSlot < 0 ? FString(TEXT("ingots")) : (IsMaterial(DropId) ? FString(MaterialName(MaterialType(DropId))) : ItemName(DropId));
	const FString Title = FString::Printf(TEXT("Drop how many %s?"), *Name);
	float TW = 0.f, TH = 0.f;
	HDTextSize(Title, TW, TH, Font, 1.4f);
	HDText(Title, FLinearColor(1.f, 0.9f, 0.6f), X + (W - TW) * 0.5f, Y + 18.f, Font, 1.4f);
	// the number box
	Tile(X + 120.f, Y + 66.f, W - 240.f, 52.f, FLinearColor(0.8f, 0.82f, 0.9f), 0.22f);
	const FString Shown = DropText + ((FMath::FloorToInt(GetWorld()->GetTimeSeconds() * 2.f) % 2 == 0) ? TEXT("|") : TEXT(" "));
	HDTextSize(Shown, TW, TH, Font, 2.0f);
	HDText(Shown, FLinearColor::White, X + (W - TW) * 0.5f, Y + 66.f + (52.f - TH) * 0.5f, Font, 2.0f);
	const FString Of = FString::Printf(TEXT("of %d"), DropMax);
	HDTextSize(Of, TW, TH, Font, 1.0f);
	HDText(Of, FLinearColor(0.7f, 0.72f, 0.8f), X + (W - TW) * 0.5f, Y + 124.f, Font, 1.0f);
	const float BY = Y + H - 62.f, BW = 120.f, BH = 40.f;
	const bool bAll = Btn(X + 30.f, BY, BW, BH, TEXT("All"), true, false, 1.2f);
	const bool bDrop = Btn(X + (W - BW) * 0.5f, BY, BW, BH, TEXT("Drop"), Amount > 0, true, 1.2f);
	const bool bCancel = Btn(X + W - 30.f - BW, BY, BW, BH, TEXT("Cancel"), true, false, 1.2f);
	if (bAll) DropText = FString::FromInt(DropMax);
	if (PC && PC->WasInputKeyJustPressed(EKeys::Escape)) { bDropOpen = false; return; }
	if (bCancel) { bDropOpen = false; return; }
	if ((bDrop || (PC && (PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar)))) && Amount > 0)
	{
		if (DropSlot < 0) Knight->DropIngots(Amount);
		else Knight->DropFromPack(DropSlot, Amount);
		bDropOpen = false;
	}
}

// ---------------------------------------------------------------------------------------------------
// An open sack (E on it): its own windows, to the left of yours, laid out the way its owner had things. Its pack (with its
// ingots in the top-left cell) beside your inventory; its armour beside your Armor window; its weapon bar above that.
// Click (or drag onto your inventory) to take a thing; "Take all" takes whatever fits.
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawLootWindows(AKnightCharacter* Knight, float IX, float IY, float PW)
{
	ALootDrop* Sack = Knight->GetLootTarget();
	if (!Sack) return;
	UFont* Font = GEngine->GetMediumFont();
	const bool bLive = Knight->IsPanelCursor() && bUIHaveMouse;
	auto Hot = [&](float X, float Y, float W, float H) { return bLive && Over(X, Y, W, H); };
	auto AddRect = [&](float X, float Y, float W, float H) { if (RectCount < 8) { RectX[RectCount] = X; RectY[RectCount] = Y; RectW[RectCount] = W; RectH[RectCount] = H; ++RectCount; } };
	const float SS = 44.f, Gap = 5.f, Pad = 10.f, TitleH = 30.f;
	const float GridW = 5 * SS + 4 * Gap, GridH = 5 * SS + 4 * Gap;
	const FLinearColor Idle(0.62f, 0.63f, 0.66f), Hover(0.86f, 0.87f, 0.9f);
	const TArray<FInvSlot>& Items = Sack->GetItems();
	auto Cell = [&](float X, float Y, float S, int32 Idx)   // one loot cell (Idx -1 = the ingots)
	{
		const bool bOver = Hot(X, Y, S, S);
		Tile(X, Y, S, S, bOver ? Hover : Idle, bOver ? 0.24f : 0.11f);
		if (LootCellCount < 64) { LootCellX[LootCellCount] = X; LootCellY[LootCellCount] = Y; LootCellIdx[LootCellCount] = Idx; LootCellS = S; ++LootCellCount; }
		const bool bLifted = DragKind == 5 && bDragging && DragIdx == Idx;
		if (Idx < 0)
		{
			if (Sack->GetIngots() <= 0) return;
			if (!bLifted) DrawIngot(X + 7.f, Y + 4.f, S - 14.f);
			HDText(FString::FromInt(Sack->GetIngots()), FLinearColor(1.f, 0.88f, 0.4f), X + 4.f, Y + S - 18.f, Font, 0.9f);
			if (bOver) SetTip({ TEXT("Ingots"), FString::Printf(TEXT("%d ingots: click to take them"), Sack->GetIngots()) });
			return;
		}
		const FInvSlot& It = Items[Idx];
		if (It.Id == 0) return;
		if (!bLifted) DrawIcon(It.Id, X + 4.f, Y + 3.f, S - 8.f);
		if (StackSize(It.Id) > 1 || It.Count > 1) HDText(FString::FromInt(It.Count), FLinearColor::White, X + S - 22.f, Y + 2.f, Font, 0.75f);
		ItemBadges(It.Id, X, Y, S);
		if (bOver) ItemTip(It.Id);
	};

	// ---- The sack's pack, beside your inventory (bottoms lined up) ----
	const float LH = Pad + TitleH + GridH + Pad;
	const float LX = IX - PW - 12.f, LY = IY + (Pad + GridH + Pad) - LH;
	Panel(LX, LY, PW, LH, 1.f, false);
	AddRect(LX, LY, PW, LH);
	HDText(TEXT("Sack"), FLinearColor(1.f, 0.9f, 0.65f), LX + Pad + 2.f, LY + Pad + 2.f, Font, 1.15f);
	if (Btn(LX + PW - Pad - 96.f, LY + Pad - 2.f, 96.f, 26.f, TEXT("Take all"), true, false, 0.9f)) { Sack->TakeEverythingFits(Knight); return; }
	{
		const float GX = LX + Pad, GY = LY + Pad + TitleH;
		Cell(GX, GY, SS, -1);
		int32 C = 1;
		for (int32 i = 0; i < Items.Num() && C < 25; ++i)
		{
			if (Sack->WhereOf(i) != 0 || Items[i].Id == 0) continue;
			Cell(GX + (C % 5) * (SS + Gap), GY + (C / 5) * (SS + Gap), SS, i);
			++C;
		}
		for (; C < 25; ++C) Tile(GX + (C % 5) * (SS + Gap), GY + (C / 5) * (SS + Gap), SS, SS, Idle, 0.07f);   // empty cells
	}

	// ---- Its armour, laid out like the knight, beside your Armor window ----
	struct FWornSlot { int32 Kind; int32 Col, Row; };
	static const FWornSlot Layout[10] = {
		{ K_Helmet, 1, 0 }, { K_Gloves, 0, 1 }, { K_Chest, 1, 1 }, { K_Shoulders, 2, 1 },
		{ K_Shield, 0, 2 }, { K_Boots, 1, 2 }, { K_Cape, 2, 2 }, { K_Ring, 3, 0 }, { K_Ring, 3, 1 }, { K_Ring, 3, 2 },
	};
	const float WS = 50.f, WG = (GridW - 4 * WS) / 3.f, RowH = WS + 8.f;
	const float AH = Pad + 3 * RowH + Pad;
	const float AY = (ArmorTopY > 0.f ? ArmorTopY : LY - AH - 10.f);
	const float AYb = FMath::Min(AY, LY - AH - 10.f);
	Panel(LX, AYb, PW, AH, 1.f, false);
	AddRect(LX, AYb, PW, AH);
	{
		// which sack entries were worn: one per slot (accessories fill the three misc slots in turn)
		int32 SlotIdx[10]; for (int32& V : SlotIdx) V = -2;
		int32 NextMisc = 7;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Sack->WhereOf(i) != 2 || Items[i].Id == 0 || !IsGear(Items[i].Id)) continue;
			const int32 K = KindOf(Items[i].Id);
			if (IsAccessoryKind(K)) { if (NextMisc < 10) SlotIdx[NextMisc++] = i; continue; }
			for (int32 s = 0; s < 7; ++s) if (Layout[s].Kind == K && SlotIdx[s] == -2) { SlotIdx[s] = i; break; }
		}
		const float GX = LX + Pad, GY = AYb + Pad;
		for (int32 s = 0; s < 10; ++s)
		{
			const float X = GX + Layout[s].Col * (WS + WG) + (Layout[s].Col == 3 ? 0.f : -2.f), Y = GY + Layout[s].Row * RowH;
			if (SlotIdx[s] >= 0) Cell(X, Y, WS, SlotIdx[s]);
			else Tile(X, Y, WS, WS, Idle, 0.07f);
		}
	}

	// ---- Its weapon bar, above its armour ----
	{
		const float WH = Pad + SS + Pad, WY = AYb - WH - 10.f;
		Panel(LX, WY, PW, WH, 1.f, false);
		AddRect(LX, WY, PW, WH);
		int32 C = 0;
		for (int32 i = 0; i < Items.Num() && C < 5; ++i)
		{
			if (Sack->WhereOf(i) != 1 || Items[i].Id == 0) continue;
			Cell(LX + Pad + C * (SS + Gap), WY + Pad, SS, i);
			++C;
		}
		for (; C < 5; ++C) Tile(LX + Pad + C * (SS + Gap), WY + Pad, SS, SS, Idle, 0.07f);
	}
}
