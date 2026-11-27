#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "KnightPlayerController.h"
#include "Inventory.h"
#include "JeebTestKit.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

using namespace JeebItems;

// ---------------------------------------------------------------------------------------------------
// Test pictures of the menus (not in Shipping). With -JeebShot -ShotUI the HUD opens every screen in turn from 7 s, takes a picture
// of each (Saved/Shots/ui_<screen>.png) and quits; the menu look (jeeb.UI.Style) is set per picture. With -ShotUIMenu (and no
// -JeebShot, so the start screen shows) it takes the start screen in each look. Log lines: JEEBUI.
// ---------------------------------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING
namespace
{
	void SetStyle(int32 S)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.UI.Style"))) V->Set(S, ECVF_SetByConsole);
	}
	struct FStage { const TCHAR* Name; int32 Style; };
	const FStage Stages[] = {
		{ TEXT("esc_map_s1"), 1 }, { TEXT("esc_map_s2"), 2 }, { TEXT("esc_map_s0"), 0 },
		{ TEXT("esc_quests"), 1 }, { TEXT("esc_settings"), 1 }, { TEXT("esc_controls"), 1 }, { TEXT("esc_dictionary"), 1 },
		{ TEXT("esc_lore"), 1 }, { TEXT("esc_look"), 1 }, { TEXT("esc_settings_s2"), 2 }, { TEXT("travel"), 1 },
		{ TEXT("panel_s1"), 1 }, { TEXT("panel_s2"), 2 }, { TEXT("panel_s0"), 0 },
		{ TEXT("kit_weapons"), 1 }, { TEXT("kit_shields"), 1 }, { TEXT("icons"), 1 }, { TEXT("tip"), 1 },
		{ TEXT("shop"), 1 }, { TEXT("smith"), 1 }, { TEXT("bank"), 1 }, { TEXT("inventory"), 1 }, { TEXT("skills"), 1 },
		{ TEXT("emotes"), 1 }, { TEXT("drop"), 1 }, { TEXT("chat"), 1 },
	};
	// (2026-11-26, -ShotUIEcon: only the economy's screens, Docs/EconomyPlan.md)
	const FStage EconStages[] = { { TEXT("shop"), 1 }, { TEXT("market"), 1 }, { TEXT("marketsell"), 1 }, { TEXT("trade"), 1 }, { TEXT("trade2"), 1 } };
	bool EconOnly() { static const bool b = FParse::Param(FCommandLine::Get(), TEXT("ShotUIEcon")); return b; }
	const FStage* StageList() { return EconOnly() ? EconStages : Stages; }
	const int32 NumStages = EconOnly() ? UE_ARRAY_COUNT(EconStages) : UE_ARRAY_COUNT(Stages);
}
#endif

