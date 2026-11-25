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
#include "GameFramework/Actor.h"

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
	// (2026-11-22, owner: "the troll isn't actually holding the club in his hand") His Meshy hand is 50 cm long and hangs about 40 cm
	// below the wrist joint, fingers curled; the knight's fist hole is 12 cm past his wrist, so the club hung from the troll's wrist
	// outside his fingers. The hole is the middle of his curled hand, measured in Blender on Troll_rigged.glb (the right hand's
	// vertices weighted over 0.6 to RightHand, from the RightHand joint, Blender metres). -TrollFist=X_Y_Z (cm, Blender axes) tries another.
	{
		FVector Fb(-0.021f, -0.066f, -0.402f);
		Fb *= 100.f;
#if !UE_BUILD_SHIPPING
		FString FS; TArray<FString> FP;
		if (FParse::Value(FCommandLine::Get(), TEXT("TrollFist="), FS) && FS.ParseIntoArray(FP, TEXT("_")) == 3) Fb = FVector(FCString::Atof(*FP[0]), FCString::Atof(*FP[1]), FCString::Atof(*FP[2]));
#endif
		TrollFistR = Q.RotateVector(Fb);
	}
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
	TrollRel = FTransform(Q, T, FVector(S, bTrollMirror ? -S : S, S));
	TrollComp->SetRelativeTransform(TrollRel);
	TrollComp->SetSkinnedAssetAndUpdate(Asset);
	TrollComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TrollComp->SetBoundsScale(2.5f);
	TrollComp->RegisterComponent();
	Comp->SetVisibility(false, false);   // (the knight's mesh keeps the pose for whatever reads it; only the troll is drawn)
	// (2026-11-21, owner: "a lot of weird smearing in the troll when he moves and swings".) A poseable mesh also rebuilds its bones in
	// its own tick whenever it was seen lately. The troll's mesh ticked after the pawn: TrollPose built this frame's pose, then the
	// mesh's own tick built the same pose a second time, which overwrote the remembered last-frame pose. His bones then wrote no
	// motion (the velocity buffer showed his limbs still while they swung; the club, a separate mesh, was fine), so TSR blended old
	// frames of his body in as ghosts. The pawn now always ticks after his mesh: the mesh's own rebuild comes first, from last frame's
	// pose, and ours is the last word, so the last-frame pose survives for the motion vectors. (The knight's mesh happened to tick
	// first already.) -TrollOldTick (test builds) leaves the old order for before/after films.
	bool bOldTick = false;
#if !UE_BUILD_SHIPPING
	bOldTick = FParse::Param(FCommandLine::Get(), TEXT("TrollOldTick"));
#endif
	if (!bOldTick) Owner->AddTickPrerequisiteComponent(TrollComp);
#if !UE_BUILD_SHIPPING
	bTrollOldSwing = FParse::Param(FCommandLine::Get(), TEXT("TrollOldSwing"));
	bTrollKnightSwing = FParse::Param(FCommandLine::Get(), TEXT("TrollKnightSwing"));
#endif
}

// The knight's sword cuts on the troll (2026-11-22): his belly is far bigger than the knight's and the club is 125 cm, so where a cut's
// follow-through brings the club across his front, the right hand is pushed straight out from his middle until every point along the
// club lies outside a round belly (an upright cylinder about his middle, live: jeeb.Troll.Belly). The hand keeps its turn, so the club's
// angle and the cut's path are the knight's. A pure function of the pose: the server's hit boxes (on the blade bone) see the same.
static TAutoConsoleVariable<FString> CVarTrollBelly(TEXT("jeeb.Troll.Belly"), TEXT("40_14"),
	TEXT("The troll's belly the club keeps clear of in his cuts: Radius_Forward (cm; an upright cylinder, centred that far ahead of his spine). 0_0: off."), ECVF_Default);

