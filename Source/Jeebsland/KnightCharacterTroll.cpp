// The playable troll (2026-11-19, Docs/TrollPlayablePlan.md): the knight's pawn with bIsTroll set. He moves and fights through the
// knight's own code; this file holds only what is his: speeds, body size, the quick hop (in the roll's place), the charged jump.
// The left punch in the kick's place is in KnightAnimatorTroll.cpp (the pose) and TickHitboxes (its hit boxes); his body in
// KnightAnimatorTroll.cpp. Numbers: Content/Data/Tuning.ini [Troll].
#include "KnightCharacter.h"
#include "KnightMovement.h"
#include "Inventory.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "KnightPlayerController.h"
#include "KnightGameMode.h"
#include "TimerManager.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"

ATrollCharacter::ATrollCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsTroll = true;
}

bool JeebItems::OwnerIsTroll(const USkillComponent* S)
{
	const AKnightCharacter* K = S ? Cast<AKnightCharacter>(S->GetOwner()) : nullptr;
	return K && K->IsTroll();
}

float AKnightCharacter::WalkNow() const { return bIsTroll ? JeebTune::V().TrollWalkSpeed : WalkSpeed; }
float AKnightCharacter::SprintNow() const { return bIsTroll ? JeebTune::V().TrollSprintSpeed : SprintSpeed; }

float AKnightCharacter::LeapChargeShare() const
{
	if (LeapChargeFrom < 0.0 || !GetWorld()) return 0.f;
	const auto& T = JeebTune::V();
	return FMath::Clamp(float(GetWorld()->GetTimeSeconds() - LeapChargeFrom - T.TrollLeapTap) / FMath::Max(T.TrollLeapChargeTime, 0.05f), 0.f, 1.f);
}

// His size (BeginPlay, every machine): the capsule from Tuning.ini, raised so his feet stay where the knight's would have been
void AKnightCharacter::ApplyTrollBody()
{
	const auto& T = JeebTune::V();
	UCapsuleComponent* Cap = GetCapsuleComponent();
	const float Was = Cap->GetUnscaledCapsuleHalfHeight();
	Cap->SetCapsuleSize(T.TrollCapsuleRadius, T.TrollCapsuleHalfHeight);
	if (HasAuthority()) AddActorWorldOffset(FVector(0.f, 0.f, T.TrollCapsuleHalfHeight - Was));
	if (USpringArmComponent* Boom = GetCameraBoom())
	{
		Boom->TargetArmLength = T.TrollBoomLength;
		Boom->SocketOffset = FVector(0.f, 0.f, T.TrollBoomUp);
	}
	GetCharacterMovement()->MaxWalkSpeed = WalkNow();
	UE_LOG(LogTemp, Log, TEXT("JEEBTROLL %s is a troll: capsule %.0f x %.0f, walk %.0f, sprint %.0f"), *GetName(), T.TrollCapsuleRadius, T.TrollCapsuleHalfHeight, WalkNow(), SprintNow());
}

// A launched hop or leap keeps its speed across the ground until it lands: no air braking (on every machine alike, so the server agrees)
void AKnightCharacter::FreeFlight(bool bOn)
{
	UCharacterMovementComponent* M = GetCharacterMovement();
	if (bOn && !bFreeFlight) { SavedFallBrake = M->BrakingDecelerationFalling; SavedFallFriction = M->FallingLateralFriction; UE_LOG(LogTemp, Log, TEXT("JEEBLEAP air braking was %.0f, friction %.2f"), SavedFallBrake, SavedFallFriction); M->BrakingDecelerationFalling = 0.f; M->FallingLateralFriction = 0.f; }
	else if (!bOn && bFreeFlight) { M->BrakingDecelerationFalling = SavedFallBrake; M->FallingLateralFriction = SavedFallFriction; }
	bFreeFlight = bOn;
}

// ---------------- The quick hop (the roll's place: wheel down + a direction, same cooldown) ----------------

void AKnightCharacter::StartHop(const FVector& Dir)
{
	DoHop(Dir);
	if (IsLocallyControlled() && !HasAuthority()) ServerHop(Dir);   // online: the server hops us too
}

