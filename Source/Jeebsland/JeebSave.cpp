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
