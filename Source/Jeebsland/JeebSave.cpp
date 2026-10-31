#include "JeebSave.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Lore.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "GameFramework/Actor.h"

using namespace JeebItems;

const FString UJeebSave::SlotName = TEXT("Jeebsland_0");

bool UJeebSave::Exists()
{
	return UGameplayStatics::DoesSaveGameExist(SlotName, 0);
}

bool UJeebSave::ValidId(int32 Id)
{
	if (Id == 0) return true;
	if (IsMaterial(Id)) return MaterialType(Id) <= EItemType::Emerald;   // (the last material; new ones go after it)
	if (IsArrow(Id) || IsPotionRange(Id)) return true;   // (any potion number: a shorter Potions.csv never loses saved potions)
	return IsGear(Id) && KindOf(Id) < K_Count;
}

void UJeebSave::Migrate()
{
	// A file without its version was written before 2026-10-30's fix. Those written before the tier redo (version 1) still have
	// the small map-fog grid; the full-size world, 21 minutes after the redo, made the grid 9 times bigger, so a full-size grid
	// means version 2. (Every save there was at the time fits: randy and tester_1 from 10-29 are version 1, the single-player
	// save from 10-30 evening is version 2.)
	if (SaveVersion <= 0) SaveVersion = (Explored.Num() == USkillComponent::FogN * USkillComponent::FogN / 8) ? 2 : 1;

	// Version 1 -> 2 (2026-10-30, the tier redo): the old tiers Wood, Bronze, Iron, Steel, Diamond become Tin, Bronze, Iron,
	// Steel, Gold (Diamond is now the wizards' legendary, so old diamond gear becomes the best a knight can still wear). Applies
	// to every weapon, tool, armour piece and shield (whose tier is the last digit) and to arrows, wherever they are kept.
	if (SaveVersion < 2)
	{
		static const int32 NewTier[5] = { T_Tin, T_Bronze, T_Iron, T_Steel, T_Gold };
		auto Fix = [](int32& Id)
		{
			if (Id >= IdArrowBase && Id < IdArrowBase + 5) { Id = IdArrowBase + NewTier[Id - IdArrowBase]; return; }
			if (!IsGear(Id)) return;
			const int32 K = KindOf(Id);
			if (!(IsWeaponKind(K) || IsArmorKind(K) || K == K_Shield)) return;   // capes, rings, amulets and the brush have no tier
			const int32 Old = VariantOf(Id);
			if (Old < 0 || Old > 4) return;
			Id += NewTier[Old] - Old;
		};
		for (int32& Id : SlotIds) Fix(Id);
		for (int32& Id : BankIds) Fix(Id);
		for (int32& Id : Hotbar) Fix(Id);
		for (int32& Id : Worn) Fix(Id);
	}
	// Version 2 -> 3 (2026-10-30): capes stopped being items; the cape now shows the PvP rank. A spare cape in the pack, the
	// bank or on the weapon bar is sold for what the store used to pay (12 ingots, 22 for the old gold cape); the worn cape is
	// replaced by the rank's cape when the save is loaded (USkillComponent::RefreshCape).
	if (SaveVersion < 3)
	{
		auto SellCape = [this](int32& Id)
		{
			if (!IsGear(Id) || KindOf(Id) != K_Cape) return;
			Ingots += (VariantOf(Id) == 4) ? 22 : 12;
			Id = 0;
		};
		for (int32& Id : SlotIds) SellCape(Id);
		for (int32& Id : BankIds) SellCape(Id);
		for (int32& Id : Hotbar) SellCape(Id);
	}
	// Version 3 -> 4 (2026-10-31): the bow joined the starting kit, right of the sword on the weapon bar, with steel and fire
	// arrows: everyone gets the starting arrows. Version 4 -> 7: characters without a bow on the weapon bar (one kept in the
	// pack or bank does not count) get a tin one there; the weapons from that slot on move one step right into the first empty
	// slot, and on a full bar the last one goes to the pack, trading places with a bow already there if any (a full pack with no
	// bow: no bow). (5 and 6 were short-lived test versions of this step.)
	auto AddToPack = [this](int32 Id, int32 Count)
	{
		const int32 Stack = StackSize(Id);
		for (int32 i = 0; i < SlotIds.Num() && Count > 0; ++i)
			if (SlotIds[i] == Id && SlotCounts.IsValidIndex(i) && SlotCounts[i] < Stack) { const int32 N = FMath::Min(Stack - SlotCounts[i], Count); SlotCounts[i] += N; Count -= N; }
		for (int32 i = 0; i < SlotIds.Num() && Count > 0; ++i)
			if (SlotIds[i] == 0 && SlotCounts.IsValidIndex(i)) { const int32 N = FMath::Min(Stack, Count); SlotIds[i] = Id; SlotCounts[i] = N; Count -= N; }
	};
	if (SaveVersion < 4)
	{
		AddToPack(ArrowId(T_Steel), USkillComponent::StartSteelArrows);
		AddToPack(IdFireArrow, USkillComponent::StartFireArrows);
	}
	if (SaveVersion < 7)
	{
		bool bHasBow = false;
		for (int32 Id : Hotbar) bHasBow |= IsGear(Id) && KindOf(Id) == K_Bow;
		if (!bHasBow && Hotbar.Num() == 5)
		{
			int32 Sword = 0;
			for (int32 h = 0; h < 5; ++h) if (IsGear(Hotbar[h]) && KindOf(Hotbar[h]) == K_Sword) { Sword = h; break; }
			const int32 At = FMath::Min(Sword + 1, 4);
			int32 Empty = -1;
			for (int32 h = At; h < 5 && Empty < 0; ++h) if (Hotbar[h] == 0) Empty = h;
			for (int32 h = 0; h < At && Empty < 0; ++h) if (Hotbar[h] == 0) Empty = h;
			int32 BowId = GearId(K_Bow, 0);
			if (Empty < 0)   // a full bar: the last weapon makes room (it trades places with a bow already in the pack, if any)
			{
				int32 PackBow = INDEX_NONE;   // (the lowest tier: one he can surely use)
				for (int32 i = 0; i < SlotIds.Num(); ++i)
					if (IsGear(SlotIds[i]) && KindOf(SlotIds[i]) == K_Bow && (PackBow == INDEX_NONE || VariantOf(SlotIds[i]) < VariantOf(SlotIds[PackBow]))) PackBow = i;
				if (PackBow != INDEX_NONE) { BowId = SlotIds[PackBow]; SlotIds[PackBow] = Hotbar[4]; Empty = 4; }
				else if (SlotIds.Contains(0)) { AddToPack(Hotbar[4], 1); Empty = 4; }
				if (Empty == 4) Hotbar[4] = 0;
			}
			if (Empty >= At) { for (int32 h = Empty; h > At; --h) Hotbar[h] = Hotbar[h - 1]; Hotbar[At] = BowId; }
			else if (Empty >= 0) Hotbar[Empty] = BowId;
		}
	}
	// Version 7 -> 8 (2026-10-31): everyone has at least the starting arrows (steel and fire). A full pack (the 4 step could not
	// fit them) sends its last things that are not arrows to the bank to make room.
	if (SaveVersion < 8)
	{
		auto Count = [this](int32 Id) { int32 N = 0; for (int32 i = 0; i < SlotIds.Num(); ++i) if (SlotIds[i] == Id && SlotCounts.IsValidIndex(i)) N += SlotCounts[i]; return N; };
		auto MakeRoom = [this]()
		{
			if (SlotIds.Contains(0)) return true;
			for (int32 i = SlotIds.Num() - 1; i >= 0; --i)
			{
				if (IsArrow(SlotIds[i]) || !SlotCounts.IsValidIndex(i)) continue;
				int32 B = BankIds.IndexOfByKey(0);
				if (B == INDEX_NONE && BankIds.Num() < USkillComponent::NumBankSlots) { B = BankIds.Add(0); BankCounts.SetNum(BankIds.Num()); }
				if (B == INDEX_NONE || !BankCounts.IsValidIndex(B)) return false;
				BankIds[B] = SlotIds[i]; BankCounts[B] = SlotCounts[i];
				SlotIds[i] = 0; SlotCounts[i] = 0;
				return true;
			}
			return false;
		};
		const int32 Want[2][2] = { { ArrowId(T_Steel), USkillComponent::StartSteelArrows }, { IdFireArrow, USkillComponent::StartFireArrows } };
		for (const auto& W : Want)
		{
			const int32 Have = Count(W[0]);
			if (Have >= W[1] || (Have == 0 && !MakeRoom())) continue;
			AddToPack(W[0], W[1] - Have);
		}
	}
	SaveVersion = CurrentVersion;
}

