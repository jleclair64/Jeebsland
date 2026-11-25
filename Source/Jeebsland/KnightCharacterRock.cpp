// The troll's throwing rocks (2026-11-22, owner: "Let's remove the bow and arrow slots from the troll. I want instead to give him rocks to
// throw. Please add a sack of rocks in his left hand and add a throwing animation ... give it a charge up mechanic where fully charged rocks
// go further and do more damage.")
//
// Key 2 (the bow's slot on the bar) takes the rocks in hand: the club goes on his back and the sack of rocks hangs from his left fist. Either
// mouse button held winds up (he turns to the camera, the reticle shows and closes in as the charge fills over RockChargeTime); letting go
// throws a rock overarm toward what is under the reticle. The charge sets how fast the rock leaves (so how far it can reach) and how hard it
// hits: a quick flick is a short lob, a full wind-up reaches across a field. The rock is launched on the arc that lands on the reticle's point
// when it can reach it; beyond its reach it goes at its farthest angle and falls short.
// Online: the player's machine decides the throw from the button and asks the server (ServerRockThrow), which checks the rocks in his pack,
// the charge against its own clock (never more than it saw held, plus a little), the direction (ahead of him) and the start (near his
// shoulder, nothing solid between), then throws: an AProjectile carrying the rock's look; damage and pick-up are the server's.
// The pose: FKnightAnimator::TrollThrow (KnightAnimatorTrollThrow.cpp). Numbers: Content/Data/Tuning.ini [TrollRocks].
#include "KnightCharacter.h"
#include "KnightAnimator.h"
#include "Inventory.h"
#include "Messages.h"
#include "Projectile.h"
#include "SkillComponent.h"
#include "JeebTuning.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "TimerManager.h"

static constexpr float RockThrowDur = 0.55f;   // s the throw's follow-through lasts (the rock leaves at its start)

static bool UnlimitedRocks()   // (TESTING: the same switch as UnlimitedArrows, [Jeebsland.Testing] in DefaultGame.ini)
{
	static const bool b = []() { bool v = false; if (GConfig) GConfig->GetBool(TEXT("Jeebsland.Testing"), TEXT("UnlimitedArrows"), v, GGameIni); return v; }();
	return b;
}

static bool RockLog() { static const bool b = FParse::Param(FCommandLine::Get(), TEXT("JeebShot")) || FParse::Param(FCommandLine::Get(), TEXT("RockLog")); return b; }

float AKnightCharacter::RockSpeedFor(float Charge) const
{
	const auto& T = JeebTune::V();
	return FMath::Lerp(T.RockSpeedLow, T.RockSpeedFull, FMath::Clamp(Charge, 0.f, 1.f));
}

float AKnightCharacter::RockSagFor(float Charge) const
{
	const auto& T = JeebTune::V();
	return FMath::Lerp(T.RockSagLow, T.RockSagFull, FMath::Clamp(Charge, 0.f, 1.f));
}

// ---------------- The rocks in hand (key 2) ----------------

void AKnightCharacter::OnRep_RockMode()
{
	if (!bRockMode && Rock != ERock::None) SetRockPhase(ERock::None);
}

void AKnightCharacter::SetRockMode(bool bOn)
{
	if (!bIsTroll || bRockMode == bOn) return;
	if (bOn && Skills && Skills->CountItem(JeebItems::IdRocks) <= 0 && !UnlimitedRocks())
	{
		if (IsLocallyControlled()) JeebMsg::Post(2.f, FColor::Orange, TEXT("You have no rocks to throw."));
		if (HasAuthority() && !IsLocallyControlled() && Skills) Skills->Say(2.f, FColor::Orange, TEXT("You have no rocks to throw."));
		return;
	}
	bRockMode = bOn;
	if (Rock != ERock::None) SetRockPhase(ERock::None);
	bAttacking = false;
	bCharging = false;
	if (RockLog()) UE_LOG(LogTemp, Log, TEXT("JEEBROCK %s rocks in hand %d"), HasAuthority() ? TEXT("server") : TEXT("client"), bOn ? 1 : 0);
	if (!HasAuthority()) ServerRockMode(bOn);
}

void AKnightCharacter::ServerRockMode_Implementation(bool bOn)
{
	SetRockMode(bOn);
	if (bRockMode != bOn) ForceNetUpdate();   // (refused: the owner hears the server's word back through OnRep)
}

// ---------------- Winding up and letting go ----------------

void AKnightCharacter::SetRockPhase(ERock P)
{
	if (P == ERock::Charging) RockCharge = 0.f;
	Rock = P;
	RockT = 0.f;
}

