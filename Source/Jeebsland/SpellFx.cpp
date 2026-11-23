#include "SpellFx.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

static UStaticMesh* FxBall() { static TWeakObjectPtr<UStaticMesh> M; if (!M.IsValid()) M = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")); return M.Get(); }
static UMaterialInterface* FxMat() { static TWeakObjectPtr<UMaterialInterface> M; if (!M.IsValid()) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Props/M_GlowOrb.M_GlowOrb")); return M.Get(); }

ASpellFx::ASpellFx()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASpellFx::Play(UWorld* World, ESpellFx Kind, const FVector& At, const FVector& Dir, const FLinearColor& Color)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer || !FxBall()) return;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ASpellFx* F = World->SpawnActor<ASpellFx>(ASpellFx::StaticClass(), At, FRotator::ZeroRotator, SP))
	{
		F->Kind = Kind; F->Color = Color; F->Dir = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir.GetSafeNormal();
		F->Start();
	}
}

ASpellFx::FBit& ASpellFx::Add(float Size)
{
	FBit B;
	B.C = NewObject<UStaticMeshComponent>(this);
	B.C->SetStaticMesh(FxBall());
	B.C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	B.C->SetCastShadow(false);
	B.C->SetupAttachment(RootComponent);
	B.C->SetUsingAbsoluteLocation(true); B.C->SetUsingAbsoluteRotation(true); B.C->SetUsingAbsoluteScale(true);
	B.C->RegisterComponent();
	B.C->SetWorldLocation(GetActorLocation());
	B.C->SetWorldScale3D(FVector(0.001f));
	if (UMaterialInterface* Mat = FxMat()) B.M = B.C->CreateDynamicMaterialInstance(0, Mat);
	B.Size = Size;
	Bits.Add(B);
	return Bits.Last();
}

void ASpellFx::Start()
{
	FRandomStream R(int32(GetActorLocation().X * 7.f + GetActorLocation().Y * 13.f));
	switch (Kind)
	{
	case ESpellFx::Muzzle: Life = 0.14f; Add(0.22f); break;
	case ESpellFx::Spark:
		Life = 0.6f;
		for (int32 i = 0; i < 2; ++i) { FBit& S = Add(R.FRandRange(0.02f, 0.035f)); S.Kind = 2; S.V = (Dir * 0.6f + R.GetUnitVector()).GetSafeNormal() * R.FRandRange(60.f, 180.f); }
		break;
	case ESpellFx::Smoke:
		Life = 0.8f;
		{ FBit& P = Add(R.FRandRange(0.14f, 0.2f)); P.Kind = 3; P.V = R.GetUnitVector() * 15.f + FVector(0.f, 0.f, 25.f); }
		break;
	case ESpellFx::Burst:
		// (owner's picture 144: a white-hot core flash, jagged lightning tendrils bursting out, a spray of fine sparks)
		Life = 0.34f;
		Add(0.4f);                  // the flash
		Add(0.8f);                  // a faint shell
		for (int32 t = 0; t < 8; ++t)   // tendrils: four jagged segments each, out to 60-130 cm, mostly back toward where the spell came from
		{
			FVector D = (R.GetUnitVector() - Dir * 0.5f).GetSafeNormal();
			const float Reach = R.FRandRange(60.f, 130.f);
			FVector P = FVector::ZeroVector;
			for (int32 s = 0; s < 4; ++s)
			{
				const FVector Kink = (D + R.GetUnitVector() * 0.55f).GetSafeNormal();
				const FVector Q = P + Kink * Reach * 0.25f;
				FBit& L = Add(1.f);
				L.Kind = 1; L.A = P; L.B = Q; L.Z = (s + 0.5f) / 4.f; L.Size = FMath::Lerp(2.2f, 0.8f, s / 3.f);   // (cm thick)
				P = Q;
			}
		}
		for (int32 i = 0; i < 14; ++i) { FBit& S = Add(R.FRandRange(0.015f, 0.03f)); S.Kind = 2; S.V = (R.GetUnitVector() - Dir * 0.4f).GetSafeNormal() * R.FRandRange(500.f, 1100.f); }
		break;
	case ESpellFx::FireBurst:
		// (2026-11-22: the Fire Bolt) a yellow-hot flash, an orange fireball swelling out and fading, a few puffs of flame round it, embers drifting up
		Life = 0.55f;
		Add(0.55f);                 // the flash
		Add(1.1f);                  // the fireball shell
		for (int32 i = 0; i < 6; ++i) { FBit& P = Add(R.FRandRange(0.25f, 0.4f)); P.Kind = 3; P.V = (R.GetUnitVector() - Dir * 0.6f).GetSafeNormal() * R.FRandRange(120.f, 240.f); }
		for (int32 i = 0; i < 16; ++i) { FBit& S = Add(R.FRandRange(0.015f, 0.03f)); S.Kind = 2; S.V = (R.GetUnitVector() - Dir * 0.5f + FVector(0.f, 0.f, 0.6f)).GetSafeNormal() * R.FRandRange(250.f, 600.f); }
		break;
	case ESpellFx::WarpOut:
	case ESpellFx::WarpIn:
		Life = 0.45f;
		Add(1.f);   // the column (stretched up and down his height)
		for (int32 i = 0; i < 12; ++i) { FBit& W = Add(R.FRandRange(0.12f, 0.22f)); W.Ang = R.FRandRange(0.f, 2.f * PI); W.R = R.FRandRange(60.f, 110.f); W.Z = R.FRandRange(-80.f, 80.f); }
		break;
	}
	Tick(0.f);
}

