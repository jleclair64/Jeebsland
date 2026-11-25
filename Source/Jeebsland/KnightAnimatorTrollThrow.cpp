// The troll's throwing rocks and his own armour (2026-11-22, owner: "add a sack of rocks in his left hand and add a throwing animation";
// "create a troll helmet, chest plate, gauntlets, boots, and shoulder armour").
//
// The throw (TrollThrow) is laid over whatever the legs are doing, from replicated state only (In.RockMode, RockU, RockT), so every
// machine poses it alike: the wind-up draws the rock hand back and up behind his head, the chest turned away and the sack arm reaching
// ahead, deeper with more charge; the throw whips the hand over the shoulder and out ahead, the chest turning through and leaning in,
// and follows through low across his body, more for a bigger charge. Mesh space: +X his left, +Y ahead, +Z up.
//
// The armour (WearTroll): /Game/TrollKit/SK_TrollArmour on the troll's own skeleton (Tools/ImportTrollKit.ps1),
// following his mesh's bones. Two material slots per piece: "<Piece>_Metal" (the iron: the tier's colour and shine, as the knight's plate)
// and "<Piece>_Soft" (leather and fur: as painted).
#include "KnightAnimator.h"
#include "Inventory.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

using namespace JeebAnim;

static TAutoConsoleVariable<float> CVarTrollArmourTint(TEXT("jeeb.Troll.ArmourTint"), 0.7f,
	TEXT("How strongly the troll armour's iron takes its tier's colour (0 = its own dark iron at every tier, 1 = as the knight's plate)."), ECVF_Default);

// ---------------- The throw ----------------

namespace
{
	struct FThrowKey { float T; FVector Hand; float Yaw; float Lean; };   // hand: from the right shoulder in arm lengths; Yaw + turns the chest to his right; Lean + leans it back
	FThrowKey LerpKey(const FThrowKey& A, const FThrowKey& B, float S)
	{
		return { FMath::Lerp(A.T, B.T, S), FMath::Lerp(A.Hand, B.Hand, S), FMath::Lerp(A.Yaw, B.Yaw, S), FMath::Lerp(A.Lean, B.Lean, S) };
	}
}

void FKnightAnimator::TrollSackHand(const FInput& In, FPose& B)
{
	// (2026-11-24, owner: the bag "not in his hand, and too small".) The sack is bigger now and hangs from the hole of his left fist, which is
	// 40 cm past the wrist joint, so with the arm hanging it lay on the ground. He carries it: the upper arm by his side, the forearm bent
	// forward and a little out, the fist about hip high ahead of his left thigh (from the left shoulder, in arm lengths). Eased in and out.
	const float W = FMath::SmoothStep(0.f, 1.f, TrollSackW);
	if (W < 0.005f) return;
	const float Arm = float(RefPos[Bone[LFore]].Size() + RefPos[Bone[LHand]].Size());
	const FVector Sh = BonePos(B, LArm), Now = BonePos(B, LHand);
	// (first film: running, he leans his shoulders down and forward and the sack dragged along the floor: the wrist never comes lower than
	// 1.26 m over his feet, which keeps the sack's bottom about 35 cm off the ground)
	FVector Want = Sh + FVector(0.24f, 0.42f, -0.66f) * Arm;
	Want.Z = FMath::Max(Want.Z, FMath::Min(BonePos(B, LToe).Z, BonePos(B, RToe).Z) + 126.f);
	L.ReachTo(B, LArm, LFore, LHand, FMath::Lerp(Now, Want, W), FVector(1.f, -0.7f, -0.2f));
	// the fist hangs from the wrist, knuckles down, so the sack's neck comes up into it (left as it was, the hand stood fingers up over the sack)
	{
		FQuat HR;
		BonePos(B, LHand, &HR);
		const FVector Hole = HR.RotateVector(L.RestRot[Bone[LHand]].Inverse().RotateVector(FVector(-L.TrollFistR.X, L.TrollFistR.Y, L.TrollFistR.Z))).GetSafeNormal();
		static const FVector Down = FVector(0.12f, 0.38f, -0.92f).GetSafeNormal();
		if (!Hole.IsNearlyZero()) TurnInMesh(B, LHand, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Hole, Down), W));
	}
}

