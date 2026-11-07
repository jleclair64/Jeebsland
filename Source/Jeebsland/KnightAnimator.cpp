#include "KnightAnimator.h"
#include "HitBoxes.h"
#include "Inventory.h"
#include "KnightDesign.h"
#include "KnightShieldRest.h"
#include "WeaponDesign.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "JeebTuning.h"
#include "Engine/SkeletalMesh.h"

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
		TEXT("FallIdle"), TEXT("FallLand"), TEXT("HardLand"), TEXT("StandRoll"),
		TEXT("SlashFore"), TEXT("SlashCross"), TEXT("SlashDown"), TEXT("SlashCrouch"), TEXT("ThrustSlash"), TEXT("KickSparta"),
		TEXT("HitHead"), TEXT("HitBody"), TEXT("HitBlocked"), TEXT("HitLargeF"), TEXT("HitSmallL"), TEXT("HitSmallR"), TEXT("HitSmallB"),
		TEXT("SweepFall"), TEXT("GetUpBack"), TEXT("FallDown"),
		TEXT("CrouchIdle"), TEXT("CrouchWalk"),
		TEXT("BowDraw"), TEXT("BowRecoil"), TEXT("AimIdle"), TEXT("AimWalkF"), TEXT("AimWalkB"), TEXT("AimWalkL"), TEXT("AimWalkR"),
		TEXT("WeightedRun"), TEXT("ChestBlock"), TEXT("Drink"),
		TEXT("HeavySwing"), TEXT("SlashWide"), TEXT("MinePick"), TEXT("CrossAim"), TEXT("CrossFire"), TEXT("CrossReload"), TEXT("CrossIdle"), TEXT("FishCast"), TEXT("FishIdle"), TEXT("Gather"), TEXT("PickUp"), TEXT("ClimbLadder"), TEXT("SitChair"), TEXT("SlideHold"), TEXT("SlashCombo"), TEXT("GreatCombo") };

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

namespace
{
	FString BakedPath() { return FPaths::ProjectContentDir() / TEXT("Data/KnightClips.bin"); }
	constexpr int32 BakedVersion = 1;
	const TCHAR* const SuitMeshPath = TEXT("/Game/NewSuit/SK_NewSuit.SK_NewSuit");
}

const FClipLib& FClipLib::Get()
{
	static const FClipLib Lib;
	return Lib;
}

FClipLib::FClipLib()
{
	const double LoadFrom = FPlatformTime::Seconds();
	bool bLoaded = false;
#if WITH_EDITOR
	// (the editor keeps the file up to date: new clips imported since it was written, or none yet)
	if (!BakeIsStale()) bLoaded = LoadBaked();
	if (!bLoaded && BakeFromAssets()) { SaveBaked(); bLoaded = true; }
	if (!bLoaded) bLoaded = LoadBaked();
#else
	bLoaded = LoadBaked();
#endif
	if (!bLoaded) { UE_LOG(LogTemp, Error, TEXT("JEEBANIM no clips: Content/Data/KnightClips.bin is missing")); return; }
	Finalize();
	bReady = Clips.Contains(TEXT("Rest"));
	UE_LOG(LogTemp, Log, TEXT("JEEBANIM %d clips, %d bones, %d moves, %.0f ms to load"), Clips.Num(), Parents.Num(), Moves.Num(), (FPlatformTime::Seconds() - LoadFrom) * 1000.0);
}

// The skeleton and every clip as plain numbers: no engine assets, so a cooked server (which strips animations) can read it as well
bool FClipLib::LoadBaked()
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *BakedPath(), FILEREAD_Silent)) return false;
	FMemoryReader Ar(Bytes, true);
	int32 Version = 0;
	Ar << Version;
	if (Version != BakedVersion) return false;
	Ar << BoneName << Parents << RefPos << RefRot;
	int32 NumClips = 0;
	Ar << NumClips;
	const int32 N = Parents.Num();
	for (int32 c = 0; c < NumClips; ++c)
	{
		TUniquePtr<FClip> C = MakeUnique<FClip>();
		Ar << C->Name << C->NumFrames << C->Len << C->Rot << C->HipPos;
		if (Ar.IsError() || C->Rot.Num() != C->NumFrames * N) return false;
		FClip* Raw = C.Get();
		ClipStore.Add(MoveTemp(C));
		Clips.Add(Raw->Name, Raw);
	}
	return !Ar.IsError() && N > 0;
}

void FClipLib::Finalize()
{
	const int32 N = Parents.Num();
	UpperMask.Init(0, N);
	for (int32 i = 0; i < B_Count; ++i)
	{
		Bone[i] = INDEX_NONE;
		for (int32 b = 0; b < N; ++b)
		{
			const FString& Nm = BoneName[b];
			if (Nm == BoneNames[i] || Nm.EndsWith(FString(TEXT(":")) + BoneNames[i])) { Bone[i] = b; break; }
		}
		if (Bone[i] == INDEX_NONE) { UE_LOG(LogTemp, Error, TEXT("JEEBANIM no bone %s"), BoneNames[i]); return; }
	}
	for (int32 i = Spine; i <= RHand; ++i) UpperMask[Bone[i]] = 1;
	// (his own walk rides high against the rest pose: this puts its planted foot on the floor)
	if (FClip* const* W = Clips.Find(TEXT("Walking"))) for (FVector& H : (*W)->HipPos) H.Z -= 3.9f;
	for (TUniquePtr<FClip>& C : ClipStore) Measure(*C);
	LoadMoves();
	SolveGear();
	// Test pictures (-SuitLogClips): where the hand and foot go through each move's clip, the way to read off KnightMoves.csv's numbers
	if (FParse::Param(FCommandLine::Get(), TEXT("SuitLogClips")))
	{
		FPose P; P.Rot = RefRot; P.Hip = RefPos[Bone[Hips]];
		FString Rest = TEXT("JEEBSKEL bones at the bind pose, fwd,right,up:");
		for (int32 i = 0; i < B_Count; ++i) { const FVector X = BindInGame(P, i).GetLocation(); Rest += FString::Printf(TEXT(" %s %.0f,%.0f,%.0f"), BoneNames[i], X.X, X.Y, X.Z); }
		UE_LOG(LogTemp, Log, TEXT("%s"), *Rest);
		if (const FClip* RC = Find(TEXT("Rest")))
		{
			Sample(*RC, 0.f, P, false);
			Rest = TEXT("JEEBSKEL bones at rest, fwd,right,up:");
			for (int32 i = 0; i < B_Count; ++i) { const FVector X = BindInGame(P, i).GetLocation(); Rest += FString::Printf(TEXT(" %s %.0f,%.0f,%.0f"), BoneNames[i], X.X, X.Y, X.Z); }
			UE_LOG(LogTemp, Log, TEXT("%s"), *Rest);
		}
		for (const TPair<FString, FMove>& It : Moves)
			if (const FClip* C = Find(*It.Value.Clip))
			{
				FString Line = FString::Printf(TEXT("JEEBPATH %s %s (%.2f s; time: hand fwd,right,up / foot R up,fwd / hip up,fwd):"), *It.Key, *It.Value.Clip, C->Len);
				for (float T = 0.f; T <= C->Len + 0.001f; T += 0.1f)
				{
					Sample(*C, T, P, false);
					const FVector H = BonePos(P, RHand), F = BonePos(P, RFoot);
					Line += FString::Printf(TEXT(" | %.1f: %.0f,%.0f,%.0f / %.0f,%.0f / %.0f,%.0f"), T, H.Y, H.X, H.Z, F.Z, F.Y, P.Hip.Z, P.Hip.Y);
				}
				UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
			}
	}
}

#if WITH_EDITOR
bool FClipLib::BakeIsStale() const
{
	const FDateTime Baked = IFileManager::Get().GetTimeStamp(*BakedPath());
	if (Baked == FDateTime::MinValue()) return true;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(FPaths::ProjectContentDir() / TEXT("NewSuit/Clips/*.uasset")), true, false);
	for (const FString& F : Files)
		if (IFileManager::Get().GetTimeStamp(*(FPaths::ProjectContentDir() / TEXT("NewSuit/Clips") / F)) > Baked) return true;
	return IFileManager::Get().GetTimeStamp(*(FPaths::ProjectContentDir() / TEXT("NewSuit/SK_NewSuit.uasset"))) > Baked;
}

bool FClipLib::BakeFromAssets()
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, SuitMeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh) return false;
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 N = Ref.GetNum();
	BoneName.SetNum(N); Parents.SetNum(N); RefPos.SetNum(N); RefRot.SetNum(N);
	TArray<FName> Names;
	Names.SetNum(N);
	for (int32 b = 0; b < N; ++b)
	{
		Names[b] = Ref.GetBoneName(b);
		BoneName[b] = Names[b].ToString();
		Parents[b] = Ref.GetParentIndex(b);
		RefPos[b] = Ref.GetRefBonePose()[b].GetLocation();
		RefRot[b] = Ref.GetRefBonePose()[b].GetRotation();
	}
	for (const TCHAR* Nm : ClipNames) LoadClipAsset(Nm, TEXT("/Game/NewSuit/Clips/A_"), Names);
	LoadClipAsset(TEXT("Rest"), TEXT("/Game/NewSuit/A_"), Names);
	LoadClipAsset(TEXT("Walking"), TEXT("/Game/NewSuit/A_"), Names);
	return Clips.Contains(TEXT("Rest"));
}

const FClip* FClipLib::LoadClipAsset(const FString& Name, const TCHAR* Folder, const TArray<FName>& Names)
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
	int32 HipB = INDEX_NONE;
	for (int32 b = 0; b < N; ++b)
	{
		Track[b] = SkelRef.FindBoneIndex(Names[b]);
		if (BoneName[b] == BoneNames[Hips] || BoneName[b].EndsWith(FString(TEXT(":")) + BoneNames[Hips])) HipB = b;
	}
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
			if (b == HipB) C->HipPos[f] = X.GetLocation();
		}
	}
	FClip* Raw = C.Get();
	ClipStore.Add(MoveTemp(C));
	Clips.Add(Name, Raw);
	return Raw;
}

void FClipLib::SaveBaked() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, true);
	int32 Version = BakedVersion;
	Ar << Version;
	FClipLib& Me = const_cast<FClipLib&>(*this);   // (the writer's << takes references; nothing is changed)
	Ar << Me.BoneName << Me.Parents << Me.RefPos << Me.RefRot;
	// (in a fixed order, so the same clips always make the same file)
	TArray<FClip*> Order;
	for (const TUniquePtr<FClip>& C : ClipStore) Order.Add(C.Get());
	Order.Sort([](const FClip& A, const FClip& B) { return A.Name < B.Name; });
	int32 NumClips = Order.Num();
	Ar << NumClips;
	for (FClip* C : Order) Ar << C->Name << C->NumFrames << C->Len << C->Rot << C->HipPos;
	FFileHelper::SaveArrayToFile(Bytes, *BakedPath());
	UE_LOG(LogTemp, Log, TEXT("JEEBANIM baked %d clips to KnightClips.bin (%d KB)"), NumClips, Bytes.Num() / 1024);
}
#endif

// How fast the planted foot moves, and when each foot is under him. All clips are in place, so a planted foot slides back under
// the body at exactly the speed the body should cover the ground.
void FClipLib::Measure(FClip& C)
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
void FClipLib::LoadMoves()
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

void FClipLib::Sample(const FClip& C, float T, FPose& Out, bool bLoop) const
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

float FClipLib::PhaseTime(const FClip& C, float P) const
{
	const float GapLR = FMath::Fmod(C.MarkR - C.MarkL + C.Len, C.Len);
	const float GapRL = C.Len - GapLR;
	return P < 0.5f ? C.MarkL + P * 2.f * GapLR : C.MarkR + (P - 0.5f) * 2.f * GapRL;
}

void FClipLib::Accum(const FPose& P, float W, FPose& Sum, float& SumW) const
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

void FClipLib::Finish(FPose& Sum, float SumW) const
{
	for (FQuat& Q : Sum.Rot) Q.Normalize();
	if (SumW > 0.f) Sum.Hip /= SumW;
}

// The raised shield: the clip's forearm points straight at it, fingers first. Turned this far it lies across behind the shield, towards his right, as a shield is worn.
static FQuat GuardForeTurn() { return FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(50.f)); }

// The turn of a bone's parent in the mesh's own space
FQuat FClipLib::ParentRot(const FPose& P, int32 B) const
{
	FQuat Q = FQuat::Identity;
	for (int32 p = Parents[Bone[B]]; p != INDEX_NONE; p = Parents[p]) Q = P.Rot[p] * Q;
	return Q;
}

// Turns one bone by Q as seen in the mesh's space (up is up, whatever the bones above it are doing)
void FClipLib::TurnInMesh(FPose& P, int32 B, const FQuat& Q) const
{
	const FQuat Pa = ParentRot(P, B);
	P.Rot[Bone[B]] = (Pa.Inverse() * Q * Pa * P.Rot[Bone[B]]).GetNormalized();
}

