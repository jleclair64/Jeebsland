// Fall damage (owner, 2026-11-23): every character, the server decides. The drop is how far he came down from the highest point of the
// fall to where he lands; the first FallSafeHeight of it costs nothing (jumps, steps, short ledges), each metre past that FallDamagePerMetre
// hit points, at most FallMaxDamage. Landing in water costs nothing (swimming starts in deep water, so a dive is never a fall, and a
// landing in water FallWaterDepth deep counts the same); nor does a fall on a horse, on a ladder or wall (they have their own rules), or
// being put somewhere (a teleport, a respawn, fast travel). Armour doesn't soften it (Hurt, environmental). Numbers: Tuning.ini [Pvp].
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "JeebTuning.h"
#include "JeebWater.h"
#include "Terrain.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AKnightCharacter::TickFall(float DeltaSeconds)
{
	if (!HasAuthority() || !Skills || !GetWorld()) return;
	UCharacterMovementComponent* M = GetCharacterMovement();
	if (!M) return;
#if !UE_BUILD_SHIPPING
	// -FallTest (single player): the highest and lowest ground on a coarse grid; stand on the high one, fast travel (TravelPlaced) to the low one (no damage),
	// then teleport 30 m up over it and fall (damage). Logs JEEBFALLTEST.
	static const bool bFallTest = FParse::Param(FCommandLine::Get(), TEXT("FallTest"));
	if (bFallTest && GetNetMode() == NM_Standalone && IsLocallyControlled())
	{
		static float FT = 0.f; static int32 FS = 0; static FVector Hi, Lo; static float HealthAt = 0.f;
		FT += DeltaSeconds;
		if (FS == 0 && FT > 2.f)
		{
			float HiZ = -1e9f, LoZ = 1e9f;
			for (float X = -30000.f; X <= 30000.f; X += 1500.f) for (float Y = -30000.f; Y <= 30000.f; Y += 1500.f)
			{
				const float Z = JeebTerrain::HeightAt(X, Y);
				float Surf = 0.f;
				if (JeebWater::DepthAt(FVector(X, Y, Z), Surf) > 0.f) continue;   // (dry ground only)
				if (Z > HiZ) { HiZ = Z; Hi = FVector(X, Y, Z); }
				if (Z < LoZ) { LoZ = Z; Lo = FVector(X, Y, Z); }
			}
			FS = 1;
			UE_LOG(LogTemp, Log, TEXT("JEEBFALLTEST high ground %.0f,%.0f z %.0f; low ground %.0f,%.0f z %.0f; health %.0f"), Hi.X, Hi.Y, Hi.Z, Lo.X, Lo.Y, Lo.Z, Health);
			TeleportTo(Hi + FVector(0.f, 0.f, 130.f), GetActorRotation(), false, true);
		}
		if (FS == 1 && FT > 6.f) { FS = 2; HealthAt = Health; UE_LOG(LogTemp, Log, TEXT("JEEBFALLTEST on the high ground: health %.0f, z %.0f. Fast travel down %.0f cm"), Health, GetActorLocation().Z, GetActorLocation().Z - Lo.Z); TravelPlaced(Lo + FVector(0.f, 0.f, 130.f), 0.f); }
		if (FS == 2 && FT > 11.f) { FS = 3; UE_LOG(LogTemp, Log, TEXT("JEEBFALLTEST %s after fast travel: health %.0f (was %.0f), z %.0f"), Health >= HealthAt ? TEXT("PASS") : TEXT("FAIL"), Health, HealthAt, GetActorLocation().Z); HealthAt = Health; TeleportTo(GetActorLocation() + FVector(0.f, 0.f, 3000.f), GetActorRotation(), false, true); }
		if (FS == 3 && FT > 18.f) { FS = 4; UE_LOG(LogTemp, Log, TEXT("JEEBFALLTEST %s after a real 30 m drop: health %.0f (was %.0f)"), Health < HealthAt ? TEXT("PASS") : TEXT("FAIL"), Health, HealthAt); }
	}
#endif
	const FVector Loc = GetActorLocation();
	const bool bAir = M->IsFalling();
	// put somewhere else: a noted teleport, or any sudden jump no fall could make (a respawn, GoTown, a SetActorLocation): never a fall (owner 2026-11-22: fast travel hurt)
	const float Step = float(FVector::Dist(Loc, FallLastLoc)), Allowed = float(M->Velocity.Size()) * FMath::Max(DeltaSeconds, 0.02f) * 1.5f + 300.f;
	const bool bPlaced = bFallTeleported || Step > Allowed || FMath::Abs(Loc.Z - FallLastZ) > 1500.f;
	if (bPlaced) { bFallWasFalling = false; FallPeakZ = float(Loc.Z); bFallTeleported = false; }
	FallLastLoc = Loc;
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

// Every teleport (TravelPlaced, the portkey, the broom's catch-up, Hazel's fetch, camp and horse moves) goes through TeleportTo: the fall tracker forgets the fall
bool AKnightCharacter::TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest, bool bNoCheck)
{
	const bool bOk = Super::TeleportTo(DestLocation, DestRotation, bIsATest, bNoCheck);
	if (bOk && !bIsATest) NoteTeleport();
	return bOk;
}
