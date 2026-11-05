#include "KnightAnimator.h"
#include "KnightCharacter.h"
#include "KnightDesign.h"
#include "KnightModel.h"
#include "KnightShieldRest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "JeebTuning.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"

using namespace JeebAnim;

namespace
{
	constexpr float MeshYawDeg = -90.f;   // the model looks along his +Y; the game's forward is +X
	const TCHAR* const BoneNames[B_Count] = { TEXT("Hips"), TEXT("Spine"), TEXT("Spine1"), TEXT("Spine2"), TEXT("Neck"), TEXT("Head"),
		TEXT("LeftShoulder"), TEXT("LeftArm"), TEXT("LeftForeArm"), TEXT("LeftHand"), TEXT("RightShoulder"), TEXT("RightArm"), TEXT("RightForeArm"), TEXT("RightHand"),
		TEXT("LeftUpLeg"), TEXT("LeftLeg"), TEXT("LeftFoot"), TEXT("LeftToeBase"), TEXT("RightUpLeg"), TEXT("RightLeg"), TEXT("RightFoot"), TEXT("RightToeBase") };

	// Every clip the animator uses, besides his rest pose, walk and run in /Game/NewSuit; a missing one is left out (Art/Knight Chainmail/Clips: Tools/ImportKnightClips.ps1).
	const TCHAR* const ClipNames[] = {
		TEXT("Idle"), TEXT("Idle_SS"), TEXT("WalkBack_SS"), TEXT("WalkL_SS"), TEXT("WalkR_SS"), TEXT("RunBack_SS"), TEXT("RunL_SS"), TEXT("RunR_SS"), TEXT("Sprint"),
		TEXT("Jump_SS"), TEXT("RunJump_SS"), TEXT("FallIdle"), TEXT("FallLand"), TEXT("HardLand"), TEXT("StandRoll"),
		TEXT("SlashIn"), TEXT("SlashDown"), TEXT("SlashCrouch"), TEXT("ThrustSlash"), TEXT("KickSparta"),
		TEXT("HitHead"), TEXT("HitBody"), TEXT("HitBlocked"), TEXT("HitLargeF"), TEXT("HitSmallL"), TEXT("HitSmallR"), TEXT("HitSmallB"),
		TEXT("SweepFall"), TEXT("GetUpBack"), TEXT("FallDown"),
		TEXT("BlockIdle"), TEXT("CrouchIdle"), TEXT("CrouchWalk"),
		TEXT("BowDraw"), TEXT("BowRecoil"), TEXT("AimIdle"), TEXT("AimWalkF"), TEXT("AimWalkB"), TEXT("AimWalkL"), TEXT("AimWalkR"), TEXT("BowWalk"), TEXT("BowRun") };

	FVector ToRotVec(const FQuat& Q0)
	{
		FQuat Q = Q0;
		if (Q.W < 0.f) { Q.X = -Q.X; Q.Y = -Q.Y; Q.Z = -Q.Z; Q.W = -Q.W; }
		const FVector V(Q.X, Q.Y, Q.Z);
		const float S = V.Size();
		if (S < 1e-5f) return V * 2.f;
		return V * (2.f * FMath::Atan2(S, Q.W) / S);
	}
	FQuat FromRotVec(const FVector& V)
	{
		const float A = V.Size();
		if (A < 1e-5f) return FQuat(V.X * 0.5f, V.Y * 0.5f, V.Z * 0.5f, 1.f).GetNormalized();
		return FQuat(V / A, A);
	}
	FQuat Nlerp(const FQuat& A, const FQuat& B, float W)
	{
		const float S = (A | B) < 0.f ? -1.f : 1.f;
		FQuat Q(A.X + (B.X * S - A.X) * W, A.Y + (B.Y * S - A.Y) * W, A.Z + (B.Z * S - A.Z) * W, A.W + (B.W * S - A.W) * W);
		Q.Normalize();
		return Q;
	}
}

// ---------------- Clips ----------------

bool FKnightAnimator::Init(UPoseableMeshComponent* InComp, USkeletalMesh* Mesh)
{
	Comp = InComp;
	if (!Comp || !Mesh) return false;
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 N = Ref.GetNum();
	Parents.SetNum(N); RefPos.SetNum(N); RefRot.SetNum(N); UpperMask.Init(0, N);
	for (int32 b = 0; b < N; ++b)
	{
		Parents[b] = Ref.GetParentIndex(b);
		RefPos[b] = Ref.GetRefBonePose()[b].GetLocation();
		RefRot[b] = Ref.GetRefBonePose()[b].GetRotation();
	}
	for (int32 i = 0; i < B_Count; ++i)
	{
		Bone[i] = INDEX_NONE;
		for (int32 b = 0; b < N; ++b)
		{
			const FString Nm = Ref.GetBoneName(b).ToString();
			if (Nm == BoneNames[i] || Nm.EndsWith(FString(TEXT(":")) + BoneNames[i])) { Bone[i] = b; break; }
		}
		if (Bone[i] == INDEX_NONE) { UE_LOG(LogTemp, Warning, TEXT("JEEBANIM no bone %s"), BoneNames[i]); return false; }
	}
	for (int32 i = Spine; i <= RHand; ++i) UpperMask[Bone[i]] = 1;
	for (FPose* P : { &Scratch, &ScratchSum, &Base, &Over, &Final, &PrevFinal }) { P->Rot = RefRot; P->Hip = RefPos[Bone[Hips]]; }
	OffRot.Init(FVector::ZeroVector, N);
	OffRotV.Init(FVector::ZeroVector, N);

	const double LoadFrom = FPlatformTime::Seconds();
	for (const TCHAR* Nm : ClipNames) LoadClip(Nm);
	LoadClip(TEXT("Rest"), TEXT("/Game/NewSuit/A_"));
	LoadClip(TEXT("Walking"), TEXT("/Game/NewSuit/A_"));
	LoadClip(TEXT("Running"), TEXT("/Game/NewSuit/A_"));
	// (his own walk and run ride high against the rest pose: this puts their planted foot on the floor)
	if (FClip* const* W = Clips.Find(TEXT("Walking"))) for (FVector& H : (*W)->HipPos) H.Z -= 3.9f;
	if (FClip* const* R = Clips.Find(TEXT("Running"))) for (FVector& H : (*R)->HipPos) H.Z -= 2.9f;
	LoadMoves();
	// Test pictures (-SuitLogClips): where the hand and foot go through each move's clip, the way to read off KnightMoves.csv's numbers
	if (FParse::Param(FCommandLine::Get(), TEXT("SuitLogClips")))
		for (const TPair<FString, FMove>& It : Moves)
			if (const FClip* C = Get(*It.Value.Clip))
			{
				FPose P; P.Rot = RefRot;
				FString Line = FString::Printf(TEXT("JEEBPATH %s %s (%.2f s; time: hand fwd,right,up / foot R up,fwd / hip up,fwd):"), *It.Key, *It.Value.Clip, C->Len);
				for (float T = 0.f; T <= C->Len + 0.001f; T += 0.1f)
				{
					Sample(*C, T, P, false);
					const FVector H = BonePos(P, RHand), F = BonePos(P, RFoot);
					Line += FString::Printf(TEXT(" | %.1f: %.0f,%.0f,%.0f / %.0f,%.0f / %.0f,%.0f"), T, H.Y, H.X, H.Z, F.Z, F.Y, P.Hip.Z, P.Hip.Y);
				}
				UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
			}
	UE_LOG(LogTemp, Log, TEXT("JEEBANIM %d clips, %d bones, %d moves, %.0f ms to load"), Clips.Num(), N, Moves.Num(), (FPlatformTime::Seconds() - LoadFrom) * 1000.0);
	return Clips.Contains(TEXT("Rest"));
}

const FClip* FKnightAnimator::LoadClip(const FString& Name, const TCHAR* Folder)
{
	const FString Path = FString(Folder) + Name + TEXT(".A_") + Name;
	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Seq || !Seq->GetSkeleton()) return nullptr;
	TUniquePtr<FClip> C = MakeUnique<FClip>();
	C->Name = Name;
	C->Len = FMath::Max(Seq->GetPlayLength(), 0.0333f);
	C->NumFrames = FMath::Max(FMath::RoundToInt(C->Len * 30.f) + 1, 2);
	const int32 N = Parents.Num();
	C->Rot.SetNum(C->NumFrames * N);
	C->HipPos.SetNum(C->NumFrames);
	const FReferenceSkeleton& SkelRef = Seq->GetSkeleton()->GetReferenceSkeleton();
	TArray<int32> Track;
	Track.SetNum(N);
	for (int32 b = 0; b < N; ++b) Track[b] = SkelRef.FindBoneIndex(Comp->GetBoneName(b));
	for (int32 f = 0; f < C->NumFrames; ++f)
	{
		const double T = C->Len * f / (C->NumFrames - 1);
		const FAnimExtractContext Ctx(T, false);
		for (int32 b = 0; b < N; ++b)
		{
			FTransform X;
			if (Track[b] != INDEX_NONE) Seq->GetBoneTransform(X, FSkeletonPoseBoneIndex(Track[b]), Ctx, false);
			else { X.SetRotation(RefRot[b]); X.SetLocation(RefPos[b]); }
			C->Rot[f * N + b] = X.GetRotation();
			if (b == Bone[Hips]) C->HipPos[f] = X.GetLocation();
		}
	}
	Measure(*C);
	FClip* Raw = C.Get();
	ClipStore.Add(MoveTemp(C));
	Clips.Add(Name, Raw);
	return Raw;
}

