#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "KnightPlayerController.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "JeebTestKit.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

using namespace JeebItems;

// ---------------------------------------------------------------------------------------------------
// TEMPORARY test kit (owner, 2026-11-13): "a special temporary testing inventory just for me on the tab screen with every item
// I can equip", and a box to set every skill to one level. Only for testers (JeebTestKit::Allowed, the [Jeebsland.Testing]
// switches); the server checks every request. Pages: weapons, each armour style, shields, and the rest (capes, rings, amulets,
// the helm brush, potions, arrows), every tier across. Click: wear or hold it (what was on goes to the pack). Right-click: into
// the pack. Remove this file, JeebTestKit.h and the Test lines in AKnightPlayerController when testing is done.
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawTestKit(AKnightCharacter* Knight, float X, float Y, float MaxW, float MaxH)
{
	AKnightPlayerController* KPC = Cast<AKnightPlayerController>(GetOwningPlayerController());
	USkillComponent* Sk = Knight->GetSkills();
	if (!KPC || !Sk) return;
	UFont* Font = GEngine->GetMediumFont();
	const TArray<JeebTestKit::FPage> Pages = JeebTestKit::Pages();
	if (Pages.Num() == 0) return;
#if !UE_BUILD_SHIPPING
	static const int32 ShotPage = []() { int32 V = -1; if (FParse::Param(FCommandLine::Get(), TEXT("JeebShot"))) FParse::Value(FCommandLine::Get(), TEXT("ShotTestPage="), V); return V; }();   // (test pictures)
	if (ShotPage >= 0) TestPage = ShotPage;
#endif
	TestPage = FMath::Clamp(TestPage, 0, Pages.Num() - 1);
	const JeebTestKit::FPage& Page = Pages[TestPage];

	// ---- the layout: tabs, a tier header, rows of items (wrapping), the level box. The cell size is set by the longest page, so the window keeps its width ----
	int32 Cols = NumTiers;
	for (const JeebTestKit::FPage& Pg : Pages) for (const JeebTestKit::FRow& R : Pg.Rows) Cols = FMath::Max(Cols, R.Ids.Num());
	Cols = FMath::Min(Cols, 8);
	const float Pad = 10.f, Gap = 4.f, LabelW = 96.f, TitleH = 30.f, TabH = 28.f, TabGap = 5.f, HeadH = 30.f, FootH = 84.f;
	const int32 TabsPerRow = 3, TabRows = (Pages.Num() + TabsPerRow - 1) / TabsPerRow;
	auto LinesOf = [&](const JeebTestKit::FPage& Pg) { int32 L = 0; for (const JeebTestKit::FRow& R : Pg.Rows) L += FMath::Max(1, (R.Ids.Num() + Cols - 1) / Cols); return L; };
	int32 MostLines = 1;
	for (const JeebTestKit::FPage& Pg : Pages) MostLines = FMath::Max(MostLines, LinesOf(Pg));
	const int32 Lines = LinesOf(Page);
	const bool bTierHead = TestPage != Pages.Num() - 1;   // (every page but the last runs tier by tier)
	const float Fixed = Pad + TitleH + TabRows * (TabH + TabGap) + 4.f + HeadH + FootH + Pad;
	const float FitH = (MaxH - Fixed) / MostLines - Gap, FitW = (MaxW - Pad * 2.f - LabelW + Gap) / Cols - Gap;
	const float CS = FMath::Clamp(FMath::Min(FitH, FitW), 26.f, 42.f);   // the cell: smaller on a short screen
	const float W = Pad * 2.f + LabelW + Cols * (CS + Gap) - Gap;
	const float H = Fixed - (bTierHead ? 0.f : HeadH) + Lines * (CS + Gap);

	// (a dark backing under the glass: it can open over the minimap and the Skills window on a short screen)
	FillRR(X, Y, W, H, 26.f, [&](float) { return FLinearColor(0.05f, 0.05f, 0.055f, 0.9f); });
	Panel(X, Y, W, H, 1.f, false);
	AddWindowRect(X, Y, W, H);
	if (CloseBtn(X, Y, W)) { bTestKitOpen = false; bLevelTyping = false; bUIClick = false; return; }
	const bool bLive = Knight->IsPanelCursor() && bUIHaveMouse;
	float CY = Y + Pad;
	WindowTitle(TEXT("TEST KIT"), X + Pad, CY - 4.f, 1.15f);
	{
		const FString Note = TEXT("testing only");
		float NW = 0.f, NH = 0.f;
		HDTextSize(Note, NW, NH, Font, 0.8f);
		HDText(Note, FLinearColor(1.f, 0.74f, 0.3f), X + W - Pad - NW - 34.f, CY + 4.f, Font, 0.8f);
	}
	CY += TitleH;
	// ---- the pages ----
	const float TabW = (W - Pad * 2.f - (TabsPerRow - 1) * TabGap) / TabsPerRow;
	for (int32 p = 0; p < Pages.Num(); ++p)
	{
		const float TX = X + Pad + (p % TabsPerRow) * (TabW + TabGap), TY = CY + (p / TabsPerRow) * (TabH + TabGap);
		if (Btn(TX, TY, TabW, TabH, Pages[p].Name, true, p == TestPage, 0.82f)) { TestPage = p; bUIClick = false; }
	}
	CY += TabRows * (TabH + TabGap) + 4.f;

	// ---- the tiers across the top ----
	const float GridX = X + Pad + LabelW;
	if (bTierHead)
	{
		for (int32 T = 0; T < NumTiers && T < Cols; ++T)
		{
			const FString Tn = TierName(T);
			float TW = 0.f, TH = 0.f;
			HDTextSize(Tn, TW, TH, Font, 0.75f);
			// (two lines, every other tier lower, so long names never run into each other over narrow cells)
			HDText(Tn, FLinearColor(0.f, 0.f, 0.f, 0.6f), GridX + T * (CS + Gap) + (CS - TW) * 0.5f + 1.f, CY + (T % 2) * 13.f + 1.f, Font, 0.75f);
			HDText(Tn, FLinearColor(0.88f, 0.9f, 0.94f), GridX + T * (CS + Gap) + (CS - TW) * 0.5f, CY + (T % 2) * 13.f, Font, 0.75f);
		}
		CY += HeadH;
	}

	// ---- the items ----
	int32 Hit = -1;
	for (const JeebTestKit::FRow& R : Page.Rows)
	{
		const int32 RowLines = FMath::Max(1, (R.Ids.Num() + Cols - 1) / Cols);
		{
			float LW = 0.f, LH = 0.f;
			HDTextSize(R.Label, LW, LH, Font, 0.85f);
			HDText(R.Label, FLinearColor(0.f, 0.f, 0.f, 0.6f), X + Pad + 1.f, CY + (CS - LH) * 0.5f + 1.f, Font, 0.85f);
			HDText(R.Label, FLinearColor(0.95f, 0.93f, 0.88f), X + Pad, CY + (CS - LH) * 0.5f, Font, 0.85f);
		}
		for (int32 i = 0; i < R.Ids.Num(); ++i)
		{
			const int32 Id = R.Ids[i];
			const float Cx = GridX + (i % Cols) * (CS + Gap), Cy = CY + (i / Cols) * (CS + Gap);
			const bool bOver = bLive && Over(Cx, Cy, CS, CS);
			Tile(Cx, Cy, CS, CS, bOver ? FLinearColor(0.86f, 0.87f, 0.9f) : FLinearColor(0.62f, 0.63f, 0.66f), bOver ? 0.26f : 0.13f);
			DrawIcon(Id, Cx + 3.f, Cy + 2.f, CS - 6.f);
			if (IsGear(Id) && !CanEquip(Sk, Id)) FillRR(Cx, Cy, CS, CS, 9.f, [&](float) { return FLinearColor(0.f, 0.f, 0.03f, 0.55f); });   // (too high a level: the tip says why)
			if (bOver)
			{
				Hit = Id;
				Outline(Cx - 1.f, Cy - 1.f, CS + 2.f, CS + 2.f, 10.f, FLinearColor(1.f, 0.8f, 0.3f, 1.f));
				ItemTip(Id);
				FString Why;
				if (IsGear(Id) && !CanEquip(Sk, Id, &Why)) TipLines.Add(Why);
				const int32 N = JeebTestKit::GiveCount(Id);
				TipLines.Add(IsArrow(Id) ? FString::Printf(TEXT("Click: %d into your pack"), N)
					: IsPotion(Id) ? FString::Printf(TEXT("Click: %d into your potion slot. Right-click: into your pack"), N)
					: TEXT("Click: wear or hold it. Right-click: into your pack"));
			}
		}
		CY += RowLines * (CS + Gap);
	}
	if (Hit != 0 && Hit != -1)
	{
		if (bUIClick) { KPC->ServerTestGive(Hit, true); bUIClick = false; }
		else if (bUIRightClick) { KPC->ServerTestGive(Hit, false); bUIRightClick = false; }
	}

	// ---- every skill to one level ----
	CY += 6.f;
	{
		const float BoxW = 56.f, BoxH = 32.f, SetW = 64.f;
		const FString Label = TEXT("Every skill to level");
		float LW = 0.f, LH = 0.f;
		HDTextSize(Label, LW, LH, Font, 0.9f);
		HDText(Label, FLinearColor(0.95f, 0.93f, 0.88f), X + Pad, CY + (BoxH - LH) * 0.5f, Font, 0.9f);
		const float BX = X + Pad + LW + 10.f;
		const bool bOverBox = bLive && Over(BX, CY, BoxW, BoxH);
		Tile(BX, CY, BoxW, BoxH, FLinearColor(0.8f, 0.82f, 0.9f), bLevelTyping ? 0.4f : (bOverBox ? 0.3f : 0.2f));
		if (bLevelTyping) Outline(BX - 1.f, CY - 1.f, BoxW + 2.f, BoxH + 2.f, 10.f, FLinearColor(1.f, 0.8f, 0.3f, 1.f));
		const FString Shown = TestLevelText + ((bLevelTyping && FMath::FloorToInt(GetWorld()->GetTimeSeconds() * 2.f) % 2 == 0) ? TEXT("|") : TEXT(""));
		float TW = 0.f, TH = 0.f;
		HDTextSize(Shown, TW, TH, Font, 1.25f);
		HDText(Shown, FLinearColor::White, BX + (BoxW - TW) * 0.5f, CY + (BoxH - TH) * 0.5f, Font, 1.25f);
		if (bOverBox) SetTip({ TEXT("Set every skill"), TEXT("Click, type a level from 1 to 99 and press Enter (or Set)"), TEXT("Testing only") });
		const bool bSet = Btn(BX + BoxW + 8.f, CY, SetW, BoxH, TEXT("Set"), TestLevelText.Len() > 0, false, 0.95f);
		if (bSet) { SubmitLevel(); bUIClick = false; }
		else if (bUIClick && bOverBox) { bLevelTyping = true; TestLevelText.Reset(); bUIClick = false; }   // (a click on the box: type a new number)
		else if (bUIClick && bLevelTyping) bLevelTyping = false;   // (a click anywhere else leaves the box)
		CY += BoxH + 8.f;
		// clearing the pack, since every click above fills it
		if (Over(X + Pad, CY, 130.f, 28.f)) SetTip({ TEXT("Empty pack"), TEXT("Clears the 24 pack cells (not your ingots, what you wear or your weapon bar)") });
		if (Btn(X + Pad, CY, 130.f, 28.f, TEXT("Empty pack"), true, false, 0.85f)) { KPC->ServerTestEmptyPack(); bUIClick = false; }
	}
}

// The level box: the number typed, 1 to 99, to the server (which checks this player may)
void AKnightHUD::SubmitLevel()
{
	bLevelTyping = false;
	const int32 Level = FCString::Atoi(*TestLevelText);
	if (Level < 1) { TestLevelText = TEXT("1"); return; }
	TestLevelText = FString::FromInt(FMath::Min(Level, 99));
	if (AKnightPlayerController* KPC = Cast<AKnightPlayerController>(GetOwningPlayerController())) KPC->ServerTestLevels(FMath::Min(Level, 99));
}