// (every machine that decides: the player's own, and the server for it) may he start a wind-up now?
static bool RockFree(const AKnightCharacter* K, bool bAttacking, bool bKnocked, float Stun, bool bRolling, bool bSliding, bool bClimbing, bool bClimbMove, bool bSitting, bool bMounted, bool bDrinking)
{
	return !(bAttacking || bKnocked || Stun > 0.f || bRolling || bSliding || bClimbing || bClimbMove || bSitting || bMounted || bDrinking);
}

void AKnightCharacter::RockPress()
{
	if (!InRockMode() || Rock == ERock::Charging) return;
	if (Rock == ERock::Throw && RockT < RockThrowDur * 0.6f) return;   // (still following through: the held button winds up again from TickRock)
	if (RockCooldown > 0.f) return;
	if (!RockFree(this, bAttacking, bKnockedDown, StunLeft, bRolling, bSliding, bClimbing, InClimbMove(), bSitting, bMounted, bDrinking)) return;
	if (Skills && Skills->CountItem(JeebItems::IdRocks) <= 0 && !UnlimitedRocks())
	{
		if (IsLocallyControlled()) JeebMsg::Post(2.f, FColor::Orange, TEXT("You have no rocks to throw."));
		return;
	}
	SetRockPhase(ERock::Charging);
	if (RockLog()) UE_LOG(LogTemp, Log, TEXT("JEEBROCK %s wind-up starts"), HasAuthority() ? TEXT("server") : TEXT("client"));
	if (GetNetMode() == NM_Standalone) return;
	if (!HasAuthority()) ServerRockCharge();
	else MulticastRock(1, 0.f);
}

void AKnightCharacter::RockRelease()
{
	if (Rock != ERock::Charging) return;
	const float C = RockCharge;
	FVector Target = FVector::ZeroVector;
	const FVector Dir = RockAimDir(C, &Target);
	const FVector From = RockStart();
	if (RockLog()) UE_LOG(LogTemp, Log, TEXT("JEEBROCK %s throw: charge %.2f from %s dir %s target %s"), HasAuthority() ? TEXT("server") : TEXT("client"), C, *From.ToCompactString(), *Dir.ToCompactString(), *Target.ToCompactString());
	SetRockPhase(ERock::Throw);
	RockThrowCharge = C;
	RockCooldown = JeebTune::V().RockCooldown;
	if (HasAuthority()) { ThrowRock(Dir, C, From); if (GetNetMode() != NM_Standalone) MulticastRock(2, C); }
	else ServerRockThrow(Dir, C, From);
}

void AKnightCharacter::ServerRockCharge_Implementation()
{
	if (Rock == ERock::Throw && RockT >= RockThrowDur * 0.4f) SetRockPhase(ERock::None);   // (the player's machine may be a moment ahead of ours)
	RockCooldown = FMath::Min(RockCooldown, 0.f);
	RockPress();
}

void AKnightCharacter::ServerRockThrow_Implementation(FVector_NetQuantizeNormal Dir, float Charge, FVector_NetQuantize10 From)
{
	if (Rock != ERock::Charging || !InRockMode())
	{
		if (RockLog()) UE_LOG(LogTemp, Log, TEXT("JEEBROCK server refused a throw: phase %d rocks in hand %d"), int32(Rock), bRockMode ? 1 : 0);
		return;
	}
	const float SawFull = RockT / FMath::Max(JeebTune::V().RockChargeTime, 0.05f) + 0.15f;   // (never more charge than we saw held)
	const float C = FMath::Clamp(FMath::Min(Charge, SawFull), 0.f, 1.f);
	FVector D = FVector(Dir).GetSafeNormal();
	if (FVector::DotProduct(D.GetSafeNormal2D(), GetActorForwardVector()) < 0.2f || D.Z > 0.9f) D = GetBaseAimRotation().Vector();   // (only ahead of him, never straight up)
	FVector S = From;
	const FVector Own = RockStart();
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(RockStart), false, this);
	if (FVector::Dist(S, Own) > 200.f || GetWorld()->LineTraceSingleByChannel(Hit, Own, S, ECC_Visibility, Q)) S = Own;
	SetRockPhase(ERock::Throw);
	RockThrowCharge = C;
	RockCooldown = JeebTune::V().RockCooldown;
	ThrowRock(D, C, S);
	MulticastRock(2, C);
}

void AKnightCharacter::ServerRockCancel_Implementation()
{
	if (Rock == ERock::Charging) { SetRockPhase(ERock::None); MulticastRock(0, 0.f); }
}

