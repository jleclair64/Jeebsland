// The playable troll's body (2026-11-19, Docs/TrollPlayablePlan.md).
//
// How the knight's moves reach the troll: both are Meshy rigs with the same 24 bones under other names (Tools/troll_rig_export.py
// wrote Content/Data/TrollRig.csv from the two .glb files). The troll's FClipLib (GetTroll) is the knight's own: the same bones, the
// same bone frames and the same clips, with only the bones' lengths and places changed to the troll's. So every move, hit box, grip
// and foot plant the animator works out comes out troll-sized, with no second set of clips. The troll's mesh (SK_Troll, its own
// skeleton) then copies the pose bone by bone: each bone is turned in the mesh's space by as much as ours has turned from the bind
// pose (TrollPose). Since our bind pose has exactly the troll's joint places, his joints land exactly where ours are.
// This was chosen over an IK Retargeter asset (the animator is code, not an Anim Blueprint, and reads no engine animation at run
// time: a cooked server must have the same pose) and over a Mixamo re-rig (same bones already; nothing to gain).
#include "KnightAnimator.h"
#include "JeebTuning.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

using namespace JeebAnim;

namespace
{
	// The 24 turns that take the axes onto the axes (no mirror)
	const TArray<FQuat>& AxisTurns()
	{
		static TArray<FQuat> Out;
		if (Out.Num()) return Out;
		const FVector Ax[3] = { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector };
		for (int32 i = 0; i < 3; ++i)
			for (int32 j = 0; j < 3; ++j)
			{
				if (j == i) continue;
				for (int32 si = -1; si <= 1; si += 2)
					for (int32 sj = -1; sj <= 1; sj += 2)
					{
						const FVector X = Ax[i] * si, Y = Ax[j] * sj, Z = FVector::CrossProduct(X, Y);
						FMatrix M = FMatrix::Identity;
						M.SetAxes(&X, &Y, &Z);
						Out.Add(FQuat(M));
					}
			}
		return Out;
	}
	// The turn (of AxisTurns) and scale that best lay the points A on the points B, about their middles; returns the mean miss (cm)
	float FitAxes(const TArray<FVector>& A, const TArray<FVector>& B, bool bScale, FQuat& OutQ, float& OutS, FVector& OutT)
	{
		const int32 N = A.Num();
		FVector MA = FVector::ZeroVector, MB = FVector::ZeroVector;
		for (int32 i = 0; i < N; ++i) { MA += A[i]; MB += B[i]; }
		MA /= FMath::Max(N, 1); MB /= FMath::Max(N, 1);
		double SA = 0.0, SB = 0.0;
		for (int32 i = 0; i < N; ++i) { SA += (A[i] - MA).SizeSquared(); SB += (B[i] - MB).SizeSquared(); }
		OutS = bScale && SA > 1e-8 ? float(FMath::Sqrt(SB / SA)) : 1.f;
		double Best = 1e30;
		for (const FQuat& Q : AxisTurns())
		{
			double E = 0.0;
			for (int32 i = 0; i < N; ++i) E += (Q.RotateVector((A[i] - MA) * OutS) - (B[i] - MB)).Size();
			if (E < Best) { Best = E; OutQ = Q; }
		}
		OutT = MB - OutQ.RotateVector(MA * OutS);
		return float(Best / FMath::Max(N, 1));
	}
	// A mirror front to back (Y): a point, and a turn seen in the mirror
	FVector MirP(const FVector& P) { return FVector(P.X, -P.Y, P.Z); }
	FQuat MirQ(const FQuat& Q) { return FQuat(-Q.X, Q.Y, -Q.Z, Q.W); }
	FString Short(const FString& Name) { int32 C; return Name.FindLastChar(TEXT(':'), C) ? Name.Mid(C + 1) : Name; }
}

void FClipLib::ComputeRest()
{
	const int32 N = Parents.Num();
	RestPos.SetNum(N); RestRot.SetNum(N);
	for (int32 b = 0; b < N; ++b)
	{
		const int32 p = Parents[b];
		if (p < 0) { RestRot[b] = RefRot[b]; RestPos[b] = RefPos[b]; }
		else { RestRot[b] = RestRot[p] * RefRot[b]; RestPos[b] = RestPos[p] + RestRot[p].RotateVector(RefPos[b]); }
	}
}

