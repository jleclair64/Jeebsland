#include "JeebChat.h"
#include "JeebAccounts.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace JeebChat
{
	FString Clean(const FString& In)
	{
		FString Out;
		bool bSpace = true;   // (drops spaces at the start)
		for (TCHAR C : In)
		{
			if (C < 32 || C == 127) continue;
			if (C == ' ') { if (bSpace) continue; bSpace = true; }
			else bSpace = false;
			Out.AppendChar(C);
		}
		Out.TrimEndInline();
		if (Out.Len() > MaxLength) Out.LeftInline(MaxLength);
		return Out;
	}

	// Words that are hidden wherever they appear (long enough that they hardly occur inside innocent words) ...
	static const TCHAR* const Inside[] = {
		TEXT("fuck"), TEXT("fuk"), TEXT("shit"), TEXT("bitch"), TEXT("bastard"), TEXT("asshole"), TEXT("cunt"), TEXT("pussy"),
		TEXT("whore"), TEXT("slut"), TEXT("nigger"), TEXT("nigga"), TEXT("faggot"), TEXT("retard"), TEXT("dickhead"), TEXT("wanker") };
	// ... and short ones that are hidden only as a whole word (so "grape", "class" and "cockatoo" are fine)
	static const TCHAR* const Whole[] = {
		TEXT("ass"), TEXT("dick"), TEXT("cock"), TEXT("fag"), TEXT("rape"), TEXT("raped"), TEXT("porn"), TEXT("piss"), TEXT("twat"), TEXT("wank"), TEXT("cum") };

	FString Censor(const FString& In)
	{
		// A lower-case copy with the common letter swaps undone, same length as the original so positions line up
		FString Norm = In.ToLower();
		for (int32 i = 0; i < Norm.Len(); ++i)
		{
			switch (Norm[i])
			{
			case '0': Norm[i] = 'o'; break;
			case '1': case '!': case '|': Norm[i] = 'i'; break;
			case '3': Norm[i] = 'e'; break;
			case '4': case '@': Norm[i] = 'a'; break;
			case '5': case '$': Norm[i] = 's'; break;
			case '7': Norm[i] = 't'; break;
			default: break;
			}
		}
		FString Out = In;
		auto IsLetter = [&](int32 i) { return i >= 0 && i < Norm.Len() && FChar::IsAlpha(Norm[i]); };
		auto Hide = [&](const TCHAR* Word, bool bWhole)
		{
			const int32 L = FCString::Strlen(Word);
			int32 From = 0;
			while (true)
			{
				const int32 At = Norm.Find(Word, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				if (At == INDEX_NONE) break;
				From = At + 1;
				if (bWhole && (IsLetter(At - 1) || IsLetter(At + L))) continue;
				for (int32 k = At; k < At + L; ++k) Out[k] = '*';
			}
		};
		for (const TCHAR* W : Inside) Hide(W, false);
		for (const TCHAR* W : Whole) Hide(W, true);

		// The same again with every gap closed up, to catch "f u c k" and "s.h.i.t" (only the long words, to keep it safe)
		FString Tight;
		TArray<int32> Map;   // Tight[j] came from In[Map[j]]
		for (int32 i = 0; i < Norm.Len(); ++i) if (FChar::IsAlpha(Norm[i])) { Tight.AppendChar(Norm[i]); Map.Add(i); }
		for (const TCHAR* W : Inside)
		{
			const int32 L = FCString::Strlen(W);
			int32 From = 0;
			while (true)
			{
				const int32 At = Tight.Find(W, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				if (At == INDEX_NONE) break;
				From = At + 1;
				if (Map[At + L - 1] - Map[At] > L * 3) continue;   // far apart: not a disguised word
				for (int32 k = At; k < At + L; ++k) Out[Map[k]] = '*';
			}
		}
		return Out;
	}

	// ---- the owner's moderation file ----
	static TSet<FString> Muted, Banned;
	static double ModLoaded = -100.0;

	static void LoadModeration()
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - ModLoaded < 5.0) return;
		ModLoaded = Now;
		Muted.Reset();
		Banned.Reset();
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("Moderation.txt")))) return;
		for (FString L : Lines)
		{
			L.TrimStartAndEndInline();
			if (L.IsEmpty() || L.StartsWith(TEXT("#"))) continue;
			FString Cmd, Name;
			if (!L.Split(TEXT(" "), &Cmd, &Name)) continue;
			const FString Key = JeebAccounts::KeyOf(Name.TrimStartAndEnd());
			if (Cmd.Equals(TEXT("mute"), ESearchCase::IgnoreCase)) Muted.Add(Key);
			else if (Cmd.Equals(TEXT("ban"), ESearchCase::IgnoreCase)) Banned.Add(Key);
		}
	}

	bool IsMuted(const FString& AccountKey) { LoadModeration(); return Muted.Contains(AccountKey); }
	bool IsBanned(const FString& AccountKey) { LoadModeration(); return Banned.Contains(AccountKey); }

	// ---- this player's own mute list ----
	static TSet<FString> Local;
	static bool bLocalLoaded = false;
	static FString LocalPath() { return FPaths::ProjectSavedDir() / TEXT("ChatMuted.txt"); }

	static void LoadLocal()
	{
		if (bLocalLoaded) return;
		bLocalLoaded = true;
		TArray<FString> Lines;
		if (FFileHelper::LoadFileToStringArray(Lines, *LocalPath()))
			for (FString L : Lines) { L.TrimStartAndEndInline(); if (!L.IsEmpty()) Local.Add(JeebAccounts::KeyOf(L)); }
	}

	bool LocalMuted(const FString& Name) { LoadLocal(); return Local.Contains(JeebAccounts::KeyOf(Name)); }

	void SetLocalMuted(const FString& Name, bool bMuted)
	{
		LoadLocal();
		if (bMuted) Local.Add(JeebAccounts::KeyOf(Name)); else Local.Remove(JeebAccounts::KeyOf(Name));
		FFileHelper::SaveStringArrayToFile(Local.Array(), *LocalPath());
	}

	TArray<FString> LocalMuteList() { LoadLocal(); return Local.Array(); }
}
