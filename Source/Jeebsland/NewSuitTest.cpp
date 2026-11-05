#include "NewSuitTest.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Inventory.h"
#include "KnightModel.h"

namespace
{
	constexpr float MeshYaw = -90.f;          // the model looks along his +Y; the game's forward is +X
	constexpr float WalkClipSpeed = 142.f;    // measured on the clips: how fast each one's planted foot moves (cm/s), so it doesn't slide
	constexpr float RunClipSpeed = 459.f;
	constexpr float RunFrom = 260.f;          // the game's normal walking pace (500) is a run on these clips
	constexpr float WalkDrop = 3.9f;          // both clips carry his hips higher than the reference pose: this puts their planted foot back on the floor
	constexpr float RunDrop = 2.9f;
}

ANewSuitTest::ANewSuitTest()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

void ANewSuitTest::BeginPlay()
{
	Super::BeginPlay();
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NewSuit/SK_NewSuit.SK_NewSuit"));
	Walking = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/NewSuit/A_Walking.A_Walking"));
	Running = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/NewSuit/A_Running.A_Running"));
	Resting = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/NewSuit/A_Rest.A_Rest"));
	if (!Mesh) { Destroy(); return; }
	Body->SetSkeletalMeshAsset(Mesh);
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);

	// Painted as silver, the way worn armour is (KnightModel::PaintSet); the mail and the dark cloth under the skirt are never tinted
	const TArray<FSkeletalMaterial>& Slots = Mesh->GetMaterials();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FString Slot = Slots[i].MaterialSlotName.ToString();
		if (Slot.Contains(TEXT("Mail")) || Slot.Contains(TEXT("Under"))) continue;
		if (UMaterialInstanceDynamic* M = Body->CreateDynamicMaterialInstance(i, Body->GetMaterial(i)))
		{
			M->SetVectorParameterValue(TEXT("Tint"), KnightModel::TierTint(JeebItems::T_Silver));
			M->SetScalarParameterValue(TEXT("MetalScale"), 0.75f);   // (Tiers.csv says 1 for silver, which reflects the dark surroundings: the old silver suits look nearly black in the same light)
			M->SetScalarParameterValue(TEXT("TierRough"), JeebItems::TierRough(JeebItems::T_Silver));
			M->SetScalarParameterValue(TEXT("RoughMix"), 0.85f);
			M->SetScalarParameterValue(TEXT("Desat"), 1.f);         // Meshy painted this steel dark, so it is greyed and lifted before the tint
			M->SetScalarParameterValue(TEXT("DesatGain"), 2.3f);    // (tried in the game: lower still reads as iron)
			float V;   // (test pictures can try other numbers without a rebuild)
			if (FParse::Value(FCommandLine::Get(), TEXT("SuitMetal="), V)) M->SetScalarParameterValue(TEXT("MetalScale"), V);
			if (FParse::Value(FCommandLine::Get(), TEXT("SuitRough="), V)) M->SetScalarParameterValue(TEXT("TierRough"), V);
			if (FParse::Value(FCommandLine::Get(), TEXT("SuitMix="), V)) M->SetScalarParameterValue(TEXT("RoughMix"), V);
			if (FParse::Value(FCommandLine::Get(), TEXT("SuitDesat="), V)) M->SetScalarParameterValue(TEXT("Desat"), V);
			if (FParse::Value(FCommandLine::Get(), TEXT("SuitGain="), V)) M->SetScalarParameterValue(TEXT("DesatGain"), V);
		}
	}

	FString Want;
	if (FParse::Value(FCommandLine::Get(), TEXT("SuitShow="), Want))
		Forced = Want == TEXT("walk") ? Show_Walk : Want == TEXT("run") ? Show_Run : Show_Stand;
	if (Show == Show_Player)
	{
		bWorn = FParse::Param(FCommandLine::Get(), TEXT("NewSuitWorn"));
		Body->SetVisibility(bWorn);
	}
	Playing = -1;
	Play(Show == Show_Player ? Show_Stand : Show, 1.f);
}

void ANewSuitTest::Play(int32 Clip, float Rate)
{
	if (Clip != Playing)
	{
		Playing = Clip;
		UAnimSequence* Seq = Clip == Show_Walk ? Walking.Get() : Clip == Show_Run ? Running.Get() : Resting.Get();
		Body->PlayAnimation(Seq, true);   // (standing: his rest pose, hands almost at his hips; made in knight_suit_export.py)
	}
	if (Playing != Show_Stand) Body->SetPlayRate(Rate);
}

void ANewSuitTest::Tick(float Dt)
{
	Super::Tick(Dt);
	if (Show != Show_Player) return;   // (the row stays where it was put)
	APlayerController* PC = GetWorld()->GetFirstPlayerController();   // (only something to look at on this machine)
	ACharacter* Me = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	if (!Me) return;
	if (PC->WasInputKeyJustPressed(EKeys::N) && !PC->bShowMouseCursor)
	{
		bWorn = !bWorn;
		Body->SetVisibility(bWorn);
	}
	Me->SetActorHiddenInGame(bWorn);
	if (!bWorn) return;
	const float Speed = FVector2D(Me->GetVelocity().X, Me->GetVelocity().Y).Size();
	int32 Clip = Speed < 20.f ? Show_Stand : Speed < RunFrom ? Show_Walk : Show_Run;
	float Rate = Clip == Show_Walk ? FMath::Clamp(Speed / WalkClipSpeed, 0.4f, 2.f) : FMath::Clamp(Speed / RunClipSpeed, 0.5f, 2.f);
	if (Forced >= 0) { Clip = Forced; Rate = 1.f; }
	Play(Clip, Rate);
	const float Drop = Playing == Show_Walk ? WalkDrop : Playing == Show_Run ? RunDrop : 0.f;
	const float Half = Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	SetActorLocationAndRotation(Me->GetActorLocation() - FVector(0.f, 0.f, Half + Drop), FRotator(0.f, Me->GetActorRotation().Yaw + MeshYaw, 0.f));
}
