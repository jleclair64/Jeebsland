#include "JeebSave.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Lore.h"
#include "Kismet/GameplayStatics.h"

using namespace JeebItems;

const FString UJeebSave::SlotName = TEXT("Jeebsland_0");

bool UJeebSave::Exists()
{
	return UGameplayStatics::DoesSaveGameExist(SlotName, 0);
}

bool UJeebSave::ValidId(int32 Id)
{
	if (Id == 0) return true;
	if (IsMaterial(Id)) return MaterialType(Id) <= EItemType::Bones;
	if (IsArrow(Id) || IsPotion(Id)) return true;
	return IsGear(Id) && KindOf(Id) < K_Count;
}

void UJeebSave::Migrate()
{
	// Version 1 is the first: nothing to convert yet
	SaveVersion = CurrentVersion;
}

bool UJeebSave::Save(const USkillComponent* Skills)
{
	if (!Skills) return false;
	UJeebSave* S = Cast<UJeebSave>(UGameplayStatics::CreateSaveGameObject(UJeebSave::StaticClass()));
	if (!S) return false;

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
	S->LastGambleHour = Skills->LastGambleHour;

	return UGameplayStatics::SaveGameToSlot(S, SlotName, 0);
}

bool UJeebSave::Restore(USkillComponent* Skills)
{
	if (!Skills || !Exists()) return false;
	UJeebSave* S = Cast<UJeebSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!S) return false;
	if (S->SaveVersion > CurrentVersion) return false;   // written by a newer game: leave it alone rather than damage it
	S->Migrate();

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
	Fill(Skills->Slots, S->SlotIds, S->SlotCounts);
	Fill(Skills->Bank, S->BankIds, S->BankCounts);

	for (int32 i = 0; i < 5; ++i) Skills->Hotbar[i] = (S->Hotbar.IsValidIndex(i) && ValidId(S->Hotbar[i])) ? S->Hotbar[i] : 0;
	Skills->SetSelected(S->Selected);
	for (int32 i = 0; i < USkillComponent::NumWornKinds; ++i) Skills->Worn[i] = (S->Worn.IsValidIndex(i) && ValidId(S->Worn[i])) ? S->Worn[i] : 0;
	Skills->RingB = ValidId(S->RingB) ? S->RingB : 0;
	Skills->PotionSlot = FInvSlot();
	if (S->PotionId != 0 && IsPotion(S->PotionId)) { Skills->PotionSlot.Id = S->PotionId; Skills->PotionSlot.Count = FMath::Clamp(S->PotionCount, 1, StackSize(S->PotionId)); }

	Skills->RestoreSkills(S->XP, S->Known, FMath::Max(0, S->Ingots));
	Skills->LoreFound.Empty();
	for (int32 L : S->LoreFound) if (L >= 0 && L < JeebLore::Entries().Num()) Skills->LoreFound.AddUnique(L);
	Skills->LastGambleHour = S->LastGambleHour;
	return true;
}