// How fast the planted foot moves, and when each foot is under him. All clips are in place, so a planted foot slides back under
// the body at exactly the speed the body should cover the ground.
void FKnightAnimator::Measure(FClip& C)
{
	const int32 N = Parents.Num();
	FPose P;
	P.Rot.SetNum(N);
	TArray<FVector> L, R;
	L.SetNum(C.NumFrames); R.SetNum(C.NumFrames);
	float BestL = -1e9f, BestR = -1e9f;
	float MinL = 1e9f, MinR = 1e9f;
	for (int32 f = 0; f < C.NumFrames; ++f)
	{
		FMemory::Memcpy(P.Rot.GetData(), &C.Rot[f * N], sizeof(FQuat) * N);
		P.Hip = C.HipPos[f];
		L[f] = BonePos(P, LFoot);
		R[f] = BonePos(P, RFoot);
		MinL = FMath::Min(MinL, float(L[f].Z)); MinR = FMath::Min(MinR, float(R[f].Z));
		const float T = C.Len * f / (C.NumFrames - 1);
		if (float(R[f].Z - L[f].Z) > BestL) { BestL = R[f].Z - L[f].Z; C.MarkL = T; }
		if (float(L[f].Z - R[f].Z) > BestR) { BestR = L[f].Z - R[f].Z; C.MarkR = T; }
	}
	const float Df = C.Len / (C.NumFrames - 1);
	TArray<float> V;
	for (int32 f = 0; f + 1 < C.NumFrames; ++f)
	{
		if (L[f].Z < MinL + 3.f && L[f + 1].Z < MinL + 3.f) V.Add(FVector2D(L[f + 1] - L[f]).Size() / Df);
		if (R[f].Z < MinR + 3.f && R[f + 1].Z < MinR + 3.f) V.Add(FVector2D(R[f + 1] - R[f]).Size() / Df);
	}
	if (V.Num()) { V.Sort(); C.FootSpeed = V[V.Num() / 2]; }
}

// Content/Data/KnightMoves.csv: Move,Clip,ClipStart,ClipHit,ClipFollow,ClipEnd,LegsFull,Blend,HipLock,SpineKeep
void FKnightAnimator::LoadMoves()
{
	Moves.Reset();
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectContentDir() / TEXT("Data/KnightMoves.csv")))) { UE_LOG(LogTemp, Warning, TEXT("JEEBANIM no KnightMoves.csv")); return; }
	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		TArray<FString> F;
		Lines[i].ParseIntoArray(F, TEXT(","), false);
		if (F.Num() < 8 || F[0].IsEmpty() || F[0].StartsWith(TEXT("#"))) continue;
		FMove M;
		M.Clip = F[1];
		M.ClipStart = FCString::Atof(*F[2]); M.ClipHit = FCString::Atof(*F[3]); M.ClipFollow = FCString::Atof(*F[4]); M.ClipEnd = FCString::Atof(*F[5]);
		M.LegsFull = FCString::Atof(*F[6]); M.Blend = FCString::Atof(*F[7]);
		if (F.Num() > 8) M.HipLock = FCString::Atof(*F[8]);
		if (F.Num() > 9) M.SpineKeep = FCString::Atof(*F[9]);
		Moves.Add(F[0], M);
	}
}

// ---------------- Sampling and blending ----------------

void FKnightAnimator::Sample(const FClip& C, float T, FPose& Out, bool bLoop) const
{
	const int32 N = Parents.Num();
	float U = bLoop ? FMath::Fmod(T, C.Len) : FMath::Clamp(T, 0.f, C.Len);
	if (U < 0.f) U += C.Len;
	const float Fr = U / C.Len * (C.NumFrames - 1);
	const int32 F0 = FMath::Min(FMath::FloorToInt(Fr), C.NumFrames - 1), F1 = FMath::Min(F0 + 1, C.NumFrames - 1);
	const float W = Fr - F0;
	const FQuat* A = &C.Rot[F0 * N];
	const FQuat* B = &C.Rot[F1 * N];
	for (int32 b = 0; b < N; ++b) Out.Rot[b] = Nlerp(A[b], B[b], W);
	Out.Hip = FMath::Lerp(C.HipPos[F0], C.HipPos[F1], W);
}

float FKnightAnimator::PhaseTime(const FClip& C, float P) const
{
	const float GapLR = FMath::Fmod(C.MarkR - C.MarkL + C.Len, C.Len);
	const float GapRL = C.Len - GapLR;
	return P < 0.5f ? C.MarkL + P * 2.f * GapLR : C.MarkR + (P - 0.5f) * 2.f * GapRL;
}

void FKnightAnimator::Accum(const FPose& P, float W, FPose& Sum, float& SumW) const
{
	if (W <= 0.f) return;
	const int32 N = Parents.Num();
	if (SumW <= 0.f)
	{
		for (int32 b = 0; b < N; ++b) { const FQuat& Q = P.Rot[b]; Sum.Rot[b] = FQuat(Q.X * W, Q.Y * W, Q.Z * W, Q.W * W); }
		Sum.Hip = P.Hip * W;
	}
	else
	{
		for (int32 b = 0; b < N; ++b)
		{
			const FQuat& Q = P.Rot[b];
			const float S = (Sum.Rot[b] | Q) < 0.f ? -W : W;
			Sum.Rot[b].X += Q.X * S; Sum.Rot[b].Y += Q.Y * S; Sum.Rot[b].Z += Q.Z * S; Sum.Rot[b].W += Q.W * S;
		}
		Sum.Hip += P.Hip * W;
	}
	SumW += W;
}

void FKnightAnimator::Finish(FPose& Sum, float SumW) const
{
	for (FQuat& Q : Sum.Rot) Q.Normalize();
	if (SumW > 0.f) Sum.Hip /= SumW;
}

// The turn of a bone's parent in the mesh's own space
FQuat FKnightAnimator::ParentRot(const FPose& P, int32 B) const
{
	FQuat Q = FQuat::Identity;
	for (int32 p = Parents[Bone[B]]; p != INDEX_NONE; p = Parents[p]) Q = P.Rot[p] * Q;
	return Q;
}

// Turns one bone by Q as seen in the mesh's space (up is up, whatever the bones above it are doing)
void FKnightAnimator::TurnInMesh(FPose& P, int32 B, const FQuat& Q) const
{
	const FQuat Pa = ParentRot(P, B);
	P.Rot[Bone[B]] = (Pa.Inverse() * Q * Pa * P.Rot[Bone[B]]).GetNormalized();
}

FVector FKnightAnimator::BonePos(const FPose& P, int32 B, FQuat* OutRot) const
{
	const int32 Start = Bone[B];
	FVector Pos = FVector::ZeroVector;
	FQuat Rot = FQuat::Identity;
	for (int32 b = Start; b != INDEX_NONE; b = Parents[b])
	{
		const FVector Local = (b == Bone[Hips]) ? P.Hip : RefPos[b];
		Pos = P.Rot[b].RotateVector(Pos) + Local;
		Rot = P.Rot[b] * Rot;
	}
	if (OutRot) *OutRot = Rot;
	return Pos;
}

// Puts Over on Base: Upper for the bones above the waist, Lower for the hips and legs. The waist is kept in the mesh's frame: when the
// legs stay with Base, the chest still faces the way Over's chest faces, not the way Base's hips happen to swing.
void FKnightAnimator::Overlay(FPose& B, const FPose& O, float Upper, float Lower) const
{
	const int32 N = Parents.Num();
	const int32 HipB = Bone[Hips], SpineB = Bone[Spine];
	const FQuat BaseHip = B.Rot[HipB], OverHip = O.Rot[HipB];
	const FQuat FinalHip = Lower > 0.f ? Nlerp(BaseHip, OverHip, Lower) : BaseHip;
	const FQuat SpineWant = FinalHip.Inverse() * (OverHip * O.Rot[SpineB]);
	const FQuat SpineKeep = FinalHip.Inverse() * (BaseHip * B.Rot[SpineB]);
	for (int32 b = 0; b < N; ++b)
	{
		if (b == SpineB || b == HipB) continue;
		const float W = UpperMask[b] ? Upper : Lower;
		if (W > 0.f) B.Rot[b] = Nlerp(B.Rot[b], O.Rot[b], W);
	}
	B.Rot[HipB] = FinalHip;
	B.Rot[SpineB] = Nlerp(SpineKeep, SpineWant, Upper);
	if (Lower > 0.f) B.Hip = FMath::Lerp(B.Hip, O.Hip, Lower);
}

