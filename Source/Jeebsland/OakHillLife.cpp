#include "OakHillLife.h"
#include "OakHillTown.h"
#include "OakHill.h"
#include "DayNight.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// ---- Tuning ----
	constexpr int32 PuffsPerStack = 10;
	constexpr float PuffLife = 8.0f;        // seconds from the flue to gone
	constexpr float RiseSpeed = 110.f;       // cm/s, slowing as it cools
	constexpr float PuffTop = 0.36f;        // opacity of the densest puff
	constexpr float ShowDistance = 15000.f; // beyond this the smoke is switched off (150 m)
	// The glow of lit windows and the forge: low in the daytime, fuller as the sun nears the horizon (ADayNight::Dusk, 0..1).
	// Multiplies each material's own GlowStrength, so the houses stay 35% dimmer than the shops (building_import.py GLOW_BY_NAME).
	constexpr float GlowAtNoon = 0.55f, GlowAtDusk = 1.35f;
	// Shop-front lamps: a warm light that is barely there at noon and full at the low sun
	constexpr float LampAtNoon = 0.15f, LampAtDusk = 1.0f;

	// One chimney, measured on the models (Blender metres in the model's own frame, scale 1: the top of the chimney pot). Unreal's
	// local frame is X, -Y, Z in cm; the components' own scale and turn come from the town.
	// Kind: 0 = a house (some smoke, some don't), 1 = the smith's forge (always, bigger and greyer), 2 = a hall or shop (always)
	struct FChimney { const TCHAR* Mesh; float X, Y, Z; int32 Kind; };
	const FChimney Chimneys[] = {
		{ TEXT("SM_OakHillHouse_300k"),       -5.53f,  0.46f,  7.93f, 0 },
		{ TEXT("SM_OakHillCornerHouse_300k"), -0.79f,  0.34f, 11.39f, 0 },
		{ TEXT("SM_OakHillSmith_300k"),       -3.21f, -1.32f, 12.24f, 1 },
		{ TEXT("SM_OakHillStore_300k"),       -0.10f,  2.83f, 13.24f, 2 },
		{ TEXT("SM_OakHillTownHall_300k"),     7.35f,  1.18f, 13.71f, 2 },   // (the hall's other end, at -7.37, stays cold)
	};

	// Shops that get a lamp just outside the door (which way the door faces comes from OakHill.h: DoorYaw, and the walls' extent that way)
	const TCHAR* LitShops[] = { TEXT("SM_OakHillSmith_300k"), TEXT("SM_OakHillStore_300k"), TEXT("SM_OakHillBank_300k"), TEXT("SM_OakHillPotionShop_300k"), TEXT("SM_OakHillTownHall_300k") };

	float Hash01(uint32 N) { N ^= N >> 16; N *= 0x7feb352dU; N ^= N >> 15; N *= 0x846ca68bU; N ^= N >> 16; return float(N & 0xffffff) / float(0x1000000); }
	float Smooth(float A, float B, float X) { const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f); return T * T * (3.f - 2.f * T); }
}

AOakHillLife::AOakHillLife()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Puffs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Puffs"));
	Puffs->SetupAttachment(RootComponent);
	Puffs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Puffs->SetCastShadow(false);
	Puffs->bAffectDynamicIndirectLighting = false;
	Puffs->bAffectDistanceFieldLighting = false;
	Puffs->NumCustomDataFloats = 4;   // tint r, g, b, and opacity (M_ChimneySmoke reads them)
	ConstructorHelpers::FObjectFinder<UStaticMesh> Ball(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Ball.Succeeded()) Puffs->SetStaticMesh(Ball.Object);
}

void AOakHillLife::BeginPlay()
{
	Super::BeginPlay();
	if (IsNetMode(NM_DedicatedServer)) { SetActorTickEnabled(false); return; }
	SetUp();
}