void ASpellFx::Tick(float Dt)
{
	Super::Tick(Dt);
	T += Dt;
	const float U = FMath::Clamp(T / Life, 0.f, 1.f);
	if (U >= 1.f) { Destroy(); return; }
	const FVector O = GetActorLocation();
	auto Set = [&](FBit& B, const FVector& P, const FVector& S, float Strength, const FLinearColor& C)
	{
		if (!B.C) return;
		B.C->SetWorldLocation(P);
		B.C->SetWorldScale3D(S.ComponentMax(FVector(0.001f)));
		if (B.M) { B.M->SetVectorParameterValue(TEXT("Color"), C); B.M->SetScalarParameterValue(TEXT("Strength"), FMath::Max(0.f, Strength)); }
	};
	const FLinearColor Hot = FMath::Lerp(Color, FLinearColor::White, 0.45f);
	const FLinearColor Smoke(0.05f, 0.12f, 0.55f);   // the dark-blue swirl (additive: it reads as a deep blue glow)
	switch (Kind)
	{
	case ESpellFx::Muzzle:
		Set(Bits[0], O, FVector(Bits[0].Size * FMath::Lerp(0.4f, 1.f, FMath::Sqrt(U))), 1.4f * (1.f - U), Hot);
		break;
	case ESpellFx::Burst:
		Set(Bits[0], O, FVector(Bits[0].Size * FMath::Lerp(0.3f, 1.f, FMath::Sqrt(U))), 1.6f * FMath::Square(1.f - U), Hot);
		Set(Bits[1], O, FVector(Bits[1].Size * FMath::Sqrt(U)), 0.35f * (1.f - U), Color);
		for (int32 i = 2; i < Bits.Num(); ++i)
		{
			FBit& S = Bits[i];
			if (S.Kind == 1)
			{
				// a lightning segment: shows once the tendril's growth (over the first 0.07 s) reaches it, crackles (flickers) and fades
				const float Grow = FMath::Clamp(T / 0.07f, 0.f, 1.f);
				if (Grow < S.Z) { Set(S, O, FVector(0.f), 0.f, Hot); continue; }
				const FVector Seg = S.B - S.A;
				const float Flick = (FMath::Frac(T * 37.f + S.Z * 3.1f + i * 0.37f) > 0.25f) ? 1.f : 0.25f;
				const float Thick = S.Size / 100.f * (1.f - 0.6f * U);
				if (S.C) S.C->SetWorldRotation(Seg.Rotation());
				Set(S, O + (S.A + S.B) * 0.5f, FVector(Seg.Size() / 100.f, Thick, Thick), 1.5f * Flick * FMath::Square(1.f - U), FMath::Lerp(Hot, FLinearColor::White, 0.3f * (1.f - S.Z)));
			}
			else
			{
				S.V *= FMath::Max(0.f, 1.f - 3.f * Dt);
				S.V.Z -= 500.f * Dt;
				Set(S, S.C->GetComponentLocation() + S.V * Dt, FVector(S.Size * (1.f - U)), 1.3f * (1.f - U), Hot);
			}
		}
		break;
	case ESpellFx::Spark:
		for (FBit& S : Bits)
		{
			S.V *= FMath::Max(0.f, 1.f - 2.f * Dt);
			S.V.Z -= 250.f * Dt;
			const float Tw = (FMath::Frac(T * 9.f + S.Size * 40.f) > 0.3f) ? 1.f : 0.5f;   // (a slow twinkle, not a per-frame flicker: TSR)
			Set(S, S.C->GetComponentLocation() + S.V * Dt, FVector(S.Size * (1.f - 0.6f * U)), 1.4f * Tw * (1.f - U), Hot);
		}
		break;
	case ESpellFx::Smoke:
	{
		FBit& P = Bits[0];
		const FLinearColor Dim = FMath::Lerp(FLinearColor(0.25f, 0.25f, 0.28f), Color, 0.35f);
		Set(P, P.C->GetComponentLocation() + P.V * Dt, FVector(P.Size * FMath::Lerp(0.5f, 1.6f, FMath::Sqrt(U))), 0.12f * FMath::Sin(PI * FMath::Min(1.f, U * 1.3f + 0.15f)), Dim);
		break;
	}
	case ESpellFx::FireBurst:
	{
		const FLinearColor Yellow(1.f, 0.8f, 0.3f);
		Set(Bits[0], O, FVector(Bits[0].Size * FMath::Lerp(0.4f, 1.f, FMath::Sqrt(U))), 1.8f * FMath::Square(1.f - U), Yellow);
		Set(Bits[1], O + FVector(0.f, 0.f, 30.f * U), FVector(Bits[1].Size * FMath::Lerp(0.2f, 1.f, FMath::Sqrt(U))), 0.6f * (1.f - U), Color);
		for (int32 i = 2; i < Bits.Num(); ++i)
		{
			FBit& S = Bits[i];
			S.V *= FMath::Max(0.f, 1.f - 4.f * Dt);
			S.V.Z += (S.Kind == 3 ? 260.f : 140.f) * Dt;   // (flame and embers rise)
			const FLinearColor C = S.Kind == 3 ? FMath::Lerp(Yellow, Color, U) : Yellow;
			const float Grow = S.Kind == 3 ? FMath::Lerp(0.5f, 1.2f, U) : (1.f - U);
			Set(S, S.C->GetComponentLocation() + S.V * Dt, FVector(S.Size * Grow), (S.Kind == 3 ? 0.7f : 1.3f) * (1.f - U), C);
		}
		break;
	}
	case ESpellFx::WarpOut:
	case ESpellFx::WarpIn:
	{
		// Out: everything closes on the point at his middle (the column thins and shortens to nothing, the wisps spiral in and down to it).
		// In: the same played backwards from the new spot, with the wisps spiralling out and fading.
		const float K = Kind == ESpellFx::WarpOut ? U : 1.f - U;               // 0 open, 1 closed to the point
		const float Fade = Kind == ESpellFx::WarpOut ? (U < 0.8f ? 1.f : (1.f - U) / 0.2f) : (1.f - U);
		const float Squeeze = FMath::InterpEaseIn(0.f, 1.f, K, 2.f);
		Set(Bits[0], O, FVector(FMath::Lerp(0.55f, 0.02f, Squeeze), FMath::Lerp(0.55f, 0.02f, Squeeze), FMath::Lerp(1.9f, 0.05f, Squeeze)), 0.5f * Fade, Color);
		for (int32 i = 1; i < Bits.Num(); ++i)
		{
			FBit& W = Bits[i];
			const float A = W.Ang + (Kind == ESpellFx::WarpOut ? 1.f : -1.f) * 9.f * T;
			const float Rr = W.R * (1.f - Squeeze);
			Set(W, O + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, W.Z * (1.f - Squeeze)), FVector(W.Size * FMath::Lerp(1.f, 0.3f, Squeeze)), 0.45f * Fade, Smoke);
		}
		break;
	}
	}
}