const FClipLib& FClipLib::GetTroll()
{
	static const FClipLib Lib(FClipLib::Get(), true);
	return Lib;
}

FClipLib::FClipLib(const FClipLib& K, bool)
{
	bTroll = true;
	for (int32 i = 0; i < B_Count; ++i) Bone[i] = INDEX_NONE;
	if (!K.bReady) return;
	BoneName = K.BoneName; Parents = K.Parents; RefRot = K.RefRot; RefPos = K.RefPos;
	const int32 N = Parents.Num();

	// The rig file: each bone's place in the knight's Blender rig and in the troll's
	TMap<FString, FVector> KB, TB;
	TMap<FString, FString> TName;
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectContentDir() / TEXT("Data/TrollRig.csv")))) { UE_LOG(LogTemp, Error, TEXT("JEEBTROLL no Content/Data/TrollRig.csv")); return; }
	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		TArray<FString> F;
		Lines[i].ParseIntoArray(F, TEXT(","), false);
		if (F.Num() < 8 || F[0].StartsWith(TEXT("#"))) continue;
		KB.Add(F[0], FVector(FCString::Atof(*F[1]), FCString::Atof(*F[2]), FCString::Atof(*F[3])) * 100.f);
		TB.Add(F[0], FVector(FCString::Atof(*F[5]), FCString::Atof(*F[6]), FCString::Atof(*F[7])) * 100.f);
		TName.Add(F[0], F[4]);
	}
	// Blender's axes in our mesh's space: the turn that lays the knight's Blender joints on his joints here
	TArray<FVector> A, B;
	TArray<int32> Matched;
	int32 HipB = INDEX_NONE;
	for (int32 b = 0; b < N; ++b)
	{
		const FString S = Short(BoneName[b]);
		if (S == TEXT("Hips")) HipB = b;
		if (const FVector* P = KB.Find(S)) { A.Add(*P); B.Add(K.RestPos[b]); Matched.Add(b); }
	}
	if (HipB == INDEX_NONE || Matched.Num() < 20) { UE_LOG(LogTemp, Error, TEXT("JEEBTROLL the rig file does not fit the knight's bones (%d matched)"), Matched.Num()); return; }
	FQuat Q; float Sc; FVector T;
	const float Miss = FitAxes(A, B, false, Q, Sc, T);
	// The troll's joints in our mesh's space, his hips over the knight's (between the feet)
	TArray<FVector> TP;
	TP.Init(FVector::ZeroVector, N);
	TArray<bool> Has;
	Has.Init(false, N);
	TrollBone.Init(FString(), N);
	for (int32 b : Matched) { const FString S = Short(BoneName[b]); TP[b] = Q.RotateVector(TB[S]) + T; Has[b] = true; TrollBone[b] = TName[S]; }
	const FVector Shift(K.RestPos[HipB].X - TP[HipB].X, K.RestPos[HipB].Y - TP[HipB].Y, 0.f);
	for (int32 b : Matched) TP[b] += Shift;
	const float S = TP[HipB].Z / FMath::Max(K.RestPos[HipB].Z, 1.f);
	// The bones' lengths and places: the troll's, in the knight's bone frames
	for (int32 b = 0; b < N; ++b)
	{
		const int32 p = Parents[b];
		if (p < 0) RefPos[b] = Has[b] ? TP[b] : K.RefPos[b] * S;
		else if (Has[b] && Has[p]) RefPos[b] = K.RestRot[p].UnrotateVector(TP[b] - TP[p]);
		else RefPos[b] = K.RefPos[b] * S;
	}
	// The same clips; the hips' path scaled about the bind pose
	const FVector KH = K.RefPos[HipB], TH = RefPos[HipB];
	for (const TUniquePtr<FClip>& Src : K.ClipStore)
	{
		TUniquePtr<FClip> C = MakeUnique<FClip>(*Src);
		for (FVector& H : C->HipPos) H = TH + (H - KH) * S;
		if (C->Name == TEXT("Walking")) for (FVector& H : C->HipPos) H.Z += 3.9f;   // (Finalize lowers his walk 3.9 cm again; the knight's was already lowered, and scaled above)
		FClip* Raw = C.Get();
		ClipStore.Add(MoveTemp(C));
		Clips.Add(Raw->Name, Raw);
	}
	HurtScale = JeebTune::V().TrollHurtScale;
	Finalize();
	bReady = Clips.Contains(TEXT("Rest"));
	UE_LOG(LogTemp, Log, TEXT("JEEBTROLL rig: knight's Blender joints fit his own to %.2f cm; troll %.2fx the knight at the hips, hips %.0f cm up, head top %.0f cm; %d clips, ready %d"),
		Miss, S, TP[HipB].Z, RestPos.Num() > 0 ? RestPos[Bone[Head] != INDEX_NONE ? Bone[Head] : 0].Z : 0.f, Clips.Num(), bReady ? 1 : 0);
}

