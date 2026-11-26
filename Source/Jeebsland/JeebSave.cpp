#include "JeebSave.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Lore.h"
#include "JeebSpells.h"
#include "QuestComponent.h"
#include "FastTravel.h"
#include "Horse.h"
#include "KnightCharacter.h"
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
	if (IsMaterial(Id)) return MaterialType(Id) <= EItemType::Bass;   // (the last material; new ones go after it)
	if (IsArrow(Id) || IsRock(Id) || IsPotionRange(Id) || JeebBroom::IsBroomRange(Id)) return true;   // (brooms: any broom number; any potion number: a shorter Potions.csv never loses saved potions)
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
	{
		const AKnightCharacter* KC = Cast<AKnightCharacter>(Skills->GetOwner());
		S->SavedClass = Skills->PackClass >= 0 ? Skills->PackClass : (KC ? KC->ClassIndex() : 0);
		S->Packs = Skills->ClassPacks;
		S->Packs.SetNum(3);
#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("TestOldSaveFormat"))) { S->SavedClass = -1; S->Packs.Empty(); }   // (test: writes a save as the game made them before separate class packs)
#endif
	}
	S->SpellLevels = Skills->SpellLevels;
	S->SpellEquip = Skills->SpellEquip;
	S->SpellPick = Skills->SpellPick;
	if (const UQuestComponent* Q = Skills->GetOwner() ? Skills->GetOwner()->FindComponentByClass<UQuestComponent>() : nullptr) Q->Capture(S->QuestKeys, S->QuestSteps, S->QuestCounts, S->QuestDone);
	if (const UTravelComponent* Tr = Skills->GetOwner() ? Skills->GetOwner()->FindComponentByClass<UTravelComponent>() : nullptr) Tr->Capture(S->TownsVisited);
	if (const URiderComponent* Rc = Skills->GetOwner() ? Skills->GetOwner()->FindComponentByClass<URiderComponent>() : nullptr) Rc->Capture(S->bHasHorse, S->HorseStable);
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

	// Separate packs per class (2026-11-24): this file's pack arrays belong to SavedClass (an older save: whichever class loads it). If the pawn is
	// another class, the file's set is put away and that class's own set comes out (never played: empty, and it gets its starter kit below).
	const AKnightCharacter* Pawn = Cast<AKnightCharacter>(Skills->GetOwner());
	const int32 Cls = Pawn ? Pawn->ClassIndex() : 0;
	const int32 From = (SavedClass >= 0 && SavedClass <= 2) ? SavedClass : Cls;
	TArray<FJeebClassPack> Others = Packs;
	Others.SetNum(3);
	bool bFreshClass = false;
	if (From != Cls)
	{
		FJeebClassPack& Away = Others[From];
		Away = FJeebClassPack();
		Away.bUsed = true; Away.SlotIds = SlotIds; Away.SlotCounts = SlotCounts; Away.Hotbar = Hotbar; Away.Selected = Selected; Away.Worn = Worn;
		Away.RingB = RingB; Away.PotionId = PotionId; Away.PotionCount = PotionCount;
		const FJeebClassPack Mine = Others[Cls];
		Others[Cls] = FJeebClassPack();
		bFreshClass = !Mine.bUsed;
		SlotIds = Mine.SlotIds; SlotCounts = Mine.SlotCounts; Hotbar = Mine.Hotbar; Selected = Mine.Selected; Worn = Mine.Worn;
		RingB = Mine.RingB; PotionId = Mine.PotionId; PotionCount = Mine.PotionCount;
		UE_LOG(LogTemp, Log, TEXT("JEEBPACK class %d -> %d: %s"), From, Cls, bFreshClass ? TEXT("starter kit (never played)") : TEXT("its own pack comes out"));
	}
	else Others[Cls] = FJeebClassPack();

	// Everything is checked on the way in: unknown ids vanish, counts are held to the stack size
	auto Fill = [](TArray<FInvSlot>& Dest, const TArray<int32>& Ids, const TArray<int32>& Counts)
	{
		for (int32 i = 0; i < Dest.Num(); ++i)
		{
			FInvSlot Sl;
			if (Ids.IsValidIndex(i) && Counts.IsValidIndex(i) && ValidId(JeebItems::ReplaceRetired(Ids[i])) && Ids[i] != 0)
			{
				Sl.Id = JeebItems::ReplaceRetired(Ids[i]);   // (a katana becomes a sword of its tier)
				Sl.Count = FMath::Clamp(Counts[i], 1, StackSize(Sl.Id));
			}
			Dest[i] = Sl;
		}
	};
	Skills->Slots.SetNum(NumSlots);
	Skills->Bank.SetNum(USkillComponent::NumBankSlots);
	Fill(Skills->Slots, SlotIds, SlotCounts);
	Fill(Skills->Bank, BankIds, BankCounts);

	for (int32 i = 0; i < 5; ++i) Skills->Hotbar[i] = (Hotbar.IsValidIndex(i) && ValidId(Hotbar[i])) ? JeebItems::ReplaceRetired(Hotbar[i]) : 0;
	Skills->SetSelected(Selected);
	for (int32 i = 0; i < USkillComponent::NumWornKinds; ++i) Skills->Worn[i] = (Worn.IsValidIndex(i) && ValidId(Worn[i])) ? JeebItems::ReplaceRetired(Worn[i]) : 0;
	Skills->RingB = ValidId(RingB) ? RingB : 0;
	Skills->PotionSlot = FInvSlot();
	if (PotionId != 0 && IsPotionRange(PotionId)) { Skills->PotionSlot.Id = PotionId; Skills->PotionSlot.Count = FMath::Clamp(PotionCount, 1, StackSize(PotionId)); }

	Skills->RestoreSkills(XP, Known, FMath::Max(0, Ingots));
	Skills->LoreFound.Empty();
	for (int32 L : LoreFound) if (L >= 0 && L < JeebLore::Entries().Num()) Skills->LoreFound.AddUnique(L);
	Skills->Explored = Explored.Num() == USkillComponent::FogN * USkillComponent::FogN / 8 ? Explored : TArray<uint8>();
	Skills->LastGambleHour = LastGambleHour;
	if (UQuestComponent* Q = Skills->GetOwner() ? Skills->GetOwner()->FindComponentByClass<UQuestComponent>() : nullptr) Q->Restore(QuestKeys, QuestSteps, QuestCounts, QuestDone);
	if (UTravelComponent* Tr = Skills->GetOwner() ? Skills->GetOwner()->FindComponentByClass<UTravelComponent>() : nullptr) Tr->Restore(TownsVisited, TownsVisited.Num() == 0);
	Skills->SpellLevels = SpellLevels;
	Skills->SpellLevels.SetNum(FMath::Min(SpellLevels.Num(), JeebSpells::Num()));
	for (uint8& Lv : Skills->SpellLevels) Lv = uint8(FMath::Min<int32>(Lv, 20));
	Skills->SpellEquip = SpellEquip;
	Skills->SpellEquip.SetNum(USkillComponent::SpellRows * USkillComponent::SpellSlots);
	for (uint8& E : Skills->SpellEquip) if (E > JeebSpells::Num()) E = 0;
	Skills->SpellPick = uint8(FMath::Clamp(SpellPick, 0, USkillComponent::SpellRows - 1));
	Skills->EnsureStarterSpells();   // TEMPORARY (test setup): every spell learned
	if (URiderComponent* Rc = Skills->GetOwner() ? Skills->GetOwner()->FindComponentByClass<URiderComponent>() : nullptr) Rc->Restore(bHasHorse, HorseStable);
	Skills->ClassPacks = Others;
	Skills->PackClass = Cls;
	if (bFreshClass)
	{
		for (FInvSlot& Sl : Skills->Slots) Sl = FInvSlot();
		Skills->PotionSlot = FInvSlot();
		Skills->ResetToStartingKit();   // (class-aware: the pawn is already this class)
	}
	Skills->StowRetiredGear();
	{
		int64 Sum = 0; int32 Items = 0;
		for (const FInvSlot& Sl : Skills->Slots) if (Sl.Id) { ++Items; Sum += int64(Sl.Id) * Sl.Count; }
		int64 WornSum = 0; for (int32 i = 0; i < USkillComponent::NumWornKinds; ++i) WornSum += Skills->Worn[i];
		UE_LOG(LogTemp, Log, TEXT("JEEBPACK applied for class %d (file's pack was class %d): %d pack items, id sum %lld, worn sum %lld, bar %d/%d"), Cls, From, Items, Sum, WornSum, Skills->Hotbar[0], Skills->Hotbar[1]);
	}   // (2026-11-22: bows off knights' and wizards' bars, into the pack or bank)
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