void AKnightHUD::ShotUISequence(AKnightCharacter* Knight)
{
#if !UE_BUILD_SHIPPING
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("JeebShot")) && FParse::Param(FCommandLine::Get(), TEXT("ShotUI"));
	if (!bOn || !Knight || !GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	constexpr float First = 7.f, Each = 1.6f, Snap = 1.1f;
	if (Now < First) return;
	const int32 Want = FMath::FloorToInt((Now - First) / Each);
	auto CloseAll = [&]()
	{
		if (Knight->IsSettingsOpen()) Knight->ToggleSettings();
		Knight->CloseShop(); Knight->CloseSmith(); Knight->CloseBank();
		if (Knight->IsUIOpen()) { /* the pack or skills window */ }
		bTestKitOpen = false; bEmoteWheel = false; bDropOpen = false; TravelAsk = -1;
		if (bChatOpen) CloseChat();
		Knight->SetPanelCursor(false);
	};
	if (Want != ShotUIStage)
	{
		if (Want >= NumStages)
		{
			if (APlayerController* PC = GetOwningPlayerController()) { UE_LOG(LogTemp, Log, TEXT("JEEBUI done")); PC->ConsoleCommand(TEXT("quit")); }
			return;
		}
		// leave the last screen, open the next
		const FString Prev = ShotUIStage >= 0 ? StageList()[ShotUIStage].Name : TEXT("");
		if (Prev == TEXT("inventory") || Prev == TEXT("skills")) { if (Prev == TEXT("inventory")) Knight->ToggleInventory(); else Knight->ToggleSkills(); }
		ShotUIStage = Want;
		bShotUITaken = false;
		const FString N = StageList()[Want].Name;
		SetStyle(StageList()[Want].Style);
		UE_LOG(LogTemp, Log, TEXT("JEEBUI stage %s style %d"), *N, StageList()[Want].Style);
		if (N.StartsWith(TEXT("esc_")))
		{
			if (!Knight->IsSettingsOpen()) { CloseAll(); Knight->ToggleSettings(); }
			SettingsTab = N.StartsWith(TEXT("esc_map")) ? 0 : N == TEXT("esc_quests") ? 6 : N.StartsWith(TEXT("esc_settings")) ? 1 : N == TEXT("esc_controls") ? 2
				: N == TEXT("esc_dictionary") ? 3 : N == TEXT("esc_lore") ? 4 : 5;
		}
		else if (N == TEXT("travel")) { if (!Knight->IsSettingsOpen()) Knight->ToggleSettings(); TestAskTravel(1); }
		else if (N.StartsWith(TEXT("panel"))) { CloseAll(); Knight->SetPanelCursor(true); }
		else if (N.StartsWith(TEXT("kit_")))
		{
			CloseAll(); Knight->SetPanelCursor(true); bTestKitOpen = true;
			const TArray<JeebTestKit::FPage> Pages = JeebTestKit::Pages();
			for (int32 p = 0; p < Pages.Num(); ++p) if (Pages[p].Name == (N == TEXT("kit_weapons") ? TEXT("Weapons") : TEXT("Shields"))) TestPage = p;
		}
		else if (N == TEXT("icons") || N == TEXT("tip")) CloseAll();
		else if (N == TEXT("shop") || N == TEXT("smith") || N == TEXT("bank")) CloseAll();
		else if (N == TEXT("inventory")) { CloseAll(); Knight->ToggleInventory(); }
		else if (N == TEXT("skills")) { CloseAll(); Knight->ToggleSkills(); }
		else if (N == TEXT("emotes")) { CloseAll(); Knight->SetPanelCursor(true); bEmoteWheel = true; bEmoteCursor = true; }
		else if (N == TEXT("drop")) { CloseAll(); Knight->SetPanelCursor(true); OpenDropBox(0, GearId(K_Staff, 3), 1); }
		else if (N == TEXT("chat"))
		{
			CloseAll();
			AddChatLine(TEXT("Wizard"), TEXT("Has anyone seen the gold staff?"), false, nullptr);
			AddChatLine(TEXT("Troll"), TEXT("Broadsword for sale, Oak Hill plaza"), true, nullptr);
			OpenChat();
		}
	}
	const FString N = StageList()[ShotUIStage].Name;
	// screens drawn by the sequence itself, every frame of their stage
	if (N == TEXT("shop")) DrawShopScreen(Knight);   // (no shopkeeper beside him: the window is drawn here, not opened)
	else if (N == TEXT("smith")) DrawSmithScreen(Knight);
	else if (N == TEXT("bank")) DrawBankScreen(Knight);
	else if (N == TEXT("market") || N == TEXT("marketsell"))
	{
		BankTab = 1; MktMode = N == TEXT("market") ? 0 : 2; MktAskAt = FPlatformTime::Seconds();
		if (MktMode == 2) { MktSellSlot = 0; MktPriceText = TEXT("120"); }
		if (Knight->GetSkills()->Slots.Num() > 2 && Knight->GetSkills()->Slots[0].Id == 0) { Knight->GetSkills()->Slots[0] = FInvSlot(GearId(K_Sword, 2), 1); Knight->GetSkills()->Slots[1] = FInvSlot(MaterialId(EItemType::Trout), 1, 334); }
		Knight->MarketRows.Reset(); Knight->bMarketMine = false; Knight->MarketTotal = 17;
		const int32 Ids[] = { GearId(K_Sword, 1), MaterialId(EItemType::Salmon), ArmourId(K_Chest, AS_Heavy, 2), PotionId(0), GearId(K_Wand, 2), ArrowId(A_Broadhead), MaterialId(EItemType::IronOre) };
		const int32 Prices[] = { 60, 85, 410, 25, 300, 18, 40 };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Ids); ++i) { FMarketRow R; R.Id = FString::FromInt(i); R.ItemId = Ids[i]; R.Count = IsArrow(Ids[i]) ? 10 : Ids[i] == MaterialId(EItemType::IronOre) ? 20 : 1; R.Size = i == 1 ? 371 : 0; R.Price = Prices[i]; R.Seller = i == 3 ? TEXT("Hazel") : TEXT("Sir Jeeb"); R.bMine = i == 4; Knight->MarketRows.Add(R); }
		DrawBankScreen(Knight);
	}
	else if (N == TEXT("trade") || N == TEXT("trade2"))
	{
		FTradeView& V = Knight->TradeView;
		V.bOpen = false; V.Partner = TEXT("Hazel"); V.Stage = N == TEXT("trade") ? 0 : 1; V.bTheyAccepted = true; V.bIAccepted = false;
		USkillComponent* Sk = Knight->GetSkills();
		if (Sk->Slots[0].Id == 0) { Sk->Slots[0] = FInvSlot(GearId(K_Sword, 2), 1); Sk->Slots[1] = FInvSlot(MaterialId(EItemType::Trout), 1, 334); }
		V.Mine = { Sk->Slots[0], Sk->Slots[1] }; V.MineSlots = { 0, 1 }; V.MyIngots = 50;
		V.Theirs = { FInvSlot(GearId(K_Wand, 2), 1), FInvSlot(PotionId(0), 3) }; V.TheirIngots = 0;
		DrawTradeScreen(Knight);
	}
	else if (N == TEXT("icons"))
	{
		UFont* Font = GEngine->GetMediumFont();
		const int32 Kinds[] = { K_Wand, K_Staff, K_Broadsword, K_Bow, K_Sword, K_Axe, K_Club };
		const float S = 60.f, Gap = 8.f, LabelW = 120.f;
		const float W = 40.f + LabelW + NumTiers * (S + Gap) + 130.f, H = 40.f + UE_ARRAY_COUNT(Kinds) * (S + Gap) + 10.f;
		const float X = 270.f, Y = 40.f;
		Panel(X, Y, W, H);
		for (int32 r = 0; r < UE_ARRAY_COUNT(Kinds); ++r)
		{
			const float RY = Y + 24.f + r * (S + Gap);
			HDText(KindName(Kinds[r]), FLinearColor(0.92f, 0.88f, 0.8f), X + 24.f, RY + S * 0.35f, Font, 1.1f);
			for (int32 t = 0; t < NumTiers; ++t)
			{
				const float CX = X + 24.f + LabelW + t * (S + Gap);
				Tile(CX, RY, S, S, FLinearColor(0.6f, 0.72f, 0.88f), 0.2f);
				DrawIcon(GearId(Kinds[r], t), CX + 4.f, RY + 3.f, S - 8.f);
			}
		}
		// the wand and staff big, as the shop and smith show them
		Tile(X + W - 120.f, Y + 24.f, 100.f, 100.f, FLinearColor(0.6f, 0.72f, 0.88f), 0.2f);
		DrawIcon(GearId(K_Wand, 3), X + W - 115.f, Y + 29.f, 90.f);
		Tile(X + W - 120.f, Y + 140.f, 100.f, 100.f, FLinearColor(0.6f, 0.72f, 0.88f), 0.2f);
		DrawIcon(GearId(K_Staff, 4), X + W - 115.f, Y + 145.f, 90.f);
	}
	else if (N == TEXT("tip"))
	{
		const bool bHad = bUIHaveMouse; const float MX = UIMouseX, MY = UIMouseY;
		bUIHaveMouse = true; UIMouseX = 300.f; UIMouseY = 120.f;
		ItemTip(GearId(K_Staff, 3)); DrawTip();
		UIMouseX = 300.f; UIMouseY = 470.f;
		ItemTip(GearId(K_Broadsword, 2)); DrawTip();
		UIMouseX = 860.f; UIMouseY = 120.f;
		ItemTip(GearId(K_Bow, 1)); DrawTip();
		TipLines.Reset();
		DrawLinesAt({ TEXT("Iron Arrows  x7"), TEXT("Fired from a bow. Bow damage with these: 10-12") }, 1100.f, 330.f, 1.f, ArrowId(1));   // (the ground label with its icon)
		DrawLinesAt({ TEXT("Wizard Staff") }, 1100.f, 560.f, 1.f, GearId(K_Staff, 3));
		bUIHaveMouse = bHad; UIMouseX = MX; UIMouseY = MY;
	}
	if (!bShotUITaken && Now - First - ShotUIStage * Each > Snap)
	{
		bShotUITaken = true;
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("ui_%s.png"), *N), true, false);
		UE_LOG(LogTemp, Log, TEXT("JEEBUI shot ui_%s.png (%.0f x %.0f)"), *N, Canvas ? Canvas->ClipX : 0.f, Canvas ? Canvas->ClipY : 0.f);
	}
#endif
}

void AKnightHUD::ShotUIMenu()
{
#if !UE_BUILD_SHIPPING
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("ShotUIMenu"));
	if (!bOn || !GetWorld()) return;
	const float Now = GetWorld()->GetRealTimeSeconds();
	const int32 Styles[] = { 1, 2, 0 };
	constexpr float First = 6.f, Each = 1.6f, Snap = 1.1f;
	if (Now < First) return;
	const int32 Want = FMath::FloorToInt((Now - First) / Each);
	if (Want != ShotUIStage)
	{
		if (Want >= UE_ARRAY_COUNT(Styles)) { if (APlayerController* PC = GetOwningPlayerController()) PC->ConsoleCommand(TEXT("quit")); return; }
		ShotUIStage = Want; bShotUITaken = false;
		SetStyle(Styles[Want]);
	}
	if (!bShotUITaken && Now - First - ShotUIStage * Each > Snap)
	{
		bShotUITaken = true;
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("ui_start_s%d.png"), Styles[ShotUIStage]), true, false);
		UE_LOG(LogTemp, Log, TEXT("JEEBUI shot ui_start_s%d.png"), Styles[ShotUIStage]);
	}
#endif
}
