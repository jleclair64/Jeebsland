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
	NorseTitle(TEXT("PACK"), PX + 30.f, PY + 64.f, 1.2f);
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
	NorseTitle(TEXT("WORN"), EX, PY + 170.f, 1.2f);
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
// Drag and drop: pick an item up from the pack or the weapon bar, drop it on a pack cell or a bar slot. The two trade places
// (or it moves into an empty cell). A click without dragging does what it always did (hold or wear the gear).
// Clicking anywhere else on the screen (not the panel, not the weapon bar) puts the cursor away again.
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::ProcessDrag(AKnightCharacter* Knight)
{
	if (!Knight->IsPanelCursor() || !bUIHaveMouse || Knight->IsBankOpen()) { DragKind = 0; bDragging = false; return; }   // (the bank screen does its own dragging)
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
		if (!bDragging)
		{
			if (DragKind == 1 && IsGear(DragId)) Knight->EquipFromInventory(DragIdx);   // a plain click: hold or wear it
			else if (DragKind == 4) Knight->UnequipWorn(WornKind[DragIdx], WornSecond[DragIdx]);   // a plain click on a worn piece takes it off
			else if (DragKind == 5 && Knight->GetLootTarget()) Knight->Inv(EInvOp::LootTakeOne, DragIdx, 0, 0, Knight->GetLootTarget());   // a click takes it
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
				if (bOnPotion) Knight->Inv(EInvOp::SwapInvPotion, DragIdx);   // onto the potion slot (potions only)
				else if (bOnDef) Knight->Inv(EInvOp::SwapInvWorn, DragIdx, K_Shield, 0);   // onto the shield slot (shields only)
				else if (ToWorn >= 0) Knight->Inv(EInvOp::SwapInvWorn, DragIdx, WornKind[ToWorn], WornSecond[ToWorn] ? 1 : 0);   // onto a worn slot: swap
				else if (ToHot >= 0) Knight->Inv(EInvOp::SwapInvHot, DragIdx, ToHot);
				else if (ToInv >= 0) Knight->Inv(EInvOp::SwapInv, DragIdx, ToInv);
				else if (!OverWindows() && !OverBar())
				{
					// let go out in the world: it drops to the ground (a stack asks how many)
					if (Sk->Slots[DragIdx].Count > 1) OpenDropBox(DragIdx, Sk->Slots[DragIdx].Id, Sk->Slots[DragIdx].Count);
					else Knight->DropFromPack(DragIdx, 1);
				}
			}
			else if (DragKind == 2)
			{
				if (ToInv >= 0) Knight->Inv(EInvOp::SwapInvHot, ToInv, DragIdx);
				else if (ToHot >= 0) Knight->Inv(EInvOp::SwapHot, DragIdx, ToHot);
				else if (!OverWindows() && !OverBar()) Knight->DropFromHotbar(DragIdx);
			}
			else if (DragKind == 4)
			{
				// a worn piece back to the pack: onto an empty cell it comes off (armour cannot be taken off, only swapped); onto a piece that belongs in that slot they swap
				if (ToInv >= 0)
				{
					if (Sk->Slots[ToInv].Id == 0) Knight->UnequipWorn(WornKind[DragIdx], WornSecond[DragIdx]);
					else Knight->Inv(EInvOp::SwapInvWorn, ToInv, WornKind[DragIdx], WornSecond[DragIdx] ? 1 : 0);
				}
			}
			else if (DragKind == 7)
			{
				// the shield back to the pack: onto an empty cell it comes off; onto another shield they swap
				if (ToInv >= 0)
				{
					if (Sk->Slots[ToInv].Id == 0) Knight->UnequipWorn(K_Shield);
					else Knight->Inv(EInvOp::SwapInvWorn, ToInv, K_Shield, 0);
				}
			}
			else if (DragKind == 3 && !OverWindows() && !OverBar()) OpenDropBox(-1, 0, Sk->GetIngots());
			else if (DragKind == 5)
			{
				// from the sack straight onto the weapon bar, the shield slot or an armor slot (the old piece goes to your pack, or
				// into the sack if your pack is full); anywhere else on your inventory or armor window just takes it
				ALootDrop* Sack = Knight->GetLootTarget();
				int32 EqHot = -1, EqKind = -1;
				if (ToHot >= 0) EqHot = ToHot;
				else if (bOnDef) EqKind = K_Shield;
				else if (ToWorn >= 0) EqKind = WornKind[ToWorn] + (WornSecond[ToWorn] ? 100 : 0);
				if (Sack && DragIdx >= 0 && (EqHot >= 0 || EqKind >= 0) && ALootDrop::Fits(DragId, EqHot, EqKind % 100))
				{
					Knight->Inv(EInvOp::LootEquip, DragIdx, EqHot, EqKind, Sack);
					DragKind = 0; bDragging = false; DragIdx = -1;
					return;
				}
				const bool bMine = (RectCount > 0 && M.X >= RectX[0] && M.X <= RectX[0] + RectW[0] && M.Y >= RectY[0] && M.Y <= RectY[0] + RectH[0])
					|| (RectCount > 1 && M.X >= RectX[1] && M.X <= RectX[1] + RectW[1] && M.Y >= RectY[1] && M.Y <= RectY[1] + RectH[1]);
				if (bMine && Sack) Knight->Inv(EInvOp::LootTakeOne, DragIdx, 0, 0, Sack);
			}
			else if (DragKind == 6 && ToInv >= 0) Knight->Inv(EInvOp::SwapInvPotion, ToInv);   // the potion slot back into the pack
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
	NorseTitle(TEXT("Sack"), LX + Pad + 2.f, LY + Pad + 2.f, 1.15f);
	if (Btn(LX + PW - Pad - 96.f, LY + Pad - 2.f, 96.f, 26.f, TEXT("Take all"), true, false, 0.9f)) { Knight->Inv(EInvOp::LootTakeAll, 0, 0, 0, Sack); return; }
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