void AKnightCharacter::DoHop(const FVector& Dir)
{
	const auto& T = JeebTune::V();
	const FVector D = Dir.GetSafeNormal2D();
	Stamina -= 20.f;
	StaminaDelay = 1.0f;
	bCrouched = false;
	bBlocking = false;
	RollCooldown = T.TrollHopCooldown;
	bAirLaunch = true;
	FreeFlight(true);
	LaunchCharacter(D * T.TrollHopSpeed + FVector(0.f, 0.f, T.TrollHopUp), true, true);
	Kick(GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), 1.2f);
	LeapFrom = GetActorLocation(); LeapAt = GetWorld()->GetTimeSeconds(); LeapShare = -1.f;
}

void AKnightCharacter::ServerHop_Implementation(FVector_NetQuantizeNormal Dir)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!bIsTroll || RollCooldown > 0.25f || Move->IsFalling() || bSliding || bSitting || bClimbing || InClimbMove() || bMounted || MoveLocked() || Smash == ESmash::Charging) return;   // (small timing differences pass)
	Stamina = FMath::Max(Stamina, 20.f);
	DoHop(FVector(Dir.X, Dir.Y, 0.f));
}

// ---------------- The charged jump (hold Space, let go) ----------------

FVector AKnightCharacter::LeapVelocity(float Charge, const FVector& Dir) const
{
	const auto& T = JeebTune::V();
	const float C = FMath::Clamp(Charge, 0.f, 1.f);
	const float Dist = FMath::Lerp(T.TrollLeapMinDist, T.TrollLeapFullDist, C);
	const float High = FMath::Lerp(T.TrollLeapMinHeight, T.TrollLeapFullHeight, C);
	const float G = FMath::Max(1.f, -GetCharacterMovement()->GetGravityZ());
	const float Up = FMath::Sqrt(2.f * G * High);
	const float Flight = 2.f * Up / G;   // (up and back down to the same height)
	return Dir.GetSafeNormal2D() * (Dist / Flight) + FVector(0.f, 0.f, Up);
}

void AKnightCharacter::DoLeap(float Charge, const FVector& Dir)
{
	LeapCooldown = JeebTune::V().TrollLeapCooldown;
	LeapCrouch = 0.f;   // (straight out of the crouch: no crouched frames into the flight)
	bAirLaunch = true;
	bCrouched = false;
	bBlocking = false;
	FreeFlight(true);
	LaunchCharacter(LeapVelocity(Charge, Dir), true, true);
	Kick(GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), 2.f);
	LeapFrom = GetActorLocation(); LeapAt = GetWorld()->GetTimeSeconds(); LeapShare = Charge;
	UE_LOG(LogTemp, Log, TEXT("JEEBLEAP %s launch charge %.2f vel %s"), HasAuthority() ? TEXT("server") : TEXT("client"), Charge, *GetCharacterMovement()->PendingLaunchVelocity.ToCompactString());
}

// Space let go: a short press is the normal jump; a longer one (and no cooldown left) the charged jump, the way he is steering (else ahead)
void AKnightCharacter::ReleaseLeap()
{
	const auto& T = JeebTune::V();
	const double Now = GetWorld()->GetTimeSeconds();
	const float Held = float(Now - LeapChargeFrom);
	const float Charge = LeapChargeShare();
	LeapChargeFrom = -1.0;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const bool bCan = Held >= T.TrollLeapTap && LeapCooldown <= 0.f && Move->IsMovingOnGround() && !bKnockedDown && !MoveLocked() && Smash != ESmash::Charging && !bSitting && !bClimbing && !InClimbMove();
	if (!bCan)
	{
		if (!HasAuthority()) ServerLeapCharge(false);
		if (Held >= T.TrollLeapTap && LeapCooldown > 0.f && Skills) Skills->Say(1.5f, FColor(200, 220, 255), FString::Printf(TEXT("Charged jump ready in %.0f s"), FMath::CeilToFloat(LeapCooldown)));
		ACharacter::Jump();   // (the normal jump: the engine's, through CheckJumpInput as ever)
		return;
	}
	FVector Dir = Move->GetCurrentAcceleration().GetSafeNormal2D();
	if (Dir.IsNearlyZero()) Dir = GetActorForwardVector().GetSafeNormal2D();
	DoLeap(Charge, Dir);
	if (!HasAuthority()) ServerLeap(Charge, Dir);
}

void AKnightCharacter::ServerLeapCharge_Implementation(bool bStart)
{
	ServerChargeFrom = bStart ? GetWorld()->GetTimeSeconds() : -1.0;
}