void FKnightAnimator::TrollClubClear(FPose& B)
{
	float Rad = 40.f, Fwd = 14.f;
	{
		TArray<FString> P;
		if (CVarTrollBelly.GetValueOnAnyThread().ParseIntoArray(P, TEXT("_")) == 2) { Rad = FCString::Atof(*P[0]); Fwd = FCString::Atof(*P[1]); }
	}
	// (motion review 2026-11-22: his long arms and 50 cm hand took the knight's cut down to his hip and the club into the floor) the fist is
	// first lifted until it is no lower than his hips and the whole club clears the floor by 35 cm
	{
		FQuat HR;
		const FVector Hand = BonePos(B, RHand, &HR);
		const FVector Grip = Hand + HR.RotateVector(L.RestRot[Bone[RHand]].Inverse().RotateVector(L.TrollFistR));
		const FVector Dir = HR.RotateVector(L.BladeInHand).GetSafeNormal();
		const float Floor = float(FMath::Min(BonePos(B, LToe).Z, BonePos(B, RToe).Z)) + 35.f;
		float Lift = FMath::Max(0.f, float(BonePos(B, Hips).Z) - float(Grip.Z));
		for (float s = 0.f; s <= 110.f; s += 10.f) Lift = FMath::Max(Lift, Floor - float((Grip + Dir * s).Z));
		if (Lift > 0.5f)
		{
			L.ReachTo(B, RArm, RFore, RHand, Hand + FVector(0.f, 0.f, Lift), FVector(-0.8f, -0.3f, -0.6f));
			FQuat Now;
			BonePos(B, RHand, &Now);
			TurnInMesh(B, RHand, HR * Now.Inverse());
		}
	}
	if (Rad <= 1.f) return;
	const FVector Mid = BonePos(B, Spine1) + FVector(0.f, Fwd, 0.f);
	const float Lo = float(BonePos(B, Hips).Z) - 45.f, Hi = float(BonePos(B, Spine2).Z) + 10.f;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		FQuat HR;
		const FVector Hand = BonePos(B, RHand, &HR);
		const FVector Grip = Hand + HR.RotateVector(L.RestRot[Bone[RHand]].Inverse().RotateVector(L.TrollFistR));   // (the fist's hole)
		const FVector Dir = HR.RotateVector(L.BladeInHand).GetSafeNormal();
		FVector Push = FVector::ZeroVector;
		for (float s = -15.f; s <= 100.f; s += 5.f)
		{
			const FVector Pt = Grip + Dir * s;
			if (Pt.Z < Lo || Pt.Z > Hi) continue;
			FVector D(Pt.X - Mid.X, Pt.Y - Mid.Y, 0.f);
			const float Dist = float(D.Size());
			if (Dist >= Rad) continue;
			if (Dist < 1.f) D = FVector(0.f, 1.f, 0.f); else D /= Dist;
			const FVector Need = D * (Rad - Dist);
			if (Need.SizeSquared() > Push.SizeSquared()) Push = Need;
		}
		if (Push.IsNearlyZero(0.5f)) return;
		L.ReachTo(B, RArm, RFore, RHand, Hand + Push, FVector(-0.8f, -0.3f, -0.6f));
		FQuat Now;
		BonePos(B, RHand, &Now);
		TurnInMesh(B, RHand, HR * Now.Inverse());   // (the club keeps its angle)
	}
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
	const FTransform& Rel = TrollRel;
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

// The troll's own one-handed blows (2026-11-24, owner: "his club swings need review (natural path, weight, follow-through)"). The knight's
// sword cuts bend the knight at the waist to reach; on the troll's long trunk that doubled him over until his chest was level with the
// floor and the club went round out of sight. These are made for him instead, on the same timing rows (Jab1 / Jab2 in Tuning.ini: From..To
// are the hit frames, past 1 the still moment after), so the server's hit boxes (they ride the blade bone) and every machine read the same
// MoveU. He stands up in both; the weight is in the trunk's coil and the slow start of each blow.
//   Blow 1: the club drawn back over his right shoulder with the chest coiled right, his weight back and the left arm out ahead as a
//           counterweight, then an overhand chop down and in across his middle line to his front left: he steps his weight in (hips
//           forward and down, feet planted), the chest comes through and leans in, the left arm swings back.
//   Blow 2: from there the club is carried up across to the left side of his chest, the chest coiled left, and swept back level across
//           his front at his chest height to end out wide on his right (a backhand), the chest uncoiling right.
// Measured on the still sparring knight (log JEEBTROLLTIP / JEEBDEAL): the club's head reaches 2.2 m (blow 1) and 2.1 m (blow 2) ahead of
// his middle and both blows land on a knight 1.7, 2.0 and 2.4 m off. (The knight's cuts on him carried the whole club 2.3 to 3 m out: they
// reached further but passed clean over a knight 2 m away.)
// The hand goes round the shoulder on an arc (its direction turned, its distance eased), the club is turned onto its line, and
// TrollClubClear then keeps the whole club off his belly and the floor. Hand places are from the right shoulder in arm lengths, club
// directions unit vectors, Twist the chest's turn (+ right shoulder back), Lean + back; mesh space: +X his left, +Y ahead, +Z up.
namespace
{
	struct FOneKey { FVector Hand; FVector Dir; float Twist; float Lean; FVector Left; float Lunge; };   // (Lunge: his hips' share of the step in, -1..1)
	static FVector ArcLerp(const FVector& A, const FVector& B, float W)
	{
		const FQuat Q = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(A.GetSafeNormal(), B.GetSafeNormal()), W);
		return Q.RotateVector(A.GetSafeNormal()) * FMath::Lerp(float(A.Size()), float(B.Size()), W);
	}
	static FOneKey LerpOne(const FOneKey& A, const FOneKey& B, float W)
	{
		return { ArcLerp(A.Hand, B.Hand, W), ArcLerp(A.Dir.GetSafeNormal(), B.Dir.GetSafeNormal(), W), FMath::Lerp(A.Twist, B.Twist, W), FMath::Lerp(A.Lean, B.Lean, W), FMath::Lerp(A.Left, B.Left, W), FMath::Lerp(A.Lunge, B.Lunge, W) };
	}
}