UJeebSave* UJeebSave::Capture(const USkillComponent* Skills, const AActor* Body)
{
	if (!Skills) return nullptr;
	UJeebSave* S = Cast<UJeebSave>(UGameplayStatics::CreateSaveGameObject(UJeebSave::StaticClass()));
	if (!S) return nullptr;

	S->SaveVersion = CurrentVersion;
	for (int32 i = 0; i < USkillComponent::NumSkills; ++i)
	{
		S->XP.Add(Skills->GetXPTotal(static_cast<ESkillType>(i)));
		S->Known.Add(Skills->Known[i] ? 1 : 0);
	}
	S->Ingots = Skills->GetIngots();
	for (const FInvSlot& Sl : Skills->Slots) { S->SlotIds.Add(Sl.Id); S->SlotCounts.Add(Sl.Count); }
	for (const FInvSlot& Sl : Skills->Bank)  { S->BankIds.Add(Sl.Id);  S->BankCounts.Add(Sl.Count); }
	for (int32 i = 0; i < 5; ++i) S->Hotbar.Add(Skills->Hotbar[i]);
	S->Selected = Skills->Selected;
	for (int32 i = 0; i < USkillComponent::NumWornKinds; ++i) S->Worn.Add(Skills->Worn[i]);
	S->RingB = Skills->RingB;
	S->PotionId = Skills->PotionSlot.Id;
	S->PotionCount = Skills->PotionSlot.Count;
	S->LoreFound = Skills->LoreFound;
	S->Explored = Skills->Explored;
	S->LastGambleHour = Skills->LastGambleHour;
	if (Body)
	{
		S->bHasPos = true;
		S->Pos = Body->GetActorLocation();
		S->Yaw = Body->GetActorRotation().Yaw;
	}
	return S;
}

