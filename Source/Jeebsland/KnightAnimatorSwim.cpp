#include "KnightAnimator.h"
#include "JeebTuning.h"

using namespace JeebAnim;

// Swimming (2026-11-13): the pose. Keys 92-95 (ChooseAction hands them over here, before anything else: nothing else plays in the water):
//   92 treading water (Mixamo's Treading Water, upright, at its own pace), 93 swimming (Mixamo's Swimming, a breaststroke: its stroke follows the
//   distance swum, so faster swimming strokes faster; ClipHit is cm per stroke, Swim at the normal pace, SwimFast at the fast one), 95 drowning
//   (the treading clip flailing fast, then slowing as he sinks).
// Tread and swim change over with some hysteresis (swim above 70 cm/s, tread after a quarter second under 40), the hips sit at a smoothed depth
// under the surface (swimming 10 cm, treading 45: his back and shoulders at the waterline), and diving or rising tips the swimming body down or up, at
// most 35 degrees (120 degrees a second, from his vertical speed); a dive with no keys swims head down too. Out of the water the body comes upright over 0.6 s (KnightAnimator.cpp, WallExitBlend).

namespace
{
	float Ramp(float A, float B, float X) { return FMath::Clamp((X - A) / FMath::Max(B - A, 0.001f), 0.f, 1.f); }
}

float FKnightAnimator::SwimMeanHip(const TCHAR* Row, float& OutY)
{
	if (const FVector2D* S = SwimHipStats.Find(Row)) { OutY = float(S->Y); return float(S->X); }
	float Z = 100.f, Y = 0.f;
	const FMove* M = Moves.Find(Row);
	const FClip* C = M ? Get(*M->Clip) : nullptr;
	if (C)
	{
		FPose P;
		P.Rot = RefRot;
		constexpr int32 N = 16;
		Z = 0.f;
		for (int32 i = 0; i < N; ++i)
		{
			Sample(*C, FMath::Lerp(M->ClipStart, M->ClipEnd, i / float(N - 1)), P, false);
			Z += P.Hip.Z; Y += P.Hip.Y;
		}
		Z /= N; Y /= N;
		UE_LOG(LogTemp, Log, TEXT("JEEBSWIMCLIP %s len %.2f hip mean z %.1f y %.1f"), Row, C->Len, Z, Y);
	}
	SwimHipStats.Add(Row, FVector2D(Z, Y));
	OutY = Y;
	return Z;
}

int32 FKnightAnimator::SwimKey(float Dt, const FInput& In)
{
	if (In.SwimMode == 7) return 95;
	const bool bFresh = !(StateKey >= 92 && StateKey <= 95);
	// diving or swimming up counts as swimming (a dive with no keys swims head down, it never sinks standing)
	const float Vz = FMath::Abs(In.SwimVz) < 50.f ? 0.f : In.SwimVz;
	const float Speed = FMath::Sqrt(FMath::Square(In.SwimSpeed) + FMath::Square(Vz));
	if (bFresh) { SwimMoveKey = Speed > 70.f ? 93 : 92; SwimSlowT = 0.f; }
	if (SwimMoveKey == 92) { if (Speed > 70.f) SwimMoveKey = 93; }
	else
	{
		SwimSlowT = Speed < 40.f ? SwimSlowT + Dt : 0.f;
		if (SwimSlowT > 0.25f) SwimMoveKey = 92;
	}
	return SwimMoveKey;
}

void FKnightAnimator::SwimPlay(int32 Key, const FInput& In, FPose& B, float& OutBlend)
{
	const JeebTune::FValues& T = JeebTune::V();
	const bool bFresh = !(StateKey >= 92 && StateKey <= 95);
	const TCHAR* Row = Key == 93 ? TEXT("Swim") : TEXT("SwimTread");
	const FMove* M = Moves.Find(Row);
	if (!M) return;
	const float Len = FMath::Max(M->ClipEnd - M->ClipStart, 0.05f);
	float Tm;
	if (Key == 93)
	{
		// one stroke per ClipHit cm, more cm per stroke the faster he goes (Swim at the normal pace, SwimFast at the fast one)
		const FMove* Fm = Moves.Find(TEXT("SwimFast"));
		const float Fast = Ramp(T.SwimSpeed, T.SwimFastSpeed, In.SwimSpeed);
		const float PerStroke = FMath::Lerp(FMath::Max(M->ClipHit, 10.f), Fm ? FMath::Max(Fm->ClipHit, 10.f) : FMath::Max(M->ClipHit, 10.f), Fast);
		const float Speed3 = FMath::Sqrt(FMath::Square(In.SwimSpeed) + FMath::Square(In.SwimVz));
		SwimPhase = FMath::Fmod(SwimPhase + FMath::Max(Speed3, 25.f) * PlayDt / PerStroke, 1.f);
		Tm = M->ClipStart + SwimPhase * Len;
	}
	else if (Key == 95)
	{
		// a short fast flail, then slower and slower as he sinks
		if (ActionT <= 0.f) SwimDrownT = 0.f;
		SwimDrownT += PlayDt * (ActionT < 0.6f ? 1.6f : FMath::Lerp(1.6f, 0.4f, Ramp(0.6f, 1.2f, ActionT)));
		Tm = M->ClipStart + FMath::Fmod(SwimDrownT, Len);
	}
	else Tm = M->ClipStart + FMath::Fmod(Time, Len);
	PlayAt(Row, Tm, 1000.f, B, &OutBlend);

	// the hips at their depth under the surface (smoothed, so bobbing at the float or a change of key never pops)
	float MeanY;
	const float MeanZ = SwimMeanHip(Row, MeanY);
	// held up to the surface only while floating: deeper than the float (diving, drowning) the hips stay with the body instead of rising out of it
	// toward the surface; eased and rate limited (150 cm/s) so a change of depth or key never jumps
	const float Want = 96.f + FMath::Min(In.SwimDepth, T.SwimFloat) - (Key == 93 ? 10.f : 45.f);
	if (bFresh || SwimHipZ < 0.f) SwimHipZ = Want;
	SwimHipZ = FMath::FInterpConstantTo(SwimHipZ, FMath::FInterpTo(SwimHipZ, Want, PlayDt, 6.f), PlayDt, 150.f);
	B.Hip.Z += SwimHipZ - MeanZ;
	B.Hip.Y -= MeanY;

	// diving tips the swimming body head down, swimming up head up
	float PitchWant = 0.f;
	if (Key == 93) PitchWant = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(-In.SwimVz, FMath::Max(In.SwimSpeed, 80.f))), -35.f, 35.f);
	if (bFresh) SwimPitch = 0.f;
	SwimPitch = FMath::FInterpConstantTo(SwimPitch, PitchWant, PlayDt, 120.f);   // from his vertical speed, never his depth, so it tips as the dive starts
	if (FMath::Abs(SwimPitch) > 0.5f) TurnInMesh(B, Hips, FQuat(FVector(1.f, 0.f, 0.f), FMath::DegreesToRadians(-SwimPitch)));
	if (In.bShotLog)
	{
		static float LogT = 0.f;
		if (Time - LogT > 0.25f || Time < LogT)
		{
			LogT = Time;
			UE_LOG(LogTemp, Log, TEXT("JEEBSWIMPOSE key %d t %.2f speed %.0f vz %.0f depth %.0f hips %.0f (want %.0f) pitch %.0f head %.0f under %d"),
				Key, Tm, In.SwimSpeed, In.SwimVz, In.SwimDepth, SwimHipZ, Want, SwimPitch, float(BonePos(B, Head).Z), In.bSwimUnder ? 1 : 0);
		}
	}
}
