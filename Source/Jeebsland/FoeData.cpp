#include "FoeData.h"
#include "QuestData.h"
#include "Inventory.h"
#include "OakHill.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "JeebWater.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogJeebFoes, Log, All);

namespace JeebFoes
{
	namespace
	{
		void Problem(const FString& What) { UE_LOG(LogJeebFoes, Error, TEXT("Jeebsland foes: %s"), *What); }

		// One CSV file (the same rules as the item and quest tables): a header row naming the columns, rows starting with # are notes,
		// "quoted" cells may hold commas ("" is one quote mark inside them)
		struct FSheet
		{
			TMap<FString, int32> Cols;
			TArray<TArray<FString>> Rows;
			bool Load(const TCHAR* Name)
			{
				FString Text;
				const FString Path = FPaths::ProjectContentDir() / TEXT("Data") / Name;
				if (!FFileHelper::LoadFileToString(Text, *Path)) { Problem(FString::Printf(TEXT("cannot read %s"), *Path)); return false; }
				TArray<TArray<FString>> All;
				TArray<FString> Row;
				FString Cell;
				bool bQuoted = false, bStarted = false;
				auto EndCell = [&]() { Row.Add(Cell.TrimStartAndEnd()); Cell.Reset(); bStarted = false; };
				auto EndRow = [&]() { EndCell(); if (Row.Num() > 1 || !Row[0].IsEmpty()) All.Add(Row); Row.Reset(); };
				for (int32 i = 0; i < Text.Len(); ++i)
				{
					const TCHAR C = Text[i];
					if (bQuoted)
					{
						if (C == '"') { if (i + 1 < Text.Len() && Text[i + 1] == '"') { Cell.AppendChar('"'); ++i; } else bQuoted = false; }
						else Cell.AppendChar(C);
						continue;
					}
					if (C == '"' && !bStarted) { bQuoted = true; bStarted = true; }
					else if (C == ',') EndCell();
					else if (C == '\n') EndRow();
					else if (C == '\r' || C == 0xFEFF) continue;
					else { Cell.AppendChar(C); if (C != ' ') bStarted = true; }
				}
				if (!Cell.IsEmpty() || Row.Num() > 0) EndRow();
				All.RemoveAll([](const TArray<FString>& R) { return R.Num() > 0 && R[0].StartsWith(TEXT("#")); });
				if (All.Num() == 0) { Problem(FString::Printf(TEXT("%s is empty"), Name)); return false; }
				for (int32 c = 0; c < All[0].Num(); ++c) Cols.Add(All[0][c].ToLower(), c);
				All.RemoveAt(0);
				Rows = MoveTemp(All);
				return true;
			}
			FString S(int32 R, const TCHAR* Col) const
			{
				const int32* C = Cols.Find(FString(Col).ToLower());
				return (C && Rows[R].IsValidIndex(*C)) ? Rows[R][*C] : FString();
			}
			float F(int32 R, const TCHAR* Col, float Def) const { const FString V = S(R, Col); return V.IsEmpty() ? Def : FCString::Atof(*V); }
			int32 I(int32 R, const TCHAR* Col, int32 Def) const { const FString V = S(R, Col); return V.IsEmpty() ? Def : FMath::RoundToInt(FCString::Atof(*V)); }
			FLinearColor Color(int32 R, const TCHAR* Col, const FLinearColor& Def) const
			{
				TArray<FString> P;
				S(R, Col).ParseIntoArrayWS(P);
				if (P.Num() < 3) return Def;
				return FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
			}
		};

		int32 TierFrom(const FString& S)
		{
			for (int32 t = 0; t < JeebItems::NumTiers; ++t) if (S.Equals(JeebItems::TierName(t), ESearchCase::IgnoreCase)) return t;
			return S.IsNumeric() ? FMath::Clamp(FCString::Atoi(*S), 0, JeebItems::NumTiers - 1) : -1;
		}

