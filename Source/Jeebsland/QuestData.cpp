#include "QuestData.h"
#include "Inventory.h"
#include "OakHill.h"
#include "Terrain.h"
#include "FoeData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogJeebQuests, Log, All);

namespace JeebQuests
{
	namespace
	{
		void Problem(const FString& What) { UE_LOG(LogJeebQuests, Error, TEXT("Jeebsland quests: %s"), *What); }

		// One CSV file (the same rules as the item tables, JeebItemData.cpp): a header row naming the columns, rows starting with #
		// are notes, "quoted" cells may hold commas ("" is one quote mark inside them)
		struct FSheet
		{
			FString File;
			TMap<FString, int32> Cols;
			TArray<TArray<FString>> Rows;
			bool Load(const TCHAR* Name)
			{
				File = Name;
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

		TArray<FString> Lines(const FString& S)
		{
			TArray<FString> Out;
			S.ParseIntoArray(Out, TEXT("|"), true);
			for (FString& L : Out) L.TrimStartAndEndInline();
			Out.RemoveAll([](const FString& L) { return L.IsEmpty(); });
			return Out;
		}

		bool SkillFrom(FString Name, ESkillType& Out)
		{
			Name.TrimStartAndEndInline();
			if (Name.Equals(TEXT("Defence"), ESearchCase::IgnoreCase)) Name = TEXT("Defense");
			for (int32 s = 0; s < USkillComponent::NumSkills; ++s)
				if (USkillComponent::SkillName(static_cast<ESkillType>(s)).Equals(Name, ESearchCase::IgnoreCase)) { Out = static_cast<ESkillType>(s); return true; }
			return false;
		}

		// An item named in a sheet: its key in Materials.csv (TinOre), or the name players see (Tin Ore, Healing Potion, Iron Sword), or its number
		int32 ItemFrom(FString Name)
		{
			using namespace JeebItems;
			Name.TrimStartAndEndInline();
			if (Name.IsEmpty()) return 0;
			if (Name.IsNumeric()) return FCString::Atoi(*Name);
			static const TCHAR* const MaterialKeys[] = { TEXT("CopperOre"), TEXT("Coal"), TEXT("Logs"), TEXT("Diamond"), TEXT("Bacon"), TEXT("PineLogs"), TEXT("RedwoodLogs"),
				TEXT("Perch"), TEXT("Trout"), TEXT("GoldenCarp"), TEXT("Hide"), TEXT("Bones"), TEXT("TinOre"), TEXT("IronOre"), TEXT("SilverOre"), TEXT("GoldOre"), TEXT("Onyx"), TEXT("Emerald"), TEXT("Venison"), TEXT("Antlers") };
			static_assert(UE_ARRAY_COUNT(MaterialKeys) == int32(EItemType::Antlers) + 1, "MaterialKeys must list every EItemType, in order (JeebItemData.cpp)");
			for (int32 m = 0; m < UE_ARRAY_COUNT(MaterialKeys); ++m) if (Name.Equals(MaterialKeys[m], ESearchCase::IgnoreCase)) return MaterialId(static_cast<EItemType>(m));
			TArray<int32> Ids;
			for (int32 m = 0; m < UE_ARRAY_COUNT(MaterialKeys); ++m) Ids.Add(MaterialId(static_cast<EItemType>(m)));
			for (int32 p = 0; p < NumPotions(); ++p) Ids.Add(PotionId(p));
			for (int32 a = 0; a < NumArrowKinds(); ++a) Ids.Add(ArrowKindId(a));
			for (int32 b = 0; b < JeebBroom::NumBrooms(); ++b) Ids.Add(BroomId(b));   // (flying brooms: "Hazel's Old Broom")
			for (int32 k = 0; k < K_Count; ++k)
				for (int32 t = 0; t < NumTiers; ++t)
				{
					if (IsRetiredKind(k)) continue;
					if (k == K_Shield) { for (int32 s = 0; s < NumShieldStyles; ++s) Ids.Add(ShieldId(s, t)); }
					else if (IsArmorKind(k)) { for (int32 s = 0; s < NumArmourStyles; ++s) Ids.Add(ArmourId(k, s, t)); }
					else Ids.Add(GearId(k, t));
				}
			for (int32 Id : Ids) if (ItemName(Id).Equals(Name, ESearchCase::IgnoreCase)) return Id;
			return -1;
		}

		FVector2D Plaza() { return FVector2D(JeebOakHill::PosX, JeebOakHill::PosY); }
		// "N E": metres north and east of Oak Hill's plaza
		bool PlaceFrom(const FString& S, FVector2D& Out)
		{
			TArray<FString> P;
			S.ParseIntoArrayWS(P);
			if (P.Num() != 2 || !P[0].IsNumeric() || !P[1].IsNumeric()) return false;
			Out = Plaza() + FVector2D(FCString::Atof(*P[0]) * 100.f, FCString::Atof(*P[1]) * 100.f);
			return true;
		}

		struct FAll
		{
			TArray<FQuestDef> Quests;
			TArray<FPersonDef> People;
		};

		void LoadPeople(FAll& A)
		{
			FSheet T;
			if (!T.Load(TEXT("QuestsPeople.csv"))) return;
			for (int32 r = 0; r < T.Rows.Num(); ++r)
			{
				FPersonDef P;
				P.Key = FName(*T.S(r, TEXT("Key")));
				P.Name = T.S(r, TEXT("Name"));
				if (P.Key.IsNone() || P.Name.IsEmpty()) { Problem(FString::Printf(TEXT("QuestsPeople.csv row %d has no Key or Name"), r + 2)); continue; }
				FVector2D Anchor = Plaza();
				const FString AnchorName = T.S(r, TEXT("Anchor")).ToLower();
				if (AnchorName == TEXT("smith") || AnchorName == TEXT("store") || AnchorName == TEXT("bank"))
				{
					const JeebOakHill::EShop Shop = AnchorName == TEXT("smith") ? JeebOakHill::EShop::Smith : AnchorName == TEXT("bank") ? JeebOakHill::EShop::Bank : JeebOakHill::EShop::Store;
					float Best = 1e12f;
					for (const JeebOakHill::FCounter& C : JeebOakHill::Counters())
						if (C.Shop == Shop && FVector2D::DistSquared(C.At, Plaza()) < Best) { Best = FVector2D::DistSquared(C.At, Plaza()); Anchor = C.At; }
					if (Best > 1e11f) Problem(FString::Printf(TEXT("QuestsPeople.csv: no %s counter for %s; measured from the plaza"), *AnchorName, *P.Name));
				}
				P.Pos = Anchor + FVector2D(T.F(r, TEXT("North"), 0.f) * 100.f, T.F(r, TEXT("East"), 0.f) * 100.f);
				const FString Face = T.S(r, TEXT("Facing"));
				const FVector2D ToOak = Plaza() - P.Pos;
				P.Yaw = (Face.IsEmpty() || Face.Equals(TEXT("oak"), ESearchCase::IgnoreCase)) ? FMath::RadiansToDegrees(FMath::Atan2(ToOak.Y, ToOak.X)) : FCString::Atof(*Face);
				P.Tunic = T.Color(r, TEXT("Tunic"), P.Tunic);
				P.Hair = T.Color(r, TEXT("Hair"), P.Hair);
				const FString Hat = T.S(r, TEXT("Hat")).ToLower();
				P.Hat = Hat == TEXT("herbalist") ? 0 : Hat == TEXT("cap") ? 3 : Hat == TEXT("baker") ? 4 : Hat == TEXT("cowl") ? 7 : Hat == TEXT("feather") ? 8 : -1;
				const FString Apron = T.S(r, TEXT("Apron")).ToLower();
				P.bApron = Apron == TEXT("yes") || Apron == TEXT("true") || Apron == TEXT("1");
				P.Scale = FMath::Clamp(T.F(r, TEXT("Scale"), 1.f), 0.6f, 1.2f);
				P.Greeting = Lines(T.S(r, TEXT("Greeting")));
				A.People.Add(P);
			}
		}

		void LoadQuests(FAll& A)
		{
			FSheet T;
			if (!T.Load(TEXT("Quests.csv"))) return;
			for (int32 r = 0; r < T.Rows.Num(); ++r)
			{
				FQuestDef Q;
				Q.Key = FName(*T.S(r, TEXT("Key")));
				Q.Title = T.S(r, TEXT("Title"));
				Q.Giver = FName(*T.S(r, TEXT("Giver")));
				if (Q.Key.IsNone() || Q.Title.IsEmpty()) { Problem(FString::Printf(TEXT("Quests.csv row %d has no Key or Title"), r + 2)); continue; }
				if (!A.People.ContainsByPredicate([&](const FPersonDef& P) { return P.Key == Q.Giver; })) { Problem(FString::Printf(TEXT("quest %s: no person \"%s\" in QuestsPeople.csv; left out"), *Q.Key.ToString(), *Q.Giver.ToString())); continue; }
				Q.Summary = T.S(r, TEXT("Summary"));
				Q.Offer = Lines(T.S(r, TEXT("Offer")));
				Q.Reminder = Lines(T.S(r, TEXT("Reminder")));
				Q.NotYet = Lines(T.S(r, TEXT("NotYet")));
				Q.Done = Lines(T.S(r, TEXT("Done")));
				TArray<FString> Parts;
				T.S(r, TEXT("Needs")).ParseIntoArray(Parts, TEXT(";"), true);
				for (FString P : Parts)
				{
					P.TrimStartAndEndInline();
					FNeed N;
					if (P.StartsWith(TEXT("Quest "), ESearchCase::IgnoreCase)) { N.Quest = FName(*P.Mid(6).TrimStartAndEnd()); Q.Needs.Add(N); continue; }
					FString SkillS, LevelS;
					if (P.Split(TEXT(" "), &SkillS, &LevelS, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && SkillFrom(SkillS, N.Skill) && LevelS.IsNumeric())
					{
						N.Level = FMath::Clamp(FCString::Atoi(*LevelS), 1, USkillComponent::MaxLevel);
						Q.Needs.Add(N);
					}
					else Problem(FString::Printf(TEXT("quest %s: cannot read the need \"%s\""), *Q.Key.ToString(), *P));
				}
				Parts.Reset();
				T.S(r, TEXT("RewardXP")).ParseIntoArray(Parts, TEXT(";"), true);
				for (FString P : Parts)
				{
					P.TrimStartAndEndInline();
					FString SkillS, XPS;
					ESkillType Sk;
					if (P.Split(TEXT(" "), &SkillS, &XPS, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && SkillFrom(SkillS, Sk) && XPS.IsNumeric())
						Q.RewardXP.Add(TPair<ESkillType, int32>(Sk, FMath::Clamp(FCString::Atoi(*XPS), 0, 1000000)));
					else Problem(FString::Printf(TEXT("quest %s: cannot read the XP reward \"%s\""), *Q.Key.ToString(), *P));
				}
				Q.RewardIngots = FMath::Max(0, T.I(r, TEXT("RewardIngots"), 0));
				Parts.Reset();
				T.S(r, TEXT("RewardItems")).ParseIntoArray(Parts, TEXT(";"), true);
				for (FString P : Parts)
				{
					P.TrimStartAndEndInline();
					int32 Count = 1;
					FString NameS, CountS;
					if (P.Split(TEXT(" x"), &NameS, &CountS, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && CountS.IsNumeric()) { P = NameS; Count = FMath::Clamp(FCString::Atoi(*CountS), 1, 999); }
					const int32 Id = ItemFrom(P);
					if (Id > 0 && !JeebItems::ItemName(Id).IsEmpty()) Q.RewardItems.Add(FInvSlot(Id, Count));
					else Problem(FString::Printf(TEXT("quest %s: no item called \"%s\""), *Q.Key.ToString(), *P));
				}
				A.Quests.Add(Q);
			}
			for (const FQuestDef& Q : A.Quests)
				for (const FNeed& N : Q.Needs)
					if (!N.Quest.IsNone() && !A.Quests.ContainsByPredicate([&](const FQuestDef& O) { return O.Key == N.Quest; }))
						Problem(FString::Printf(TEXT("quest %s needs the quest \"%s\", which isn't in Quests.csv (it can never be started)"), *Q.Key.ToString(), *N.Quest.ToString()));
		}

		void LoadSteps(FAll& A)
		{
			FSheet T;
			if (!T.Load(TEXT("QuestsSteps.csv"))) return;
			for (int32 r = 0; r < T.Rows.Num(); ++r)
			{
				const FName QKey(*T.S(r, TEXT("Quest")));
				FQuestDef* Q = A.Quests.FindByPredicate([&](const FQuestDef& D) { return D.Key == QKey; });
				if (!Q) { Problem(FString::Printf(TEXT("QuestsSteps.csv row %d: no quest \"%s\""), r + 2, *QKey.ToString())); continue; }
				FStepDef S;
				const FString Type = T.S(r, TEXT("Type")).ToLower();
				S.Type = Type == TEXT("bring") ? EStep::Bring : Type == TEXT("kill") ? EStep::Kill : Type == TEXT("visit") ? EStep::Visit : EStep::Talk;
				if (Type != TEXT("talk") && Type != TEXT("bring") && Type != TEXT("kill") && Type != TEXT("visit")) Problem(FString::Printf(TEXT("QuestsSteps.csv row %d: unknown type \"%s\" (talk, bring, kill or visit); read as talk"), r + 2, *Type));
				S.At = FName(*T.S(r, TEXT("At")));
				S.Target = T.S(r, TEXT("Target"));
				S.Count = FMath::Clamp(T.I(r, TEXT("Count"), 1), 1, 999);
				S.Radius = FMath::Clamp(T.F(r, TEXT("Radius"), 12.f), 2.f, 200.f) * 100.f;
				S.Mark = T.S(r, TEXT("Mark"));
				S.Log = T.S(r, TEXT("Log"));
				S.Lines = Lines(T.S(r, TEXT("Lines")));
				S.Waiting = Lines(T.S(r, TEXT("Waiting")));
				bool bOk = true;
				const bool bNeedsPerson = S.Type == EStep::Talk || S.Type == EStep::Bring;
				if (bNeedsPerson && !A.People.ContainsByPredicate([&](const FPersonDef& P) { return P.Key == S.At; })) { Problem(FString::Printf(TEXT("QuestsSteps.csv row %d: no person \"%s\""), r + 2, *S.At.ToString())); bOk = false; }
				if (S.Type == EStep::Bring)
				{
					S.ItemId = ItemFrom(S.Target);
					if (S.ItemId <= 0 || JeebItems::ItemName(S.ItemId).IsEmpty()) { Problem(FString::Printf(TEXT("QuestsSteps.csv row %d: no item called \"%s\""), r + 2, *S.Target)); bOk = false; }
				}
				if (S.Type == EStep::Visit)
				{
					S.bRuin = S.Target.Equals(TEXT("Ruin"), ESearchCase::IgnoreCase);
					if (!S.bRuin && !PlaceFrom(S.Target, S.Place)) { Problem(FString::Printf(TEXT("QuestsSteps.csv row %d: visit \"%s\" is neither Ruin nor \"north east\" metres"), r + 2, *S.Target)); bOk = false; }
				}
				if (S.Type == EStep::Kill && !JeebFoes::IsFoeTarget(S.Target)) Problem(FString::Printf(TEXT("QuestsSteps.csv row %d: kill \"%s\": not PenKnight, nor a Key or Group in Foes.csv"), r + 2, *S.Target));
				if (bOk) Q->Steps.Add(S);
				else Q->Steps.Add(FStepDef());   // (a broken step still holds its place, so the step numbers in saves keep their meaning; it reads as a talk to nobody)
			}
			A.Quests.RemoveAll([](const FQuestDef& Q) { if (Q.Steps.Num() == 0) { Problem(FString::Printf(TEXT("quest %s has no steps; left out"), *Q.Key.ToString())); return true; } return false; });
		}

		const FAll& All()
		{
			static const FAll A = []()
			{
				FAll Out;
				LoadPeople(Out);
				LoadQuests(Out);
				LoadSteps(Out);
				int32 Steps = 0;
				for (const FQuestDef& Q : Out.Quests) Steps += Q.Steps.Num();
				UE_LOG(LogJeebQuests, Log, TEXT("JEEBQUEST data: %d quests, %d steps, %d people"), Out.Quests.Num(), Steps, Out.People.Num());
				return Out;
			}();
			return A;
		}
	}

	const TArray<FQuestDef>& Quests() { return All().Quests; }
	const TArray<FPersonDef>& People() { return All().People; }

	const FQuestDef* FindQuest(FName Key)
	{
		for (const FQuestDef& Q : Quests()) if (Q.Key == Key) return &Q;
		return nullptr;
	}

	const FPersonDef* FindPerson(FName Key)
	{
		for (const FPersonDef& P : People()) if (P.Key == Key) return &P;
		return nullptr;
	}

	FString PersonName(FName Key)
	{
		const FPersonDef* P = FindPerson(Key);
		if (!P) return Key.ToString();
		FString Short, Rest;
		if (!P->Name.Split(TEXT(","), &Short, &Rest)) Short = P->Name;   // "Marta Fernside, Steward" -> "Marta Fernside"
		return Short.TrimStartAndEnd();
	}

	FString NeedText(const FNeed& N)
	{
		if (!N.Quest.IsNone())
		{
			const FQuestDef* Q = FindQuest(N.Quest);
			return FString::Printf(TEXT("Finish \"%s\""), Q ? *Q->Title : *N.Quest.ToString());
		}
		return FString::Printf(TEXT("%s level %d"), *USkillComponent::SkillName(N.Skill).Replace(TEXT("Defense"), TEXT("Defence")), N.Level);
	}

	FString RewardText(const FQuestDef& Q)
	{
		TArray<FString> Parts;
		for (const TPair<ESkillType, int32>& X : Q.RewardXP) Parts.Add(FString::Printf(TEXT("%d %s XP"), X.Value, *USkillComponent::SkillName(X.Key).Replace(TEXT("Defense"), TEXT("Defence"))));
		if (Q.RewardIngots > 0) Parts.Add(FString::Printf(TEXT("%d ingots"), Q.RewardIngots));
		for (const FInvSlot& I : Q.RewardItems) Parts.Add(I.Count > 1 ? FString::Printf(TEXT("%d %s"), I.Count, *JeebItems::ItemName(I.Id)) : JeebItems::ItemName(I.Id));
		return FString::Join(Parts, TEXT(", "));
	}

	int32 ItemFromName(const FString& Name) { return ItemFrom(Name); }

	bool MarkSpot(const FString& Mark, FVector2D& Out)
	{
		const FString M = Mark.TrimStartAndEnd().ToLower();
		if (M.IsEmpty()) return false;
		if (const FPersonDef* P = FindPerson(FName(*Mark.TrimStartAndEnd()))) { Out = P->Pos; return true; }
		if (M == TEXT("pen")) { Out = JeebTerrain::PenCenter(); return true; }
		if (M == TEXT("pond")) { Out = JeebTerrain::PondCenter(); return true; }
		if (M == TEXT("rocks")) { Out = FVector2D(900.f, -100.f); return true; }   // (the tin rocks by the old starting point, east of the old village site: KnightGameState.cpp)
		return PlaceFrom(Mark, Out);
	}
}
