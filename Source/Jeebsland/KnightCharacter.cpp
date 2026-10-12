#include "KnightCharacter.h"
#include "KnightShieldRest.h"
#include "JeebSave.h"
#include "Net/UnrealNetwork.h"
#include "Messages.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "EnhancedInputComponent.h"
#include "SkillComponent.h"
#include "ResourceNode.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Terrain.h"
#include "GrassField.h"
#include "Canopy.h"
#include "Pig.h"
#include "Inventory.h"
#include "Npc.h"
#include "Horse.h"
#include "Cottage.h"
#include "Loot.h"
#include "Pond.h"
#include "HealthComponent.h"
#include "Projectile.h"
#include "Combat.h"
#include "WeaponDesign.h"
#include "ProceduralMeshComponent.h"
#include "Misc/ConfigCacheIni.h"

AKnightCharacter::AKnightCharacter()
{
	// Hide the (empty) skeletal mesh slot; the knight is made of shapes instead
	GetMesh()->SetVisibility(false);

	PrimaryActorTick.bCanEverTick = true;

	Skills = CreateDefaultSubobject<USkillComponent>(TEXT("Skills"));

	// Movement feel
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 480.f, 0.f);
	GetCharacterMovement()->JumpZVelocity *= 0.894f; // about 20% lower jump (height grows with the square of launch speed)

	// Camera: a bit further back and higher, with gentle lag for a smooth follow
	if (USpringArmComponent* Boom = GetCameraBoom())
	{
		Boom->TargetArmLength = 540.f;   // 20% further back than before
		Boom->SocketOffset = FVector(0.f, 0.f, 60.f);
		Boom->bEnableCameraLag = true;
		Boom->CameraLagSpeed = 8.f;
	}

	// Dust puffs live in world space, so they stay where they were kicked up
	{
		ConstructorHelpers::FObjectFinder<UStaticMesh> DustMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		DustISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Dust"));
		DustISM->SetupAttachment(RootComponent);
		DustISM->SetStaticMesh(DustMesh.Object);
		DustISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DustISM->SetCastShadow(false);
		DustISM->SetUsingAbsoluteLocation(true);
		DustISM->SetUsingAbsoluteRotation(true);
		DustISM->SetUsingAbsoluteScale(true);
		// little clods of dirt and blades of grass kicked up by the feet (cubes, squashed to shape)
		ConstructorHelpers::FObjectFinder<UStaticMesh> BitMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
		for (UInstancedStaticMeshComponent** Slot : { &ClodISM, &BladeISM, &LeafAISM, &LeafBISM })
		{
			UInstancedStaticMeshComponent* I = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Slot == &ClodISM ? TEXT("Clods") : Slot == &BladeISM ? TEXT("Blades") : Slot == &LeafAISM ? TEXT("LeavesA") : TEXT("LeavesB"));
			I->SetupAttachment(RootComponent);
			I->SetStaticMesh(BitMesh.Object);
			I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			I->SetCastShadow(false);
			I->SetUsingAbsoluteLocation(true);
			I->SetUsingAbsoluteRotation(true);
			I->SetUsingAbsoluteScale(true);
			*Slot = I;
		}
	}

	// Every body part hangs off a pivot so it can be animated. Positions below are given relative to the
	// capsule center (feet at Z = -96, forward = +X, right = +Y); the helpers convert them to pivot space.
	TMap<USceneComponent*, FVector> PivotPos;

	auto MakePivot = [&](const TCHAR* Name, USceneComponent* Parent, FVector CapsulePos) -> USceneComponent*
	{
		USceneComponent* P = CreateDefaultSubobject<USceneComponent>(Name);
		P->SetupAttachment(Parent ? Parent : RootComponent.Get());
		P->SetRelativeLocation(CapsulePos - (Parent ? PivotPos.FindChecked(Parent) : FVector::ZeroVector));
		PivotPos.Add(P, CapsulePos);
		return P;
	};

	// ---- Pivots (the skeleton) ----
	BodyRoot   = MakePivot(TEXT("BodyRoot"),   nullptr,   FVector(0, 0, 0));       // bounces up and down
	HipL       = MakePivot(TEXT("HipL"),       BodyRoot,  KnightDesign::PivotPos(KnightDesign::P_HipL));
	HipR       = MakePivot(TEXT("HipR"),       BodyRoot,  KnightDesign::PivotPos(KnightDesign::P_HipR));
	KneeL      = MakePivot(TEXT("KneeL"),      HipL,      KnightDesign::PivotPos(KnightDesign::P_KneeL));
	KneeR      = MakePivot(TEXT("KneeR"),      HipR,      KnightDesign::PivotPos(KnightDesign::P_KneeR));
	AnkleL     = MakePivot(TEXT("AnkleL"),     KneeL,     KnightDesign::PivotPos(KnightDesign::P_AnkleL));
	AnkleR     = MakePivot(TEXT("AnkleR"),     KneeR,     KnightDesign::PivotPos(KnightDesign::P_AnkleR));
	UpperPivot = MakePivot(TEXT("UpperPivot"), BodyRoot,  FVector(0, 0, -41.f));     // chest: leans and twists
	HeadPivot  = MakePivot(TEXT("HeadPivot"),  UpperPivot, FVector(0, 0, 23.f));
	PlumePivot = MakePivot(TEXT("PlumePivot"), HeadPivot, FVector(-2, 0, 71.f));
	// (the helm is modelled at its full size: no head scaling)
	CapeUpper  = MakePivot(TEXT("CapeUpper"),  UpperPivot, FVector(-26, 0, 13.f));
	CapeLower  = MakePivot(TEXT("CapeLower"),  CapeUpper, FVector(-26, 0, -19.f));
	ArmPivot   = MakePivot(TEXT("ArmPivotR"),  UpperPivot, KnightDesign::PivotPos(KnightDesign::P_ArmR));     // right shoulder (sword arm)
	ArmPivotL  = MakePivot(TEXT("ArmPivotL"),  UpperPivot, KnightDesign::PivotPos(KnightDesign::P_ArmL));    // left shoulder (shield arm)
	ElbowR     = MakePivot(TEXT("ElbowR"),     ArmPivot,  KnightDesign::PivotPos(KnightDesign::P_ElbowR));
	ElbowL     = MakePivot(TEXT("ElbowL"),     ArmPivotL, KnightDesign::PivotPos(KnightDesign::P_ElbowL));
}

void AKnightCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Tick after the movement has moved us this frame: the cape (which lives in world space) is pinned to where the body
	// really is, not where it was a frame ago. Otherwise in a fast jump it lags one frame behind on some frames and not others.
	if (GetCharacterMovement()) AddTickPrerequisiteComponent(GetCharacterMovement());
	PrevYaw = GetActorRotation().Yaw;
	SpawnLoc = GetActorLocation();
	// Pick up where you left off; with no save yet, start fresh with the temporary practice pack. The save file on this PC is
	// for single player only: in an online game the server will own every character (not built yet), so they start fresh.
	if (Skills && !(UsesLocalSave() && UJeebSave::Restore(Skills)))
	{
		Skills->FillPracticePack();   // TEMPORARY: a pack full of random gear for practising swaps (remove this line when done)
		Skills->AddInvItem(JeebItems::GearId(JeebItems::K_Crest, 0), 1);   // the Red Helm Brush, ready to wear
	}
	if (Skills) { MaxHealth = 100 + JeebItems::CombinedMods(Skills).MaxHealthBonus; Health = (float)MaxHealth; }
	if (UsesLocalSave()) GetWorldTimerManager().SetTimer(AutosaveTimer, this, &AKnightCharacter::Autosave, 60.f, true);

	// Saved choices (side panel). Graphics always start low: fancy lighting off, 60% resolution.
	if (GConfig)
	{
		
		// (resolution is not loaded either: the game always starts on low graphics, and you raise them in Settings)
		GConfig->GetBool(TEXT("Jeebsland"), TEXT("PanelAlways"), bPanelAlways, GGameUserSettingsIni);
	}
	GraphicsLevel = 1;        // graphics start on Low every time
	RenderScale = 0.6f;       // and a low render resolution
	ApplyGraphics();
	UpperBase = UpperPivot->GetRelativeLocation();
	LeftBase = ArmPivotL->GetRelativeLocation();

	Dust.SetNum(16);
	{
		TArray<FTransform> Hidden;
		for (int32 i = 0; i < Dust.Num(); ++i) Hidden.Add(FTransform(FQuat::Identity, FVector(0, 0, -9000.f), FVector(0.001f)));
		DustISM->AddInstances(Hidden, false, false, false);
		UMaterialInterface* DBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
		if (UMaterialInstanceDynamic* DM = DustISM->CreateDynamicMaterialInstance(0, DBase))
		{
			DM->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.50f, 0.40f, 0.26f));
		}
		Clods.SetNum(48); Blades.SetNum(30); LeavesA.SetNum(24); LeavesB.SetNum(24);
		auto Prep = [&](UInstancedStaticMeshComponent* I, int32 N, const FLinearColor& C)
		{
			TArray<FTransform> H;
			for (int32 i = 0; i < N; ++i) H.Add(FTransform(FQuat::Identity, FVector(0, 0, -9000.f), FVector(0.001f)));
			I->AddInstances(H, false, false, false);
			if (UMaterialInstanceDynamic* M = I->CreateDynamicMaterialInstance(0, DBase)) M->SetVectorParameterValue(TEXT("Color"), C);
		};
		Prep(ClodISM, Clods.Num(), FLinearColor(0.27f, 0.17f, 0.09f));    // the path's soil
		Prep(BladeISM, Blades.Num(), FLinearColor(0.34f, 0.62f, 0.14f));  // fresh grass green (bright enough to show against the field)
		Prep(LeafAISM, LeavesA.Num(), FLinearColor(0.95f, 0.42f, 0.06f));   // fallen leaves: orange
		Prep(LeafBISM, LeavesB.Num(), FLinearColor(0.62f, 0.12f, 0.04f));   // and deep red
	}

	BuildKnight();

	// The cape is cloth: its points live in world space, so the mesh component sits at the world origin
	CapeCloth = NewObject<UProceduralMeshComponent>(this);
	CapeCloth->SetupAttachment(RootComponent);
	CapeCloth->SetUsingAbsoluteLocation(true);
	CapeCloth->SetUsingAbsoluteRotation(true);
	CapeCloth->SetUsingAbsoluteScale(true);
	CapeCloth->SetWorldLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	CapeCloth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CapeCloth->bCastContactShadow = false;           // the cloth's own small shadows flickered across it as it moved
	CapeCloth->bAffectDistanceFieldLighting = false;
	CapeCloth->RegisterComponent();
	{
		UMaterialInterface* ClothMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Cloth.M_Cloth"));
		if (!ClothMat) ClothMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
		CapeMid = UMaterialInstanceDynamic::Create(ClothMat, this);
	}

	RefreshEquipment();
	// Online, our own pack and gear arrive from the server: redraw the knight whenever they change
	if (Skills) Skills->OnGearChanged.AddUObject(this, &AKnightCharacter::RefreshEquipment);
}

void AKnightCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	Autosave();
	Super::EndPlay(Reason);
}

void AKnightCharacter::Autosave()
{
	if (Skills && UsesLocalSave()) UJeebSave::Save(Skills);
}

bool AKnightCharacter::UsesLocalSave() const
{
	return GetNetMode() == NM_Standalone;
}

void AKnightCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AKnightCharacter, RepLook, COND_SkipOwner);   // the owner already wears it
	DOREPLIFETIME_CONDITION(AKnightCharacter, RepMoveFlags, COND_SkipOwner);
	DOREPLIFETIME(AKnightCharacter, Health);
	DOREPLIFETIME(AKnightCharacter, MaxHealth);
	DOREPLIFETIME_CONDITION(AKnightCharacter, GuardPool, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AKnightCharacter, GuardMax, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AKnightCharacter, bGuardBroken, COND_OwnerOnly);
}

void AKnightCharacter::SendLook()
{
	if (!Skills || !HasAuthority() || GetNetMode() == NM_Standalone) return;
	TArray<int32> Look;
	Look.Reserve(USkillComponent::NumWornKinds + 2);
	for (int32 i = 0; i < USkillComponent::NumWornKinds; ++i) Look.Add(Skills->Worn[i]);
	Look.Add(Skills->RingB);
	Look.Add(Skills->SelectedItem());
	if (Look != RepLook) RepLook = Look;
}

void AKnightCharacter::OnRep_Look()
{
	if (!IsLocallyControlled()) ApplyLook(RepLook);
}

void AKnightCharacter::ApplyLook(const TArray<int32>& Look)
{
	if (!Skills || Look.Num() != USkillComponent::NumWornKinds + 2) return;
	for (int32 i = 0; i < USkillComponent::NumWornKinds; ++i) Skills->Worn[i] = Look[i];
	Skills->RingB = Look[USkillComponent::NumWornKinds];
	Skills->Selected = 0;
	Skills->Hotbar[0] = Look[USkillComponent::NumWornKinds + 1];
	// The look can arrive before the body is built; BeginPlay dresses it then
	if (HasActorBegunPlay()) RefreshEquipment();
}

void AKnightCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Build the input actions and key bindings in code (WASD, mouse look, space to jump)
	JumpAction = NewObject<UInputAction>(this, TEXT("KnightJump"));
	JumpAction->ValueType = EInputActionValueType::Boolean;

	MoveAction = NewObject<UInputAction>(this, TEXT("KnightMove"));
	MoveAction->ValueType = EInputActionValueType::Axis2D;

	LookAction = NewObject<UInputAction>(this, TEXT("KnightLook"));
	LookAction->ValueType = EInputActionValueType::Axis2D;

	MouseLookAction = NewObject<UInputAction>(this, TEXT("KnightMouseLook"));
	MouseLookAction->ValueType = EInputActionValueType::Axis2D;

	KnightContext = NewObject<UInputMappingContext>(this, TEXT("KnightContext"));

	auto MakeSwizzle = [this]()
	{
		UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(this);
		S->Order = EInputAxisSwizzle::YXZ;
		return S;
	};
	auto MakeNegate = [this]()
	{
		return NewObject<UInputModifierNegate>(this);
	};

	SprintAction = NewObject<UInputAction>(this, TEXT("KnightSprint"));
	SprintAction->ValueType = EInputActionValueType::Boolean;

	KnightContext->MapKey(JumpAction, EKeys::SpaceBar);
	KnightContext->MapKey(SprintAction, EKeys::LeftShift);

	InteractAction = NewObject<UInputAction>(this, TEXT("KnightInteract"));
	InteractAction->ValueType = EInputActionValueType::Boolean;
	KnightContext->MapKey(InteractAction, EKeys::LeftMouseButton);

	auto MakeBool = [this](const TCHAR* Name)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = EInputActionValueType::Boolean;
		return A;
	};
	CrouchAction = MakeBool(TEXT("KnightCrouch"));
	BlockAction  = MakeBool(TEXT("KnightBlock"));
	SlideAction  = MakeBool(TEXT("KnightSlide"));
	RollAction   = MakeBool(TEXT("KnightRoll"));
	SitAction    = MakeBool(TEXT("KnightSit"));
	KnightContext->MapKey(CrouchAction, EKeys::LeftControl);
	KnightContext->MapKey(BlockAction, EKeys::RightMouseButton);
	KnightContext->MapKey(SlideAction, EKeys::C);
	KnightContext->MapKey(RollAction, EKeys::V);
	KnightContext->MapKey(RollAction, EKeys::LeftAlt);
	DodgeLAction = MakeBool(TEXT("KnightDodgeL"));
	KnightContext->MapKey(DodgeLAction, EKeys::A);
	DodgeRAction = MakeBool(TEXT("KnightDodgeR"));
	KnightContext->MapKey(DodgeRAction, EKeys::D);
	DodgeFAction = MakeBool(TEXT("KnightDodgeF"));
	KnightContext->MapKey(DodgeFAction, EKeys::W);
	DodgeBAction = MakeBool(TEXT("KnightDodgeB"));
	KnightContext->MapKey(DodgeBAction, EKeys::S);
	PotionAction = MakeBool(TEXT("KnightPotion"));
	KnightContext->MapKey(PotionAction, EKeys::Q);
	KnightContext->MapKey(SitAction, EKeys::E);

	{
		static const FKey HotbarKeys[5] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five };
		for (int32 i = 0; i < 5; ++i)
		{
			HotbarActions[i] = MakeBool(*FString::Printf(TEXT("KnightHotbar%d"), i + 1));
			KnightContext->MapKey(HotbarActions[i], HotbarKeys[i]);
		}
	}
		ScrollUpAction = MakeBool(TEXT("KnightScrollUp"));
	KnightContext->MapKey(ScrollUpAction, EKeys::MouseScrollUp);
	ScrollDownAction = MakeBool(TEXT("KnightScrollDown"));
	KnightContext->MapKey(ScrollDownAction, EKeys::MouseScrollDown);
	InventoryAction = MakeBool(TEXT("KnightInventory"));
	KnightContext->MapKey(InventoryAction, EKeys::Tab);
	PanelTabAction = MakeBool(TEXT("KnightPanelTab"));
	KnightContext->MapKey(PanelTabAction, EKeys::CapsLock);
	SkillsAction = MakeBool(TEXT("KnightSkills"));
	KnightContext->MapKey(SkillsAction, EKeys::K);
	CallHorseAction = MakeBool(TEXT("KnightCallHorse"));
	KnightContext->MapKey(CallHorseAction, EKeys::T);

	SettingsAction = NewObject<UInputAction>(this, TEXT("KnightSettings"));
	SettingsAction->ValueType = EInputActionValueType::Boolean;
	KnightContext->MapKey(SettingsAction, EKeys::O);
	KnightContext->MapKey(SettingsAction, EKeys::Escape);   // (in the editor, Escape also stops Play: use O there)

	KnightContext->MapKey(MoveAction, EKeys::D); // right = +X
	KnightContext->MapKey(MoveAction, EKeys::A).Modifiers.Add(MakeNegate());
	KnightContext->MapKey(MoveAction, EKeys::W).Modifiers.Add(MakeSwizzle()); // forward = +Y
	{
		FEnhancedActionKeyMapping& Back = KnightContext->MapKey(MoveAction, EKeys::S);
		Back.Modifiers.Add(MakeSwizzle());
		Back.Modifiers.Add(MakeNegate());
	}

	{
		UInputModifierNegate* FlipY = NewObject<UInputModifierNegate>(this);
		FlipY->bX = false;
		FlipY->bY = true;
		FlipY->bZ = false;
		GConfig->GetFloat(TEXT("Jeebsland"), TEXT("LookSensitivity"), LookSensitivity, GGameUserSettingsIni);
		FEnhancedActionKeyMapping& MouseMap = KnightContext->MapKey(MouseLookAction, EKeys::Mouse2D);
		MouseMap.Modifiers.Add(FlipY);
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				Subsystem->AddMappingContext(KnightContext, 0);
			}
		}
	}

	// The template's code now binds these actions to movement, looking, and jumping
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Hold Shift to sprint
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &AKnightCharacter::StartSprint);
		EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AKnightCharacter::StopSprint);
		EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &AKnightCharacter::Interact);
		EIC->BindAction(InteractAction, ETriggerEvent::Completed, this, &AKnightCharacter::InteractReleased);
		EIC->BindAction(SettingsAction, ETriggerEvent::Started, this, &AKnightCharacter::OnSettingsKey);
		EIC->BindAction(CrouchAction, ETriggerEvent::Started, this, &AKnightCharacter::StartCrouch);
		EIC->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AKnightCharacter::StopCrouch);
		EIC->BindAction(BlockAction, ETriggerEvent::Started, this, &AKnightCharacter::StartBlock);
		EIC->BindAction(BlockAction, ETriggerEvent::Completed, this, &AKnightCharacter::StopBlock);
		EIC->BindAction(SlideAction, ETriggerEvent::Started, this, &AKnightCharacter::StartSlide);
		EIC->BindAction(RollAction, ETriggerEvent::Started, this, &AKnightCharacter::StartRoll);
		EIC->BindAction(SitAction, ETriggerEvent::Started, this, &AKnightCharacter::OnInteractKey);
		EIC->BindAction(InventoryAction, ETriggerEvent::Started, this, &AKnightCharacter::OnInventoryKey);
		EIC->BindAction(PanelTabAction, ETriggerEvent::Started, this, &AKnightCharacter::CyclePanelTab);
		EIC->BindAction(DodgeLAction, ETriggerEvent::Started, this, &AKnightCharacter::DodgeLeft);
		EIC->BindAction(DodgeRAction, ETriggerEvent::Started, this, &AKnightCharacter::DodgeRight);
		EIC->BindAction(DodgeFAction, ETriggerEvent::Started, this, &AKnightCharacter::DodgeFwd);
		EIC->BindAction(DodgeBAction, ETriggerEvent::Started, this, &AKnightCharacter::DodgeBack);
		EIC->BindAction(PotionAction, ETriggerEvent::Started, this, &AKnightCharacter::DrinkPotion);
		EIC->BindAction(CallHorseAction, ETriggerEvent::Started, this, &AKnightCharacter::CallHorse);
		EIC->BindAction(SkillsAction, ETriggerEvent::Started, this, &AKnightCharacter::OnSkillsKey);
				EIC->BindAction(ScrollUpAction, ETriggerEvent::Started, this, &AKnightCharacter::ScrollUp);
		EIC->BindAction(ScrollDownAction, ETriggerEvent::Started, this, &AKnightCharacter::ScrollDown);
		EIC->BindAction(HotbarActions[0], ETriggerEvent::Started, this, &AKnightCharacter::Select1);
		EIC->BindAction(HotbarActions[1], ETriggerEvent::Started, this, &AKnightCharacter::Select2);
		EIC->BindAction(HotbarActions[2], ETriggerEvent::Started, this, &AKnightCharacter::Select3);
		EIC->BindAction(HotbarActions[3], ETriggerEvent::Started, this, &AKnightCharacter::Select4);
		EIC->BindAction(HotbarActions[4], ETriggerEvent::Started, this, &AKnightCharacter::Select5);
	}
}