		// "Ingots 4-10; Iron Arrows 3-8 @30; Healing Potion x2 @10; Tin Helmet | Tin Boots @4"
		void ParseLoot(const FString& Key, const FString& Text, TArray<FLootRoll>& Out)
		{
			TArray<FString> Parts;
			Text.ParseIntoArray(Parts, TEXT(";"), true);
			for (FString P : Parts)
			{
				P.TrimStartAndEndInline();
				if (P.IsEmpty()) continue;
				FLootRoll L;
				FString Body = P, Chance;
				if (P.Split(TEXT("@"), &Body, &Chance)) { Chance.TrimStartAndEndInline(); Chance.RemoveFromEnd(TEXT("%")); L.Chance = FMath::Clamp(FCString::Atof(*Chance) / 100.f, 0.f, 1.f); }
				TArray<FString> Alts;
				Body.ParseIntoArray(Alts, TEXT("|"), true);
				bool bCountSet = false;
				for (FString A : Alts)
				{
					A.TrimStartAndEndInline();
					// a count at the end: "x2", "3-8" or "5"
					TArray<FString> W;
					A.ParseIntoArrayWS(W);
					if (W.Num() >= 2)
					{
						FString Last = W.Last();
						FString Lo, Hi;
						bool bCount = false;
						if (Last.StartsWith(TEXT("x")) && Last.Mid(1).IsNumeric()) { L.Min = L.Max = FCString::Atoi(*Last.Mid(1)); bCount = true; }
						else if (Last.Split(TEXT("-"), &Lo, &Hi) && Lo.IsNumeric() && Hi.IsNumeric()) { L.Min = FCString::Atoi(*Lo); L.Max = FCString::Atoi(*Hi); bCount = true; }
						else if (Last.IsNumeric()) { L.Min = L.Max = FCString::Atoi(*Last); bCount = true; }
						if (bCount) { bCountSet = true; W.Pop(); A = FString::Join(W, TEXT(" ")); }
					}
					if (A.Equals(TEXT("Ingots"), ESearchCase::IgnoreCase) || A.Equals(TEXT("Ingot"), ESearchCase::IgnoreCase)) { L.Ids.Add(0); continue; }
					const int32 Id = JeebQuests::ItemFromName(A);
					if (Id > 0 && !JeebItems::ItemName(Id).IsEmpty()) L.Ids.Add(Id);
					else Problem(FString::Printf(TEXT("Foes.csv, %s: no item called \"%s\" in its loot"), *Key, *A));
				}
				if (!bCountSet) L.Min = L.Max = 1;
				L.Max = FMath::Max(L.Min, L.Max);
				if (L.Ids.Num() > 0) Out.Add(L);
			}
		}

		struct FAll
		{
			TArray<FFoeDef> Foes;
			TArray<FCampDef> Camps;
		};

		void LoadFoes(FAll& A)
		{
			FSheet T;
			if (!T.Load(TEXT("Foes.csv"))) return;
			for (int32 r = 0; r < T.Rows.Num(); ++r)
			{
				FFoeDef D;
				D.Key = FName(*T.S(r, TEXT("Key")));
				if (D.Key.IsNone()) continue;
				D.Name = T.S(r, TEXT("Name"));
				if (D.Name.IsEmpty()) D.Name = D.Key.ToString();
				D.Group = FName(*T.S(r, TEXT("Group")));
				D.Tier = TierFrom(T.S(r, TEXT("Tier")));
				if (D.Tier < 0) { Problem(FString::Printf(TEXT("Foes.csv, %s: unknown tier \"%s\"; read as Iron"), *D.Key.ToString(), *T.S(r, TEXT("Tier")))); D.Tier = JeebItems::T_Iron; }
				const FString Sh = T.S(r, TEXT("Shield")).ToLower();
				D.Shield = Sh == TEXT("kite") ? 0 : Sh == TEXT("round") ? 1 : Sh == TEXT("tower") ? 2 : -1;
				if (D.Shield < 0 && !Sh.IsEmpty() && Sh != TEXT("none")) Problem(FString::Printf(TEXT("Foes.csv, %s: shield \"%s\" is none, kite, round or tower; read as none"), *D.Key.ToString(), *Sh));
				D.Hp = FMath::Max(1, T.I(r, TEXT("Health"), 90));
				D.HitMin = FMath::Max(0, T.I(r, TEXT("HitMin"), 11));
				D.HitMax = FMath::Max(D.HitMin, T.I(r, TEXT("HitMax"), 15));
				D.Takes = T.F(r, TEXT("Takes"), -1.f);
				if (D.Takes >= 0.f) D.Takes = FMath::Clamp(D.Takes, 0.05f, 1.f);
				D.Speed = FMath::Clamp(T.F(r, TEXT("Speed"), 4.f), 1.f, 9.f) * 100.f;
				D.Sight = FMath::Clamp(T.F(r, TEXT("Sight"), 14.f), 0.f, 60.f) * 100.f;
				D.Behind = FMath::Clamp(T.F(r, TEXT("Behind"), 0.5f), 0.f, 1.f);
				D.Leash = FMath::Clamp(T.F(r, TEXT("Leash"), 35.f), 8.f, 120.f) * 100.f;
				D.Windup = FMath::Clamp(T.F(r, TEXT("Windup"), 0.65f), 0.2f, 2.f);
				D.CoolMin = FMath::Max(0.3f, T.F(r, TEXT("CoolMin"), 1.6f));
				D.CoolMax = FMath::Max(D.CoolMin, T.F(r, TEXT("CoolMax"), 2.6f));
				D.Block = FMath::Clamp(T.F(r, TEXT("Block"), 0.4f), 0.f, 1.f);
				D.Respawn = FMath::Clamp(T.F(r, TEXT("Respawn"), 60.f), 8.f, 3600.f);
				D.XP = FMath::Max(0, T.I(r, TEXT("XP"), 0));
				D.Helm = T.Color(r, TEXT("Helm"), FLinearColor::White);
				D.Body = T.Color(r, TEXT("Body"), FLinearColor::White);
				ParseLoot(D.Key.ToString(), T.S(r, TEXT("Loot")), D.Loot);
				if (A.Foes.ContainsByPredicate([&](const FFoeDef& O) { return O.Key == D.Key; })) { Problem(FString::Printf(TEXT("Foes.csv: %s twice; the second row is left out"), *D.Key.ToString())); continue; }
				A.Foes.Add(D);
			}
		}