bool UJeebSave::ApplyTo(USkillComponent* Skills)
{
	if (!Skills) return false;
	if (SaveVersion > CurrentVersion) return false;   // written by a newer game: leave it alone rather than damage it
	Migrate();

	// Everything is checked on the way in: unknown ids vanish, counts are held to the stack size
	auto Fill = [](TArray<FInvSlot>& Dest, const TArray<int32>& Ids, const TArray<int32>& Counts)
	{
		for (int32 i = 0; i < Dest.Num(); ++i)
		{
			FInvSlot Sl;
			if (Ids.IsValidIndex(i) && Counts.IsValidIndex(i) && ValidId(Ids[i]) && Ids[i] != 0)
			{
				Sl.Id = Ids[i];
				Sl.Count = FMath::Clamp(Counts[i], 1, StackSize(Ids[i]));
			}
			Dest[i] = Sl;
		}
	};
	Skills->Slots.SetNum(NumSlots);
	Skills->Bank.SetNum(USkillComponent::NumBankSlots);
	Fill(Skills->Slots, SlotIds, SlotCounts);
	Fill(Skills->Bank, BankIds, BankCounts);

	for (int32 i = 0; i < 5; ++i) Skills->Hotbar[i] = (Hotbar.IsValidIndex(i) && ValidId(Hotbar[i])) ? Hotbar[i] : 0;
	Skills->SetSelected(Selected);
	for (int32 i = 0; i < USkillComponent::NumWornKinds; ++i) Skills->Worn[i] = (Worn.IsValidIndex(i) && ValidId(Worn[i])) ? Worn[i] : 0;
	Skills->RingB = ValidId(RingB) ? RingB : 0;
	Skills->PotionSlot = FInvSlot();
	if (PotionId != 0 && IsPotionRange(PotionId)) { Skills->PotionSlot.Id = PotionId; Skills->PotionSlot.Count = FMath::Clamp(PotionCount, 1, StackSize(PotionId)); }

	Skills->RestoreSkills(XP, Known, FMath::Max(0, Ingots));
	Skills->LoreFound.Empty();
	for (int32 L : LoreFound) if (L >= 0 && L < JeebLore::Entries().Num()) Skills->LoreFound.AddUnique(L);
	Skills->Explored = Explored.Num() == USkillComponent::FogN * USkillComponent::FogN / 8 ? Explored : TArray<uint8>();
	Skills->LastGambleHour = LastGambleHour;
	Skills->RefreshCape();
	return true;
}

bool UJeebSave::Save(const USkillComponent* Skills)
{
	UJeebSave* S = Capture(Skills, nullptr);
	return S && UGameplayStatics::SaveGameToSlot(S, SlotName, 0);
}

bool UJeebSave::Restore(USkillComponent* Skills)
{
	if (!Skills || !Exists()) return false;
	UJeebSave* S = Cast<UJeebSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	return S && S->ApplyTo(Skills);
}

bool UJeebSave::WriteFile(UJeebSave* Save, const FString& Path)
{
	TArray<uint8> Bytes;
	if (!Save || !UGameplayStatics::SaveGameToMemory(Save, Bytes)) return false;
	const FString Tmp = Path + TEXT(".tmp");
	if (!FFileHelper::SaveArrayToFile(Bytes, *Tmp)) return false;
	return IFileManager::Get().Move(*Path, *Tmp, true, true);
}

UJeebSave* UJeebSave::ReadFile(const FString& Path)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent) || Bytes.Num() == 0) return nullptr;
	return Cast<UJeebSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
}
