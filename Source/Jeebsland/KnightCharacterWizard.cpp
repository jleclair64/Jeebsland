// The playable wizard (2026-11-21, owner): the knight's pawn with bIsWizard set, on the knight's own skeleton, moves and dash attack.
// This file holds only what is his:
//   - the orb: his guard is a see-through blue ball all round him in place of a shield (same guard health, drain and break rules; it
//     stops blows from every side). Every machine draws it from bBlocking, which is already shared.
//   - the teleport: in the roll's place (wheel down + a direction, the roll's cooldown and stamina) he vanishes and appears a few feet
//     away. Only the server chooses the spot (BlinkSpot: on walkable ground, in the open, in clear sight, never past the distance);
//     the player's machine asks with a direction and nothing else.
//   - his starting kit and what he may wear are in SkillComponent.cpp and Inventory.h (CanEquip); his body in KnightAnimatorWizard.cpp.
// Numbers: Content/Data/Tuning.ini [Wizard].
#include "KnightCharacter.h"
#include "KnightMovement.h"
#include "Inventory.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "KnightPlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

AWizardCharacter::AWizardCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsWizard = true;
}

bool JeebItems::OwnerIsWizard(const USkillComponent* S)
{
	const AKnightCharacter* K = S ? Cast<AKnightCharacter>(S->GetOwner()) : nullptr;
	return K && K->IsWizard();
}

// His orb is as strong as a kite shield of his spell book's tier (tin with no book): the book is what a better guard comes from
int32 AKnightCharacter::WizardGuardItem() const
{
	const int32 Book = Skills ? Skills->Worn[JeebItems::K_Book] : 0;
	return JeebItems::ShieldId(0, Book ? JeebItems::TierOf(Book) : JeebItems::T_Tin);
}

// ---------------- The teleport (the roll's place) ----------------

// Where a teleport that way would put him: the farthest spot within the distance that is on walkable ground near his own height (never
// off an edge or up a wall), with room for his body and in clear sight of where he stands (never through a wall). False: nowhere.
bool AKnightCharacter::BlinkSpot(const FVector& Dir, FVector& Out) const
{
	const UCapsuleComponent* Cap = GetCapsuleComponent();
	const float HH = Cap->GetScaledCapsuleHalfHeight(), R = Cap->GetScaledCapsuleRadius();
	const FVector D = Dir.GetSafeNormal2D(), Loc = GetActorLocation();
	if (D.IsNearlyZero() || !GetWorld()) return false;
	const auto& T = JeebTune::V();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebBlink), false, this);
	const FCollisionShape Body = FCollisionShape::MakeCapsule(R * 0.95f, HH * 0.95f);
	for (float d = T.WizardBlinkDist; d >= 50.f; d -= 25.f)
	{
		const FVector P = Loc + D * d;
		FHitResult Floor;
		if (!GetWorld()->LineTraceSingleByChannel(Floor, P + FVector(0.f, 0.f, 20.f), P - FVector(0.f, 0.f, HH + T.WizardBlinkDrop), ECC_Pawn, Q)) continue;   // (no ground: an edge)
		if (Floor.bStartPenetrating || Floor.ImpactNormal.Z < 0.7f) continue;   // (inside something, or too steep to stand on)
		if (Floor.ImpactPoint.Z > Loc.Z - HH + T.WizardBlinkStep) continue;     // (a ledge above him: not climbed by teleport)
		const FVector C = Floor.ImpactPoint + FVector(0.f, 0.f, HH + 2.f);
		if (GetWorld()->OverlapBlockingTestByChannel(C + FVector(0.f, 0.f, 4.f), FQuat::Identity, ECC_Pawn, Body, Q)) continue;   // (no room)
		FHitResult Sight;
		if (GetWorld()->LineTraceSingleByChannel(Sight, Loc + FVector(0.f, 0.f, 30.f), C + FVector(0.f, 0.f, 30.f), ECC_Pawn, Q)) continue;   // (something between: a wall)
		Out = C;
		return true;
	}
	return false;
}

void AKnightCharacter::StartBlink(const FVector& Dir)
{
	if (HasAuthority()) { DoBlink(Dir); return; }
	// online: only the server moves him. This machine asks, and starts the cooldown so a held wheel does not ask again.
	RollCooldown = JeebTune::V().WizardBlinkCooldown;
	ServerBlink(Dir);
}

