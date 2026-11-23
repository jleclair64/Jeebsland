#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "JeebSpells.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

// The wizard's spells on screen (owner, 2026-11-22: "three spells to be able to be equipped at the same time. One is cast with left click, the
// other two ... G4 and G5 ... also 1, 2, and 3 ... three equipped icons in the bottom next to the wand ... swap spells in and out of the magic
// book menu"; "Something similar to Hogwarts game ... a list of all the spells, and also a 3x3 grid where the player can drag spells into, and
// these sets of three will be what's cycled when F is clicked").
// The grid, the active row and what is learned are the server's (USkillComponent::SpellEquip / SpellPick / SpellLevels); the clicks and
// drags here only ask (EInvOp::EquipSpell / ChooseSpell).

namespace
{
	FLinearColor Hot(const FLinearColor& C) { return FMath::Lerp(C, FLinearColor::White, 0.55f); }
	const TCHAR* SlotKeys[3] = { TEXT("LMB / 1"), TEXT("M4 / 2"), TEXT("M5 / 3") };
	int32 DragSpell = -1;   // the spell row being dragged in the book (-1: none)
	int32 DragFrom = -1;    // the grid cell it was lifted from (-1: from the list)
}

void AKnightHUD::DrawSpellCell(AKnightCharacter* Knight, float X, float Y, float S, float Step)
{
	USkillComponent* Sk = Knight ? Knight->GetSkills() : nullptr;
	if (!Sk) return;
	UFont* Font = GEngine->GetMediumFont();
	for (int32 k = 0; k < USkillComponent::SpellSlots; ++k)
	{
		const float CX0 = X + k * Step;
		Panel(CX0 - 3.f, Y - 3.f, S + 6.f, S + 6.f);
		const int32 Row = Sk->SpellAt(k);
		if (Row >= 0)
		{
			const JeebSpells::FSpell& Sp = JeebSpells::Get(Row);
			const bool bCharging = Knight->IsChargingCast() && Sk->CastSlot == k;
			Tile(CX0 + 2.f, Y + 2.f, S - 4.f, S - 4.f, bCharging ? FLinearColor(1.f, 0.85f, 0.4f) : Sp.Color, bCharging ? 0.45f : 0.22f, false);
			const float Cx = CX0 + S * 0.5f, Cy = Y + S * 0.5f;
			Disc(Cx, Cy, 13.f, 13.f, FLinearColor(Sp.Color.R, Sp.Color.G, Sp.Color.B, 0.45f));
			Disc(Cx, Cy, 8.f, 8.f, Sp.Color);
			Disc(Cx, Cy, 4.f, 4.f, Hot(Sp.Color));
			if (Knight->IsPanelCursor() && Over(CX0, Y, S, S))
				SetTip({ Sp.Name, Sp.Blurb, FString::Printf(TEXT("Level %d. Cast with %s; F for the next loadout row"), FMath::Max(1, Sk->SpellLevel(Row)), SlotKeys[k]) });
		}
		else if (Knight->IsPanelCursor() && Over(CX0, Y, S, S)) SetTip({ TEXT("Empty spell slot"), TEXT("Tab, then SPELL BOOK: drag a spell into this row") });
		HDText(FString::FromInt(k + 1), FLinearColor(0.7f, 0.7f, 0.7f), CX0 + 4.f, Y + 1.f, Font, 0.7f);
	}
	// the active loadout row above the middle cell: three marks, the lit one is the row F moved to
	const float MX = X + Step + S * 0.5f - 16.f, MY = Y - 11.f;
	for (int32 r = 0; r < USkillComponent::SpellRows; ++r)
		Disc(MX + r * 16.f, MY, 4.f, 4.f, r == Sk->SpellPick ? FLinearColor(1.f, 0.85f, 0.4f) : FLinearColor(0.45f, 0.45f, 0.5f, 0.8f));
	HDText(TEXT("F"), FLinearColor(0.7f, 0.7f, 0.7f), MX - 16.f, MY - 8.f, Font, 0.7f);
}

