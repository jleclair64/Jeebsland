#include "KnightHUD.h"
#include "SkillComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "KnightCharacter.h"
#include "JeebGameInstance.h"
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
// The Escape screen: the world map (with Save and Back to menu), settings, controls, the dictionary and the lore. Escape
// (or O) opens and closes it; it always opens on the map.
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
	const TCHAR* TabNames[5] = { TEXT("MAP"), TEXT("SETTINGS"), TEXT("CONTROLS"), TEXT("DICTIONARY"), TEXT("LORE") };
	for (int32 t = 0; t < 5; ++t)
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
		// The map on the left; Save and Back to menu on the right
		const float MS = PH - 80.f;
		DrawWorldMap(Knight, PX + 30.f, PY + 58.f, MS);
		const float BX = PX + 30.f + MS + 30.f, BW = PX + PW - 30.f - BX;
		const float Now = GetWorld()->GetTimeSeconds();
		if (Btn(BX, PY + 70.f, BW, 54.f, Now < SavedNoteUntil ? TEXT("Saved") : TEXT("SAVE"), true, Now < SavedNoteUntil, 1.3f))
		{
			Knight->SaveNow();
			SavedNoteUntil = Now + 2.f;
		}
		Wrapped(TEXT("Your character is also saved every minute, and whenever you leave."), BX + 4.f, PY + 134.f, BW - 8.f, 0.9f, FLinearColor(0.7f, 0.72f, 0.78f));
		const bool bOnline = GetNetMode() == NM_Client;
		if (Btn(BX, PY + 210.f, BW, 54.f, TEXT("BACK TO MENU"), bOnline, false, 1.3f))
		{
			if (UJeebGameInstance* GI = GetGameInstance<UJeebGameInstance>()) GI->ReturnToMenu(FString());   // (the server saves you as you leave)
			return;
		}
		if (!bOnline) Wrapped(TEXT("(Offline play has no starting screen.)"), BX + 4.f, PY + 274.f, BW - 8.f, 0.9f, FLinearColor(0.6f, 0.6f, 0.65f));
		HDText(TEXT("Press Esc to go back to the game."), FLinearColor(0.8f, 0.8f, 0.8f), BX + 4.f, PY + PH - 50.f, Font, 1.0f);
	}
	else if (SettingsTab == 1)
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
				if (DraggingSlider == 1) Knight->SetRenderScale(FMath::Lerp(0.35f, 1.0f, T));

				// Checkbox: toggles once per click
				if (bClicked && MX >= BoxX - 4.f && MX <= BoxX + 560.f && MY >= BoxY + 86.f && MY <= BoxY + 122.f)
					Knight->SetPanelAlways(!Knight->GetPanelAlways());
				if (bClicked && MX >= BoxX - 4.f && MX <= BoxX + 560.f && MY >= BoxY + 132.f && MY <= BoxY + 168.f)
				{
					bShowFps = !bShowFps;
					if (GConfig) GConfig->SetBool(TEXT("Jeebsland"), TEXT("ShowFps"), bShowFps, GGameUserSettingsIni);
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
		Slider(ScaleY, FMath::Clamp((RS - 0.35f) / 0.65f, 0.f, 1.f), FLinearColor(0.3f, 0.7f, 0.9f));

		// Graphics: two levels, one click each
		static const TCHAR* LevelNames[2] = { TEXT("LOW"), TEXT("HIGH") };
		static const TCHAR* LevelText[2] = {
			TEXT("For weaker computers: no clouds, simple shadows and edges"),
			TEXT("Clouds and soft shadows") };
		HDText(TEXT("Graphics:"), FLinearColor::White, BoxX, BoxY + 4.f, Font, 1.1f);
		for (int32 L = 0; L < 2; ++L)
		{
			if (Btn(BoxX + 130.f + L * 140.f, BoxY, 130.f, 32.f, LevelNames[L], true, Knight->GetGraphicsLevel() == L, 1.05f))
				Knight->SetGraphicsLevel(L);
		}
		HDText(LevelText[FMath::Clamp(Knight->GetGraphicsLevel(), 0, 1)], FLinearColor(0.75f, 0.75f, 0.75f), BoxX + 130.f, BoxY + 42.f, Font, 1.0f);
		// Side panel: always on screen, or only while the cursor is out (Tab)
		DrawRect(FLinearColor::White, BoxX, BoxY + 90.f, 26.f, 26.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 1.f), BoxX + 2.f, BoxY + 92.f, 22.f, 22.f);
		if (Knight->GetPanelAlways()) DrawRect(FLinearColor(0.9f, 0.55f, 0.15f), BoxX + 5.f, BoxY + 95.f, 16.f, 16.f);
		HDText(TEXT("Show the inventory panel all the time (off: only while Tab is held open)"), FLinearColor::White, BoxX + 38.f, BoxY + 92.f, Font, 1.1f);
		// Frame rate counter in the top right corner
		DrawRect(FLinearColor::White, BoxX, BoxY + 136.f, 26.f, 26.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 1.f), BoxX + 2.f, BoxY + 138.f, 22.f, 22.f);
		if (bShowFps) DrawRect(FLinearColor(0.9f, 0.55f, 0.15f), BoxX + 5.f, BoxY + 141.f, 16.f, 16.f);
		HDText(TEXT("Show the frame rate"), FLinearColor::White, BoxX + 38.f, BoxY + 138.f, Font, 1.1f);

	}
	else if (SettingsTab == 2)
	{
		struct FRow { const TCHAR* Key; const TCHAR* Action; };
		static const FRow Rows[] = {
			{ TEXT("W A S D"),        TEXT("Move") },
			{ TEXT("Mouse"),          TEXT("Look around") },
			{ TEXT("Space"),          TEXT("Jump (also: stand up, let go of a ladder)") },
			{ TEXT("Shift (hold)"),   TEXT("Sprint (uses stamina; better boots give more)") },
			{ TEXT("Ctrl (hold)"),    TEXT("Crouch") },
			{ TEXT("1 - 5 / R"),      TEXT("Take that weapon bar slot in hand (or click it with Tab); R steps to the next, left to right") },
			{ TEXT("Left click"),     TEXT("Chop / mine what is in front of you (the right tool is picked for you), or attack") },
			{ TEXT("Right click (hold)"), TEXT("Raise your shield and duck (needs a shield; not with two-handed weapons)") },
			{ TEXT("C while sprinting"), TEXT("Slide") },
			{ TEXT("V or Alt"),       TEXT("Dodge roll") },
			{ TEXT("Wheel click or X"), TEXT("Strafe mode on/off (on from the start): keep facing where you look, step sideways and back") },
			{ TEXT("Tab"),            TEXT("Inventory: click gear to wear or hold it, click worn gear to take it off") },
			{ TEXT("K"),              TEXT("Skills: your levels, shown as icons") },
			{ TEXT("E at a door"),    TEXT("Open or close it. Locked huts need Lockpicking; shutting one locks it again") },
			{ TEXT("Click / hold"),   TEXT("Sword: click to swing (click on to chain), hold to charge a smash attack. Spear: hold, let go to thrust") },
			{ TEXT("Wheel up"),       TEXT("Kick: goes through a raised shield and knocks him down") },
			{ TEXT("Knocked down"),   TEXT("A or D rolls away that way; W, S, Space or a click gets straight up") },
			{ TEXT("E"),              TEXT("Talk to the shopkeeper, blacksmith and others; sit on benches") },
			{ TEXT("W / S at a ladder"), TEXT("Climb up / down (walk into it)") },
			{ TEXT("Esc (or O)"),     TEXT("The world map, saving, settings, controls, dictionary and the lore you have collected") },
		};
		float RowY = PY + 70.f;
		for (const FRow& R : Rows)
		{
			HDText(R.Key, FLinearColor(1.f, 0.85f, 0.3f), PX + 30.f, RowY, Font, 1.1f);
			HDText(R.Action, FLinearColor::White, PX + 250.f, RowY, Font, 1.05f);
			RowY += 27.f;   // (20 rows)
		}
		HDText(TEXT("Press Esc to close."), FLinearColor(0.8f, 0.8f, 0.8f), PX + 30.f, PY + PH - 40.f, Font, 1.0f);
	}
	else if (SettingsTab == 3)
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
		Y += Wrapped(TEXT("Every weapon, shield and piece of armour comes in ranks, weakest to strongest: tin, iron, bronze, steel, silver and gold, then one legendary rank for each class (onyx for knights, emerald for trolls, diamond for wizards). Each rank is a different colour, a little stronger than the last, and needs a higher level to use."), CX, Y, CW, 1.05f, FLinearColor::White) + 10.f;
		for (int32 t = 0; t < NumTiers; ++t)
		{
			DrawRect(Palette(t, 0), CX, Y + 4.f, 14.f, 14.f);
			Y += Wrapped(TierBlurb(t), CX + 24.f, Y, CW - 24.f, 1.0f, FLinearColor(0.9f, 0.9f, 0.9f)) + 4.f;
		}
		Y += 8.f;
		Y += Wrapped(TEXT("HOW IT WORKS: mine ore (each rock always holds one kind: tin, copper, iron, coal, silver, gold, or rarely a gem), then take it to the blacksmith. Tin uses tin ore, iron uses iron ore, bronze uses copper and tin, steel uses iron ore and coal, silver and gold use their ores, and onyx uses onyx and gold. Smithing earns Smithing XP, and higher ranks need a higher Smithing level. The store sells tin, iron and bronze gear and arrows; everything better has to be smithed.  LEVELS TO USE: weapons need Attack (bows Archery, wands and staffs Magic), axes Woodcutting, pickaxes Mining, fishing poles Fishing, and armour and shields Defence: tin 1, iron 5, bronze 10, steel 20, silver 30, gold 40, legendary 50."), CX, Y, CW, 1.0f, FLinearColor(0.85f, 0.95f, 0.85f)) + 8.f;
		Y += Wrapped(TEXT("GATHERING LEVELS: coal needs Mining 6, iron ore 8, silver ore 20, gold ore 30 and the gems (onyx, emerald, diamond) 45. Pine needs Woodcutting 6 and redwood Woodcutting 15. Trout needs Fishing 5 and the golden carp Fishing 12. Better pickaxes, axes and fishing poles work faster."), CX, Y, CW, 1.0f, FLinearColor(0.9f, 0.85f, 0.7f)) + 8.f;
		Wrapped(TEXT("DAMAGE: every hit rolls a number between a weapon's lowest and highest damage. Rings and amulets move or reshape that range."), CX, Y, CW, 1.0f, FLinearColor(0.9f, 0.8f, 0.8f));
		return;
	}

	if (E.Kind == -2)
	{
		Y += Wrapped(TEXT("Arrows are limited: every shot uses one, and the bow uses the best arrows you carry. Better arrows hit harder. Buy bundles of ten at the store (tin, iron and bronze) or smith any rank up to onyx."), CX, Y, CW, 1.05f, FLinearColor::White) + 12.f;
		const TCHAR* Heads[] = { TEXT("RANK"), TEXT("BOW DAMAGE"), TEXT("SMITH LV"), TEXT("MATERIALS (10)"), TEXT("STORE (10)") };
		const float Xs[] = { 0.f, 110.f, 250.f, 360.f, 580.f };
		for (int32 c = 0; c < 5; ++c) HDText(Heads[c], FLinearColor(0.7f, 0.7f, 0.7f), CX + Xs[c], Y, Font, 0.95f);
		Y += 28.f;
		for (int32 t = 0; t < NumTiersKnight; ++t)
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
		// the PvP ranks a knight can reach, lowest first (the cape colours come from Tiers.csv)
		for (int32 c = 0; c < NumTiersKnight; ++c)
		{
			DrawIcon(GearId(K_Cape, c), CX, Y - 4.f, 30.f);
			HDText(FString::Printf(TEXT("%s rank"), TierName(c)), FLinearColor::White, CX + 40.f, Y, Font, 1.0f);
			if (c == NumTiersKnight - 1) HDText(TEXT("the knights' highest rank"), FLinearColor(1.f, 0.85f, 0.3f), CX + 200.f, Y, Font, 1.0f);
			Y += 34.f;
		}
		return;
	}
	if (IsAccessoryKind(K))
	{
		const int32 N = (K == K_Ring) ? NumRings() : NumAmulets();
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
	const TCHAR* Heads[] = { TEXT("RANK"), bWeapon ? TEXT("DAMAGE") : TEXT("DEFENCE"), TEXT("NEEDS"), TEXT("SMITH LV"), TEXT("MATERIALS"), TEXT("STORE") };
	const float Xs[] = { 44.f, 140.f, 225.f, 330.f, 420.f, 660.f };
	for (int32 c = 0; c < 6; ++c) HDText(Heads[c], FLinearColor(0.7f, 0.7f, 0.7f), CX + Xs[c], Y, Font, 0.95f);
	Y += 28.f;
	for (int32 t = 0; t < NumTiersKnight; ++t)   // (tin to onyx: the ranks a knight can use)
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
		HDText(FString::Printf(TEXT("%s %d"), SkillNameOf(EquipSkill(K)), EquipLevel(t)), FLinearColor(0.85f, 0.85f, 1.f), CX + Xs[2], Y, Font, 0.9f);
		HDText(FString::Printf(TEXT("%d"), R.Level), FLinearColor::White, CX + Xs[3], Y, Font, 1.0f);
		HDText(RecipeText(R), FLinearColor(0.9f, 0.9f, 0.8f), CX + Xs[4], Y, Font, 0.9f);
		HDText(t <= StoreMaxTier ? FString::Printf(TEXT("%d ingots"), BuyPrice(K, t)) : FString(TEXT("smith only")), FLinearColor(1.f, 0.85f, 0.3f), CX + Xs[5], Y, Font, 1.0f);
		if (K == K_Boots) HDText(FString::Printf(TEXT("stamina x%.2f"), BootStaminaMult(t)), FLinearColor(0.6f, 0.9f, 0.6f), CX + Xs[1], Y + 18.f, Font, 0.85f);
		Y += (K == K_Boots) ? 42.f : 38.f;
	}
	if (K == K_Pickaxe || K == K_Axe || K == K_Rod)
		Wrapped(TEXT("Better tools gather faster: up to 40% less time with an onyx tool than a tin one."), CX, Y + 6.f, CW, 0.95f, FLinearColor(0.7f, 0.9f, 0.7f));
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
// Lore tab (in the Escape screen): every fragment you have heard, sorted by kind
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



