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
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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
	// The mesh's own space (as it was imported) laid on ours: a turn of the axes, a scale and an offset. (2026-11-21, owner: "the troll
	// is facing the wrong direction", "his hand behind his back".) Meshy rigged him back to front: his toe bones and head-front marker
	// point out of his back. Fitted with them, the joints matched only mirrored front to back (a -1 Y scale), which showed his skin
	// back to front: we saw his back as he walked at us, and an arm swung forward looked like it was behind him. Now those three markers
	// are left out of the fit: a plain turn (no mirror, 13 cm mean miss from his own slightly different build) puts his face ahead.
	// (-TrollOldMirror: the old fit, for before and after pictures.)
	auto Build = [&](bool bSwap, TArray<FVector>& OutA, TArray<FVector>& OutB, TArray<int32>& OutFrom)
	{
		OutA.Reset(); OutB.Reset(); OutFrom.Init(INDEX_NONE, M);
		for (int32 b = 0; b < L.TrollBone.Num(); ++b)
		{
			if (L.TrollBone[b].IsEmpty()) continue;
			FString Name = L.TrollBone[b];
			if (bSwap)
			{
				if (Name.Contains(TEXT("Left"))) Name = Name.Replace(TEXT("Left"), TEXT("Right"));
				else if (Name.Contains(TEXT("Right"))) Name = Name.Replace(TEXT("Right"), TEXT("Left"));
			}
			const int32 m = R.FindBoneIndex(FName(*Name));
			if (m == INDEX_NONE) continue;
			OutFrom[m] = b;
			if (Name.Contains(TEXT("Toe")) || Name.Contains(TEXT("front")) || Name.Contains(TEXT("_end"))) continue;   // (his rig points these backwards: see above)
			OutA.Add(CS[m].GetLocation());
			OutB.Add(L.RestPos[b]);
		}
	};
	TArray<FVector> A, B; TArray<int32> From;
	Build(false, A, B, From);
	FQuat Q = FQuat::Identity; float S = 1.f; FVector T = FVector::ZeroVector;
	float Miss = FitAxes(A, B, true, Q, S, T);
	bTrollMirror = false;
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("TrollOldMirror")))   // (test pictures only: the old mirrored fit, before and after)
	{
		for (FVector& P : A) P = MirP(P);
		Miss = FitAxes(A, B, true, Q, S, T);
		bTrollMirror = true;
	}
#endif
	const bool bSwapped = false;
	TrollFrom = From;
	TrollQ = Q;
	TrollRestK.SetNum(M);
	for (int32 m = 0; m < M; ++m) TrollRestK[m] = Q * (bTrollMirror ? MirQ(CS[m].GetRotation()) : CS[m].GetRotation());
	UE_LOG(LogTemp, Log, TEXT("JEEBTROLL mesh: %d bones, %d follow ours; fits our bind pose to %.2f cm at scale %.3f, sides swapped %d, mirrored %d, turn %s"), M, A.Num(), Miss, S, bSwapped ? 1 : 0, bTrollMirror ? 1 : 0, *Q.Rotator().ToCompactString());

	TrollComp = NewObject<UPoseableMeshComponent>(Owner);
	TrollComp->SetupAttachment(Comp);
	TrollComp->SetRelativeTransform(FTransform(Q, T, FVector(S, bTrollMirror ? -S : S, S)));
	TrollComp->SetSkinnedAssetAndUpdate(Asset);
	TrollComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TrollComp->SetBoundsScale(2.5f);
	TrollComp->RegisterComponent();
	Comp->SetVisibility(false, false);   // (the knight's mesh keeps the pose for whatever reads it; only the troll is drawn)
}

// The troll's own shadow on himself (2026-11-21, owner: his body "isn't crisp"): the soft-shadow filter (PCSS) scatters its samples per
// pixel and leans on TSR history; on his big matte body the wide penumbra of his head and arms on his shoulders and back showed as a moving
// stipple whenever he moved. A per-object (inset) shadow map is drawn at a much finer resolution, so the same kernel covers a far narrower
// penumbra on him. 0 = the plain whole-scene shadow as before. Live; looks only.
static TAutoConsoleVariable<int32> CVarTrollInset(TEXT("jeeb.Troll.InsetShadow"), 1,
	TEXT("1: the troll casts a per-object (inset) shadow, finer on himself, so no soft-shadow stipple on his body; 0: the whole-scene shadow as before."), ECVF_Default);

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
	const bool bInset = CVarTrollInset.GetValueOnGameThread() != 0;
	if (TrollComp->bCastInsetShadow != bInset) TrollComp->SetCastInsetShadow(bInset);
}

