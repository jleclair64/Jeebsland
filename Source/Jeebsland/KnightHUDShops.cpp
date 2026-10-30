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
#include "KnightGameState.h"

using namespace JeebItems;

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
	NorseTitle(TEXT("THE TRADING POST"), PX + 30.f, PY + 14.f, 1.8f);
	{
		const FString Bal = FString::Printf(TEXT("%d"), Skills->GetIngots());
		float TW = 0.f, TH = 0.f;
		HDTextSize(Bal, TW, TH, Font, 1.8f);
		HDText(Bal, FLinearColor(1.f, 0.85f, 0.3f), PX + PW - 100.f - TW, PY + 14.f, Font, 1.8f);
		DrawIngot(PX + PW - 92.f, PY + 10.f, 40.f);
	}
	if (CloseBtn(PX, PY, PW)) { Knight->CloseShop(); return; }

	// ----- Sell (left) -----
	NorseTitle(TEXT("SELL"), PX + 30.f, PY + 62.f, 1.3f);
		static const EItemType Sellable[18] = { EItemType::Logs, EItemType::PineLogs, EItemType::RedwoodLogs, EItemType::TinOre, EItemType::CopperOre, EItemType::IronOre, EItemType::Coal,
			EItemType::SilverOre, EItemType::GoldOre, EItemType::Onyx, EItemType::Emerald, EItemType::Diamond, EItemType::Bacon, EItemType::Perch, EItemType::Trout, EItemType::GoldenCarp, EItemType::Hide, EItemType::Bones };
	// (only what you carry, up to ten rows: there are more kinds of material than fit since the new ores)
	int32 Row = 0;
	for (int32 m = 0; m < UE_ARRAY_COUNT(Sellable) && Row < 10; ++m)
	{
		const EItemType T = Sellable[m];
		const int32 N = Skills->GetItemCount(T);
		if (N <= 0) continue;
		const float RY = PY + 94.f + Row++ * 38.f;
		Tile(PX + 30.f, RY, 510.f, 34.f, FLinearColor(0.6f, 0.72f, 0.88f), 0.16f, false);
		DrawIcon(MaterialId(T), PX + 34.f, RY + 1.f, 32.f);
		HDText(MaterialName(T), FLinearColor::White, PX + 74.f, RY + 8.f, Font, 1.0f);
		HDText(FString::Printf(TEXT("x %d"), N), FLinearColor(0.85f, 0.85f, 0.85f), PX + 210.f, RY + 8.f, Font, 1.05f);
		DrawIngot(PX + 280.f, RY + 4.f, 24.f);
		HDText(FString::Printf(TEXT("%d each"), SellValue(T)), FLinearColor(1.f, 0.85f, 0.3f), PX + 310.f, RY + 8.f, Font, 1.0f);
		if (Btn(PX + 430.f, RY + 2.f, 104.f, 30.f, TEXT("SELL ALL"), N > 0, false, 1.0f)) Knight->Inv(EInvOp::SellAll, static_cast<int32>(T));
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
			if (bUIClick) Knight->Inv(EInvOp::SellSlot, i, S.Id);
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

		AKnightGameState::ApplyStore(GetWorld());   // online: what is left is the server's count
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
			else if (IsPotion(W.Id)) { Stat = *PotionRow(PotionType(W.Id)).Shop; }
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
					else Knight->Inv(EInvOp::Buy, i, W.Id);   // the server says how it went
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
			if (Btn(RX + RW - 128.f, BY + 20.f, 116.f, 36.f, TEXT("OPEN"), !bUsed && bAfford, false, 1.1f)) Knight->Inv(EInvOp::Gamble);   // the server rolls it
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
	const float PW = 1500.f, PH = 690.f;
	const float PX = (Canvas->ClipX - PW) * 0.5f, PY = (Canvas->ClipY - PH) * 0.5f;
	const float Now = GetWorld()->GetTimeSeconds();
	Panel(PX, PY, PW, PH);
	NorseTitle(TEXT("THE SMITHY"), PX + 30.f, PY + 14.f, 1.8f);
	if (CloseBtn(PX, PY, PW)) { Knight->CloseSmith(); return; }

	// Materials you carry (every ore and gem a knight smiths with), and your smithing level
	{
		static const EItemType Mats[8] = { EItemType::TinOre, EItemType::CopperOre, EItemType::IronOre, EItemType::Coal, EItemType::SilverOre, EItemType::GoldOre, EItemType::Onyx, EItemType::Diamond };
		for (int32 m = 0; m < 8; ++m)
		{
			const float X = PX + 30.f + m * 128.f;
			DrawIcon(MaterialId(Mats[m]), X, PY + 58.f, 32.f);
			HDText(FString::Printf(TEXT("%d"), Skills->GetItemCount(Mats[m])), FLinearColor::White, X + 38.f, PY + 64.f, Font, 1.1f);
			if (Over(X, PY + 58.f, 110.f, 34.f)) TipLines = { MaterialName(Mats[m]) };
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

	const float CW = 150.f, CH = 500.f, CGap = 8.f;
	const float CX0 = PX + 370.f, CY = PY + 112.f;
	for (int32 t = 0; t < NumTiersKnight; ++t)   // Tin to Onyx (Emerald and Diamond are for trolls and wizards)
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
		for (int32 i = 0; i < 2; ++i)
		{
			if (R.Num[i] <= 0) continue;
			const int32 Have = Skills->GetItemCount(R.Mat[i]);
			const bool bOk = Have >= R.Num[i];
			if (!bOk) bHaveAll = false;
			DrawIcon(MaterialId(R.Mat[i]), CX + 6.f, Y - 3.f, 24.f);
			HDText(FString::Printf(TEXT("%s %d/%d"), MaterialName(R.Mat[i]), Have, R.Num[i]), bOk ? FLinearColor(0.7f, 1.f, 0.7f) : FLinearColor(1.f, 0.45f, 0.4f), CX + 34.f, Y, Font, 0.85f);
			Y += 26.f;
		}

		const int32 Lv = Skills->GetLevel(ESkillType::Smithing);
		const bool bLevelOk = Lv >= R.Level;
		Y += 6.f;
		HDText(FString::Printf(TEXT("Needs Smithing %d"), R.Level), bLevelOk ? FLinearColor(0.7f, 1.f, 0.7f) : FLinearColor(1.f, 0.45f, 0.4f), CX + 8.f, Y, Font, 0.95f); Y += 24.f;
		HDText(FString::Printf(TEXT("+%d XP"), R.XP), FLinearColor(0.5f, 0.85f, 1.f), CX + 8.f, Y, Font, 0.95f);

		const bool bCan = bHaveAll && bLevelOk;
		if (Btn(CX + 10.f, CY + CH - 50.f, CW - 20.f, 38.f, TEXT("SMITH"), bCan, false, 1.1f))
		{
			const int32 Count = bArrows ? ArrowBundle : 1;
			if (!Skills->CanAdd(Id, Count)) { ShopMsg = TEXT("Your pack is full."); ShopMsgUntil = Now + 3.f; }
			else Knight->Inv(EInvOp::Smith, bArrows ? -1 : Kind, t);   // the server checks the recipe and makes it
		}
		if (Over(CX, CY, CW, CH - 56.f) && !bArrows) ItemTip(Id);
	}
	if (Now < ShopMsgUntil) HDText(ShopMsg, FLinearColor(0.6f, 1.f, 0.6f), PX + 370.f, PY + PH - 64.f, Font, 1.1f);
	HDText(TEXT("Tin: tin ore. Iron: iron ore. Bronze: copper and tin. Steel: iron ore and coal. Silver and gold: their ores. Onyx: onyx and gold.   E or X to leave."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 30.f, Font, 0.95f);
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
	NorseTitle(TEXT("THE VAULT"), PX + 30.f, PY + 14.f, 1.8f);
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
			if (bUIClick) Knight->Inv(EInvOp::BankDeposit, i);
		}
	}
	if (Btn(GX, GY + 4 * (SS + Gap) + 8.f, 4 * (SS + Gap) - Gap, 34.f, TEXT("DEPOSIT EVERYTHING"), true, false, 1.0f))
	{
		Knight->Inv(EInvOp::BankDeposit, -1);
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
			if (bUIClick) Knight->Inv(EInvOp::BankWithdraw, Idx);
		}
	}
	const float PgY = GY + Rows * (BS + BG) + 10.f;
	if (Btn(BX0, PgY, 90.f, 32.f, TEXT("< PREV"), BankPage > 0, false, 1.0f)) BankPage = FMath::Max(0, BankPage - 1);
	if (Btn(BX0 + 100.f, PgY, 90.f, 32.f, TEXT("NEXT >"), BankPage < Pages - 1, false, 1.0f)) BankPage = FMath::Min(Pages - 1, BankPage + 1);
	HDText(FString::Printf(TEXT("Page %d / %d   (mouse wheel also turns pages)"), BankPage + 1, Pages), FLinearColor(0.8f, 0.8f, 0.8f), BX0 + 210.f, PgY + 6.f, Font, 1.0f);
	// Weapons can be dragged from the weapon bar (at the bottom of the screen) onto the bank
	if (bHotCells && bUIClick && BankDragHot < 0)
		for (int32 h = 0; h < 5; ++h)
			if (Sk->Hotbar[h] != 0 && Over(HotCellX[h], HotCellY[h], HotCellS, HotCellS)) BankDragHot = h;
	if (BankDragHot >= 0)
	{
		if (Sk->Hotbar[BankDragHot] != 0) DrawIcon(Sk->Hotbar[BankDragHot], UIMouseX - 28.f, UIMouseY - 28.f, 56.f);
		if (bUIRelease || !bUIWasDown)
		{
			if (Over(PX, PY, PW, PH)) Knight->Inv(EInvOp::BankDepositHot, BankDragHot);
			BankDragHot = -1;
		}
	}
	HDText(TEXT("Worn gear stays on you. Drag weapons from your weapon bar onto the bank to store them. Press E to close."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 34.f, Font, 1.0f);
}


