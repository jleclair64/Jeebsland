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
	WristR     = MakePivot(TEXT("WristR"),     ElbowR,    KnightDesign::PivotPos(KnightDesign::P_WristR));   // (2026-10-31) the sword hand
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
	// TESTING ONLY: editor Play in single player on the owner's PC gets every skill at 99 while MaxLevelInEditor is on
	// ([Jeebsland.Testing] in DefaultGame.ini; online logins are handled by AKnightGameMode). Never in a packaged game.
	if (Skills && HasAuthority() && UsesLocalSave() && GetWorld() && GetWorld()->IsPlayInEditor())
	{
		bool bEditorMax = false;
		if (GConfig) GConfig->GetBool(TEXT("Jeebsland.Testing"), TEXT("MaxLevelInEditor"), bEditorMax, GGameIni);
		if (bEditorMax) { Skills->SetAllMaxLevel(); Skills->Say(5.f, FColor(255, 215, 90), TEXT("Editor test: every skill set to level 99.")); }
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
	DOREPLIFETIME_CONDITION(AKnightCharacter, RadarDots, COND_OwnerOnly);
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
	StrafeAction = MakeBool(TEXT("KnightStrafe"));
	SitAction    = MakeBool(TEXT("KnightSit"));
	KnightContext->MapKey(CrouchAction, EKeys::LeftControl);
	KnightContext->MapKey(BlockAction, EKeys::RightMouseButton);
	KnightContext->MapKey(SlideAction, EKeys::C);
	KnightContext->MapKey(RollAction, EKeys::V);
	KnightContext->MapKey(RollAction, EKeys::LeftAlt);
	KnightContext->MapKey(StrafeAction, EKeys::MiddleMouseButton);   // strafe mode; X does the same (laptops have no wheel click)
	KnightContext->MapKey(StrafeAction, EKeys::X);
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
	KnightContext->MapKey(ScrollDownAction, EKeys::MouseScrollDown);   // (the low sweep)
	NextWeaponAction = MakeBool(TEXT("KnightNextWeapon"));
	KnightContext->MapKey(NextWeaponAction, EKeys::R);   // (R steps through the weapon bar; the wheel no longer does)
	ArrowsAction = MakeBool(TEXT("KnightArrows"));
	KnightContext->MapKey(ArrowsAction, EKeys::F);   // (F picks the arrows: the arrow slot)
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
		EIC->BindAction(StrafeAction, ETriggerEvent::Started, this, &AKnightCharacter::ToggleStrafe);
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
		EIC->BindAction(NextWeaponAction, ETriggerEvent::Started, this, &AKnightCharacter::NextWeapon);
		EIC->BindAction(ArrowsAction, ETriggerEvent::Started, this, &AKnightCharacter::CycleArrows);
		EIC->BindAction(HotbarActions[0], ETriggerEvent::Started, this, &AKnightCharacter::Select1);
		EIC->BindAction(HotbarActions[1], ETriggerEvent::Started, this, &AKnightCharacter::Select2);
		EIC->BindAction(HotbarActions[2], ETriggerEvent::Started, this, &AKnightCharacter::Select3);
		EIC->BindAction(HotbarActions[3], ETriggerEvent::Started, this, &AKnightCharacter::Select4);
		EIC->BindAction(HotbarActions[4], ETriggerEvent::Started, this, &AKnightCharacter::Select5);
	}
}

// Every frame, in this order. Each step lives in the file it belongs to:
//   KnightCharacter.cpp            TickHousekeeping, TickGatherReach (and the ladders, here)
//   KnightCharacterMovement.cpp    TickRollSlide, TickStamina, TickGroundSpeed
//   KnightCharacterCombat.cpp      TickAttack, TickHealth, TickGear (the shield guard, stowing, map fog)
//   KnightCharacterBow.cpp         TickBow: the bow's draw, the camera over the shoulder, the string and the quiver
//   KnightCharacterPose.cpp        TickPose: the whole body's pose (legs and stride, body, cape, arms)
//   KnightCharacterFx.cpp          TickDust: footfall dust, clods, grass and leaves
void AKnightCharacter::Tick(float DeltaSeconds)
{
	TickHousekeeping();

	Super::Tick(DeltaSeconds);

	TickGatherReach();

	const float Dt = FMath::Max(DeltaSeconds, 0.0001f);
	const FVector Vel = GetVelocity();
	const float Speed = Vel.Size2D();
	const bool bFalling = GetCharacterMovement()->IsFalling();

	// ---------- Ladders ----------
	// (online the player's own machine climbs and the server takes its word for where the knight is on the ladder; the
	// server can't climb by itself because it doesn't get the W and S keys)
	if (IsLocallyControlled() || GetNetMode() == NM_Standalone) { if (bClimbing) UpdateClimb(Dt); else TryStartClimb(); }
	else if (bClimbing && !HasAuthority()) ClimbPhase += Vel.Z / 400.f * 7.f * Dt;   // another player's knight: hands and feet follow how fast it rises

	float SpecialSpeed = 0.f;
	const bool bSpecial = TickRollSlide(Dt, bFalling, SpecialSpeed);
	TickAttack(Dt);
	TickBow(Dt);
	TickKnock(Dt);
	TickHealth(Dt);
	const bool bWantSprint = TickStamina(Dt, Speed, bFalling, bSpecial);
	TickGear(Dt);
	const bool bFaceCam = TickGroundSpeed(Dt, Speed, bFalling, bSpecial, SpecialSpeed, bWantSprint);
	float S = 0.f, C = 0.f;   // where the stride is (for the footfall dust)
	TickPose(Dt, Vel, Speed, bFalling, bFaceCam, S, C);
	if (bShotFace && bAttacking) LogSwordTip();   // (test pictures only: the sword tip's path, for checking a move)
	TickHitboxes(Dt);   // (after the pose: the hit boxes and hurt boxes follow the joints as posed this frame)
	TickDust(Dt, bFalling, bSpecial, S, C);
	TickSwordTrail(Dt);
}

// Online messages, graphics once our knight is ours, camera limits, and walking away from a trader
void AKnightCharacter::TickHousekeeping()
{
	if (HasAuthority()) UpdateRadar();
	if (HasAuthority()) SendLook();   // online: the server tells everyone what this knight now wears and holds
	SendState();                      // online: our sprint / crouch / shield button, to the server
	if (!bGraphicsApplied && IsLocallyControlled()) ApplyGraphics();   // online, our knight may only become ours after BeginPlay
	// Coming from the starting screen the mouse is still in menu mode: hand it back to the camera once our knight is ours
	if (!bUIModeSet && IsLocallyControlled() && GetController()) { bUIModeSet = true; RefreshUIMode(); }

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
}

// Working continues while you move; it only stops if you drift about a body length away
// from where you started, or turn so you are no longer facing the tree or rock
void AKnightCharacter::TickGatherReach()
{
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
}