void AKnightCharacter::Tick(float DeltaSeconds)
{
	if (HasAuthority()) SendLook();   // online: the server tells everyone what this knight now wears and holds
	SendState();                      // online: our sprint / crouch / shield button, to the server
	if (!bGraphicsApplied && IsLocallyControlled()) ApplyGraphics();   // online, our knight may only become ours after BeginPlay

	// The camera can't swing all the way overhead or underfoot: from looking 60 degrees down to 35 degrees up
	if (!bPitchLimitsSet)
	{
		if (APlayerController* PCtl = Cast<APlayerController>(GetController()))
		{
			if (PCtl->PlayerCameraManager)
			{
				PCtl->PlayerCameraManager->ViewPitchMin = -60.f;
				PCtl->PlayerCameraManager->ViewPitchMax = 35.f;
				bPitchLimitsSet = true;
			}
		}
	}

	if (bShopOpen || bSmithOpen || bDialogueOpen || bBankOpen)
	{
		// Walking (or being pushed) away from whoever you are trading with ends the conversation
		const ANpc* N = TradeNpc.Get();
		if (!N || FVector::Dist2D(GetActorLocation(), N->GetActorLocation()) > 650.f)
		{
			CloseDialogue();
			if (bShopOpen) CloseShop();
			if (bSmithOpen) CloseSmith();
			if (bBankOpen) CloseBank();
		}
	}

	Super::Tick(DeltaSeconds);

	// Working continues while you move; it only stops if you drift about a body length away
	// from where you started, or turn so you are no longer facing the tree or rock
	if (AResourceNode* Node = ActiveNode.Get())
	{
				const bool bTooFar = FVector::Dist2D(GetActorLocation(), GatherStart) > (bFishing ? 320.f : 200.f);
		const FVector ToNode = Node->GetActorLocation() - GetActorLocation();
		const bool bLookedAway = !bFishing && FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToNode.GetSafeNormal2D()) < 0.34f; // beyond about 70 degrees
		if (bTooFar || bLookedAway)
		{
			Node->CancelGathering();
		}
	}

	const float Dt = FMath::Max(DeltaSeconds, 0.0001f);
	const FVector Vel = GetVelocity();
	const float Speed = Vel.Size2D();
	const bool bFalling = GetCharacterMovement()->IsFalling();

	// ---------- Ladders ----------
	if (bClimbing) UpdateClimb(Dt); else TryStartClimb();

	// ---------- Special moves: roll, slide ----------
	UCharacterMovementComponent* Move = GetCharacterMovement();
	SlideCooldown = FMath::Max(0.f, SlideCooldown - Dt);
	RollCooldown = FMath::Max(0.f, RollCooldown - Dt);
	float SpecialSpeed = 0.f;

	if (bRolling)
	{
		RollT += Dt;
		const float K = FMath::Clamp(RollT / RollDur, 0.f, 1.f);
		SpecialSpeed = FMath::Lerp(900.f, 380.f, K * K);
		Move->Velocity.X = RollDir.X * SpecialSpeed;
		Move->Velocity.Y = RollDir.Y * SpecialSpeed;
		if (RollT >= RollDur || bFalling)
		{
			bRolling = false;
			RollCooldown = 1.0f;   // one second before the next roll (every kind of roll)
			Move->bOrientRotationToMovement = bSavedOrient;
		}
	}
	if (bSliding)
	{
		SlideT += Dt;
		// Downhill keeps the slide going; flat ground slows it
		float Downhill = 0.f;
		if (Move->CurrentFloor.IsWalkableFloor())
		{
			Downhill = FMath::Max(0.f, FVector::DotProduct(Move->CurrentFloor.HitResult.ImpactNormal, SlideDir));
		}
		SlideSpeed = FMath::Clamp(SlideSpeed + (-650.f + 2600.f * Downhill) * Dt, 0.f, 1100.f);
		SpecialSpeed = SlideSpeed + 50.f;
		Move->Velocity.X = SlideDir.X * SlideSpeed;
		Move->Velocity.Y = SlideDir.Y * SlideSpeed;
		if (SlideSpeed < 190.f || SlideT > 3.f || AirAlpha > 0.8f)
		{
			bSliding = false;
			SlideCooldown = 0.9f;
			Move->bOrientRotationToMovement = bSavedOrient;
		}
	}
	const bool bSpecial = bRolling || bSliding;

	if (bAttacking)
	{
		AttackT += Dt;
		const float HitFrac = (AtkStyle == EAtkStyle::Slash) ? 0.43f : (AtkStyle == EAtkStyle::Thrust ? 0.5f : 0.3f);
		if (!bAttackHit && AttackT >= AttackDur * HitFrac)
		{
			bAttackHit = true;
			AttackLands();
		}
		if (AttackT >= AttackDur)
		{
			bAttacking = false;
			const float Now = GetWorld()->GetTimeSeconds();
			LastSwingEnd = Now;
			LastSwingDir = AttackDir;
			LastStyle = AtkStyle;
			// Clicked during the swing (within 0.3 s of it ending): go straight into the left-to-right follow-up
			if (AtkStyle == EAtkStyle::Slash && AttackDir > 0 && Now - QueuedClick <= 0.3f && !IsUIOpen())
			{
				StartAttack(EAtkStyle::Slash, -1, 0.f);
			}
		}
	}

	// Spear: charging builds up while the button is held
	if (bCharging)
	{
		ChargeT += Dt;
		if (IsUIOpen() || bRolling || bSliding || bClimbing || bSitting || SelectedKind() != JeebItems::K_Spear) bCharging = false;
	}

	// The horse heals the same way: 10 s after its last hit, a full bar takes 10 s. (Not in PvP areas, once those exist.)
	if (HorseHealth < HorseMaxHealth && GetWorld()->GetTimeSeconds() - LastHorseHurtTime > 10.f)
		HorseHealth = FMath::Min(HorseMaxHealth, HorseHealth + HorseMaxHealth * 0.1f * Dt);
	// Health: your maximum grows with Defence and the Vitality amulet; you heal when left alone for 10 s (the server's job online)
	if (Skills && HasAuthority())
	{
		int32 NewMax = 100 + (Skills->GetLevel(ESkillType::Defense) - 1) * 3 + JeebItems::CombinedMods(Skills).MaxHealthBonus;
		if (GetWorld()->GetTimeSeconds() < VigourUntil) NewMax = FMath::RoundToInt(NewMax * 1.5f);   // vigour potion: a bigger bar for a minute
		if (NewMax != MaxHealth)
		{
			if (NewMax > MaxHealth) Health += NewMax - MaxHealth;   // a bigger bar comes with the extra already full
			else Health = FMath::Clamp(Health, 1.f, (float)NewMax);   // a smaller one only trims what does not fit
			MaxHealth = NewMax;
		}
		if (Health < MaxHealth && GetWorld()->GetTimeSeconds() - LastHurtTime > 10.f)   // 10 s after the last hit it starts to heal
		{
			Health = FMath::Min((float)MaxHealth, Health + MaxHealth * 0.1f * Dt);   // a full bar takes 10 s
		}
	}

	// ---------- Sprint stamina ----------
	const bool bWantSprint = !bMounted && bSprinting && !bCrouched && !bBlocking && !bExhausted;
	const bool bDrain = bWantSprint && Speed > 330.f && !bFalling && !bSpecial;
	if (bDrain)
	{
		Stamina = FMath::Max(0.f, Stamina - 16.f * Dt / GetStaminaMult());   // better boots: it lasts longer...
		StaminaDelay = 1.0f;
		if (Stamina <= 0.f) bExhausted = true;
	}
	else
	{
		StaminaDelay = FMath::Max(0.f, StaminaDelay - Dt);
		if (StaminaDelay <= 0.f) Stamina = FMath::Min(MaxStamina, Stamina + 16.f * Dt * GetStaminaMult());   // ...and comes back faster
	}
	if (bExhausted && Stamina >= 4.f) bExhausted = false;   // sprint again as soon as a little has come back

	// An open sack closes when you walk away from it, or put the cursor away
	if (LootTarget.IsValid() && (!bPanelCursor || FVector::Dist2D(GetActorLocation(), LootTarget->GetActorLocation()) > 400.f)) LootTarget = nullptr;

	// Climbing, riding or sitting: the shield and the weapon go on the back; they come back to the hands afterwards
	{
		const bool bWantStow = bClimbing || bMounted || bSitting;
		if (bWantStow != bStowed)
		{
			bStowed = bWantStow;
			PlaceShield(bStowed);
			RebuildHeld();
			if (bStowed) bBlocking = false;
		}
	}

	// Shield guard: its size is the shield's Defence; a new or changed shield starts full. It recovers like health:
	// 10 s after the last blow it took, a full bar in 10 s. A broken guard is mended only once it is full again.
	if (HasAuthority())   // (online: the server's job; the player's machine is sent the numbers)
	{
		const int32 ShieldId = (Skills && ShieldActive()) ? Skills->Worn[JeebItems::K_Shield] : 0;
		const float NewMax = ShieldId ? float(JeebItems::ShieldGuard(ShieldId)) : 0.f;
		const float RecoverDelay = ShieldId ? JeebItems::ShieldRecoverDelay(JeebItems::StyleOf(ShieldId)) : 10.f;   // the round shield recovers sooner
		if (!FMath::IsNearlyEqual(NewMax, GuardMax)) { GuardMax = NewMax; GuardPool = NewMax; bGuardBroken = false; }
		if (GuardMax > 0.f && GuardPool < GuardMax && GetWorld()->GetTimeSeconds() - LastGuardHitTime > RecoverDelay)
		{
			GuardPool = FMath::Min(GuardMax, GuardPool + GuardMax * 0.1f * Dt);
			if (GuardPool >= GuardMax && bGuardBroken)
			{
				bGuardBroken = false;
				if (Skills) Skills->Say(2.5f, FColor(140, 200, 255), TEXT("Your shield guard is back to full."));
			}
		}
	}

	// The shield and the sword never work together: it is down during a swing, and comes back up once the swing is over if the button is still held
	if (bAttacking || bRolling || Move->IsFalling()) bBlocking = false;   // no guard mid-swing, mid-roll or in the air
	else if (bBlockHeld && !bBlocking && ShieldActive() && !bRolling && !Move->IsFalling() && !bSliding && !bClimbing && !bSitting && !bMounted && !IsUIOpen()) bBlocking = true;

	// ---------- Ground speed (crouching and blocking are slow); slower in the air with a smooth slide down to it ----------
	const float AirMul = 0.69f;
	// Riding: twice the walking speed, and twice the sprint. The horse sprints for about four seconds, then needs
	// its 100-point bar to refill completely before the next gallop.
	bool bHorseSprint = false;
	if (bMounted)
	{
		const bool bAsk = bSprinting && Speed > 200.f && !bHorseTired && !bFalling && (GetWorld()->GetTimeSeconds() - LastBackTime > 0.15);
		if (bAsk && HorseEnergy > 0.f)
		{
			// the horse never tires: its gallop energy does not drain
			bHorseSprint = true;
			if (HorseEnergy <= 0.f) bHorseTired = true;
		}
		else
		{
			HorseEnergy = FMath::Min(100.f, HorseEnergy + (bHorseTired ? 10.f : 16.f) * Dt);
			if (bHorseTired && HorseEnergy >= 4.f) bHorseTired = false;
		}
	}
	// The horse is 20% slower than before, and builds up to a gallop: it does not jump to full speed when you sprint
	float HorseTarget = 0.8f * (bHorseSprint ? 2.f * SprintSpeed : 2.f * WalkSpeed);
	if (!bMounted) HorseSpeedNow = 0.f;
	else
	{
		if (HorseSpeedNow < 1.f) HorseSpeedNow = 0.8f * 2.f * WalkSpeed;
		HorseSpeedNow = FMath::FInterpConstantTo(HorseSpeedNow, HorseTarget, Dt, HorseTarget > HorseSpeedNow ? 330.f : 900.f);   // speeds up over about 1.7 s, eases off quicker
	}
	// The horse backs up slowly with S (never sprinting). On foot, a raised shield is a combat stance: you face where the camera
	// points and sidestep or back up instead of turning round, a little slower than walking (forward 60%, sidestep 50%, back 40%).
	const bool bBackStep = bMounted && (GetWorld()->GetTimeSeconds() - LastBackTime < 0.15);
	const bool bStance = bBlockHeld && !bMounted && !bRolling && !bSliding && !bClimbing && !bSitting;
	if (bStance && HasAuthority() && !IsLocallyControlled() && GetController())
	{
		// (the server doesn't get the player's keys: it reads the stance step from the way the knight is being pushed)
		const FVector Acc = Move->GetCurrentAcceleration().GetSafeNormal2D();
		const FRotator CamYaw(0.f, GetController()->GetControlRotation().Yaw, 0.f);
		StanceFwd = FVector::DotProduct(Acc, FRotationMatrix(CamYaw).GetUnitAxis(EAxis::X));
		StanceRight = FVector::DotProduct(Acc, FRotationMatrix(CamYaw).GetUnitAxis(EAxis::Y));
	}
	if (HasAuthority() && GetNetMode() != NM_Standalone) RepMoveFlags = (bCrouched ? 1 : 0) | (bBlocking ? 2 : 0) | (bRolling ? 4 : 0) | (bSliding ? 8 : 0);
	if (bStance && !bBackOrient) { bBackOrient = true; bBackSavedOrient = Move->bOrientRotationToMovement; Move->bOrientRotationToMovement = false; }
	else if (!bStance && bBackOrient && !bRolling && !bSliding) { bBackOrient = false; Move->bOrientRotationToMovement = bBackSavedOrient; }
	if (bStance && GetController())   // turn to the camera quickly but smoothly (about 0.15 s for a quarter turn)
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, GetController()->GetControlRotation().Yaw, 0.f), Dt, 640.f));
	const float StanceFactor = StanceFwd < -0.1f ? 0.4f : (StanceFwd > 0.1f ? 0.6f : (FMath::Abs(StanceRight) > 0.1f ? 0.5f : 0.6f));
	const float ShieldMove = (Skills && Skills->Worn[JeebItems::K_Shield]) ? JeebItems::ShieldMoveMul(JeebItems::StyleOf(Skills->Worn[JeebItems::K_Shield])) : 1.f;   // round: quicker, tower: slower
	const float BaseSpeed = bBackStep ? 0.5f * WalkSpeed : bMounted ? HorseSpeedNow
	                                 : (bStance ? WalkSpeed * StanceFactor * ShieldMove : ((bCrouched || bBlocking) ? CrouchSpeed : (bWantSprint ? SprintSpeed : WalkSpeed))) * ((!bMounted && GetWorld()->GetTimeSeconds() < SpeedUntil) ? 1.5f : 1.f)   // swiftness potion
	                                 * ((!bMounted && Skills) ? JeebItems::CombinedMods(Skills).SpeedMul : 1.f);   // the Red Helm Brush
	if (bFalling && !bSpecial)
	{
		AirSpeedMul = AirMul;
		const float AirCap = BaseSpeed * AirMul;
		FVector Horizontal(Move->Velocity.X, Move->Velocity.Y, 0.f);
		const float HSpeed = Horizontal.Size();
		if (HSpeed > AirCap)
		{
			const float NewSpeed = FMath::FInterpConstantTo(HSpeed, AirCap, Dt, 1500.f); // about 0.2 s to go from a sprint to the air speed
			Horizontal *= NewSpeed / HSpeed;
			Move->Velocity.X = Horizontal.X;
			Move->Velocity.Y = Horizontal.Y;
		}
	}
	else if (!bFalling)
	{
		AirSpeedMul = FMath::FInterpTo(AirSpeedMul, 1.f, Dt, 6.f); // speeding back up after landing, as before
	}
	Move->MaxWalkSpeed = bSpecial ? SpecialSpeed : BaseSpeed * AirSpeedMul;

	// ---------- Landing detection ----------
	if (bFalling)
	{
		if (!bWasFalling) LastFallVz = 0.f;
		LastFallVz = FMath::Min(LastFallVz, Vel.Z);
	}
	else if (bWasFalling)
	{
		LandImpact = FMath::Clamp(-LastFallVz / 900.f, 0.3f, 1.f); // harder landings squash more
		LandTimer = LandDuration;
	}
	bWasFalling = bFalling;
	LandTimer = FMath::Max(0.f, LandTimer - Dt);
	const float Squash = LandImpact * (LandTimer / LandDuration);

	// ---------- Blends (everything eases in and out) ----------
	MoveBlend = FMath::FInterpTo(MoveBlend, FMath::Clamp(Speed / 150.f, 0.f, 1.f), Dt, 10.f);
	RunAlpha  = FMath::FInterpTo(RunAlpha, FMath::Clamp((Speed - 250.f) / 500.f, 0.f, 1.f), Dt, 6.f); // 0 = walk, 1 = full sprint
	AirAlpha  = FMath::FInterpTo(AirAlpha, bFalling ? 1.f : 0.f, Dt, 14.f);
	CrouchAlpha = FMath::FInterpTo(CrouchAlpha, bCrouched ? 1.f : 0.f, Dt, 10.f);
	BlockAlpha  = FMath::FInterpTo(BlockAlpha, bBlocking ? 1.f : 0.f, Dt, 14.f);
	SlideAlpha  = FMath::FInterpTo(SlideAlpha, bSliding ? 1.f : 0.f, Dt, 12.f);
	SitAlpha    = FMath::FInterpTo(SitAlpha, bSitting ? 1.f : 0.f, Dt, 8.f);
	MountAlpha  = FMath::FInterpTo(MountAlpha, bMounted ? 1.f : 0.f, Dt, 8.f);
	TuckAlpha   = FMath::FInterpTo(TuckAlpha, bRolling ? 1.f : 0.f, Dt, 18.f);
	ClimbAlpha  = FMath::FInterpTo(ClimbAlpha, bClimbing ? 1.f : 0.f, Dt, 12.f);
	const float CS = FMath::Sin(ClimbPhase);
	const float Air = AirAlpha;
	const float Ground = 1.f - Air;
	const float Duck = FMath::Max(CrouchAlpha, BlockAlpha) * Ground; // how low the knight is sitting
	const float Stream = RunAlpha * MoveBlend; // how hard the wind pushes the cape and plume

	// ---------- Gait clock: faster strides when running ----------
	const float Hz = FMath::Lerp(1.3f, 2.1f, RunAlpha) * FMath::Lerp(1.f, 0.7f, CrouchAlpha); // crouch-walking is about 40% slower
	GaitPhase = FMath::Fmod(GaitPhase + 2.f * PI * Hz * Dt, 2.f * PI);
	IdleTime += Dt;
	FlutterTime += Dt * (1.f + 1.5f * Stream);
	const float S = FMath::Sin(GaitPhase); // right leg forward when positive
	const float C = FMath::Cos(GaitPhase);

	// ---------- Strength of each motion ----------
	const float StepScale  = 1.f - 0.4f * CrouchAlpha; // smaller steps when crouched
	const float HipAmp     = FMath::Lerp(32.f, 80.f, RunAlpha) * MoveBlend * StepScale;
	const float KneeAmp    = FMath::Lerp(40.f, 105.f, RunAlpha) * MoveBlend * StepScale;
	const float ArmAmp     = FMath::Lerp(22.f, 55.f, RunAlpha) * MoveBlend;
	const float ElbowBase  = FMath::Lerp(12.f, 70.f, RunAlpha) * MoveBlend;
	const float ElbowSwing = FMath::Lerp(8.f, 25.f, RunAlpha) * MoveBlend;
	const float Lean       = FMath::Lerp(3.f, 20.f, RunAlpha) * MoveBlend;
	const float Twist      = FMath::Lerp(5.f, 12.f, RunAlpha) * MoveBlend;

	// ---------- Jump pose: where are we in the arc? 0 = rising fast ... 1 = falling fast ----------
	const float F01 = FMath::Clamp(0.5f - Vel.Z / 1000.f, 0.f, 1.f);
	const float Asym = FMath::Clamp(Speed / 250.f, 0.f, 1.f); // running leap (one knee up) vs standing jump (both tucked)

	// ---------- Legs ----------
	// On the ground: legs swing opposite each other; the knee bends most as a leg passes under the body
	// (smooth everywhere: no sharp corners in the curve, so no jerks)
	const float SwingR = 0.1f + 0.9f * FMath::Pow(0.5f * (1.f + C), 2.5f) + 0.22f * RunAlpha * FMath::Square(0.5f * (1.f - C));
	const float SwingL = 0.1f + 0.9f * FMath::Pow(0.5f * (1.f - C), 2.5f) + 0.22f * RunAlpha * FMath::Square(0.5f * (1.f + C));
	// Shield-up stance: the legs step the way you are actually moving. Backing up runs the stride in reverse; a sidestep
	// swings the legs out to the side instead (the leading leg reaches out, the other closes up after it).
	{
		const FVector LV = GetActorRotation().UnrotateVector(FVector(Vel.X, Vel.Y, 0.f));
		float Fw = 1.f, Sd = 0.f;
		if (bBlockHeld && !bMounted && LV.Size() > 30.f) { Fw = LV.X / LV.Size(); Sd = LV.Y / LV.Size(); }
		StepFw = FMath::FInterpTo(StepFw, Fw, Dt, 8.f);
		StepSd = FMath::FInterpTo(StepSd, Sd, Dt, 8.f);
	}
	const float HipRg = HipAmp * S * StepFw;
	const float HipLg = -HipAmp * S * StepFw;
	const float SideAmp = 24.f * MoveBlend * Ground * (1.f - SlideAlpha) * (1.f - SitAlpha) * (1.f - MountAlpha) * (1.f - ClimbAlpha) * (1.f - TuckAlpha);
	const float HipRroll = SideAmp * StepSd * (0.5f + 0.5f * S);   // positive roll swings a hanging leg to the right
	const float HipLroll = SideAmp * StepSd * (0.5f - 0.5f * S);
	const float KneeRg = -KneeAmp * SwingR;
	const float KneeLg = -KneeAmp * SwingL;
	// In the air: knees tuck on the way up, legs reach forward on the way down
	const float HipRa  = FMath::Lerp(FMath::Lerp(30.f, 60.f, Asym), FMath::Lerp(15.f, 35.f, Asym), F01);
	const float HipLa  = FMath::Lerp(FMath::Lerp(30.f, -15.f, Asym), FMath::Lerp(15.f, 0.f, Asym), F01);
	const float KneeRa = -FMath::Lerp(FMath::Lerp(55.f, 85.f, Asym), 25.f, F01);
	const float KneeLa = -FMath::Lerp(FMath::Lerp(55.f, 40.f, Asym), 20.f, F01);

	float HipRf  = FMath::Lerp(HipRg, HipRa, Air) + 35.f * Squash + 55.f * Duck;
	float HipLf  = FMath::Lerp(HipLg, HipLa, Air) + 35.f * Squash + 55.f * Duck;
	float KneeRf = FMath::Lerp(KneeRg, KneeRa, Air) - 70.f * Squash - 95.f * Duck;
	float KneeLf = FMath::Lerp(KneeLg, KneeLa, Air) - 70.f * Squash - 95.f * Duck;
	// Standing still: slow breathing (the chest rises, the knees ease a little), like the idle in Breath of the Wild
	const float IdleAmt = (1.f - MoveBlend) * Ground * (1.f - SlideAlpha) * (1.f - SitAlpha) * (1.f - MountAlpha) * (1.f - ClimbAlpha) * (1.f - TuckAlpha);
	const float BreathS = FMath::Sin(IdleTime * 1.65f);   // one breath every 3.8 s
	const float Stance = StanceHip - 1.5f * BreathS * IdleAmt;
	// A relaxed stance: on the ground the knees are always a little bent (eased out as he crouches, which bends them anyway)
	const float Relax = Ground * (1.f - Duck);
	HipRf += Stance * Relax;   HipLf += Stance * Relax;
	KneeRf -= 2.f * Stance * Relax;   KneeLf -= 2.f * Stance * Relax;
	// Sliding: one leg stretched out in front, the other folded under
	HipRf = FMath::Lerp(HipRf, 78.f, SlideAlpha);   KneeRf = FMath::Lerp(KneeRf, -4.f, SlideAlpha);
	HipLf = FMath::Lerp(HipLf, 45.f, SlideAlpha);   KneeLf = FMath::Lerp(KneeLf, -115.f, SlideAlpha);
	// Sitting: thighs level, shins hanging down
	HipRf = FMath::Lerp(HipRf, 90.f, SitAlpha);     KneeRf = FMath::Lerp(KneeRf, -90.f, SitAlpha);
	HipLf = FMath::Lerp(HipLf, 90.f, SitAlpha);     KneeLf = FMath::Lerp(KneeLf, -90.f, SitAlpha);
	// Riding: thighs forward and a little apart, shins hanging beside the horse
	HipRf = FMath::Lerp(HipRf, 62.f, MountAlpha);   KneeRf = FMath::Lerp(KneeRf, -42.f, MountAlpha);
	HipLf = FMath::Lerp(HipLf, 62.f, MountAlpha);   KneeLf = FMath::Lerp(KneeLf, -42.f, MountAlpha);
	// Climbing: legs step up the rungs alternately
	HipRf = FMath::Lerp(HipRf, 55.f + 45.f * CS, ClimbAlpha);   KneeRf = FMath::Lerp(KneeRf, -(65.f + 35.f * CS), ClimbAlpha);
	HipLf = FMath::Lerp(HipLf, 55.f - 45.f * CS, ClimbAlpha);   KneeLf = FMath::Lerp(KneeLf, -(65.f - 35.f * CS), ClimbAlpha);
	// Rolling: tucked into a ball
	HipRf = FMath::Lerp(HipRf, 105.f, TuckAlpha);   KneeRf = FMath::Lerp(KneeRf, -125.f, TuckAlpha);
	HipLf = FMath::Lerp(HipLf, 105.f, TuckAlpha);   KneeLf = FMath::Lerp(KneeLf, -125.f, TuckAlpha);

	HipR->SetRelativeRotation(FRotator(HipRf, 0.f, HipRroll));
	HipL->SetRelativeRotation(FRotator(HipLf, 0.f, HipLroll));
	KneeR->SetRelativeRotation(FRotator(KneeRf, 0.f, 0.f));
	KneeL->SetRelativeRotation(FRotator(KneeLf, 0.f, 0.f));
	// Feet stay flat on the ground (a running stride lets them tip a little); in the air, sitting, riding, climbing,
	// sliding and rolling they follow the shins as before
	const float Level = Ground * (1.f - SlideAlpha) * (1.f - SitAlpha) * (1.f - MountAlpha) * (1.f - ClimbAlpha) * (1.f - TuckAlpha) * (1.f - 0.4f * RunAlpha * MoveBlend);
	AnkleR->SetRelativeRotation(FRotator(-(HipRf + KneeRf) * Level, 0.f, 0.f));
	AnkleL->SetRelativeRotation(FRotator(-(HipLf + KneeLf) * Level, 0.f, 0.f));

	// ---------- Body bounce ----------
	// The hips dip as the legs spread (so planted feet stay on the ground) and rise over each step
	// Walking: the hips dip when the legs spread. Running: the body is lowest mid-stance and floats up
	// while the legs are spread, so there is a brief moment with both feet off the ground.
	const float LegLen = 46.5f;   // hip to ankle (the feet stay flat, so the ankle is what rises and falls)
	const float WalkBounce = -LegLen * (1.f - FMath::Cos(FMath::DegreesToRadians(HipAmp * S)));
	const float RunBounce  = -0.75f * LegLen * (1.f - FMath::Cos(FMath::DegreesToRadians(HipAmp * S))) + 6.f * S * S;
	const float Bounce = FMath::Lerp(WalkBounce, RunBounce, RunAlpha);
	// Bent knees (relaxed stance, crouching, guarding, landing): the body sinks exactly as far as the legs fold, so the
	// flat feet stay on the ground (thigh 23 cm, shin 23.5 cm)
	const float PoseH = 35.f * Squash + 55.f * Duck + Stance * Relax;
	const float PoseK = -70.f * Squash - 95.f * Duck - 2.f * Stance * Relax;
	const float LegDrop = 23.f * (1.f - FMath::Cos(FMath::DegreesToRadians(PoseH))) + 23.5f * (1.f - FMath::Cos(FMath::DegreesToRadians(PoseH + PoseK)));
	float BodyZ = FMath::Lerp(Bounce, 0.f, Air) - LegDrop;
	BodyZ = FMath::Lerp(BodyZ, -42.f, SlideAlpha);
	BodyZ = FMath::Lerp(BodyZ, 0.f, SitAlpha);
	BodyZ = FMath::Lerp(BodyZ, 98.f, MountAlpha);                  // up onto the saddle
	BodyZ = FMath::Lerp(BodyZ, -45.f, TuckAlpha);
	SmoothBodyZ = FMath::FInterpTo(SmoothBodyZ, BodyZ, Dt, 22.f); // soften any remaining steps
	BodyRoot->SetRelativeLocation(FVector(0.f, 0.f, SmoothBodyZ));

	// ---------- Chest: lean into the run, twist against the hips, lean into turns ----------
	const float NewYaw = GetActorRotation().Yaw;
	const float TurnRate = FMath::Clamp(FMath::FindDeltaAngleDegrees(PrevYaw, NewYaw) / Dt, -720.f, 720.f);
	PrevYaw = NewYaw;
	YawRate = FMath::FInterpTo(YawRate, TurnRate, Dt, 8.f);
	const float TurnRoll = FMath::Clamp(YawRate * 0.025f, -12.f, 12.f) * Stream;

	const float AirLean = FMath::Lerp(4.f, -10.f, F01); // slight arch back while rising, forward while falling
	// at ease the chest leans a little forward (not standing against a wall) and lifts with each breath
	float UpperPitch = FMath::Lerp(-Lean - RestLean * Ground, AirLean, Air) - 12.f * Squash - 14.f * Duck - 10.f * BlockAlpha * Ground + 0.9f * BreathS * IdleAmt;   // behind the shield he hunches over it
	UpperPitch = FMath::Lerp(UpperPitch, 28.f, SlideAlpha);  // lean back into the slide
	UpperPitch = FMath::Lerp(UpperPitch, 0.f, SitAlpha);
	UpperPitch = FMath::Lerp(UpperPitch, -6.f * RunAlpha, MountAlpha);   // a little forward in a gallop
	UpperPitch = FMath::Lerp(UpperPitch, -45.f, TuckAlpha);
	const float Sway = 0.9f * FMath::Sin(IdleTime * 0.62f) * IdleAmt;   // a slow shift of weight from side to side while standing
	UpperPivot->SetRelativeRotation(FRotator(UpperPitch, Twist * S * Ground, TurnRoll + Sway));

	// Chest bob: the upper body sinks as each foot takes weight and springs back up between steps.
	// (Only the chest moves, so the feet never sink into the ground.) In the air it stretches on the way up.
	const float BobAmp = FMath::Lerp(4.f, 16.f, RunAlpha) * MoveBlend;
	const float ChestTarget = (Ground * (-BobAmp * (1.f - S * S)) + Air * FMath::Lerp(5.f, -4.f, F01)) * (1.f - FMath::Max(SlideAlpha, SitAlpha)) + 0.6f * BreathS * IdleAmt;   // the chest rises as he breathes in
	SmoothChestZ = FMath::FInterpTo(SmoothChestZ, ChestTarget, Dt, 30.f);
	UpperPivot->SetRelativeLocation(UpperBase + FVector(0.f, 0.f, SmoothChestZ));

	// Pelvis sways with the steps: a little roll as weight shifts, and hips turning against the shoulders
	float FlipPitch = 0.f;
	if (bRolling)
	{
		const float K = FMath::Clamp(RollT / RollDur, 0.f, 1.f);
		FlipPitch = -360.f * (K * K * (3.f - 2.f * K)); // one forward somersault
	}
	BodyRoot->SetRelativeRotation(FRotator(FlipPitch, -0.5f * Twist * S * Ground, 2.5f * MoveBlend * C * Ground));

	// The head counter-steers so the knight keeps looking ahead, with a small nod on each step
	const float Nod = 2.5f * FMath::Sin(2.f * GaitPhase) * MoveBlend * Ground;
	HeadPivot->SetRelativeRotation(FRotator(-0.6f * UpperPitch + Nod - 7.f * Ground - 12.f * BlockAlpha * Ground, -0.7f * Twist * S * Ground, -0.5f * TurnRoll - 0.7f * Sway));

	// ---------- Cape and plume ----------
	const float FlapAmp = 3.f + 9.f * Stream;
	const float CapeUpperG = FMath::Lerp(-4.f, -28.f, Stream) + FlapAmp * FMath::Sin(FlutterTime * 3.1f);
	const float CapeLowerG = FMath::Lerp(-6.f, -38.f, Stream) + FlapAmp * 1.6f * FMath::Sin(FlutterTime * 5.3f + 1.2f);
	const float CapeUpperA = FMath::Lerp(-5.f, -40.f, F01);  // streams down while rising, flies up while falling
	const float CapeLowerA = FMath::Lerp(-8.f, -65.f, F01);
	CapeUpper->SetRelativeRotation(FRotator(
		FMath::Lerp(CapeUpperG, CapeUpperA, Air), 0.f,
		(2.f + 5.f * Stream) * FMath::Sin(FlutterTime * 2.3f) - 0.5f * TurnRoll));
	CapeLower->SetRelativeRotation(FRotator(
		FMath::Lerp(CapeLowerG, CapeLowerA, Air), 0.f,
		(3.f + 9.f * Stream) * FMath::Sin(FlutterTime * 3.7f + 0.8f) - 0.8f * TurnRoll));

	const float PlumeG = FMath::Lerp(0.f, 20.f, Stream) + (3.f + 5.f * Stream) * FMath::Sin(FlutterTime * 4.1f + 0.5f);
	const float PlumeA = FMath::Lerp(20.f, -15.f, F01);
	PlumePivot->SetRelativeRotation(FRotator(FMath::Lerp(PlumeG, PlumeA, Air), 0.f, 4.f * Stream * FMath::Sin(FlutterTime * 3.3f)));
	TickCape(Dt);

	// ---------- Arms ----------
	const float ArmSway = 1.5f * FMath::Sin(IdleTime * 1.7f) * (1.f - MoveBlend);
	const float ArmAirPitch = FMath::Lerp(100.f, 25.f, F01);   // arms thrown up on the way up, then out
	const float ArmAirSpread = FMath::Lerp(20.f, 50.f, F01);

	// Left arm (shield): swings opposite the left leg; raised in front of the chest when blocking
	ShieldHoldA = FMath::FInterpTo(ShieldHoldA, (ShieldActive() && !bStowed) ? 1.f : 0.f, Dt, 8.f);   // no shield: the left arm hangs normally
	float PitchL = FMath::Lerp(0.6f * ArmAmp * S + ArmSway, ArmAirPitch, Air) + 25.f * Squash + 4.f * Ground;
	float RollL = Air * ArmAirSpread * 0.7f + 1.2f * BreathS * IdleAmt;
	float ElbowLdeg = FMath::Lerp(ElbowBase + ElbowSwing * FMath::Max(S, 0.f), 15.f, Air) + ShieldRest().ElbowPitch * Ground * ShieldHoldA;   // at rest the left hand rests on the inside of the shield (when there is one)
	PitchL = FMath::Lerp(PitchL, 38.f, BlockAlpha);
	ElbowLdeg = FMath::Lerp(ElbowLdeg, 55.f, BlockAlpha);
	PitchL = FMath::Lerp(PitchL, -40.f, SlideAlpha);   RollL = FMath::Lerp(RollL, 30.f, SlideAlpha);
	PitchL = FMath::Lerp(PitchL, 32.f, SitAlpha);      ElbowLdeg = FMath::Lerp(ElbowLdeg, 70.f, SitAlpha);
	PitchL = FMath::Lerp(PitchL, 38.f, MountAlpha);    ElbowLdeg = FMath::Lerp(ElbowLdeg, 55.f, MountAlpha);    // holding the reins
	PitchL = FMath::Lerp(PitchL, 85.f, TuckAlpha);     ElbowLdeg = FMath::Lerp(ElbowLdeg, 100.f, TuckAlpha);
	PitchL = FMath::Lerp(PitchL, 140.f + 35.f * CS, ClimbAlpha);  ElbowLdeg = FMath::Lerp(ElbowLdeg, 25.f, ClimbAlpha);
	ArmPivotL->SetRelativeLocation(LeftBase + BlockAlpha * FVector(22.f, 30.f, 10.f)); // shield comes across in front of the body
	ArmPivotL->SetRelativeRotation(FRotator(PitchL, 0.f, RollL));
	ElbowL->SetRelativeRotation(FRotator(ElbowLdeg, ShieldRest().ElbowYaw * Ground * (1.f - BlockAlpha) * ShieldHoldA, ShieldRest().ElbowRoll * Ground * (1.f - BlockAlpha) * ShieldHoldA));   // the forearm flat behind the shield
	HoldShield();

	// Right arm (sword): swings opposite the right leg, unless it is busy mining or chopping
	float TargetPitch = FMath::Lerp(-0.22f * ArmAmp * S - ArmSway, ArmAirPitch, Air) + 25.f * Squash + 4.f * Ground;   // arms hang a touch forward (relaxed shoulders)
	float TargetYaw = 0.f;
	float RollR = -Air * ArmAirSpread - 1.2f * BreathS * IdleAmt - (EaseKind == JeebItems::K_Staff ? 14.f * EaseAlpha : 0.f);   // the arms ease out a little with each breath (and out to the side to hold a staff)
	float ElbowRdeg = FMath::Lerp(HeldElbow + 0.25f * ElbowBase, 40.f, Air);
	float ArmSpeed = 25.f;

	TargetPitch = FMath::Lerp(TargetPitch, -35.f, SlideAlpha);   RollR = FMath::Lerp(RollR, -30.f, SlideAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 32.f, SitAlpha);      ElbowRdeg = FMath::Lerp(ElbowRdeg, 70.f, SitAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 38.f, MountAlpha);    ElbowRdeg = FMath::Lerp(ElbowRdeg, 55.f, MountAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 85.f, TuckAlpha);     ElbowRdeg = FMath::Lerp(ElbowRdeg, 100.f, TuckAlpha);
	TargetPitch = FMath::Lerp(TargetPitch, 140.f - 35.f * CS, ClimbAlpha);  ElbowRdeg = FMath::Lerp(ElbowRdeg, 25.f, ClimbAlpha);

	if (bCharging && !bAttacking && !bSwinging)
	{
		// Spear drawn back, the stronger the pull the further back
		const float K = FMath::Clamp(ChargeT / 1.0f, 0.f, 1.f);
		TargetPitch = FMath::Lerp(70.f, 45.f, K);
		TargetYaw = FMath::Lerp(0.f, 18.f, K);
		ElbowRdeg = FMath::Lerp(60.f, 110.f, K);
		RollR = 0.f;
		ArmSpeed = 20.f;
	}

	if (bAttacking && !bSwinging)
	{
		const float T = AttackT / AttackDur;
		if (AtkStyle == EAtkStyle::Thrust)
		{
			// Pull back, then drive the arm straight out
			if (T < 0.35f)
			{
				const float A = T / 0.35f;
				TargetPitch = FMath::Lerp(55.f, 45.f, A);  TargetYaw = FMath::Lerp(10.f, 18.f, A);  ElbowRdeg = FMath::Lerp(90.f, 115.f, A);
			}
			else
			{
				const float A = FMath::Clamp((T - 0.35f) / 0.25f, 0.f, 1.f);
				TargetPitch = FMath::Lerp(45.f, 88.f, A);  TargetYaw = FMath::Lerp(18.f, 0.f, A);  ElbowRdeg = FMath::Lerp(115.f, 4.f, A);
			}
		}
		else if (AtkStyle == EAtkStyle::Cast)
		{
			// Raise the arm and point
			const float A = FMath::Clamp(T / 0.3f, 0.f, 1.f);
			const float E = A * A * (3.f - 2.f * A);
			TargetPitch = FMath::Lerp(20.f, 92.f, E);  TargetYaw = 0.f;  ElbowRdeg = FMath::Lerp(50.f, 8.f, E);
		}
		else if (AttackDir > 0)
		{
						// First swing, right to left: the arm comes up to shoulder height and swings out to the right,
			// then the whole arm sweeps flat across the body
			if (T < 0.35f)
			{
				const float A = T / 0.35f;
				const float E = A * A * (3.f - 2.f * A);
				TargetPitch = FMath::Lerp(10.f, 78.f, E);
				TargetYaw = FMath::Lerp(0.f, 80.f, E);
				ElbowRdeg = FMath::Lerp(ElbowRdeg, 65.f, E);
			}
			else if (T < 0.55f)
			{
				const float A = (T - 0.35f) / 0.2f;
				TargetPitch = FMath::Lerp(78.f, 84.f, A);
				TargetYaw = FMath::Lerp(80.f, -70.f, A * A);
				ElbowRdeg = FMath::Lerp(65.f, 5.f, A);
			}
			else { TargetPitch = 84.f; TargetYaw = -70.f; ElbowRdeg = 5.f; }
		}
		else
		{
						// Follow-up, left to right: starts where the first one ended, draws back across the body, whips flat back out
			if (T < 0.35f)
			{
				const float A = T / 0.35f;
				const float E = A * A * (3.f - 2.f * A);
				TargetPitch = FMath::Lerp(84.f, 80.f, E);
				TargetYaw = FMath::Lerp(-70.f, -88.f, E);
				ElbowRdeg = FMath::Lerp(5.f, 70.f, E);
			}
			else if (T < 0.55f)
			{
				const float A = (T - 0.35f) / 0.2f;
				TargetPitch = FMath::Lerp(80.f, 84.f, A);
				TargetYaw = FMath::Lerp(-88.f, 75.f, A * A);
				ElbowRdeg = FMath::Lerp(70.f, 5.f, A);
			}
			else { TargetPitch = 84.f; TargetYaw = 75.f; ElbowRdeg = 5.f; }
		}
		RollR = 0.f;
		ArmSpeed = 38.f;
	}

	if (bFishing)
	{
		// Pole held out over the water, with a slow bob
		TargetPitch = 66.f + 3.f * FMath::Sin(IdleTime * 2.f);
		TargetYaw = 0.f;
		RollR = 0.f;
		ElbowRdeg = 22.f;
		ArmSpeed = 14.f;
	}

	if (bSwinging)
	{
		SwingTime += Dt;
		const float T = FMath::Fmod(SwingTime, SwingPeriod) / SwingPeriod;

		if (T < 0.55f)
		{
			// Wind up: raise the arm smoothly
			const float A = T / 0.55f;
			TargetPitch = FMath::Lerp(StrikeAngle, WindupAngle, A * A * (3.f - 2.f * A));
		}
		else if (T < 0.7f)
		{
			// Strike: fast swing down
			const float A = (T - 0.55f) / 0.15f;
			TargetPitch = FMath::Lerp(WindupAngle, StrikeAngle, A * A);
		}
		else
		{
			// Brief pause on impact
			TargetPitch = StrikeAngle;
		}
		if (bSideways)
		{
			// The swing value drives a horizontal sweep, with the arm held out in front
			TargetYaw = TargetPitch;
			TargetPitch = 80.f;
		}
		RollR = 0.f;
		ElbowRdeg = 0.f;
		ArmSpeed = 30.f;
	}

	ArmPitch = FMath::FInterpTo(ArmPitch, TargetPitch, Dt, ArmSpeed);
	ArmYaw = FMath::FInterpTo(ArmYaw, TargetYaw, Dt, ArmSpeed);
	ArmPivot->SetRelativeRotation(FRotator(ArmPitch, ArmYaw, RollR));
	ElbowRCur = FMath::FInterpTo(ElbowRCur, ElbowRdeg, Dt, 18.f);
	ElbowR->SetRelativeRotation(FRotator(ElbowRCur, 0.f, 0.f));
	HoldWeaponAtEase(Dt, bAttacking || bCharging || bSwinging || bFishing || Air > 0.5f || FMath::Max(FMath::Max(SitAlpha, MountAlpha), FMath::Max(ClimbAlpha, FMath::Max(TuckAlpha, SlideAlpha))) > 0.5f);

	// ---------- Dust puffs: footfalls when running, landings, slides ----------
	const FVector FeetBase = GetActorLocation() + FVector(0.f, 0.f, -94.f);
	if (!bFalling && !bSpecial && MoveBlend > 0.6f && RunAlpha > 0.3f && ((C > 0.f) != (PrevC > 0.f)))
	{
		const float Side = (S > 0.f) ? 13.f : -13.f;
		Kick(FeetBase + GetActorForwardVector() * 8.f + GetActorRightVector() * Side, 0.7f + 0.4f * RunAlpha);
	}
	PrevC = C;
	if (!bFalling && bWasFalling == false && LandTimer > LandDuration - Dt * 1.5f && LandImpact > 0.45f)
	{
		Kick(FeetBase + GetActorRightVector() * 13.f, 0.8f + 1.0f * LandImpact);
		Kick(FeetBase - GetActorRightVector() * 13.f, 0.8f + 1.0f * LandImpact);
	}
	if (bSliding)
	{
		SlideDustTimer += Dt;
		if (SlideDustTimer > 0.12f)
		{
			SlideDustTimer = 0.f;
			Kick(FeetBase - SlideDir * 20.f + GetActorRightVector() * FMath::FRandRange(-15.f, 15.f), 0.6f);
		}
	}
	for (int32 i = 0; i < Dust.Num(); ++i)
	{
		FDust& D = Dust[i];
		D.Age += Dt;
		FTransform T(FQuat::Identity, FVector(0.f, 0.f, -9000.f), FVector(0.001f));
		if (D.Age < D.Life)
		{
			const float K = D.Age / D.Life;
			const float Sz = D.Size * (0.4f + 0.9f * FMath::Sqrt(K)) * (1.f - K * K) / 100.f;
			T = FTransform(FQuat::Identity, D.Pos + FVector(0.f, 0.f, 30.f * K), FVector(FMath::Max(Sz, 0.001f)));
		}
		DustISM->UpdateInstanceTransform(i, T, true, i == Dust.Num() - 1, true);
	}
	TickBits(Clods, ClodISM, Dt, 980.f, 0.6f);    // clods: heavy, they drop straight back
	TickBits(Blades, BladeISM, Dt, 520.f, 2.4f);  // blades: light, they flutter down
	TickBits(LeavesA, LeafAISM, Dt, 380.f, 3.2f);  // leaves: lightest, they drift down
	TickBits(LeavesB, LeafBISM, Dt, 380.f, 3.2f);
}

