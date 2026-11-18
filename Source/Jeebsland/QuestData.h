#pragma once

#include "CoreMinimal.h"
#include "SkillComponent.h"

// ===================================================================================================
// Quests are data (2026-11-18): every quest, step and quest giver lives in spreadsheet files in Content/Data/, read once when the
// game starts (like the item tables, JeebItemData.h):
//   Quests.csv         one row per quest: title, giver, what it needs, what is said, the reward
//   QuestsSteps.csv    the steps of each quest, in order: talk, bring, kill, visit
//   QuestsPeople.csv   the people who give quests: name, where they stand, how they look, their greetings
// Each file explains its columns at its top. The SERVER's copy decides everything (what a quest needs, what it gives); players' copies
// only change what their screens show. A row that cannot be read is left out and the reason goes to the log ("Jeebsland quests:").
// The quest state of a character is UQuestComponent (QuestComponent.h); the people are ANpc (role QuestGiver), placed by AQuestPeople.
// ===================================================================================================
namespace JeebQuests
{
	enum class EStep : uint8 { Talk, Bring, Kill, Visit };

	struct FNeed
	{
		FName Quest;                              // set: that quest must be finished
		ESkillType Skill = ESkillType::Mining;    // otherwise: this skill at Level
		int32 Level = 1;
	};

	struct FStepDef
	{
		EStep Type = EStep::Talk;
		FName At;                 // the person (talk, bring)
		int32 ItemId = 0;         // bring: the item
		FString Target;           // kill: what (PenKnight); visit: "Ruin" or a place
		bool bRuin = false;       // visit: any forest ruin
		FVector2D Place = FVector2D::ZeroVector;   // visit a place: world cm
		float Radius = 1200.f;    // visit: cm
		int32 Count = 1;
		FString Mark;             // where the map points while the step isn't ready (see QuestsSteps.csv)
		FString Log;
		TArray<FString> Lines;    // said by At when the step is done (visit: the message on arriving)
		TArray<FString> Waiting;  // bring: said by At when you haven't enough
	};

	struct FQuestDef
	{
		FName Key;
		FString Title, Summary;
		FName Giver;
		TArray<FNeed> Needs;
		TArray<FString> Offer, Reminder, NotYet, Done;
		TArray<TPair<ESkillType, int32>> RewardXP;
		int32 RewardIngots = 0;
		TArray<FInvSlot> RewardItems;
		TArray<FStepDef> Steps;
	};

	struct FPersonDef
	{
		FName Key;
		FString Name;
		FVector2D Pos = FVector2D::ZeroVector;    // world cm (before stepping aside from anything standing there)
		float Yaw = 0.f;
		FLinearColor Tunic = FLinearColor(0.4f, 0.3f, 0.2f), Hair = FLinearColor(0.3f, 0.2f, 0.1f);
		int32 Hat = -1;           // the villagers' hats (ANpc::Setup): -1 none, 0 herbalist, 3 cap, 4 baker, 7 cowl, 8 feather
		bool bApron = false;
		float Scale = 1.f;
		TArray<FString> Greeting;
	};

	const TArray<FQuestDef>& Quests();
	const TArray<FPersonDef>& People();
	const FQuestDef* FindQuest(FName Key);
	const FPersonDef* FindPerson(FName Key);
	FString PersonName(FName Key);
	FString NeedText(const FNeed& N);
	FString RewardText(const FQuestDef& Q);   // "120 Woodcutting XP, 40 ingots, 2 Healing Potion"

	// A mark (QuestsSteps.csv Mark) as a world spot; false if it names nothing known (people are found by the caller)
	bool MarkSpot(const FString& Mark, FVector2D& Out);
	// How close to a quest giver the server lets a request count (cm; players talk from 2.3 m, the rest is lag)
	constexpr float TalkReach = 650.f;
	// An item by its name or key as the CSV files write it ("Healing Potion", "Iron Arrows", "TinOre", or a number); 0 empty, -1 unknown
	int32 ItemFromName(const FString& Name);
}
