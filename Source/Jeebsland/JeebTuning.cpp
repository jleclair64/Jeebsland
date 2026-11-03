#include "JeebTuning.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogJeebTune, Log, All);

namespace JeebTune
{
	namespace
	{
		bool bLoaded = false;
		int32 GVersion = 0;
		FDateTime GStamp;
		double GNextCheck = 0.0;

		FString FilePath() { return FPaths::ProjectContentDir() / TEXT("Data") / TEXT("Tuning.ini"); }

		// Each number by its section and name, pointing into the values (Storage)
		struct FEntry { const TCHAR* Section; const TCHAR* Name; float* Value; float Default; };
		TArray<FEntry>& Entries()
		{
			static TArray<FEntry> E;
			if (E.Num() == 0)
			{
#define JEEB_TUNE(Section, Name, Default) E.Add({ TEXT(#Section), TEXT(#Name), &Storage().Name, Default });
#include "JeebTuningList.h"
#undef JEEB_TUNE
			}
			return E;
		}

		// The file: [Section] lines, then "Name = number" lines; anything after ; or # is a comment
		void Load()
		{
			bLoaded = true;
			++GVersion;
			const FString Path = FilePath();
			GStamp = IFileManager::Get().GetTimeStamp(*Path);
			TArray<FEntry>& E = Entries();
			for (FEntry& En : E) *En.Value = En.Default;   // (a line taken out goes back to its fallback)
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				UE_LOG(LogJeebTune, Error, TEXT("Jeebsland tuning: cannot read %s (every number keeps its fallback)"), *Path);
				return;
			}
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines);
			TSet<int32> Found;
			FString Section;
			for (int32 n = 0; n < Lines.Num(); ++n)
			{
				FString L = Lines[n];
				int32 Cut;
				if (L.FindChar(TEXT(';'), Cut)) L.LeftInline(Cut);
				if (L.FindChar(TEXT('#'), Cut)) L.LeftInline(Cut);
				L.TrimStartAndEndInline();
				if (L.IsEmpty()) continue;
				if (L.StartsWith(TEXT("[")) && L.EndsWith(TEXT("]"))) { Section = L.Mid(1, L.Len() - 2).TrimStartAndEnd(); continue; }
				FString Key, Val;
				if (!L.Split(TEXT("="), &Key, &Val)) { UE_LOG(LogJeebTune, Error, TEXT("Jeebsland tuning: line %d is not \"Name = number\": %s"), n + 1, *Lines[n]); continue; }
				Key.TrimStartAndEndInline(); Val.TrimStartAndEndInline();
				const int32 Idx = E.IndexOfByPredicate([&](const FEntry& En) { return Key.Equals(En.Name, ESearchCase::IgnoreCase) && Section.Equals(En.Section, ESearchCase::IgnoreCase); });
				if (Idx == INDEX_NONE) { UE_LOG(LogJeebTune, Error, TEXT("Jeebsland tuning: line %d, [%s] %s is not a tuning number (a typo?)"), n + 1, *Section, *Key); continue; }
				if (!Val.IsNumeric()) { UE_LOG(LogJeebTune, Error, TEXT("Jeebsland tuning: line %d, %s = \"%s\" is not a number (keeps %g)"), n + 1, *Key, *Val, E[Idx].Default); Found.Add(Idx); continue; }
				float Num = FCString::Atof(*Val);
				if (Num < 0.001f)   // (no tuning number is negative, and several are divided by: 0 would break them)
				{
					UE_LOG(LogJeebTune, Error, TEXT("Jeebsland tuning: line %d, %s = %s is too small (uses 0.001)"), n + 1, *Key, *Val);
					Num = 0.001f;
				}
				*E[Idx].Value = Num;
				Found.Add(Idx);
			}
			for (int32 i = 0; i < E.Num(); ++i)
				if (!Found.Contains(i)) UE_LOG(LogJeebTune, Error, TEXT("Jeebsland tuning: no line for [%s] %s (keeps %g)"), E[i].Section, E[i].Name, E[i].Default);
			UE_LOG(LogJeebTune, Log, TEXT("Jeebsland tuning: read %d numbers from %s"), Found.Num(), *Path);
		}
	}

	FValues& Storage()
	{
		static FValues S;   // (inside a function: safe to bind to from any file's statics, whatever order they start in)
		return S;
	}

	const FValues& V()
	{
		if (!bLoaded) Load();
		return Storage();
	}

	void Poll()
	{
		const double Now = FPlatformTime::Seconds();
		if (Now < GNextCheck) return;
		GNextCheck = Now + 0.5;
		if (!bLoaded || IFileManager::Get().GetTimeStamp(*FilePath()) != GStamp) Load();
	}

	int32 Version()
	{
		V();
		return GVersion;
	}
}