void AKnightCharacter::MulticastRock_Implementation(uint8 Phase, float Charge)
{
	if (HasAuthority() || IsLocallyControlled() || Phase > 2) return;   // (they already know)
	SetRockPhase(static_cast<ERock>(Phase));
	if (Phase == 2) RockThrowCharge = Charge;
}

// (server) One rock out of the pack, thrown
void AKnightCharacter::ThrowRock(const FVector& Dir, float Charge, const FVector& From)
{
	if (!Skills || !HasAuthority()) return;
	if (Skills->CountItem(JeebItems::IdRocks) <= 0 && !UnlimitedRocks()) { Skills->Say(2.f, FColor::Orange, TEXT("You have no rocks to throw.")); return; }
	if (!UnlimitedRocks()) Skills->RemoveItems(JeebItems::IdRocks, 1);
	const auto& T = JeebTune::V();
	const float C = FMath::Clamp(Charge, 0.f, 1.f);
	const int32 Level = Skills->GetLevel(ESkillType::Archery);
	const float Base = FMath::Lerp(T.RockDamageLow, T.RockDamageFull, FMath::Pow(C, 1.5f)) * (1.f + T.RockDamagePerLevel * float(Level - 1));
	const int32 Dmg = FMath::Max(1, FMath::RoundToInt(Base * FMath::FRandRange(0.85f, 1.15f)));
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AProjectile* P = GetWorld()->SpawnActor<AProjectile>(AProjectile::StaticClass(), From, Dir.Rotation(), SP))
		P->Launch(this, Dir, RockSpeedFor(C), 0.f, Dmg, false, ESkillType::Archery, false, 0, JeebItems::IdRocks, RockSagFor(C), 0.f);
	UE_LOG(LogTemp, Log, TEXT("JEEBROCK server threw: charge %.2f speed %.0f sag %.0f damage %d, %d rocks left"), C, RockSpeedFor(C), RockSagFor(C), Dmg, Skills->CountItem(JeebItems::IdRocks));
}

// ---------------- Aiming ----------------

FVector AKnightCharacter::RockStart() const
{
	// high over his right shoulder, a little ahead, where the throwing hand passes as it lets go (the same on every machine)
	return GetActorTransform().TransformPosition(FVector(35.f, 38.f, 95.f));
}

FVector AKnightCharacter::RockAimDir(float Charge, FVector* OutTarget) const
{
	const FVector Start = RockStart();
	FVector Target = Start + GetBaseAimRotation().Vector() * 3000.f;
	if (IsLocallyControlled() && !ShotSideOn())
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
			if (PC->PlayerCameraManager)
			{
				// what is under the reticle (the middle of the screen), as the bow finds it
				const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
				const FVector CamDir = PC->PlayerCameraManager->GetCameraRotation().Vector();
				FHitResult Hit;
				FCollisionQueryParams Q(SCENE_QUERY_STAT(RockAim), false, this);
				const FVector From = CamLoc + CamDir * FMath::Max(150.f, FVector::DotProduct(Start - CamLoc, CamDir) + 20.f);
				Target = CamLoc + CamDir * 12000.f;
				if (GetWorld()->LineTraceSingleByChannel(Hit, From, Target, ECC_Visibility, Q)) Target = Hit.ImpactPoint;
				const float Body = AProjectile::FirstBodyAlong(GetWorld(), From, Target, this);
				if (Body <= 1.f) Target = From + (Target - From) * Body;
			}
	if (ShotSideOn() || !IsLocallyControlled()) Target = Start + GetActorForwardVector() * 1500.f;
	if (OutTarget) *OutTarget = Target;
	// The arc that lands on the target: speed v, falling g (the projectile's own sag). The lower of the two arcs; out of reach, the
	// farthest-reaching angle (it falls short); right up close, straight at it.
	const FVector D = Target - Start;
	const float Dx = FVector2D(D.X, D.Y).Size(), Dy = D.Z;
	const FVector Flat = FVector(D.X, D.Y, 0.f).GetSafeNormal();
	if (Dx < 60.f || Flat.IsNearlyZero()) return D.GetSafeNormal();
	const float V = RockSpeedFor(Charge), G = FMath::Max(RockSagFor(Charge), 1.f);
	const float V2 = V * V, Disc = V2 * V2 - G * (G * Dx * Dx + 2.f * Dy * V2);
	float Angle;
	if (Disc >= 0.f) Angle = FMath::Atan((V2 - FMath::Sqrt(Disc)) / (G * Dx));
	else Angle = FMath::DegreesToRadians(FMath::Clamp(40.f + FMath::RadiansToDegrees(FMath::Atan2(Dy, Dx)) * 0.5f, 20.f, 60.f));
	return (Flat * FMath::Cos(Angle) + FVector::UpVector * FMath::Sin(Angle)).GetSafeNormal();
}