FVector FClipLib::BonePos(const FPose& P, int32 B, FQuat* OutRot) const
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
void FClipLib::Overlay(FPose& B, const FPose& O, float Upper, float Lower) const
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
void FKnightAnimator::Locomotion(float Dt, const FInput& In, FPose& Out)
{
	const FVector V = In.Vel;
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
			const FVector Lv = In.Rot.UnrotateVector(FVector(V.X, V.Y, 0.f));
			if (Speed > 1.f) { DirF = Lv.X / Speed; DirR = Lv.Y / Speed; }
			bLocoHand = false;
		}
	}
	// the arms: what he holds
	const int32 Kind = In.Kind;
	const bool bBow = JeebItems::IsDrawnBow(Kind);
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
	const FVector LV = In.Rot.UnrotateVector(FVector(V.X, V.Y, 0.f));
	if (Speed > 1.f)
	{
		const float Rate = 11.f;
		DirF = FMath::FInterpTo(DirF, LV.X / Speed, Dt, Rate);
		DirR = FMath::FInterpTo(DirR, LV.Y / Speed, Dt, Rate);
	}
	const float LegYaw = In.LegYaw;
	const float Ang = FMath::RadiansToDegrees(FMath::Atan2(DirR, DirF)) - LegYaw;
	const float CosA = FMath::Cos(FMath::DegreesToRadians(Ang)), SinA = FMath::Sin(FMath::DegreesToRadians(Ang));
	float WD[4] = { FMath::Max(CosA, 0.f), FMath::Max(SinA, 0.f), FMath::Max(-CosA, 0.f), FMath::Max(-SinA, 0.f) };   // ahead, right, back, left
	const float WS = WD[0] + WD[1] + WD[2] + WD[3];
	for (float& W : WD) W /= FMath::Max(WS, 0.01f);
	// walk, run and sprint by speed
	const float URun = FMath::SmoothStep(230.f, 400.f, SmSpeed);
	const float USprint = WD[0] > 0.7f ? FMath::SmoothStep(560.f, 700.f, SmSpeed) : 0.f;

	// (ahead he walks upright and runs on WeightedRun: arms bent in at the ribs, feet close but not in one line, owner 2026-11-05; the sword-and-shield clips only for going sideways and backing up)
	const TCHAR* Names[9] = { TEXT("Walking"), TEXT("WalkR_SS"), TEXT("WalkBack_SS"), TEXT("WalkL_SS"), TEXT("WeightedRun"), TEXT("RunR_SS"), TEXT("RunBack_SS"), TEXT("RunL_SS"), TEXT("Sprint") };
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
	// In the air nothing pushes on the ground, so the legs have no stride to keep up with: they carry on at half their pace and settle back on landing (owner, 2026-11-05; 50% more on 2026-11-06)
	AirLegs = FMath::FInterpTo(AirLegs, In.bFalling ? 0.5f : 1.f, Dt, In.bFalling ? 9.f : 14.f);
	Phase = FMath::Fmod(Phase + Rate * AirLegs * Dt, 1.f);

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
	// Running, the clip's fist pumps the blade from level to straight up in front of his face: the wrist keeps it steady, out in front and a little up, and only part of the swing shows
	if (!bBow && Kind >= 0)
	{
		const float Steady = 0.8f * FMath::Max(URun, USprint);
		if (Steady > 0.01f)
		{
			FQuat HandRot;
			BonePos(Out, RHand, &HandRot);
			const FVector Dir = HandRot.RotateVector(L.BladeInHand).GetSafeNormal();
			static const FVector Want = FVector(-0.2f, 0.92f, 0.34f).GetSafeNormal();   // (in the mesh's frame: -X his right, +Y ahead, +Z up)
			const FQuat Turn = FQuat::FindBetweenNormals(Dir, Want);
			TurnInMesh(Out, RHand, Nlerp(FQuat::Identity, Turn, Steady));
		}
	}
	// The longbow swung too much in a sprint (owner 2026-11-06): the left arm that carries it keeps half of the sprint's swing, the rest is the standing pose (longbow only)
	if (Kind == JeebItems::K_Longbow && USprint > 0.01f)
		if (const FClip* Rs = Get(TEXT("Rest")))
		{
			Sample(*Rs, 0.f, Scratch, false);
			for (const int32 i : { LShoulder, LArm, LFore, LHand }) Out.Rot[Bone[i]] = Nlerp(Out.Rot[Bone[i]], Scratch.Rot[Bone[i]], 0.5f * USprint);
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

void FKnightAnimator::StartBlend(const FPose& Target, float Seconds, float HandSeconds, float LegSeconds)
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
	BlendLambdaLegs = LegSeconds > 0.f ? FMath::Min(BlendLambda, 4.7f / LegSeconds) : BlendLambda;   // (a swing from rest: the arm goes at once, the hips and legs need longer to get under it)
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
	const float Lam = BlendLambda, T = BlendT;
	const float LegLam = BlendLambdaLegs;
	const float E = FMath::Exp(-LegLam * T);
	if (FMath::Min(BlendLambdaHand, LegLam) * T > 8.f) bBlending = false;
	for (int32 b = 0; b < N; ++b)
	{
		// (the sword hand turns further than anything else between his own walk and the sword clips, and the blade makes it show: it takes a little longer)
		const float LH = b == Bone[RHand] ? BlendLambdaHand : Lam;
		const FVector Y = (OffRot[b] + (OffRotV[b] + OffRot[b] * LH) * T) * FMath::Exp(-LH * T);
		Out.Rot[b] = (FromRotVec(Y) * Target.Rot[b]).GetNormalized();
	}
	for (const int32 i : { LUp, LLeg, LFoot, LToe, RUp, RLeg, RFoot, RToe })
	{
		const int32 b = Bone[i];
		const FVector Y = (OffRot[b] + (OffRotV[b] + OffRot[b] * LegLam) * T) * E;
		Out.Rot[b] = (FromRotVec(Y) * Target.Rot[b]).GetNormalized();
	}
	Out.Hip = Target.Hip + (OffHip + (OffHipV + OffHip * LegLam) * T) * E;
}

// A low pose can put the point of the sword through the floor: the wrist lifts it just clear, keeping the way it points. Only as much as it takes, and it
// comes and goes with the pose, so it can't be seen happening.
void FKnightAnimator::KeepTipUp(FPose& P, const FInput& In, float Dt)
{
	if (JeebItems::IsShooter(In.Kind)) return;
	constexpr float Clear = 6.f;
	const float Reach = 9.f + WeaponDesign::BladeReach(In.Kind);   // (from the hand to the point)
	FQuat HandRot;
	const FVector HandAt = BonePos(P, JeebAnim::RHand, &HandRot);
	const FVector Dir = HandRot.RotateVector(L.BladeInHand).GetSafeNormal();
	const float WantZ = FMath::Clamp((Clear - float(HandAt.Z)) / Reach, -1.f, 0.9f);
	const float FlatLen = FVector2D(Dir.X, Dir.Y).Size();
	const float Heading = FlatLen > 0.02f ? FMath::Atan2(float(Dir.Y), float(Dir.X)) : TipHeading;
	if (Dir.Z >= WantZ) { if (FlatLen > 0.3f) TipHeading = Heading; return; }
	// (a blade passing straight down has no real heading, and it can swap sides in a frame: lifted as it stands, the point would flash across. So the heading on the floor turns at the speed of a fast swing, no faster.)
	const float MaxTurn = FMath::DegreesToRadians(2200.f) * Dt;
	TipHeading = FMath::UnwindRadians(TipHeading + FMath::Clamp(FMath::FindDeltaAngleRadians(TipHeading, Heading), -MaxTurn, MaxTurn));
	const FVector Flat(FMath::Cos(TipHeading), FMath::Sin(TipHeading), 0.f);
	const FVector NewDir = Flat * FMath::Sqrt(1.f - WantZ * WantZ) + FVector::UpVector * WantZ;
	TurnInMesh(P, JeebAnim::RHand, FQuat::FindBetweenNormals(Dir, NewDir));
}

// A tool or battle axe has a wide head at the end of its haft where a sword has only a thin point: in the guard and on the way back to it, the sword's clips bring that end up by his cheek.
// The wrist turns just enough to keep the head off the helm, more the closer it would have come, so it never jumps. Chopping and mining have their own hands (TwoHandTool).
void FKnightAnimator::ToolHeadClear(FPose& P, const FInput& In) const
{
	if ((In.Kind != JeebItems::K_Axe && In.Kind != JeebItems::K_Pickaxe && In.Kind != JeebItems::K_BattleAxe) || TwoHandW > 0.f) return;
	constexpr float Need = 34.f;   // (from the helm's middle to the head's: the helm's 14 and the head's 17, and a little air)
	FQuat HandRot;
	const FVector HandAt = BonePos(P, JeebAnim::RHand, &HandRot);
	const FVector Dir = HandRot.RotateVector(L.BladeInHand).GetSafeNormal();
	const FVector Helm = BonePos(P, JeebAnim::Head) + FVector(0.f, 0.f, 10.f);
	const FVector ToolHead = HandAt + Dir * (WeaponDesign::BladeReach(In.Kind) + 1.f);
	const FVector Away = ToolHead - Helm;
	const float Off = Away.Size();
	if (Off >= Need || Off < 1.f) return;
	TurnInMesh(P, JeebAnim::RHand, FQuat::FindBetweenNormals(Dir, (Helm + Away / Off * Need - HandAt).GetSafeNormal()));
}

// ---------------- The frame ----------------

static constexpr float GreatLeftBelow = 14.f;   // (the big sword: how far behind the right fist's grip point the left fist's goes, along the grip)

void FKnightAnimator::Update(float Dt, const FInput& In, const FVector& Feet, float ActorYaw)
{
	if (!L.bReady) return;
	Time += Dt;
	Locomotion(Dt, In, Base);
	Overlays(Dt, In, Base);
	float BlendFor = bMoving ? 0.26f : 0.28f;   // (he sets off at once, but the lean and stride build; a stop settles)
	const int32 Act = ChooseAction(Dt, In, Base, BlendFor);
	if (Act == 0 && StateKey >= 10) BlendFor = StateKey == 72 ? 0.45f : StateKey == 34 ? 0.6f : StateKey == 30 || StateKey == 31 ? 0.5f : StateKey >= 30 && StateKey < 40 ? 0.36f : 0.3f;   // (out of any move, back to walking or standing: the body eases up out of it, never pops; a jab ends far from the guard)
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
		// (out of the low sweep the blade rises from the floor to the guard: one slow arc, never three heights in a few frames)
		if (StateKey != -2) StartBlend(Base, BlendFor, Act == 0 ? (StateKey == 34 || (bMoving && StateKey >= 30 && StateKey < 40) ? 0.48f : 0.34f) : Key == 31 ? 0.2f : 0.16f, Key == 30 ? 0.4f : Key == 31 ? 0.3f : Key == 34 ? 0.25f : 0.f);   // (the sweep's hips must be down by the cut, or the blade rides high; the second jab's feet stand elsewhere than the first's, and they get there over its whole wind-up)
		if ((StateKey >= 30 && StateKey < 40) || StateKey == 72) LiftWindow = 0.5f;
		StateKey = Key;
	}
	// (the bow's phases are layers, not states: drawing, loosing and lowering still flow into each other)
	const int32 BowKey = In.BowMode + (In.Kind == JeebItems::K_Crossbow && In.BowMode == 3 && In.BowT >= 0.22f ? 10 : 0);   // (the crossbow's shot into its reload is a change of clip too)
	if (BowKey != LastBowKey && Key == StateKey) StartBlend(Base, BowKey == 13 ? 0.25f : 0.16f, BowKey == 13 ? 0.25f : 0.2f);
	LastBowKey = BowKey;
	PrevFinal = Final;
	PrevDt = Dt;
	Inertialize(Base, Final, Dt);
	// (a part-way landing or a chest-only move can leave a boot sunk: the hips rise just enough. Floor clips lie lower.)
	{
		const float Sole = (StateKey == 10 || StateKey == 11 || StateKey == 20 || StateKey == 71 || StateKey == 72) ? 1.f : 7.f;
		const float Low = FMath::Min(float(BonePos(Final, LFoot).Z), float(BonePos(Final, RFoot).Z));
		if (Low < Sole) Final.Hip.Z += Sole - Low;
	}
	LiftDraggedFeet(Final, In, Dt);
	GreatGripLine(Final, In, Dt);
	KeepTipUp(Final, In, Dt);
	ToolHeadClear(Final, In);
	TwoHandTool(Final, In, Dt);
	if (GreatW > 0.005f) L.BothHandsOnGrip(Final, FMath::SmoothStep(0.f, 1.f, GreatW), GreatLeftBelow);
	bHavePrev = true;

	ToWorld = FTransform(FRotator(0.f, ActorYaw + YawNow, 0.f), Feet);
	const int32 N = Parents.Num();

	// Into the mesh (when it can be seen: the pose above is what the game reads)
	if (Comp)
	{
		Comp->SetWorldLocationAndRotation(Feet, FRotator(0.f, ActorYaw + YawNow + MeshYawDeg, 0.f));
		TArray<FTransform>& Bones = Comp->BoneSpaceTransforms;
		if (Bones.Num() == N && (Comp->WasRecentlyRendered(0.25f) || In.bShotLog))
		{
			const int32 HipB = Bone[Hips];
			for (int32 b = 0; b < N; ++b)
			{
				Bones[b].SetRotation(Final.Rot[b]);
				Bones[b].SetTranslation(b == HipB ? Final.Hip : RefPos[b]);
				Bones[b].SetScale3D(In.bDrinking && b == Bone[RHand] ? FVector(0.02f) : FVector::OneVector);   // (while he drinks the fist folds away: an open hand round the bottle takes its place, AKnightCharacter::DrinkHand)
			}
			Comp->MarkRefreshTransformDirty();
			Comp->RefreshBoneTransforms();
		}
	}

	// A check for the logs (test pictures only): the biggest jump of any bone between two frames: pops show here before they show in a picture
	if (In.bShotLog && bHavePrev)
	{
		float Worst = 0.f; int32 WorstB = 0;
		for (int32 b = 0; b < N; ++b)
		{
			const float A = FMath::RadiansToDegrees(ToRotVec(Final.Rot[b] * PrevFinal.Rot[b].Inverse()).Size());
			if (A > Worst) { Worst = A; WorstB = b; }
		}
		const float Sp = FVector2D(In.Vel.X, In.Vel.Y).Size();
		const FVector FL = BonePos(Final, LFoot), FR = BonePos(Final, RFoot);
		// how far a foot on the ground slides on the floor since the last frame (cm/s): body speed against its speed under the body
		FVector& PrevL = LogPrevL; FVector& PrevR = LogPrevR;
		const FVector VWorld = FRotator(0.f, -In.Rot.Yaw - MeshYawDeg, 0.f).RotateVector(FVector(In.Vel.X, In.Vel.Y, 0.f));
		const float SlideL = FL.Z < 14.f ? FVector2D((FL - PrevL) / FMath::Max(Dt, 0.001f) + VWorld).Size() : -1.f;
		const float SlideR = FR.Z < 14.f ? FVector2D((FR - PrevR) / FMath::Max(Dt, 0.001f) + VWorld).Size() : -1.f;
		PrevL = FL; PrevR = FR;
		FQuat HandRot; const FVector HandAt = BonePos(Final, RHand, &HandRot);
		const FVector TipAt = HandAt + HandRot.RotateVector(L.BladeInHand) * (9.f + WeaponDesign::BladeReach(In.Kind));
		const float TipZ = TipAt.Z;
		// The blade against his own body (owner, 2026-11-05: a jab stabbed his own chest and the sheets didn't show it): how far the blade's axis stays from the torso and helm
		// capsules (under about 0 means it is inside him), and how far ahead of his chest the point is (mesh +Y is ahead)
		if (In.Kind >= 0 && !JeebItems::IsShooter(In.Kind))
		{
			const FVector Hd = BonePos(Final, Head), Nk = BonePos(Final, Neck), Sp2 = BonePos(Final, Spine2), Hp = BonePos(Final, Hips);
			FVector P1, P2;
			FMath::SegmentDistToSegmentSafe(HandAt, TipAt, Hp, Sp2 + (Nk - Sp2) * 0.5f, P1, P2);
			const float ToBody = float((P1 - P2).Size()) - 17.f;   // (the torso capsule's radius, GatherHurt)
			const float AlongBody = float((P1 - HandAt).Size());
			FMath::SegmentDistToSegmentSafe(HandAt, TipAt, Nk, Hd + (Hd - Nk), P1, P2);
			const float ToHead = float((P1 - P2).Size()) - 13.f;
			UE_LOG(LogTemp, Log, TEXT("JEEBSELF t %.3f key %d body %.0f at %.0f head %.0f tipahead %.0f handahead %.0f tiphigh %.0f handhigh %.0f"), Time, StateKey, ToBody, AlongBody, ToHead, float(TipAt.Y - Sp2.Y), float(HandAt.Y - Sp2.Y), float(TipAt.Z), float(HandAt.Z));
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBANIM t %.3f key %d speed %.0f phase %.2f worst %.1f deg %s feet %.1f %.1f hip %.1f slide %.0f %.0f tip %.0f hand %.0f,%.0f,%.0f tipat %.0f,%.0f,%.0f"), Time, StateKey, Sp, Phase, Worst, *L.BoneName[WorstB], FL.Z, FR.Z, Final.Hip.Z, SlideL, SlideR, TipZ, HandAt.Y, -HandAt.X, HandAt.Z, TipAt.Y, -TipAt.X, TipAt.Z);
	}
}

