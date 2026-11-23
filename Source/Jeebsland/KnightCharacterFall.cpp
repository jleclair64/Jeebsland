// Fall damage (owner, 2026-11-23): every character, the server decides. The drop is how far he came down from the highest point of the
// fall to where he lands; the first FallSafeHeight of it costs nothing (jumps, steps, short ledges), each metre past that FallDamagePerMetre
// hit points, at most FallMaxDamage. Landing in water costs nothing (swimming starts in deep water, so a dive is never a fall, and a
// landing in water FallWaterDepth deep counts the same); nor does a fall on a horse, on a ladder or wall (they have their own rules), or
// being put somewhere (a teleport, a respawn, fast travel). Armour doesn't soften it (Hurt, environmental). Numbers: Tuning.ini [Pvp].
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "JeebTuning.h"
#include "JeebWater.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AKnightCharacter::TickFall()
{
	if (!HasAuthority() || !Skills || !GetWorld()) return;
	UCharacterMovementComponent* M = GetCharacterMovement();
	if (!M) return;
	const FVector Loc = GetActorLocation();
	const bool bAir = M->IsFalling();
	const bool bPlaced = FMath::Abs(Loc.Z - FallLastZ) > 1500.f;   // (put somewhere else in one step: a teleport is not a fall)
	if (bPlaced) { bFallWasFalling = false; FallPeakZ = float(Loc.Z); }
#if !UE_BUILD_SHIPPING
	static const bool bTrace = FParse::Param(FCommandLine::Get(), TEXT("JeebPvpTest"));   // (the online test: every start and end of a fall)
	if (bTrace && bAir != bFallWasFalling) UE_LOG(LogTemp, Log, TEXT("JEEBFALL trace %s air %d z %.0f peak %.0f mode %d"), *GetName(), bAir ? 1 : 0, Loc.Z, FallPeakZ, int32(M->MovementMode));
#endif
	if (bAir)
	{
		FallPeakZ = FMath::Max(FallPeakZ, float(Loc.Z));   // (a fall starts from where he last stood: FallPeakZ follows him on the ground)
	}
	else if (bFallWasFalling)
	{
		// he stopped falling this step: landed on the ground (walking), or went into the water / onto a ladder / wall (a custom mode: no fall)
		const bool bOnGround = M->MovementMode == MOVE_Walking || M->MovementMode == MOVE_NavWalking;
		const float Drop = FallPeakZ - float(Loc.Z);
		float Surf = 0.f;
		const float Feet = float(Loc.Z) - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float Water = JeebWater::DepthAt(FVector(Loc.X, Loc.Y, Feet), Surf);
		const auto& T = JeebTune::V();
		const bool bSafe = !bOnGround || bMounted || Health <= 0.f || Water >= T.FallWaterDepth || bClimbing;
		int32 Dmg = 0;
		if (!bSafe && Drop > T.FallSafeHeight && T.FallDamagePerMetre > 0.f)
			Dmg = FMath::Clamp(FMath::RoundToInt((Drop - T.FallSafeHeight) / 100.f * T.FallDamagePerMetre), 1, FMath::Max(1, FMath::RoundToInt(T.FallMaxDamage)));
		if (Drop > 150.f || Dmg > 0)
			UE_LOG(LogTemp, Log, TEXT("JEEBFALL %s dropped %.0f cm, %s: %d damage (safe up to %.0f cm, water %.0f cm)"), *GetName(), Drop, bOnGround ? TEXT("landed") : TEXT("not on ground"), Dmg, T.FallSafeHeight, Water);
		if (Dmg > 0)
		{
			Hurt(Dmg, nullptr, true);
			if (Health > 0.f) Skills->Say(2.f, FColor(255, 170, 120), FString::Printf(TEXT("You land hard: %d damage"), Dmg));
		}
		FallPeakZ = -1.0e9f;
	}
	if (!bAir) FallPeakZ = float(Loc.Z);   // (standing, walking, swimming, climbing: the next fall starts from here)
	bFallWasFalling = bAir;
	FallLastZ = float(Loc.Z);
}
