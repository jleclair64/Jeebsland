#include "KnightAnimator.h"
#include "JeebEmotes.h"

using namespace JeebAnim;

// Emotes (2026-11-18, JeebEmotes.h): key 100 + the emote's id. The whole body plays the row's clip (Emote_<Name> in KnightMoves.csv)
// from ClipStart to ClipEnd, round and round for the looping ones; the game (KnightCharacterEmote.cpp) decides when it ends. Into it the
// row's Blend eases him; out of it Update eases him back (0.45 s, 0.7 s up off the ground after sitting).
void FKnightAnimator::EmotePlay(int32 Key, const FInput& In, FPose& B, float& OutBlend)
{
	const JeebEmotes::FDef* D = JeebEmotes::Get(Key - 100);
	if (!D) return;
	const FMove* M = Moves.Find(D->Row);
	if (!M) return;
	const float Len = FMath::Max(M->ClipEnd - M->ClipStart, 0.05f);
	const float T = D->Loops == 1 ? FMath::Min(In.EmoteT, Len) : FMath::Fmod(In.EmoteT, Len);
	// (a shield on his arm stays held out as he walks with it: the clips swing an empty off hand)
	if (!D->bStow && In.bShieldArm) PlayKeepOffArm = 1.f;
	const bool bRestart = In.EmoteT + 0.05f < EmotePrevT;   // (the same emote again: its own key, so no new blend unless asked for here)
	EmotePrevT = In.EmoteT;
	PlayAt(D->Row, M->ClipStart + T, 1000.f, B, &OutBlend);
	// (standing ones: the lower boot on the floor, as he stands at rest. The dance's retarget left both boots 6-7 cm up most of the time.)
	if (Key != 106)
	{
		if (EmoteRestSole < 0.f)
			if (const FClip* R = Get(TEXT("Rest"))) { Sample(*R, 0.f, Scratch, false); EmoteRestSole = FMath::Min(float(BonePos(Scratch, LFoot).Z), float(BonePos(Scratch, RFoot).Z)); }
		if (EmoteRestSole >= 0.f) B.Hip.Z += EmoteRestSole - FMath::Min(float(BonePos(B, LFoot).Z), float(BonePos(B, RFoot).Z));
	}
	if (bRestart) StartBlend(B, M->Blend);
}