// Test pictures (-SuitGallery): every picture is one clip shown as eight frozen knights in a row, across its length, with its name above
void FKnightAnimator::TickGallery(AActor* Owner, const FVector& Floor)
{
	if (!Comp) return;
	Comp->SetVisibility(false);
	if (GalleryComps.Num() == 0)
	{
		for (const TUniquePtr<FClip>& C : L.ClipStore) GalleryOrder.Add(C.Get());
		GalleryOrder.Sort([](const FClip& A, const FClip& B) { return A.Name < B.Name; });
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
	if (!GalleryOrder.IsValidIndex(Shown)) return;
	const FClip& Cl = *GalleryOrder[Shown];
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
void FKnightAnimator::Overlays(float Dt, const FInput& In, FPose& B)
{
	const FVector V = In.Vel;
	const float Speed = FVector2D(V.X, V.Y).Size();
	const float MoveW = FMath::Clamp(Speed / 150.f, 0.f, 1.f);
	if (In.CrouchAlpha > 0.01f)
	{
		const FClip* C = bMoving ? Get(TEXT("CrouchWalk")) : Get(TEXT("CrouchIdle"));
		if (C)
		{
			Sample(*C, bMoving ? PhaseTime(*C, Phase) : Time, Over, true);
			Overlay(B, Over, In.CrouchAlpha * 0.75f, In.CrouchAlpha);
			// (the walking clip stands much taller than the crouch: mixing the crouch's legs back in keeps him low, with a shorter stride that suits the slow speed)
			if (bMoving)
				if (const FClip* Low = Get(TEXT("CrouchIdle")))
				{
					Sample(*Low, Time, Over, true);
					Overlay(B, Over, 0.f, In.CrouchAlpha * 0.55f);
				}
		}
	}
	// A shield is held out, not hung (owner 2026-11-06, with pictures of a viking and a templar): the forearm nearly level, square behind the shield. A fixed pose, so the walk doesn't swing it,
	// and what a sword clip leaves of the left arm comes from here too (PlayKeepOffArm).
	ShieldArmW = FMath::FInterpTo(ShieldArmW, In.bShieldArm ? 1.f : 0.f, Dt, 9.f);
	if (ShieldArmW > 0.005f)
	{
		const int32 Arm[4] = { LShoulder, LArm, LFore, LHand };
		for (int32 i = 0; i < 4; ++i) B.Rot[Bone[Arm[i]]] = Nlerp(B.Rot[Bone[Arm[i]]], L.ShieldArm[i], ShieldArmW);
	}
	if (In.BlockAlpha > 0.01f)
		if (const FMove* M = Moves.Find(TEXT("Block")))
			if (const FClip* C = Get(*M->Clip))
			{
				// (owner 2026-11-05: the shield close in front of his chest. ClipStart..ClipHit is the stretch that loops, ClipFollow how slowly: a guard, not a boxer's bounce.)
				Sample(*C, M->ClipStart + FMath::Fmod(Time * M->ClipFollow, FMath::Max(M->ClipHit - M->ClipStart, 0.1f)), Over, false);
				// (ChestBlock's right fist sits at his collar, which would lay the blade across his neck: that arm keeps the on-guard stance, sword out to the front)
				if (const FClip* G = Get(TEXT("Idle_SS")))
				{
					Sample(*G, Time, Scratch, true);
					for (const int32 i : { RShoulder, RArm, RFore, RHand }) Over.Rot[Bone[i]] = Scratch.Rot[Bone[i]];
				}
				TurnInMesh(Over, LFore, GuardForeTurn());
				Overlay(B, Over, In.BlockAlpha, In.BlockAlpha * (1.f - MoveW) * (1.f - In.CrouchAlpha));
				// (owner 2026-11-06: a little down into the knees behind it, the chest over them: defensive)
				if (const FClip* Low = Get(TEXT("CrouchIdle")))
				{
					Sample(*Low, Time, Over, true);
					Overlay(B, Over, 0.f, In.BlockAlpha * 0.32f * (1.f - In.CrouchAlpha));
				}
				TurnInMesh(B, Spine, FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-5.f * In.BlockAlpha)));
			}
	// (the crossbow is two-handed even at ease: held low across him, whatever his legs do)
	const bool bCross = In.Kind == JeebItems::K_Crossbow && !In.bClimbing && !In.bSitting;
	if (bCross)
		if (const FClip* I = Get(TEXT("CrossIdle"))) { Sample(*I, Time, Over, true); Overlay(B, Over, 1.f, 1.f - MoveW); }
	if (In.BowAimAlpha > 0.01f)
	{
		const float W = In.BowAimAlpha;
		const FClip* Upper = nullptr; float T = Time; bool bLoop = true;
		if (bCross)
		{
			constexpr float FireFor = 0.22f;
			if (In.BowMode == 1) Upper = Get(TEXT("CrossAim"));
			else if (In.BowMode == 3 && In.BowT < FireFor)
			{
				Upper = Get(TEXT("CrossFire")); bLoop = false;
				if (const FMove* M = Moves.Find(TEXT("CrossFire"))) T = FMath::Lerp(M->ClipStart, M->ClipEnd, In.BowT / FireFor);
			}
			else if (In.BowMode == 3)
			{
				Upper = Get(TEXT("CrossReload")); bLoop = false;
				if (const FMove* M = Moves.Find(TEXT("CrossReload"))) T = FMath::Lerp(M->ClipStart, M->ClipEnd, FMath::Clamp((In.BowT - FireFor) / FMath::Max(JeebTune::V().CrossReload - FireFor, 0.1f), 0.f, 1.f));
			}
			else Upper = Get(TEXT("CrossIdle"));
		}
		else if (In.BowMode == 1)
		{
			const FClip* D = Get(TEXT("BowDraw"));
			const FMove* M = Moves.Find(TEXT("BowDraw"));
			const float U = In.BowT / FMath::Max(In.BowDrawFor, 0.1f);
			if (D && M && U < 1.f) { Upper = D; T = FMath::Lerp(M->ClipStart, M->ClipEnd, U); bLoop = false; }
			else Upper = Get(TEXT("AimIdle"));
		}
		else if (In.BowMode == 2)
		{
			const FMove* M = Moves.Find(TEXT("BowRecoil"));
			Upper = Get(TEXT("BowRecoil")); bLoop = false;
			if (M) T = M->ClipStart + In.BowT * 1.2f;
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
			// (the rifle clip's reload holds it up by his face, where a crossbow's limbs reach the visor: nearer the low hold)
			if (bCross && In.BowMode == 3 && In.BowT >= 0.22f)
				if (const FClip* I = Get(TEXT("CrossIdle")))
				{
					Sample(*I, Time, Scratch, true);
					for (const int32 i : { RShoulder, RArm, RFore, RHand, LShoulder, LArm, LFore, LHand }) Over.Rot[Bone[i]] = Nlerp(Over.Rot[Bone[i]], Scratch.Rot[Bone[i]], 0.5f);
				}
			// (owner 2026-11-05: the clip's right hand first goes up and back for an arrow in a quiver. ClipHit is where it meets the string: until then the arm just waits there.)
			if (In.BowMode == 1 && !bLoop && !bCross)
				if (const FMove* M = Moves.Find(TEXT("BowDraw")))
					if (T < M->ClipHit)
					{
						Sample(*Upper, M->ClipHit, Scratch, false);
						for (const int32 i : { RShoulder, RArm, RFore, RHand }) Over.Rot[Bone[i]] = Scratch.Rot[Bone[i]];
					}
			Overlay(B, Over, W, W * (1.f - MoveW));
		}
		// moving, the legs walk his aiming stance's way, in step
		if (bMoving && MoveW > 0.01f)
		{
			const FVector LV = In.Rot.UnrotateVector(FVector(V.X, V.Y, 0.f));
			const float CosA = LV.X / FMath::Max(Speed, 1.f), SinA = LV.Y / FMath::Max(Speed, 1.f);
			const float WD[4] = { FMath::Max(CosA, 0.f), FMath::Max(SinA, 0.f), FMath::Max(-CosA, 0.f), FMath::Max(-SinA, 0.f) };
			const TCHAR* Nm[4] = { TEXT("AimWalkF"), TEXT("AimWalkR"), TEXT("AimWalkB"), TEXT("AimWalkL") };
			float Sum = 0.f;
			FPose& Tmp = ScratchSum;
			for (int32 i = 0; i < 4; ++i)
				if (WD[i] > 0.01f)
					if (const FClip* C = Get(Nm[i])) { Sample(*C, PhaseTime(*C, Phase), Scratch, true); Accum(Scratch, WD[i], Tmp, Sum); }
			if (Sum > 0.f)
			{
				Finish(Tmp, Sum);
				// (the crossbow up at its quick walk, or sprinting while it reloads: his own walk's and run's legs, in step, not an aiming shuffle skating at that speed)
				const float RunLegs = bCross ? FMath::SmoothStep(270.f, 340.f, Speed) : 0.f;
				Overlay(B, Tmp, 0.f, W * MoveW * (1.f - RunLegs));
			}
		}
		// the whole chest follows the camera up and down, so the bow's line stays on what he aims at
		{
			const FQuat Q(FVector::XAxisVector, FMath::DegreesToRadians(In.AimPitch * 0.5f * W));
			TurnInMesh(B, Spine1, Q);
			TurnInMesh(B, Spine2, Q);
		}
	}
	// Running with the crossbow (owner 2026-11-06): a two-handed carry stays in the hands, so the life comes from the body: the chest turns against the stride, the shoulders rock, the whole
	// upper body leans in and bobs with each step, the head stays on where he is going, and both forearms pump a little together. Fades in with speed; aiming never reaches it.
	if (bCross)
	{
		const float Run = FMath::SmoothStep(230.f, 640.f, SmSpeed);
		if (Run > 0.01f)
		{
			const float Stride = FMath::Sin(2.f * PI * Phase), Step = FMath::Sin(4.f * PI * Phase + 0.6f);
			const float Turn = FMath::DegreesToRadians(15.f * Stride * Run);   // (his right foot ahead, his right shoulder back)
			for (const int32 S : { Spine, Spine1, Spine2 }) TurnInMesh(B, S, FQuat(FVector::ZAxisVector, Turn / 3.f));
			TurnInMesh(B, Spine1, FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-3.f * FMath::Sin(2.f * PI * (Phase - 0.125f)) * Run)));
			TurnInMesh(B, Spine, FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-(26.f + 1.5f * Step) * Run)));   // (owner 2026-11-06: leaning into the run, more than the clip's own lean, with a bob on each step)
			const FQuat Pump(FVector::XAxisVector, FMath::DegreesToRadians(6.f * Step * Run));
			TurnInMesh(B, RFore, Pump);
			TurnInMesh(B, LFore, Pump);
			TurnInMesh(B, Head, FQuat(FVector::ZAxisVector, -Turn * 0.7f));
			TurnInMesh(B, Head, FQuat(FVector::XAxisVector, FMath::DegreesToRadians(14.f * Run)));   // (still looking where he is going)
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
	float Lower = M->LegsFull >= 2.f ? 1.f : M->LegsFull <= 0.f ? 0.f : 1.f - FMath::Clamp((FMath::Min(Speed, SmSpeed) - 40.f) / 140.f, 0.f, 1.f);
	if (M->LegsFull > 0.f && M->LegsFull < 2.f)
	{
		// (a step in the swing must not flip the legs from the clip's to his walk's in a frame: the feet would jump half a metre)
		LegsW = Time - LegsAt > 0.25f ? Lower : FMath::FInterpConstantTo(LegsW, Lower, PlayDt, 3.f);
		LegsAt = Time;
		Lower = LegsW;
	}
	// (SpineKeep: the chest stays partly with what he was doing, so a clip's big wind-round doesn't turn his back on what he hits)
	FQuat Keep[3];
	const float KeepW = M->SpineKeep > 0.f ? FMath::Lerp(M->SpineKeep, 0.7f, FMath::Clamp(Speed / 300.f, 0.f, 1.f)) : 0.f;
	if (KeepW > 0.f) { Keep[0] = InOut.Rot[Bone[Spine]]; Keep[1] = InOut.Rot[Bone[Spine1]]; Keep[2] = InOut.Rot[Bone[Spine2]]; }
	const int32 OffArm[4] = { LShoulder, LArm, LFore, LHand };
	FQuat OffKeep[4];
	if (PlayKeepOffArm > 0.f) for (int32 i = 0; i < 4; ++i) OffKeep[i] = InOut.Rot[Bone[OffArm[i]]];
	Overlay(InOut, Over, PlayUpper, Lower * PlayLower);
	if (PlayKeepOffArm > 0.f) for (int32 i = 0; i < 4; ++i) InOut.Rot[Bone[OffArm[i]]] = Nlerp(InOut.Rot[Bone[OffArm[i]]], OffKeep[i], PlayKeepOffArm);
	PlayKeepOffArm = 0.f;
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

// Opens the right elbow by Deg x W, the hand keeping its own turn: the arm reaches further out, and the blade, its trail and its hit boxes go with it (they read the pose)
void FKnightAnimator::ExtendArm(FPose& P, float Deg, float W) const
{
	if (W <= 0.001f) return;
	FQuat HandRot;
	const FVector F = BonePos(P, RHand, &HandRot);
	const FVector K = BonePos(P, RFore), H = BonePos(P, RArm);
	const float Now = FMath::Acos(FMath::Clamp(float(FVector::DotProduct((H - K).GetSafeNormal(), (F - K).GetSafeNormal())), -1.f, 1.f));
	const float Then = FMath::Min(Now + FMath::DegreesToRadians(Deg) * W, FMath::DegreesToRadians(172.f));
	FVector Axis = FVector::CrossProduct(H - K, F - K);
	if (Then <= Now || !Axis.Normalize()) return;
	TurnInMesh(P, RFore, FQuat(Axis, Then - Now));
	FQuat Turned;
	BonePos(P, RHand, &Turned);
	TurnInMesh(P, RHand, HandRot * Turned.Inverse());
}

