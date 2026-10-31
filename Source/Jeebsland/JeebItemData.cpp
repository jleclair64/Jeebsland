#include "JeebItemData.h"
#include "Inventory.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogJeebItems, Log, All);

namespace JeebItems
{
	namespace
	{
		// The Key column of the fixed tables, in enum order
		const TCHAR* const TierKeys[NumTiers] = { TEXT("Tin"), TEXT("Iron"), TEXT("Bronze"), TEXT("Steel"), TEXT("Silver"), TEXT("Gold"), TEXT("Onyx"), TEXT("Emerald"), TEXT("Diamond") };
		const TCHAR* const KindKeys[K_Count] = { TEXT("Sword"), TEXT("Axe"), TEXT("Pickaxe"), TEXT("Spear"), TEXT("Broadsword"), TEXT("BattleAxe"), TEXT("Katana"),
			TEXT("Wand"), TEXT("Staff"), TEXT("Bow"), TEXT("Rod"), TEXT("Shield"), TEXT("Helmet"), TEXT("Chest"), TEXT("Shoulders"), TEXT("Gloves"), TEXT("Boots"),
			TEXT("Cape"), TEXT("Ring"), TEXT("Amulet"), TEXT("Crest") };
		const TCHAR* const MaterialKeys[] = { TEXT("CopperOre"), TEXT("Coal"), TEXT("Logs"), TEXT("Diamond"), TEXT("Bacon"), TEXT("PineLogs"), TEXT("RedwoodLogs"),
			TEXT("Perch"), TEXT("Trout"), TEXT("GoldenCarp"), TEXT("Hide"), TEXT("Bones"), TEXT("TinOre"), TEXT("IronOre"), TEXT("SilverOre"), TEXT("GoldOre"),
			TEXT("Onyx"), TEXT("Emerald") };
		constexpr int32 NumMaterials = UE_ARRAY_COUNT(MaterialKeys);
		static_assert(NumMaterials == int32(EItemType::Emerald) + 1, "MaterialKeys must list every EItemType, in order");

		void Problem(const FString& What) { UE_LOG(LogJeebItems, Error, TEXT("Jeebsland items: %s"), *What); }

		// One CSV file: a header row naming the columns, then one row per entry. Rows whose first cell starts with # are notes.
		// Cells may be "quoted" (a quoted cell can hold commas; "" inside it is one quote mark).
		struct FTable
		{
			FString File;
			TMap<FString, int32> Cols;   // lower-case column name -> index
			TArray<TArray<FString>> Rows;
			mutable TSet<FString> Missing;

			bool Load(const TCHAR* Name)
			{
				File = Name;
				FString Text;
				const FString Path = FPaths::ProjectContentDir() / TEXT("Data") / Name;
				if (!FFileHelper::LoadFileToString(Text, *Path)) { Problem(FString::Printf(TEXT("cannot read %s"), *Path)); return false; }
				TArray<TArray<FString>> All;
				TArray<FString> Row;
				FString Cell;
				bool bQuoted = false, bCellStarted = false;
				auto EndCell = [&]() { Row.Add(Cell.TrimStartAndEnd()); Cell.Reset(); bCellStarted = false; };
				auto EndRow = [&]() { EndCell(); if (Row.Num() > 1 || !Row[0].IsEmpty()) All.Add(Row); Row.Reset(); };
				for (int32 i = 0; i < Text.Len(); ++i)
				{
					const TCHAR C = Text[i];
					if (bQuoted)
					{
						if (C == '"')
						{
							if (i + 1 < Text.Len() && Text[i + 1] == '"') { Cell.AppendChar('"'); ++i; }
							else bQuoted = false;
						}
						else Cell.AppendChar(C);
						continue;
					}
					if (C == '"' && !bCellStarted) { bQuoted = true; bCellStarted = true; }
					else if (C == ',') EndCell();
					else if (C == '\n') EndRow();
					else if (C == '\r' || C == 0xFEFF) continue;   // (Windows line ends, a byte-order mark)
					else { Cell.AppendChar(C); if (C != ' ') bCellStarted = true; }
				}
				if (!Cell.IsEmpty() || Row.Num() > 0) EndRow();
				All.RemoveAll([](const TArray<FString>& R) { return R.Num() > 0 && R[0].StartsWith(TEXT("#")); });
				if (All.Num() == 0) { Problem(FString::Printf(TEXT("%s is empty"), Name)); return false; }
				for (int32 c = 0; c < All[0].Num(); ++c) Cols.Add(All[0][c].ToLower(), c);
				All.RemoveAt(0);
				Rows = MoveTemp(All);
				return true;
			}