void AKnightCharacter::DoBlink(const FVector& Dir)
{
	const auto& T = JeebTune::V();
	const FVector From = GetActorLocation();
	Stamina -= 20.f;
	StaminaDelay = 1.0f;
	bCrouched = false;
	bBlocking = false;
	RollCooldown = T.WizardBlinkCooldown;
	FVector To;
	if (!BlinkSpot(Dir, To)) { UE_LOG(LogTemp, Log, TEXT("JEEBBLINK %s: nowhere to go that way"), *GetName()); return; }
	TeleportTo(To, GetActorRotation(), false, true);
	if (UCharacterMovementComponent* M = GetCharacterMovement()) { M->Velocity.Z = 0.f; M->bJustTeleported = true; }
	UE_LOG(LogTemp, Log, TEXT("JEEBBLINK %s from %s to %s: %.0f cm"), *GetName(), *From.ToCompactString(), *To.ToCompactString(), FVector::Dist2D(From, To));
	MultiBlink(From, To);
}

void AKnightCharacter::ServerBlink_Implementation(FVector_NetQuantizeNormal Dir)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!bIsWizard || Health <= 0.f || RollCooldown > 0.25f || Move->IsFalling() || bRolling || bSliding || bSitting || bClimbing || InClimbMove() || bMounted || bKnockedDown || StunLeft > 0.f || MoveLocked() || Smash == ESmash::Charging) return;   // (small timing differences pass)
	if (Stamina < 15.f) return;
	DoBlink(FVector(Dir.X, Dir.Y, 0.f));
}

// Everyone hears where he went: his own machine moves there at once (it never guessed the spot), every machine that draws shows the puffs
void AKnightCharacter::MultiBlink_Implementation(FVector_NetQuantize From, FVector_NetQuantize To)
{
	if (!HasAuthority() && IsLocallyControlled())
	{
		TeleportTo(To, GetActorRotation(), false, true);
		if (UCharacterMovementComponent* M = GetCharacterMovement()) { M->Velocity.Z = 0.f; M->bJustTeleported = true; }
	}
	BlinkFx(From, To);
}

void AKnightCharacter::BlinkFx(const FVector& From, const FVector& To)
{
	if (IsNetMode(NM_DedicatedServer)) return;
	UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Wizard/M_WizardOrb.M_WizardOrb"));
	if (!Ball || !Mat) return;
	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Ball);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetUsingAbsoluteLocation(true); C->SetUsingAbsoluteRotation(true); C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		C->SetWorldLocation(i == 0 ? From : To);
		FBlinkPuff P;
		P.Comp = C; P.Mid = C->CreateDynamicMaterialInstance(0, Mat); P.bIn = i == 1;
		if (P.Mid.IsValid()) P.Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.8f, 1.f));
		BlinkPuffs.Add(P);
	}
	BlinkHide = 0.f;
	UE_LOG(LogTemp, Log, TEXT("JEEBBLINK flashes for %s (mine %d): %.0f cm"), *GetName(), IsLocallyControlled() ? 1 : 0, FVector::Dist2D(From, To));
}

