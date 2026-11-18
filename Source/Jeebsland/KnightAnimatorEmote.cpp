#include "KnightAnimator.h"
#include "JeebEmotes.h"
#include "HAL/IConsoleManager.h"

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
	if (Key == 108) SaluteToVisor(B);
	if (bRestart) StartBlend(B, M->Blend);
}

// The salute (2026-11-18, motion review): Mixamo's hand stops at his shoulder and neck, lower still in heavy armour where the pauldron
// takes the arm. While the clip's hand is up, a two-bone reach lifts it to the visor's right edge; the hand keeps the clip's own turn.
// Live: jeeb.Knight.SaluteLift (0 = the clip as it was).
static TAutoConsoleVariable<float> CVarSaluteLift(TEXT("jeeb.Knight.SaluteLift"), 1.f, TEXT("The salute's hand lifted to the visor (1), or the clip's own height (0)."), ECVF_Default);
static TAutoConsoleVariable<FString> CVarSaluteAt(TEXT("jeeb.Knight.SaluteAt"), TEXT("-7,14,6"), TEXT("Where the salute's hand goes from the head bone (cm, mesh frame: -X his right, +Y ahead, +Z up)."), ECVF_Default);
void FKnightAnimator::SaluteToVisor(FPose& B)
{
	const float Lift = FMath::Clamp(CVarSaluteLift.GetValueOnGameThread(), 0.f, 1.f);
	if (Lift <= 0.f) return;
	FVector Off(-7.f, 14.f, 6.f);
	{
		TArray<FString> P;
		CVarSaluteAt.GetValueOnGameThread().ParseIntoArray(P, TEXT(","));
		if (P.Num() == 3) Off = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
	}
	FQuat HandWas;
	const FVector Hand = BonePos(B, RHand, &HandWas);
	const FVector Shoulder = BonePos(B, RArm);
	// (how far the clip's hand is up: nothing while it hangs, all of it from about the chin)
	const float W = Lift * FMath::SmoothStep(float(Shoulder.Z) - 25.f, float(Shoulder.Z) + 5.f, float(Hand.Z));
	if (W <= 0.001f) return;
	const FVector Want = FMath::Lerp(Hand, BonePos(B, Head) + Off, W);
	L.ReachTo(B, RArm, RFore, RHand, Want, FVector(-1.f, 0.35f, -0.25f));   // (the elbow out to his right and a little forward, as a salute holds it)
	FQuat HandNow;
	BonePos(B, RHand, &HandNow);
	TurnInMesh(B, RHand, HandWas * HandNow.Inverse());
}