			// A cell, or an empty string (a missing column is reported once)
			FString S(int32 R, const TCHAR* Col) const
			{
				const int32* C = Cols.Find(FString(Col).ToLower());
				if (!C) { if (!Missing.Contains(Col)) { Missing.Add(Col); Problem(FString::Printf(TEXT("%s has no column \"%s\""), *File, Col)); } return FString(); }
				return Rows[R].IsValidIndex(*C) ? Rows[R][*C] : FString();
			}
			float F(int32 R, const TCHAR* Col, float Def) const { const FString V = S(R, Col); return V.IsEmpty() ? Def : FCString::Atof(*V); }
			int32 I(int32 R, const TCHAR* Col, int32 Def) const { const FString V = S(R, Col); return V.IsEmpty() ? Def : FMath::RoundToInt(FCString::Atof(*V)); }
			bool B(int32 R, const TCHAR* Col) const { const FString V = S(R, Col).ToLower(); return V == TEXT("yes") || V == TEXT("true") || V == TEXT("1"); }
			// "0.52 0.54 0.55" (red green blue, optionally alpha), 0 to 1
			FLinearColor Color(int32 R, const TCHAR* Col, const FLinearColor& Def) const
			{
				TArray<FString> P;
				S(R, Col).ParseIntoArrayWS(P);
				if (P.Num() < 3) return Def;
				return FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), P.Num() > 3 ? FCString::Atof(*P[3]) : 1.f);
			}
			// The row whose Key column matches, or -1
			int32 Find(const TCHAR* Key) const
			{
				for (int32 r = 0; r < Rows.Num(); ++r) if (S(r, TEXT("Key")).Equals(Key, ESearchCase::IgnoreCase)) return r;
				Problem(FString::Printf(TEXT("%s has no row for \"%s\" (it keeps plain defaults)"), *File, Key));
				return -1;
			}
		};

		bool MaterialFromKey(const FString& Key, EItemType& Out)
		{
			for (int32 m = 0; m < NumMaterials; ++m) if (Key.Equals(MaterialKeys[m], ESearchCase::IgnoreCase)) { Out = static_cast<EItemType>(m); return true; }
			return false;
		}

		void LoadTiers(FItemTables& T)
		{
			T.Tiers.SetNum(NumTiers);
			FTable Tab;
			const bool bOk = Tab.Load(TEXT("Tiers.csv"));
			for (int32 t = 0; t < NumTiers; ++t)
			{
				FTierRow& Row = T.Tiers[t];
				Row.Name = TierKeys[t];
				for (FLinearColor& C : Row.Palette) C = FLinearColor(0.5f, 0.5f, 0.5f);
				const int32 r = bOk ? Tab.Find(TierKeys[t]) : -1;
				if (r < 0) continue;
				Row.Name = Tab.S(r, TEXT("Name"));
				Row.Blurb = Tab.S(r, TEXT("Blurb"));
				const FString Cls = Tab.S(r, TEXT("Class")).ToLower();
				Row.Class = Cls == TEXT("knight") ? 1 : Cls == TEXT("troll") ? 2 : Cls == TEXT("wizard") ? 3 : 0;
				Row.EquipLevel = Tab.I(r, TEXT("EquipLevel"), 1);
				Row.DefenceMult = Tab.F(r, TEXT("DefenceMult"), 1.f);
				Row.DamageSteps = Tab.F(r, TEXT("DamageSteps"), 0.f);
				Row.BootStamina = Tab.F(r, TEXT("BootStamina"), 1.f);
				Row.PriceMult = Tab.F(r, TEXT("PriceMult"), 1.f);
				Row.SmithLevel = Tab.I(r, TEXT("SmithLevel"), 1);
				Row.SmithXP = Tab.I(r, TEXT("SmithXP"), 0);
				const TCHAR* MatCols[2] = { TEXT("Mat1"), TEXT("Mat2") };
				const TCHAR* ShareCols[2] = { TEXT("Mat1Share"), TEXT("Mat2Share") };
				for (int32 m = 0; m < 2; ++m)
				{
					const FString K = Tab.S(r, MatCols[m]);
					if (K.IsEmpty()) continue;
					if (!MaterialFromKey(K, Row.Mat[m])) { Problem(FString::Printf(TEXT("Tiers.csv, %s: no material called \"%s\""), TierKeys[t], *K)); continue; }
					Row.Share[m] = Tab.F(r, ShareCols[m], 0.f);
				}
				Row.Gem = Tab.Color(r, TEXT("GemColor"), FLinearColor::White);
				const TCHAR* Roles[7] = { TEXT("Plate"), TEXT("Under"), TEXT("Trim"), TEXT("Cloth"), TEXT("Accent"), TEXT("Leather"), TEXT("BootBody") };
				for (int32 k = 0; k < 7; ++k) Row.Palette[k] = Tab.Color(r, Roles[k], Row.Palette[k]);
				Row.CapeColor = Tab.Color(r, TEXT("CapeColor"), Row.CapeColor);
			}
		}

		void LoadKinds(FItemTables& T)
		{
			T.Kinds.SetNum(K_Count);
			FTable Tab;
			const bool bOk = Tab.Load(TEXT("Kinds.csv"));
			for (int32 k = 0; k < K_Count; ++k)
			{
				FKindRow& Row = T.Kinds[k];
				Row.Name = KindKeys[k];
				const int32 r = bOk ? Tab.Find(KindKeys[k]) : -1;
				if (r < 0) continue;
				Row.Name = Tab.S(r, TEXT("Name"));
				Row.Blurb = Tab.S(r, TEXT("Blurb"));
				Row.DamageMin = Tab.I(r, TEXT("DamageMin"), 1);
				Row.DamageMax = FMath::Max(Row.DamageMin, Tab.I(r, TEXT("DamageMax"), 2));
				Row.Range = Tab.F(r, TEXT("Range"), 200.f);
				Row.Cooldown = FMath::Max(0.05f, Tab.F(r, TEXT("Cooldown"), 1.f));
				Row.Defence = Tab.I(r, TEXT("Defence"), 0);
				Row.BuyPrice = Tab.I(r, TEXT("BuyPrice"), 0);
				Row.Mass = Tab.I(r, TEXT("Mass"), 0);
				Row.SmithExtraLevel = Tab.I(r, TEXT("SmithExtraLevel"), 0);
				Row.bTwoHanded = Tab.B(r, TEXT("TwoHanded"));
			}
		}

		void LoadMaterials(FItemTables& T)
		{
			T.Materials.SetNum(NumMaterials);
			FTable Tab;
			const bool bOk = Tab.Load(TEXT("Materials.csv"));
			for (int32 m = 0; m < NumMaterials; ++m)
			{
				FMaterialRow& Row = T.Materials[m];
				Row.Name = MaterialKeys[m];
				const int32 r = bOk ? Tab.Find(MaterialKeys[m]) : -1;
				if (r < 0) continue;
				Row.Name = Tab.S(r, TEXT("Name"));
				const FString G = Tab.S(r, TEXT("Group")).ToLower();
				Row.Group = G == TEXT("ore") ? MG_Ore : G == TEXT("gem") ? MG_Gem : G == TEXT("wood") ? MG_Wood : G == TEXT("fish") ? MG_Fish : MG_Animal;
				Row.StackSize = FMath::Max(1, Tab.I(r, TEXT("StackSize"), 5));
				Row.SellValue = Tab.I(r, TEXT("SellValue"), 1);
				Row.Color = Tab.Color(r, TEXT("Color"), Row.Color);
				Row.GatherLevel = Tab.I(r, TEXT("GatherLevel"), 1);
				Row.FishXP = Tab.I(r, TEXT("FishXP"), 8);
			}
		}

		void LoadPotions(FItemTables& T)
		{
			FTable Tab;
			if (!Tab.Load(TEXT("Potions.csv"))) return;
			for (int32 r = 0; r < Tab.Rows.Num() && T.Potions.Num() < MaxPotions; ++r)
			{
				FPotionRow Row;
				Row.Name = Tab.S(r, TEXT("Name"));
				Row.Text = Tab.S(r, TEXT("Text"));
				Row.Shop = Tab.S(r, TEXT("Shop"));
				const FString E = Tab.S(r, TEXT("Effect")).ToLower();
				Row.Effect = E == TEXT("speed") ? PE_Speed : E == TEXT("heal") ? PE_Heal : E == TEXT("vigour") || E == TEXT("vigor") ? PE_Vigour : E == TEXT("radar") ? PE_Radar : PE_None;
				if (Row.Effect == PE_None) Problem(FString::Printf(TEXT("Potions.csv, %s: unknown effect \"%s\" (speed, heal, vigour or radar)"), *Row.Name, *E));
				Row.Amount = Tab.F(r, TEXT("Amount"), 1.f);
				Row.Seconds = Tab.F(r, TEXT("Seconds"), 0.f);
				Row.Cooldown = Tab.F(r, TEXT("Cooldown"), 60.f);
				Row.Price = Tab.I(r, TEXT("Price"), 10);
				Row.StackSize = FMath::Max(1, Tab.I(r, TEXT("StackSize"), 10));
				Row.Color = Tab.Color(r, TEXT("Color"), FLinearColor::White);
				T.Potions.Add(Row);
			}
		}

		void LoadAccessories(FItemTables& T)
		{
			T.Crest.Name = TEXT("Helm Brush");
			FTable Tab;
			if (!Tab.Load(TEXT("Accessories.csv"))) return;
			bool bCrest = false;
			for (int32 r = 0; r < Tab.Rows.Num(); ++r)
			{
				FAccessoryRow Row;
				Row.Name = Tab.S(r, TEXT("Name"));
				Row.Text = Tab.S(r, TEXT("Text"));
				Row.Price = Tab.I(r, TEXT("Price"), 100);
				Row.Mods.AttackMul = Tab.F(r, TEXT("AttackMul"), 1.f);
				Row.Mods.DefenseMul = Tab.F(r, TEXT("DefenceMul"), 1.f);
				Row.Mods.CritChance = Tab.F(r, TEXT("CritChance"), 0.f);
				Row.Mods.FloorDelta = Tab.I(r, TEXT("DamageFloor"), 0);
				Row.Mods.CeilDelta = Tab.I(r, TEXT("DamageCeiling"), 0);
				Row.Mods.bWild = Tab.B(r, TEXT("Wild"));
				Row.Mods.SpeedMul = Tab.F(r, TEXT("SpeedMul"), 1.f);
				Row.Mods.MaxHealthBonus = Tab.I(r, TEXT("MaxHealth"), 0);
				Row.Band = Tab.Color(r, TEXT("Band"), Row.Band);
				Row.Gem = Tab.Color(r, TEXT("Gem"), Row.Gem);
				const FString K = Tab.S(r, TEXT("Kind")).ToLower();
				if (K == TEXT("ring")) { if (T.Rings.Num() < MaxVariants) T.Rings.Add(Row); else Problem(TEXT("Accessories.csv: more than 10 rings")); }
				else if (K == TEXT("amulet")) { if (T.Amulets.Num() < MaxVariants) T.Amulets.Add(Row); else Problem(TEXT("Accessories.csv: more than 10 amulets")); }
				else if (K == TEXT("crest")) { if (!bCrest) T.Crest = Row; bCrest = true; }
				else Problem(FString::Printf(TEXT("Accessories.csv, %s: unknown kind \"%s\" (ring, amulet or crest)"), *Row.Name, *K));
			}
		}

		void LoadShieldStyles(FItemTables& T)
		{
			T.ShieldStyles.SetNum(NumShieldStyles);
			const TCHAR* Names[NumShieldStyles] = { TEXT("Kite Shield"), TEXT("Round Shield"), TEXT("Tower Shield") };
			for (int32 s = 0; s < NumShieldStyles; ++s) T.ShieldStyles[s].Name = Names[s];
			FTable Tab;
			if (!Tab.Load(TEXT("ShieldStyles.csv"))) return;
			if (Tab.Rows.Num() < NumShieldStyles) Problem(TEXT("ShieldStyles.csv needs three rows: kite, round, tower"));
			for (int32 s = 0; s < NumShieldStyles && s < Tab.Rows.Num(); ++s)
			{
				FShieldStyleRow& Row = T.ShieldStyles[s];
				Row.Name = Tab.S(s, TEXT("Name"));
				Row.Note = Tab.S(s, TEXT("Note"));
				Row.GuardMul = Tab.F(s, TEXT("GuardMul"), 1.f);
				Row.MoveMul = Tab.F(s, TEXT("MoveMul"), 1.f);
				Row.RecoverDelay = Tab.F(s, TEXT("RecoverDelay"), 10.f);
			}
		}
	}

	const FItemTables& Tables()
	{
		static FItemTables T;
		static bool bLoaded = false;
		if (!bLoaded)
		{
			bLoaded = true;
			LoadTiers(T);
			LoadKinds(T);
			LoadMaterials(T);
			LoadPotions(T);
			LoadAccessories(T);
			LoadShieldStyles(T);
			UE_LOG(LogJeebItems, Log, TEXT("Jeebsland items: read %d potions, %d rings, %d amulets from Content/Data"), T.Potions.Num(), T.Rings.Num(), T.Amulets.Num());
		}
		return T;
	}
}