void FKnightAnimator::TrollSwingOneHand(const FInput& In, FPose& B, float& OutBlend, bool b2)
{
	const auto& Tn = JeebTune::V();
	if (const FClip* G = Get(TEXT("Idle_SS"))) { Sample(*G, IdleT, Over, true); Overlay(B, Over, 1.f, 1.f); }
	const float U = In.MoveU;
	const float From = b2 ? Tn.Jab2From : Tn.Jab1From, To = b2 ? Tn.Jab2To : Tn.Jab1To;
	const float UEnd = 1.f + (b2 ? Tn.Jab2Pause : Tn.Jab1Pause) / FMath::Max(b2 ? Tn.Jab2Share : Tn.Jab1Share, 0.05f);
	static const FOneKey Rest  = { FVector(-0.25f, 0.45f, -0.05f), FVector(-0.10f, 0.35f, 0.93f),   0.f,   0.f, FVector(0.10f, 0.35f, -0.50f), 0.f };
	static const FOneKey Wind1 = { FVector(-0.44f, -0.20f, 0.70f), FVector(-0.25f, -0.62f, 0.74f),  32.f,   7.f, FVector(0.05f, 0.62f, -0.08f), -0.25f };   // over the right shoulder, the head tipped back, his weight back
	// (hit check, log JEEBTROLLTIP: aimed straight ahead of his right shoulder the head came down half a metre to the right of a knight stood in
	// front of him; the hand and the club are angled in so the head crosses his middle line at a knight's chest height, 1.75 m out)
	static const FOneKey Hit1  = { FVector(0.15f, 0.96f, -0.12f), FVector(0.37f, 0.90f, -0.30f), -26.f, -20.f, FVector(0.42f, -0.02f, -0.50f), 1.f };  // the arm out across his front, the head at a knight's chest on his middle line
	static const FOneKey End1  = { FVector(0.34f, 0.62f, -0.50f),  FVector(0.52f, 0.52f, -0.68f), -40.f, -22.f, FVector(0.48f, -0.20f, -0.48f), 0.8f };  // run on low to his front left, the head clear of his left leg
	// (blow 2 is swept at his own chest height, about 1.3 m: a knight's chest and head; at his shoulder height it passed over a knight)
	static const FOneKey Wind2 = { FVector(0.50f, 0.42f, -0.12f),  FVector(0.62f, -0.55f, 0.55f), -46.f,  -4.f, FVector(0.50f, -0.20f, -0.45f), 0.2f };  // carried up across to the left side of his chest, the head behind it
	static const FOneKey Hit2  = { FVector(-0.30f, 0.90f, -0.40f), FVector(-0.35f, 0.93f, -0.04f),  8.f, -14.f, FVector(0.50f, 0.05f, -0.45f), 1.f };   // level across his front at arm's length
	static const FOneKey End2  = { FVector(-0.98f, 0.25f, -0.30f), FVector(-0.80f, -0.55f, 0.16f),  40.f,  -3.f, FVector(0.30f, 0.50f, -0.30f), 0.4f };   // out wide on his right, the head run on behind
	const FOneKey& K0 = b2 ? Wind2 : Wind1, &K1 = b2 ? Hit2 : Hit1, &K2 = b2 ? End2 : End1;
	FOneKey Now;
	if (U < From) Now = LerpOne(b2 ? End1 : Rest, K0, FMath::SmoothStep(0.f, From * 0.88f, U));   // (drawn back, and a breath there: the weight gathers)
	// (hit check on the sparring knight 2 m off: with the blow only arriving at the last hit frame, blow 1 never touched him. The club is on
	// its hit line 70% of the way through the hit frames and carries on a third of the way into the follow-through while they last.)
	const FOneKey Through = LerpOne(K1, K2, 0.35f);
	if (U < From) {}
	else if (U < To)
	{
		const float F = (U - From) / FMath::Max(To - From, 0.01f);
		Now = F < 0.7f ? LerpOne(K0, K1, FMath::Pow(F / 0.7f, 1.6f)) : LerpOne(K1, Through, (F - 0.7f) / 0.3f);   // (a heavy blow: slow to start, fastest at the hit)
	}
	else { const float S = FMath::Clamp((U - To) / FMath::Max((UEnd - To) * 0.75f, 0.01f), 0.f, 1.f); Now = LerpOne(Through, K2, 1.f - FMath::Square(1.f - S)); }   // (it runs on and stops)
	// the step in (hit check: standing bolt upright his club reached 1.8 m, no further than a knight's sword): his hips go forward and down
	// with the blow, back a little in the wind-up, and both legs are bent to keep his feet where they stand, turned as they were
	if (FMath::Abs(Now.Lunge) > 0.01f)
	{
		FQuat LQ, RQ;
		const FVector LF = BonePos(B, LFoot, &LQ), RF = BonePos(B, RFoot, &RQ);
		B.Hip += FVector(0.f, 36.f * Now.Lunge, -10.f * FMath::Abs(Now.Lunge));
		L.ReachTo(B, LUp, LLeg, LFoot, LF, FVector(0.2f, 1.f, 0.2f));
		L.ReachTo(B, RUp, RLeg, RFoot, RF, FVector(-0.2f, 1.f, 0.2f));
		FQuat Q;
		BonePos(B, LFoot, &Q); TurnInMesh(B, LFoot, LQ * Q.Inverse());
		BonePos(B, RFoot, &Q); TurnInMesh(B, RFoot, RQ * Q.Inverse());
	}
	// the trunk: the coil shared up the spine, the head turned back by most of it so he keeps his eyes on what he hits
	const float Yaw = FMath::DegreesToRadians(Now.Twist), Lean = FMath::DegreesToRadians(Now.Lean);
	static const float Share[3] = { 0.22f, 0.36f, 0.42f };
	const int32 SpineB[3] = { Spine, Spine1, Spine2 };
	for (int32 i = 0; i < 3; ++i) TurnInMesh(B, SpineB[i], FQuat(FVector::UpVector, Yaw * Share[i]) * FQuat(FVector::XAxisVector, Lean * Share[i]));
	TurnInMesh(B, Neck, FQuat(FVector::UpVector, -Yaw * 0.35f));
	TurnInMesh(B, Head, FQuat(FVector::UpVector, -Yaw * 0.35f));
	// the right hand, then the club turned onto its line
	const float Arm = float(RefPos[Bone[RFore]].Size() + RefPos[Bone[RHand]].Size());
	const FVector Sh = BonePos(B, RArm);
	L.ReachTo(B, RArm, RFore, RHand, Sh + Now.Hand * Arm, FVector(-0.8f, -0.3f, -0.6f));
	FQuat HandRot;
	BonePos(B, RHand, &HandRot);
	TurnInMesh(B, RHand, FQuat::FindBetweenNormals(HandRot.RotateVector(L.BladeInHand).GetSafeNormal(), Now.Dir.GetSafeNormal()));
	// the left arm: his balance
	const FVector ShL = BonePos(B, LArm), NowL = BonePos(B, LHand);
	L.ReachTo(B, LArm, LFore, LHand, FMath::Lerp(NowL, ShL + Now.Left * Arm, 0.85f), FVector(0.8f, -0.4f, -0.5f));
	TrollClubClear(B);
	OutBlend = 0.12f;
}

