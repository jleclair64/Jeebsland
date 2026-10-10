#include "KnightHUD.h"
#include "SkillComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "KnightCharacter.h"
#include "Pig.h"
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
		DrawText(Line, Col, X, Cy, Font, Scale);
		Cy += LineH + 3.f;
		Line.Reset();
	};
	for (const FString& W : Words)
	{
		const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
		float TW = 0.f, TH = 0.f;
		GetTextSize(Try, TW, TH, Font, Scale);
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

bool AKnightHUD::Btn(float X, float Y, float W, float H, const FString& Label, bool bEnabled, bool bActive, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	const bool bOver = bEnabled && Over(X, Y, W, H);
	FLinearColor Fill = FLinearColor(0.18f, 0.18f, 0.18f);
	if (!bEnabled) Fill = FLinearColor(0.14f, 0.14f, 0.14f);
	else if (bActive) Fill = FLinearColor(0.55f, 0.32f, 0.08f);
	else if (bOver) Fill = FLinearColor(0.34f, 0.26f, 0.12f);
	DrawRect(Fill, X, Y, W, H);
	float TW = 0.f, TH = 0.f;
	GetTextSize(Label, TW, TH, Font, Scale);
	const FLinearColor Txt = !bEnabled ? FLinearColor(0.45f, 0.45f, 0.45f) : (bActive ? FLinearColor(1.f, 0.9f, 0.5f) : FLinearColor(0.85f, 0.85f, 0.85f));
	DrawText(Label, Txt, X + (W - TW) * 0.5f, Y + (H - TH) * 0.5f, Font, Scale);
	return bOver && bUIClick;
}

bool AKnightHUD::CloseBtn(float PX, float PY, float PW)
{
	UFont* Font = GEngine->GetMediumFont();
	const float BX = PX + PW - 34.f, BY = PY + 6.f;
	const bool bOver = Over(BX, BY, 26.f, 26.f);
	DrawRect(bOver ? FLinearColor(0.7f, 0.15f, 0.1f) : FLinearColor(0.25f, 0.25f, 0.25f), BX, BY, 26.f, 26.f);
	DrawText(TEXT("X"), FLinearColor::White, BX + 7.f, BY + 3.f, Font, 1.0f);
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
		GetTextSize(TipLines[i], TW, TH, Font, i == 0 ? 1.15f : 0.95f);
		W = FMath::Max(W, TW); H += TH + 3.f;
	}
	float X = UIMouseX + 16.f, Y = UIMouseY + 12.f;
	if (X + W + 20.f > Canvas->ClipX) X = UIMouseX - W - 30.f;
	if (Y + H + 14.f > Canvas->ClipY) Y = Canvas->ClipY - H - 16.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.94f), X - 6.f, Y - 6.f, W + 20.f, H + 12.f);
	DrawRect(FLinearColor(0.55f, 0.32f, 0.08f), X - 6.f, Y - 6.f, W + 20.f, 2.f);
	float Cy = Y;
	for (int32 i = 0; i < TipLines.Num(); ++i)
	{
		const float Sc = i == 0 ? 1.15f : 0.95f;
		float TW = 0.f, TH = 0.f;
		GetTextSize(TipLines[i], TW, TH, Font, Sc);
		DrawText(TipLines[i], i == 0 ? FLinearColor(1.f, 0.9f, 0.5f) : FLinearColor(0.88f, 0.88f, 0.88f), X + 2.f, Cy, Font, Sc);
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
	else if (K == K_Shield) { L.Add(FString::Printf(TEXT("Blocks %d damage"), DefenseOf(K, V))); }
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

	APlayerController* PC = GetOwningPlayerController();
	bUIHaveMouse = PC && PC->GetMousePosition(UIMouseX, UIMouseY);
	const bool bDownNow = PC && PC->IsInputKeyDown(EKeys::LeftMouseButton);
	bUIClick = bDownNow && !bUIWasDown;
	bUIWasDown = bDownNow;
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
			GetTextSize(Nm, TW, TH, Font, 2.f);
			DrawText(Nm, FLinearColor(0.f, 0.f, 0.f, 0.8f * A), SP.X - TW * 0.5f + 2.f, SP.Y - TH + 2.f, Font, 2.f);
			DrawText(Nm, FLinearColor(1.f, 0.85f, 0.45f, 0.95f * A), SP.X - TW * 0.5f, SP.Y - TH, Font, 2.f);
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
			GetTextSize(Title, TW, TH, Font, 3.f);
			DrawText(Title, FLinearColor(1.f, 0.62f, 0.2f, Alpha), (Canvas->ClipX - TW) * 0.5f, Canvas->ClipY * 0.14f, Font, 3.f);
		}
	}

	DrawText(TEXT("O  -  Settings / Controls / Dictionary"), FLinearColor(1.f, 1.f, 1.f, 0.55f), Canvas->ClipX - 340.f, 14.f, Font, 1.0f);
	DrawText(TEXT("Tab  -  Inventory"), FLinearColor(1.f, 1.f, 1.f, 0.55f), Canvas->ClipX - 340.f, 34.f, Font, 1.0f);
	DrawIngot(Canvas->ClipX - 340.f, 58.f, 22.f);
	DrawText(FString::Printf(TEXT("%d"), Skills->GetIngots()), FLinearColor(1.f, 0.85f, 0.3f), Canvas->ClipX - 308.f, 58.f, Font, 1.2f);

	DrawMinimap(Pawn);
	DrawWeaponBar(Knight);
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
			GetTextSize(M.Text, TW, TH, Font, 1.15f);
			const float X = (Canvas->ClipX - TW) * 0.5f;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * A), X - 10.f, Y - 3.f, TW + 20.f, TH + 6.f);
			DrawText(M.Text, FLinearColor(M.Color.R, M.Color.G, M.Color.B, A), X, Y, Font, 1.15f);
			Y -= TH + 10.f;
		}
	}

	// Stamina bar (bottom centre)
	{
		const float BW = 260.f, BH = 12.f;
		const float BX = (Canvas->ClipX - BW) * 0.5f;
		const float BY = Canvas->ClipY - 36.f;
		const float F = Knight->GetStaminaFraction();
		const FLinearColor Fill = Knight->IsExhausted() ? FLinearColor(0.85f, 0.15f, 0.1f) : FLinearColor(0.35f, 0.8f, 0.3f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), BX - 3.f, BY - 3.f, BW + 6.f, BH + 6.f);
		DrawRect(FLinearColor(0.15f, 0.15f, 0.15f, 1.f), BX, BY, BW, BH);
		DrawRect(Fill, BX, BY, BW * F, BH);
	}

	// Health bar (above the stamina bar)
	{
		const float BW = 260.f, BH = 14.f;
		const float BX = (Canvas->ClipX - BW) * 0.5f;
		const float BY = Canvas->ClipY - 56.f;
		const float F = FMath::Clamp(Knight->GetHealth() / (float)FMath::Max(1, Knight->GetMaxHealth()), 0.f, 1.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), BX - 3.f, BY - 3.f, BW + 6.f, BH + 6.f);
		DrawRect(FLinearColor(0.05f, 0.06f, 0.12f, 1.f), BX, BY, BW, BH);
				const FLinearColor HpCol = (F < 0.2f) ? FLinearColor(0.85f, 0.12f, 0.10f) : (F < 0.5f ? FLinearColor(0.95f, 0.78f, 0.12f) : FLinearColor(0.15f, 0.42f, 0.95f));
		DrawRect(HpCol, BX, BY, BW * F, BH);
		DrawText(FString::Printf(TEXT("%d / %d"), FMath::CeilToInt(Knight->GetHealth()), Knight->GetMaxHealth()), FLinearColor::White, BX + BW * 0.5f - 22.f, BY - 1.f, Font, 0.8f);
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
			FLinearColor Col(1.f, 0.15f, 0.12f);
			float Sc = 1.6f;
			if (Nums[i].Type == JeebCombat::N_Blocked) Col = FLinearColor(0.3f, 0.55f, 1.f);
			else if (Nums[i].Type == JeebCombat::N_Crit) { Col = FLinearColor(1.f, 0.82f, 0.1f); Sc = 2.3f; }
			Col.A = FMath::Clamp((1.2f - Age) / 0.4f, 0.f, 1.f);
			const FString Txt = FString::FromInt(Nums[i].Amount);
			float TW = 0.f, TH = 0.f;
			GetTextSize(Txt, TW, TH, Font, Sc);
			DrawText(Txt, FLinearColor(0.f, 0.f, 0.f, Col.A), SP.X - TW * 0.5f + 2.f, SP.Y + 2.f, Font, Sc);
			DrawText(Txt, Col, SP.X - TW * 0.5f, SP.Y, Font, Sc);
		}
	}

	// Horse stamina: a bar counting 100 down while you gallop, then refilling before the next sprint
	if (Knight->IsMounted())
	{
		const float BW = 260.f, BH = 12.f;
		const float BX = (Canvas->ClipX - BW) * 0.5f;
		const float BY = Canvas->ClipY - 84.f;
		const float F = FMath::Clamp(Knight->GetHorseEnergy() / 100.f, 0.f, 1.f);
		const FLinearColor Fill = Knight->IsHorseTired() ? FLinearColor(0.85f, 0.35f, 0.1f) : FLinearColor(0.95f, 0.75f, 0.2f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), BX - 3.f, BY - 3.f, BW + 6.f, BH + 6.f);
		DrawRect(FLinearColor(0.15f, 0.15f, 0.15f, 1.f), BX, BY, BW, BH);
		DrawRect(Fill, BX, BY, BW * F, BH);
		DrawText(FString::Printf(TEXT("Horse  %d"), FMath::RoundToInt(Knight->GetHorseEnergy())), FLinearColor::White, BX + BW + 12.f, BY - 3.f, Font, 1.0f);
	}

	// "Press E" prompt
	{
		FString Prompt;
		if (!Knight->IsUIOpen() && Knight->GetInteractPrompt(Prompt))
		{
			float TW = 0.f, TH = 0.f;
			GetTextSize(Prompt, TW, TH, Font, 1.5f);
			const float PX2 = (Canvas->ClipX - TW) * 0.5f, PY2 = Canvas->ClipY * 0.6f;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), PX2 - 14.f, PY2 - 6.f, TW + 28.f, TH + 12.f);
			DrawText(Prompt, FLinearColor(1.f, 0.9f, 0.4f), PX2, PY2, Font, 1.5f);
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

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X, Y, W, H);
	DrawText(TEXT("SKILLS"), FLinearColor(1.f, 0.85f, 0.3f), X + 16.f, Y + 8.f, Font, 1.3f);

	auto SkillRow = [&](float RowY, const TCHAR* Name, ESkillType Skill, const FLinearColor& BarColor)
	{
		int32 Level, Into, Needed;
		Skills->GetProgress(Skill, Level, Into, Needed);
		DrawText(FString::Printf(TEXT("%s   Lv %d"), Name, Level), FLinearColor::White, X + 16.f, RowY, Font, Scale);
		const float BarX = X + 16.f, BarY = RowY + 26.f, BarW = W - 32.f;
		const float Fill = Needed > 0 ? FMath::Clamp(float(Into) / float(Needed), 0.f, 1.f) : 0.f;
		DrawRect(FLinearColor(0.15f, 0.15f, 0.15f, 1.f), BarX, BarY, BarW, 9.f);
		DrawRect(BarColor, BarX, BarY, BarW * Fill, 9.f);
		DrawText(FString::Printf(TEXT("%d / %d XP"), Into, Needed), FLinearColor(0.8f, 0.8f, 0.8f), BarX + BarW - 100.f, RowY + 2.f, Font, 0.9f);
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
	DrawRect(bLevelGlow ? FLinearColor(1.f, 0.8f, 0.25f, A * (0.5f + 0.5f * Pulse)) : FLinearColor(0.55f, 0.32f, 0.08f, A), X - 2.f, Y - 2.f, W + 4.f, H + 4.f);
	DrawRect(FLinearColor(0.06f, 0.05f, 0.04f, 0.88f * A), X, Y, W, H);

	const ESkillType T = Skills->LastSkill;
	DrawSkillIcon(static_cast<int32>(T), X + 10.f, Y + 10.f, 72.f);
	int32 Lv, Into, Need;
	Skills->GetProgress(T, Lv, Into, Need);
	DrawText(USkillComponent::SkillName(T), FLinearColor(1.f, 0.9f, 0.6f, A), X + 92.f, Y + 10.f, Font, 1.2f);
	{
		const FString LvS = FString::Printf(TEXT("Lv %d"), Lv);
		float TW = 0.f, TH = 0.f;
		GetTextSize(LvS, TW, TH, Font, 1.35f);
		DrawText(LvS, bLevelGlow ? FLinearColor(1.f, 0.85f, 0.3f, A) : FLinearColor(1.f, 1.f, 1.f, A), X + W - TW - 12.f, Y + 8.f, Font, 1.35f);
	}
	const float BX = X + 92.f, BY = Y + 46.f, BW = W - 104.f, BH = 12.f;
	const float F = Need > 0 ? FMath::Clamp(float(Into) / float(Need), 0.f, 1.f) : 0.f;
	DrawRect(FLinearColor(0.18f, 0.18f, 0.18f, A), BX, BY, BW, BH);
	DrawRect(FLinearColor(0.35f, 0.8f, 0.3f, A), BX, BY, BW * F, BH);
	DrawText(FString::Printf(TEXT("%d / %d XP"), Into, Need), FLinearColor(0.8f, 0.8f, 0.8f, A), BX, BY + 16.f, Font, 0.9f);
	if (Since < 2.5f && Skills->LastXPGain > 0)
	{
		const float GA = A * FMath::Clamp((2.5f - Since) / 0.8f, 0.f, 1.f);
		const FString Gain = FString::Printf(TEXT("+%d"), Skills->LastXPGain);
		float TW = 0.f, TH = 0.f;
		GetTextSize(Gain, TW, TH, Font, 1.0f);
		DrawText(Gain, FLinearColor(0.6f, 1.f, 0.5f, GA), BX + BW - TW, BY + 16.f - Since * 4.f, Font, 1.0f);
	}
	if (bLevelGlow) DrawText(TEXT("LEVEL UP!"), FLinearColor(1.f, 0.85f, 0.3f, A * (0.6f + 0.4f * Pulse)), X + 92.f, Y - 26.f, Font, 1.2f);
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
	const float SS = 66.f, Gap = 8.f;
	const float TotalW = 5 * SS + 4 * Gap;
	const float X0 = (Canvas->ClipX - TotalW) * 0.5f - 40.f;
	const float Y0 = Canvas->ClipY - 36.f - 12.f - SS;

	int32 Arrows = 0;
	for (int32 T = 0; T < NumTiers; ++T) Arrows += Skills->CountItem(ArrowId(T));

	for (int32 i = 0; i < NumHotbar; ++i)
	{
		const float X = X0 + i * (SS + Gap);
		const bool bSel = (Skills->Selected == i);
		const int32 Id = Skills->Hotbar[i];
		DrawRect(bSel ? FLinearColor(1.f, 0.7f, 0.2f, 1.f) : FLinearColor(0.f, 0.f, 0.f, 0.7f), X - 3.f, Y0 - 3.f, SS + 6.f, SS + 6.f);
		DrawRect(FLinearColor(0.11f, 0.10f, 0.09f, 0.92f), X, Y0, SS, SS);
		DrawText(FString::Printf(TEXT("%d"), i + 1), bSel ? FLinearColor(1.f, 0.85f, 0.4f) : FLinearColor(0.75f, 0.75f, 0.75f), X + 4.f, Y0 + 2.f, Font, 0.95f);
		if (Id == 0) continue;
		DrawIcon(Id, X + 6.f, Y0 + 4.f, SS - 12.f);
		int32 A, B;
		if (HotbarDamage(Skills, i, A, B))
		{
			const FString Txt = FString::Printf(TEXT("%d-%d"), A, B);
			float TW = 0.f, TH = 0.f;
			GetTextSize(Txt, TW, TH, Font, 0.95f);
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), X + SS - TW - 8.f, Y0 + SS - TH - 4.f, TW + 6.f, TH + 2.f);
			DrawText(Txt, FLinearColor(1.f, 0.55f, 0.45f), X + SS - TW - 5.f, Y0 + SS - TH - 3.f, Font, 0.95f);
		}
		if (IsGear(Id) && KindOf(Id) == K_Bow)
			DrawText(FString::Printf(TEXT("x%d"), Arrows), Arrows > 0 ? FLinearColor(0.9f, 0.9f, 0.6f) : FLinearColor(1.f, 0.3f, 0.3f), X + SS - 34.f, Y0 + 2.f, Font, 0.95f);
		if (Over(X, Y0, SS, SS)) ItemTip(Id);
	}

	// Defence total, to the right of the bar
	{
		const float X = X0 + TotalW + 22.f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), X, Y0, 92.f, SS);
		const int32 Def = TotalDefense(Skills, Knight->ShieldActive());
		DrawText(TEXT("DEFENCE"), FLinearColor(0.7f, 0.75f, 0.9f), X + 8.f, Y0 + 6.f, Font, 0.9f);
		DrawText(FString::Printf(TEXT("%d"), Def), FLinearColor(0.55f, 0.75f, 1.f), X + 8.f, Y0 + 28.f, Font, 1.8f);
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

	// Backdrop: a gold ring round a dark disc (smooth, anti-aliased circles)
	DrawCircle(Cx, Cy, Half + 7.f, FLinearColor(0.f, 0.f, 0.f, 0.75f));
	DrawCircle(Cx, Cy, Half + 5.f, FLinearColor(0.62f, 0.44f, 0.18f, 1.f));
	DrawCircle(Cx, Cy, Half + 1.5f, FLinearColor(0.04f, 0.04f, 0.04f, 1.f));
	DrawCircle(Cx, Cy, Half, FLinearColor(0.12f, 0.13f, 0.12f, 0.92f));
	DrawCircle(Cx, Cy, Half * 0.5f + 0.75f, FLinearColor(1.f, 1.f, 1.f, 0.06f));   // a faint inner ring at half range
	DrawCircle(Cx, Cy, Half * 0.5f - 0.75f, FLinearColor(0.12f, 0.13f, 0.12f, 1.f));

	// World (X forward, Y right) -> map (up, right). Returns false if off the map unless clamped to the edge.
	auto ToMap = [&](const FVector2D& W, float R, bool bClamp, float& OutX, float& OutY) -> bool
	{
		float dx = (W.Y - PL.Y) * K;   // map right
		float dy = -(W.X - PL.X) * K;  // map down
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
	// Enemies (any other character)
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		if (*It == Pawn) continue;
		Marker(FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y), 3.f, FLinearColor(0.95f, 0.15f, 0.1f), false);
	}

	// You: a filled arrow pointing the way you face
	{
		const FVector F = Pawn->GetActorForwardVector();
		const FVector2D Fwd = FVector2D(F.Y, -F.X).GetSafeNormal();   // map direction
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
		auto Letter = [&](const TCHAR* T, float Dx, float Dy, const FLinearColor& Col)
		{
			float TW = 0.f, TH = 0.f;
			GetTextSize(T, TW, TH, Font, 0.85f);
			const float X = Cx + Dx * (Half + 3.f), Y = Cy + Dy * (Half + 3.f);
			DrawCircle(X, Y, 9.f, FLinearColor(0.04f, 0.04f, 0.04f, 1.f));
			DrawText(T, Col, X - TW * 0.5f, Y - TH * 0.5f, Font, 0.85f);
		};
		const FLinearColor CC(1.f, 1.f, 1.f, 0.85f);
		Letter(TEXT("N"), 0.f, -1.f, FLinearColor(1.f, 0.35f, 0.3f, 1.f));
		Letter(TEXT("S"), 0.f, 1.f, CC);
		Letter(TEXT("W"), -1.f, 0.f, CC);
		Letter(TEXT("E"), 1.f, 0.f, CC);
	}
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
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.88f), PX, PY, PW, PH);
	DrawRect(FLinearColor(0.55f, 0.32f, 0.08f, 1.f), PX, PY, PW, 4.f);
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
		DrawText(FString::Printf(TEXT("Look sensitivity:  %d%%"), FMath::RoundToInt(Look * 100.f)), FLinearColor::White, PX + 40.f, PY + 96.f, Font, Scale);
		Slider(LookY, FMath::Clamp((Look - 0.1f) / 1.9f, 0.f, 1.f), FLinearColor(0.9f, 0.55f, 0.15f));

		const float RS = Knight->GetRenderScale();
		DrawText(FString::Printf(TEXT("Render resolution:  %d%%   (lower = faster)"), FMath::RoundToInt(RS * 100.f)), FLinearColor::White, PX + 40.f, PY + 186.f, Font, Scale);
		Slider(ScaleY, FMath::Clamp((RS - 0.5f) / 0.5f, 0.f, 1.f), FLinearColor(0.3f, 0.7f, 0.9f));

		DrawRect(FLinearColor::White, BoxX, BoxY, 26.f, 26.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 1.f), BoxX + 2.f, BoxY + 2.f, 22.f, 22.f);
		if (Knight->GetFancyLighting()) DrawRect(FLinearColor(0.9f, 0.55f, 0.15f), BoxX + 5.f, BoxY + 5.f, 16.f, 16.f);
		DrawText(TEXT("Fancy lighting (bounce light, clouds) - slower"), FLinearColor::White, BoxX + 38.f, BoxY + 2.f, Font, 1.1f);

		DrawText(TEXT("Click or drag the sliders.   Press O to close."), FLinearColor(0.8f, 0.8f, 0.8f), PX + 40.f, PY + PH - 40.f, Font, 1.0f);
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
			DrawText(R.Key, FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, RowY, Font, 1.1f);
			DrawText(R.Action, FLinearColor::White, PX + 250.f, RowY, Font, 1.05f);
			RowY += 30.f;
		}
		DrawText(TEXT("Press O to close."), FLinearColor(0.8f, 0.8f, 0.8f), PX + 30.f, PY + PH - 40.f, Font, 1.0f);
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
	DrawText(E.Name, FLinearColor(1.f, 0.85f, 0.3f), CX, Y, Font, 1.7f);
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
		for (int32 c = 0; c < 5; ++c) DrawText(Heads[c], FLinearColor(0.7f, 0.7f, 0.7f), CX + Xs[c], Y, Font, 0.95f);
		Y += 28.f;
		for (int32 t = 0; t < NumTiers; ++t)
		{
			int32 A, B; WeaponDamage(K_Bow, t, A, B, t);
			const FRecipe R = SmithRecipe(K_Bow, t, true);
			DrawIcon(ArrowId(t), CX - 4.f, Y - 4.f, 28.f);
			DrawText(TierName(t), FLinearColor::White, CX + 30.f, Y, Font, 1.0f);
			DrawText(FString::Printf(TEXT("%d-%d"), A, B), FLinearColor(1.f, 0.6f, 0.5f), CX + Xs[1], Y, Font, 1.0f);
			DrawText(FString::Printf(TEXT("%d"), R.Level), FLinearColor::White, CX + Xs[2], Y, Font, 1.0f);
			DrawText(RecipeText(R), FLinearColor(0.9f, 0.9f, 0.8f), CX + Xs[3], Y, Font, 1.0f);
			DrawText(t <= StoreMaxTier ? FString::Printf(TEXT("%d ingots"), ArrowBundlePrice(t)) : FString(TEXT("smith only")), FLinearColor(1.f, 0.85f, 0.3f), CX + Xs[4], Y, Font, 1.0f);
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
			DrawText(CapeColorName(c), FLinearColor::White, CX + 40.f, Y, Font, 1.0f);
			DrawText(FString::Printf(TEXT("%d ingots"), BuyPrice(K_Cape, c)), FLinearColor(1.f, 0.85f, 0.3f), CX + 200.f, Y, Font, 1.0f);
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
			DrawText(AccessoryName(K, v), FLinearColor(1.f, 0.9f, 0.6f), CX + 46.f, Y, Font, 1.05f);
			DrawText(AccessoryText(K, v), FLinearColor(0.85f, 0.85f, 0.85f), CX + 46.f, Y + 20.f, Font, 0.95f);
			DrawText(FString::Printf(TEXT("%d ingots"), AccessoryPrice(K, v)), FLinearColor(1.f, 0.85f, 0.3f), CX + CW - 120.f, Y, Font, 1.0f);
			Y += 50.f;
		}
		Wrapped(TEXT("Rings and amulets are sold at the store. Two rings and one amulet can be worn at once."), CX, Y + 8.f, CW, 0.95f, FLinearColor(0.7f, 0.7f, 0.7f));
		return;
	}

	// Weapons, shield and armour: one row per rank
	const bool bWeapon = IsWeaponKind(K);
	const TCHAR* Heads[] = { TEXT("RANK"), bWeapon ? TEXT("DAMAGE") : TEXT("DEFENCE"), TEXT("SMITH LV"), TEXT("MATERIALS"), TEXT("STORE") };
	const float Xs[] = { 44.f, 150.f, 250.f, 350.f, 580.f };
	for (int32 c = 0; c < 5; ++c) DrawText(Heads[c], FLinearColor(0.7f, 0.7f, 0.7f), CX + Xs[c], Y, Font, 0.95f);
	Y += 28.f;
	for (int32 t = 0; t < NumTiers; ++t)
	{
		const FRecipe R = SmithRecipe(K, t);
		DrawIcon(GearId(K, t), CX - 2.f, Y - 6.f, 36.f);
		DrawText(TierName(t), FLinearColor::White, CX + Xs[0], Y, Font, 1.0f);
		if (bWeapon)
		{
			int32 A, B; WeaponDamage(K, t, A, B, t);
			DrawText(FString::Printf(TEXT("%d-%d"), A, B), FLinearColor(1.f, 0.6f, 0.5f), CX + Xs[1], Y, Font, 1.0f);
		}
		else DrawText(FString::Printf(TEXT("%d"), DefenseOf(K, t)), FLinearColor(0.55f, 0.75f, 1.f), CX + Xs[1], Y, Font, 1.0f);
		DrawText(FString::Printf(TEXT("%d"), R.Level), FLinearColor::White, CX + Xs[2], Y, Font, 1.0f);
		DrawText(RecipeText(R), FLinearColor(0.9f, 0.9f, 0.8f), CX + Xs[3], Y, Font, 1.0f);
		DrawText(t <= StoreMaxTier ? FString::Printf(TEXT("%d ingots"), BuyPrice(K, t)) : FString(TEXT("smith only")), FLinearColor(1.f, 0.85f, 0.3f), CX + Xs[4], Y, Font, 1.0f);
		if (K == K_Boots) DrawText(FString::Printf(TEXT("stamina x%.2f"), BootStaminaMult(t)), FLinearColor(0.6f, 0.9f, 0.6f), CX + Xs[1], Y + 18.f, Font, 0.85f);
		Y += (K == K_Boots) ? 42.f : 38.f;
	}
	if (K == K_Pickaxe || K == K_Axe || K == K_Rod)
		Wrapped(TEXT("Gathering time compared with wood: bronze takes 14% less, iron 28% less, steel 42% less and diamond 56% less."), CX, Y + 6.f, CW, 0.95f, FLinearColor(0.7f, 0.9f, 0.7f));
}

