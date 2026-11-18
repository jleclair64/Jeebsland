// Fast travel, the knight's side (2026-11-18; the rest is FastTravel.h / FastTravel.cpp).
#include "KnightCharacter.h"
#include "KnightMovement.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

// Why he can't travel right now (the server asks before starting a travel and every tenth of a second during the 3 s channel). Empty: free.
FString AKnightCharacter::TravelBlocker() const
{
	if (Health <= 0.f) return TEXT("You can't travel now.");
	if (bMounted) return TEXT("You can't fast travel while riding. Get off first.");
	if (bClimbing || InClimbMove()) return TEXT("You can't fast travel while climbing.");
	if (const UKnightMovement* KM = KnightMove()) if (KM->InWater()) return TEXT("You can't fast travel while swimming.");
	if (bKnockedDown) return TEXT("You can't fast travel while knocked down.");
	if (StunLeft > 0.f) return TEXT("You can't fast travel while stunned.");
	if (bAttacking || bCharging || bRolling || bSliding) return TEXT("You can't fast travel while fighting.");
	if (bSitting) return TEXT("Stand up first.");
	if (bDrinking) return TEXT("Finish your drink first.");
	if (!GetCharacterMovement() || !GetCharacterMovement()->IsMovingOnGround()) return TEXT("You must be standing on the ground to fast travel.");
	return FString();
}

// The jump itself, on the server (the knight is moved as a teleport, so the movement component takes it as one and does not send the
// player's machine a correction) and on the player's machine (ClientTravelArrived), as a fallen knight is put in Oak Hill (ClientDied).
void AKnightCharacter::TravelPlaced(const FVector& Dest, float Yaw)
{
	EndGathering();
	CloseLoot();
	GetCharacterMovement()->Velocity = FVector::ZeroVector;
	TeleportTo(Dest, FRotator(0.f, Yaw, 0.f), false, true);
	if (AController* C = GetController()) if (IsLocallyControlled() || HasAuthority()) C->SetControlRotation(FRotator(0.f, Yaw, 0.f));
}
