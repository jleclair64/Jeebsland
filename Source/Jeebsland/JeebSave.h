#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "JeebSave.generated.h"

class USkillComponent;

// A player's character. Single player: written to Saved/SaveGames/Jeebsland_0.sav. Online: the SERVER writes one file per
// account (Saved/Characters/<name>.sav, see JeebAccounts). See Docs/SaveLoadPlan.md.
// RULES: never reorder JeebItems::EKind / EItemType (item ids are stored as numbers); when the layout of this class changes in a
// way old saves cannot be read, bump CurrentVersion and add a step to UJeebSave::Migrate. (Adding a new UPROPERTY is fine:
// old saves simply don't have it and it keeps its default.)
UCLASS()
class UJeebSave : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 9;   // 2: the tier redo; 3: capes became the PvP rank; 4: arrows for everyone; 7: a bow on the weapon bar; 8: at least the starting arrows; 9: arrows stack to 99, topped up to 99 of each (Migrate converts older ones)
	static const FString SlotName;

	// 0 = "not in the file". (Unreal leaves out a value that equals its default, so while the default was CurrentVersion no
	// file ever stored it and every old save read as current. Since 2026-10-30 Capture always writes it; see Migrate.)
	UPROPERTY() int32 SaveVersion = 0;

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
	UPROPERTY() TArray<uint8> Explored;   // map fog (USkillComponent::Explored)
	UPROPERTY() int64 LastGambleHour = -1;

	// Where the knight stood (online: you log back in where you logged out)
	UPROPERTY() bool bHasPos = false;
	UPROPERTY() FVector Pos = FVector::ZeroVector;
	UPROPERTY() float Yaw = 0.f;

	// Single player: writes the component's state to disk. False if the write failed.
	static bool Save(const USkillComponent* Skills);
	// Single player: reads the save and puts it into the component. False (component untouched) if there is no usable save.
	static bool Restore(USkillComponent* Skills);
	static bool Exists();

	// The building blocks (the server uses these per account). Body: the knight, for its position (may be null).
	static UJeebSave* Capture(const USkillComponent* Skills, const AActor* Body);
	bool ApplyTo(USkillComponent* Skills);   // checked on the way in; false (component untouched) if written by a newer game
	// A file of its own: written to a temporary file first and then swapped in, so a crash mid-write can't destroy the save
	static bool WriteFile(UJeebSave* Save, const FString& Path);
	static UJeebSave* ReadFile(const FString& Path);   // null if missing or unreadable

private:
	// Brings an older save up to CurrentVersion (nothing to do yet)
	void Migrate();
	// Is this a real item id in this version of the game?
	static bool ValidId(int32 Id);
};