void AKnightCharacter::TickWizard(float Dt)
{
	// (online tests: -WizardNetDo on a client makes its own wizard teleport ahead 8 s after he appears, raise the orb at 10 s and lower it at 13 s,
	// through the same calls as the keys; never in Shipping)
#if !UE_BUILD_SHIPPING
	if (IsLocallyControlled() && !HasAuthority())
	{
		static const bool bNetDo = FParse::Param(FCommandLine::Get(), TEXT("WizardNetDo"));
		static float NetT = 0.f; static int32 NetStep = 0;
		if (bNetDo)
		{
			NetT += Dt;
			if (NetStep == 0 && NetT > 8.f) { NetStep = 1; UE_LOG(LogTemp, Log, TEXT("JEEBBLINK net test: asking, at %s"), *GetActorLocation().ToCompactString()); StartBlink(GetActorForwardVector().GetSafeNormal2D()); }
			else if (NetStep == 1 && NetT > 10.f) { NetStep = 2; UE_LOG(LogTemp, Log, TEXT("JEEBWIZARD net test: block, now at %s"), *GetActorLocation().ToCompactString()); StartBlock(); }
			else if (NetStep == 2 && NetT > 13.f) { NetStep = 3; UE_LOG(LogTemp, Log, TEXT("JEEBWIZARD net test: block off, blocking was %d"), bBlocking ? 1 : 0); StopBlock(); }
		}
	}
#endif
	if (IsNetMode(NM_DedicatedServer)) return;
	const auto& T = JeebTune::V();
	// The orb: grows out of him as the guard comes up, a slow breath while held
	if (BlockAlpha > 0.01f && !WizardOrb)
	{
		UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Wizard/M_WizardOrb.M_WizardOrb"));
		if (Ball && Mat)
		{
			WizardOrb = NewObject<UStaticMeshComponent>(this);
			WizardOrb->SetStaticMesh(Ball);
			WizardOrb->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			WizardOrb->SetCastShadow(false);
			WizardOrb->SetupAttachment(GetRootComponent());
			WizardOrb->RegisterComponent();
			WizardOrbMid = WizardOrb->CreateDynamicMaterialInstance(0, Mat);
			UE_LOG(LogTemp, Log, TEXT("JEEBWIZARD orb drawn for %s (mine %d)"), *GetName(), IsLocallyControlled() ? 1 : 0);
		}
	}
	if (WizardOrb)
	{
		const bool bShow = BlockAlpha > 0.01f;
		if (WizardOrb->IsVisible() != bShow) WizardOrb->SetVisibility(bShow);
		if (bShow)
		{
			const float Breath = 1.f + 0.015f * FMath::Sin(GetWorld()->GetTimeSeconds() * 4.f);
			const float Scale = T.WizardOrbRadius / 50.f * FMath::Lerp(0.6f, 1.f, BlockAlpha) * Breath;   // (the engine's ball is 50 cm in radius)
			WizardOrb->SetRelativeScale3D(FVector(Scale));
			WizardOrb->SetRelativeLocation(FVector(0.f, 0.f, T.WizardOrbUp));
			if (WizardOrbMid)
			{
				WizardOrbMid->SetScalarParameterValue(TEXT("Opacity"), BlockAlpha * T.WizardOrbOpacity);
				WizardOrbMid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.25f, 0.55f, 1.f));
			}
		}
	}
	// The teleport's puffs: a ball of light that folds in where he left and one that opens where he arrives
	for (int32 i = BlinkPuffs.Num() - 1; i >= 0; --i)
	{
		FBlinkPuff& P = BlinkPuffs[i];
		P.T += Dt;
		const float U = FMath::Clamp(P.T / FMath::Max(T.WizardBlinkPuff, 0.05f), 0.f, 1.f);
		UStaticMeshComponent* C = P.Comp.Get();
		if (!C || U >= 1.f) { if (C) C->DestroyComponent(); BlinkPuffs.RemoveAtSwap(i); continue; }
		const float Size = P.bIn ? FMath::Lerp(0.5f, 2.6f, U) : FMath::Lerp(2.4f, 0.2f, U);
		C->SetWorldScale3D(FVector(Size * 0.8f, Size * 0.8f, Size * 1.25f));
		if (UMaterialInstanceDynamic* M = P.Mid.Get()) M->SetScalarParameterValue(TEXT("Opacity"), (1.f - U) * 1.6f);
	}
}

// ---------------- Playing as the wizard (tests) ----------------
// -PlayAs=Wizard on the command line (KnightGameMode, KnightPlayerController). In single player (test builds) the console also swaps:
// jeeb.PlayWizard 1 saves and comes back as the wizard in the same spot, jeeb.PlayWizard 0 as the knight. (As the troll's jeeb.PlayAs.)
void AKnightCharacter::SwapWizardForTest(bool bWizard)
{
#if !UE_BUILD_SHIPPING
	if (GetNetMode() != NM_Standalone || bWizard == bIsWizard) return;
	AKnightPlayerController* PC = Cast<AKnightPlayerController>(GetController());
	AGameModeBase* GM = GetWorld()->GetAuthGameMode();
	if (!PC || !GM) return;
	if (UsesLocalSave()) Autosave();
	PC->ClassChoice = bWizard ? 2 : 0;
	const FTransform At(GetActorRotation(), GetActorLocation());
	// (as the troll's swap: undo everything this pawn put on the player before it goes, or the new one has no input)
	bSettingsOpen = bSkillsOpen = bBankOpen = bInventoryOpen = bShopOpen = bSmithOpen = false;
	bPanelCursor = false;
	if (ULocalPlayer* LP = PC->GetLocalPlayer())
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (KnightContext) Sub->RemoveMappingContext(KnightContext);
			Sub->ClearAllMappings();
		}
	PC->SetShowMouseCursor(false);
	PC->ResetIgnoreLookInput();
	PC->ResetIgnoreMoveInput();
	PC->SetInputMode(FInputModeGameOnly());
	PC->UnPossess();
	Destroy();
	GM->RestartPlayerAtTransform(PC, At);
	UE_LOG(LogTemp, Log, TEXT("JEEBSWAP now %s"), bWizard ? TEXT("wizard") : TEXT("knight"));
#endif
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs CmdPlayWizard(TEXT("jeeb.PlayWizard"), TEXT("Single player, test builds: jeeb.PlayWizard 1 | 0. Saves, then the wizard (1) or the knight (0) appears in the same spot."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;   // (a local test command: the one player)
		if (AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr)
			K->SwapWizardForTest(Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0);
	}));
#endif