// ---------------- Locomotion ----------------
// One phase for every direction and speed: 0 = the left foot under him, 0.5 = the right. It moves by distance covered, so
// whatever the mix of clips, the planted foot keeps pace with the ground. Standing is its own pose; starts and stops are inertial blends.
void FKnightAnimator::Locomotion(float Dt, AKnightCharacter* Me, FPose& Out)
{
	const FVector V = Me->GetVelocity();
	const float Speed = FVector2D(V.X, V.Y).Size();
	SmSpeed = FMath::FInterpTo(SmSpeed, Speed, Dt, 9.f);
	const bool bMoveNow = Speed > (bMoving ? 12.f : 25.f);
	if (bMoveNow != bMoving)
	{
		bMoving = bMoveNow;
		if (bMoving)
		{
			// the first step: start with one foot under him and the other swinging through, so it steps off at once. Every start uses the other foot.
			Phase = bPhaseLeft ? 0.f : 0.5f;
			bPhaseLeft = !bPhaseLeft;
			if (SmSpeed < 150.f) SmSpeed = FMath::Min(Speed, 230.f);
			// (from standing he sets off straight in the way he means to go, not swinging round through the other ways from where he last went)
			const FVector Lv = Me->GetActorRotation().UnrotateVector(FVector(V.X, V.Y, 0.f));
			if (Speed > 1.f) { DirF = Lv.X / Speed; DirR = Lv.Y / Speed; }
			bLocoHand = false;
		}
	}
	// the arms: what he holds
	const int32 Kind = Me->SelectedKind();
	const bool bBow = Kind == JeebItems::K_Bow;
	float SumW = 0.f;
	if (!bMoving)
	{
		IdleT += Dt;
		// Just after a fight he stays on guard a moment, in the stance the sword clips start and end in, so one move runs into the next
		// without his feet moving; then he eases back to rest
		bOnGuard = CombatT > 0.f && !bBow;
		if (bOnGuard)
			if (const FClip* G = Get(TEXT("Idle_SS"))) { Sample(*G, IdleT, Out, true); return; }
		// Standing: the rest pose the owner chose (hands almost at his hips), breathing on top
		if (const FClip* C = Get(TEXT("Rest"))) Sample(*C, 0.f, Out, false);
		if (const FClip* I = Get(TEXT("Idle")))
		{
			Sample(*I, IdleT, Scratch, true);
			const int32 N = Parents.Num();
			const FQuat* Zero = &I->Rot[0];
			for (int32 i = Spine; i <= Head; ++i)
			{
				const int32 b = Bone[i];
				const FQuat Delta = Scratch.Rot[b] * Zero[b].Inverse();
				Out.Rot[b] = (Nlerp(FQuat::Identity, Delta, 0.5f) * Out.Rot[b]).GetNormalized();
			}
			for (const int32 i : { LArm, RArm })
			{
				const int32 b = Bone[i];
				const FQuat Delta = Scratch.Rot[b] * Zero[b].Inverse();
				Out.Rot[b] = (Nlerp(FQuat::Identity, Delta, 0.25f) * Out.Rot[b]).GetNormalized();
			}
		}
		return;
	}
	// the way he is going, in his own frame (strafe mode: he faces the camera; hips and legs turn up to 45 degrees toward the way he goes, as the old pose does)
	const FVector LV = Me->GetActorRotation().UnrotateVector(FVector(V.X, V.Y, 0.f));
	if (Speed > 1.f)
	{
		const float Rate = 11.f;
		DirF = FMath::FInterpTo(DirF, LV.X / Speed, Dt, Rate);
		DirR = FMath::FInterpTo(DirR, LV.Y / Speed, Dt, Rate);
	}
	const float LegYaw = Me->LegYaw;
	const float Ang = FMath::RadiansToDegrees(FMath::Atan2(DirR, DirF)) - LegYaw;
	const float CosA = FMath::Cos(FMath::DegreesToRadians(Ang)), SinA = FMath::Sin(FMath::DegreesToRadians(Ang));
	float WD[4] = { FMath::Max(CosA, 0.f), FMath::Max(SinA, 0.f), FMath::Max(-CosA, 0.f), FMath::Max(-SinA, 0.f) };   // ahead, right, back, left
	const float WS = WD[0] + WD[1] + WD[2] + WD[3];
	for (float& W : WD) W /= FMath::Max(WS, 0.01f);
	// walk, run and sprint by speed
	const float URun = FMath::SmoothStep(230.f, 400.f, SmSpeed);
	const float USprint = WD[0] > 0.7f ? FMath::SmoothStep(560.f, 700.f, SmSpeed) : 0.f;

	// (ahead he walks and runs upright, as the owner saw and liked; the sword-and-shield clips only for going sideways and backing up: their forward run leans far over)
	const TCHAR* Names[9] = { TEXT("Walking"), TEXT("WalkR_SS"), TEXT("WalkBack_SS"), TEXT("WalkL_SS"), TEXT("Running"), TEXT("RunR_SS"), TEXT("RunBack_SS"), TEXT("RunL_SS"), TEXT("Sprint") };
	float W[9];
	for (int32 i = 0; i < 4; ++i) { W[i] = WD[i] * (1.f - URun) * (1.f - USprint); W[i + 4] = WD[i] * URun * (1.f - USprint); }
	W[8] = USprint;
	// the distance one full cycle covers, for this mix
	float Stride = 0.f, TotalW = 0.f;
	const FClip* Use[9];
	for (int32 i = 0; i < 9; ++i)
	{
		Use[i] = W[i] > 0.001f ? Get(Names[i]) : nullptr;
		if (Use[i] && Use[i]->FootSpeed > 10.f) { Stride += W[i] * Use[i]->FootSpeed * Use[i]->Len; TotalW += W[i]; }
	}
	if (TotalW > 0.f) Stride /= TotalW; else Stride = 200.f;
	// (never plays faster or slower than looks right)
	float CycleLen = 0.f;
	for (int32 i = 0; i < 9; ++i) if (Use[i]) CycleLen += W[i] * Use[i]->Len;
	CycleLen = FMath::Max(CycleLen / FMath::Max(TotalW, 0.01f), 0.1f);
	const float Rate = FMath::Clamp(Speed / FMath::Max(Stride, 1.f), 0.45f / CycleLen, 1.45f / CycleLen);
	Phase = FMath::Fmod(Phase + Rate * Dt, 1.f);

	for (int32 i = 0; i < 9; ++i)
		if (Use[i] && W[i] > 0.001f)
		{
			Sample(*Use[i], PhaseTime(*Use[i], Phase), Scratch, true);
			Accum(Scratch, W[i], Out, SumW);
		}
	Finish(Out, SumW);
	// (the hand differs most between his own walk and the sword-and-shield clips, and a mix of them swings it fast at the halfway point, with a blade in it: it
	// follows at a steady rate instead)
	{
		const int32 HandB = Bone[RHand];
		if (bLocoHand)
		{
			const float MaxTurn = FMath::DegreesToRadians(900.f) * Dt;
			const float Angle = float(LocoHand.AngularDistance(Out.Rot[HandB]));
			if (Angle > MaxTurn) Out.Rot[HandB] = FQuat::Slerp(LocoHand, Out.Rot[HandB], MaxTurn / Angle).GetNormalized();
		}
		LocoHand = Out.Rot[HandB];
		bLocoHand = true;
	}
	// with the bow in hand: its own carry, in step
	if (bBow)
	{
		const FClip* Ca = Get(TEXT("BowWalk"));
		const FClip* Cb = Get(TEXT("BowRun"));
		float SumA = 0.f;
		if (Ca) { Sample(*Ca, PhaseTime(*Ca, Phase), Scratch, true); Accum(Scratch, 1.f - URun, Over, SumA); }
		if (Cb) { Sample(*Cb, PhaseTime(*Cb, Phase), Scratch, true); Accum(Scratch, URun, Over, SumA); }
		// (the arms only: these clips stoop far over, and he runs upright)
		if (SumA > 0.f)
		{
			Finish(Over, SumA);
			for (const int32 i : { LShoulder, LArm, LFore, LHand, RShoulder, RArm, RFore, RHand }) Out.Rot[Bone[i]] = Over.Rot[Bone[i]];
		}
	}
	// the hips turn toward the way he goes, the chest stays square to the camera
	if (FMath::Abs(LegYaw) > 0.1f)
	{
		const float R = FMath::DegreesToRadians(LegYaw);
		TurnInMesh(Out, Hips, FQuat(FVector::UpVector, R));
		TurnInMesh(Out, Spine, FQuat(FVector::UpVector, -R * 0.45f));
		TurnInMesh(Out, Spine1, FQuat(FVector::UpVector, -R * 0.3f));
		TurnInMesh(Out, Spine2, FQuat(FVector::UpVector, -R * 0.25f));
	}
}

// ---------------- Inertial blending ----------------

