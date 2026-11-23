#include "JeebSpells.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	TArray<JeebSpells::FSpell> Load()
	{
		using namespace JeebSpells;
		TArray<FSpell> Out;
		FString Text;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data") / TEXT("Spells.csv");
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines);
			TMap<FString, int32> Cols;
			for (const FString& Line0 : Lines)
			{
				const FString Line = Line0.TrimStartAndEnd();
				if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) continue;
				TArray<FString> C;
				Line.ParseIntoArray(C, TEXT(","), false);
				for (FString& S : C) S.TrimStartAndEndInline();
				if (Cols.Num() == 0) { for (int32 i = 0; i < C.Num(); ++i) Cols.Add(C[i].ToLower(), i); continue; }
				auto Str = [&](const TCHAR* N) { const int32* I = Cols.Find(N); return (I && C.IsValidIndex(*I)) ? C[*I] : FString(); };
				auto Num = [&](const TCHAR* N, float Def) { const FString S = Str(N); return S.IsEmpty() ? Def : FCString::Atof(*S); };
				FSpell Sp;
				Sp.Key = Str(TEXT("key"));
				Sp.Name = Str(TEXT("name"));
				Sp.Blurb = Str(TEXT("blurb"));
				TArray<FString> RGB;
				Str(TEXT("colour")).ParseIntoArrayWS(RGB);
				if (RGB.Num() >= 3) Sp.Color = FLinearColor(FCString::Atof(*RGB[0]), FCString::Atof(*RGB[1]), FCString::Atof(*RGB[2]));
				Sp.DamageMul = FMath::Max(0.05f, Num(TEXT("damagemul"), 1.f));
				Sp.SpeedMul = FMath::Clamp(Num(TEXT("speedmul"), 1.f), 0.1f, 5.f);
				Sp.CostMul = FMath::Max(0.f, Num(TEXT("costmul"), 1.f));
				Sp.Size = FMath::Clamp(Num(TEXT("size"), 1.f), 0.3f, 3.f);
				Sp.LevelNeeded = FMath::Clamp(int32(Num(TEXT("levelneeded"), 1.f)), 1, 99);
				Sp.MaxLevel = FMath::Clamp(int32(Num(TEXT("maxlevel"), 5.f)), 1, 20);
				Sp.PerLevel = FMath::Max(0.f, Num(TEXT("perlevel"), 0.1f));
				Sp.Look = Str(TEXT("look")).Equals(TEXT("fire"), ESearchCase::IgnoreCase) ? Look_Fire : Look_Bolt;
				if (Sp.Name.IsEmpty()) Sp.Name = Sp.Key;
				Out.Add(Sp);
			}
		}
		if (Out.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Jeebsland spells: cannot read %s; using the built-in blue bolt"), *Path);
			FSpell B; B.Key = TEXT("arcane_bolt"); B.Name = TEXT("Arcane Bolt");
			Out.Add(B);
		}
		else UE_LOG(LogTemp, Log, TEXT("Jeebsland spells: read %d spells from Content/Data/Spells.csv"), Out.Num());
		return Out;
	}
}

const TArray<JeebSpells::FSpell>& JeebSpells::All()
{
	static const TArray<FSpell> Table = Load();
	return Table;
}

int32 JeebSpells::Num() { return All().Num(); }

const JeebSpells::FSpell& JeebSpells::Get(int32 Row)
{
	const TArray<FSpell>& T = All();
	return T.IsValidIndex(Row) ? T[Row] : T[0];
}

float JeebSpells::DamageMul(int32 Row, int32 Level)
{
	const FSpell& S = Get(Row);
	return S.DamageMul * (1.f + S.PerLevel * FMath::Clamp(Level - 1, 0, S.MaxLevel - 1));
}
