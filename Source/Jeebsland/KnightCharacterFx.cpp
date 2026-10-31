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

// Dust puffs, clods, grass blades and leaves (Tick step; S and C are where the stride is)
void AKnightCharacter::TickDust(float Dt, bool bFalling, bool bSpecial, float S, float C)
{
	// ---------- Dust puffs: footfalls when running, landings, slides ----------
	const FVector FeetBase = GetActorLocation() + FVector(0.f, 0.f, -94.f);
	if (!bFalling && !bSpecial && MoveBlend > 0.6f && RunAlpha > 0.3f && ((C > 0.f) != (PrevC > 0.f)))
	{
		const float Side = (S > 0.f) ? 13.f : -13.f;
		Kick(FeetBase + GetActorForwardVector() * 8.f + GetActorRightVector() * Side, 0.7f + 0.4f * RunAlpha);
	}
	PrevC = C;
	if (!bFalling && bWasFalling == false && LandTimer > LandDuration - Dt * 1.5f && LandImpact > 0.45f)
	{
		Kick(FeetBase + GetActorRightVector() * 13.f, 0.8f + 1.0f * LandImpact);
		Kick(FeetBase - GetActorRightVector() * 13.f, 0.8f + 1.0f * LandImpact);
	}
	if (bSliding)
	{
		SlideDustTimer += Dt;
		if (SlideDustTimer > 0.12f)
		{
			SlideDustTimer = 0.f;
			Kick(FeetBase - SlideDir * 20.f + GetActorRightVector() * FMath::FRandRange(-15.f, 15.f), 0.6f);
		}
	}
	for (int32 i = 0; i < Dust.Num(); ++i)
	{
		FDust& D = Dust[i];
		D.Age += Dt;
		FTransform T(FQuat::Identity, FVector(0.f, 0.f, -9000.f), FVector(0.001f));
		if (D.Age < D.Life)
		{
			const float K = D.Age / D.Life;
			const float Sz = D.Size * (0.4f + 0.9f * FMath::Sqrt(K)) * (1.f - K * K) / 100.f;
			T = FTransform(FQuat::Identity, D.Pos + FVector(0.f, 0.f, 30.f * K), FVector(FMath::Max(Sz, 0.001f)));
		}
		DustISM->UpdateInstanceTransform(i, T, true, i == Dust.Num() - 1, true);
	}
	TickBits(Clods, ClodISM, Dt, 980.f, 0.6f);    // clods: heavy, they drop straight back
	TickBits(Blades, BladeISM, Dt, 520.f, 2.4f);  // blades: light, they flutter down
	TickBits(LeavesA, LeafAISM, Dt, 380.f, 3.2f);  // leaves: lightest, they drift down
	TickBits(LeavesB, LeafBISM, Dt, 380.f, 3.2f);
}

