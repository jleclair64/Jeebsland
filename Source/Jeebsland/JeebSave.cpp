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
	if (IsMaterial(Id)) return MaterialType(Id) <= EItemType::Bones;
	if (IsArrow(Id) || IsPotion(Id)) return true;
	return IsGear(Id) && KindOf(Id) < K_Count;
}

void UJeebSave::Migrate()
{
	// Version 1 is the first: nothing to convert yet
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
	if (PotionId != 0 && IsPotion(PotionId)) { Skills->PotionSlot.Id = PotionId; Skills->PotionSlot.Count = FMath::Clamp(PotionCount, 1, StackSize(PotionId)); }

	Skills->RestoreSkills(XP, Known, FMath::Max(0, Ingots));
	Skills->LoreFound.Empty();
	for (int32 L : LoreFound) if (L >= 0 && L < JeebLore::Entries().Num()) Skills->LoreFound.AddUnique(L);
	Skills->LastGambleHour = LastGambleHour;
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
