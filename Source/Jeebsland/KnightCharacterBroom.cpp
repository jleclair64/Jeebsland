// The wizard's flying broom (2026-11-22, owner: "a flying broom for the wizard ... as fast as the horse ... flies up to 80 m above the ground,
// drains magic while flying ... T calls the broom ... when magic runs out it slows to a halt, then drifts straight down").
//
// ONLINE: the server owns the ride. T on the player's machine asks (ServerToggleBroom); the server checks he is a wizard with a broom in his pack
// and magic to spare, then sets bOnBroom (replicated to everyone) and puts his movement into flight; the owner's machine and everyone else's
// follow from OnRep_Broom. The flight itself is UKnightMovement::PhysFlying below, run inside each saved move on both the owner's machine and
// the server, so they agree: WASD steers on the level, Space (bWantsRise) climbs, Ctrl (bWantsLetGo) sinks, never above BroomCeiling metres over
// the ground. The server drains the magic bar (Brooms.csv Drain); at 0 it sets bBroomEmpty, the broom slows to a halt and drifts down at
// BroomDrift, and touching the ground ends the ride. The broom itself is items 930+ (Brooms.csv); Silver Point's stall sells them (Broom.cpp).
// Also here: the bar's size by Magic level (MagicCap), the per-item spell costs and the flying cost (SpellCostMul), and the orb guard's drain.
#include "KnightCharacter.h"
#include "KnightMovement.h"
#include "Broom.h"
#include "Inventory.h"
#include "SkillComponent.h"
#include "QuestComponent.h"
#include "QuestPeople.h"
#include "Npc.h"
#include "Messages.h"
#include "Terrain.h"
#include "JeebTuning.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

