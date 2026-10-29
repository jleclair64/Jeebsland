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
// The screen
// ===================================================================================================
void AKnightHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine) return;

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
	TipLines.Reset();

	// No knight: the starting screen, or (online) waiting for the server to let us in
	APawn* Pawn = GetOwningPawn();
	if (!Pawn) { DrawNoKnight(); return; }
	HideTyping();
	AKnightCharacter* Knight = Cast<AKnightCharacter>(Pawn);
	USkillComponent* Skills = Pawn->FindComponentByClass<USkillComponent>();
	if (!Skills || !Knight) return;
	InvCellCount = 0; bHotCells = false; RectCount = 0; WornCellCount = 0; LootCellCount = 0; bPotionCell = false; bDefCell = false;
	TipLines.Reset();

	// A dropped thing close by says what it is (the same description you see in the inventory), floating above it
	if (PC && !Knight->IsUIOpen())
	{
		ALootDrop* Best = nullptr;
		float BestD = 300.f;
		for (TActorIterator<ALootDrop> It(GetWorld()); It; ++It)
		{
			if (!It->IsItemLook()) continue;
			const float D = FVector::Dist2D(It->GetActorLocation(), Pawn->GetActorLocation());
			if (D < BestD) { BestD = D; Best = *It; }
		}
		FVector2D SP;
		if (Best && PC->ProjectWorldLocationToScreen(Best->TopOfLook() + FVector(0.f, 0.f, 10.f), SP))
		{
			TArray<FString> Lines;
			if (const int32 Id = Best->ShownId())
			{
				ItemTip(Id);
				Lines = TipLines;
				TipLines.Reset();
				if (Lines.Num() > 0 && Best->ShownCount() > 1) Lines[0] += FString::Printf(TEXT("  x%d"), Best->ShownCount());
			}
			else if (Best->GetIngots() > 0) Lines = { TEXT("Ingots"), FString::Printf(TEXT("%d ingots"), Best->GetIngots()) };
			DrawLinesAt(Lines, SP.X, SP.Y, FMath::Clamp((300.f - BestD) / 80.f, 0.f, 1.f));   // fades in over the last metre
		}
	}

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

	// Name of the town you appear in, fading in and out when the game starts
	// (Blackrock for now; switch to the class's home town when Oak Hill and fast travel are built)
	{
		const float T = GetWorld()->GetTimeSeconds();
		const float Alpha = FMath::Clamp(FMath::Min(T / 1.2f, (7.f - T) / 1.5f), 0.f, 1.f);
		if (Alpha > 0.f)
		{
			NorseText(TEXT("BLACKROCK"), Canvas->ClipY * 0.14f, FMath::Clamp(Canvas->ClipY * 0.049f, 30.f, 62.f), Alpha);
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
			NorseText(TEXT("BLACKROCK"), Canvas->ClipY * 0.15f, FMath::Clamp(Canvas->ClipY * 0.049f, 30.f, 62.f), A);
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
	if (Knight->IsSettingsOpen() && !bSettingsWasOpen) SettingsTab = 0;   // it always opens on the map
	bSettingsWasOpen = Knight->IsSettingsOpen();
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
	NorseTitle(TEXT("SKILLS"), X + 16.f, Y + 8.f, 1.3f);

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
	const AKnightCharacter* MapKnight = Cast<AKnightCharacter>(Pawn);
	auto Marker = [&](const FVector2D& W, float R, const FLinearColor& Col, bool bClamp)
	{
		if (MapKnight && MapKnight->GetSkills() && !MapKnight->GetSkills()->IsExplored(W.X, W.Y)) return;   // hidden under the map fog
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
		if (*It == Pawn || It->IsA<AKnightCharacter>()) continue;   // (other players never show)
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

