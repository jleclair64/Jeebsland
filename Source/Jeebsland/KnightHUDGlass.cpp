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
		if (IsFireArrow(Id)) L.Add(TEXT("Sets what it hits burning for a few seconds"));
		SetTip(L);
		return;
	}
	if (!IsGear(Id)) return;
	const int32 K = KindOf(Id), V = VariantOf(Id);
	L.Add(ItemName(Id));
	if (IsWeaponKind(K) || IsArmorKind(K) || K == K_Shield)   // the level it needs (or why you can't use it)
	{
		const USkillComponent* Sk = GetOwningPawn() ? GetOwningPawn()->FindComponentByClass<USkillComponent>() : nullptr;
		FString Why;
		L.Add(CanEquip(Sk, Id, &Why) ? FString::Printf(TEXT("Needs %s %d"), SkillNameOf(EquipSkill(K)), EquipLevel(V)) : Why);
	}
	if (IsWeaponKind(K))
	{
		int32 A, B; WeaponDamage(K, V, A, B, V);
		L.Add(FString::Printf(TEXT("Damage %d-%d%s"), A, B, K == K_Bow ? TEXT(" with same-tier arrows") : TEXT("")));
		if (K == K_Pickaxe || K == K_Axe || K == K_Rod) L.Add(FString::Printf(TEXT("Gathers %d%% quicker than tin"), FMath::RoundToInt((1.f - ToolTimeMult(V)) * 100.f)));
		if (!AllowsShield(K)) L.Add(TEXT("Two-handed: cannot be used with a shield"));
	}
	else if (K == K_Shield)
	{
		L.Add(FString::Printf(TEXT("Guard %d: a raised shield takes every blow on it; the one that empties the guard drops it until it recovers"), ShieldGuard(Id)));
		L.Add(ShieldStyleRow(StyleOf(Id)).Note);
	}
	else if (IsArmorKind(K))
	{
		L.Add(FString::Printf(TEXT("Defence +%d"), DefenseOf(K, V)));
		if (K == K_Boots) L.Add(FString::Printf(TEXT("Stamina x%.2f"), BootStaminaMult(V)));
	}
	else if (IsAccessoryKind(K)) L.Add(AccessoryText(K, V));
	if (K == K_Cape) { L.Add(TEXT("Shows your PvP rank. It is yours for good: never dropped, sold or traded")); SetTip(L); return; }
	L.Add(FString::Printf(TEXT("Worth %d ingots"), GearSellPrice(Id)));
	SetTip(L);
}