static TAutoConsoleVariable<float> CVarBroomFwd(TEXT("jeeb.Broom.Forward"), 6.f, TEXT("The broom under the wizard: cm forward of his hips (looks only)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarBroomPitch(TEXT("jeeb.Broom.Pitch"), 6.f, TEXT("The broom under the wizard: degrees its tip is raised (looks only)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarBroomScale(TEXT("jeeb.Broom.Scale"), 1.f, TEXT("The broom under the wizard: its size (looks only; the model is 175 cm long)."), ECVF_Default);

// ---------------------------------------------------------------- the magic bar, costs and the orb's drain
float AKnightCharacter::MagicCap() const
{
	const auto& T = JeebTune::V();
	const int32 Level = Skills ? Skills->GetLevel(ESkillType::Magic) : 1;
	return FMath::Max(1.f, T.MagicMax + T.MagicPerLevel * FMath::Max(0, Level - 1));
}

float AKnightCharacter::SpellCostMul(int32 Kind, int32 Tier) const
{
	const TCHAR* Item = Kind == JeebItems::K_Wand ? TEXT("Wand") : Kind == JeebItems::K_Staff ? TEXT("Staff") : nullptr;
	float Mul = Item ? JeebBroom::CastMul(Item, Tier) : 1.f;
	if (bOnBroom) Mul *= FMath::Max(0.f, JeebTune::V().BroomSpellCostMul);
	return Mul;
}

float AKnightCharacter::WizardGuardDrain() const
{
	const int32 Off = Skills ? Skills->Worn[JeebItems::K_Book] : 0;
	const int32 Style = Off ? JeebItems::StyleOf(Off) : JeebItems::WO_Book;
	const TCHAR* Item = Style == JeebItems::WO_Ball ? TEXT("Ball") : Style == JeebItems::WO_Pipe ? TEXT("Pipe") : TEXT("Book");
	return JeebBroom::GuardDrain(Item, Off ? JeebItems::TierOf(Off) : JeebItems::T_Tin);
}

// ---------------------------------------------------------------- the stall
bool AKnightCharacter::NearBroomStall() const
{
	return FVector::Dist2D(GetActorLocation(), JeebBroom::StallSpot()) < JeebBroom::StallReach && FMath::Abs(GetActorLocation().Z - JeebBroom::StallSpot().Z) < 400.f;
}

void AKnightCharacter::OpenBroomShop()
{
	OpenShop();
	bBroomShop = bShopOpen;
}

// ---------------------------------------------------------------- calling and leaving the broom
int32 AKnightCharacter::BestBroomRow() const
{
	if (!Skills) return -1;
	int32 Best = -1;
	for (int32 b = 0; b < JeebBroom::NumBrooms(); ++b)
		if (Skills->CountItem(JeebItems::BroomId(b)) > 0 && (Best < 0 || JeebBroom::Row(b).SpeedMul >= JeebBroom::Row(Best).SpeedMul)) Best = b;
	return Best;
}

void AKnightCharacter::ToggleBroom()
{
	if (!bIsWizard || IsUIOpen()) return;
	if (!bOnBroom && BestBroomRow() < 0)
	{
		JeebMsg::Post(3.5f, FColor::Yellow, TEXT("You have no broom. Hazel Fernwick in Oak Hill makes them, and Silver Point's stall sells them."));
		return;
	}
	ServerToggleBroom();
}

void AKnightCharacter::ServerToggleBroom_Implementation()
{
	if (!bIsWizard || !Skills) return;
	const auto& T = JeebTune::V();
	const float Above = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - JeebTerrain::HeightAt(GetActorLocation().X, GetActorLocation().Y);
	if (bOnBroom)
	{
		if (Above <= T.BroomStepOff * 100.f || GetCharacterMovement()->IsMovingOnGround()) EndBroomSrv(TEXT("stepped off"));
		else Skills->Say(2.5f, FColor::Yellow, FString::Printf(TEXT("Fly lower to step off (within %.0f m of the ground)."), T.BroomStepOff));
		return;
	}
	const int32 Row = BestBroomRow();
	if (Row < 0) { Skills->Say(3.f, FColor::Yellow, TEXT("You have no broom.")); return; }
	UKnightMovement* KM = KnightMove();
	if (bMounted || bSitting || bClimbing || InClimbMove() || bKnockedDown || Health <= 0.f || (KM && KM->InWater())) { Skills->Say(2.5f, FColor::Yellow, TEXT("You can't call your broom now.")); return; }
	if (Magic < T.BroomMinMagic) { Skills->Say(2.5f, FColor::Yellow, TEXT("Not enough magic to fly. Let it fill a little.")); return; }
	StartBroomSrv(Row);
}

void AKnightCharacter::StartBroomSrv(int32 Row)
{
	bOnBroom = true;
	bBroomEmpty = false;
	BroomRow = uint8(FMath::Clamp(Row, 0, 255));
	MagicUsedAt = GetWorld()->GetTimeSeconds();
	ApplyBroomMovement();
	UpdateBroomModel();
	GetCharacterMovement()->Velocity.Z = 350.f;   // (a little hop up as he takes off)
	ForceNetUpdate();
	if (Skills) Skills->Say(3.f, FColor(190, 140, 255), FString::Printf(TEXT("%s. Space climbs, Ctrl sinks, T near the ground steps off."), *JeebItems::ItemName(JeebItems::BroomId(Row))));
	UE_LOG(LogTemp, Log, TEXT("JEEBBROOM %s takes off on %s at %s, magic %.0f of %.0f"), *GetName(), *JeebItems::ItemName(JeebItems::BroomId(Row)), *GetActorLocation().ToCompactString(), Magic, MagicCap());
}

void AKnightCharacter::EndBroomSrv(const TCHAR* Why)
{
	if (!bOnBroom) return;
	bOnBroom = false;
	bBroomEmpty = false;
	ApplyBroomMovement();
	UpdateBroomModel();
	ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("JEEBBROOM %s off the broom (%s) at %s, magic %.0f"), *GetName(), Why, *GetActorLocation().ToCompactString(), Magic);
}

void AKnightCharacter::OnRep_Broom()
{
	ApplyBroomMovement();
	UpdateBroomModel();
	UE_LOG(LogTemp, Log, TEXT("JEEBBROOMSEE %s (mine %d): on %d, empty %d, broom %d at %s"), *GetName(), IsLocallyControlled() ? 1 : 0, bOnBroom ? 1 : 0, bBroomEmpty ? 1 : 0, int32(BroomRow), *GetActorLocation().ToCompactString());
}

// Puts this machine's movement into flight or out of it (the server, the owner, and everyone else for a smooth look)
void AKnightCharacter::ApplyBroomMovement()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move) return;
	if (bOnBroom)
	{
		Move->MaxFlySpeed = JeebTune::V().BroomSpeed * JeebBroom::Row(BroomRow).SpeedMul;
		Move->BrakingDecelerationFlying = JeebTune::V().BroomAccel;
		if (!bBroomApplied)
		{
			bBroomApplied = true;
			bCrouched = false; bSprinting = false; bAttacking = false;
			if (Move->MovementMode != MOVE_Flying && (HasAuthority() || IsLocallyControlled())) Move->SetMovementMode(MOVE_Flying);
		}
	}
	else if (bBroomApplied)
	{
		bBroomApplied = false;
		if (Move->MovementMode == MOVE_Flying && (HasAuthority() || IsLocallyControlled())) Move->SetMovementMode(MOVE_Falling);
	}
}