void AOakHillLife::SetUp()
{
	AOakHillTown* Town = nullptr;
	for (TActorIterator<AOakHillTown> It(GetWorld()); It; ++It) { Town = *It; break; }
	if (!Town) return;   // (-NoOakHill: no town, nothing to do)
	bSetUp = true;

	UMaterialInterface* SmokeMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_ChimneySmoke.M_ChimneySmoke"));
	if (SmokeMat) Puffs->SetMaterial(0, SmokeMat);
	else UE_LOG(LogTemp, Warning, TEXT("JEEBLIFE M_ChimneySmoke is missing: run Tools/BuildTownLifeMaterials.ps1. No chimney smoke."));

	TInlineComponentArray<UStaticMeshComponent*> Comps;
	Town->GetComponents(Comps);
	FVector Sum = FVector::ZeroVector;
	int32 NumBuilt = 0, NumSmoking = 0, NumLamps = 0;
	for (UStaticMeshComponent* C : Comps)
	{
		UStaticMesh* Mesh = C->GetStaticMesh();
		if (!Mesh || C->IsA<UInstancedStaticMeshComponent>()) continue;
		const FString Name = Mesh->GetName();
		++NumBuilt;
		Sum += C->GetComponentLocation();
		const FTransform Xf = C->GetComponentTransform();
		for (const FChimney& Ch : Chimneys)
		{
			if (Name != Ch.Mesh) continue;
			const FVector Top = Xf.TransformPosition(FVector(Ch.X * 100.f, -Ch.Y * 100.f, Ch.Z * 100.f));
			// Not every chimney smokes: a house does with a chance of 70% (the same on every machine, by where it stands)
			const uint32 Seed = uint32(FMath::RoundToInt(Top.X * 0.37f)) * 73856093u ^ uint32(FMath::RoundToInt(Top.Y * 0.37f)) * 19349663u;
			if (Ch.Kind == 0 && Hash01(Seed) > 0.70f) continue;
			FStack S;
			S.Base = Top - FVector(0.f, 0.f, 12.f);   // out of the flue, a little below the rim
			S.Phase = Hash01(Seed + 5u);
			S.Strength = Ch.Kind == 1 ? 1.0f : (Ch.Kind == 2 ? 0.8f : 0.65f + 0.2f * Hash01(Seed + 9u));
			S.Size = Ch.Kind == 1 ? 1.35f : 1.0f;
			S.Dark = Ch.Kind == 1 ? 0.62f : 1.0f;   // the forge's smoke is greyer
			Stacks.Add(S);
			++NumSmoking;
		}
		// a lamp at the shop front
		for (const TCHAR* Shop : LitShops)
		{
			if (Name != Shop) continue;
			FString Asset = Name.Mid(3);   // "OakHillSmith_300k"
			Asset.RemoveFromEnd(TEXT("_300k"));
			const JeebOakHill::FModel* Mdl = JeebOakHill::FindModel(*Asset);
			if (!Mdl) continue;
			// the door faces the component's yaw plus DoorYaw; it stands at the walls' edge on that side
			const float Door = C->GetComponentRotation().Yaw + Mdl->DoorYaw;
			const float Half = (Mdl->DoorYaw > 0.f ? -Mdl->WY0 : Mdl->WY1) * Mdl->Scale;   // Unreal's Y is Blender's -Y
			const FVector Dir = FRotator(0.f, Door, 0.f).Vector();
			const FVector P = C->GetComponentLocation() + Dir * (Half + 90.f) + FVector(0.f, 0.f, 260.f);
			UPointLightComponent* L = NewObject<UPointLightComponent>(this);
			L->SetupAttachment(RootComponent);
			L->SetWorldLocation(P);
			L->SetIntensity(0.f);
			L->SetLightColor(Name.Contains(TEXT("Smith")) ? FLinearColor(1.0f, 0.45f, 0.14f) : FLinearColor(1.0f, 0.66f, 0.30f));
			L->SetAttenuationRadius(800.f);
			L->SetCastShadows(false);
			L->RegisterComponent();
			Lamps.Add(L);
			FLamp Lp; Lp.Base = Name.Contains(TEXT("Smith")) ? 5200.f : 3600.f; Lp.Phase = Hash01(uint32(NumLamps) + 31u) * 100.f;
			LampData.Add(Lp);
			++NumLamps;
		}
	}
	if (NumBuilt) Centre = Sum / float(NumBuilt);

	if (SmokeMat)
		for (int32 s = 0; s < Stacks.Num(); ++s)
			for (int32 i = 0; i < PuffsPerStack; ++i)
				Puffs->AddInstance(FTransform(FRotator::ZeroRotator, Stacks[s].Base, FVector(0.001f)), true);
	SetUpGlow(Town);
	UE_LOG(LogTemp, Log, TEXT("JEEBLIFE %d buildings, %d chimneys smoking (%d puffs), %d shop lamps, %d glow materials"), NumBuilt, NumSmoking, Puffs->GetInstanceCount(), NumLamps, GlowMids.Num());
}

// Each distinct building material gets one dynamic copy that all the buildings of that design share, so the glow can follow the sun
// without giving every house its own (Nanite) material
void AOakHillLife::SetUpGlow(AOakHillTown* Town)
{
	TInlineComponentArray<UStaticMeshComponent*> Comps;
	Town->GetComponents(Comps);
	TMap<UMaterialInterface*, int32> Done;
	for (UStaticMeshComponent* C : Comps)
	{
		UStaticMesh* Mesh = C->GetStaticMesh();
		if (!Mesh || C->IsA<UInstancedStaticMeshComponent>() || !Mesh->GetPathName().Contains(TEXT("/Game/Buildings/"))) continue;
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			UMaterialInterface* M = C->GetMaterial(i);
			if (!M) continue;
			int32* Got = Done.Find(M);
			if (!Got)
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(M, this);
				const float Base = Mid->K2_GetScalarParameterValue(TEXT("GlowStrength"));
				GlowMids.Add(Mid);
				FGlowMat G; G.Mid = GlowMids.Num() - 1; G.Base = Base;
				GlowBase.Add(G);
				Got = &Done.Add(M, GlowMids.Num() - 1);
			}
			C->SetMaterial(i, GlowMids[*Got]);
		}
	}
}

