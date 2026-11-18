#pragma once

#include "CoreMinimal.h"

// Emotes (2026-11-18): short Mixamo clips the knight plays for everyone to see. Typed in chat (/wave, /bow ...) or picked from the
// emote wheel (B). The player's machine asks (AKnightCharacter::ServerEmote), the server checks and replicates the emote (RepEmote),
// and every machine plays it (KnightCharacterEmote.cpp). Moving, jumping, attacking, drawing the bow, the shield, a blow or a roll
// ends it. The clips and their cut points are rows Emote_<Name> in Content/Data/KnightMoves.csv; the animator plays them as keys
// 100 + id (KnightAnimatorEmote.cpp). Ids are sent over the network: add new emotes at the end, never reorder.
namespace JeebEmotes
{
	struct FDef
	{
		const TCHAR* Name;     // the chat command (/wave) and the KnightMoves.csv row (Emote_Wave)
		const TCHAR* Row;
		const TCHAR* Label;    // on the wheel
		int32 Loops;           // how many times the row's ClipStart..ClipEnd plays (0 = until something ends it)
		bool bStow;            // the weapon and the shield go on his back meanwhile (both hands are busy), and stay there through the blend back to standing
	};

	inline const FDef* Defs(int32& OutNum)
	{
		static const FDef D[] = {
			{ nullptr, nullptr, nullptr, 0, false },                          // 0: none
			{ TEXT("wave"),   TEXT("Emote_Wave"),   TEXT("Wave"),   4, true },
			{ TEXT("bow"),    TEXT("Emote_Bow"),    TEXT("Bow"),    1, true },    // (stowed since 2026-11-18: held, the sword lay across his thighs as he bent)
			{ TEXT("cheer"),  TEXT("Emote_Cheer"),  TEXT("Cheer"),  1, true },
			{ TEXT("clap"),   TEXT("Emote_Clap"),   TEXT("Clap"),   3, true },
			{ TEXT("dance"),  TEXT("Emote_Dance"),  TEXT("Dance"),  0, true },
			{ TEXT("sit"),    TEXT("Emote_Sit"),    TEXT("Sit"),    0, true },
			{ TEXT("point"),  TEXT("Emote_Point"),  TEXT("Point"),  1, true },
			{ TEXT("salute"), TEXT("Emote_Salute"), TEXT("Salute"), 1, true },
		};
		OutNum = UE_ARRAY_COUNT(D);
		return D;
	}

	inline int32 Num() { int32 N = 0; Defs(N); return N; }   // (including 0, none)
	inline const FDef* Get(int32 Id) { int32 N = 0; const FDef* D = Defs(N); return Id > 0 && Id < N ? &D[Id] : nullptr; }
	inline int32 FindByName(const FString& Name)
	{
		int32 N = 0;
		const FDef* D = Defs(N);
		for (int32 i = 1; i < N; ++i) if (Name.Equals(D[i].Name, ESearchCase::IgnoreCase)) return i;
		return 0;
	}
	inline FString CommandList()
	{
		int32 N = 0;
		const FDef* D = Defs(N);
		FString S;
		for (int32 i = 1; i < N; ++i) S += (i > 1 ? TEXT(" /") : TEXT("/")) + FString(D[i].Name);
		return S;
	}
}
