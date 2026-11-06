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
	// The saves were reset for the six tiers (2026-11-04, owner); ApplyTo refuses anything older, so older conversions are gone.
	// Every load (2026-11-03): Light shoulders were removed (the tunic has its own sleeves); any still in a save become
	// Classic shoulders of the same tier
	auto FixPiece = [](int32& Id)
	{
		if (IsGear(Id) && IsArmorKind(KindOf(Id)) && !StyleHasPiece(KindOf(Id), StyleOf(Id))) Id = ArmourId(KindOf(Id), AS_Classic, VariantOf(Id));
	};
	for (TArray<int32>* A : { &SlotIds, &BankIds, &Hotbar, &Worn }) for (int32& Id : *A) FixPiece(Id);
	if (SaveVersion == 11)
	{
		// Once (owner, 2026-11-05): characters made while Light had no shoulders get a pair on, and the cape comes off into the pack, or the bank if that is full
		if (Worn.IsValidIndex(K_Shoulders) && Worn[K_Shoulders] == 0) Worn[K_Shoulders] = ArmourId(K_Shoulders, AS_Classic, T_Tin);
		if (Worn.IsValidIndex(K_Cape) && Worn[K_Cape] != 0)
		{
			auto Into = [this](TArray<int32>& Ids, TArray<int32>& Counts, int32 Size)
			{
				Ids.SetNum(Size); Counts.SetNum(Size);
				for (int32 i = 0; i < Size; ++i)
					if (Ids[i] == 0) { Ids[i] = Worn[K_Cape]; Counts[i] = 1; return true; }
				return false;
			};
			if (Into(SlotIds, SlotCounts, NumSlots) || Into(BankIds, BankCounts, USkillComponent::NumBankSlots)) Worn[K_Cape] = 0;
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
	if (SaveVersion != CurrentVersion && SaveVersion != 11)   // a newer game's, or from before the six tiers (its item numbers mean other things): never read it
	{
		UE_LOG(LogTemp, Warning, TEXT("Jeebsland save: refused a file of version %d (this game reads %d); the character starts fresh."), SaveVersion, CurrentVersion);
		return false;
	}
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