// (2026-11-24, owner: the hand "too close to his face when holding/aiming: further from the face and more horizontal, not straight up and
// down".) Where the wind-up holds the rock, for a charge C: the upper arm out to his right at shoulder height, the forearm lying back
// nearly level behind it, so the fist and the rock (40 cm on past the wrist) sit out to the right of and behind his head, clear of his
// face and of the camera's line. From the right shoulder, in arm lengths.
static FVector TrollWindHand(float C) { return FVector(-0.54f, -0.40f - 0.14f * C, 0.08f + 0.08f * C); }

void FKnightAnimator::TrollThrow(const FInput& In, FPose& B, float Dt)
{
	const bool bOn = In.RockMode != 0;
	TrollThrowW = FMath::FInterpConstantTo(TrollThrowW, bOn ? 1.f : 0.f, Dt, bOn ? 9.f : 3.5f);
	// (first film: when the throw ended its clock went back to 0 while the pose was still fading out, so the throw played a second time, faintly.
	// The phase, its time and its charge are kept from the last frame it was on: a finished throw fades out from its end, a wind-up let down
	// without throwing from where it was held.)
	if (bOn) { TrollThrowMode = In.RockMode; TrollThrowT = In.RockT; TrollThrowC = In.RockU; }
	else if (TrollThrowW <= 0.f) TrollThrowMode = 0;
	else if (TrollThrowMode == 2) TrollThrowT += Dt;
	const uint8 Mode = TrollThrowMode;
	const float RockT = TrollThrowT;
	TrollSackW = FMath::FInterpConstantTo(TrollSackW, In.bRockSack ? 1.f : 0.f, Dt, 5.f);
	TrollSackHand(In, B);
	if (TrollThrowW < 0.005f) return;
	const float W = FMath::SmoothStep(0.f, 1.f, TrollThrowW);
	const float Arm = float(RefPos[Bone[RFore]].Size() + RefPos[Bone[RHand]].Size());
	const float C = FMath::Clamp(TrollThrowC, 0.f, 1.f);
	FThrowKey K;
	float Side = 0.f;       // degrees the chest tips to his right (the throwing shoulder drops in the wind-up, comes up and over in the throw)
	FVector ElbowHint(-1.f, -0.25f, -0.15f);   // (the elbow out to his right, level with the shoulder)
	FVector LeftWant;   // the sack arm, from the left shoulder in arm lengths
	float LeftW = 0.f;
	if (Mode != 2)
	{
		// the wind-up: in over a fifth of a second, then deeper as the charge fills
		const float Wind = FMath::SmoothStep(0.f, 0.22f, RockT);
		K = { 0.f, TrollWindHand(C), (16.f + 22.f * C) * Wind, (3.f + 4.f * C) * Wind };
		K.Hand = FMath::Lerp(FVector(-0.25f, 0.15f, -0.2f), K.Hand, Wind);
		Side = (4.f + 5.f * C) * Wind;
		LeftWant = FVector(0.20f, 0.46f, -0.60f);   // (the sack swings a little ahead as he leans back: a counterweight, not held out stiff)
		LeftW = 0.4f * Wind;
	}
	else
	{
		// the throw: the elbow leads past his ear, the hand whips over and out ahead (the rock leaves about here), then low across his body
		const float Big = 0.6f + 0.4f * C;
		const FThrowKey Keys[] = {
			{ 0.00f, TrollWindHand(C), 38.f * Big, 7.f * Big },
			{ 0.07f, FVector(-0.42f, 0.02f, 0.66f), 10.f, -4.f },
			{ 0.14f, FVector(-0.14f, 0.84f, 0.40f), -22.f * Big, -15.f * Big },
			{ 0.30f, FVector(0.22f, 0.56f, -0.34f), -34.f * Big, -20.f * Big },
			{ 0.55f, FVector(0.16f, 0.42f, -0.42f), -24.f * Big, -11.f * Big } };
		const float T = RockT;
		int32 i = 0;
		while (i + 2 < UE_ARRAY_COUNT(Keys) && T > Keys[i + 1].T) ++i;
		const float S = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - Keys[i].T) / FMath::Max(Keys[i + 1].T - Keys[i].T, 0.01f), 0.f, 1.f));
		K = LerpKey(Keys[i], Keys[i + 1], S);
		// (the hand goes round the shoulder, not through it: the way from the shoulder is turned, its length eased)
		{
			const FVector A = Keys[i].Hand, Bk = Keys[i + 1].Hand;
			const FQuat Q = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(A.GetSafeNormal(), Bk.GetSafeNormal()), S);
			K.Hand = Q.RotateVector(A.GetSafeNormal()) * FMath::Lerp(float(A.Size()), float(Bk.Size()), S);
		}
		Side = FMath::Lerp(9.f * Big, -10.f * Big, FMath::SmoothStep(0.f, 0.16f, T)) * (1.f - FMath::SmoothStep(0.3f, 0.55f, T));
		ElbowHint = FMath::Lerp(FVector(-1.f, -0.25f, -0.15f), FVector(-0.8f, -0.3f, -0.6f), FMath::SmoothStep(0.05f, 0.2f, T));
		LeftWant = FMath::Lerp(FVector(0.20f, 0.46f, -0.60f), FVector(0.34f, 0.12f, -0.66f), FMath::SmoothStep(0.f, 0.18f, T));   // (the sack arm swings back for balance)
		LeftW = 0.5f * (1.f - FMath::SmoothStep(0.3f, 0.55f, T));
	}
	// the chest first (the shoulders move with it), shared along the spine so the whole trunk coils and comes through, not just the top;
	// the head turns back by most of it so he keeps looking where he throws
	const float Yaw = FMath::DegreesToRadians(K.Yaw * W), Lean = FMath::DegreesToRadians(K.Lean * W), Tip = FMath::DegreesToRadians(Side * W);
	static const float Share[3] = { 0.22f, 0.36f, 0.42f };
	const int32 SpineB[3] = { Spine, Spine1, Spine2 };
	for (int32 i = 0; i < 3; ++i)
		TurnInMesh(B, SpineB[i], FQuat(FVector::ZAxisVector, Yaw * Share[i]) * FQuat(FVector::XAxisVector, Lean * Share[i]) * FQuat(FVector::YAxisVector, -Tip * Share[i]));
	TurnInMesh(B, Neck, FQuat(FVector::ZAxisVector, -Yaw * 0.35f));
	TurnInMesh(B, Head, FQuat(FVector::ZAxisVector, -Yaw * 0.4f));
	// the throwing arm
	const FVector ShR = BonePos(B, RArm), NowR = BonePos(B, RHand);
	L.ReachTo(B, RArm, RFore, RHand, FMath::Lerp(NowR, ShR + K.Hand * Arm, W), ElbowHint);
	// the hand itself lies back along the forearm, nearly level, while he holds the wind-up (his hand hangs 30 degrees off his forearm at rest,
	// which stood the fist up on end beside his head); let go of in the first tenth of a second of the throw
	{
		const float Flat = W * (Mode != 2 ? FMath::SmoothStep(0.f, 0.22f, RockT) : 1.f - FMath::SmoothStep(0.f, 0.1f, RockT));
		if (Flat > 0.005f)
		{
			FQuat HR;
			const FVector Wrist = BonePos(B, RHand, &HR);
			const FVector Hole = HR.RotateVector(L.RestRot[Bone[RHand]].Inverse().RotateVector(L.TrollFistR)).GetSafeNormal();
			const FVector Fore = (Wrist - BonePos(B, RFore)).GetSafeNormal();
			const FVector Want = FVector(Fore.X, Fore.Y, Fore.Z * 0.35f).GetSafeNormal();
			if (!Hole.IsNearlyZero() && !Want.IsNearlyZero()) TurnInMesh(B, RHand, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Hole, Want), Flat));
		}
	}
	// the sack arm
	if (LeftW > 0.005f)
	{
		const FVector ShL = BonePos(B, LArm), NowL = BonePos(B, LHand);
		L.ReachTo(B, LArm, LFore, LHand, FMath::Lerp(NowL, ShL + LeftWant * Arm, W * LeftW), FVector(1.f, -0.7f, -0.2f));
	}
}