// (server) The client has let go of a charged jump: checked here, then launched here too. The charge can't be more than the server
// saw held (from ServerLeapCharge, with a little slack for the network); the cooldown is the server's.
void AKnightCharacter::ServerLeap_Implementation(float Charge, FVector_NetQuantizeNormal Dir)
{
	const auto& T = JeebTune::V();
	const double Now = GetWorld()->GetTimeSeconds();
	const float SawHeld = ServerChargeFrom >= 0.0 ? float(Now - ServerChargeFrom) + 0.15f : 0.f;
	ServerChargeFrom = -1.0;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!bIsTroll || LeapCooldown > 0.25f || Move->IsFalling() || bKnockedDown || MoveLocked() || bSitting || bClimbing || InClimbMove() || bMounted || SawHeld < T.TrollLeapTap)
	{
		UE_LOG(LogTemp, Log, TEXT("JEEBLEAP server refused: cooldown %.2f falling %d held %.2f"), LeapCooldown, Move->IsFalling() ? 1 : 0, SawHeld);
		ClientLeapRefused(LeapCooldown);
		return;
	}
	const float MaxCharge = FMath::Clamp((SawHeld - T.TrollLeapTap) / FMath::Max(T.TrollLeapChargeTime, 0.05f), 0.f, 1.f);
	DoLeap(FMath::Clamp(Charge, 0.f, MaxCharge), FVector(Dir.X, Dir.Y, 0.f));
}

void AKnightCharacter::ClientLeapRefused_Implementation(float CooldownLeft)
{
	LeapCooldown = CooldownLeft;
}

void AKnightCharacter::TickTroll(float Dt)
{
	LeapCooldown = FMath::Max(0.f, LeapCooldown - Dt);
	// (a charge ends if he leaves the ground or is stopped: no jump comes of it)
	if (LeapChargeFrom >= 0.0 && (GetCharacterMovement()->IsFalling() || bKnockedDown || bClimbing || InClimbMove() || bSitting || bMounted))
	{
		LeapChargeFrom = -1.0;
		if (!HasAuthority()) ServerLeapCharge(false);
	}
	// (online tests: -TrollNetDo=<a -SuitDo name> runs it on a client's own troll 8 s after he appears, through the same RPCs as the keys; never in Shipping)
#if !UE_BUILD_SHIPPING
	if (IsLocallyControlled() && !HasAuthority() && !bNetDoDone && (NetDoT += Dt) > 8.f)
	{
		bNetDoDone = true;
		FString NetDo;
		if (FParse::Value(FCommandLine::Get(), TEXT("TrollNetDo="), NetDo)) { UE_LOG(LogTemp, Log, TEXT("JEEBLEAP net test %s"), *NetDo); TrollSuitTest(NetDo); }
	}
#endif
	// the crouch as it charges (this machine only)
	if (IsLocallyControlled()) LeapCrouch = FMath::FInterpTo(LeapCrouch, IsLeapCharging() ? 0.25f + 0.75f * LeapChargeShare() : 0.f, Dt, 12.f);
}

void AKnightCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (!bAirLaunch) return;
	bAirLaunch = false;
	FreeFlight(false);
	if (LeapAt > 0.0)
	{
		UE_LOG(LogTemp, Log, TEXT("JEEBLEAP %s %s landed: %.0f cm across, %.0f up/down, %.2f s, charge %.2f (-1 a hop)"), HasAuthority() ? TEXT("server") : TEXT("client"), *GetName(),
			FVector::Dist2D(LeapFrom, GetActorLocation()), GetActorLocation().Z - LeapFrom.Z, GetWorld()->GetTimeSeconds() - LeapAt, LeapShare);
		LeapAt = 0.0;
	}
}

// ---------------- Playing as the troll (tests) ----------------
// -PlayAs=Troll on the command line (KnightGameMode, KnightPlayerController). In single player (test builds), the console also swaps:
// jeeb.PlayAs troll / jeeb.PlayAs knight saves, then the other one appears in the same spot.
void AKnightCharacter::SwapClassForTest(bool bTroll)
{
#if !UE_BUILD_SHIPPING
	if (GetNetMode() != NM_Standalone) return;
	SwapToClass(bTroll ? 1 : 0);
#endif
}