// ---------------- The troll's own mesh ----------------

void FKnightAnimator::MakeTrollMesh(AActor* Owner)
{
	USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/People/SK_Troll.SK_Troll"));
	if (!Asset || !Comp) { UE_LOG(LogTemp, Error, TEXT("JEEBTROLL no troll mesh (/Game/People/SK_Troll)")); return; }
	const FReferenceSkeleton& R = Asset->GetRefSkeleton();
	const int32 M = R.GetNum();
	TArray<FTransform> CS;
	CS.SetNum(M);
	for (int32 m = 0; m < M; ++m)
	{
		const int32 p = R.GetParentIndex(m);
		CS[m] = p >= 0 ? R.GetRefBonePose()[m] * CS[p] : R.GetRefBonePose()[m];
	}
	TrollFrom.Init(INDEX_NONE, M);
	TArray<FVector> A, B;
	for (int32 b = 0; b < L.TrollBone.Num(); ++b)
	{
		if (L.TrollBone[b].IsEmpty()) continue;
		const int32 m = R.FindBoneIndex(FName(*L.TrollBone[b]));
		if (m == INDEX_NONE) continue;
		TrollFrom[m] = b;
		A.Add(CS[m].GetLocation());
		B.Add(L.RestPos[b]);
	}
	// The mesh's own space (as it was imported) laid on ours: a turn of the axes, a scale and an offset
	FQuat Q = FQuat::Identity; float S = 1.f; FVector T = FVector::ZeroVector;
	float Miss = FitAxes(A, B, true, Q, S, T);
	// (the troll was imported mirrored front to back against the knight: try his points mirrored too, and keep whichever fits)
	TArray<FVector> AM = A;
	for (FVector& P : AM) P = MirP(P);
	FQuat QM; float SM; FVector TM;
	const float MissM = FitAxes(AM, B, true, QM, SM, TM);
	bTrollMirror = MissM < Miss;
	if (bTrollMirror) { Miss = MissM; Q = QM; S = SM; T = TM; }
	TrollQ = Q;
	TrollRestK.SetNum(M);
	for (int32 m = 0; m < M; ++m) TrollRestK[m] = Q * (bTrollMirror ? MirQ(CS[m].GetRotation()) : CS[m].GetRotation());
	UE_LOG(LogTemp, Log, TEXT("JEEBTROLL mesh: %d bones, %d follow ours; fits our bind pose to %.2f cm at scale %.3f, mirrored %d, turn %s"), M, A.Num(), Miss, S, bTrollMirror ? 1 : 0, *Q.Rotator().ToCompactString());

	TrollComp = NewObject<UPoseableMeshComponent>(Owner);
	TrollComp->SetupAttachment(Comp);
	TrollComp->SetRelativeTransform(FTransform(Q, T, FVector(S, bTrollMirror ? -S : S, S)));
	TrollComp->SetSkinnedAssetAndUpdate(Asset);
	TrollComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TrollComp->SetBoundsScale(2.5f);
	TrollComp->RegisterComponent();
	Comp->SetVisibility(false, false);   // (the knight's mesh keeps the pose for whatever reads it; only the troll is drawn)
}