// The troll's charged jump in the air (review 2026-11-19: he went straight from the crouch into the tuck). First a take-off stretch:
// the legs pushed out straight under and a little behind him, the arms swung up and forward; then over about a quarter second the
// stretch eases into the tuck (the knees drawn up, more for a bigger charge); the landing is the usual knee give. Returns the tuck share
// for the FallLand overlay. (mesh space: +X his left, +Y ahead, +Z up)
float FKnightAnimator::TrollLeapAir(const FInput& In, FPose& B)
{
	const float T = AirT;
	const float S = FMath::SmoothStep(0.f, 0.06f, T) * (1.f - FMath::SmoothStep(0.16f, 0.38f, T));   // the stretch
	const float Tuck = FMath::SmoothStep(0.16f, 0.42f, T) * (0.4f + 0.3f * FMath::Clamp(In.LeapFly, 0.f, 1.f));
	if (S > 0.005f)
	{
		// legs: the feet straight down from the hips and back, the left a little further (the last to leave the ground)
		const float Leg = float(RefPos[Bone[LLeg]].Size() + RefPos[Bone[LFoot]].Size());
		auto Straight = [&](int32 Up, int32 Mid, int32 End, float Back)
		{
			const FVector Hip = BonePos(B, Up), Now = BonePos(B, End);
			const FVector Want = Hip + FVector(0.f, -Back, -0.96f).GetSafeNormal() * Leg * 0.97f;
			L.ReachTo(B, Up, Mid, End, FMath::Lerp(Now, Want, S), FVector(0.f, 1.f, 0.f));   // (the knee ahead)
		};
		Straight(LUp, LLeg, LFoot, 0.32f);
		Straight(RUp, RLeg, RFoot, 0.12f);
		// arms: swung up and forward from the shoulders, elbows out
		const float Arm = float(RefPos[Bone[LFore]].Size() + RefPos[Bone[LHand]].Size());
		auto Swing = [&](int32 Up, int32 Mid, int32 End, float Side)
		{
			const FVector Sh = BonePos(B, Up), Now = BonePos(B, End);
			const FVector Want = Sh + FVector(0.3f * Side, 0.5f, 0.62f) * Arm;
			L.ReachTo(B, Up, Mid, End, FMath::Lerp(Now, Want, S), FVector(Side, -0.3f, -0.4f));
		};
		Swing(LArm, LFore, LHand, 1.f);
		Swing(RArm, RFore, RHand, -1.f);
		// the chest lifts and opens a little
		TurnInMesh(B, Spine2, FQuat(FVector::XAxisVector, FMath::DegreesToRadians(6.f * S)));   // (about +X, his left: the chest's top goes back)
	}
	return Tuck;
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

// The troll's club blows (owner 2026-11-21: in the sword's swing the club's follow-through crossed his belly). Made from the club's own
// poses instead of the sword's clips, in the same place and on the same timing (Jab1 / Jab2 rows of Tuning.ini: From..To are the hit
// frames, past 1 the still moment after), so the server's hit boxes (they ride the blade bone) and every other machine read the same
// MoveU. Swing 1: an overhead blow from over the right shoulder, down and forward, the club ending low and out beside the right hip.
// Swing 2: a flat sweep from the right side across the front, at arm's length ahead, ending out on the left. Both hands hold the haft:
// the right on the grip, the left a hand's breadth up it. The hand sits at least a little way ahead of or outside his middle through the
// whole blow, so the haft (125 cm) never lies through his belly. Mesh space: +X his left, +Y ahead, +Z up. Hand places are in arm lengths
// from the right shoulder, club directions are unit vectors; Twist is the chest's turn in degrees (+ to the right, right shoulder back).
namespace
{
	struct FClubKey { FVector Hand; FVector Dir; float Twist; };
	static FClubKey LerpKey(const FClubKey& A, const FClubKey& B, float W)
	{
		return { FMath::Lerp(A.Hand, B.Hand, W), FQuat::Slerp(FQuat::FindBetweenNormals(FVector::ForwardVector, A.Dir.GetSafeNormal()), FQuat::FindBetweenNormals(FVector::ForwardVector, B.Dir.GetSafeNormal()), W).RotateVector(FVector::ForwardVector), FMath::Lerp(A.Twist, B.Twist, W) };
	}
}

float FKnightAnimator::TrollClubSwing(const FInput& In, FPose& B, float& OutBlend, bool b2)
{
	const auto& Tn = JeebTune::V();
	if (const FClip* G = Get(TEXT("Idle_SS"))) { Sample(*G, IdleT, Over, true); Overlay(B, Over, 1.f, 1.f); }
	const float U = In.MoveU;
	const float From = b2 ? Tn.Jab2From : Tn.Jab1From, To = b2 ? Tn.Jab2To : Tn.Jab1To;
	const float UEnd = 1.f + (b2 ? Tn.Jab2Pause : Tn.Jab1Pause) / FMath::Max(b2 ? Tn.Jab2Share : Tn.Jab1Share, 0.05f);
	static const FClubKey Rest   = { FVector(-0.25f, 0.45f, -0.05f), FVector(-0.1f, 0.35f, 0.93f), 0.f };
	static const FClubKey Wind1  = { FVector(-0.35f, -0.05f, 0.80f), FVector(-0.2f, -0.75f, 0.62f), 28.f };    // over the right shoulder, the head tipped back
	static const FClubKey Hit1   = { FVector(-0.45f, 0.80f, -0.10f), FVector(-0.2f, 0.80f, -0.58f), -22.f };   // the head down in front, right of centre
	static const FClubKey End1   = { FVector(-1.00f, 0.45f, -0.45f), FVector(-0.80f, 0.45f, -0.40f), -34.f };  // out beside the right hip, head ahead and low, away from him
	static const FClubKey Wind2  = { FVector(-0.95f, 0.50f, 0.10f), FVector(-0.95f, -0.25f, 0.15f), 36.f };    // flat out on his right, the head behind
	static const FClubKey Hit2   = { FVector(-0.05f, 0.90f, 0.05f), FVector(0.90f, 0.40f, -0.05f), -14.f };    // the club across in front of him at arm's length
	static const FClubKey End2   = { FVector(0.70f, 0.60f, 0.05f), FVector(0.95f, 0.15f, -0.20f), -38.f };     // out on his left, ahead of the hip
	const FClubKey& K0 = b2 ? Wind2 : Wind1, &K1 = b2 ? Hit2 : Hit1, &K2 = b2 ? End2 : End1;
	FClubKey Now;
	if (U < From) { const float S = FMath::SmoothStep(0.f, From, U); Now = LerpKey(b2 ? End1 : Rest, K0, S); }
	else if (U < To) { const float F = (U - From) / FMath::Max(To - From, 0.01f); Now = LerpKey(K0, K1, FMath::Pow(F, 1.6f)); }   // (a heavy blow: slow to start, fastest at the hit)
	else { const float S = FMath::Clamp((U - To) / FMath::Max((UEnd - To) * 0.7f, 0.01f), 0.f, 1.f); Now = LerpKey(K1, K2, 1.f - FMath::Square(1.f - S)); }   // (it runs on and stops)
	// the chest
	const FQuat Half(FVector::UpVector, FMath::DegreesToRadians(0.5f * Now.Twist));
	TurnInMesh(B, Spine1, Half);
	TurnInMesh(B, Spine2, Half);
	// the right hand on the grip, then the club turned onto its line, then the left hand up the haft
	const FVector Sh = BonePos(B, RArm);
	const float Arm = float(RefPos[Bone[RFore]].Size() + RefPos[Bone[RHand]].Size());
	L.ReachTo(B, RArm, RFore, RHand, Sh + Now.Hand * Arm, FVector(-0.8f, -0.3f, -0.6f));
	FQuat HandRot;
	BonePos(B, RHand, &HandRot);
	const FVector Cur = HandRot.RotateVector(L.BladeInHand).GetSafeNormal();
	TurnInMesh(B, RHand, FQuat::FindBetweenNormals(Cur, Now.Dir.GetSafeNormal()));
	FQuat Turned;
	const FVector RHandAt = BonePos(B, RHand, &Turned);
	const FVector Haft = Turned.RotateVector(L.BladeInHand).GetSafeNormal();
	L.ReachTo(B, LArm, LFore, LHand, RHandAt + Haft * (0.12f * Arm + 12.f) + FVector(0.f, 0.f, 0.f), FVector(0.8f, -0.3f, -0.6f));
	OutBlend = 0.12f;
	bTrollHaft = true;   // (both hands on the haft: the left fist closes)
	return 1.f;
}