// (2026-11-21, owner: knight -> troll -> knight left him frozen: no walking, looking or Tab.) Undo everything this pawn put on the
// player before it goes: its input mapping context (the subsystem holds it strongly, so a dead pawn's context stayed applied with
// the same keys and ate them), the open screen's mouse cursor, look-ignore and UI input mode. Only on the player's own machine.
void AKnightCharacter::ReleaseInputForSwap()
{
	bSettingsOpen = bSkillsOpen = bBankOpen = bInventoryOpen = bShopOpen = bSmithOpen = false;
	bPanelCursor = false;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController()) return;
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
}

// The player's machine: let go of this pawn's input and ask the server (AKnightPlayerController::ServerTestSwapClass). If the
// server says no, the input comes back after 3 s, so a refused swap never leaves the player frozen.
void AKnightCharacter::RequestClass(int32 Class)
{
	if (Class == ClassIndex()) return;
	AKnightPlayerController* PC = Cast<AKnightPlayerController>(GetController());
	if (!PC || !PC->IsLocalController()) return;
	if (HasAuthority()) { PC->ServerTestSwapClass(Class); return; }   // (single player: runs here at once and cleans up itself)
	ReleaseInputForSwap();
	PC->ServerTestSwapClass(Class);
	TWeakObjectPtr<AKnightCharacter> W(this);
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([W]()
	{
		AKnightCharacter* K = W.Get();
		APlayerController* P = K ? Cast<APlayerController>(K->GetController()) : nullptr;
		if (!P || !P->IsLocalController()) return;
		if (ULocalPlayer* LP = P->GetLocalPlayer())
			if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
				if (K->KnightContext && !Sub->HasMappingContext(K->KnightContext)) Sub->AddMappingContext(K->KnightContext, 0);
		UE_LOG(LogTemp, Warning, TEXT("JEEBSWAP refused or late: input given back to %s"), *K->GetName());
	}), 3.f, false);
}

// The server: save, then the new class appears in the same spot (KnightGameMode::SwapPawnClass).
void AKnightCharacter::SwapToClass(int32 Class)
{
	Class = FMath::Clamp(Class, 0, 2);
	if (!HasAuthority() || Class == ClassIndex()) return;
	AKnightPlayerController* PC = Cast<AKnightPlayerController>(GetController());
	AKnightGameMode* GM = GetWorld()->GetAuthGameMode<AKnightGameMode>();
	if (!PC || !GM) return;
	if (UsesLocalSave()) Autosave();
	const FTransform At(GetActorRotation(), GetActorLocation() + FVector(0.f, 0.f, Class == 1 ? 30.f : 0.f));
	ReleaseInputForSwap();   // (this machine's own player only: single player)
	GM->SwapPawnClass(PC, Class, At);   // (destroys this pawn: nothing below may use this)
	AKnightCharacter* NewK = Cast<AKnightCharacter>(PC->GetPawn());
	UE_LOG(LogTemp, Log, TEXT("JEEBSWAP now %s: possessed %s, look ignored %d, move ignored %d, cursor %d"), Class == 1 ? TEXT("troll") : Class == 2 ? TEXT("wizard") : TEXT("knight"),
		NewK ? *NewK->GetName() : TEXT("NOTHING"), PC->IsLookInputIgnored() ? 1 : 0, PC->IsMoveInputIgnored() ? 1 : 0, PC->ShouldShowMouseCursor() ? 1 : 0);
}

// -SwapTest (test builds, single player or a client): knight -> troll -> wizard -> knight, six swaps 4 s apart, each asked the way the
// buttons ask; each new pawn logs (on its own machine) whether it has input and its context. Called from BeginPlay of a locally
// controlled pawn (PawnClientRestart).
void AKnightCharacter::SwapTestStep()
{
#if !UE_BUILD_SHIPPING
	static int32 SwapsLeft = FParse::Param(FCommandLine::Get(), TEXT("SwapTest")) ? 7 : 0;
	if (SwapsLeft <= 0) return;
	TWeakObjectPtr<AKnightCharacter> W(this);
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([W]()
	{
		AKnightCharacter* K = W.Get();
		if (!K || SwapsLeft <= 0) return;
		APlayerController* P = Cast<APlayerController>(K->GetController());
		if (!P || !P->IsLocalController()) return;
		const UEnhancedInputLocalPlayerSubsystem* S = P->GetLocalPlayer() ? P->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		UE_LOG(LogTemp, Log, TEXT("JEEBSWAP check %s (%s): input component %d, its context applied %d, look ignored %d, move ignored %d"),
			K->ClassIndex() == 1 ? TEXT("troll") : K->ClassIndex() == 2 ? TEXT("wizard") : TEXT("knight"), K->HasAuthority() ? TEXT("server") : TEXT("client"),
			K->InputComponent ? 1 : 0, S && K->KnightContext ? (S->HasMappingContext(K->KnightContext) ? 1 : 0) : 0, P->IsLookInputIgnored() ? 1 : 0, P->IsMoveInputIgnored() ? 1 : 0);
		if (--SwapsLeft > 0) K->RequestClass((K->ClassIndex() + 1) % 3);
	}), 5.f, false);