void FKnightAnimator::StartBlend(const FPose& Target, float Seconds, float HandSeconds)
{
	if (!bHavePrev) return;
	const int32 N = Parents.Num();
	const float Dt = FMath::Max(PrevDt, 0.001f);
	for (int32 b = 0; b < N; ++b)
	{
		OffRot[b] = ToRotVec(Final.Rot[b] * Target.Rot[b].Inverse());
		FVector W = ToRotVec(Final.Rot[b] * PrevFinal.Rot[b].Inverse()) / Dt;
		OffRotV[b] = W.GetClampedToMaxSize(10.f);
	}
	OffHip = Final.Hip - Target.Hip;
	OffHipV = ((Final.Hip - PrevFinal.Hip) / Dt).GetClampedToMaxSize(300.f);
	BlendT = 0.f;
	BlendLambda = 4.7f / FMath::Max(Seconds, 0.02f);   // (down to 5% by then)
	BlendLambdaHand = FMath::Min(BlendLambda, 4.7f / FMath::Max(HandSeconds, 0.02f));
	bBlending = true;
}

void FKnightAnimator::Inertialize(const FPose& Target, FPose& Out, float Dt)
{
	const int32 N = Parents.Num();
	Out.Hip = Target.Hip;
	if (!bBlending)
	{
		for (int32 b = 0; b < N; ++b) Out.Rot[b] = Target.Rot[b];
		return;
	}
	BlendT += Dt;
	const float L = BlendLambda, T = BlendT;
	const float E = FMath::Exp(-L * T);
	if (BlendLambdaHand * T > 8.f) bBlending = false;
	for (int32 b = 0; b < N; ++b)
	{
		// (the sword hand turns further than anything else between his own walk and the sword clips, and the blade makes it show: it takes a little longer)
		const float LH = b == Bone[RHand] ? BlendLambdaHand : L;
		const FVector Y = (OffRot[b] + (OffRotV[b] + OffRot[b] * LH) * T) * FMath::Exp(-LH * T);
		Out.Rot[b] = (FromRotVec(Y) * Target.Rot[b]).GetNormalized();
	}
	Out.Hip = Target.Hip + (OffHip + (OffHipV + OffHip * L) * T) * E;
}

// A low pose can put the point of the sword through the floor: the wrist lifts it just clear, keeping the way it points. Only as much as it takes, and it
// comes and goes with the pose, so it can't be seen happening.
void FKnightAnimator::KeepTipUp(FPose& P, AKnightCharacter* Me) const
{
	if (Me->SelectedKind() == JeebItems::K_Bow) return;
	constexpr float Reach = 95.f, Clear = 5.f;   // (about the sword's length from the wrist)
	FQuat HandRot;
	const FVector HandAt = BonePos(P, JeebAnim::RHand, &HandRot);
	const FVector Dir = HandRot.RotateVector(BladeInHand).GetSafeNormal();
	const float WantZ = FMath::Clamp((Clear - float(HandAt.Z)) / Reach, -1.f, 0.9f);
	if (Dir.Z >= WantZ) return;
	const FVector Flat = FVector(Dir.X, Dir.Y, 0.f).GetSafeNormal();
	if (Flat.IsNearlyZero()) return;
	const FVector NewDir = Flat * FMath::Sqrt(1.f - WantZ * WantZ) + FVector::UpVector * WantZ;
	TurnInMesh(P, JeebAnim::RHand, FQuat::FindBetweenNormals(Dir, NewDir));
}

// ---------------- The frame ----------------

void FKnightAnimator::Update(float Dt, AKnightCharacter* Me)
{
	if (!Comp || !Me) return;
	Time += Dt;
	Locomotion(Dt, Me, Base);
	Overlays(Dt, Me, Base);
	float BlendFor = bMoving ? 0.26f : 0.28f;   // (he sets off at once, but the lean and stride build; a stop settles)
	const int32 Act = ChooseAction(Dt, Me, Base, BlendFor);
	if (Act == 0 && StateKey >= 10) BlendFor = StateKey >= 30 && StateKey < 40 ? 0.36f : 0.3f;   // (out of any move, back to walking or standing: the body eases up out of it, never pops)
	if (Act == 0 && !bMoving && StateKey == 2 && !bOnGuard) BlendFor = 0.55f;   // (easing back to rest takes its time)
	int32 Key = Act != 0 ? Act : (bMoving ? 1 : bOnGuard ? 2 : 0);

	if (!ForcedClip.IsEmpty())
	{
		if (const FClip* C = Get(*ForcedClip))
			Sample(*C, ForcedPhase >= 0.f ? ForcedPhase * C->Len : Time, Base, true);
		Key = 999;
	}

	if (Key != StateKey)
	{
		// (between his own walk and the sword-and-shield clips the hand turns furthest of all, and the blade shows it: never a whirl)
		if (StateKey != -2) StartBlend(Base, BlendFor, Act == 0 ? 0.34f : 0.16f);
		StateKey = Key;
	}
	// (the bow's phases are layers, not states: drawing, loosing and lowering still flow into each other)
	const int32 BowKey = int32(Me->Bow);
	if (BowKey != LastBowKey && Key == StateKey) StartBlend(Base, 0.16f, 0.2f);
	LastBowKey = BowKey;
	PrevFinal = Final;
	PrevDt = Dt;
	Inertialize(Base, Final, Dt);
	// (a part-way landing or a chest-only move can leave a boot sunk: the hips rise just enough. Floor clips lie lower.)
	{
		const float Sole = (StateKey == 10 || StateKey == 11 || StateKey == 20) ? 1.f : 7.f;
		const float Low = FMath::Min(float(BonePos(Final, LFoot).Z), float(BonePos(Final, RFoot).Z));
		if (Low < Sole) Final.Hip.Z += Sole - Low;
	}
	KeepTipUp(Final, Me);
	bHavePrev = true;

	// Into the mesh
	TArray<FTransform>& Bones = Comp->BoneSpaceTransforms;
	const int32 N = Parents.Num();
	if (Bones.Num() == N)
	{
		const int32 HipB = Bone[Hips];
		for (int32 b = 0; b < N; ++b)
		{
			Bones[b].SetRotation(Final.Rot[b]);
			Bones[b].SetTranslation(b == HipB ? Final.Hip : RefPos[b]);
			Bones[b].SetScale3D(FVector::OneVector);
		}
		Comp->MarkRefreshTransformDirty();
		Comp->RefreshBoneTransforms();
	}

	// A check for the logs (test pictures only): the biggest jump of any bone between two frames: pops show here before they show in a picture
	if (Me->bShotFace && bHavePrev)
	{
		float Worst = 0.f; int32 WorstB = 0;
		for (int32 b = 0; b < N; ++b)
		{
			const float A = FMath::RadiansToDegrees(ToRotVec(Final.Rot[b] * PrevFinal.Rot[b].Inverse()).Size());
			if (A > Worst) { Worst = A; WorstB = b; }
		}
		const float Sp = FVector2D(Me->GetVelocity().X, Me->GetVelocity().Y).Size();
		const FVector FL = BonePos(Final, LFoot), FR = BonePos(Final, RFoot);
		// how far a foot on the ground slides on the floor since the last frame (cm/s): body speed against its speed under the body
		static FVector PrevL = FVector::ZeroVector, PrevR = FVector::ZeroVector;
		const FVector VWorld = FRotator(0.f, -Me->GetActorRotation().Yaw - MeshYawDeg, 0.f).RotateVector(FVector(Me->GetVelocity().X, Me->GetVelocity().Y, 0.f));
		const float SlideL = FL.Z < 14.f ? FVector2D((FL - PrevL) / FMath::Max(Dt, 0.001f) + VWorld).Size() : -1.f;
		const float SlideR = FR.Z < 14.f ? FVector2D((FR - PrevR) / FMath::Max(Dt, 0.001f) + VWorld).Size() : -1.f;
		PrevL = FL; PrevR = FR;
		FQuat HandRot; const FVector HandAt = BonePos(Final, RHand, &HandRot);
		const float TipZ = HandAt.Z + HandRot.RotateVector(BladeInHand).Z * 95.f;   // (about the sword's length: over the floor)
		UE_LOG(LogTemp, Log, TEXT("JEEBANIM t %.3f key %d speed %.0f phase %.2f worst %.1f deg %s feet %.1f %.1f hip %.1f slide %.0f %.0f tip %.0f"), Time, StateKey, Sp, Phase, Worst, *Comp->GetBoneName(WorstB).ToString(), FL.Z, FR.Z, Final.Hip.Z, SlideL, SlideR, TipZ);
	}
}