void AOakHillLife::UpdateGlow()
{
	const float D = ADayNight::Dusk;
	if (FMath::Abs(D - LastGlowDusk) < 0.005f) return;
	LastGlowDusk = D;
	const float F = FMath::Lerp(GlowAtNoon, GlowAtDusk, D);
	for (const FGlowMat& G : GlowBase) if (G.Base > 0.f) GlowMids[G.Mid]->SetScalarParameterValue(TEXT("GlowStrength"), G.Base * F);
}

void AOakHillLife::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bSetUp) { SetUp(); if (!bSetUp) return; }
	UpdateGlow();
	const float T = GetWorld()->GetTimeSeconds();
	const float D = ADayNight::Dusk;

	// shop-front lamps: a living flicker, brighter as the sun lowers
	const float LampLevel = FMath::Lerp(LampAtNoon, LampAtDusk, D);
	for (int32 i = 0; i < Lamps.Num(); ++i)
	{
		const float Ph = LampData[i].Phase;
		const float W = 0.5f * FMath::Sin(T * 6.1f + Ph) + 0.3f * FMath::Sin(T * 11.7f + Ph * 2.3f) + 0.2f * FMath::Sin(T * 2.3f + Ph * 0.7f);
		Lamps[i]->SetIntensity(LampData[i].Base * LampLevel * (1.f + 0.14f * W));
	}

	// chimney smoke: only when the player is near (it is a small town, but the smoke is for whoever is looking)
	if (Puffs->GetInstanceCount() == 0) return;
	FVector Cam = Centre;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
	const bool bNear = FVector::DistSquared(Cam, Centre) < FMath::Square(ShowDistance);
	if (bNear != bPuffsOn) { bPuffsOn = bNear; Puffs->SetVisibility(bNear); }
	if (!bNear) return;

	// The light of the hour tints the smoke (white at noon, soft gold at the low sun)
	const FLinearColor Light = ADayNight::TrailLight;
	const FVector2D Wind(0.94f, 0.34f);   // downwind, the same way the grass leans (build_materials.py add_wind)
	int32 N = 0;
	const int32 Last = Puffs->GetInstanceCount() - 1;
	for (int32 s = 0; s < Stacks.Num(); ++s)
	{
		const FStack& S = Stacks[s];
		for (int32 i = 0; i < PuffsPerStack; ++i, ++N)
		{
			const float Slot = (float(i) + 0.35f * FMath::Sin(float(i) * 12.9898f + S.Phase * 78.233f)) / PuffsPerStack;
			const float X = T / PuffLife + Slot + S.Phase;
			const float Cycle = FMath::FloorToFloat(X);
			const float U = X - Cycle;                           // 0 at the flue, 1 gone
			const float Age = U * PuffLife;
			const float Born = (Cycle - Slot - S.Phase) * PuffLife;   // when this puff left the flue: it keeps the wind of that moment
			// breeze at that moment: a slow, uneven swell
			const float Gust = 0.5f + 0.5f * FMath::Sin(Born * 0.23f + S.Phase * 6.f) * FMath::Sin(Born * 0.11f + 1.3f);
			const float Speed = 18.f + 45.f * FMath::Clamp(Gust, 0.f, 1.f);   // cm/s
			const float Bend = FMath::Pow(U, 1.25f);                           // it climbs straight at first, then the wind takes it
			const FVector Drift(Wind.X * Speed * PuffLife * Bend, Wind.Y * Speed * PuffLife * Bend, 0.f);
			const float Rise = RiseSpeed * Age * (1.f - 0.30f * U);
			const float Wobble = FMath::Sin(Age * 1.15f + float(i) * 2.1f + S.Phase * 9.f) * 14.f * U;
			const FVector Pos = S.Base + Drift + FVector(-Wind.Y * Wobble, Wind.X * Wobble, Rise);
			const float Size = FMath::Lerp(0.30f, 1.75f, FMath::Pow(U, 0.8f)) * S.Size * (0.85f + 0.3f * Hash01(uint32(i) * 97u + uint32(S.Phase * 1000.f)));
			const float Op = PuffTop * S.Strength * Smooth(0.f, 0.12f, U) * (1.f - Smooth(0.12f, 0.95f, U));
			const FLinearColor Col = FLinearColor(0.82f, 0.82f, 0.84f) * Light * S.Dark;
			const float Custom[4] = { Col.R, Col.G, Col.B, Op };
			Puffs->SetCustomData(N, TArrayView<const float>(Custom, 4), false);
			Puffs->UpdateInstanceTransform(N, FTransform(FRotator(0.f, Age * 12.f, 0.f), Pos, FVector(Size)), true, N == Last, false);
		}
	}
}