// ===================================================================================================
// Drawing helpers and icons
// ===================================================================================================
void AKnightHUD::Poly(const TArray<FVector2D>& P, const FLinearColor& C)
{
	// Filled by thin horizontal strips (works for any shape whose rows are one solid run)
	if (P.Num() < 3) return;
	float MinY = P[0].Y, MaxY = P[0].Y;
	for (const FVector2D& V : P) { MinY = FMath::Min(MinY, V.Y); MaxY = FMath::Max(MaxY, V.Y); }
	const float Step = PolyStep;
	for (float Y = MinY + Step * 0.5f; Y < MaxY; Y += Step)
	{
		float X0 = 1e9f, X1 = -1e9f;
		for (int32 i = 0; i < P.Num(); ++i)
		{
			const FVector2D& A = P[i];
			const FVector2D& B = P[(i + 1) % P.Num()];
			if ((A.Y <= Y && B.Y > Y) || (B.Y <= Y && A.Y > Y))
			{
				const float X = A.X + (Y - A.Y) / (B.Y - A.Y) * (B.X - A.X);
				X0 = FMath::Min(X0, X);
				X1 = FMath::Max(X1, X);
			}
		}
		if (X1 > X0) DrawRect(C, X0, Y - Step * 0.5f, X1 - X0, Step + 0.3f);
	}
}