// Test pictures (-SuitGallery): every picture is one clip shown as eight frozen knights in a row, across its length, with its name above
void FKnightAnimator::TickGallery(AKnightCharacter* Me)
{
	AActor* Owner = Comp->GetOwner();
	Me->SetActorHiddenInGame(true);
	Comp->SetVisibility(false);
	const FVector Floor = Me->GetActorLocation() - FVector(0.f, 0.f, Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (GalleryComps.Num() == 0)
	{
		ClipStore.Sort([](const TUniquePtr<FClip>& A, const TUniquePtr<FClip>& B) { return A->Name < B->Name; });
		for (int32 i = 0; i < 8; ++i)
		{
			UPoseableMeshComponent* C = NewObject<UPoseableMeshComponent>(Owner);
			C->SetSkinnedAssetAndUpdate(Comp->GetSkinnedAsset());
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetBoundsScale(3.f);
			C->RegisterComponent();
			for (int32 m = 0; m < Comp->GetNumMaterials(); ++m) C->SetMaterial(m, Comp->GetMaterial(m));
			C->SetUsingAbsoluteLocation(true); C->SetUsingAbsoluteRotation(true);
			GalleryComps.Add(C);
		}
		GalleryLabel = NewObject<UTextRenderComponent>(Owner);
		GalleryLabel->RegisterComponent();
		GalleryLabel->SetUsingAbsoluteLocation(true); GalleryLabel->SetUsingAbsoluteRotation(true);
		GalleryLabel->SetHorizontalAlignment(EHTA_Center);
		GalleryLabel->SetWorldSize(40.f);
		GalleryLabel->SetTextRenderColor(FColor::Yellow);
	}
	int32 First = 0;
	FParse::Value(FCommandLine::Get(), TEXT("SuitGalleryFrom="), First);
	const int32 Shown = First + GalleryTick++ / 2;
	if (!ClipStore.IsValidIndex(Shown)) return;
	const FClip& Cl = *ClipStore[Shown];
	GalleryLabel->SetWorldLocationAndRotation(Floor + FVector(0.f, 0.f, 215.f), FRotator(0.f, 180.f, 0.f));
	GalleryLabel->SetText(FText::FromString(FString::Printf(TEXT("%s  %.2f s  (0 .. 7/8)"), *Cl.Name, Cl.Len)));
	FPose P;
	P.Rot = RefRot;
	for (int32 i = 0; i < 8; ++i)
	{
		Sample(Cl, Cl.Len * (i / 7.f), P, false);
		UPoseableMeshComponent* C = GalleryComps[i];
		C->SetWorldLocationAndRotation(Floor + FVector(0.f, (i - 3.5f) * 145.f, 0.f), FRotator(0.f, 90.f + MeshYawDeg, 0.f));
		for (int32 b = 0; b < Parents.Num(); ++b)
		{
			C->BoneSpaceTransforms[b].SetRotation(P.Rot[b]);
			C->BoneSpaceTransforms[b].SetTranslation(b == Bone[Hips] ? P.Hip : RefPos[b]);
		}
		C->MarkRefreshTransformDirty();
		C->RefreshBoneTransforms();
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBGALLERY %d %s"), Shown, *Cl.Name);
}

// ---------------- Layers over the walk: crouching, the raised shield, aiming ----------------
void FKnightAnimator::Overlays(float Dt, AKnightCharacter* Me, FPose& B)
{
	const FVector V = Me->GetVelocity();
	const float Speed = FVector2D(V.X, V.Y).Size();
	const float MoveW = FMath::Clamp(Speed / 150.f, 0.f, 1.f);
	if (Me->CrouchAlpha > 0.01f)
	{
		const FClip* C = bMoving ? Get(TEXT("CrouchWalk")) : Get(TEXT("CrouchIdle"));
		if (C)
		{
			Sample(*C, bMoving ? PhaseTime(*C, Phase) : Time, Over, true);
			Overlay(B, Over, Me->CrouchAlpha * 0.75f, Me->CrouchAlpha);
			// (the walking clip stands much taller than the crouch: mixing the crouch's legs back in keeps him low, with a shorter stride that suits the slow speed)
			if (bMoving)
				if (const FClip* Low = Get(TEXT("CrouchIdle")))
				{
					Sample(*Low, Time, Over, true);
					Overlay(B, Over, 0.f, Me->CrouchAlpha * 0.55f);
				}
		}
	}
	if (Me->BlockAlpha > 0.01f)
		if (const FClip* C = Get(TEXT("BlockIdle")))
		{
			Sample(*C, Time, Over, true);
			Overlay(B, Over, Me->BlockAlpha, Me->BlockAlpha * (1.f - MoveW) * (1.f - Me->CrouchAlpha));
		}
	if (Me->BowAimAlpha > 0.01f)
	{
		const float W = Me->BowAimAlpha;
		const FClip* Upper = nullptr; float T = Time; bool bLoop = true;
		if (Me->Bow == AKnightCharacter::EBow::Drawing)
		{
			const FClip* D = Get(TEXT("BowDraw"));
			const FMove* M = Moves.Find(TEXT("BowDraw"));
			const float U = Me->BowT / FMath::Max(JeebTune::V().BowDrawTime, 0.1f);
			if (D && M && U < 1.f) { Upper = D; T = FMath::Lerp(M->ClipStart, M->ClipEnd, U); bLoop = false; }
			else Upper = Get(TEXT("AimIdle"));
		}
		else if (Me->Bow == AKnightCharacter::EBow::Recover)
		{
			const FMove* M = Moves.Find(TEXT("BowRecoil"));
			Upper = Get(TEXT("BowRecoil")); bLoop = false;
			if (M) T = M->ClipStart + Me->BowT * 1.2f;
		}
		else
		{
			// (the arms come down from where the loose left them)
			const FMove* M = Moves.Find(TEXT("BowRecoil"));
			Upper = Get(TEXT("BowRecoil")); bLoop = false;
			if (M) T = M->ClipEnd;
		}
		if (Upper)
		{
			Sample(*Upper, T, Over, bLoop);
			Overlay(B, Over, W, W * (1.f - MoveW));
		}
		// moving, the legs walk his aiming stance's way, in step
		if (bMoving && MoveW > 0.01f)
		{
			const FVector LV = Me->GetActorRotation().UnrotateVector(FVector(V.X, V.Y, 0.f));
			const float CosA = LV.X / FMath::Max(Speed, 1.f), SinA = LV.Y / FMath::Max(Speed, 1.f);
			const float WD[4] = { FMath::Max(CosA, 0.f), FMath::Max(SinA, 0.f), FMath::Max(-CosA, 0.f), FMath::Max(-SinA, 0.f) };
			const TCHAR* Nm[4] = { TEXT("AimWalkF"), TEXT("AimWalkR"), TEXT("AimWalkB"), TEXT("AimWalkL") };
			float Sum = 0.f;
			FPose& Tmp = ScratchSum;
			for (int32 i = 0; i < 4; ++i)
				if (WD[i] > 0.01f)
					if (const FClip* C = Get(Nm[i])) { Sample(*C, PhaseTime(*C, Phase), Scratch, true); Accum(Scratch, WD[i], Tmp, Sum); }
			if (Sum > 0.f) { Finish(Tmp, Sum); Overlay(B, Tmp, 0.f, W * MoveW); }
		}
		// the whole chest follows the camera up and down, so the bow's line stays on what he aims at
		if (APlayerController* PC = Cast<APlayerController>(Me->GetController()))
		{
			const float Pitch = FMath::UnwindDegrees(PC->GetControlRotation().Pitch);
			const FQuat Q(FVector::XAxisVector, FMath::DegreesToRadians(Pitch * 0.5f * W));
			TurnInMesh(B, Spine1, Q);
			TurnInMesh(B, Spine2, Q);
		}
	}
}

// ---------------- What he is doing ----------------
// Plays a move's clip part way through: U runs 0..1 over the move in the game's own timing, UHit..UFollow its live frames. Each stretch
// of the game's timing takes its own stretch of the clip.
float FKnightAnimator::MoveClipTime(const FMove& M, float U, float UHit, float UFollow) const
{
	U = FMath::Clamp(U, 0.f, 1.f);
	if (U < UHit) return FMath::Lerp(M.ClipStart, M.ClipHit, U / FMath::Max(UHit, 0.001f));
	if (U < UFollow) return FMath::Lerp(M.ClipHit, M.ClipFollow, (U - UHit) / FMath::Max(UFollow - UHit, 0.001f));
	return FMath::Lerp(M.ClipFollow, M.ClipEnd, (U - UFollow) / FMath::Max(1.f - UFollow, 0.001f));
}

// Laid over InOut, at clip time T; returns false if the move or clip is missing (he keeps walking: the game goes on)
bool FKnightAnimator::PlayAt(const TCHAR* MoveName, float T, float Speed, FPose& InOut, float* OutBlend)
{
	const FMove* M = Moves.Find(MoveName);
	if (!M) return false;
	const FClip* C = Get(*M->Clip);
	if (!C) return false;
	Sample(*C, T, Over, false);
	Over.Hip.Z += PlayLift;
	PlayLift = 0.f;
	// (HipLock: how much of the clip's own travel is taken out, where the capsule does the travelling)
	if (M->HipLock > 0.f && C->HipPos.Num())
	{
		const FVector& H0 = C->HipPos[FMath::Clamp(FMath::RoundToInt(M->ClipStart / FMath::Max(C->Len, 0.01f) * (C->NumFrames - 1)), 0, C->NumFrames - 1)];
		Over.Hip.X = FMath::Lerp(Over.Hip.X, H0.X, M->HipLock);
		Over.Hip.Y = FMath::Lerp(Over.Hip.Y, H0.Y, M->HipLock);
	}
	// LegsFull: 2 = always the whole body; 1 = the legs too while he stands, his own walk while he moves; 0 = chest and arms only
	const float Lower = M->LegsFull >= 2.f ? 1.f : M->LegsFull <= 0.f ? 0.f : 1.f - FMath::Clamp(Speed / 120.f, 0.f, 1.f);
	// (SpineKeep: the chest stays partly with what he was doing, so a clip's big wind-round doesn't turn his back on what he hits)
	FQuat Keep[3];
	const float KeepW = M->SpineKeep > 0.f ? FMath::Lerp(M->SpineKeep, 0.7f, FMath::Clamp(Speed / 300.f, 0.f, 1.f)) : 0.f;
	if (KeepW > 0.f) { Keep[0] = InOut.Rot[Bone[Spine]]; Keep[1] = InOut.Rot[Bone[Spine1]]; Keep[2] = InOut.Rot[Bone[Spine2]]; }
	Overlay(InOut, Over, PlayUpper, Lower * PlayLower);
	if (KeepW > 0.f)
	{
		InOut.Rot[Bone[Spine]] = Nlerp(InOut.Rot[Bone[Spine]], Keep[0], KeepW);
		InOut.Rot[Bone[Spine1]] = Nlerp(InOut.Rot[Bone[Spine1]], Keep[1], KeepW);
		InOut.Rot[Bone[Spine2]] = Nlerp(InOut.Rot[Bone[Spine2]], Keep[2], KeepW);
	}
	PlayUpper = PlayLower = 1.f;
	if (OutBlend) *OutBlend = M->Blend;
	return true;
}

int32 FKnightAnimator::ChooseAction(float Dt, AKnightCharacter* Me, FPose& B, float& OutBlend)
{
	using EStyle = AKnightCharacter::EAtkStyle;
	using ESm = AKnightCharacter::ESmash;
	const FVector V = Me->GetVelocity();
	const float Speed = FVector2D(V.X, V.Y).Size();
	const bool bAir = Me->GetCharacterMovement()->IsFalling();

	// landing and being hit are moments, not states: catch them here
	if (bAir) { AirT += Dt; PeakFall = FMath::Max(PeakFall, float(-V.Z)); bWasAir = true; }
	else
	{
		LandT = bWasAir ? 0.f : LandT + Dt;
		if (bWasAir)
		{
			bLandHard = PeakFall > 1250.f; LandStrength = FMath::Clamp(0.3f + PeakFall / 1200.f, 0.3f, 1.f); bWasAir = false; PeakFall = 0.f;
			if (const FMove* M = Moves.Find(bLandHard ? TEXT("LandHard") : TEXT("Land"))) LandFor = (M->ClipEnd - M->ClipStart) / FMath::Max(M->ClipHit, 0.1f);
		}
		AirT = 0.f;
	}
	if (Me->StunLeft > PrevStun + 0.01f)
	{
		++HitSerial;
		// which way the blow pushed him picks the flinch
		const FVector LV = Me->GetActorRotation().UnrotateVector(V);
		const bool bPushed = FVector2D(LV.X, LV.Y).Size() > 60.f;
		HitPick = Me->StunLeft >= 0.5f ? 0 : (!bPushed || LV.X < -FMath::Abs(LV.Y)) ? 1 + (HitSerial % 2) : LV.X > FMath::Abs(LV.Y) ? 3 : LV.Y > 0.f ? 4 : 5;
	}
	PrevStun = Me->StunLeft;
	if (Me->bKnockedDown && !bWasDown) { bFallPushed = Speed > 100.f; }
	bWasDown = Me->bKnockedDown;
	if (Me->bKnockedDown && Me->KnockPhase == 1 && RiseLeft <= 0.f) RiseLeft = 1.f;   // (the rising plays on a little past the locked part, unless he moves off)

	// Which state
	int32 Key = 0;
	if (Me->bKnockedDown) Key = Me->KnockPhase == 1 ? 11 : 10;
	else if (Me->bRolling) Key = 20;
	else if (Me->bAttacking)
	{
		if (Me->Smash == ESm::Rebound) Key = 36;
		else if (Me->AtkStyle == EStyle::Kick) Key = 33;
		else if (Me->AtkStyle == EStyle::Sweep) Key = 34;
		else if (Me->AtkStyle == EStyle::Dash) Key = 35;
		else if (Me->Smash == ESm::Charging) Key = 32;
		else if (Me->Smash == ESm::Swing || Me->Smash == ESm::EndLag) Key = 37;
		else Key = Me->AttackDir == AKnightCharacter::JabSecond ? 31 : 30;
	}
	else if (Me->StunLeft > 0.f) Key = 60 + HitSerial % 3;
	else if (RiseLeft > 0.f && !bMoving) Key = 11;
	else if (bAir) Key = AirT > 0.8f ? 41 : 40;
	else if (LandT < LandFor && !bMoving) Key = bLandHard ? 51 : 50;
	if (Key != 11) RiseLeft = 0.f;
	if ((Key >= 30 && Key < 40) || Key >= 60 || Key == 10 || Key == 11) CombatT = 4.f; else CombatT = FMath::Max(CombatT - Dt, 0.f);

	ActionT = Key == LastActionKey ? ActionT + Dt : 0.f;
	if (Key != LastActionKey && Key == 40) bJumpRunning = Speed > 300.f;
	LastActionKey = Key;

	// Rolls go the way he rolls, whichever way he faces
	float YawWant = 0.f;
	bRollBack = false;
	if (Key == 20)
	{
		float A = FMath::FindDeltaAngleDegrees(Me->GetActorRotation().Yaw, Me->RollDir.Rotation().Yaw);
		if (FMath::Abs(A) > 135.f) { bRollBack = true; A = FMath::UnwindDegrees(A + 180.f); }
		YawWant = A;
	}
	YawNow = FMath::FInterpTo(YawNow, YawWant, Dt, Key == 20 ? 22.f : 12.f);

	// (landing on the run he doesn't stop: the knees give for a moment, by how fast he came down, and the stride carries on over them)
	if (Key == 0 && bMoving && LandT < 0.3f)
		if (const FClip* C = Get(TEXT("FallLand")))
		{
			Sample(*C, 0.5f, Over, false);
			Overlay(B, Over, 0.f, FMath::Sin(LandT / 0.3f * PI) * 0.8f * LandStrength);
		}
	if (Key == 0) return 0;
	const auto& Tn = JeebTune::V();
	switch (Key)
	{
	case 10:
	{
		const TCHAR* Nm = bFallPushed ? TEXT("FallPushed") : TEXT("FallDropped");
		if (const FMove* M = Moves.Find(Nm))
		{
			const float U = FMath::Clamp(ActionT / FMath::Max(M->ClipHit, 0.1f), 0.f, 1.f);
			PlayLift = 3.f * U;
			PlayAt(Nm, FMath::Lerp(M->ClipStart, M->ClipEnd, U), 1000.f, B, &OutBlend);   // (ClipHit: how long the fall shows)
			// (lying there, he breathes)
			TurnInMesh(B, Spine1, FQuat(FVector::XAxisVector, FMath::DegreesToRadians(FMath::Sin(Time * 2.4f) * 1.2f * U)));
		}
		break;
	}
	case 11:
	{
		if (const FMove* M = Moves.Find(TEXT("Rise")))
		{
			// (ClipHit..ClipFollow: where the locked part ends in the clip; he is free to move after KnockRise)
			const float Show = FMath::Max(M->ClipFollow, 0.1f);
			const float T = FMath::Lerp(M->ClipStart, M->ClipEnd, FMath::Clamp(ActionT / Show, 0.f, 1.f));
			// (his boots sink through this clip's floor while he gathers himself: up on them, then out by the time he stands)
			PlayLift = FMath::Lerp(3.f, 9.f, FMath::SmoothStep(3.f, 4.f, T)) * (1.f - FMath::SmoothStep(4.8f, 5.6f, T));
			PlayAt(TEXT("Rise"), T, 1000.f, B, &OutBlend);
			RiseLeft = Show - ActionT;
		}
		break;
	}
	case 20:
	{
		if (const FMove* M = Moves.Find(TEXT("Roll")))
		{
			const float U = FMath::Clamp(Me->RollT / FMath::Max(Me->RollDur, 0.01f), 0.f, 1.f);
			// (backward: the clip run in reverse, from the crouch where it comes up, right over his shoulders to his hands on the floor. He pushes up from there
			// in the blend out, because the clip's own start is a lean forward.)
			if (bRollBack) PlayUpper = PlayLower = 1.f - 0.5f * FMath::SmoothStep(0.75f, 1.f, U);
			PlayAt(TEXT("Roll"), bRollBack ? FMath::Lerp(M->ClipEnd - 0.3f, M->ClipStart + 0.2f, U) : FMath::Lerp(M->ClipStart, M->ClipEnd, U), 1000.f, B, &OutBlend);
		}
		break;
	}
	case 30: case 31:
	{
		const bool b2 = Key == 31;
		if (const FMove* M = Moves.Find(b2 ? TEXT("Jab2") : TEXT("Jab1")))
		{
			const float U = Me->AttackT / FMath::Max(Me->AttackDur, 0.01f);
			PlayAt(b2 ? TEXT("Jab2") : TEXT("Jab1"), MoveClipTime(*M, U, b2 ? Tn.Jab2From : Tn.Jab1From, b2 ? Tn.Jab2To : Tn.Jab1To), Speed, B, &OutBlend);
		}
		break;
	}
	case 32: case 37:
	{
		if (const FMove* M = Moves.Find(TEXT("Smash")))
		{
			const float D = FMath::Max(Me->AttackDur, 0.01f);
			float T = M->ClipHit;
			if (Me->Smash == ESm::Charging)
			{
				// the wind-up, then a held tremble that grows with the charge: never a frozen frame
				const float W = FMath::Clamp(ActionT / 0.3f, 0.f, 1.f);
				T = FMath::Lerp(M->ClipStart, M->ClipHit, FMath::InterpEaseOut(0.f, 1.f, W, 2.f)) + (W >= 1.f ? FMath::Sin(Time * (9.f + 10.f * Me->SmashCharge)) * (0.012f + 0.02f * Me->SmashCharge) : 0.f);
			}
			else if (Me->Smash == ESm::Swing)
				T = FMath::Lerp(M->ClipHit, M->ClipFollow, FMath::Clamp((Me->AttackT - AKnightCharacter::SmashApexT * D) / ((AKnightCharacter::SmashPauseT - AKnightCharacter::SmashApexT) * D), 0.f, 1.f));
			else
				// (the clip loops, so it reaches its end still at full speed: he slows into the stance instead of stopping dead)
				T = FMath::Lerp(M->ClipFollow, M->ClipEnd, FMath::Sin(FMath::Clamp((Me->SmashT + Me->AttackT - AKnightCharacter::SmashPauseT * D) / FMath::Max(Tn.SmashEndPause + (1.f - AKnightCharacter::SmashPauseT) * D, 0.05f), 0.f, 1.f) * HALF_PI));
			PlayAt(TEXT("Smash"), T, Speed, B, &OutBlend);
			if (Key == 37) OutBlend = 0.05f;   // (from the held wind-up straight into the swing)
		}
		break;
	}
	case 33:
		if (const FMove* M = Moves.Find(TEXT("Kick")))
			PlayAt(TEXT("Kick"), MoveClipTime(*M, Me->AttackT / FMath::Max(Me->AttackDur, 0.01f), Tn.KickActiveFrom, Tn.KickActiveTo), 1000.f, B, &OutBlend);
		break;
	case 34:
		if (const FMove* M = Moves.Find(TEXT("Sweep")))
		{
			const float U = Me->AttackT / FMath::Max(Me->AttackDur, 0.01f);
			PlayAt(TEXT("Sweep"), MoveClipTime(*M, U, Tn.SweepActiveFrom, Tn.SweepActiveTo), 1000.f, B, &OutBlend);
		}
		break;
	case 35:
		if (const FMove* M = Moves.Find(TEXT("Dash")))
		{
			const float D = FMath::Max(Me->AttackDur, 0.01f);
			// (the end lag: he draws himself back up out of the lunge, not all at once at the end)
			PlayUpper = PlayLower = 1.f - FMath::SmoothStep(0.5f, 1.f, Me->AttackT / D);
			PlayAt(TEXT("Dash"), MoveClipTime(*M, Me->AttackT / D, Tn.DashActiveFrom / D, Tn.DashActiveTo / D), 1000.f, B, &OutBlend);
		}
		break;
	case 36:
		if (const FMove* M = Moves.Find(TEXT("Rebound")))
			PlayAt(TEXT("Rebound"), FMath::Lerp(M->ClipStart, M->ClipEnd, FMath::Clamp(Me->SmashT / FMath::Max(Me->ReboundDur, 0.05f), 0.f, 1.f)), Speed, B, &OutBlend);
		break;
	case 40:
	{
		const TCHAR* Nm = bJumpRunning ? TEXT("JumpRun") : TEXT("JumpStand");
		if (const FMove* M = Moves.Find(Nm))
			PlayAt(Nm, FMath::Min(M->ClipStart + ActionT * FMath::Max(M->ClipHit, 0.1f), M->ClipEnd), 1000.f, B, &OutBlend);   // (ClipHit: its speed)
		break;
	}
	case 41:
		if (const FClip* C = Get(TEXT("FallIdle")))
		{
			Sample(*C, Time, Over, true);
			Overlay(B, Over, 0.5f, 1.f);   // (half: its arms go right up, which would throw the shield over his head)
			OutBlend = 0.3f;
		}
		break;
	case 50: case 51:
	{
		const TCHAR* Nm = Key == 51 ? TEXT("LandHard") : TEXT("Land");
		if (const FMove* M = Moves.Find(Nm))
		{
			PlayUpper = 0.35f;   // (the knees take a landing, by how fast he came down; his arms stay his own)
			PlayLower = Key == 51 ? 1.f : LandStrength;
			PlayAt(Nm, M->ClipStart + LandT * M->ClipHit, Speed, B, &OutBlend);
		}
		break;
	}
	default:   // 60-62: flinching
	{
		static const TCHAR* const Flinch[6] = { TEXT("HitLarge"), TEXT("HitBody"), TEXT("HitHead"), TEXT("HitBack"), TEXT("HitRight"), TEXT("HitLeft") };
		const TCHAR* Nm = Flinch[FMath::Clamp(HitPick, 0, 5)];
		if (const FMove* M = Moves.Find(Nm))
			PlayAt(Nm, FMath::Min(M->ClipStart + ActionT * FMath::Max(M->ClipHit, 0.1f), M->ClipEnd), 1000.f, B, &OutBlend);
		break;
	}
	}
	return Key;
}

// ---------------- Gear on the new bones ----------------

// Where bone B sits in the game's frame (forward +X, right +Y, up +Z; origin between his feet), for pose P
FTransform FKnightAnimator::BindInGame(const FPose& P, int32 B) const
{
	FQuat R;
	const FVector Pos = BonePos(P, B, &R);
	return FTransform(R, Pos) * FTransform(FRotator(0.f, MeshYawDeg, 0.f));
}

void FKnightAnimator::SolveGear(AKnightCharacter* Me)
{
	bGearReady = true;
	FPose P;
	P.Rot = RefRot;
	P.Hip = RefPos[Bone[Hips]];
	// The sword hand. The old wrist's frame was the game's own at rest: the blade leaves the fist forward past the thumb (KnightDesign::BladeDir).
	// Turned as the forearm's rest direction differs, and moved so the grip sits in the new fist.
	{
		const FTransform Hand = BindInGame(P, RHand), Fore = BindInGame(P, RFore);
		const FVector NewF = (Hand.GetLocation() - Fore.GetLocation()).GetSafeNormal();
		const FVector OldF = (KnightDesign::WristPos(1) - KnightDesign::PivotPos(KnightDesign::P_ElbowR)).GetSafeNormal();
		const FQuat Q = FQuat::FindBetweenNormals(OldF, NewF);
		const FVector G = KnightDesign::GripPos(false) - KnightDesign::PivotPos(KnightDesign::P_WristR);
		FVector Along(7.f, 0.f, 0.f); FRotator Turn(0.f, 0.f, 0.f);
		FString S; TArray<FString> Pr;   // (test pictures: -SuitGrip=Along_Side_Fwd_Pitch_Yaw_Roll tries another grip without a rebuild)
		if (FParse::Value(FCommandLine::Get(), TEXT("SuitGrip="), S) && S.ParseIntoArray(Pr, TEXT("_")) == 6)
		{ Along = FVector(FCString::Atof(*Pr[0]), FCString::Atof(*Pr[1]), FCString::Atof(*Pr[2])); Turn = FRotator(FCString::Atof(*Pr[3]), FCString::Atof(*Pr[4]), FCString::Atof(*Pr[5])); }
		const FVector Fist = Hand.GetLocation() + NewF * Along.X + FVector(0.f, Along.Y, 0.f) + FVector(Along.Z, 0.f, 0.f);
		const FQuat Rot = Turn.Quaternion() * Q;
		RightGrip = FTransform(Rot, Fist - Rot.RotateVector(G)) * Hand.Inverse();
		// The fist as Meshy made it is turned about the wrist against the Mixamo hand the clips were made on (his arms are his own: 'a pair of bent
		// arms as Meshy made them'). In the sword clips the hand holds its sword past the thumb, so here the hand is turned about its own length
		// to put the fist's grip where theirs is, found by measuring eight swings: which way out of the hand fits the way the hand travels out from the chest (19 degrees from this, averaged over forehands and backhands). Not in his rest pose,
		// walk and run: those were made on his own skeleton.
		BladeInHand = RightGrip.TransformVectorNoScale(KnightDesign::BladeDir(1)).GetSafeNormal();
		float TwistDeg = 126.f;
		FParse::Value(FCommandLine::Get(), TEXT("SuitTwist="), TwistDeg);
		const FQuat Twist(FVector::YAxisVector, FMath::DegreesToRadians(TwistDeg));
		const int32 NB = Parents.Num(), HandB = Bone[RHand];
		for (TPair<FString, FClip*>& It : Clips)
		{
			if (It.Key == TEXT("Rest") || It.Key == TEXT("Walking") || It.Key == TEXT("Running")) continue;
			FClip& C = *It.Value;
			for (int32 f = 0; f < C.NumFrames; ++f) C.Rot[f * NB + HandB] = (C.Rot[f * NB + HandB] * Twist).GetNormalized();
		}
	}
	// The shield: at rest on the outside of the forearm as the old rest pose had it; raised, facing straight ahead in front of the forearm at the guard pose
	{
		const FVector Strap(14.f, -41.f, -8.f);
		const FClip* RestC = Get(TEXT("Rest"));
		if (RestC) Sample(*RestC, 0.f, P, false);
		{
			const FTransform Fore = BindInGame(P, LFore), Hand = BindInGame(P, LHand);
			const FVector Mid = FMath::Lerp(Fore.GetLocation(), Hand.GetLocation(), 0.55f);
			const FQuat Q = ::ShieldRest().InArm.GetRotation();
			const FVector Face = Q.RotateVector(FVector::ForwardVector);
			const FVector At = Mid + Face * 11.f;
			ShieldAtRest = FTransform(Q, At - Q.RotateVector(Strap)) * Fore.Inverse();
		}
		const FClip* GuardC = Get(TEXT("BlockIdle"));
		if (GuardC) Sample(*GuardC, 0.f, P, true);
		{
			const FTransform Fore = BindInGame(P, LFore), Hand = BindInGame(P, LHand);
			const FVector Mid = FMath::Lerp(Fore.GetLocation(), Hand.GetLocation(), 0.55f);
			const FQuat Q = FRotator(0.f, -18.f, 0.f).Quaternion();   // (a little turned to his left: it covers the side he leads with)
			const FVector Face = Q.RotateVector(FVector::ForwardVector);
			const FVector At = Mid + Face * 11.f;
			ShieldAtGuard = FTransform(Q, At - Q.RotateVector(Strap)) * Fore.Inverse();
		}
	}
	// The bow: at rest as the old rest pose had it, in the left fist; aiming upright with the string toward the drawing hand. X is along the limbs, Z toward the string.
	{
		const FClip* RestC = Get(TEXT("Rest"));
		if (RestC) Sample(*RestC, 0.f, P, false);
		{
			const FTransform Hand = BindInGame(P, LHand), Fore = BindInGame(P, LFore);
			const FVector Dir = (Hand.GetLocation() - Fore.GetLocation()).GetSafeNormal();
			BowAtRest = FTransform(FQuat(Me->BowRestQuat()), Hand.GetLocation() + Dir * 7.f) * Hand.Inverse();
		}
		const FClip* AimC = Get(TEXT("AimIdle"));
		if (AimC) Sample(*AimC, AimC->Len * 0.5f, P, true);
		{
			const FTransform Hand = BindInGame(P, LHand), Fore = BindInGame(P, LFore), Other = BindInGame(P, RHand);
			const FVector Dir = (Hand.GetLocation() - Fore.GetLocation()).GetSafeNormal();
			const FVector At = Hand.GetLocation() + Dir * 7.f;
			const FVector ToString = (Other.GetLocation() - At).GetSafeNormal();
			const FVector Up = (FVector::UpVector - ToString * FVector::DotProduct(FVector::UpVector, ToString)).GetSafeNormal();
			const FQuat Q = FRotationMatrix::MakeFromXZ(Up, ToString).ToQuat();
			BowAtAim = FTransform(Q, At) * Hand.Inverse();
		}
	}
	P.Rot = RefRot;
	P.Hip = RefPos[Bone[Hips]];
	// The quiver: where the old hips carried it, on the new hips (their rest heights differ little)
	{
		const FTransform Hip = BindInGame(P, Hips);
		const FVector Old = KnightDesign::PivotPos(KnightDesign::P_Body);
		const float Half = Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		QuiverOnHips = FTransform(FRotator(-5.f, 0.f, -6.f), FVector(-22.f, 24.f, -29.f) + Old + FVector(0.f, 0.f, Half)) * Hip.Inverse();
	}
}

void FKnightAnimator::HideOldBody(AKnightCharacter* Me, bool bHide)
{
	if (Me->Skin.IsValid())
		for (int32 s = 0; s < KnightModel::Set_Count; ++s)
			if (USkinnedMeshComponent* C = Me->Skin->Sets[s]) C->SetVisibility(!bHide);
	if (bHide)
	{
		for (FArmorPart& P : Me->ArmorParts)
			if (P.Comp && P.Slot != KnightDesign::SlotShield) P.Comp->SetVisibility(false);
		if (Me->CapeCloth) Me->CapeCloth->SetVisibility(false);
	}
	else Me->RefreshEquipment();
}

void FKnightAnimator::PlaceGear(AKnightCharacter* Me)
{
	if (!Comp || !Me) return;
	if (!bGearReady) SolveGear(Me);
	HideOldBody(Me, true);
	const FTransform Mesh = Comp->GetComponentTransform();
	const FTransform GameToWorld = FTransform(FRotator(0.f, -MeshYawDeg, 0.f)) * Mesh;   // (the game's frame in the world: mesh space with the yaw taken back out)
	auto InWorld = [&](int32 B) { return BindInGame(Final, B) * GameToWorld; };

	// The right hand: the old wrist joint follows it, so the weapon, its trail and its hit boxes all come along
	if (Me->WristR)
	{
		if (!bWristSaved) { WristRel = Me->WristR->GetRelativeLocation(); WristRelRot = Me->WristR->GetRelativeRotation().Quaternion(); bWristSaved = true; }
		Me->WristR->SetWorldTransform(RightGrip * InWorld(RHand));
	}
	// The shield, strapped to the forearm: rest to guard by how far it is raised
	if (!Me->bStowed)
	{
		FTransform Strap;
		Strap.Blend(ShieldAtRest, ShieldAtGuard, Me->BlockAlpha);
		const FTransform At = Strap * InWorld(LFore);
		for (FArmorPart& P : Me->ArmorParts)
			if (P.Slot == KnightDesign::SlotShield && P.Comp) P.Comp->SetWorldTransform(At);
	}
	// The kicking leg's old joints follow the new leg: the kick's hit boxes read them
	if (Me->KneeR && Me->AnkleR)
	{
		if (!bLegSaved) { KneeRel = Me->KneeR->GetRelativeLocation(); AnkleRel = Me->AnkleR->GetRelativeLocation(); bLegSaved = true; }
		Me->KneeR->SetWorldLocation(InWorld(RLeg).GetLocation());
		Me->AnkleR->SetWorldLocation(InWorld(RFoot).GetLocation());
		Me->AnkleR->SetWorldRotation(FRotator(0.f, Comp->GetComponentRotation().Yaw - MeshYawDeg, 0.f));
	}
	// The bow in the left fist
	if (Me->BowHold)
	{
		FTransform Hold;
		Hold.Blend(BowAtRest, BowAtAim, Me->BowAimAlpha);
		Me->BowHold->SetWorldTransform(Hold * InWorld(LHand));
	}
	Me->UpdateBowBits();
	// The quiver on the hips
	if (Me->QuiverParts.Num() > 0 && Me->QuiverParts[0])
		if (USceneComponent* Q = Me->QuiverParts[0]->GetAttachParent())
		{
			if (!bQuiverSaved) { QuiverRel = Q->GetRelativeTransform(); bQuiverSaved = true; }
			Q->SetWorldTransform(QuiverOnHips * InWorld(Hips));
		}
}

void FKnightAnimator::ReleaseGear(AKnightCharacter* Me)
{
	if (!Me) return;
	HideOldBody(Me, false);
	if (Me->WristR && bWristSaved) { Me->WristR->SetRelativeLocationAndRotation(WristRel, WristRelRot); bWristSaved = false; }
	if (bLegSaved && Me->KneeR && Me->AnkleR) { Me->KneeR->SetRelativeLocation(KneeRel); Me->AnkleR->SetRelativeLocation(AnkleRel); }
	bLegSaved = false;
	YawNow = 0.f;
	if (bQuiverSaved && Me->QuiverParts.Num() > 0 && Me->QuiverParts[0])
		if (USceneComponent* Q = Me->QuiverParts[0]->GetAttachParent()) Q->SetRelativeTransform(QuiverRel);
	bQuiverSaved = false;
	StateKey = -1;
	bHavePrev = false;
	bBlending = false;
}