		void LoadCamps(FAll& A)
		{
			FSheet T;
			if (!T.Load(TEXT("FoeCamps.csv"))) return;
			for (int32 r = 0; r < T.Rows.Num(); ++r)
			{
				FCampDef C;
				C.Key = FName(*T.S(r, TEXT("Key")));
				if (C.Key.IsNone()) continue;
				C.Name = T.S(r, TEXT("Name"));
				C.bRuin = T.S(r, TEXT("Where")).Equals(TEXT("ruin"), ESearchCase::IgnoreCase);
				C.Near = FVector2D(JeebOakHill::PosX + T.F(r, TEXT("North"), 0.f) * 100.f, JeebOakHill::PosY + T.F(r, TEXT("East"), 0.f) * 100.f);
				TArray<FString> P;
				T.S(r, TEXT("Foes")).ParseIntoArray(P, TEXT(";"), true);
				for (FString F : P)
				{
					F.TrimStartAndEndInline();
					int32 N = 1;
					FString K = F, Cnt;
					if (F.Split(TEXT(" x"), &K, &Cnt, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && Cnt.IsNumeric()) N = FMath::Clamp(FCString::Atoi(*Cnt), 1, 8);
					else K = F;
					K.TrimStartAndEndInline();
					const int32 Idx = A.Foes.IndexOfByPredicate([&](const FFoeDef& D) { return D.Key.ToString().Equals(K, ESearchCase::IgnoreCase); });
					if (Idx == INDEX_NONE) { Problem(FString::Printf(TEXT("FoeCamps.csv, %s: no foe \"%s\" in Foes.csv"), *C.Key.ToString(), *K)); continue; }
					for (int32 n = 0; n < N; ++n) C.Foes.Add(Idx);
				}
				TArray<FString> Pr;
				T.S(r, TEXT("Props")).ParseIntoArray(Pr, TEXT(";"), true);
				for (FString X : Pr) { X.TrimStartAndEndInline(); if (!X.IsEmpty()) C.Props.Add(FName(*X)); }
				if (C.Foes.Num() > 12) { Problem(FString::Printf(TEXT("FoeCamps.csv, %s: more than 12 foes; the rest are left out"), *C.Key.ToString())); C.Foes.SetNum(12); }
				A.Camps.Add(C);
			}
		}

		const FAll& All()
		{
			static const FAll A = []()
			{
				FAll X;
				LoadFoes(X);
				LoadCamps(X);
				UE_LOG(LogJeebFoes, Log, TEXT("JEEBFOE data: %d kinds of foe, %d camps"), X.Foes.Num(), X.Camps.Num());
				return X;
			}();
			return A;
		}
	}

	const TArray<FFoeDef>& Foes() { return All().Foes; }
	const TArray<FCampDef>& Camps() { return All().Camps; }