// The troll's block with no shield (owner 2026-11-21: "just have him put his hand up as if he's trying to block with just his bare arm").
// The left forearm is raised across in front of his chest and face, the elbow down and out, the fist up; the right arm keeps the guard the
// block overlay left it. Placed by reach (like the punch), blended in by the block's own alpha, so the pose is a pure function of
// In.BlockAlpha, which every machine derives from the replicated block flag: others see it too. Mesh space: +X his left, +Y ahead, +Z up.
static TAutoConsoleVariable<FString> CVarTrollGuard(TEXT("jeeb.Troll.Guard"), TEXT("0.0_0.7_-0.45_90"),
	TEXT("The troll's bare-hand block: Across_Ahead_Up (his left wrist from his head, in arm lengths; + his left, ahead, up) _Roll (degrees the hand turns about its fingers)."), ECVF_Default);

void FKnightAnimator::TrollBareGuard(const FInput& In, FPose& B)
{
	const float W = FMath::Clamp(In.BlockAlpha * (1.f - In.CrouchAlpha), 0.f, 1.f);
	if (W < 0.01f) return;
	// (2026-11-22, owner: "his hand in front of his face with his palm facing away from his head, as if he's trying to block sunlight from
	// his eyes", picture pt23/134.) The left hand is raised in front of his face, fingers up and leaning a little across to his right,
	// palm out (ahead). His hand mesh runs 50 cm on from the wrist joint, so the wrist is placed below and ahead of the face and the hand
	// stands up from it. Live: jeeb.Troll.Guard = Across_Ahead_Up (the wrist from his head, arm lengths) _Roll (degrees about the fingers).
	float Ax = 0.f, Ah = 0.7f, Up = -0.45f, Roll = 90.f;
	{
		TArray<FString> P;
		if (CVarTrollGuard.GetValueOnAnyThread().ParseIntoArray(P, TEXT("_")) == 4) { Ax = FCString::Atof(*P[0]); Ah = FCString::Atof(*P[1]); Up = FCString::Atof(*P[2]); Roll = FCString::Atof(*P[3]); }
	}
	const FVector HeadAt = BonePos(B, Head);
	const FVector Now = BonePos(B, LHand);
	const float Arm = float(RefPos[Bone[LFore]].Size() + RefPos[Bone[LHand]].Size());
	const FVector Want = HeadAt + FVector(Ax, Ah, Up + JeebTune::V().TrollGuardUp) * Arm;
	FQuat Was;
	BonePos(B, LHand, &Was);
	L.ReachTo(B, LArm, LFore, LHand, FMath::Lerp(Now, Want, W), FVector(0.6f, -0.1f, -1.f));   // (the elbow down and out)
	// the hand's own turn: its fingers (wrist to the middle of the hand, in the bind pose) up, its palm (the bind pose's down side) ahead
	const FQuat Rest = L.RestRot[Bone[LHand]];
	const FVector FingL = Rest.Inverse().RotateVector(FVector(L.TrollFistR.X * -1.f, L.TrollFistR.Y, L.TrollFistR.Z).GetSafeNormal());   // (the left hand: the right's mirror across X)
	FVector PalmM = FVector(0.f, 0.f, -1.f);
	{
		const FVector Fm = Rest.RotateVector(FingL);
		PalmM = (PalmM - Fm * FVector::DotProduct(PalmM, Fm)).GetSafeNormal();
	}
	const FVector PalmL = Rest.Inverse().RotateVector(PalmM);
	FQuat Cur;
	BonePos(B, LHand, &Cur);
	const FVector FingWant = FVector(-0.30f, 0.12f, 1.f).GetSafeNormal();   // (up, leaning a little across to his right)
	FVector PalmWant = FVector(0.f, 1.f, 0.f);
	PalmWant = FQuat(FingWant, FMath::DegreesToRadians(Roll)).RotateVector((PalmWant - FingWant * FVector::DotProduct(PalmWant, FingWant)).GetSafeNormal());
	const FQuat CurFrame = FRotationMatrix::MakeFromXZ(Cur.RotateVector(FingL), Cur.RotateVector(PalmL)).ToQuat();
	const FQuat WantFrame = FRotationMatrix::MakeFromXZ(FingWant, PalmWant).ToQuat();
	TurnInMesh(B, LHand, FQuat::Slerp(FQuat::Identity, WantFrame * CurFrame.Inverse(), W));
}