// The fists, in the world (for the sack and the rock in hand). (2026-11-24, owner: "the rock isn't in his hand when he winds back", the bag
// "not in his hand".) They used to sit a little past the wrist joint, but his hand runs 40 cm on from there: the rock rode on his wrist,
// which the wind-up holds over his shoulder. Now it is the hole of the curled fist itself, the same spot the club's grip lies in
// (FClipLib::TrollFistR; the left hand's is its mirror across X).
// (first film: placed from our own bones the sack still hung a hand's length off his drawn fingers. His drawn mesh fits our bind pose only to
// 13 cm, and that miss is carried out to the hands, so the hole is now read off his own mesh's hand bone as it is drawn: the offset is the
// same bind-pose vector, turned by as much as that bone has turned. AlongBlade moves it along the line a held haft leaves the fist, cm.)
static TAutoConsoleVariable<float> CVarTrollRockOut(TEXT("jeeb.Troll.RockOut"), 12.f,
	TEXT("How far the rock in the troll's throwing fist sits out of the fist's hole, toward the thumb side (cm), so it shows between his fingers."), ECVF_Default);

FTransform FKnightAnimator::TrollFistWorld(bool bLeft, float AlongBlade) const
{
	const int32 HandB = Bone[bLeft ? LHand : RHand];
	FVector Hole(bLeft ? -L.TrollFistR.X : L.TrollFistR.X, L.TrollFistR.Y, L.TrollFistR.Z);   // (mesh space, bind pose, from the wrist joint)
	if (AlongBlade != 0.f)
	{
		FVector Blade = L.RestRot[Bone[RHand]].RotateVector(L.BladeInHand).GetSafeNormal();
		if (bLeft) Blade.X = -Blade.X;
		Hole += Blade * AlongBlade;
	}
	if (TrollComp && !bTrollMirror)
		for (int32 m = 0; m < TrollFrom.Num(); ++m)
			if (TrollFrom[m] == HandB && TrollRestK.IsValidIndex(m))
			{
				const FTransform BoneW = TrollComp->GetBoneTransform(m);   // (world)
				const FQuat R = (BoneW.GetRotation() * TrollRestK[m].Inverse()).GetNormalized();   // (a bind-pose mesh-space vector, as it lies in the world now)
				return FTransform(R, BoneW.GetLocation() + R.RotateVector(Hole));
			}
	const FTransform H = BoneWorld(bLeft ? LHand : RHand);
	return FTransform(H.GetRotation(), H.TransformPosition(L.RestRot[HandB].Inverse().RotateVector(Hole)));
}

