#include "KnightMovement.h"
#include "KnightCharacter.h"
#include "Horse.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

// The horse's automatic jump (owner 2026-11-22: "a horse jump ... when it runs into logs or rocks and stuff that it looks like it should
// be able to jump over. Please make this automatic."). While riding, the knight's own movement carries the horse, so the jump is the
// rider's: decided at the end of each move, from that move's position and speed, on the player's machine and on the server alike (the
// same move gives the same answer, so the server never corrects it). Something low ahead (JeebHorse::JumpProbe: up to JumpMaxTop high,
// open above, level ground beyond) launches him in an arc just over it; the horse under him pitches with the arc (AHorse::TickRidden).
// Taller things, trees and houses still block. Fall damage is skipped while riding (KnightCharacterFall.cpp).
bool UKnightMovement::TryHorseJump()
{
	AKnightCharacter* K = Knight();
	if (!K || !K->IsMounted() || !UpdatedComponent || K->GetLocalRole() == ROLE_SimulatedProxy || !IsMovingOnGround()) return false;
	const FVector2D V(Velocity);
	const float Spd = V.Size();
	if (Spd < 200.f) return false;
	const FVector2D Dir = V / Spd;
	if (FVector2D::DotProduct(Dir, FVector2D(UpdatedComponent->GetForwardVector()).GetSafeNormal()) < 0.5f) return false;   // (not backing up)
	const UCapsuleComponent* Cap = K->GetCapsuleComponent();
	const FVector Feet = UpdatedComponent->GetComponentLocation() - FVector(0.f, 0.f, Cap ? Cap->GetScaledCapsuleHalfHeight() : 90.f);
	const float Radius = Cap ? Cap->GetScaledCapsuleRadius() : 40.f;
	float Top = 0.f, Dist = 0.f;
	if (!JeebHorse::JumpProbe(GetWorld(), Feet, Dir, Radius + FMath::Max(70.f, Spd * 0.22f), K, K->GetRideHorse(), Top, Dist)) return false;
	// up just over it (a little to spare), keeping the speed he has
	const float G = FMath::Abs(GetGravityZ());
	Velocity.Z = FMath::Sqrt(2.f * G * (Top + 45.f));
	SetMovementMode(MOVE_Falling);
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSEJUMP %s %s rides over %.0f cm (%.1f m ahead, %.0f cm/s)"), CharacterOwner->HasAuthority() ? TEXT("server") : TEXT("client"),
		*K->GetName(), Top, Dist / 100.f, Spd);
	return true;
}
