// The playable wizard's body (2026-11-21, owner: "the third playable character, the wizard ... the exact same dimensions as the knight").
//
// He IS the knight's skeleton: Tools/wizard_export.py lays his Meshy bodies on the knight's own 24 bones, so the knight's animator, clips,
// hit boxes and grips run unchanged (no second rig, unlike the troll). Comp, the knight's mesh, still carries the pose with every section
// hidden; WizComp (/Game/Wizard/SK_Wizard, on the suit's skeleton) follows it bone for bone.
//
// What shows is chosen by material slot, and a worn piece REPLACES what is under it, so nothing can come through:
//   robe (the chest slot)   Wiz_RobeUpper in place of Wiz_BaseUpper (each is the whole man above the boot line, head and beard included)
//   boots                   Wiz_RobeFeet in place of Wiz_BaseFeet
//   hat (the helmet slot)   Wiz_HatBase or Wiz_HatRobe (fitted to each head)
//   spell book              Wiz_BookBase or Wiz_BookRobe (on his belt behind the left hip, fitted to each outfit)
// With nothing worn he is the "naked" wizard: shirt, waistcoat, plaid breeches, ankle boots.
#include "KnightAnimator.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "GameFramework/Actor.h"

using namespace JeebAnim;

void FKnightAnimator::MakeWizardMesh(AActor* Owner)
{
	USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Wizard/SK_Wizard.SK_Wizard"));
	if (!Asset || !Comp) { UE_LOG(LogTemp, Error, TEXT("JEEBWIZARD no wizard mesh (/Game/Wizard/SK_Wizard: run Tools/ImportWizard.ps1)")); return; }
	// the knight's own plates: all hidden (the mesh stays, carrying the pose for whatever reads it)
	if (const FSkeletalMeshRenderData* R = Comp->GetSkinnedAsset() ? Comp->GetSkinnedAsset()->GetResourceForRendering() : nullptr)
		if (R->LODRenderData.Num() > 0)
		{
			const TArray<FSkelMeshRenderSection>& Secs = R->LODRenderData[0].RenderSections;
			for (int32 i = 0; i < Secs.Num(); ++i) Comp->ShowMaterialSection(Secs[i].MaterialIndex, i, false, 0);
		}
	Comp->SetCastShadow(false);
	WizComp = NewObject<USkeletalMeshComponent>(Owner);
	WizComp->SetupAttachment(Comp);
	WizComp->SetSkinnedAssetAndUpdate(Asset);
	WizComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WizComp->SetBoundsScale(2.5f);
	WizComp->SetLightingChannels(true, true, false);
	WizComp->SetCastInsetShadow(true);   // (his own fine shadow, as the knight's and the troll's: no soft-shadow stipple on cloth)
	WizComp->RegisterComponent();
	WizComp->SetLeaderPoseComponent(Comp);
	UE_LOG(LogTemp, Log, TEXT("JEEBWIZARD mesh: %d slots, %d bones"), Asset->GetMaterials().Num(), Asset->GetRefSkeleton().GetNum());
	RefreshWizard();
}

void FKnightAnimator::RefreshWizard()
{
	if (!WizComp) return;
	const bool bRobe = bWizWorn[Piece_Chest], bBoots = bWizWorn[Piece_Legs], bHat = bWizWorn[Piece_Helmet];
	const struct { const TCHAR* Slot; bool bOn; } Slots[] = {
		{ TEXT("Wiz_BaseUpper"), !bRobe }, { TEXT("Wiz_RobeUpper"), bRobe },
		{ TEXT("Wiz_BaseFeet"), !bBoots }, { TEXT("Wiz_RobeFeet"), bBoots },
		{ TEXT("Wiz_HatBase"), bHat && !bRobe }, { TEXT("Wiz_HatRobe"), bHat && bRobe },
		{ TEXT("Wiz_BookBase"), bWizBook && !bRobe }, { TEXT("Wiz_BookRobe"), bWizBook && bRobe } };
	for (const auto& S : Slots)
	{
		const int32 Mat = WizComp->GetMaterialIndex(FName(S.Slot));
		if (Mat == INDEX_NONE) { UE_LOG(LogTemp, Warning, TEXT("JEEBWIZARD slot %s missing"), S.Slot); continue; }
		ShowDarkSection(WizComp, Mat, S.bOn);
	}
}
