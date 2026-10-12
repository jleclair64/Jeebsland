#include "KnightCharacter.h"
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

void AKnightCharacter::SpawnDust(const FVector& Pos, float Size)
{
	if (Dust.Num() == 0) return;
	FDust& D = Dust[DustNext];
	DustNext = (DustNext + 1) % Dust.Num();
	D.Pos = Pos;
	D.Age = 0.f;
	D.Life = FMath::FRandRange(0.4f, 0.6f);
	D.Size = Size * 0.45f; // small, subtle puffs
}

int32 AKnightCharacter::GroundUnderfoot() const
{
	const UCharacterMovementComponent* M = GetCharacterMovement();
	if (!M || M->IsFalling() || !M->CurrentFloor.bBlockingHit) return 0;
	const AActor* A = M->CurrentFloor.HitResult.GetActor();
	if (!A || !A->IsA<AGrassField>()) return 0;   // decks, floors, bridges, stone: nothing to kick up
	const FVector L = GetActorLocation();
	if (FVector2D::Distance(FVector2D(L.X, L.Y), JeebTerrain::CastlePos()) < JeebTerrain::CastleWall + 100.f) return 0;   // the castle's paved yard
	return JeebTerrain::PathClearance(L.X, L.Y) < 0.f ? 1 : 2;
}

void AKnightCharacter::Kick(const FVector& Foot, float Strength)
{
	const int32 G = GroundUnderfoot();
	if (G == 0 || Clods.Num() == 0 || Blades.Num() == 0) return;
	// under the broad-leaf trees the ground is carpeted with fallen leaves: kick some of those up as well
	const bool bLeaves = JeebCanopy::Cover(Foot.X, Foot.Y) > 0.3f && LeavesA.Num() > 0;
	const FVector Back = -GetActorForwardVector(), Side = GetActorRightVector();
	auto Launch = [&](FBit& B, float Up, float Out, float SpinMax)
	{
		B.Pos = Foot + Side * FMath::FRandRange(-5.f, 5.f) + Back * FMath::FRandRange(0.f, 6.f) + FVector(0.f, 0.f, 2.f);
		B.Vel = Back * FMath::FRandRange(0.4f, 1.f) * Out * Strength + Side * FMath::FRandRange(-0.45f, 0.45f) * Out + FVector(0.f, 0.f, FMath::FRandRange(0.6f, 1.f) * Up * FMath::Sqrt(Strength));
		B.Rot = FQuat(FMath::VRand(), FMath::FRandRange(0.f, 6.28f));
		B.Spin = FMath::VRand() * FMath::FRandRange(0.4f, 1.f) * SpinMax;
		B.Floor = Foot.Z;
		B.Age = 0.f;
	};
	if (G == 1)
	{
		// a dirt path: a few small clods and a faint puff of dust
		const int32 N = FMath::RoundToInt(4.f + 3.f * Strength);
		for (int32 i = 0; i < N; ++i)
		{
			FBit& B = Clods[ClodNext];
			ClodNext = (ClodNext + 1) % Clods.Num();
			Launch(B, 230.f, 130.f, 14.f);
			const float S = FMath::FRandRange(2.2f, 4.2f) * 0.01f;   // chunks of 2 to 4 cm
			B.Size = FVector(S, S * FMath::FRandRange(0.7f, 1.2f), S * FMath::FRandRange(0.6f, 1.f));
			B.Life = FMath::FRandRange(0.8f, 1.2f);
		}
		// (no round puff: just the soil itself)
	}
	else if (!bLeaves)
	{
		// grass: a blade or two flicked up (three on a hard landing)
		const int32 N = Strength >= 1.4f ? 5 : (FMath::FRand() < 0.5f ? 2 : 3);
		for (int32 i = 0; i < N; ++i)
		{
			FBit& B = Blades[BladeNext];
			BladeNext = (BladeNext + 1) % Blades.Num();
			Launch(B, 260.f, 90.f, 11.f);
			B.Size = FVector(0.008f, FMath::FRandRange(0.024f, 0.034f), FMath::FRandRange(0.12f, 0.17f));   // 12-17 cm blades
			B.Life = FMath::FRandRange(1.0f, 1.4f);
		}
	}
	if (bLeaves)
	{
		// a little flurry of orange and red leaves that flutter back down
		const int32 N = Strength >= 1.4f ? 8 : FMath::RandRange(3, 5);
		for (int32 i = 0; i < N; ++i)
		{
			TArray<FBit>& Pile = (i % 3 == 2) ? LeavesB : LeavesA;
			FBit& B = Pile[(LeafNext + i) % Pile.Num()];
			Launch(B, 210.f, 85.f, 7.f);
			const float S = FMath::FRandRange(0.9f, 1.3f);
			B.Size = FVector(0.065f * S, 0.05f * S, 0.004f);   // a leaf about 6 by 5 cm
			B.Life = FMath::FRandRange(1.4f, 2.0f);
		}
		LeafNext = (LeafNext + N) % 24;
	}
}

// The kicked-up bits fly, tumble, land and shrink away
void AKnightCharacter::TickBits(TArray<FBit>& Bits, UInstancedStaticMeshComponent* ISM, float Dt, float Gravity, float Drag)
{
	if (!ISM) return;
	for (int32 i = 0; i < Bits.Num(); ++i)
	{
		FBit& B = Bits[i];
		FTransform T(FQuat::Identity, FVector(0.f, 0.f, -9000.f), FVector(0.001f));
		if (B.Age < B.Life)
		{
			B.Age += Dt;
			if (B.Pos.Z > B.Floor + 0.6f || B.Vel.Z > 0.f)
			{
				B.Vel.Z -= Gravity * Dt;
				B.Vel *= FMath::Max(0.f, 1.f - Drag * Dt);
				B.Pos += B.Vel * Dt;
				if (!B.Spin.IsNearlyZero()) B.Rot = FQuat(B.Spin.GetSafeNormal(), B.Spin.Size() * Dt) * B.Rot;
				if (B.Pos.Z < B.Floor + 0.6f) { B.Pos.Z = B.Floor + 0.6f; B.Vel = FVector::ZeroVector; B.Spin = FVector::ZeroVector; }   // landed
			}
			const float Fade = FMath::Clamp((B.Life - B.Age) / (0.3f * B.Life), 0.f, 1.f);
			T = FTransform(B.Rot, B.Pos, B.Size * FMath::Max(Fade, 0.01f));
		}
		ISM->UpdateInstanceTransform(i, T, true, i == Bits.Num() - 1, true);
	}
}