	namespace
	{
		struct FAnimalTables { TArray<FAnimalDef> Animals; TArray<FHerdDef> Herds; };
		const FAnimalTables& AllAnimals()
		{
			static const FAnimalTables T = []()
			{
				FAnimalTables Out;
				FSheet A;
				if (A.Load(TEXT("Animals.csv")))
					for (int32 r = 0; r < A.Rows.Num(); ++r)
					{
						FAnimalDef D;
						const FString Key = A.S(r, TEXT("Key"));
						if (Key.IsEmpty()) continue;
						D.Key = FName(*Key);
						D.Name = A.S(r, TEXT("Name")); if (D.Name.IsEmpty()) D.Name = Key;
						D.Group = FName(*A.S(r, TEXT("Group")));
						D.Model = A.S(r, TEXT("Model")); if (D.Model.IsEmpty()) D.Model = Key;
						D.Scale = FMath::Clamp(A.F(r, TEXT("Scale"), 1.f), 0.2f, 5.f);
						D.Hp = FMath::Max(1, A.I(r, TEXT("Health"), 24));
						D.WalkSpeed = FMath::Max(10.f, A.F(r, TEXT("WalkSpeed"), 1.f) * 100.f);
						D.RunSpeed = FMath::Max(D.WalkSpeed, A.F(r, TEXT("RunSpeed"), 8.6f) * 100.f);
						D.Sight = A.F(r, TEXT("Sight"), 30.f) * 100.f;
						D.Behind = FMath::Clamp(A.F(r, TEXT("Behind"), 0.35f), 0.f, 1.f);
						D.HearRun = A.F(r, TEXT("HearRun"), 24.f) * 100.f;
						D.HearWalk = A.F(r, TEXT("HearWalk"), 13.f) * 100.f;
						D.HearSneak = A.F(r, TEXT("HearSneak"), 4.f) * 100.f;
						D.FleeAt = A.F(r, TEXT("FleeAt"), 15.f) * 100.f;
						D.Calm = FMath::Max(1.f, A.F(r, TEXT("Calm"), 9.f));
						D.Roam = FMath::Max(200.f, A.F(r, TEXT("Roam"), 12.f) * 100.f);
						D.Respawn = FMath::Max(1.f, A.F(r, TEXT("Respawn"), 300.f));
						D.XP = FMath::Max(0, A.I(r, TEXT("XP"), 0));
						D.WalkClip = FMath::Max(5.f, A.F(r, TEXT("WalkClip"), 63.f));
						D.RunClip = FMath::Max(5.f, A.F(r, TEXT("RunClip"), 400.f));
						ParseLoot(FString::Printf(TEXT("(Animals.csv) %s"), *Key), A.S(r, TEXT("Loot")), D.Loot);
						D.HuntXP = FMath::Max(0, A.I(r, TEXT("HuntXP"), 0));
						D.DropTable = FName(*A.S(r, TEXT("DropTable")));
						auto ItemCell = [&](const TCHAR* Col) { const FString N = A.S(r, Col); if (N.IsEmpty()) return 0; const int32 Id = JeebQuests::ItemFromName(N); if (Id <= 0) Problem(FString::Printf(TEXT("Animals.csv, %s: no item called \"%s\" in %s"), *Key, *N, Col)); return FMath::Max(0, Id); };
						{
							const FString Tm = A.S(r, TEXT("Temper")).ToLower();
							D.Temper = Tm == TEXT("cornered") ? 1 : Tm == TEXT("fight") ? 2 : Tm == TEXT("moody") ? 3 : 0;
						}
						D.Damage = FMath::Max(0, A.I(r, TEXT("Damage"), 0));
						D.Reach = FMath::Max(50.f, A.F(r, TEXT("Reach"), 1.8f) * 100.f);
						D.Windup = FMath::Clamp(A.F(r, TEXT("Windup"), 0.5f), 0.1f, 3.f);
						D.Cool = FMath::Clamp(A.F(r, TEXT("Cool"), 1.6f), 0.3f, 10.f);
						D.Leash = FMath::Max(1000.f, A.F(r, TEXT("Leash"), 45.f) * 100.f);
						D.LieDrop = A.F(r, TEXT("LieDrop"), 42.f);
						D.BoxLong = FMath::Clamp(A.F(r, TEXT("BoxLong"), 1.f), 0.1f, 4.f);
						D.BoxTall = FMath::Clamp(A.F(r, TEXT("BoxTall"), 1.f), 0.1f, 4.f);
						D.MeatId = ItemCell(TEXT("Meat"));
						D.BonesId = ItemCell(TEXT("Bones"));
						Out.Animals.Add(D);
					}
				FSheet H;
				if (H.Load(TEXT("AnimalHerds.csv")))
					for (int32 r = 0; r < H.Rows.Num(); ++r)
					{
						FHerdDef D;
						const FString Key = H.S(r, TEXT("Key"));
						if (Key.IsEmpty()) continue;
						D.Key = FName(*Key);
						const FName Animal(*H.S(r, TEXT("Animal")));
						D.Animal = Out.Animals.IndexOfByPredicate([&](const FAnimalDef& X) { return X.Key == Animal; });
						if (D.Animal == INDEX_NONE) { Problem(FString::Printf(TEXT("AnimalHerds.csv, %s: no animal \"%s\" in Animals.csv"), *Key, *Animal.ToString())); continue; }
						const FString Count = H.S(r, TEXT("Count"));
						FString Lo, Hi;
						if (Count.Split(TEXT("-"), &Lo, &Hi)) { D.Min = FCString::Atoi(*Lo); D.Max = FCString::Atoi(*Hi); }
						else if (!Count.IsEmpty()) D.Min = D.Max = FCString::Atoi(*Count);
						D.Min = FMath::Clamp(D.Min, 1, 12); D.Max = FMath::Clamp(D.Max, D.Min, 12);
						D.bWild = H.S(r, TEXT("Where")).Equals(TEXT("wild"), ESearchCase::IgnoreCase);
						D.Herds = D.bWild ? FMath::Clamp(H.I(r, TEXT("Herds"), 8), 0, 60) : 1;
						D.Near = FVector2D(JeebOakHill::PosX + H.F(r, TEXT("North"), 0.f) * 100.f, JeebOakHill::PosY + H.F(r, TEXT("East"), 0.f) * 100.f);
						Out.Herds.Add(D);
					}
				UE_LOG(LogJeebFoes, Log, TEXT("Jeebsland animals: %d kinds, %d herd rows"), Out.Animals.Num(), Out.Herds.Num());
				return Out;
			}();
			return T;
		}
	}
	const TArray<FAnimalDef>& Animals() { return AllAnimals().Animals; }
	const TArray<FHerdDef>& Herds() { return AllAnimals().Herds; }