// The wind trail behind a sword cut (BotW style). Every frame of a sword slash the blade's middle and tip are noted; the last
// moments of that path are drawn as a soft ribbon (material M_SwordTrail), smoothed between frames, fading with age and only
// where the blade was really moving (a slow wind-up leaves no smear). Purely visual: each machine draws its own.
void AKnightCharacter::TickSwordTrail(float Dt)
{
	if (IsNetMode(NM_DedicatedServer)) return;
	if (!SwordTrail)
	{
		SwordTrail = NewObject<UProceduralMeshComponent>(this, TEXT("SwordTrail"));
		SwordTrail->SetupAttachment(RootComponent);
		SwordTrail->SetUsingAbsoluteLocation(true); SwordTrail->SetUsingAbsoluteRotation(true); SwordTrail->SetUsingAbsoluteScale(true);
		SwordTrail->SetWorldTransform(FTransform::Identity);   // (its points are world positions)
		SwordTrail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SwordTrail->SetCastShadow(false);
		SwordTrail->RegisterComponent();
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_SwordTrail.M_SwordTrail"))) SwordTrail->SetMaterial(0, M);
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const float Slow = SwingSpeedScale();
	const float Life = 0.09f / Slow;   // how long a bit of the trail lasts (longer in slow motion)
	if (bAttacking && AtkStyle == EAtkStyle::Slash && SelectedKind() == JeebItems::K_Sword && WristR)
	{
		const FTransform W = WristR->GetComponentTransform();
		auto Along = [&](float Cm) { return W.TransformPosition(KnightDesign::GripPos(false) + KnightDesign::BladeDir(1) * Cm - KnightDesign::PivotPos(KnightDesign::P_WristR)); };
		const FVector Tip = Along(79.5f), In = Along(67.f);   // a thin band at the tip (the sword's tip is 79.5 cm from the grip)
		const float Speed = bTrailPrev ? (Tip - TrailPrevTip).Size() / FMath::Max(Dt, 0.0001f) / Slow : 0.f;   // cm/s at normal speed
		TrailPrevTip = Tip; bTrailPrev = true;
		// Only the cut itself leaves a trail (owner, 2026-10-31): hit 1's forward arc and hit 2's backward arc, from the end of
		// the wind-up to the slowing of the follow-through (the parts of each swing in PoseArms' key tables), faded in and out
		const float T = AttackT / FMath::Max(AttackDur, 0.01f);
		const float From = (AttackDir == JabSecond) ? 0.30f : 0.28f, To = (AttackDir == JabSecond) ? 0.74f : 0.64f;
		const float Window = FMath::SmoothStep(From, From + 0.05f, T) * (1.f - FMath::SmoothStep(To - 0.08f, To, T));
		FTrailSample S; S.In = In; S.Out = Tip; S.Time = Now; S.Strength = Window * FMath::Clamp((Speed - 250.f) / 500.f, 0.f, 1.f);
		TrailSamples.Add(S);
	}
	else bTrailPrev = false;
	TrailSamples.RemoveAll([&](const FTrailSample& S) { return Now - S.Time > Life; });
	if (TrailSamples.Num() < 2) { if (SwordTrail->GetNumSections() > 0) SwordTrail->ClearAllMeshSections(); return; }

	// Smooth curves through the noted points (Catmull-Rom), four steps between each pair
	TArray<FVector> V; TArray<FVector2D> UV; TArray<FLinearColor> Col; TArray<int32> Tri;
	auto CR = [](const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float T)
	{
		const float T2 = T * T, T3 = T2 * T;
		return 0.5f * ((2.f * P1) + (-P0 + P2) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 + (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3);
	};
	const int32 N = TrailSamples.Num(), Steps = 4;
	for (int32 i = 0; i < N - 1; ++i)
	{
		const FTrailSample& A = TrailSamples[FMath::Max(i - 1, 0)];
		const FTrailSample& B = TrailSamples[i];
		const FTrailSample& C = TrailSamples[i + 1];
		const FTrailSample& D = TrailSamples[FMath::Min(i + 2, N - 1)];
		for (int32 s = 0; s < Steps || (i == N - 2 && s == Steps); ++s)
		{
			const float T = float(s) / Steps;
			const float Age = FMath::Clamp((Now - FMath::Lerp(B.Time, C.Time, T)) / Life, 0.f, 1.f);
			const float Alpha = FMath::Lerp(B.Strength, C.Strength, T) * FMath::Pow(1.f - Age, 1.5f);
			V.Add(CR(A.In, B.In, C.In, D.In, T));   UV.Add(FVector2D(Age, 0.f)); Col.Add(FLinearColor(1.f, 1.f, 1.f, Alpha));
			V.Add(CR(A.Out, B.Out, C.Out, D.Out, T)); UV.Add(FVector2D(Age, 1.f)); Col.Add(FLinearColor(1.f, 1.f, 1.f, Alpha));
		}
	}
	for (int32 k = 0; k + 3 < V.Num(); k += 2) Tri.Append({ k, k + 1, k + 3, k, k + 3, k + 2 });
	SwordTrail->CreateMeshSection_LinearColor(0, V, Tri, TArray<FVector>(), UV, Col, TArray<FProcMeshTangent>(), false);
}
