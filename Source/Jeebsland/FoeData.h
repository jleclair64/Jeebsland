#pragma once

#include "CoreMinimal.h"
#include "SkillComponent.h"

// ===================================================================================================
// The world's enemies are data (2026-11-18): every kind of foe and every camp lives in spreadsheet files in Content/Data/, read once
// when the game starts (like the items, JeebItemData.h, and the quests, QuestData.h):
//   Foes.csv       one row per kind of foe: name, armour tier, shield, health, damage, speed, sight, leash, respawn, XP, loot, colours
//   FoeCamps.csv   one row per camp: where it is (metres from Oak Hill's plaza, or at the forest ruin nearest that spot), which foes, which props
// Each file explains its columns at its top. The SERVER's copy decides everything (health, damage, loot); players' copies only change
// looks and names. A row that cannot be read is left out and the reason goes to the log ("Jeebsland foes:").
// The foes themselves are AFoeKnight (FoeKnight.h; the bandit brain is FoeBandit.cpp); the camps are AFoeCamps (FoeCamps.h).
// ===================================================================================================
namespace JeebFoes
{
	// One loot line: Chance (0..1) of one of Ids (picked at random) x Min..Max; Id 0 = ingots
	struct FLootRoll
	{
		TArray<int32> Ids;
		int32 Min = 1, Max = 1;
		float Chance = 1.f;
	};

	struct FFoeDef
	{
		FName Key;
		FString Name;
		FName Group;                 // quests can ask for any foe of the group ("Bandit")
		int32 Tier = 1;              // armour and sword (JeebItems::ETier)
		int32 Shield = -1;           // -1 none, else the shield style (0 kite, 1 round, 2 tower)
		int32 Hp = 90;
		int32 HitMin = 11, HitMax = 15;
		float Takes = -1.f;          // share of a blow his armour lets through (< 0: the tier's own)
		float Speed = 400.f;         // cm/s when he chases
		float Sight = 1400.f;        // cm: how near a player must come to be noticed from in front ...
		float Behind = 0.5f;         // ... and the share of that from behind
		float Leash = 3500.f;        // cm from his camp: past it he gives up and walks home
		float Windup = 0.65f;        // s: the telegraph before his cut (the sword held high)
		float CoolMin = 1.6f, CoolMax = 2.6f;   // s between his swings
		float Block = 0.4f;          // chance he raises his shield after a blow (with a shield)
		float Respawn = 60.f;        // s from his fall to standing at camp again
		int32 XP = 0;                // Attack XP to whoever beats him (on top of the XP for every blow)
		FLinearColor Helm = FLinearColor::White, Body = FLinearColor::White;   // his helmet's and chestplate's colour, x the tier's
		TArray<FLootRoll> Loot;
	};

	struct FCampDef
	{
		FName Key;
		FString Name;
		bool bRuin = false;          // at the forest ruin nearest (North, East)
		FVector2D Near = FVector2D::ZeroVector;   // world cm (from the CSV's metres north and east of Oak Hill's plaza)
		TArray<int32> Foes;          // indices into Foes()
		TArray<FName> Props;         // FillKinds.csv kinds, the first in the middle (the campfire)
	};

	const TArray<FFoeDef>& Foes();
	const TArray<FCampDef>& Camps();
	int32 FindFoe(FName Key);        // INDEX_NONE if not there
	// Is Name something a quest's kill step may ask for (PenKnight, a foe's Key or a Group)?
	bool IsFoeTarget(const FString& Name);
	// Does foe Index count for a kill step asking for Name (its Key or its Group)?
	bool Matches(int32 Index, const FString& Name);
	// The server's roll of a beaten foe's loot (ingots, and items with their counts)
	void RollLoot(const FFoeDef& D, FRandomStream& R, TArray<FInvSlot>& OutItems, int32& OutIngots);

	// Places a foe never goes (towns): Oak Hill's box, the original square round the start, every other town on the world map. Margin: cm
	// added round each. To keep foes out of a new place, add it here (or to JeebWorld::Towns(), which this reads).
	bool InTown(float X, float Y, float Margin = 0.f);
	// Water deeper than wading (the bed more than about half a metre under the surface)
	bool DeepWater(float X, float Y);
	// A filler prop's model and size from FillKinds.csv (camps dress themselves with them): Mesh as a load path, Size in cm (footprint x, y,
	// height), bSolid when it has collision there. False if the kind isn't listed.
	bool FillKind(FName Kind, FString& OutMesh, FVector& OutSize, bool& bSolid);

	// ---- Huntable animals (2026-11-22, Docs/DeerPlan.md): Animals.csv and AnimalHerds.csv, read like the foes. Deer.h runs them.
	struct FAnimalDef
	{
		FName Key;
		FString Name;
		FName Group;
		FString Model;               // /Game/Animals/SK_<Model> and its clips
		float Scale = 1.f;
		int32 Hp = 24;
		float WalkSpeed = 100.f, RunSpeed = 860.f;                       // cm/s
		float Sight = 3000.f, Behind = 0.35f;                            // cm, share
		float HearRun = 2400.f, HearWalk = 1300.f, HearSneak = 400.f;    // cm
		float FleeAt = 1500.f, Calm = 9.f, Roam = 1200.f, Respawn = 300.f;
		int32 XP = 0;
		float WalkClip = 63.f, RunClip = 400.f;                          // cm/s the clips cover at scale 1
		TArray<FLootRoll> Loot;
		int32 MeatId = 0, BonesId = 0;   // what a kill drops, counted by the killer's Hunting level (HuntDrops.csv, table DropTable); 0 = none
		FName DropTable;
		int32 HuntXP = 0;                // Hunting XP for the kill
		// (2026-11-24, Docs/AnimalsPlan2.md) 0 flee, 1 cornered (flees; turns on whoever hurts it from close), 2 fight, 3 moody (50/50 per animal)
		uint8 Temper = 0;
		int32 Damage = 0;                // raw damage of a strike on a player
		float Reach = 180.f, Windup = 0.5f, Cool = 1.6f, Leash = 4500.f;   // cm, s, s, cm from home
		float LieDrop = 42.f;            // cm the death pose's body is lowered (at scale 1)
		float BoxLong = 1.f, BoxTall = 1.f;   // the stag's hurt boxes stretched to this animal
	};
	struct FHerdDef
	{
		FName Key;
		int32 Animal = INDEX_NONE;   // index into Animals()
		int32 Min = 3, Max = 5;      // how many in each herd
		bool bWild = false;          // spread over the open world: Herds of them
		int32 Herds = 1;
		FVector2D Near = FVector2D::ZeroVector;   // world cm (plaza rows)
	};
	const TArray<FAnimalDef>& Animals();
	const TArray<FHerdDef>& Herds();
	void RollLoot(const TArray<FLootRoll>& Loot, FRandomStream& R, TArray<FInvSlot>& OutItems, int32& OutIngots);
	// How many of a hunted thing one kill drops at this Hunting level: a roll on HuntDrops.csv's table, its chances blended between the Level1 and Level99 columns
	int32 HuntCount(FName Table, int32 HuntingLevel, FRandomStream& R);

	// The ground under foes as it is drawn (the world's 8 m mesh in the forest, Oak Hill's own 1 m ground)
	float Ground(float X, float Y);
}