// Both hands on a chopping or mining tool (owner 2026-11-06). These clips are made for a long two-handed weapon, so the arms need nothing: the haft lies on the line through the two fists' real grip
// points (measured, not guessed: a fist's grip point moves as the wrist turns, so this goes round a few times), each hand turning only the shortest way onto it. The tool then rolls in the fist about
// its own haft until its edge leads, and the hand has no part in that: turning the hand as well broke the wrist back into the forearm. And it slides back through the right fist until its butt is past
// the left: HeavySwing's hands are up to 34 cm apart, and an axe has 16 cm behind the grip.
static float ToolSlide(int32 Kind) { return Kind == JeebItems::K_Axe ? 30.f : 18.f; }
static float ToolEdgeSign(int32 Kind) { return Kind == JeebItems::K_Axe ? -1.f : 1.f; }   // (the axe's head is built on the -X side of its haft, so that it faces forward as he carries it)

static FVector GripInRightHand(const FTransform& RightGrip) { return RightGrip.TransformPosition(KnightDesign::GripPos(false) - KnightDesign::PivotPos(KnightDesign::P_WristR)); }
static FVector GripInLeftHand(const FTransform& LeftGrip) { return LeftGrip.TransformPosition(KnightDesign::GripPos(true) - KnightDesign::PivotPos(KnightDesign::P_ElbowL)); }

float FClipLib::ToolPose(FPose& P, float W, int32 Kind) const
{
	const FVector RG = GripInRightHand(RightGrip), LG = GripInLeftHand(LeftGrip);
	const FVector LeftHole = LeftGrip.TransformVectorNoScale(KnightDesign::BladeDir(-1)).GetSafeNormal();
	const FQuat R0 = P.Rot[Bone[RHand]], L0 = P.Rot[Bone[LHand]];
	FVector Dir = FVector::ZeroVector;
	FQuat HR, LR;
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector A = BonePos(P, RHand, &HR), B = BonePos(P, LHand, &LR);
		Dir = (A + HR.RotateVector(RG) - B - LR.RotateVector(LG)).GetSafeNormal();
		if (Dir.IsNearlyZero()) return 0.f;
		TurnInMesh(P, RHand, FQuat::FindBetweenNormals(HR.RotateVector(BladeInHand).GetSafeNormal(), Dir));
		TurnInMesh(P, LHand, FQuat::FindBetweenNormals(LR.RotateVector(LeftHole).GetSafeNormal(), Dir));
	}
	// (the edge: square to his right, so it leads in a swing up and down in front of him. Where the tool lies nearly along his right that means nothing, and it keeps the roll it has.)
	BonePos(P, RHand, &HR);
	const FVector Cross = FVector::CrossProduct(FVector(-1.f, 0.f, 0.f), Dir);
	const float Sure = FMath::SmoothStep(0.2f, 0.5f, float(Cross.Size()));
	const FVector Want = Cross.GetSafeNormal();
	FVector Now = HR.RotateVector(ToolEdgeInHand) * ToolEdgeSign(Kind);
	Now = (Now - Dir * FVector::DotProduct(Now, Dir)).GetSafeNormal();
	const float Roll = FMath::Atan2(float(FVector::DotProduct(FVector::CrossProduct(Now, Want), Dir)), float(FVector::DotProduct(Now, Want))) * Sure;
	P.Rot[Bone[RHand]] = Nlerp(R0, P.Rot[Bone[RHand]], W);
	P.Rot[Bone[LHand]] = Nlerp(L0, P.Rot[Bone[LHand]], W);
	return Roll * W;
}

FVector FClipLib::ToolStrike(const FTransform& Grip, int32 Kind) const
{
	// (from the grip. The axe as WeaponDesign builds it; the pickaxe's point measured on its model, Tools/prop_clean.py's Pickaxe.fbx: 68.5 down the haft, 24.7 out)
	const float HeadAlong = Kind == JeebItems::K_Pickaxe ? 68.5f : 64.f, EdgeOut = Kind == JeebItems::K_Pickaxe ? 24.7f : 17.f;
	return Grip.TransformPosition(KnightDesign::GripPos(false) - KnightDesign::PivotPos(KnightDesign::P_WristR))
		+ Grip.TransformVectorNoScale(KnightDesign::BladeDir(1)).GetSafeNormal() * HeadAlong
		+ Grip.TransformVectorNoScale(ToolEdgeTurn.RotateVector(FVector::ForwardVector)).GetSafeNormal() * EdgeOut * ToolEdgeSign(Kind);
}

// The tool's roll about its haft and its slide along it, applied to where the fist has it
static void PlaceToolInFist(FTransform& G, float Roll, float Slide)
{
	const FVector Along = G.TransformVectorNoScale(KnightDesign::BladeDir(1)).GetSafeNormal();
	const FVector Pivot = G.TransformPosition(KnightDesign::GripPos(false) - KnightDesign::PivotPos(KnightDesign::P_WristR));
	const FQuat Q(Along, Roll);
	G.SetLocation(Pivot + Q.RotateVector(G.GetLocation() - Pivot) - Along * Slide);
	G.SetRotation((Q * G.GetRotation()).GetNormalized());
}

FTransform FKnightAnimator::RightGripWorld() const
{
	FTransform G = L.RightGrip * BoneWorld(JeebAnim::RHand);
	if (TwoHandW > 0.005f) PlaceToolInFist(G, ToolRoll, ToolSlide(ToolKind) * FMath::SmoothStep(0.f, 1.f, TwoHandW));
	return G;
}

void FKnightAnimator::ToolHands(float& OutOffHaft, float& OutFromButt) const
{
	const FTransform Fist = L.RightGrip * BoneWorld(JeebAnim::RHand);
	const FVector Along = Fist.TransformVectorNoScale(KnightDesign::BladeDir(1)).GetSafeNormal();
	const FVector Rg = BoneWorld(JeebAnim::RHand).TransformPosition(GripInRightHand(L.RightGrip)), Lg = BoneWorld(JeebAnim::LHand).TransformPosition(GripInLeftHand(L.LeftGrip));
	const float Back = float(FVector::DotProduct(Rg - Lg, Along));
	OutOffHaft = float(FVector::Dist(Lg + Along * Back, Rg));
	OutFromButt = 16.f + ToolSlide(ToolKind) - Back;   // (an axe has 16 cm of haft behind its grip)
}

void FKnightAnimator::TwoHandTool(FPose& P, const FInput& In, float Dt)
{
	const bool bOn = StateKey == 73 || StateKey == 74;
	TwoHandW = FMath::FInterpConstantTo(TwoHandW, bOn ? 1.f : 0.f, Dt, 3.3f);   // (about 0.3 s, eased: the tool turns from how he carries it into both hands without a jump)
	if (bOn) ToolKind = In.Kind;
	ToolRoll = TwoHandW >= 0.005f ? L.ToolPose(P, FMath::SmoothStep(0.f, 1.f, TwoHandW), ToolKind) : 0.f;
}

// A two-bone reach: the arm bends just enough, aims at the spot, then turns round the line from the shoulder to it until the elbow is on the side the hint says. The hand's own turn is the caller's to restore.
void FClipLib::ReachTo(FPose& P, int32 Upper, int32 Mid, int32 End, const FVector& Target, const FVector& ElbowHint) const
{
	const FVector S = BonePos(P, Upper), E = BonePos(P, Mid), H = BonePos(P, End);
	const float A = float(FVector::Dist(S, E)), C = float(FVector::Dist(E, H));
	const float D = FMath::Clamp(float(FVector::Dist(S, Target)), FMath::Abs(A - C) + 1.f, A + C - 0.5f);
	const float Now = FMath::Acos(FMath::Clamp(float(FVector::DotProduct((S - E).GetSafeNormal(), (H - E).GetSafeNormal())), -1.f, 1.f));
	const float Then = FMath::Acos(FMath::Clamp((A * A + C * C - D * D) / (2.f * A * C), -1.f, 1.f));
	FVector Axis = FVector::CrossProduct(S - E, H - E);
	if (Axis.Normalize()) TurnInMesh(P, Mid, FQuat(Axis, Then - Now));
	TurnInMesh(P, Upper, FQuat::FindBetweenNormals((BonePos(P, End) - S).GetSafeNormal(), (Target - S).GetSafeNormal()));
	const FVector Line = (Target - S).GetSafeNormal();
	const FVector ToElbow = BonePos(P, Mid) - S;
	const FVector Cur = (ToElbow - Line * FVector::DotProduct(ToElbow, Line)).GetSafeNormal();
	const FVector Want = (ElbowHint - Line * FVector::DotProduct(ElbowHint, Line)).GetSafeNormal();
	if (!Cur.IsNearlyZero() && !Want.IsNearlyZero())
		TurnInMesh(P, Upper, FQuat(Line, FMath::Atan2(float(FVector::DotProduct(FVector::CrossProduct(Cur, Want), Line)), float(FVector::DotProduct(Cur, Want)))));
}

// The left fist on the grip behind the right (the big sword, owner 2026-11-06): the hole in the fist along the blade, the wrist as straight as that allows. If the right arm is out further than the left can
// reach (the one-handed sword's clips, the low sweep, the dash), the right comes in by what is missing and the blade comes with it, as it would with both hands on one grip.
void FClipLib::BothHandsOnGrip(FPose& P, float W, float Below) const
{
	const int32 Arms[6] = { LArm, LFore, LHand, RArm, RFore, RHand };
	FQuat Was[6];
	for (int32 i = 0; i < 6; ++i) Was[i] = P.Rot[Bone[Arms[i]]];
	const FVector RG = GripInRightHand(RightGrip), LG = GripInLeftHand(LeftGrip);
	const FVector LeftHole = LeftGrip.TransformVectorNoScale(KnightDesign::BladeDir(-1)).GetSafeNormal();
	// (which way the forearm runs in the hand's own frame, with the wrist straight: from the bind pose)
	FPose R0;
	R0.Rot = RefRot;
	R0.Hip = RefPos[Bone[Hips]];
	FQuat BindHand;
	const FVector BindWrist = BonePos(R0, LHand, &BindHand);
	const FVector ForeInHand = BindHand.UnrotateVector((BindWrist - BonePos(R0, LFore)).GetSafeNormal());
	const float ArmLen = float(FVector::Dist(BonePos(P, LArm), BonePos(P, LFore)) + FVector::Dist(BonePos(P, LFore), BonePos(P, LHand)));
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		FQuat HR, LR;
		const FVector RWrist = BonePos(P, RHand, &HR);
		const FVector Dir = HR.RotateVector(BladeInHand).GetSafeNormal();
		const FVector Grip = RWrist + HR.RotateVector(RG) - Dir * Below;
		// the hand first: along the blade, then rolled about it so the wrist is as straight as it can be
		BonePos(P, LHand, &LR);
		TurnInMesh(P, LHand, FQuat::FindBetweenNormals(LR.RotateVector(LeftHole).GetSafeNormal(), Dir));
		BonePos(P, LHand, &LR);
		const FVector Fore = (Grip - LR.RotateVector(LG) - BonePos(P, LFore)).GetSafeNormal();
		const FVector WantFlat = (Fore - Dir * FVector::DotProduct(Fore, Dir)).GetSafeNormal();
		const FVector NowDir = LR.RotateVector(ForeInHand);
		const FVector NowFlat = (NowDir - Dir * FVector::DotProduct(NowDir, Dir)).GetSafeNormal();
		if (!WantFlat.IsNearlyZero() && !NowFlat.IsNearlyZero())
			TurnInMesh(P, LHand, FQuat(Dir, FMath::Atan2(float(FVector::DotProduct(FVector::CrossProduct(NowFlat, WantFlat), Dir)), float(FVector::DotProduct(NowFlat, WantFlat)))));
		BonePos(P, LHand, &LR);
		FVector Wrist = Grip - LR.RotateVector(LG);
		// the right arm comes in if the left can't get there
		const FVector Shoulder = BonePos(P, LArm);
		const float Over = float(FVector::Dist(Wrist, Shoulder)) - (ArmLen - 1.f);
		if (Over > 0.f)
		{
			const FVector Pull = (Shoulder - Wrist).GetSafeNormal() * Over;
			const FVector RS = BonePos(P, RArm);
			ReachTo(P, RArm, RFore, RHand, RWrist + Pull, BonePos(P, RFore) - RS);
			FQuat Turned;
			BonePos(P, RHand, &Turned);
			TurnInMesh(P, RHand, HR * Turned.Inverse());
			Wrist += Pull;
		}
		ReachTo(P, LArm, LFore, LHand, Wrist, FVector(0.7f, -0.2f, -1.f));
		FQuat Turned;
		BonePos(P, LHand, &Turned);
		TurnInMesh(P, LHand, LR * Turned.Inverse());
	}
	if (W < 0.999f) for (int32 i = 0; i < 6; ++i) P.Rot[Bone[Arms[i]]] = Nlerp(Was[i], P.Rot[Bone[Arms[i]]], W);
}

// When the big sword is in both hands: every attack, and on guard standing still. Walking or running, he carries it in the right. In its own swings (the two-handed clip) the blade lies on the line through his fists.
void FKnightAnimator::GreatGripLine(FPose& P, const FInput& In, float Dt)
{
	const bool bGreat = In.Kind == JeebItems::K_Broadsword;
	const bool bAttack = StateKey >= 30 && StateKey < 40;
	const bool bOwnClip = bGreat && (StateKey == 30 || StateKey == 31 || StateKey == 32 || StateKey == 37);
	GreatW = FMath::FInterpConstantTo(GreatW, bGreat && (bAttack || (bOnGuard && !bMoving && StateKey == 2)) ? 1.f : 0.f, Dt, bAttack ? 10.f : 4.f);
	GreatLineW = FMath::FInterpConstantTo(GreatLineW, bOwnClip ? 1.f : 0.f, Dt, 10.f);
	if (GreatLineW > 0.005f) L.ToolPose(P, FMath::SmoothStep(0.f, 1.f, GreatLineW), In.Kind);
}