bool AKnightHUD::DrawSpellTabs(AKnightCharacter* Knight, float X, float Y, float W)
{
	// (the apparel window above the inventory is 232 tall with a 10 gap: KnightHUDInventory.cpp; the book is taller; the tabs sit just over either)
	const float AH = bSpellBookTab ? 352.f : 232.f, AY = Y - AH - 10.f, TH = 28.f, TY = AY - TH - 4.f;
	SpellTabsTop = TY;
	const float Half = (W - 4.f) * 0.5f;
	AddWindowRect(X, TY, W, TH);
	if (Btn(X, TY, Half, TH, TEXT("APPAREL"), true, !bSpellBookTab, 0.85f)) { bSpellBookTab = false; bUIClick = false; }
	if (Btn(X + Half + 4.f, TY, Half, TH, TEXT("SPELL BOOK"), true, bSpellBookTab, 0.85f)) { bSpellBookTab = true; bUIClick = false; }
	if (!bSpellBookTab) { DragSpell = -1; return false; }
	DrawSpellBook(Knight, X, AY, W, AH);
	return true;
}

void AKnightHUD::DrawSpellBook(AKnightCharacter* Knight, float X, float Y, float W, float H)
{
	USkillComponent* Sk = Knight ? Knight->GetSkills() : nullptr;
	if (!Sk) return;
	UFont* Font = GEngine->GetMediumFont();
	Panel(X, Y, W, H, 1.f, false);
	AddWindowRect(X, Y, W, H);
	const float Pad = 10.f, CS = 44.f, Gap = 5.f;
	auto Orb = [&](int32 Row, float CX0, float CY0, float S, float A)
	{
		const JeebSpells::FSpell& Sp = JeebSpells::Get(Row);
		const FLinearColor HC = Hot(Sp.Color);
		Disc(CX0 + S * 0.5f, CY0 + S * 0.5f, S * 0.3f, S * 0.3f, FLinearColor(Sp.Color.R, Sp.Color.G, Sp.Color.B, 0.5f * A));
		Disc(CX0 + S * 0.5f, CY0 + S * 0.5f, S * 0.17f, S * 0.17f, FLinearColor(HC.R, HC.G, HC.B, A));
	};
	auto SpellTip = [&](int32 r, const FString& How)
	{
		const JeebSpells::FSpell& Sp = JeebSpells::Get(r);
		const int32 Lv = Sk->SpellLevel(r);
		if (Lv <= 0) { SetTip({ Sp.Name, Sp.Blurb, TEXT("Not learned yet") }); return; }
		SetTip({ Sp.Name, Sp.Blurb,
			FString::Printf(TEXT("Damage %d%%, speed %d%%, magic cost %d%% of your wand's or staff's"), FMath::RoundToInt(JeebSpells::DamageMul(r, Lv) * 100.f), FMath::RoundToInt(Sp.SpeedMul * 100.f), FMath::RoundToInt(Sp.CostMul * 100.f)),
			FString::Printf(TEXT("Upgrade level %d of %d (each level +%d%% damage)"), Lv, Sp.MaxLevel, FMath::RoundToInt(Sp.PerLevel * Sp.DamageMul * 100.f)),
			Sk->CanUseSpell(r) ? How : FString::Printf(TEXT("Needs Magic level %d"), Sp.LevelNeeded) });
	};

	// ---- the 3x3 loadout grid: F steps through its rows; in a row, slot 1 left click or 1, slot 2 mouse 4 or 2, slot 3 mouse 5 or 3 ----
	float CY = Y + Pad;
	HDText(TEXT("Loadouts  (F: next row)"), FLinearColor(0.85f, 0.85f, 0.85f), X + Pad, CY, Font, 0.8f);
	CY += 20.f;
	const float LabelW = 34.f, GX = X + Pad + LabelW;
	int32 DropCell = -1;
	for (int32 r = 0; r < USkillComponent::SpellRows; ++r)
	{
		const float RY = CY + r * (CS + Gap);
		const bool bActive = Sk->SpellPick == r;
		if (Btn(X + Pad, RY + 8.f, LabelW - 6.f, CS - 16.f, FString::FromInt(r + 1), true, bActive, 0.8f) && !bActive)
		{
			if (!Knight->HasAuthority()) Sk->SpellPick = uint8(r);   // (shown at once; the server checks it)
			Knight->Inv(EInvOp::ChooseSpell, r);
			bUIClick = false;
		}
		for (int32 k = 0; k < USkillComponent::SpellSlots; ++k)
		{
			const int32 Cell = r * USkillComponent::SpellSlots + k;
			const float CX0 = GX + k * (CS + Gap);
			const bool bOver = Over(CX0, RY, CS, CS);
			Tile(CX0, RY, CS, CS, bActive ? FLinearColor(1.f, 0.78f, 0.35f) : (bOver ? FLinearColor(0.86f, 0.87f, 0.9f) : FLinearColor(0.62f, 0.63f, 0.66f)), bActive ? 0.3f : (bOver ? 0.24f : 0.11f));
			const int32 E = Sk->SpellEquip.IsValidIndex(Cell) ? Sk->SpellEquip[Cell] : 0;
			if (E > 0 && !(DragSpell >= 0 && DragFrom == Cell)) Orb(E - 1, CX0, RY, CS, 1.f);
			if (!bOver) continue;
			DropCell = Cell;
			if (DragSpell >= 0) continue;
			if (E > 0) SpellTip(E - 1, FString::Printf(TEXT("%s. Drag it out to empty the slot, or right-click"), SlotKeys[k]));
			else SetTip({ TEXT("Empty slot"), FString::Printf(TEXT("%s. Drag a spell here from the list below"), SlotKeys[k]) });
			if (bUIClick && E > 0) { DragSpell = E - 1; DragFrom = Cell; bUIClick = false; }
			if (bUIRightClick && E > 0) { Knight->Inv(EInvOp::EquipSpell, Cell, 0); bUIRightClick = false; }
		}
	}
	for (int32 k = 0; k < USkillComponent::SpellSlots; ++k)   // the slots' keys under the grid
	{
		float TW = 0.f, TH = 0.f;
		HDTextSize(SlotKeys[k], TW, TH, Font, 0.6f);
		HDText(SlotKeys[k], FLinearColor(0.7f, 0.7f, 0.72f), GX + k * (CS + Gap) + (CS - TW) * 0.5f, CY + 3 * (CS + Gap), Font, 0.6f);
	}
	CY += 3 * (CS + Gap) + 18.f;

	// ---- every spell: learned ones can be dragged into the grid; the rest are shown dim and locked ----
	HDText(TEXT("Spells"), FLinearColor(0.85f, 0.85f, 0.85f), X + Pad, CY, Font, 0.8f);
	CY += 20.f;
	const int32 PerRow = 5;
	const float LS = (W - 2.f * Pad - (PerRow - 1) * Gap) / PerRow;
	for (int32 r = 0; r < JeebSpells::Num(); ++r)
	{
		const float CX0 = X + Pad + (r % PerRow) * (LS + Gap), CY0 = CY + (r / PerRow) * (LS + Gap);
		if (CY0 + LS > Y + H - 4.f) break;   // (about 15 fit)
		const int32 Lv = Sk->SpellLevel(r);
		const bool bCan = Sk->CanUseSpell(r);
		const bool bOver = Over(CX0, CY0, LS, LS);
		Tile(CX0, CY0, LS, LS, bOver ? FLinearColor(0.86f, 0.87f, 0.9f) : FLinearColor(0.62f, 0.63f, 0.66f), bOver ? 0.24f : 0.11f);
		Orb(r, CX0, CY0, LS, bCan ? 1.f : 0.25f);
		if (Lv > 0) HDText(FString::Printf(TEXT("%d"), Lv), FLinearColor(1.f, 0.88f, 0.5f), CX0 + LS - 12.f, CY0 + LS - 16.f, Font, 0.65f);
		else HDText(TEXT("locked"), FLinearColor(0.6f, 0.6f, 0.6f), CX0 + 3.f, CY0 + LS - 15.f, Font, 0.55f);
		if (bOver && DragSpell < 0)
		{
			SpellTip(r, TEXT("Drag it into a loadout slot above"));
			if (bUIClick && bCan) { DragSpell = r; DragFrom = -1; bUIClick = false; }
		}
	}

	// ---- the drag: the orb follows the cursor; let go on a grid slot to put it there, outside the grid to empty the slot it came from ----
	if (DragSpell >= 0)
	{
		Orb(DragSpell, UIMouseX - 22.f, UIMouseY - 22.f, 44.f, 0.9f);
		if (bUIRelease || !bUIWasDown)
		{
			if (DropCell >= 0 && DropCell != DragFrom)
			{
				Knight->Inv(EInvOp::EquipSpell, DropCell, DragSpell + 1);
				if (DragFrom >= 0) Knight->Inv(EInvOp::EquipSpell, DragFrom, 0);   // (slot to slot: it moves)
			}
			else if (DropCell < 0 && DragFrom >= 0) Knight->Inv(EInvOp::EquipSpell, DragFrom, 0);
			DragSpell = -1; DragFrom = -1;
		}
	}
}