	int32 FindFoe(FName Key)
	{
		return Foes().IndexOfByPredicate([&](const FFoeDef& D) { return D.Key == Key; });
	}

	bool Matches(int32 Index, const FString& Name)
	{
		if (!Foes().IsValidIndex(Index)) return false;
		const FFoeDef& D = Foes()[Index];
		return D.Key.ToString().Equals(Name, ESearchCase::IgnoreCase) || (!D.Group.IsNone() && D.Group.ToString().Equals(Name, ESearchCase::IgnoreCase));
	}

	bool IsFoeTarget(const FString& Name)
	{
		if (Name.Equals(TEXT("PenKnight"), ESearchCase::IgnoreCase)) return true;
		for (int32 i = 0; i < Foes().Num(); ++i) if (Matches(i, Name)) return true;
		for (const FAnimalDef& A : Animals())   // (the huntable animals: "Stag", or the group "Deer")
			if (A.Key.ToString().Equals(Name, ESearchCase::IgnoreCase) || (!A.Group.IsNone() && A.Group.ToString().Equals(Name, ESearchCase::IgnoreCase))) return true;
		return false;
	}

	void RollLoot(const FFoeDef& D, FRandomStream& R, TArray<FInvSlot>& OutItems, int32& OutIngots) { RollLoot(D.Loot, R, OutItems, OutIngots); }

	void RollLoot(const TArray<FLootRoll>& Loot, FRandomStream& R, TArray<FInvSlot>& OutItems, int32& OutIngots)
	{
		OutIngots = 0;
		for (const FLootRoll& L : Loot)
		{
			if (R.FRand() >= L.Chance) continue;
			const int32 Id = L.Ids[R.RandRange(0, L.Ids.Num() - 1)];
			const int32 N = R.RandRange(L.Min, L.Max);
			if (N <= 0) continue;
			if (Id == 0) { OutIngots += N; continue; }
			if (FInvSlot* Have = OutItems.FindByPredicate([&](const FInvSlot& S) { return S.Id == Id; })) { Have->Count += N; continue; }
			OutItems.Add(FInvSlot(Id, N));
		}
	}