void FKnightAnimator::TrollPose()
{
	const USkinnedAsset* Asset = TrollComp->GetSkinnedAsset();
	if (!Asset) return;
	const FReferenceSkeleton& R = Asset->GetRefSkeleton();
	const int32 M = R.GetNum(), N = Parents.Num();
	TArray<FTransform>& Bones = TrollComp->BoneSpaceTransforms;
	if (Bones.Num() != M || TrollFrom.Num() != M) return;
	TArray<FQuat, TInlineAllocator<32>> W;
	W.SetNum(N);
	for (int32 b = 0; b < N; ++b) W[b] = Parents[b] >= 0 ? W[Parents[b]] * Final.Rot[b] : Final.Rot[b];
	const FQuat QInv = TrollQ.Inverse();
	const FTransform Rel = TrollComp->GetRelativeTransform();
	auto MirIf = [this](const FQuat& Q) { return bTrollMirror ? MirQ(Q) : Q; };
	TArray<FTransform, TInlineAllocator<64>> CT;
	CT.SetNum(M);
	for (int32 m = 0; m < M; ++m)
	{
		const int32 p = R.GetParentIndex(m);
		const FTransform& Ref = R.GetRefBonePose()[m];
		const int32 b = TrollFrom[m];
		const FQuat Rot = b != INDEX_NONE ? MirIf((QInv * (W[b] * L.RestRot[b].Inverse() * TrollRestK[m])).GetNormalized()) : (p >= 0 ? CT[p].GetRotation() * Ref.GetRotation() : Ref.GetRotation());
		const FVector Pos = (b != INDEX_NONE && b == Bone[Hips]) ? Rel.InverseTransformPosition(Final.Hip) : (p >= 0 ? CT[p].TransformPosition(Ref.GetLocation()) : Ref.GetLocation());
		CT[m] = FTransform(Rot, Pos);
		const FTransform Local = p >= 0 ? CT[m].GetRelativeTransform(CT[p]) : CT[m];
		Bones[m].SetRotation(Local.GetRotation());
		Bones[m].SetTranslation(Local.GetLocation());
		Bones[m].SetScale3D(FVector::OneVector);
	}
	TrollComp->MarkRefreshTransformDirty();
	TrollComp->RefreshBoneTransforms();
}

// The troll's left punch, in the kick's place and on the kick's timing (Tuning.ini [Kick]): the sword's guard stance underneath, the
// left fist pulled back a little through the wind-up, driven straight out ahead of the left shoulder over the hit frames with the chest
// turning in behind it, held, then brought back to the guard. The hit boxes ride the left fist and forearm (TickHitboxes).
void FKnightAnimator::TrollPunch(const FInput& In, FPose& B, float& OutBlend)
{
	const auto& Tn = JeebTune::V();
	if (const FClip* G = Get(TEXT("Idle_SS"))) { Sample(*G, IdleT, Over, true); Overlay(B, Over, 1.f, 1.f); }
	const float U = In.MoveU, From = Tn.KickActiveFrom, To = Tn.KickActiveTo;
	const float Out = From + 0.35f * (To - From);   // (fully out a third of the way into the hit frames)
	float Ext;   // -1 pulled back, 0 at the guard, 1 the arm out straight
	if (U < From) Ext = -FMath::SmoothStep(0.f, From, U);
	else if (U < Out) Ext = FMath::Lerp(-1.f, 1.f, FMath::SmoothStep(From, Out, U));
	else { const float Hold = Out + 0.15f * (To - Out); Ext = U < Hold ? 1.f : 1.f - FMath::SmoothStep(Hold, 1.f, U); }   // (a moment at full stretch, then eased back: review 2026-11-19, it hung out too long)
	// the chest turns the left shoulder forward with the blow (mesh: +X his left, +Y ahead), back a little in the wind-up
	const float Twist = Ext >= 0.f ? Tn.TrollPunchTwist * Ext : -0.4f * Tn.TrollPunchTwist * -Ext;
	const FQuat Half(FVector::UpVector, FMath::DegreesToRadians(0.5f * Twist));
	TurnInMesh(B, Spine1, Half);
	TurnInMesh(B, Spine2, Half);
	const FVector Sh = BonePos(B, LArm);
	const float Arm = float(RefPos[Bone[LFore]].Size() + RefPos[Bone[LHand]].Size());
	const FVector Guard = Sh + FVector(-0.15f, 0.45f, -0.2f) * Arm;   // (in front of his chest, toward the middle)
	const FVector Back = Sh + FVector(-0.05f, 0.25f, -0.25f) * Arm;
	const FVector Hit = Sh + FVector(-0.15f, Tn.TrollPunchReach, -0.05f) * Arm;
	const FVector Target = Ext >= 0.f ? FMath::Lerp(Guard, Hit, Ext) : FMath::Lerp(Guard, Back, -Ext);
	L.ReachTo(B, LArm, LFore, LHand, Target, FVector(0.6f, -0.3f, -1.f));   // (the elbow out and down)
	OutBlend = 0.08f;
}