// Holds a cut at a height, by how far each part must go: the upper arm brings the hand to HandWant, the wrist brings the point to TipWant. The way the blade points across the floor stays the clip's,
// no faster than a fast swing where it passes nearly straight up or down (there it has no real heading and could swap sides in a frame).
void FKnightAnimator::LevelCut(FPose& B, int32 Kind, float W, float HandWant, float TipWant, float Dt)
{
	if (W <= 0.001f) return;
	const FVector Sh = BonePos(B, RArm);
	const FVector ToHand = BonePos(B, RHand) - Sh;
	const float Len = FMath::Max(float(ToHand.Size()), 1.f);
	const FVector FlatArm = FVector(ToHand.X, ToHand.Y, 0.f).GetSafeNormal();
	if (!FlatArm.IsNearlyZero())
	{
		const float SinE = FMath::Clamp((HandWant - float(Sh.Z)) / Len, -0.98f, 0.98f);
		const FVector WantArm = FlatArm * FMath::Sqrt(1.f - SinE * SinE) + FVector::UpVector * SinE;
		TurnInMesh(B, RArm, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(ToHand / Len, WantArm), W));
	}
	FQuat HandRot;
	const FVector HandAt = BonePos(B, RHand, &HandRot);
	const FVector Dir = HandRot.RotateVector(L.BladeInHand).GetSafeNormal();
	const float RawHeading = FVector2D(Dir.X, Dir.Y).Size() > 0.02f ? FMath::Atan2(float(Dir.Y), float(Dir.X)) : CutHeading;
	const float MaxTurn = FMath::DegreesToRadians(2600.f) * Dt;
	CutHeading = Time - CutAt > 0.1f ? RawHeading : FMath::UnwindRadians(CutHeading + FMath::Clamp(FMath::FindDeltaAngleRadians(CutHeading, RawHeading), -MaxTurn, MaxTurn));
	CutAt = Time;
	const FVector FlatDir(FMath::Cos(CutHeading), FMath::Sin(CutHeading), 0.f);
	const float SinB = FMath::Clamp((TipWant - float(HandAt.Z)) / (9.f + WeaponDesign::BladeReach(Kind)), -0.98f, 0.98f);
	const FVector WantDir = FlatDir * FMath::Sqrt(1.f - SinB * SinB) + FVector::UpVector * SinB;
	TurnInMesh(B, RHand, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Dir, WantDir), W));
}

// How far the arm reaches out through a swing: U through it, From..To its hit frames
static float SwingReach(float U, float From, float To) { return FMath::SmoothStep(From * 0.3f, From + 0.1f, U) * (1.f - FMath::SmoothStep(To, 1.f, U)); }
static constexpr float Jab1Reach = 34.f, Jab2Reach = 24.f;
static constexpr float ChargeApexU = 0.25f;   // (AKnightCharacter::SmashApexT: where the first swing waits while the button decides)

// A boot flat on the floor being dragged across it, in a swing's recovery or rising from a slide, lifts for those frames: a small step, not a slide
void FKnightAnimator::LiftDraggedFeet(FPose& P, const FInput& In, float Dt)
{
	const bool bOn = !In.bFalling && StateKey != 72 && ((StateKey >= 30 && StateKey < 40) || LiftWindow > 0.f);
	LiftWindow = FMath::Max(0.f, LiftWindow - Dt);
	if (!bOn && FootLift[0] <= 0.f && FootLift[1] <= 0.f) return;
	const FVector VW = FRotator(0.f, -In.Rot.Yaw - MeshYawDeg, 0.f).RotateVector(FVector(In.Vel.X, In.Vel.Y, 0.f));
	const int32 Foot[2] = { LFoot, RFoot }, Knee[2] = { LLeg, RLeg }, Thigh[2] = { LUp, RUp };
	for (int32 i = 0; i < 2; ++i)
	{
		FQuat FootRot;
		const FVector F = BonePos(P, Foot[i], &FootRot);
		const float Slide = F.Z < 14.f ? FVector2D((F - FootPrev[i]) / FMath::Max(Dt, 0.001f) + VW).Size() : 0.f;
		FootPrev[i] = F;
		const float Want = bOn && Dt < 0.05f ? FMath::Clamp((Slide - 60.f) * 0.12f, 0.f, 9.f) : 0.f;
		FootLift[i] = FMath::FInterpTo(FootLift[i], Want, Dt, Want > FootLift[i] ? 12.f : 10.f);
		if (FootLift[i] < 0.2f) { FootLift[i] = Want > 0.f ? FootLift[i] : 0.f; continue; }
		// (the knee bends just enough, then the thigh aims the foot at the spot; the boot keeps its own angle)
		const FVector H = BonePos(P, Thigh[i]), K = BonePos(P, Knee[i]), T = F + FVector(0.f, 0.f, FootLift[i]);
		const float A = FVector::Dist(H, K), C = FVector::Dist(K, F);
		const float D = FMath::Clamp(float(FVector::Dist(H, T)), FMath::Abs(A - C) + 1.f, A + C - 0.5f);
		const float Now = FMath::Acos(FMath::Clamp(float(FVector::DotProduct((H - K).GetSafeNormal(), (F - K).GetSafeNormal())), -1.f, 1.f));
		const float Then = FMath::Acos(FMath::Clamp((A * A + C * C - D * D) / (2.f * A * C), -1.f, 1.f));
		FVector Axis = FVector::CrossProduct(H - K, F - K);
		if (Axis.Normalize()) TurnInMesh(P, Knee[i], FQuat(Axis, Then - Now));
		const FVector F2 = BonePos(P, Foot[i]);
		TurnInMesh(P, Thigh[i], FQuat::FindBetweenNormals((F2 - H).GetSafeNormal(), (T - H).GetSafeNormal()));
		FQuat Turned;
		BonePos(P, Foot[i], &Turned);
		TurnInMesh(P, Foot[i], FootRot * Turned.Inverse());
	}
}