	namespace
	{
		struct FHuntRow { FName Table; int32 Count = 1; float P1 = 0.f, P99 = 0.f; };
		const TArray<FHuntRow>& HuntRows()
		{
			static const TArray<FHuntRow> Rows = []()
			{
				TArray<FHuntRow> Out;
				FSheet T;
				if (T.Load(TEXT("HuntDrops.csv")))
					for (int32 r = 0; r < T.Rows.Num(); ++r)
					{
						FHuntRow W;
						W.Table = FName(*T.S(r, TEXT("Table")));
						if (W.Table.IsNone()) continue;
						W.Count = FMath::Max(1, T.I(r, TEXT("Count"), 1));
						W.P1 = FMath::Max(0.f, T.F(r, TEXT("Level1"), 0.f));
						W.P99 = FMath::Max(0.f, T.F(r, TEXT("Level99"), 0.f));
						Out.Add(W);
					}
				return Out;
			}();
			return Rows;
		}
	}

	int32 HuntCount(FName Table, int32 HuntingLevel, FRandomStream& R)
	{
		const float B = FMath::Clamp((HuntingLevel - 1) / 98.f, 0.f, 1.f);
		float Total = 0.f;
		for (const FHuntRow& W : HuntRows()) if (W.Table == Table) Total += FMath::Lerp(W.P1, W.P99, B);
		if (Total <= 0.f) return 1;
		float Pick = R.FRand() * Total;
		for (const FHuntRow& W : HuntRows())
		{
			if (W.Table != Table) continue;
			Pick -= FMath::Lerp(W.P1, W.P99, B);
			if (Pick <= 0.f) return W.Count;
		}
		return 1;
	}

	bool InTown(float X, float Y, float Margin)
	{
		if (JeebOakHill::InBox(X, Y, Margin)) return true;   // Oak Hill (its whole box: plots, roads, the fringe farms)
		const float Sq = JeebTerrain::GroundHalf + Margin;   // the original square round the start: the old village site, the market, the pen, the meadow
		if (FMath::Abs(X) < Sq && FMath::Abs(Y) < Sq) return true;
		for (const JeebWorld::FTown& T : JeebWorld::Towns())   // every other town on the world map (Steele Creek, Silver Point ...)
		{
			if (FCString::Stricmp(T.Name, TEXT("Oak Hill")) == 0) continue;
			const float R = FMath::Max(T.Flat, 10000.f) + Margin;
			if (FVector2D::DistSquared(FVector2D(X, Y), T.Pos) < R * R) return true;
		}
		return false;
	}

	bool DeepWater(float X, float Y)
	{
		float S;
		if (!JeebWater::SurfaceAt(X, Y, S)) return false;
		return S - Ground(X, Y) > 50.f;
	}

	bool FillKind(FName Kind, FString& OutMesh, FVector& OutSize, bool& bSolid)
	{
		struct FKindRow { FString Mesh; FVector Size; bool bSolid; };
		static const TMap<FName, FKindRow> Rows = []()
		{
			TMap<FName, FKindRow> M;
			FSheet T;
			if (!T.Load(TEXT("FillKinds.csv"))) return M;
			for (int32 r = 0; r < T.Rows.Num(); ++r)
			{
				FKindRow K;
				K.Mesh = T.S(r, TEXT("Mesh"));
				if (!K.Mesh.IsEmpty() && !K.Mesh.Contains(TEXT("."))) K.Mesh += TEXT(".") + FPaths::GetBaseFilename(K.Mesh);
				K.Size = FVector(T.F(r, TEXT("FootX"), 1.f), T.F(r, TEXT("FootY"), 1.f), T.F(r, TEXT("Height"), 1.f)) * 100.f;
				const FString C = T.S(r, TEXT("Collision")).ToLower();
				K.bSolid = !C.IsEmpty() && C != TEXT("none");
				M.Add(FName(*T.S(r, TEXT("Kind"))), K);
			}
			return M;
		}();
		const FKindRow* K = Rows.Find(Kind);
		if (!K) return false;
		OutMesh = K->Mesh; OutSize = K->Size; bSolid = K->bSolid;
		return true;
	}

	float Ground(float X, float Y)
	{
		return JeebTerrain::DrawnHeight(X, Y);
	}
}