// ---------------- Every frame ----------------

void AKnightCharacter::TickRock(float Dt)
{
	if (!bIsTroll) return;
	RockT += Dt;
	RockCooldown = FMath::Max(0.f, RockCooldown - Dt);
	const bool bTaken = !InRockMode() || bAttacking || bKnockedDown || StunLeft > 0.f || bRolling || bSliding || bClimbing || InClimbMove() || bSitting || bMounted || Health <= 0.f || bDrinking;
	if (Rock == ERock::Charging)
	{
		RockCharge = FMath::Min(1.f, RockT / FMath::Max(JeebTune::V().RockChargeTime, 0.05f));
		if (bTaken || (IsLocallyControlled() && (IsUIOpen() || bPanelCursor)))   // (let down without throwing: the rock goes back in the sack)
		{
			SetRockPhase(ERock::None);
			if (GetNetMode() != NM_Standalone)
			{
				if (IsLocallyControlled() && !HasAuthority()) ServerRockCancel();
				else if (HasAuthority()) MulticastRock(0, 0.f);
			}
		}
	}
	else if (Rock == ERock::Throw && RockT >= RockThrowDur) SetRockPhase(ERock::None);
	// The button still down after a throw: wind up again as soon as he can
	if (IsLocallyControlled() && InRockMode() && (bAttackDown || bAimHeld) && Rock == ERock::None && !bTaken && !IsUIOpen() && !bPanelCursor) RockPress();
	// (testing) topped up to a full stack
	if (HasAuthority() && Skills && UnlimitedRocks())
		if (const int32 Have = Skills->CountItem(JeebItems::IdRocks); Have < 40) Skills->AddInvItem(JeebItems::IdRocks, 40 - Have);
	RockAimAlpha = FMath::FInterpTo(RockAimAlpha, IsAimingRock() ? 1.f : 0.f, Dt, IsAimingRock() ? 8.f : 5.f);
}

// ---------------- The sack in his left fist and the rock in his right ----------------