void AKnightHUD::Disc(float Cx, float Cy, float Rx, float Ry, const FLinearColor& C)
{
	TArray<FVector2D> P;
	for (int32 i = 0; i < 18; ++i)
	{
		const float A = i * 2.f * PI / 18.f;
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

void AKnightHUD::DrawIcon(int32 Id, float X, float Y, float S)
{
	PolyStep = FMath::Clamp(S / 44.f, 1.f, 2.5f);
	const float U = S / 64.f;
	auto P = [&](float A, float B) { return FVector2D(X + A * U, Y + B * U); };
	auto Quad = [&](float x0, float y0, float x1, float y1, const FLinearColor& C) { Poly({ P(x0, y0), P(x1, y0), P(x1, y1), P(x0, y1) }, C); };
	auto DiscP = [&](float Cx, float Cy, float Rx, float Ry, const FLinearColor& C) { Disc(X + Cx * U, Y + Cy * U, Rx * U, Ry * U, C); };

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
			Poly({ P(10, 6), P(54, 6), P(54, 28), P(32, 60), P(10, 28) }, Tr);
			Poly({ P(14, 10), P(50, 10), P(50, 27), P(32, 54), P(14, 27) }, Pl);
			Quad(29, 12, 35, 46, Ac);
			Quad(19, 21, 45, 27, Ac);
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
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.9f), PX, PY, PW, PH);
	DrawRect(FLinearColor(0.55f, 0.32f, 0.08f, 1.f), PX, PY, PW, 4.f);

	if (DrawPackTabs(Knight, PX, PY, false)) return;
	{
		const FString Bal = FString::Printf(TEXT("%d"), Skills->GetIngots());
		float TW = 0.f, TH = 0.f;
		GetTextSize(Bal, TW, TH, Font, 1.8f);
		DrawText(Bal, FLinearColor(1.f, 0.85f, 0.3f), PX + PW - 100.f - TW, PY + 14.f, Font, 1.8f);
		DrawIngot(PX + PW - 92.f, PY + 10.f, 40.f);
	}
	if (CloseBtn(PX, PY, PW)) { Knight->ToggleInventory(); return; }

	auto SlotBox = [&](float X, float Y, float S, bool bOver, bool bSel)
	{
		DrawRect(bSel ? FLinearColor(1.f, 0.7f, 0.2f, 1.f) : (bOver ? FLinearColor(0.50f, 0.38f, 0.16f, 1.f) : FLinearColor(0.32f, 0.26f, 0.16f, 1.f)), X, Y, S, S);
		DrawRect(FLinearColor(0.10f, 0.09f, 0.08f, 1.f), X + 3.f, Y + 3.f, S - 6.f, S - 6.f);
	};

	// ----- The pack: 16 slots -----
	DrawText(TEXT("PACK"), FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, PY + 64.f, Font, 1.2f);
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
			DrawText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, SX + SS - 30.f, SY + SS - 24.f, Font, 1.1f);
		if (bOver)
		{
			ItemTip(S.Id);
			if (bUIClick && IsGear(S.Id)) { Knight->EquipFromInventory(i); }
		}
	}

	// ----- Weapon bar -----
	const float EX = GX + 4 * (SS + Gap) + 34.f;
	DrawText(TEXT("WEAPON BAR  (keys 1-5)"), FLinearColor(1.f, 0.85f, 0.3f), EX, PY + 64.f, Font, 1.2f);
	const float HS = 66.f, HG = 10.f;
	for (int32 h = 0; h < NumHotbar; ++h)
	{
		const float X = EX + h * (HS + HG), Y = PY + 92.f;
		const bool bOver = Over(X, Y, HS, HS);
		SlotBox(X, Y, HS, bOver, Skills->Selected == h);
		DrawText(FString::Printf(TEXT("%d"), h + 1), FLinearColor(0.7f, 0.7f, 0.7f), X + 4.f, Y + 2.f, Font, 0.9f);
		const int32 Id = Skills->Hotbar[h];
		if (Id == 0) continue;
		DrawIcon(Id, X + 7.f, Y + 5.f, HS - 14.f);
		int32 A, B;
		if (HotbarDamage(Skills, h, A, B))
			DrawText(FString::Printf(TEXT("%d-%d"), A, B), FLinearColor(1.f, 0.55f, 0.45f), X + HS - 44.f, Y + HS - 20.f, Font, 0.9f);
		if (bOver)
		{
			ItemTip(Id);
			if (bUIClick) Knight->UnequipHotbar(h);
		}
	}

	// ----- Worn gear -----
	DrawText(TEXT("WORN"), FLinearColor(1.f, 0.85f, 0.3f), EX, PY + 170.f, Font, 1.2f);
	struct FWornSlot { int32 Kind; bool bSecond; const TCHAR* Label; };
	static const FWornSlot Worn[10] = {
		{ K_Shield, false, TEXT("Shield") }, { K_Helmet, false, TEXT("Helmet") }, { K_Chest, false, TEXT("Chest") }, { K_Shoulders, false, TEXT("Shoulders") }, { K_Gloves, false, TEXT("Gloves") },
		{ K_Boots, false, TEXT("Boots") }, { K_Cape, false, TEXT("Cape") }, { K_Ring, false, TEXT("Ring") }, { K_Ring, true, TEXT("Ring") }, { K_Amulet, false, TEXT("Amulet") },
	};
	for (int32 w = 0; w < 10; ++w)
	{
		const float X = EX + (w % 5) * (HS + HG), Y = PY + 198.f + (w / 5) * (HS + 34.f);
		const bool bOver = Over(X, Y, HS, HS);
		SlotBox(X, Y, HS, bOver, false);
		DrawText(Worn[w].Label, FLinearColor(0.7f, 0.7f, 0.7f), X + 2.f, Y + HS + 4.f, Font, 0.85f);
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
		DrawText(FString::Printf(TEXT("Defence: %d%s"), TotalDefense(Skills, Knight->ShieldActive()), Knight->ShieldActive() ? TEXT("  (shield included)") : TEXT("")), FLinearColor(0.6f, 0.78f, 1.f), EX, Y0, Font, 1.1f);
		const int32 Sel = Skills->SelectedItem();
		if (IsGear(Sel) && IsWeaponKind(KindOf(Sel)))
		{
			int32 A, B;
			HotbarDamage(Skills, Skills->Selected, A, B);
			DrawText(FString::Printf(TEXT("In hand: %s    Damage %d-%d"), *ItemName(Sel), A, B), FLinearColor(1.f, 0.65f, 0.55f), EX, Y0 + 26.f, Font, 1.1f);
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

	DrawText(TEXT("Click gear in your pack to hold or wear it.  Click worn gear to take it off.  Tab to close, K for skills."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 30.f, Font, 1.0f);
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
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.92f), PX, PY, PW, PH);
	DrawRect(FLinearColor(0.55f, 0.32f, 0.08f, 1.f), PX, PY, PW, 4.f);
	DrawText(TEXT("THE TRADING POST"), FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, PY + 14.f, Font, 1.8f);
	{
		const FString Bal = FString::Printf(TEXT("%d"), Skills->GetIngots());
		float TW = 0.f, TH = 0.f;
		GetTextSize(Bal, TW, TH, Font, 1.8f);
		DrawText(Bal, FLinearColor(1.f, 0.85f, 0.3f), PX + PW - 100.f - TW, PY + 14.f, Font, 1.8f);
		DrawIngot(PX + PW - 92.f, PY + 10.f, 40.f);
	}
	if (CloseBtn(PX, PY, PW)) { Knight->CloseShop(); return; }

	// ----- Sell (left) -----
	DrawText(TEXT("SELL"), FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, PY + 62.f, Font, 1.3f);
		static const EItemType Sellable[12] = { EItemType::Logs, EItemType::PineLogs, EItemType::RedwoodLogs, EItemType::CopperOre, EItemType::Coal, EItemType::Diamond, EItemType::Bacon, EItemType::Perch, EItemType::Trout, EItemType::GoldenCarp, EItemType::Hide, EItemType::Bones };
	static const TCHAR* SellNames[12] = { TEXT("Logs"), TEXT("Pine logs"), TEXT("Redwood logs"), TEXT("Copper ore"), TEXT("Coal"), TEXT("Diamond"), TEXT("Raw pork"), TEXT("Perch"), TEXT("Trout"), TEXT("Golden carp"), TEXT("Beast hide"), TEXT("Bones") };
	for (int32 r = 0; r < 12; ++r)
	{
		const EItemType T = Sellable[r];
		const float RY = PY + 94.f + r * 38.f;
		const int32 N = Skills->GetItemCount(T);
		DrawRect(FLinearColor(0.16f, 0.14f, 0.11f, 1.f), PX + 30.f, RY, 510.f, 34.f);
		DrawIcon(MaterialId(T), PX + 34.f, RY + 1.f, 32.f);
		DrawText(SellNames[r], FLinearColor::White, PX + 74.f, RY + 8.f, Font, 1.0f);
		DrawText(FString::Printf(TEXT("x %d"), N), FLinearColor(0.85f, 0.85f, 0.85f), PX + 210.f, RY + 8.f, Font, 1.05f);
		DrawIngot(PX + 280.f, RY + 4.f, 24.f);
		DrawText(FString::Printf(TEXT("%d each"), SellValue(T)), FLinearColor(1.f, 0.85f, 0.3f), PX + 310.f, RY + 8.f, Font, 1.0f);
		if (Btn(PX + 430.f, RY + 2.f, 104.f, 30.f, TEXT("SELL ALL"), N > 0, false, 1.0f))
		{
			const int32 Pay = Skills->SellAll(T);
			ShopMsg = FString::Printf(TEXT("Sold for %d ingots."), Pay);
			ShopMsgUntil = Now + 3.f;
		}
	}
	DrawText(TEXT("Click gear or arrows in your pack to sell them:"), FLinearColor(0.75f, 0.75f, 0.75f), PX + 30.f, PY + 94.f + 10 * 38.f + 6.f, Font, 0.95f);
	for (int32 i = 0; i < NumSlots; ++i)
	{
		const float X = PX + 30.f + (i % 8) * 64.f, Y = PY + 94.f + 10 * 38.f + 32.f + (i / 8) * 62.f;
		const FInvSlot S = Skills->Slots[i];
		const bool bSellable = S.Id != 0 && (IsGear(S.Id) || IsArrow(S.Id));
		const bool bOver = Over(X, Y, 58.f, 58.f);
		DrawRect(bOver && bSellable ? FLinearColor(0.50f, 0.38f, 0.16f) : FLinearColor(0.26f, 0.21f, 0.13f), X, Y, 58.f, 58.f);
		DrawRect(FLinearColor(0.10f, 0.09f, 0.08f), X + 2.f, Y + 2.f, 54.f, 54.f);
		if (S.Id == 0) continue;
		DrawIcon(S.Id, X + 5.f, Y + 3.f, 48.f);
		if (StackSize(S.Id) > 1) DrawText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, X + 36.f, Y + 38.f, Font, 0.85f);
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
		DrawText(TEXT("TODAY'S WARES"), FLinearColor(1.f, 0.85f, 0.3f), RX, PY + 62.f, Font, 1.3f);
		{
			const int32 Mins = JeebStore::MinutesToRestock();
			const FString Re = Mins <= 0 ? FString(TEXT("Restocking any moment")) : FString::Printf(TEXT("New stock in %d min"), Mins + 1);
			float TW = 0.f, TH = 0.f;
			GetTextSize(Re, TW, TH, Font, 1.0f);
			DrawText(Re, FLinearColor(0.75f, 0.75f, 0.75f), RX + RW - TW, PY + 66.f, Font, 1.0f);
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
			DrawRect(bOver ? FLinearColor(0.30f, 0.24f, 0.13f) : FLinearColor(0.16f, 0.14f, 0.11f), CX, CY, CW, CH);
			if (i >= 8) DrawRect(FLinearColor(0.75f, 0.55f, 0.15f, 0.9f), CX, CY, CW, 3.f);       // the valuable row gets a gold edge
			DrawIcon(W.Id, CX + 6.f, CY + 6.f, 58.f);
			FString Name = W.Count > 1 ? FString::Printf(TEXT("%d %s"), W.Count, *ItemName(W.Id)) : ItemName(W.Id);
			Wrapped(Name, CX + 70.f, CY + 8.f, CW - 76.f, 0.85f, bSoldOut ? FLinearColor(0.45f, 0.45f, 0.45f) : FLinearColor::White);
			FString Stat;
			if (IsGear(W.Id))
			{
				const int32 K = KindOf(W.Id), V = VariantOf(W.Id);
				if (IsWeaponKind(K)) { int32 A, B; WeaponDamage(K, V, A, B, V); Stat = FString::Printf(TEXT("Damage %d-%d"), A, B); }
				else if (K == K_Shield || IsArmorKind(K)) Stat = FString::Printf(TEXT("Defence %d"), DefenseOf(K, V));
				else if (IsAccessoryKind(K)) Stat = AccessoryText(K, V);
			}
			else if (IsArrow(W.Id)) { int32 A, B; WeaponDamage(K_Bow, ArrowTier(W.Id), A, B, ArrowTier(W.Id)); Stat = FString::Printf(TEXT("Bow damage %d-%d"), A, B); }
			if (!Stat.IsEmpty()) Wrapped(Stat, CX + 8.f, CY + 70.f, CW - 16.f, 0.78f, FLinearColor(0.75f, 0.8f, 0.95f));
			DrawIngot(CX + 8.f, CY + CH - 30.f, 22.f);
			DrawText(FString::Printf(TEXT("%d"), W.Price), bAfford ? FLinearColor(1.f, 0.85f, 0.3f) : FLinearColor(0.9f, 0.3f, 0.25f), CX + 36.f, CY + CH - 28.f, Font, 1.1f);
			const FString Left = bSoldOut ? FString(TEXT("Sold out")) : FString::Printf(TEXT("%d left"), W.Stock);
			float TW = 0.f, TH = 0.f;
			GetTextSize(Left, TW, TH, Font, 0.85f);
			DrawText(Left, bSoldOut ? FLinearColor(0.9f, 0.35f, 0.3f) : FLinearColor(0.7f, 0.7f, 0.7f), CX + CW - TW - 8.f, CY + CH - 25.f, Font, 0.85f);
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
			DrawRect(FLinearColor(0.22f, 0.12f, 0.20f), RX, BY, RW, BH);
			DrawRect(FLinearColor(0.70f, 0.40f, 0.85f, 0.9f), RX, BY, RW, 3.f);
			// a little drawstring sack
			Disc(RX + 38.f, BY + 44.f, 22.f, 20.f, FLinearColor(0.55f, 0.36f, 0.18f));
			Disc(RX + 38.f, BY + 20.f, 9.f, 5.f, FLinearColor(0.45f, 0.28f, 0.14f));
			DrawText(TEXT("?"), FLinearColor(1.f, 0.9f, 0.5f), RX + 33.f, BY + 32.f, Font, 1.3f);
			DrawText(TEXT("MYSTERY SATCHEL"), FLinearColor(0.95f, 0.8f, 1.f), RX + 76.f, BY + 10.f, Font, 1.15f);
			DrawText(bUsed ? FString::Printf(TEXT("Opened this hour. Another in %d min."), JeebStore::MinutesToRestock() + 1)
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
		if (Now < ShopMsgUntil) DrawText(ShopMsg, FLinearColor(0.6f, 1.f, 0.6f), RX, PY + PH - 30.f, Font, 1.05f);
	}
	DrawText(TEXT("Press E or X to leave."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 30.f, Font, 1.0f);
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
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.92f), PX, PY, PW, PH);
	DrawRect(FLinearColor(0.75f, 0.30f, 0.08f, 1.f), PX, PY, PW, 4.f);
	DrawText(TEXT("THE SMITHY"), FLinearColor(1.f, 0.6f, 0.25f), PX + 30.f, PY + 14.f, Font, 1.8f);
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
			DrawText(FString::Printf(TEXT("%s  %d"), Mats[m].N, Mats[m].Have), FLinearColor::White, X + 40.f, PY + 66.f, Font, 1.1f);
		}
		int32 Lv, Into, Need;
		Skills->GetProgress(ESkillType::Smithing, Lv, Into, Need);
		DrawText(FString::Printf(TEXT("Smithing  Lv %d"), Lv), FLinearColor(1.f, 0.7f, 0.4f), PX + PW - 330.f, PY + 56.f, Font, 1.2f);
		DrawRect(FLinearColor(0.15f, 0.15f, 0.15f), PX + PW - 330.f, PY + 86.f, 290.f, 9.f);
		DrawRect(FLinearColor(0.8f, 0.3f, 0.15f), PX + PW - 330.f, PY + 86.f, 290.f * (Need > 0 ? float(Into) / float(Need) : 0.f), 9.f);
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
		DrawRect(FLinearColor(0.16f, 0.14f, 0.11f), CX, CY, CW, CH);
		DrawRect(Palette(t, 0), CX, CY, CW, 5.f);
		DrawIcon(Id, CX + 25.f, CY + 14.f, 100.f);
		float Y = CY + 120.f;
		DrawText(bArrows ? FString::Printf(TEXT("%s Arrows"), TierName(t)) : ItemName(Id), FLinearColor(1.f, 0.9f, 0.6f), CX + 8.f, Y, Font, 1.05f);
		Y += 26.f;
		if (bArrows)
		{
			int32 A, B; WeaponDamage(K_Bow, t, A, B, t);
			DrawText(FString::Printf(TEXT("Makes %d arrows"), ArrowBundle), FLinearColor(0.9f, 0.9f, 0.9f), CX + 8.f, Y, Font, 0.95f); Y += 20.f;
			DrawText(FString::Printf(TEXT("Bow damage %d-%d"), A, B), FLinearColor(1.f, 0.6f, 0.5f), CX + 8.f, Y, Font, 0.95f); Y += 26.f;
		}
		else if (IsWeaponKind(Kind))
		{
			int32 A, B; WeaponDamage(Kind, t, A, B, t);
			DrawText(FString::Printf(TEXT("Damage %d-%d"), A, B), FLinearColor(1.f, 0.6f, 0.5f), CX + 8.f, Y, Font, 0.95f); Y += 20.f;
			if (!AllowsShield(Kind)) { DrawText(TEXT("Two-handed"), FLinearColor(0.7f, 0.7f, 0.7f), CX + 8.f, Y, Font, 0.85f); }
			Y += 20.f;
		}
		else
		{
			DrawText(FString::Printf(TEXT("Defence %d"), DefenseOf(Kind, t)), FLinearColor(0.6f, 0.78f, 1.f), CX + 8.f, Y, Font, 0.95f); Y += 20.f;
			if (Kind == K_Boots) DrawText(FString::Printf(TEXT("Stamina x%.2f"), BootStaminaMult(t)), FLinearColor(0.6f, 0.9f, 0.6f), CX + 8.f, Y, Font, 0.85f);
			Y += 20.f;
		}

		// Materials: have / need
		DrawText(TEXT("MATERIALS"), FLinearColor(0.65f, 0.65f, 0.65f), CX + 8.f, Y, Font, 0.85f); Y += 20.f;
		bool bHaveAll = true;
		auto MatLine = [&](EItemType Ty, const TCHAR* Name, int32 Have, int32 Need)
		{
			if (Need <= 0) return;
			const bool bOk = Have >= Need;
			if (!bOk) bHaveAll = false;
			DrawIcon(MaterialId(Ty), CX + 6.f, Y - 3.f, 24.f);
			DrawText(FString::Printf(TEXT("%s %d/%d"), Name, Have, Need), bOk ? FLinearColor(0.7f, 1.f, 0.7f) : FLinearColor(1.f, 0.45f, 0.4f), CX + 34.f, Y, Font, 0.95f);
			Y += 26.f;
		};
		MatLine(EItemType::Logs, TEXT("Logs"), Logs, R.Logs);
		MatLine(EItemType::CopperOre, TEXT("Copper"), Copper, R.Copper);
		MatLine(EItemType::Coal, TEXT("Coal"), Coal, R.Coal);
		MatLine(EItemType::Diamond, TEXT("Diamond"), Gems, R.Diamonds);

		const int32 Lv = Skills->GetLevel(ESkillType::Smithing);
		const bool bLevelOk = Lv >= R.Level;
		Y += 6.f;
		DrawText(FString::Printf(TEXT("Needs Smithing %d"), R.Level), bLevelOk ? FLinearColor(0.7f, 1.f, 0.7f) : FLinearColor(1.f, 0.45f, 0.4f), CX + 8.f, Y, Font, 0.95f); Y += 24.f;
		DrawText(FString::Printf(TEXT("+%d XP"), R.XP), FLinearColor(0.5f, 0.85f, 1.f), CX + 8.f, Y, Font, 0.95f);

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
	if (Now < ShopMsgUntil) DrawText(ShopMsg, FLinearColor(0.6f, 1.f, 0.6f), PX + 370.f, PY + PH - 64.f, Font, 1.1f);
	DrawText(TEXT("Wood uses logs, bronze copper, iron coal, steel coal and copper, diamond diamonds and coal.   Press E or X to leave."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 30.f, Font, 0.95f);
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
	const float W = FMath::Min(960.f, Canvas->ClipX - 80.f), H = 200.f;
	const float X = (Canvas->ClipX - W) * 0.5f, Y = Canvas->ClipY - H - 110.f;
	auto Shadowed = [&](const FString& Txt, float Tx, float Ty, float Sc, const FLinearColor& Col)
	{
		DrawText(Txt, FLinearColor(0.f, 0.f, 0.f, 0.9f), Tx + 2.f, Ty + 2.f, Font, Sc);
		DrawText(Txt, FLinearColor(0.f, 0.f, 0.f, 0.9f), Tx - 1.f, Ty + 1.f, Font, Sc);
		DrawText(Txt, Col, Tx, Ty, Font, Sc);
	};
	Shadowed(Name, X + 26.f, Y + 14.f, 1.6f, FLinearColor(1.f, 0.85f, 0.3f));
	Wrapped(Line, X + 28.f, Y + 62.f, W - 52.f, 1.25f, FLinearColor(0.f, 0.f, 0.f, 0.9f));
	Wrapped(Line, X + 26.f, Y + 60.f, W - 52.f, 1.25f, FLinearColor(0.97f, 0.97f, 0.97f));
	const FString Hint = bLast ? TEXT("Press E or click to continue") : TEXT("Press E or click for more");
	float TW = 0.f, TH = 0.f;
	GetTextSize(Hint, TW, TH, Font, 1.0f);
	Shadowed(Hint, X + W - TW - 24.f, Y + H - TH - 14.f, 1.0f, FLinearColor(1.f, 0.9f, 0.4f));
	if (bUIClick && Over(X, Y, W, H)) Knight->AdvanceDialogue();
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
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.9f), PX, PY, PW, PH);
	DrawRect(FLinearColor(0.55f, 0.32f, 0.08f, 1.f), PX, PY, PW, 4.f);
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
		GetTextSize(Tot, TW, TH, Font, 1.3f);
		DrawText(Tot, FLinearColor(1.f, 0.85f, 0.3f), PX + PW - 60.f - TW, PY + 20.f, Font, 1.3f);
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
		DrawRect(bOver ? FLinearColor(0.50f, 0.38f, 0.16f, 1.f) : FLinearColor(0.32f, 0.26f, 0.16f, 1.f), X, Y, BW, BH);
		DrawRect(FLinearColor(0.10f, 0.09f, 0.08f, 1.f), X + 3.f, Y + 3.f, BW - 6.f, BH - 6.f);
		DrawSkillIcon(static_cast<int32>(T), X + 16.f, Y + 16.f, 96.f);

		int32 Lv, Into, Need;
		Sk->GetProgress(T, Lv, Into, Need);
		DrawText(USkillComponent::SkillName(T), FLinearColor(0.95f, 0.9f, 0.8f), X + 126.f, Y + 18.f, Font, 1.2f);
		const FString LvS = FString::FromInt(Lv);
		float TW = 0.f, TH = 0.f;
		GetTextSize(LvS, TW, TH, Font, 2.8f);
		DrawText(LvS, FLinearColor(1.f, 0.9f, 0.45f), X + BW - 22.f - TW, Y + 48.f, Font, 2.8f);
		const float F = Need > 0 ? FMath::Clamp((float)Into / (float)Need, 0.f, 1.f) : 0.f;
		DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 1.f), X + 16.f, Y + BH - 24.f, BW - 32.f, 10.f);
		DrawRect(FLinearColor(0.35f, 0.8f, 0.3f), X + 16.f, Y + BH - 24.f, (BW - 32.f) * F, 10.f);
		DrawText(FString::Printf(TEXT("%d / %d XP"), Into, Need), FLinearColor(0.7f, 0.7f, 0.7f), X + 126.f, Y + BH - 46.f, Font, 0.9f);
		if (bOver)
		{
			SetTip({ USkillComponent::SkillName(T), FString::Printf(TEXT("Level %d"), Lv), FString::Printf(TEXT("%d XP to level %d"), FMath::Max(0, Need - Into), Lv + 1) });
		}
	}
	DrawText(TEXT("K or Tab to close."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 30.f, Font, 1.0f);
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

	DrawText(FString::Printf(TEXT("Fragments heard:  %d / %d      Talk to the people of Jeebsland. They remember things."), Sk->LoreFound.Num(), E.Num()),
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
		DrawRect(LoreSel == i ? FLinearColor(0.55f, 0.32f, 0.08f, 1.f) : (bOver ? FLinearColor(0.30f, 0.24f, 0.12f, 1.f) : FLinearColor(0.14f, 0.13f, 0.12f, 1.f)), LX, Y, LW, RowH - 4.f);
		DrawText(bKnown ? FString(E[i].Title) : FString(TEXT("???")), bKnown ? FLinearColor(1.f, 0.92f, 0.65f) : FLinearColor(0.4f, 0.4f, 0.4f), LX + 10.f, Y + 5.f, Font, 1.05f);
		if (bOver && bUIClick) LoreSel = i;
	}

	// Reading pane
	const float RX = PX + 440.f, RY = PY + 130.f, RW = PW - 440.f - 30.f;
	DrawRect(FLinearColor(0.08f, 0.07f, 0.06f, 1.f), RX - 10.f, RY - 6.f, RW + 20.f, PH - 130.f - 60.f);
	if (LoreSel >= 0 && E.IsValidIndex(LoreSel) && Sk->LoreFound.Contains(LoreSel))
	{
		const JeebLore::FEntry& En = E[LoreSel];
		DrawText(En.Title, FLinearColor(1.f, 0.85f, 0.3f), RX, RY, Font, 1.5f);
		DrawText(FString::Printf(TEXT("as told by %s"), JeebLore::SpeakerName(En.Speaker)), FLinearColor(0.65f, 0.65f, 0.7f), RX, RY + 34.f, Font, 0.95f);
		TArray<FString> Parts;
		FString(En.Text).ParseIntoArray(Parts, TEXT("|"), true);
		float Y = RY + 72.f;
		for (const FString& P : Parts) Y += Wrapped(P, RX, Y, RW, 1.1f, FLinearColor(0.95f, 0.93f, 0.88f)) + 14.f;
	}
	else
	{
		DrawText(TEXT("Choose a fragment to read it."), FLinearColor(0.5f, 0.5f, 0.5f), RX, RY, Font, 1.1f);
	}
	DrawText(TEXT("Press O to close."), FLinearColor(0.8f, 0.8f, 0.8f), PX + 30.f, PY + PH - 40.f, Font, 1.0f);
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
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.92f), PX, PY, PW, PH);
	DrawRect(FLinearColor(0.55f, 0.32f, 0.08f, 1.f), PX, PY, PW, 4.f);
	DrawText(TEXT("THE VAULT"), FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, PY + 14.f, Font, 1.8f);
	if (CloseBtn(PX, PY, PW)) { Knight->CloseBank(); return; }

	auto SlotBox = [&](float X, float Y, float S, bool bOver)
	{
		DrawRect(bOver ? FLinearColor(0.50f, 0.38f, 0.16f, 1.f) : FLinearColor(0.32f, 0.26f, 0.16f, 1.f), X, Y, S, S);
		DrawRect(FLinearColor(0.10f, 0.09f, 0.08f, 1.f), X + 3.f, Y + 3.f, S - 6.f, S - 6.f);
	};

	// ----- Your pack -----
	DrawText(TEXT("YOUR PACK  (click to deposit)"), FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, PY + 62.f, Font, 1.1f);
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
		if (StackSize(S.Id) > 1) DrawText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, X + SS - 28.f, Y + SS - 22.f, Font, 1.0f);
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
	DrawText(FString::Printf(TEXT("BANK  %d / %d slots used   (click to take out)"), Sk->BankUsed(), USkillComponent::NumBankSlots), FLinearColor(1.f, 0.85f, 0.3f), BX0, PY + 62.f, Font, 1.1f);
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
		if (StackSize(S.Id) > 1) DrawText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, X + BS - 26.f, Y + BS - 20.f, Font, 0.85f);
		if (bOver)
		{
			ItemTip(S.Id);
			if (bUIClick) Sk->BankWithdraw(Idx);
		}
	}
	const float PgY = GY + Rows * (BS + BG) + 10.f;
	if (Btn(BX0, PgY, 90.f, 32.f, TEXT("< PREV"), BankPage > 0, false, 1.0f)) BankPage = FMath::Max(0, BankPage - 1);
	if (Btn(BX0 + 100.f, PgY, 90.f, 32.f, TEXT("NEXT >"), BankPage < Pages - 1, false, 1.0f)) BankPage = FMath::Min(Pages - 1, BankPage + 1);
	DrawText(FString::Printf(TEXT("Page %d / %d   (mouse wheel also turns pages)"), BankPage + 1, Pages), FLinearColor(0.8f, 0.8f, 0.8f), BX0 + 210.f, PgY + 6.f, Font, 1.0f);
	DrawText(TEXT("Gear you are wearing or holding stays on you. Press E to close."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 34.f, Font, 1.0f);
}
