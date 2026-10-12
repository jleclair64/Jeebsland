#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "JeebSave.generated.h"

class USkillComponent;

// What is written to disk (Saved/SaveGames/Jeebsland_0.sav). See Docs/SaveLoadPlan.md.
// RULES: never reorder JeebItems::EKind / EItemType (item ids are stored as numbers); when the layout of this class changes in a
// way old saves cannot be read, bump CurrentVersion and add a step to UJeebSave::Migrate.
UCLASS()
class UJeebSave : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 1;
	static const FString SlotName;

	UPROPERTY() int32 SaveVersion = CurrentVersion;

	// The player (everything USkillComponent holds)
	UPROPERTY() TArray<int32> XP;
	UPROPERTY() TArray<uint8> Known;
	UPROPERTY() int32 Ingots = 0;
	UPROPERTY() TArray<int32> SlotIds;
	UPROPERTY() TArray<int32> SlotCounts;
	UPROPERTY() TArray<int32> BankIds;
	UPROPERTY() TArray<int32> BankCounts;
	UPROPERTY() TArray<int32> Hotbar;
	UPROPERTY() int32 Selected = 0;
	UPROPERTY() TArray<int32> Worn;
	UPROPERTY() int32 RingB = 0;
	UPROPERTY() int32 PotionId = 0;
	UPROPERTY() int32 PotionCount = 0;
	UPROPERTY() TArray<int32> LoreFound;
	UPROPERTY() int64 LastGambleHour = -1;

	// Writes the component's state to disk. False if the write failed.
	static bool Save(const USkillComponent* Skills);
	// Reads the save and puts it into the component. False (component untouched) if there is no usable save.
	static bool Restore(USkillComponent* Skills);
	static bool Exists();

private:
	// Brings an older save up to CurrentVersion (nothing to do yet)
	void Migrate();
	// Is this a real item id in this version of the game?
	static bool ValidId(int32 Id);
};