int32 FKnightAnimator::ChooseAction(float Dt, const FInput& In, FPose& B, float& OutBlend)
{
	const FVector V = In.Vel;
	const float Speed = FVector2D(V.X, V.Y).Size();
	const bool bAir = In.bFalling;
	PlayDt = Dt;

	// landing and being hit are moments, not states: catch them here
	if (bAir) { AirT += Dt; PeakFall = FMath::Max(PeakFall, float(-V.Z)); bWasAir = true; }
	else
	{
		LandT = bWasAir ? 0.f : LandT + Dt;
		if (bWasAir)
		{
			bLandHard = PeakFall > 1250.f; LandStrength = FMath::Clamp(0.3f + PeakFall / 1200.f, 0.3f, 1.f); bWasAir = false; PeakFall = 0.f;
			LandFor = 0.f;
			if (const FMove* M = bLandHard ? Moves.Find(TEXT("LandHard")) : nullptr) LandFor = (M->ClipEnd - M->ClipStart) / FMath::Max(M->ClipHit, 0.1f);
		}
		AirT = 0.f;
	}
	if (In.StunLeft > PrevStun + 0.01f)
	{
		++HitSerial;
		// which way the blow pushed him picks the flinch
		const FVector LV = In.Rot.UnrotateVector(V);
		const bool bPushed = FVector2D(LV.X, LV.Y).Size() > 60.f;
		HitPick = In.StunLeft >= 0.5f ? 0 : (!bPushed || LV.X < -FMath::Abs(LV.Y)) ? 1 + (HitSerial % 2) : LV.X > FMath::Abs(LV.Y) ? 3 : LV.Y > 0.f ? 4 : 5;
	}
	PrevStun = In.StunLeft;
	if (In.bKnockedDown && !bWasDown) { bFallPushed = Speed > 100.f; }
	bWasDown = In.bKnockedDown;
	if (In.bKnockedDown && In.KnockPhase == 1 && RiseLeft <= 0.f) RiseLeft = 1.f;   // (the rising plays on a little past the locked part, unless he moves off)

	// Which state
	int32 Key = 0;
	if (In.bKnockedDown) Key = In.KnockPhase == 1 ? 11 : 10;
	else if (In.bRolling) Key = 20;
	else if (In.Move != EMove::None)
	{
		switch (In.Move)
		{
		case EMove::Rebound: Key = 36; break;
		case EMove::Kick: Key = 33; break;
		case EMove::Sweep: Key = 34; break;
		case EMove::Dash: Key = 35; break;
		case EMove::SmashCharge: Key = 32; break;
		case EMove::SmashSwing: case EMove::SmashEnd: Key = 37; break;
		case EMove::Jab2: Key = 31; break;
		case EMove::Thrust: Key = 38; break;
		case EMove::Cast: Key = 39; break;
		default: Key = 30; break;
		}
	}
	else if (In.StunLeft > 0.f) Key = 60 + HitSerial % 3;
	else if (RiseLeft > 0.f && !bMoving) Key = 11;
	else if (In.bDrinking) Key = 76;
	else if (In.bClimbing) Key = 70;
	else if (In.bSitting) Key = 71;
	else if (In.bSliding) Key = 72;
	else if (In.bSwinging) Key = In.bSideways ? 73 : 74;
	else if (In.bFishing) Key = 75;
	else if (!bAir && bLandHard && LandT < LandFor && !bMoving && In.BowMode == 0) Key = 51;
	if (Key != 11) RiseLeft = 0.f;
	if ((Key >= 30 && Key < 40) || (Key >= 60 && Key < 70) || Key == 10 || Key == 11) CombatT = 4.f; else CombatT = FMath::Max(CombatT - Dt, 0.f);

	ActionT = Key == LastActionKey ? ActionT + Dt : 0.f;
	LastActionKey = Key;

	// Rolls go the way he rolls, whichever way he faces
	float YawWant = 0.f;
	bRollBack = false;
	if (Key == 20)
	{
		float A = In.RollYaw;
		if (FMath::Abs(A) > 135.f) { bRollBack = true; A = FMath::UnwindDegrees(A + 180.f); }
		YawWant = A;
	}
	YawNow = FMath::FInterpTo(YawNow, YawWant, Dt, Key == 20 ? 22.f : 12.f);

	// A jump is not a move (owner, 2026-11-05): he is lifted from whatever he is doing, so the legs keep the way they face, the arms keep drawing the bow or holding the guard, and no key
	// changes, so nothing resets. Only the legs answer: the knees come up a little in the air, a long fall eases into the falling pose, and landing the knees give by how fast he came down.
	AirShare = FMath::FInterpTo(AirShare, bAir ? 1.f : 0.f, Dt, bAir ? 7.f : 16.f);
	FallShare = FMath::FInterpTo(FallShare, bAir ? FMath::SmoothStep(0.55f, 1.2f, AirT) : 0.f, Dt, bAir ? 5.f : 12.f);
	if (Key == 0)
	{
		const float Knees = AirShare * 0.33f + (LandT < 0.34f ? FMath::Sin(LandT / 0.34f * PI) * 0.8f * LandStrength : 0.f);
		if (Knees > 0.005f)
			if (const FClip* C = Get(TEXT("FallLand")))
			{
				Sample(*C, 0.5f, Over, false);
				Overlay(B, Over, 0.f, FMath::Min(Knees, 1.f));
			}
		if (FallShare > 0.005f)
			if (const FClip* C = Get(TEXT("FallIdle")))
			{
				Sample(*C, Time, Over, true);
				Overlay(B, Over, In.BowMode == 1 ? 0.f : 0.5f * FallShare, FallShare);   // (half at most: its arms go right up, which would throw the shield over his head)
			}
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
			PlayKeepOffArm = 0.65f;   // (the clip pushes off an empty left hand; the shield on it would stand out to the side)
			PlayAt(TEXT("Rise"), T, 1000.f, B, &OutBlend);
			RiseLeft = Show - ActionT;
		}
		break;
	}
	case 20:
	{
		if (const FMove* M = Moves.Find(TEXT("Roll")))
		{
			const float U = FMath::Clamp(In.RollU, 0.f, 1.f);
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
		const bool bGreat = In.Kind == JeebItems::K_Broadsword;   // (the big sword has its own two-handed rows; it has no shield to keep still)
		const TCHAR* Nm = b2 ? (bGreat ? TEXT("Great_Jab2") : TEXT("Jab2")) : (bGreat ? TEXT("Great_Jab1") : TEXT("Jab1"));
		if (const FMove* M = Moves.Find(Nm))
		{
			const float U = In.MoveU;
			const float From = b2 ? Tn.Jab2From : Tn.Jab1From, To = b2 ? Tn.Jab2To : Tn.Jab1To;
			// (owner 2026-11-06) The first swing and a charge start the same way, since nobody can tell yet which it is: the blade only comes up beside him, and the deep draw is the charge's
			// (case 32). It used to go on behind his back here and then come forward again for the charge. Let go, and it goes from there into the cut.
			float W = 1.f;
			if (!b2)
				if (const FMove* J = Moves.Find(bGreat ? TEXT("Great_JabCharge") : TEXT("JabCharge")))
				{
					W = FMath::SmoothStep(ChargeApexU, From + 0.12f, U);
					PlayKeepOffArm = bGreat ? 0.f : 0.6f;
					PlayAt(bGreat ? TEXT("Great_JabCharge") : TEXT("JabCharge"), MoveClipTime(*J, ChargeApexU, Tn.Jab1From, Tn.Jab1To), Speed, B, &OutBlend);
				}
			PlayKeepOffArm = bGreat ? 0.f : 0.6f;   // (the shield stays mostly where it is held)
			PlayUpper = PlayLower = W;
			PlayAt(Nm, MoveClipTime(*M, U, From, To), Speed, B, &OutBlend);
			if (!bGreat) ExtendArm(B, b2 ? Jab2Reach : Jab1Reach, SwingReach(U, From, To));   // (owner 2026-11-06: the arm out further in both swings)
			if (b2 && !bGreat)
			{
				// (owner 2026-11-07, a line drawn on a screenshot) The clip's cut dives from high on his left down to his knee and comes up again. It should be flat: from about his shoulders on the
				// left, across in front of him, a little lower on the right. The clip still says when and which way round; this only says how high.
				const float Share = FMath::Clamp((U - From) / FMath::Max(To - From, 0.01f), 0.f, 1.f);
				LevelCut(B, In.Kind, FMath::SmoothStep(From - 0.16f, From, U) * (1.f - FMath::SmoothStep(To, To + 0.2f, U)), FMath::Lerp(140.f, 118.f, Share), FMath::Lerp(158.f, 122.f, Share), Dt);
			}
		}
		break;
	}
	case 32: case 37:
	{
		const bool bGreat = In.Kind == JeebItems::K_Broadsword;
		const TCHAR* SmashRow = bGreat ? TEXT("Great_Smash") : TEXT("Smash");
		if (const FMove* M = Moves.Find(SmashRow))
		{
			if (In.Move == EMove::SmashCharge)
			{
				// (owner 2026-11-06) It starts exactly where the swing was holding, with the blade up beside him, and draws back into the big wind-up over the whole charge, never forward. The shake
				// is in the hand and a little of the forearm, across the blade, so the shoulders stay still.
				const float C = FMath::Clamp(In.SmashCharge, 0.f, 1.f), Creep = C * C * (3.f - 2.f * C);
				if (const FMove* J = Moves.Find(bGreat ? TEXT("Great_JabCharge") : TEXT("JabCharge")))
				{
					PlayKeepOffArm = bGreat ? 0.f : 0.6f;
					PlayAt(bGreat ? TEXT("Great_JabCharge") : TEXT("JabCharge"), MoveClipTime(*J, ChargeApexU, Tn.Jab1From, Tn.Jab1To), Speed, B, nullptr);
					if (!bGreat) ExtendArm(B, Jab1Reach, SwingReach(In.MoveU, Tn.Jab1From, Tn.Jab1To) * (1.f - Creep));
				}
				PlayKeepOffArm = bGreat ? 0.f : 0.7f;
				PlayUpper = PlayLower = Creep;
				PlayAt(SmashRow, M->ClipHit, Speed, B, &OutBlend);
				ShakePhase = FMath::Fmod(ShakePhase + Dt * (7.3f + 8.1f * C), 2.f * PI);
				FQuat HandRot;
				BonePos(B, RHand, &HandRot);
				const FVector Across = FVector::CrossProduct(HandRot.RotateVector(L.BladeInHand), FVector::UpVector).GetSafeNormal();
				if (!Across.IsNearlyZero())
				{
					const float Shake = FMath::DegreesToRadians((1.5f + 2.5f * C) * FMath::SmoothStep(0.f, 0.1f, C)) * FMath::Sin(ShakePhase);
					TurnInMesh(B, RFore, FQuat(Across, Shake * 0.35f));
					TurnInMesh(B, RHand, FQuat(Across, Shake * 0.65f));
				}
			}
			else
			{
				float T = M->ClipHit;
				if (In.Move == EMove::SmashSwing)
					T = FMath::Lerp(M->ClipHit, M->ClipFollow, In.SmashSwingU);
				else
					// (the clip loops, so it reaches its end still at full speed: he slows into the stance instead of stopping dead)
					T = FMath::Lerp(M->ClipFollow, M->ClipEnd, FMath::Sin(In.SmashEndU * HALF_PI));
				PlayKeepOffArm = bGreat ? 0.f : 0.7f;   // (the clip lays the shield out flat like a tray: it stays mostly where the stance has it)
				PlayAt(SmashRow, T, Speed, B, &OutBlend);
				OutBlend = FMath::Lerp(0.2f, 0.05f, In.SmashCharge);   // (from the held wind-up into the swing: let go early and he is still nearer the small pull-back, and has further to come)
			}
		}
		break;
	}
	case 33:
		if (const FMove* M = Moves.Find(TEXT("Kick")))
			PlayAt(TEXT("Kick"), MoveClipTime(*M, In.MoveU, Tn.KickActiveFrom, Tn.KickActiveTo), 1000.f, B, &OutBlend);
		break;
	case 34:
		if (const FMove* M = Moves.Find(TEXT("Sweep")))
		{
			// (from the end of the cut the clip waits, the hand stays on the side the cut finished, and only the wrist moves: one curl up, then the slow way back to the guard with the tip already high)
			PlayKeepOffArm = 0.7f;   // (as in the smash: the clip lays the shield out flat)
			PlayAt(TEXT("Sweep"), MoveClipTime(*M, FMath::Min(In.MoveU, Tn.SweepActiveTo), Tn.SweepActiveFrom, Tn.SweepActiveTo), 1000.f, B, &OutBlend);
			// The clip cuts at his waist, and this is a cut at the ankles (owner, 2026-11-05, with a line drawn on a screenshot): over the hit frames the chest leans in, the arm drops and
			// the wrist points the blade down, so the tip skims just over the floor and curls back up at the end. By how far each must go, so that it fades in and out with the move.
			const float W = FMath::SmoothStep(Tn.SweepActiveFrom - 0.24f, Tn.SweepActiveFrom, In.MoveU) * (1.f - 0.6f * FMath::SmoothStep(Tn.SweepActiveTo, 1.f, In.MoveU));
			const float Curl = FMath::SmoothStep(Tn.SweepActiveTo - 0.08f, 1.f, In.MoveU);
			if (W > 0.001f)
			{
				constexpr float LeanDeg = 20.f, HandWant = 44.f, TipWant = 12.f;
				TurnInMesh(B, Spine, FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-LeanDeg * W)));
				ExtendArm(B, 45.f, W);   // (owner 2026-11-06: stretched well out; the blade and its hit boxes go with the hand)
				const FVector Sh = BonePos(B, RArm);
				const FVector ToHand = BonePos(B, RHand) - Sh;
				const float Len = FMath::Max(float(ToHand.Size()), 1.f);
				const FVector FlatArm = FVector(ToHand.X, ToHand.Y, 0.f).GetSafeNormal();
				if (!FlatArm.IsNearlyZero() && ToHand.Z / Len > (HandWant - Sh.Z) / Len)
				{
					const float SinE = FMath::Clamp((HandWant - float(Sh.Z)) / Len, -0.98f, 0.98f);
					const FVector WantArm = FlatArm * FMath::Sqrt(1.f - SinE * SinE) + FVector::UpVector * SinE;
					TurnInMesh(B, RArm, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(ToHand / Len, WantArm), W));
				}
				FQuat HandRot;
				const FVector HandAt = BonePos(B, RHand, &HandRot);
				const FVector Dir = HandRot.RotateVector(L.BladeInHand).GetSafeNormal();
				// (the clip's blade passes nearly straight up or down on the way in, where it has no real heading and can swap sides in a frame: the heading across the floor turns no faster than a fast swing)
				const float RawHeading = FVector2D(Dir.X, Dir.Y).Size() > 0.02f ? FMath::Atan2(float(Dir.Y), float(Dir.X)) : SweepHeading;
				const float MaxHeadingTurn = FMath::DegreesToRadians(W < 0.999f ? 1500.f : 2600.f) * Dt;   // (slower on the way in: the point comes round once, never back and forth)
				SweepHeading = Time - SweepAt > 0.1f ? RawHeading : FMath::UnwindRadians(SweepHeading + FMath::Clamp(FMath::FindDeltaAngleRadians(SweepHeading, RawHeading), -MaxHeadingTurn, MaxHeadingTurn));
				SweepAt = Time;
				const FVector FlatDir(FMath::Cos(SweepHeading), FMath::Sin(SweepHeading), 0.f);
				const float Reach = 9.f + WeaponDesign::BladeReach(In.Kind);
				{
					const float SinB = FMath::Lerp(FMath::Clamp((TipWant - float(HandAt.Z)) / Reach, -0.98f, 0.98f), 0.8f, Curl);
					const FVector WantDir = FlatDir * FMath::Sqrt(1.f - SinB * SinB) + FVector::UpVector * SinB;
					TurnInMesh(B, RHand, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Dir, WantDir), FMath::Max(W, Curl)));
				}
			}
		}
		break;
	case 35:
		if (const FMove* M = Moves.Find(TEXT("Dash")))
		{
			// (the end lag: he draws himself back up out of the lunge, not all at once at the end. DashFrom/DashTo: its hit frames as shares of the move)
			PlayUpper = PlayLower = 1.f - FMath::SmoothStep(0.5f, 1.f, In.MoveU);
			PlayAt(TEXT("Dash"), MoveClipTime(*M, In.MoveU, In.DashFrom, In.DashTo), 1000.f, B, &OutBlend);
		}
		break;
	case 36:
		if (const FMove* M = Moves.Find(TEXT("Rebound")))
			PlayAt(TEXT("Rebound"), FMath::Lerp(M->ClipStart, M->ClipEnd, FMath::Clamp(In.ReboundU, 0.f, 1.f)), Speed, B, &OutBlend);
		break;
	case 51:   // a hard landing from standing: the knees take it, his arms stay his own
		if (const FMove* M = Moves.Find(TEXT("LandHard")))
		{
			PlayUpper = 0.35f;
			PlayAt(TEXT("LandHard"), M->ClipStart + LandT * M->ClipHit, Speed, B, &OutBlend);
		}
		break;
	case 76:   // drinking a potion: the arm brings the bottle up, the off arm stays where it is, the legs keep walking
		if (const FMove* M = Moves.Find(TEXT("Drink")))
		{
			PlayKeepOffArm = 1.f;
			PlayAt(TEXT("Drink"), FMath::Lerp(M->ClipStart, M->ClipEnd, FMath::Clamp(In.DrinkU, 0.f, 1.f)), Speed, B, &OutBlend);
		}
		break;
	case 38: case 39:   // the spear's thrust and a wand's or staff's cast: the sword's lunge with the arms only
		if (const FMove* M = Moves.Find(TEXT("Dash")))
		{
			PlayLower = 0.f;
			PlayUpper = 1.f - FMath::SmoothStep(0.6f, 1.f, In.MoveU);
			PlayAt(TEXT("Dash"), MoveClipTime(*M, In.MoveU, 0.35f, 0.6f), Speed, B, &OutBlend);
		}
		break;
	case 70:   // up a ladder: hand over hand with how far he has climbed
		if (const FMove* M = Moves.Find(TEXT("Climb")))
		{
			const float U = FMath::Fmod(FMath::Abs(In.ClimbPhase) / (2.f * PI), 1.f);
			PlayAt(TEXT("Climb"), FMath::Lerp(M->ClipStart, M->ClipEnd, U), 1000.f, B, &OutBlend);
			B.Hip.Z -= 68.f;   // (the clip climbs on the spot well above its own floor: his hips where they are standing)
			// (Mixamo's climber faces the other way from every other clip: he has to face the ladder)
			TurnInMesh(B, Hips, FQuat(FVector::UpVector, PI));
			B.Hip.X = -B.Hip.X; B.Hip.Y = -B.Hip.Y;
		}
		break;
	case 71:   // sitting: down into the seat over ClipStart..ClipHit, then held
		if (const FMove* M = Moves.Find(TEXT("Sit")))
			PlayAt(TEXT("Sit"), FMath::Min(M->ClipStart + ActionT, M->ClipHit), 1000.f, B, &OutBlend);
		break;
	case 72:   // sliding: into the slide over ClipStart..ClipHit, then held
		if (const FMove* M = Moves.Find(TEXT("Slide")))
		{
			PlayAt(TEXT("Slide"), FMath::Min(M->ClipStart + ActionT, M->ClipHit), 1000.f, B, &OutBlend);
			// (the left arm from one still pose, on guard, not from a walk cycle that keeps running under him and swings the shield)
			if (const FClip* G = Get(TEXT("Idle_SS")))
			{
				Sample(*G, 0.f, Scratch, false);
				for (const int32 i : { LShoulder, LArm, LFore, LHand }) B.Rot[Bone[i]] = Scratch.Rot[Bone[i]];
			}
			// (the pose tips his shoulders down to his right, which from the camera behind him reads as falling over: mesh +Y is ahead, so this lifts him back towards his left)
			TurnInMesh(B, Spine, FQuat(FVector::YAxisVector, FMath::DegreesToRadians(24.f)));
			TurnInMesh(B, Spine2, FQuat(FVector::YAxisVector, FMath::DegreesToRadians(10.f)));
		}
		break;
	case 73: case 74:   // a tool at work (owner 2026-11-06: it was a quick draw back and a very fast blow). Each swing starts with the tool up (ClipStart): a blow that gathers speed down to ClipFollow,
	{                   // a moment stuck in the rock, then a long unhurried lift back up. ClipHit is not used here. (Both hands on the tool: the shield goes on his back meanwhile.)
		const TCHAR* Nm = Key == 73 ? TEXT("Chop") : TEXT("Mine");
		if (const FMove* M = Moves.Find(Nm))
		{
			constexpr float BlowEnds = 0.32f, LiftFrom = 0.42f;
			const float U = In.SwingU;
			const float A = U < BlowEnds ? FMath::Pow(U / BlowEnds, 2.f) : U < LiftFrom ? 1.f : 1.f - FMath::SmoothStep(LiftFrom, 1.f, U);
			PlayAt(Nm, FMath::Lerp(M->ClipStart, M->ClipFollow, A), Speed, B, &OutBlend);
		}
		break;
	}
	case 75:   // fishing: the cast, then waiting
		if (const FMove* M = Moves.Find(TEXT("FishCast")))
		{
			if (ActionT < M->ClipEnd - M->ClipStart) { PlayKeepOffArm = 1.f; PlayAt(TEXT("FishCast"), M->ClipStart + ActionT, 1000.f, B, &OutBlend); }
			else if (const FClip* C = Get(TEXT("FishIdle")))
			{
				const int32 OffArm[4] = { LShoulder, LArm, LFore, LHand };
				FQuat OffKeep[4];
				for (int32 i = 0; i < 4; ++i) OffKeep[i] = B.Rot[Bone[OffArm[i]]];
				Sample(*C, Time, Over, true);
				Overlay(B, Over, 1.f, 1.f);
				for (int32 i = 0; i < 4; ++i) B.Rot[Bone[OffArm[i]]] = OffKeep[i];
			}
		}
		break;
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

// ---------------- Gear on the bones ----------------

// Where bone B sits in the game's frame (forward +X, right +Y, up +Z; origin between his feet), for pose P
FTransform FClipLib::BindInGame(const FPose& P, int32 B) const
{
	FQuat R;
	const FVector Pos = BonePos(P, B, &R);
	return FTransform(R, Pos) * FTransform(FRotator(0.f, MeshYawDeg, 0.f));
}