// ---------------------------------------------------------------- every frame
void AKnightCharacter::TickBroom(float Dt)
{
	const auto& T = JeebTune::V();
	UCharacterMovementComponent* Move = GetCharacterMovement();
#if !UE_BUILD_SHIPPING
	static const bool bNetTest = FParse::Param(FCommandLine::Get(), TEXT("BroomNetTest"));
	static const bool bNetDo = FParse::Param(FCommandLine::Get(), TEXT("BroomNetDo"));
#else
	static const bool bNetTest = false, bNetDo = false;
#endif
	if (HasAuthority())
	{
		if (bOnBroom)
		{
			if (Move && Move->MovementMode != MOVE_Flying) EndBroomSrv(Move->IsMovingOnGround() ? TEXT("landed") : TEXT("left the air"));
			else if (Health <= 0.f || bKnockedDown || !bIsWizard) EndBroomSrv(TEXT("knocked off"));
			else
			{
				const float Drain = JeebBroom::Row(BroomRow).Drain;
				Magic = FMath::Max(0.f, Magic - Drain * Dt);
				MagicUsedAt = GetWorld()->GetTimeSeconds();   // (no refilling while flying)
				if (Magic <= 0.f && !bBroomEmpty)
				{
					bBroomEmpty = true;
					ForceNetUpdate();
					if (Skills) Skills->Say(3.f, FColor(190, 140, 255), TEXT("Your magic is spent. The broom slows and sinks..."));
					UE_LOG(LogTemp, Log, TEXT("JEEBBROOM %s out of magic at %s (%.0f m above the ground)"), *GetName(), *GetActorLocation().ToCompactString(),
						(GetActorLocation().Z - JeebTerrain::HeightAt(GetActorLocation().X, GetActorLocation().Y)) / 100.f);
				}
			}
		}
		// The orb guard drains magic while held (MagicCosts.csv); with none left it drops (GuardActive keeps it down until BroomMinMagic is back)
		if (bIsWizard && bBlocking)
		{
			Magic = FMath::Max(0.f, Magic - WizardGuardDrain() * Dt);
			MagicUsedAt = GetWorld()->GetTimeSeconds();
			if (Magic <= 0.f) bBlocking = false;
		}
	}
	else if (bIsWizard && bBlocking && Magic <= 0.f && IsLocallyControlled()) bBlocking = false;
	if (bOnBroom) ApplyBroomMovement();   // (live tuning: the speed)
	UpdateBroomModel();

#if !UE_BUILD_SHIPPING
	// Online test (-BroomNetTest on the server, -BroomNetDo on wizard clients; Tools/broom_net.sh). The first wizard to arrive flies: the server
	// gives him a broom, his machine calls it, climbs to the ceiling, then flies on; the server then sets his magic low so it runs out, and he
	// drifts down and lands. The second wizard is put at Silver Point's stall with ingots, buys an Ashwood Broom through the stall, then the
	// server plays Hazel's quest for him (accept, logs, hides) and logs the reward.
	static TArray<TWeakObjectPtr<AKnightCharacter>> Order;
	if ((bNetTest && HasAuthority() && bIsWizard) || (bNetDo && IsLocallyControlled() && !HasAuthority()))
	{
		BroomTestT += Dt;
		int32 TestRole = 0;
		if (HasAuthority()) { Order.RemoveAll([](const TWeakObjectPtr<AKnightCharacter>& W) { return !W.IsValid(); }); TestRole = Order.AddUnique(this); }
		const float Above = (GetActorLocation().Z - JeebTerrain::HeightAt(GetActorLocation().X, GetActorLocation().Y)) / 100.f;
		const int32 Tick2 = int32(BroomTestT / 2.f);
		if (HasAuthority() && Skills)
		{
			if (TestRole == 0)
			{
				if (BroomTestStep == 0 && BroomTestT > 2.f) { BroomTestStep = 1; Skills->AddInvItem(JeebItems::BroomId(0), 1); UE_LOG(LogTemp, Log, TEXT("JEEBBROOMTEST flier %s given %s"), *GetName(), *JeebItems::ItemName(JeebItems::BroomId(0))); }
				if (BroomTestStep == 1 && bOnBroom && Above > T.BroomCeiling - 1.5f) { BroomTestStep = 2; UE_LOG(LogTemp, Log, TEXT("JEEBBROOMTEST PASS reached the ceiling: %.1f m above the ground"), Above); }
				// (once the other wizard has been in the world 22 s, so his machine has seen the flight: the magic set low so it runs out)
				if (BroomTestStep == 2) Magic = FMath::Max(Magic, 30.f);   // (kept flying while he waits)
				if (BroomTestStep == 2 && Order.ContainsByPredicate([this](const TWeakObjectPtr<AKnightCharacter>& W) { return W.Get() && W.Get() != this && W->BroomTestT > 22.f; })) { BroomTestStep = 4; Magic = FMath::Min(Magic, 6.f); UE_LOG(LogTemp, Log, TEXT("JEEBBROOMTEST flier magic set to %.0f so it runs out, %.1f m up, speed %.0f"), Magic, Above, Move ? Move->Velocity.Size2D() : 0.f); }
				if (BroomTestStep == 4 && !bOnBroom) { BroomTestStep = 5; UE_LOG(LogTemp, Log, TEXT("JEEBBROOMTEST PASS landed and stepped off at %s, %.1f m above the ground, falling %.0f cm/s"), *GetActorLocation().ToCompactString(), Above, Move ? Move->Velocity.Z : 0.f); }
				if (bOnBroom && Tick2 != int32((BroomTestT - Dt) / 2.f))
					UE_LOG(LogTemp, Log, TEXT("JEEBBROOMTEST flier t %.0f: %.1f m up, speed %.0f across %.0f up, magic %.1f, empty %d"), BroomTestT, Above, Move ? Move->Velocity.Size2D() : 0.f, Move ? Move->Velocity.Z : 0.f, Magic, bBroomEmpty ? 1 : 0);
			}
			else if (TestRole == 1)
			{
				if (BroomTestStep == 0 && BroomTestT > 4.f)
				{
					BroomTestStep = 1;
					Skills->AddIngots(1500);
					const FVector S = JeebBroom::StallSpot();
					const FVector2D To = FVector2D(S) + FVector2D(FMath::Cos(FMath::DegreesToRadians(JeebBroom::StallYaw())), FMath::Sin(FMath::DegreesToRadians(JeebBroom::StallYaw()))) * 200.f;
					TeleportTo(FVector(To.X, To.Y, JeebTerrain::HeightAt(To.X, To.Y) + 120.f), GetActorRotation(), false, true);
					UE_LOG(LogTemp, Log, TEXT("JEEBBROOMTEST shopper %s at the stall with %d ingots"), *GetName(), Skills->GetIngots());
				}
				if (BroomTestStep == 1 && BroomTestT > 28.f)
				{
					BroomTestStep = 2;
					UE_LOG(LogTemp, Log, TEXT("JEEBBROOMTEST %s shopper has %d Ashwood Broom, %d ingots"), Skills->CountItem(JeebItems::BroomId(2)) > 0 ? TEXT("PASS") : TEXT("FAIL"), Skills->CountItem(JeebItems::BroomId(2)), Skills->GetIngots());
					UQuestComponent* Q = FindComponentByClass<UQuestComponent>();
					const ANpc* Hazel = AQuestPeople::Find(GetWorld(), TEXT("Hazel"));
					if (Q && Hazel)
					{
						TeleportTo(Hazel->GetActorLocation() + Hazel->GetActorForwardVector() * 150.f + FVector(0.f, 0.f, 20.f), GetActorRotation(), false, true);
						UE_LOG(LogTemp, Log, TEXT("JEEBBROOMTEST shopper beside Hazel"));
					}
					else UE_LOG(LogTemp, Log, TEXT("JEEBBROOMTEST FAIL no quest component (%d) or no Hazel (%d)"), Q ? 1 : 0, Hazel ? 1 : 0);
				}
				UQuestComponent* QC = FindComponentByClass<UQuestComponent>();   // (one request a step: the server ignores asks closer than 0.2 s)
				if (QC && BroomTestStep == 2 && BroomTestT > 29.f) { BroomTestStep = 3; QC->ServerQuest(uint8(UQuestComponent::EOp::Accept), TEXT("Broom")); }
				if (QC && BroomTestStep == 3 && BroomTestT > 30.f) { BroomTestStep = 4; Skills->AddItem(EItemType::Logs, 4); QC->ServerQuest(uint8(UQuestComponent::EOp::Talk), TEXT("Hazel")); }
				if (BroomTestStep == 4 && BroomTestT > 31.f)
				{
					BroomTestStep = 5;
					if (UQuestComponent* Q = QC)
					{
						Skills->AddItem(EItemType::Hide, 2);
						Q->ServerQuest(uint8(UQuestComponent::EOp::Talk), TEXT("Hazel"));
						UE_LOG(LogTemp, Log, TEXT("JEEBBROOMTEST %s quest reward: %d Hazel's Old Broom, logs left %d, hides left %d, Magic xp %d"), Skills->CountItem(JeebItems::BroomId(0)) > 0 ? TEXT("PASS") : TEXT("FAIL"),
							Skills->CountItem(JeebItems::BroomId(0)), Skills->GetItemCount(EItemType::Logs), Skills->GetItemCount(EItemType::Hide), Skills->GetXPTotal(ESkillType::Magic));
					}
				}
			}
		}
		else if (!HasAuthority())
		{
			// The player's own machine: the flier presses T at 7 s and holds Space until the ceiling, then flies ahead; the shopper buys at 10 s
			UKnightMovement* KM = KnightMove();
			if (BroomTestStep == 0 && BroomTestT > 7.f && BestBroomRow() >= 0) { BroomTestStep = 1; UE_LOG(LogTemp, Log, TEXT("JEEBBROOMDO pressing T (magic %.0f)"), Magic); ToggleBroom(); }
			if (BroomTestStep == 0 && Tick2 != int32((BroomTestT - Dt) / 2.f) && BroomTestT > 10.f && BroomTestT < 30.f)
				UE_LOG(LogTemp, Log, TEXT("JEEBBROOMDO stall check t %.0f: %.0f cm from the stall, %.0f up, near %d"), BroomTestT, FVector::Dist2D(GetActorLocation(), JeebBroom::StallSpot()), GetActorLocation().Z - JeebBroom::StallSpot().Z, NearBroomStall() ? 1 : 0);
			if (BroomTestStep == 0 && BroomTestT > 12.f && NearBroomStall()) { BroomTestStep = 10; OpenBroomShop(); UE_LOG(LogTemp, Log, TEXT("JEEBBROOMDO at the stall, shop open %d, buying an Ashwood Broom"), IsBroomShopOpen() ? 1 : 0); Inv(EInvOp::BuyBroom, 2, JeebItems::BroomId(2)); }
			if (BroomTestStep == 10 && BroomTestT > 13.f) { BroomTestStep = 11; CloseShop(); UE_LOG(LogTemp, Log, TEXT("JEEBBROOMDO my pack holds %d Ashwood Broom"), Skills ? Skills->CountItem(JeebItems::BroomId(2)) : -1); }
			if (BroomTestStep == 1 && bOnBroom && KM) { KM->bWantsRise = Above < T.BroomCeiling + 5.f && BroomTestT < 40.f; if (!KM->bWantsRise) BroomTestStep = 2; }
			if (BroomTestStep == 2 && bOnBroom) AddMovementInput(GetActorForwardVector().GetSafeNormal2D(), 1.f);
			if (bOnBroom && Tick2 != int32((BroomTestT - Dt) / 2.f))
				UE_LOG(LogTemp, Log, TEXT("JEEBBROOMDO t %.0f: %.1f m up, speed %.0f, magic I see %.1f, empty %d"), BroomTestT, Above, Move ? Move->Velocity.Size2D() : 0.f, Magic, bBroomEmpty ? 1 : 0);
		}
	}
	// Pictures and films, single player (-PlayAs=Wizard -BroomShot or -BroomShot=fly, with -JeebShot -ShotEyes=...): a broom and up at 1 s;
	// "fly" flies ahead at full speed after 2 s (a close-up eye follows him: the smear film)
	static FString ShotMode;
	static const bool bShot = FParse::Value(FCommandLine::Get(), TEXT("BroomShot="), ShotMode) || FParse::Param(FCommandLine::Get(), TEXT("BroomShot"));
	if (bShot && GetNetMode() == NM_Standalone && IsLocallyControlled() && bIsWizard && Skills)
	{
		BroomTestT += Dt;
		if (BroomTestStep == 0 && BroomTestT > 1.f) { BroomTestStep = 1; Skills->AddInvItem(JeebItems::BroomId(0), 1); Magic = 1000.f; ServerToggleBroom(); UE_LOG(LogTemp, Log, TEXT("JEEBBROOMSHOT on the broom %d"), bOnBroom ? 1 : 0); }
		if (bOnBroom) Magic = FMath::Max(Magic, 50.f);   // (never runs out in a film)
		if (bOnBroom && ShotMode.StartsWith(TEXT("fly")) && BroomTestT > 2.f) AddMovementInput(GetActorForwardVector().GetSafeNormal2D(), 1.f);
	}
	// Anyone's machine with -BroomNetDo: what it sees of the other players' brooms, every 3 s
	if (bNetDo && !HasAuthority() && !IsLocallyControlled() && bOnBroom)
	{
		BroomTestT += Dt;
		if (int32(BroomTestT / 3.f) != int32((BroomTestT - Dt) / 3.f))
			UE_LOG(LogTemp, Log, TEXT("JEEBBROOMSEE other %s on a broom at %s (%.1f m up), empty %d, model %d"), *GetName(), *GetActorLocation().ToCompactString(),
				(GetActorLocation().Z - JeebTerrain::HeightAt(GetActorLocation().X, GetActorLocation().Y)) / 100.f, bBroomEmpty ? 1 : 0, BroomModel && BroomModel->IsVisible() ? 1 : 0);
	}
#endif
}