FTransform FKnightAnimator::TrollLeftFistWorld() const { return TrollFistWorld(true, 0.f); }
FTransform FKnightAnimator::TrollRightFistWorld() const { return TrollFistWorld(false, CVarTrollRockOut.GetValueOnAnyThread()); }

// ---------------- The troll's armour ----------------

void FKnightAnimator::WearTroll(int32 Piece, int32 Tier)
{
	if (!TrollComp || Piece < 0 || Piece >= Piece_Count) return;
	if (!TrollArmourComp)
	{
		if (Tier < 0) return;
		USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/TrollKit/SK_TrollArmour.SK_TrollArmour"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Asset) { UE_LOG(LogTemp, Warning, TEXT("JEEBTROLL no troll armour mesh (/Game/TrollKit/SK_TrollArmour)")); return; }
		TrollArmourComp = NewObject<USkeletalMeshComponent>(TrollComp->GetOwner());
		TrollArmourComp->SetupAttachment(TrollComp);
		TrollArmourComp->SetSkinnedAssetAndUpdate(Asset);
		TrollArmourComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TrollArmourComp->SetBoundsScale(2.5f);
		TrollArmourComp->SetLightingChannels(true, true, false);   // (as the knight's suit: the sun's glint, channel 1)
		TrollArmourComp->SetCastInsetShadow(TrollComp->bCastInsetShadow);
		TrollArmourComp->RegisterComponent();
		TrollArmourComp->SetLeaderPoseComponent(TrollComp);
		static const TCHAR* const Names[Piece_Count] = { TEXT("Chest"), TEXT("Legs"), TEXT("Helmet"), TEXT("Arms"), TEXT("Shoulders") };
		const TArray<FSkeletalMaterial>& Slots = Asset->GetMaterials();
		for (int32 m = 0; m < Slots.Num(); ++m)
			for (int32 i = 0; i < Piece_Count; ++i)
			{
				const FString N = Slots[m].MaterialSlotName.ToString();
				if (!N.StartsWith(Names[i])) continue;
				if (N.Contains(TEXT("Metal"))) TrollArmourMat[i] = m; else TrollSoftMat[i] = m;
			}
		for (int32 i = 0; i < Piece_Count; ++i)
		{
			if (TrollArmourMat[i] != INDEX_NONE) ShowDarkSection(TrollArmourComp, TrollArmourMat[i], false);
			if (TrollSoftMat[i] != INDEX_NONE) ShowDarkSection(TrollArmourComp, TrollSoftMat[i], false);
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBTROLL armour mesh: %d slots"), Slots.Num());
	}
	if (TrollArmourMat[Piece] != INDEX_NONE) ShowDarkSection(TrollArmourComp, TrollArmourMat[Piece], Tier >= 0);
	if (TrollSoftMat[Piece] != INDEX_NONE) ShowDarkSection(TrollArmourComp, TrollSoftMat[Piece], Tier >= 0);
	bool bAny = false;
	for (int32 i = 0; i < Piece_Count; ++i) bAny |= WornTiers[i] >= 0 && WornStyles[i] == JeebItems::AS_Troll;
	TrollArmourComp->SetVisibility(bAny);
	if (Tier < 0 || TrollArmourMat[Piece] == INDEX_NONE) return;
	if (!TrollArmourMid[Piece]) TrollArmourMid[Piece] = TrollArmourComp->CreateDynamicMaterialInstance(TrollArmourMat[Piece], TrollArmourComp->GetMaterial(TrollArmourMat[Piece]));
	if (UMaterialInstanceDynamic* M = TrollArmourMid[Piece])
	{
		// The iron takes the tier's colour, as the knight's plate does, but only part way (its painted dark iron stays readable)
		const FLinearColor Steel(0.90f, 0.97f, 1.0f), Col = JeebItems::Palette(Tier, 0);
		const float S = FMath::Clamp(CVarTrollArmourTint.GetValueOnGameThread(), 0.f, 1.f);
		M->SetVectorParameterValue(TEXT("Tint"), FLinearColor(FMath::Lerp(1.f, FMath::Min(Col.R / Steel.R, 2.f), S), FMath::Lerp(1.f, FMath::Min(Col.G / Steel.G, 2.f), S), FMath::Lerp(1.f, FMath::Min(Col.B / Steel.B, 2.f), S), 1.f));
		M->SetScalarParameterValue(TEXT("Desat"), S);
		M->SetScalarParameterValue(TEXT("TierRough"), JeebItems::TierRough(Tier));
		M->SetScalarParameterValue(TEXT("MetalScale"), JeebItems::TierIsMetal(Tier) ? 0.9f : 0.6f);
	}
}
