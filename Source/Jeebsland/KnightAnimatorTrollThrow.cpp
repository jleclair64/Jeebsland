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
	// The sack hangs from the left fist: the arm a little out from his side and forward so the sack swings clear of his thigh
	const FVector Now = BonePos(B, LHand);
	L.ReachTo(B, LArm, LFore, LHand, Now + FVector(14.f, 8.f, 6.f), FVector(1.f, -0.3f, -0.4f));
}

void FKnightAnimator::TrollThrow(const FInput& In, FPose& B, float Dt)
{
	const bool bOn = In.RockMode != 0;
	TrollThrowW = FMath::FInterpConstantTo(TrollThrowW, bOn ? 1.f : 0.f, Dt, bOn ? 9.f : 3.5f);
	if (In.bRockSack) TrollSackHand(In, B);
	if (TrollThrowW < 0.005f) return;
	const float W = FMath::SmoothStep(0.f, 1.f, TrollThrowW);
	const float Arm = float(RefPos[Bone[RFore]].Size() + RefPos[Bone[RHand]].Size());
	const float C = FMath::Clamp(In.RockU, 0.f, 1.f);
	FThrowKey K;
	FVector LeftWant;   // the sack arm, from the left shoulder in arm lengths
	float LeftW = 0.f;
	if (In.RockMode == 1)
	{
		// the wind-up: in over a fifth of a second, then deeper as the charge fills
		const float Wind = FMath::SmoothStep(0.f, 0.22f, In.RockT);
		K = { 0.f, FVector(-0.32f, -0.30f - 0.32f * C, 0.52f + 0.12f * C), (14.f + 22.f * C) * Wind, 5.f * Wind };
		K.Hand = FMath::Lerp(FVector(-0.25f, 0.15f, -0.2f), K.Hand, Wind);
		LeftWant = FVector(0.28f, 0.55f, 0.05f + 0.1f * C);
		LeftW = 0.75f * Wind;
	}
	else
	{
		// the throw: over the shoulder, out ahead (the rock leaves about here), then low across his body
		const float Big = 0.6f + 0.4f * C;
		const FThrowKey Keys[] = {
			{ 0.00f, FVector(-0.32f, -0.62f, 0.64f), 36.f * Big, 5.f },
			{ 0.07f, FVector(-0.26f, 0.05f, 0.82f), 12.f, -4.f },
			{ 0.14f, FVector(-0.12f, 0.82f, 0.42f), -20.f * Big, -14.f * Big },
			{ 0.30f, FVector(0.22f, 0.55f, -0.35f), -32.f * Big, -18.f * Big },
			{ 0.55f, FVector(0.18f, 0.40f, -0.45f), -26.f * Big, -12.f * Big } };
		const float T = In.RockT;
		int32 i = 0;
		while (i + 2 < UE_ARRAY_COUNT(Keys) && T > Keys[i + 1].T) ++i;
		const float S = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((T - Keys[i].T) / FMath::Max(Keys[i + 1].T - Keys[i].T, 0.01f), 0.f, 1.f));
		K = LerpKey(Keys[i], Keys[i + 1], S);
		LeftWant = FMath::Lerp(FVector(0.28f, 0.55f, 0.1f), FVector(0.4f, -0.2f, -0.55f), FMath::SmoothStep(0.f, 0.18f, T));   // (the sack arm swings back for balance)
		LeftW = 0.7f * (1.f - FMath::SmoothStep(0.3f, 0.55f, T));
	}
	// the chest first (the shoulders move with it), shared between the two chest bones
	const float Yaw = FMath::DegreesToRadians(K.Yaw * W), Lean = FMath::DegreesToRadians(K.Lean * W);
	TurnInMesh(B, Spine1, FQuat(FVector::ZAxisVector, Yaw * 0.4f) * FQuat(FVector::XAxisVector, Lean * 0.4f));
	TurnInMesh(B, Spine2, FQuat(FVector::ZAxisVector, Yaw * 0.6f) * FQuat(FVector::XAxisVector, Lean * 0.6f));
	// the throwing arm, the elbow out to his right and down
	const FVector ShR = BonePos(B, RArm), NowR = BonePos(B, RHand);
	L.ReachTo(B, RArm, RFore, RHand, FMath::Lerp(NowR, ShR + K.Hand * Arm, W), FVector(-1.f, -0.3f, -0.5f));
	// the sack arm
	if (LeftW > 0.005f)
	{
		const FVector ShL = BonePos(B, LArm), NowL = BonePos(B, LHand);
		L.ReachTo(B, LArm, LFore, LHand, FMath::Lerp(NowL, ShL + LeftWant * Arm, W * LeftW), FVector(1.f, -0.3f, -0.5f));
	}
}

// The fists, in the world (for the sack and the rock in hand): a little past the wrist joint, along the forearm
FTransform FKnightAnimator::TrollLeftFistWorld() const
{
	const FTransform H = BoneWorld(LHand);
	const FVector Along = (H.GetLocation() - BoneAt(LFore)).GetSafeNormal();
	return FTransform(H.GetRotation(), H.GetLocation() + Along * 0.3f * float(RefPos[Bone[LHand]].Size()));
}

FTransform FKnightAnimator::TrollRightFistWorld() const
{
	const FTransform H = BoneWorld(RHand);
	const FVector Along = (H.GetLocation() - BoneAt(RFore)).GetSafeNormal();
	return FTransform(H.GetRotation(), H.GetLocation() + Along * 0.3f * float(RefPos[Bone[RHand]].Size()));
}

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