void FClipLib::SolveGear()
{
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
		// The real grip, measured on Mixamo's own Paladin W/Prop, who holds a sword on these clips (2026-11-05, Tools/knight_ref_grip.py).
		// In his T-pose the blade leaves the fist forward, 14 degrees out and a little down; carried through the same line-up the clips use (knight_clip_retarget.py), that is
		// SwordInBind on our knight's bind pose. So in every sword clip the hand turns just far enough to bring the fist's own hole onto that line, and the blade then follows the
		// Paladin's exactly. (His rest pose, walk and run were made on his own skeleton: those keep the fist as it is.)
		BladeInHand = RightGrip.TransformVectorNoScale(KnightDesign::BladeDir(1)).GetSafeNormal();
		{
			const FVector E = KM::DownTo(KnightDesign::BladeDir(1));   // (a tool is built with its haft along -Z and its edge along +X, turned onto the blade's line as FistPlace does)
			ToolEdgeTurn = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(E.Z)) * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(E.Y)) * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(E.X));
			ToolEdgeInHand = RightGrip.TransformVectorNoScale(ToolEdgeTurn.RotateVector(FVector::ForwardVector)).GetSafeNormal();
		}
		static const FVector SwordInBind = FVector(-0.2543f, 0.9586f, 0.1285f).GetSafeNormal();
		FQuat HandBind;
		BonePos(P, RHand, &HandBind);
		const FVector TrueInHand = HandBind.Inverse().RotateVector(SwordInBind);
		const FQuat Fit = FQuat::FindBetweenNormals(BladeInHand, TrueInHand);
		UE_LOG(LogTemp, Log, TEXT("JEEBANIM grip: the fist's hole against the real sword %.1f deg"), FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct(BladeInHand, TrueInHand)), -1.f, 1.f))));
		const int32 NB = Parents.Num(), HandB = Bone[RHand];
		for (TPair<FString, FClip*>& It : Clips)
		{
			if (It.Key == TEXT("Rest") || It.Key == TEXT("Walking")) continue;
			FClip& C = *It.Value;
			for (int32 f = 0; f < C.NumFrames; ++f) C.Rot[f * NB + HandB] = (C.Rot[f * NB + HandB] * Fit).GetNormalized();
		}
		// The open hand round a bottle (owner 2026-11-06). There are no finger bones, so it is a hand of its own (BuildOpenHand), and this is where it goes on the hand bone. Its frame: Z along the
		// bottle with the cork at -Z, X from the bottle's axis out through the palm, Y the way the fingers run; the wrist is at DrinkWrist. Read off the real hand's axes in the T-pose,
		// carried through Fit as every clip's are: fingers along the arm, palm down, thumb forward.
		{
			const FVector F = NewF;
			const FVector T = (FVector::ForwardVector - F * FVector::DotProduct(FVector::ForwardVector, F)).GetSafeNormal();
			const FVector Pa = FVector::CrossProduct(F, T);   // (palm down, and T = Pa x F)
			const FQuat ToHand = Fit.Inverse() * Hand.GetRotation().Inverse();
			FVector Wrist(9.5f, -8.5f, 0.f); FRotator HandTurn(0.f, 0.f, -20.f);   // (the wrist bent so the cork comes to his visor's mouth: the clip was made for a small cup)
			FString HS; TArray<FString> HP;   // (test pictures: -HandFit=X_Y_Z_Pitch_Yaw_Roll)
			if (!UE_BUILD_SHIPPING && FParse::Value(FCommandLine::Get(), TEXT("HandFit="), HS) && HS.ParseIntoArray(HP, TEXT("_")) == 6)
			{ Wrist = FVector(FCString::Atof(*HP[0]), FCString::Atof(*HP[1]), FCString::Atof(*HP[2])); HandTurn = FRotator(FCString::Atof(*HP[3]), FCString::Atof(*HP[4]), FCString::Atof(*HP[5])); }
			const FQuat HQ = FQuat(FMatrix(ToHand.RotateVector(-Pa), ToHand.RotateVector(F), ToHand.RotateVector(-T), FVector::ZeroVector)) * HandTurn.Quaternion();
			DrinkInHand = FTransform(HQ, -HQ.RotateVector(Wrist));
		}
	}
	// His mouth, in the head bone's frame: a little up and out to the front of the visor
	{
		const FTransform HeadAt = BindInGame(P, JeebAnim::Head);
		MouthInHead = HeadAt.InverseTransformPosition(HeadAt.GetLocation() + FVector(13.f, 0.f, 8.f));
	}
	// The left hand's joint, the same way, for a second blade (the katana's): the blade leaves the fist forward as the forearm lies at rest
	{
		const FTransform Hand = BindInGame(P, LHand), Fore = BindInGame(P, LFore);
		const FVector NewF = (Hand.GetLocation() - Fore.GetLocation()).GetSafeNormal();
		const FVector OldF = (KnightDesign::WristPos(-1) - KnightDesign::PivotPos(KnightDesign::P_ElbowL)).GetSafeNormal();
		const FQuat Q = FQuat::FindBetweenNormals(OldF, NewF);
		const FVector G = KnightDesign::GripPos(true) - KnightDesign::PivotPos(KnightDesign::P_ElbowL);
		LeftGrip = FTransform(Q, Hand.GetLocation() + NewF * 7.f - Q.RotateVector(G)) * Hand.Inverse();
	}
	// The shield: at rest on the outside of the forearm as the old rest pose had it; raised, facing straight ahead in front of the forearm at the guard pose
	{
		const FVector Strap(14.f, -41.f, -8.f);
		const FClip* RestC = Find(TEXT("Rest"));
		if (RestC) Sample(*RestC, 0.f, P, false);
		{
			const FTransform Fore = BindInGame(P, LFore), Hand = BindInGame(P, LHand);
			// (owner 2026-11-06: strapped to the outside of the forearm as a real shield is, its long axis along the bone and its back against the arm; it had come from the old knight's rest pose and hung off the back of the hand)
			float Share = 0.5f, Out = 8.f, TurnFwd = 20.f, Tilt = 0.f;
			FString S; TArray<FString> Pr;   // (test pictures: -ShieldRestTune=Share_Out_Turn_Tilt)
			if (!UE_BUILD_SHIPPING && FParse::Value(FCommandLine::Get(), TEXT("ShieldRestTune="), S) && S.ParseIntoArray(Pr, TEXT("_")) == 4)
			{ Share = FCString::Atof(*Pr[0]); Out = FCString::Atof(*Pr[1]); TurnFwd = FCString::Atof(*Pr[2]); Tilt = FCString::Atof(*Pr[3]); }
			const FVector Mid = FMath::Lerp(Fore.GetLocation(), Hand.GetLocation(), Share);
			const FVector Along = (Hand.GetLocation() - Fore.GetLocation()).GetSafeNormal();
			FVector Face = (FVector(0.f, -1.f, 0.f) - Along * FVector::DotProduct(FVector(0.f, -1.f, 0.f), Along)).GetSafeNormal();   // (straight out to his left)
			Face = FQuat(Along, -FMath::DegreesToRadians(TurnFwd)).RotateVector(Face);   // (and a little toward the front, as the arm hangs)
			const FVector Up = FQuat(Face, FMath::DegreesToRadians(Tilt)).RotateVector(-Along);
			const FQuat Q = FRotationMatrix::MakeFromXZ(Face, Up).ToQuat();
			ShieldAtRest = FTransform(Q, Mid + Face * Out - Q.RotateVector(Strap)) * Fore.Inverse();
		}
		// Held out (owner 2026-11-06, with a drawing over a picture): the elbow well out to his left, the forearm level and running forward and in across him, thumb up, and the shield flush along the
		// outside of the forearm, parallel to it and centred on it. These bones' turns are the pose every frame takes (ShieldArm); the same strap serves any clip with the forearm up.
		{
			if (RestC) Sample(*RestC, 0.f, P, false);
			auto ToMesh = [](const FVector& G) { return FVector(-G.Y, G.X, G.Z); };   // (game frame to mesh frame)
			FVector ArmGame(0.30f, -0.65f, -0.68f), ForeGame(0.75f, 0.66f, 0.06f);
			float OutFromFore = 8.f;
			FString S; TArray<FString> Pr;   // (test pictures: -ShieldArmTune=ArmX_ArmY_ForeX_ForeY_ForeZ_Out)
			if (!UE_BUILD_SHIPPING && FParse::Value(FCommandLine::Get(), TEXT("ShieldArmTune="), S) && S.ParseIntoArray(Pr, TEXT("_")) == 6)
			{ ArmGame = FVector(FCString::Atof(*Pr[0]), FCString::Atof(*Pr[1]), -0.68f); ForeGame = FVector(FCString::Atof(*Pr[2]), FCString::Atof(*Pr[3]), FCString::Atof(*Pr[4])); OutFromFore = FCString::Atof(*Pr[5]); }
			const FVector ArmDir = ToMesh(ArmGame.GetSafeNormal()), ForeDir = ToMesh(ForeGame.GetSafeNormal());
			TurnInMesh(P, LArm, FQuat::FindBetweenNormals((BonePos(P, LFore) - BonePos(P, LArm)).GetSafeNormal(), ArmDir));
			TurnInMesh(P, LFore, FQuat::FindBetweenNormals((BonePos(P, LHand) - BonePos(P, LFore)).GetSafeNormal(), ForeDir));
			// (thumb up: the way a blade would leave this fist stands as upright as a forearm this level allows)
			FQuat LR;
			BonePos(P, LHand, &LR);
			FVector Thumb = LR.RotateVector(LeftGrip.TransformVectorNoScale(KnightDesign::BladeDir(-1)));
			Thumb = (Thumb - ForeDir * FVector::DotProduct(Thumb, ForeDir)).GetSafeNormal();
			const FVector Upright = (FVector::UpVector - ForeDir * FVector::DotProduct(FVector::UpVector, ForeDir)).GetSafeNormal();
			TurnInMesh(P, LFore, FQuat(ForeDir, FMath::Atan2(float(FVector::DotProduct(FVector::CrossProduct(Thumb, Upright), ForeDir)), float(FVector::DotProduct(Thumb, Upright)))));
			const int32 Arm[4] = { LShoulder, LArm, LFore, LHand };
			for (int32 i = 0; i < 4; ++i) ShieldArm[i] = P.Rot[Bone[Arm[i]]];
			const FTransform Fore = BindInGame(P, LFore), Hand = BindInGame(P, LHand);
			const FVector Along = (Hand.GetLocation() - Fore.GetLocation()).GetSafeNormal();
			const FVector Face = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();   // (level, square to the forearm, on its outer side: forward and to his left)
			const FQuat Q = FRotationMatrix::MakeFromXZ(Face, FVector::UpVector).ToQuat();
			ShieldAtGuard = FTransform(Q, FMath::Lerp(Fore.GetLocation(), Hand.GetLocation(), 0.72f) + Face * OutFromFore - Q.RotateVector(Strap)) * Fore.Inverse();
		}
			// Held up to guard (the Block row of KnightMoves.csv): facing straight ahead, tilted back a touch, in the middle of his chest
			if (const FMove* M = Moves.Find(TEXT("Block")))
				if (const FClip* C = Find(*M->Clip))
				{
					Sample(*C, M->ClipStart, P, false);
					TurnInMesh(P, LFore, GuardForeTurn());
					const FTransform Fore = BindInGame(P, LFore), Hand = BindInGame(P, LHand);
					const FVector Mid = FMath::Lerp(Fore.GetLocation(), Hand.GetLocation(), 0.5f);
					const FVector Face = FRotator(6.f, 0.f, 0.f).Vector();
					const FQuat Q = FRotationMatrix::MakeFromXZ(Face, FVector::UpVector).ToQuat();
					FVector At = Mid + Face * 13.f;   // (the forearm lies across behind it, strapped on: GuardForeTurn)
					At.Y = 0.f;   // (owner 2026-11-05: square in front of him, not off to the left)
					ShieldAtBlock = FTransform(Q, At - Q.RotateVector(Strap)) * Fore.Inverse();
					ShieldInGuard = At;
				}
	}
	// The bow: at rest as the old rest pose had it, in the left fist; aiming upright with the string toward the drawing hand. X is along the limbs, Z toward the string.
	{
		const FClip* RestC = Find(TEXT("Rest"));
		if (RestC) Sample(*RestC, 0.f, P, false);
		{
			const FTransform Hand = BindInGame(P, LHand), Fore = BindInGame(P, LFore);
			const FVector Dir = (Hand.GetLocation() - Fore.GetLocation()).GetSafeNormal();
			BowAtRest = FTransform(KnightBowRestQuat(), Hand.GetLocation() + Dir * 7.f) * Hand.Inverse();
		}
		const FClip* AimC = Find(TEXT("AimIdle"));
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
		QuiverOnHips = FTransform(FRotator(-5.f, 0.f, -6.f), FVector(-22.f, 24.f, -29.f) + Old + FVector(0.f, 0.f, 96.f)) * Hip.Inverse();
	}
	// The flask on his belt: the other side from the quiver, its neck tucked behind the belt (the model's cork is along -Z)
	{
		const FTransform Hip = BindInGame(P, Hips);
		const FVector Old = KnightDesign::PivotPos(KnightDesign::P_Body);
		FVector At(-14.f, -14.f, 14.f);   // (owner 2026-11-06: same height and spot on the belt, but touching it; it hung 5 cm clear of his back)
		FString S; TArray<FString> Pr;   // (test pictures: -FlaskAt=X_Y_Z)
		if (!UE_BUILD_SHIPPING && FParse::Value(FCommandLine::Get(), TEXT("FlaskAt="), S) && S.ParseIntoArray(Pr, TEXT("_")) == 3) At = FVector(FCString::Atof(*Pr[0]), FCString::Atof(*Pr[1]), FCString::Atof(*Pr[2]));
		BeltFlaskOnHips = FTransform(FRotator(5.f, 0.f, 186.f).Quaternion(), At + Old + FVector(0.f, 0.f, 96.f), FVector(0.7f)) * Hip.Inverse();
	}
	// The crossbow in the right hand: as the rifle lies in the aiming clip, along the line from the right fist to the left
	if (const FClip* C = Find(TEXT("CrossAim")))
	{
		Sample(*C, 0.f, P, false);
		const FTransform RH = BindInGame(P, RHand), RF = BindInGame(P, RFore), LH = BindInGame(P, LHand), LF = BindInGame(P, LFore);
		const FVector At = RH.GetLocation() + (RH.GetLocation() - RF.GetLocation()).GetSafeNormal() * 7.f;
		const FVector Front = LH.GetLocation() + (LH.GetLocation() - LF.GetLocation()).GetSafeNormal() * 7.f;
		const FVector Line = (Front - At).GetSafeNormal();
		CrossInHand = FTransform(FRotationMatrix::MakeFromXZ(Line, FVector::UpVector).ToQuat(), At + Line * 12.f) * RH.Inverse();   // (12 cm ahead: the left fist stays behind the prod, the right on the stock's wrist)
		P.Rot = RefRot;
		P.Hip = RefPos[Bone[Hips]];
	}
	// The chest at rest: the frame the cape is pinned in, and gear goes on his back in
	if (const FClip* RestC = Find(TEXT("Rest"))) Sample(*RestC, 0.f, P, false);
	ChestRest = BindInGame(P, Spine2).GetRotation();
}

FKnightAnimator::FKnightAnimator()
	: L(FClipLib::Get()), Bone(L.Bone), Parents(L.Parents), RefPos(L.RefPos), RefRot(L.RefRot), Moves(L.Moves)
{
	for (int32 i = 0; i < Piece_Count; ++i) { PieceMat[i] = INDEX_NONE; PieceMid[i] = nullptr; }
	if (!L.bReady) return;
	const int32 N = Parents.Num();
	for (FPose* P : { &Scratch, &ScratchSum, &Base, &Over, &Final, &PrevFinal }) { P->Rot = RefRot; P->Hip = RefPos[Bone[Hips]]; }
	if (const FClip* C = Get(TEXT("Rest"))) { Sample(*C, 0.f, Final, false); PrevFinal = Final; }
	OffRot.Init(FVector::ZeroVector, N);
	OffRotV.Init(FVector::ZeroVector, N);
}