#endif
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs CmdPlayAs(TEXT("jeeb.PlayAs"), TEXT("Single player, test builds: jeeb.PlayAs troll | knight. Saves, then the other appears in the same spot."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;   // (a local test command: the one player)
		if (AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr)
			K->SwapClassForTest(Args.Num() > 0 && Args[0].Equals(TEXT("troll"), ESearchCase::IgnoreCase));
	}));
#endif

// Test pictures (-JeebShot, single player): -SuitDo=punch (the left punch), hop<deg> (a hop that way from where he faces: hop90 his right),
// block (the bare-arm guard, held), hopall (eight hops, every 45 degrees, 1.4 s apart), chargejump<hundredths> (Space held for that share of a full charge, then let go),
// leapsizes (least, half and full charge, 3 s apart, past the cooldown), leaptest (a full charge; again at 4 s: the cooldown turns it into
// a tap jump; again at 10 s at half charge: it leaps). Logs JEEBLEAP.
bool AKnightCharacter::TrollSuitTest(const FString& Do)
{
	if (!bIsTroll) return false;
	const auto& T = JeebTune::V();
	auto Later = [this](float At, TFunction<void()> F)
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, MoveTemp(F)), FMath::Max(At, 0.01f), false);
	};
	auto Hold = [this, &T, Later](float Share, float At)
	{
		Later(At, [this]() { LeapChargeFrom = ServerChargeFrom = GetWorld()->GetTimeSeconds(); if (!HasAuthority()) ServerLeapCharge(true); UE_LOG(LogTemp, Log, TEXT("JEEBLEAP test: Space down")); });
		Later(At + T.TrollLeapTap + Share * T.TrollLeapChargeTime + 0.02f, [this]() { UE_LOG(LogTemp, Log, TEXT("JEEBLEAP test: Space up, charge %.2f, cooldown %.1f"), LeapChargeShare(), LeapCooldown); ReleaseLeap(); });
	};
	if (Do == TEXT("punch")) { StartAttack(EAtkStyle::Kick, 1, 0.f); return true; }
	if (Do == TEXT("block")) { Later(0.3f, [this]() { StartBlock(); UE_LOG(LogTemp, Log, TEXT("JEEBTROLLBLOCK raised: blocking %d guard %.0f of %.0f"), bBlocking ? 1 : 0, GuardPool, GuardMax); }); return true; }   // (the bare-arm guard, held)
	if (Do == TEXT("hopall"))
	{
		for (int32 i = 0; i < 8; ++i)
			Later(0.2f + 1.4f * i, [this, i]() { UE_LOG(LogTemp, Log, TEXT("JEEBLEAP test: hop %d deg"), i * 45); StartHop(FRotator(0.f, GetActorRotation().Yaw + 45.f * i, 0.f).Vector()); });
		return true;
	}
	if (Do == TEXT("hop") || (Do.StartsWith(TEXT("hop")) && Do.Len() > 3 && (FChar::IsDigit(Do[3]) || Do[3] == TEXT('-'))))
	{
		StartHop(FRotator(0.f, GetActorRotation().Yaw + FCString::Atof(*Do.Mid(3)), 0.f).Vector());
		return true;
	}
	if (Do.StartsWith(TEXT("chargejump"))) { Hold(Do.Len() > 10 ? FCString::Atoi(*Do.Mid(10)) / 100.f : 1.f, 0.3f); return true; }
	if (Do == TEXT("leapsizes"))
	{
		const float Shares[3] = { 0.f, 0.5f, 1.f };
		for (int32 i = 0; i < 3; ++i) Later(0.3f + 3.f * i, [this, S = Shares[i]]() { LeapCooldown = 0.f; DoLeap(S, GetActorForwardVector()); });
		return true;
	}
	if (Do == TEXT("leaptest")) { Hold(1.f, 0.3f); Hold(1.f, 4.f); Hold(0.5f, 10.f); return true; }
	return false;
}