static UStaticMeshComponent* MakeRockBit(AActor* Owner, const TCHAR* Path, const TCHAR* Fallback, const FLinearColor& Col)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
	C->SetupAttachment(Owner->GetRootComponent());
	C->SetUsingAbsoluteLocation(true); C->SetUsingAbsoluteRotation(true); C->SetUsingAbsoluteScale(true);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	C->SetStaticMesh(M ? M : LoadObject<UStaticMesh>(nullptr, Fallback));
	C->RegisterComponent();
	if (!M)
		if (UMaterialInstanceDynamic* D = C->CreateDynamicMaterialInstance(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"))))
			D->SetVectorParameterValue(TEXT("Color"), Col);
	C->SetVisibility(false);
	return C;
}

void AKnightCharacter::UpdateRockSack()
{
	if (!bIsTroll || !Anim || GetNetMode() == NM_DedicatedServer) return;
	const bool bShow = InRockMode() && !bStowed && !bDrinking;
	// the club is put away while the rocks are in hand (both his hands are busy: the sack and the throw)
	if (InRockMode() != bRockClubHidden)
	{
		bRockClubHidden = InRockMode();
		for (UProceduralMeshComponent* C : HeldComps) if (C) C->SetHiddenInGame(bRockClubHidden);
		for (UStaticMeshComponent* C : HeldStatics) if (C) C->SetHiddenInGame(bRockClubHidden);
	}
	else if (bRockClubHidden)   // (a rebuild of the held weapon while the rocks are in hand)
	{
		for (UProceduralMeshComponent* C : HeldComps) if (C && !C->bHiddenInGame) C->SetHiddenInGame(true);
		for (UStaticMeshComponent* C : HeldStatics) if (C && !C->bHiddenInGame) C->SetHiddenInGame(true);
	}
	if (!bShow && !RockSack) return;
	if (!RockSack)
	{
		RockSack = MakeRockBit(this, TEXT("/Game/TrollKit/SM_RockSack.SM_RockSack"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), FLinearColor(0.32f, 0.2f, 0.1f));
		RockInHand = MakeRockBit(this, TEXT("/Game/TrollKit/SM_TrollRock.SM_TrollRock"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), FLinearColor(0.42f, 0.42f, 0.44f));
		if (!RockSack->GetStaticMesh() || RockSack->GetStaticMesh()->GetName() == TEXT("Sphere")) RockSack->SetWorldScale3D(FVector(0.3f, 0.3f, 0.36f));
		if (!RockInHand->GetStaticMesh() || RockInHand->GetStaticMesh()->GetName() == TEXT("Sphere")) RockInHand->SetWorldScale3D(FVector(0.15f));
	}
	RockSack->SetVisibility(bShow);
	// (the rock is in his right fist from the start of a wind-up until it leaves at the throw)
	RockInHand->SetVisibility(bShow && Rock == ERock::Charging);
	if (!bShow) return;
	// The sack hangs from his left fist by its neck: upright in the world (it swings from the fist, it isn't held stiff), turned with him
	const FTransform L = Anim->TrollLeftFistWorld();
	const FRotator Yaw(0.f, GetActorRotation().Yaw + 90.f, 0.f);
	RockSack->SetWorldLocationAndRotation(L.GetLocation(), Yaw);
	// (2026-11-24, owner: the bag "not in his hand, and too small": bigger, and its neck in the hole of his fist, KnightAnimatorTrollThrow.cpp)
	if (RockSack->GetStaticMesh() && RockSack->GetStaticMesh()->GetName() != TEXT("Sphere")) RockSack->SetWorldScale3D(FVector(FMath::Clamp(JeebTune::V().RockSackScale, 0.3f, 4.f)));
	else RockSack->SetWorldLocation(L.GetLocation() - FVector(0.f, 0.f, 18.f));
	const FTransform R = Anim->TrollRightFistWorld();
	if (RockLog() && Rock == ERock::Charging && RockT > 1.f && RockT < 1.05f)   // (test log: where the drawn fists are against our own bones' wrists)
		UE_LOG(LogTemp, Log, TEXT("JEEBROCK hands: left fist %s (our wrist %s), right fist %s (our wrist %s), sack %s rock %s"), *L.GetLocation().ToCompactString(), *Anim->BoneAt(JeebAnim::LHand).ToCompactString(),
			*R.GetLocation().ToCompactString(), *Anim->BoneAt(JeebAnim::RHand).ToCompactString(), *RockSack->GetComponentLocation().ToCompactString(), *RockInHand->GetComponentLocation().ToCompactString());
	RockInHand->SetWorldLocationAndRotation(R.GetLocation(), R.GetRotation());
	if (RockInHand->GetStaticMesh() && RockInHand->GetStaticMesh()->GetName() != TEXT("Sphere")) RockInHand->SetWorldScale3D(FVector(FMath::Clamp(JeebTune::V().RockHandScale, 0.3f, 4.f)));
}

// ---------------- Tests ----------------
// -SuitDo=rock<hundredths> (single player) or -TrollNetDo=rock<hundredths> (a client): rocks in hand, then a wind-up held for that share of a
// full charge and let go, twice, 4 s apart (rock100: a full charge; rock0: a flick). rockpair: a flick, then a full charge 4 s later.
bool AKnightCharacter::TrollRockTest(const FString& Do)
{
	if (!bIsTroll || !Do.StartsWith(TEXT("rock"))) return false;
	auto Later = [this](float At, TFunction<void()> F)
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, MoveTemp(F)), FMath::Max(At, 0.01f), false);
	};
	if (Do == TEXT("rocksprint"))   // (a sprint ahead for 6 s from 0.5 s, the club in hand, then the sack from 3 s: armour and sack checks)
	{
		Later(0.5f, [this]() { StartSprint(); });
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (GetWorld()->GetTimeSeconds() < 6.5f) AddMovementInput(GetActorForwardVector(), 1.f);
		}), 0.005f, true, 0.5f);
		Later(3.f, [this]() { SetRockMode(true); });
		return true;
	}
	SetRockMode(true);
	auto Throw = [this, Later](float Share, float At)
	{
		Later(At, [this]() { UE_LOG(LogTemp, Log, TEXT("JEEBROCK test: button down")); RockPress(); });
		Later(At + 0.02f + Share * JeebTune::V().RockChargeTime, [this]() { UE_LOG(LogTemp, Log, TEXT("JEEBROCK test: button up at charge %.2f"), RockCharge); RockRelease(); });
	};
	if (Do == TEXT("rockpair")) { Throw(0.f, 1.f); Throw(1.f, 5.f); return true; }
	const float Share = Do.Len() > 4 ? FCString::Atoi(*Do.Mid(4)) / 100.f : 1.f;
	Throw(Share, 1.f);
	Throw(Share, 5.f);
	return true;
}