// The broom under him: placed at his hips each frame, along his facing (everyone's machine; none on a server)
void AKnightCharacter::UpdateBroomModel()
{
	if (IsNetMode(NM_DedicatedServer)) return;
	if (!bOnBroom)
	{
		if (BroomModel) BroomModel->SetVisibility(false);
		return;
	}
	if (!BroomModel)
	{
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Props/SM_Broom.SM_Broom"));
		if (!M) return;
		BroomModel = NewObject<UStaticMeshComponent>(this, TEXT("BroomModel"));
		BroomModel->SetStaticMesh(M);
		BroomModel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BroomModel->SetUsingAbsoluteLocation(true); BroomModel->SetUsingAbsoluteRotation(true);
		BroomModel->SetupAttachment(GetRootComponent());
		BroomModel->RegisterComponent();
	}
	BroomModel->SetVisibility(true);
	FVector Hip = GetActorLocation();
	if (USkeletalMeshComponent* Sk = GetMesh(); Sk && Sk->GetBoneIndex(TEXT("Hips")) != INDEX_NONE) Hip = Sk->GetBoneLocation(TEXT("Hips"));
	const FRotator Face(CVarBroomPitch.GetValueOnGameThread(), GetActorRotation().Yaw, 0.f);
	BroomModel->SetWorldLocationAndRotation(Hip + Face.Vector() * CVarBroomFwd.GetValueOnGameThread() + FVector(0.f, 0.f, JeebTune::V().BroomSeatUp), Face);
	BroomModel->SetWorldScale3D(FVector(CVarBroomScale.GetValueOnGameThread()));
}

