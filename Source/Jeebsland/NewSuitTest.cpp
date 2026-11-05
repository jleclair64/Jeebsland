#include "NewSuitTest.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Inventory.h"
#include "KnightAnimator.h"
#include "KnightCharacter.h"
#include "KnightModel.h"

namespace
{
	constexpr float MeshYaw = -90.f;          // the model looks along his +Y; the game's forward is +X
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

ANewSuitTest::~ANewSuitTest() {}

// Painted as silver, the way worn armour is (KnightModel::PaintSet); the mail and the dark cloth under the skirt are never tinted
void ANewSuitTest::Paint(USkinnedMeshComponent* C)
{
	const TArray<FSkeletalMaterial>& Slots = SuitMesh->GetMaterials();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FString Slot = Slots[i].MaterialSlotName.ToString();
		if (Slot.Contains(TEXT("Mail")) || Slot.Contains(TEXT("Under"))) continue;
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(i, C->GetMaterial(i)))
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
}

void ANewSuitTest::BeginPlay()
{
	Super::BeginPlay();
	SuitMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NewSuit/SK_NewSuit.SK_NewSuit"));
	if (!SuitMesh) { Destroy(); return; }
	if (Show == Show_Player)
	{
		// The player's: bones written by the animator. (Body stays, empty, as the actor's root.)
		Pose = NewObject<UPoseableMeshComponent>(this);
		Pose->SetupAttachment(Body);
		Pose->SetSkinnedAssetAndUpdate(SuitMesh);
		Pose->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Pose->SetBoundsScale(2.5f);   // (a roll or a kick reaches well past the standing bounds)
		Pose->RegisterComponent();
		Paint(Pose);
		Pose->SetVisibility(false);
		bWorn = false;
		return;   // (put on by the first Tick when -NewSuitWorn is given, or N)
	}
	Walking = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/NewSuit/A_Walking.A_Walking"));
	Running = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/NewSuit/A_Running.A_Running"));
	Resting = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/NewSuit/A_Rest.A_Rest"));
	Body->SetSkeletalMeshAsset(SuitMesh);
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Paint(Body);
	FString Want;
	if (FParse::Value(FCommandLine::Get(), TEXT("SuitShow="), Want))
		Forced = Want == TEXT("walk") ? Show_Walk : Want == TEXT("run") ? Show_Run : Show_Stand;
	Playing = -1;
	Play(Show, 1.f);
}

void ANewSuitTest::EndPlay(const EEndPlayReason::Type Why)
{
	Anim.Reset();
	Super::EndPlay(Why);
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

void ANewSuitTest::SetWorn(bool bOn, AKnightCharacter* Me)
{
	if (bOn == bWorn) return;
	if (bOn && !Anim && !bAnimFailed)
	{
		Anim = MakeShared<FKnightAnimator>();
		if (!Anim->Init(Pose, SuitMesh)) { Anim.Reset(); bAnimFailed = true; }
		else
		{
			FString S;
			if (FParse::Value(FCommandLine::Get(), TEXT("SuitClip="), S)) Anim->ForcedClip = S;
			float V;
			if (FParse::Value(FCommandLine::Get(), TEXT("SuitClipAt="), V)) Anim->ForcedPhase = V;
		}
	}
	if (bOn && !Anim) return;
	bWorn = bOn;
	Pose->SetVisibility(bOn);
	if (!bOn && Anim) Anim->ReleaseGear(Me);
}

// Test pictures: -SuitWalk=<direction>_<share>_<from>_<for>: walks that way (0 = where he faces, 90 = his right), at that share of
// his top speed, starting <from> seconds in and lasting <for>. -SuitSprint sprints.
void ANewSuitTest::TestInput(AKnightCharacter* Me, float Dt)
{
	static FString S;
	static bool bRead = false;
	if (!bRead) { FParse::Value(FCommandLine::Get(), TEXT("SuitWalk="), S); bRead = true; }
	if (S.IsEmpty()) return;
	TArray<FString> P;
	if (S.ParseIntoArray(P, TEXT("_")) != 4) return;
	TestT += Dt;
	const float From = FCString::Atof(*P[2]), For = FCString::Atof(*P[3]);
	if (TestT < From || TestT > From + For) { if (Me->bSprinting) Me->StopSprint(); return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("SuitSprint")) && !Me->bSprinting) Me->StartSprint();
	Me->AddMovementInput(FRotator(0.f, Me->GetActorRotation().Yaw + FCString::Atof(*P[0]), 0.f).Vector(), FCString::Atof(*P[1]));
}

// Test pictures: -SuitDo=roll<deg>, stun<hundredths>, knock, drop: done to him once the pictures have started
void ANewSuitTest::TestAct(AKnightCharacter* Me)
{
	if (bTestActed || !Me->bShotFace) return;
	bTestActed = true;
	FString S;
	if (!FParse::Value(FCommandLine::Get(), TEXT("SuitDo="), S)) return;
	if (S.StartsWith(TEXT("roll")))
	{
		// (StartRoll refuses during test pictures, against stray keys: this is its body)
		Me->RollDir = FRotator(0.f, Me->GetActorRotation().Yaw + FCString::Atof(*S.Mid(4)), 0.f).Vector();
		Me->RollT = 0.f;
		Me->bSavedOrient = Me->GetCharacterMovement()->bOrientRotationToMovement;
		Me->GetCharacterMovement()->bOrientRotationToMovement = false;
		Me->bRolling = true;
	}
	else if (S == TEXT("crouch")) Me->bCrouched = true;
	else if (S.StartsWith(TEXT("stun"))) Me->ApplyReact(FCString::Atoi(*S.Mid(4)) / 100.f, -Me->GetActorForwardVector() * 300.f, false);
	else if (S == TEXT("knock")) { Me->LaunchCharacter(-Me->GetActorForwardVector() * 300.f, true, false); Me->GetCharacterMovement()->Velocity = -Me->GetActorForwardVector() * 300.f; Me->MulticastKnock_Implementation(true, 0); }   // (pushed: the fall a kick gives)
	else if (S == TEXT("drop")) Me->MulticastKnock_Implementation(true, 1);
}

void ANewSuitTest::Tick(float Dt)
{
	Super::Tick(Dt);
	if (Show != Show_Player || !Pose) return;   // (the row stays where it was put)
	APlayerController* PC = GetWorld()->GetFirstPlayerController();   // (only something to look at on this machine)
	AKnightCharacter* Me = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
	if (!Me) return;
	if (GetNetMode() != NM_Standalone) return;   // (online the server's rules read the old joints)
	if (Me != Tied)
	{
		if (Me) AddTickPrerequisiteActor(Me);   // (after the knight has moved and the old pose has run)
		Tied = Me;
		if (FParse::Param(FCommandLine::Get(), TEXT("NewSuitWorn"))) SetWorn(true, Me);
	}
	if (PC->WasInputKeyJustPressed(EKeys::N) && !PC->bShowMouseCursor) SetWorn(!bWorn, Me);
	if (!bWorn || !Anim) return;
	if (Me->bShotFace && FParse::Param(FCommandLine::Get(), TEXT("SuitGallery"))) { Anim->TickGallery(Me); return; }
	TestInput(Me, Dt);
	TestAct(Me);
	const float Half = Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Anim->Update(Dt, Me);
	SetActorLocationAndRotation(Me->GetActorLocation() - FVector(0.f, 0.f, Half), FRotator(0.f, Me->GetActorRotation().Yaw + MeshYaw + Anim->BodyYaw(), 0.f));
	Anim->PlaceGear(Me);
}