UPoseableMeshComponent* FKnightAnimator::MakeMesh(AActor* Owner, USceneComponent* Parent)
{
	if (!L.bReady || !Owner) return nullptr;
	USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, SuitMeshPath);
	if (!Asset) { UE_LOG(LogTemp, Error, TEXT("JEEBANIM no suit mesh")); return nullptr; }
	Comp = NewObject<UPoseableMeshComponent>(Owner);
	Comp->SetupAttachment(Parent);
	Comp->SetUsingAbsoluteLocation(true);
	Comp->SetUsingAbsoluteRotation(true);
	Comp->SetSkinnedAssetAndUpdate(Asset);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetBoundsScale(2.5f);   // (a roll or a kick reaches well past the standing bounds)
	Comp->RegisterComponent();
	// Each piece is a material slot of the suit (Tools/knight_suit_import.py); the dark cloth under the skirt is always there
	static const TCHAR* const SlotNames[Piece_Count] = { TEXT("Chest"), TEXT("Legs"), TEXT("Helmet"), TEXT("Gauntlets"), TEXT("Shoulders") };
	const TArray<FSkeletalMaterial>& Slots = Asset->GetMaterials();
	for (int32 m = 0; m < Slots.Num(); ++m)
		for (int32 i = 0; i < Piece_Count; ++i)
			if (Slots[m].MaterialSlotName.ToString().Contains(SlotNames[i])) PieceMat[i] = m;
	// His mail, always there under the plate (owner 2026-11-06: an arm seen from below or a helmet seen through its visor looked hollow). Tools/knight_under_export.py keeps it from ever showing through:
	// its bone weights are the nearest plate's, and its material moves it a few centimetres away from the camera as it draws.
	if (USkeletalMesh* Under = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NewSuit/SK_NewSuitUnder.SK_NewSuitUnder")))
	{
		UnderComp = NewObject<USkeletalMeshComponent>(Owner);
		UnderComp->SetupAttachment(Comp);
		UnderComp->SetSkinnedAssetAndUpdate(Under);
		UnderComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UnderComp->SetCastShadow(false);
		UnderComp->SetBoundsScale(2.5f);
		UnderComp->RegisterComponent();
		UnderComp->SetLeaderPoseComponent(Comp);
		if (!UE_BUILD_SHIPPING && FParse::Param(FCommandLine::Get(), TEXT("ShotUnderRed")))   // (test pictures: anything red coming through a plate is wrong)
			if (UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(UnderComp->GetMaterial(0), UnderComp))
			{
				M->SetVectorParameterValue(TEXT("Tint"), FLinearColor(3.f, 0.f, 0.f));
				UnderComp->SetMaterial(0, M);
			}
	}
	else UE_LOG(LogTemp, Warning, TEXT("JEEBANIM no under mesh"));
	return Comp;
}

void FKnightAnimator::Wear(int32 Piece, int32 Tier)
{
	if (!Comp || Piece < 0 || Piece >= Piece_Count || PieceMat[Piece] == INDEX_NONE) return;
	const int32 Mat = PieceMat[Piece];
	if (const FSkeletalMeshRenderData* R = Comp->GetSkinnedAsset() ? Comp->GetSkinnedAsset()->GetResourceForRendering() : nullptr)
		if (R->LODRenderData.Num() > 0)
		{
			const TArray<FSkelMeshRenderSection>& Secs = R->LODRenderData[0].RenderSections;
			for (int32 i = 0; i < Secs.Num(); ++i)
				if (Secs[i].MaterialIndex == Mat) Comp->ShowMaterialSection(Mat, i, Tier >= 0, 0);
		}
	WornTiers[Piece] = Tier;
	ShowBare(Piece, Tier < 0);
	if (Tier < 0) return;
	if (!PieceMid[Piece]) PieceMid[Piece] = Comp->CreateDynamicMaterialInstance(Mat, Comp->GetMaterial(Mat));
	if (UMaterialInstanceDynamic* M = PieceMid[Piece])
	{
		// The tier's colour and shine (Tiers.csv). Meshy painted this steel dark, so it is greyed and lifted before the tint. MetalShare: at Tiers.csv's full metal it mirrors the dark surroundings and looks nearly black.
		const FLinearColor Steel(0.90f, 0.97f, 1.0f), C = JeebItems::Palette(Tier, 0);
		M->SetVectorParameterValue(TEXT("Tint"), FLinearColor(FMath::Min(C.R / Steel.R, 2.f), FMath::Min(C.G / Steel.G, 2.f), FMath::Min(C.B / Steel.B, 2.f), 1.f));
		M->SetScalarParameterValue(TEXT("MetalScale"), JeebItems::TierMetal(Tier) * 0.75f);
		M->SetScalarParameterValue(TEXT("TierRough"), JeebItems::TierRough(Tier));
		M->SetScalarParameterValue(TEXT("RoughMix"), 0.85f);
		M->SetScalarParameterValue(TEXT("Desat"), 1.f);
		M->SetScalarParameterValue(TEXT("DesatGain"), 2.3f);
	}
}

// An empty slot shows the mail body underneath. It is a whole mesh of its own, since the suit has no mail under its plate (it never shows through); it is drawn only while some slot is empty.
// A join between two pieces (owner 2026-11-06: the shoulder with chest and gauntlets on, the neck with the helmet on and the chest off) belongs to both, so its mail shows when either of them is off:
// the plates that are still on cover their own part of it, and what is between them is filled.
void FKnightAnimator::ShowBare(int32 Piece, bool bShow)
{
	if (bBare[Piece] == bShow) return;
	bBare[Piece] = bShow;
	if (!BareComp)
	{
		if (!bShow) return;
		USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NewSuit/SK_NewSuitBare.SK_NewSuitBare"));
		if (!Asset) { UE_LOG(LogTemp, Warning, TEXT("JEEBANIM no bare mesh")); return; }
		BareComp = NewObject<USkeletalMeshComponent>(Comp->GetOwner());
		BareComp->SetupAttachment(Comp);
		BareComp->SetSkinnedAssetAndUpdate(Asset);
		BareComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BareComp->SetBoundsScale(2.5f);
		BareComp->RegisterComponent();
		BareComp->SetLeaderPoseComponent(Comp);
		// (Meshy painted the mail nearly black, like the plate: greyed and lifted the same way)
		if (UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BareComp->GetMaterial(0), BareComp))
		{
			M->SetScalarParameterValue(TEXT("Desat"), 1.f);
			M->SetScalarParameterValue(TEXT("DesatGain"), 2.3f);
			M->SetScalarParameterValue(TEXT("MetalScale"), 0.5f);
			for (int32 m = 0; m < BareComp->GetNumMaterials(); ++m) BareComp->SetMaterial(m, M);
		}
	}
	const FSkeletalMeshRenderData* R = BareComp->GetSkinnedAsset()->GetResourceForRendering();
	if (!R || R->LODRenderData.Num() == 0) return;
	const TArray<FSkelMeshRenderSection>& Secs = R->LODRenderData[0].RenderSections;
	const bool bChest = bBare[Piece_Chest], bHelm = bBare[Piece_Helmet], bArms = bBare[Piece_Arms], bSh = bBare[Piece_Shoulders];
	const struct { const TCHAR* Slot; bool bOn; } Regions[] = {
		{ TEXT("Bare_Helmet"), bHelm },
		{ TEXT("Bare_Neck"), bHelm || bChest },
		{ TEXT("Bare_Chest"), bChest },
		{ TEXT("Bare_Collar"), bChest || bSh },
		{ TEXT("Bare_UpperArm"), bArms || bSh || bChest },
		{ TEXT("Bare_Gauntlets"), bArms },
		{ TEXT("Bare_Legs"), bBare[Piece_Legs] },
		{ TEXT("Bare_Waist"), bChest || bBare[Piece_Legs] } };
	bool bAny = false;
	for (const auto& Rg : Regions)
	{
		bAny |= Rg.bOn;
		const int32 Mat = BareComp->GetMaterialIndex(FName(Rg.Slot));
		if (Mat == INDEX_NONE) { UE_LOG(LogTemp, Warning, TEXT("JEEBANIM bare slot %s missing"), Rg.Slot); continue; }   // (a section nobody hides stays visible: mail through the armour)
		for (int32 i = 0; i < Secs.Num(); ++i)
			if (Secs[i].MaterialIndex == Mat) BareComp->ShowMaterialSection(Mat, i, Rg.bOn, 0);
	}
	BareComp->SetVisibility(bAny);
}

void FKnightAnimator::MoveTo(const FVector& Feet, float ActorYaw)
{
	ToWorld = FTransform(FRotator(0.f, ActorYaw + YawNow, 0.f), Feet);
}

FTransform FKnightAnimator::BoneWorld(int32 B) const
{
	return L.BindInGame(Final, B) * ToWorld;
}

FTransform FKnightAnimator::ChestWorld() const
{
	const FTransform Chest = L.BindInGame(Final, Spine2);
	return FTransform(Chest.GetRotation() * L.ChestRest.Inverse(), Chest.GetLocation()) * ToWorld;
}

// The bottle's cork stays aimed at his mouth all through the drink (owner 2026-11-06), the hand turning about the wrist. The turn moves the bottle's middle a little, so it takes a couple of steps.
FTransform FKnightAnimator::DrinkWorld() const
{
	FTransform G = L.DrinkInHand * BoneWorld(JeebAnim::RHand);
	const FVector Wrist = BoneAt(JeebAnim::RHand), Mouth = MouthWorld();
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector C = G.GetLocation();
		const FVector Cork = -G.GetRotation().GetAxisZ();
		const FQuat Q = FQuat::FindBetweenNormals(Cork.GetSafeNormal(), (Mouth - C).GetSafeNormal());
		G.SetLocation(Wrist + Q.RotateVector(C - Wrist));
		G.SetRotation((Q * G.GetRotation()).GetNormalized());
	}
	return G;
}

FVector FKnightAnimator::ToolHeadWorld(int32 Kind) const { return L.ToolStrike(RightGripWorld(), Kind); }

FVector FClipLib::ToolContact(bool bChop, int32 Kind, float Share) const
{
	const FMove* M = Moves.Find(bChop ? TEXT("Chop") : TEXT("Mine"));
	const FClip* C = M ? Find(*M->Clip) : nullptr;
	if (!C) return FVector(100.f, 0.f, 100.f);
	FPose P;
	P.Rot = RefRot;
	Sample(*C, FMath::Lerp(M->ClipStart, M->ClipFollow, Share), P, false);
	const float Roll = ToolPose(P, 1.f, Kind);
	FTransform G = RightGrip * BindInGame(P, RHand);
	PlaceToolInFist(G, Roll, ToolSlide(Kind));
	return ToolStrike(G, Kind);
}

FTransform FKnightAnimator::ShieldWorld(float BlockAlpha) const
{
	// (at his side with the forearm hanging it hangs too, tip down; once the forearm comes up, as in every sword clip, it sits as strapped)
	const FVector ForeDir = (BonePos(Final, LHand) - BonePos(Final, LFore)).GetSafeNormal();
	const float Up = 1.f - FMath::SmoothStep(0.45f, 0.85f, float(-ForeDir.Z));
	FTransform Strap;
	Strap.Blend(L.ShieldAtRest, L.ShieldAtGuard, Up);
	if (BlockAlpha > 0.f) { const FTransform Open = Strap; Strap.Blend(Open, L.ShieldAtBlock, BlockAlpha); }
	return Strap * BoneWorld(LFore);
}

FTransform FKnightAnimator::BowWorld(float AimAlpha) const
{
	FTransform Hold;
	Hold.Blend(L.BowAtRest, L.BowAtAim, AimAlpha);
	return Hold * BoneWorld(LHand);
}

// ---------------- Hurt boxes: capsules on the bones, the size of the plates on them (owner, 2026-11-05: fat old ones did not match) ----------------
namespace
{
	constexpr float HelmFrom = 8.f, HelmTo = 18.f, HelmR = 13.f;     // along the head from the head bone; the helm's top is HelmTo + HelmR above it
	constexpr float TorsoR = 17.f, UpperArmR = 8.f, ForearmR = 7.f, ThighR = 10.f, ShinR = 8.f, FootR = 7.f;
}

void FKnightAnimator::GatherHurt(TArray<JeebHit::FCapsule>& Out) const
{
	using namespace JeebHit;
	uint8 Zone = Z_None;
	auto Add = [&](const FVector& A, const FVector& B, float R) { FCapsule C; C.A = A; C.B = B; C.R = R; C.Zone = Zone; Out.Add(C); };
	const FVector Hd = BoneAt(Head), Nk = BoneAt(Neck);
	const FVector HeadUp = (Hd - Nk).GetSafeNormal();
	Zone = Z_Head;
	Add(Hd + HeadUp * HelmFrom, Hd + HeadUp * HelmTo, HelmR);
	Zone = Z_Torso;
	Add(BoneAt(Hips) + FVector(0.f, 0.f, -2.f), BoneAt(Spine2) + (Nk - BoneAt(Spine2)) * 0.5f, TorsoR);
	const int32 Sides[2][7] = { { LArm, LFore, LHand, LUp, LLeg, LFoot, LToe }, { RArm, RFore, RHand, RUp, RLeg, RFoot, RToe } };
	for (const auto& S : Sides)
	{
		const FVector Sh = BoneAt(S[0]), El = BoneAt(S[1]), Wr = BoneAt(S[2]), Hp = BoneAt(S[3]), Kn = BoneAt(S[4]), An = BoneAt(S[5]), To = BoneAt(S[6]);
		Zone = Z_UpperArm;
		Add(Sh, El, UpperArmR);
		Zone = Z_Forearm;
		Add(El, Wr + (Wr - El).GetSafeNormal() * 9.f, ForearmR);   // (to the knuckles)
		Zone = Z_Leg;
		Add(Hp, Kn, ThighR);
		Add(Kn, An, ShinR);
		Add(An, To + (To - An).GetSafeNormal() * 6.f, FootR);
	}
}

float FKnightAnimator::HelmTopZ() const
{
	const FVector Hd = BoneAt(Head);
	return float(Hd.Z) + FMath::Max(0.f, float((Hd - BoneAt(Neck)).GetSafeNormal().Z)) * HelmTo + HelmR;
}

// His raised shield as hit boxes see it: the same band as ever, moved so that its middle lies where the raised shield really is (owner, 2026-11-05)
JeebHit::FShieldWedge FKnightAnimator::ShieldWedge(const FVector& Centre, const FVector& Forward) const
{
	JeebHit::FShieldWedge W = JeebHit::KnightShield(Centre, Forward, HelmTopZ());
	const float Mid = 0.5f * (W.Inner + W.Radius);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, W.Fwd);
	constexpr float SideShare = 0.5f;   // (half way over to the shield's side: the far edge of the band still covers his sword arm)
	W.Apex = Centre + W.Fwd * (L.ShieldInGuard.X - Mid) + Right * (L.ShieldInGuard.Y * SideShare);
	return W;
}