// ---------------------------------------------------------------- the flight (inside each move: the owner's machine and the server agree)
void UKnightMovement::PhysFlying(float DeltaTime, int32 Iterations)
{
	AKnightCharacter* K = Knight();
	if (!K || !K->IsOnBroom() || DeltaTime < MIN_TICK_TIME) { Super::PhysFlying(DeltaTime, Iterations); return; }
	const JeebTune::FValues& T = JeebTune::V();
	const bool bEmpty = K->IsBroomEmpty();
	const FVector Loc = UpdatedComponent->GetComponentLocation();
	const float Half = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Above = Loc.Z - Half - JeebTerrain::HeightAt(Loc.X, Loc.Y);

	// Across: toward the push at the broom's speed; with no magic, slowing to a halt
	FVector H(Velocity.X, Velocity.Y, 0.f);
	const FVector Push(Acceleration.X, Acceleration.Y, 0.f);
	const FVector Want = bEmpty ? FVector::ZeroVector : Push.GetClampedToMaxSize(GetMaxAcceleration()) / FMath::Max(1.f, GetMaxAcceleration()) * MaxFlySpeed;
	H = FMath::VInterpConstantTo(H, Want, DeltaTime, bEmpty ? T.BroomStop : T.BroomAccel);
	// Up and down: Space climbs, Ctrl sinks; with no magic it hangs until it has stopped, then drifts straight down
	float VzWant = bEmpty ? (H.Size() < 60.f ? -T.BroomDrift : 0.f) : (bWantsRise ? T.BroomClimb : bWantsLetGo ? -T.BroomClimb : 0.f);
	if (!bEmpty && Above > T.BroomCeiling * 100.f) VzWant = FMath::Min(VzWant, -120.f);                   // (over the ceiling: eased back under it)
	else if (!bEmpty && Above > T.BroomCeiling * 100.f - 150.f) VzWant = FMath::Min(VzWant, 0.f);         // (at the ceiling: no higher)
	const float Vz = FMath::FInterpConstantTo(Velocity.Z, VzWant, DeltaTime, 1400.f);
	Velocity = H + FVector(0.f, 0.f, Vz);

	const FVector Delta = Velocity * DeltaTime;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.Time < 1.f)
	{
		const bool bGround = Hit.ImpactNormal.Z > 0.7f && Vz <= 0.f;
		if (bGround && bEmpty) { SetMovementMode(MOVE_Walking); return; }   // (out of magic and down: he steps off; the server ends the ride)
		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}
	if (!HasValidData()) return;
	if (!bJustTeleported) Velocity = (UpdatedComponent->GetComponentLocation() - Loc) / DeltaTime;
	if (bEmpty && Above < 6.f)   // (within a hand of the ground: down)
	{
		FFindFloorResult F;
		FindFloor(UpdatedComponent->GetComponentLocation(), F, false);
		if (F.IsWalkableFloor() && F.FloorDist < 10.f) SetMovementMode(MOVE_Walking);
	}
}
