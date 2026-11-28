// Light in the dark (2026-11-27, owner: "a spell that makes the staff or wand light up ... so the wizard can get light in the tunnel", and "the
// ability for a knight or troll or wizard to hold a torch (smithed from coal and wood) ... a general item that lights the way").
//
// The server decides both and replicates LightBits (1 a torch lit, 2 Glimmer on) to everyone:
//  - The torch (item 941, Inventory.h IsTorch): L, or Equip on one in the pack, calls ToggleTorch. Lighting takes one torch from the pack (or relights the
//    partly burnt one in hand: TorchBurnLeft); it burns Tuning.ini [Light] TorchBurnSeconds of being lit, then it is used up. L again puts it out and
//    keeps the rest (only until he logs out: the rest is not saved). Not with a bow or a two-handed sword in hand (both hands): taking one up puts it out.
//    Held in the LEFT hand, the free one for every class: the knight's shield goes on his back meanwhile (as for a tool) and cannot be raised (Skyrim and
//    Elden Ring do the same: the torch takes the shield's place); the troll's left hand is empty; the wizard's book, ball or pipe is put away meanwhile
//    (his wand or staff stays in his right hand, so he can still cast). The arm takes the wizard's off-hand hold (forearm forward, FKnightAnimator
//    WizHoldArm), so walking never swings it. Smithed at the smithy (EInvOp::Smith with A -3): coal and logs, TorchMakes torches.
//  - Glimmer (Spells.csv row with Look "light"): casting it toggles a soft cool-white orb at the wand's tip or the staff's orb; it drains GlimmerDrain magic
//    a second and goes out at 0 or when cast again, or when he lets go of his wand and staff.
// Every machine but a dedicated server draws them: the torch model (/Game/Props/SM_Torch, Meshy), three camera-facing flame cards (/Game/Effects/M_TorchFire:
// noise-scrolled teardrops, outer red-orange, middle, white-yellow core), a few rising embers and a thin smoke wisp, and Glimmer's orb (M_GlimmerOrb). One
// shadowless point light per player, only while lit (the torch's if both), flickering in step with the flame's height.
#include "KnightCharacter.h"
#include "KnightAnimator.h"
#include "SkillComponent.h"
#include "JeebTuning.h"
#include "JeebProps.h"
#include "JeebSpells.h"
#include "Inventory.h"
#include "Terrain.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

static TAutoConsoleVariable<FString> CVarTorchHold(TEXT("jeeb.Torch.Hold"), TEXT("3,0,-1,-12,0,0,1"),
	TEXT("The torch in the left hand: x,y,z (cm, x forward along the forearm, z up) of its grip from the hand bone, pitch,yaw,roll (degrees; pitch < 0 leans the head forward), scale."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTorchFlameUp(TEXT("jeeb.Torch.FlameUp"), 38.f, TEXT("How far above the grip, along the torch, the flame's foot sits (cm; the head's top is about 48)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTorchFlameSize(TEXT("jeeb.Torch.FlameSize"), 1.f, TEXT("The torch flame's size (x the built-in sizes)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTorchFlameBright(TEXT("jeeb.Torch.FlameBright"), 1.f, TEXT("The torch flame's brightness on screen (x)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTorchCardSpin(TEXT("jeeb.Torch.CardSpin"), 0.f, TEXT("Turns the flame cards about their faces (degrees; for fitting the card's picture upright)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarTorchEmbers(TEXT("jeeb.Torch.Embers"), 1, TEXT("1: embers and a thin smoke wisp rise from the held torch (this machine only); 0: none."), ECVF_Default);
static TAutoConsoleVariable<float> CVarGlimmerSize(TEXT("jeeb.Glimmer.Size"), 1.f, TEXT("Glimmer's orb size (x)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarGlimmerBright(TEXT("jeeb.Glimmer.Bright"), 1.f, TEXT("Glimmer's orb brightness on screen (x)."), ECVF_Default);
#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarTestLight(TEXT("jeeb.Test.Light"), -1,
	TEXT("Single player test pictures (-ShotEyesSet): 1 your torch lit, 2 Glimmer on, 3 both, 0 neither, -1 leave it (no item or magic needed)"));
static TAutoConsoleVariable<int32> CVarLightCrowd(TEXT("jeeb.Test.LightCrowd"), 0,
	TEXT("Single player frame-cost test: N extra knights without a player, each holding a lit torch, in a row 5 m ahead of the camera"));
#endif

namespace
{
	const int32 NumCards = 3, NumEmbers = 7, NumPuffs = 4;
	// The three flame layers: width, height (cm), Core (how hot), brightness, scroll speed, how much each flickers in height
	struct FCard { float W, H, Core, Bright, Speed, Flick; FLinearColor Tint; };
	const FCard Cards[NumCards] = {
		{ 21.f, 38.f, 0.0f, 2.2f, 0.85f, 1.0f, FLinearColor(1.0f, 0.13f, 0.02f) },   // the wide outer flame, red at its edges
		{ 15.f, 29.f, 0.5f, 2.6f, 1.15f, 0.7f, FLinearColor(1.0f, 0.30f, 0.04f) },   // the middle, orange
		{ 8.5f, 16.f, 1.0f, 3.2f, 1.5f, 0.4f, FLinearColor(1.0f, 0.55f, 0.12f) },    // the hot core, white-yellow
	};
	bool ParseHold(FVector& At, FRotator& R, float& Scale)
	{
		TArray<FString> P;
		CVarTorchHold.GetValueOnGameThread().Replace(TEXT("_"), TEXT(",")).ParseIntoArray(P, TEXT(","));
		float V[7] = { 3, 0, -1, -12, 0, 0, 1 };
		for (int32 i = 0; i < FMath::Min(P.Num(), 7); ++i) V[i] = FCString::Atof(*P[i]);
		At = FVector(V[0], V[1], V[2]); R = FRotator(V[3], V[4], V[5]); Scale = FMath::Max(V[6], 0.05f);
		return true;
	}
	bool BlocksTorch(int32 K) { return K >= 0 && (JeebItems::IsShooter(K) || (!JeebItems::AllowsShield(K) && K != JeebItems::K_Staff)); }   // (both hands on it)
}

bool AKnightCharacter::TorchInHand() const
{
	return IsTorchLit() && !bStowed && !bSwinging && !bFishing && !bDrinking && !bOnBroom && !bClimbing && !bSmithOpen;
}

void AKnightCharacter::SetLightBits(uint8 B)
{
	if (B == LightBits) return;
	LightBits = B;
	ForceNetUpdate();
	OnRep_Lights();   // (the server's own picture, on a listen server or single player)
}

void AKnightCharacter::OnRep_Lights()
{
	if (GetNetMode() != NM_DedicatedServer) UpdateLightComps();
	if (GetNetMode() == NM_Client)
		UE_LOG(LogTemp, Log, TEXT("JEEBLIGHTSEE %s %s: torch %d glimmer %d"), IsLocallyControlled() ? TEXT("me") : TEXT("other player"), *GetName(), IsTorchLit() ? 1 : 0, IsGlimmerOn() ? 1 : 0);
}

void AKnightCharacter::TorchKey()
{
	if (!IsLocallyControlled() || IsUIOpen()) return;
	ServerToggleTorch();
}

void AKnightCharacter::ServerToggleTorch_Implementation() { ToggleTorch(); }

bool AKnightCharacter::ToggleTorch()
{
	if (!HasAuthority() || !Skills) return false;
	if (IsTorchLit())
	{
		SetLightBits(LightBits & ~1);
		Skills->Say(2.f, FColor(255, 200, 120), FString::Printf(TEXT("You put out your torch (%d min %02d s left in it)."), int32(TorchBurnLeft) / 60, int32(TorchBurnLeft) % 60));
		UE_LOG(LogTemp, Log, TEXT("JEEBTORCH %s put out, %.0f s left"), *GetName(), TorchBurnLeft);
		return true;
	}
	if (BlocksTorch(SelectedKind())) { Skills->Say(2.5f, FColor::Orange, TEXT("Both your hands are full: put your bow or two-handed weapon away (R) to hold a torch.")); return false; }
	if (TorchBurnLeft <= 0.f)
	{
		if (Skills->CountItem(JeebItems::IdTorch) <= 0) { Skills->Say(2.5f, FColor::Orange, TEXT("You have no torch. The smith makes them from coal and logs.")); return false; }
		Skills->RemoveItems(JeebItems::IdTorch, 1);
		TorchBurnLeft = FMath::Max(1.f, JeebTune::V().TorchBurnSeconds);
	}
	SetLightBits(LightBits | 1);
	Skills->Say(2.f, FColor(255, 200, 120), TEXT("You light a torch (L puts it out)."));
	UE_LOG(LogTemp, Log, TEXT("JEEBTORCH %s lit, %.0f s to burn, %d left in the pack"), *GetName(), TorchBurnLeft, Skills->CountItem(JeebItems::IdTorch));
	return true;
}

bool AKnightCharacter::ToggleGlimmer()
{
	if (!HasAuthority()) return false;
	if (IsGlimmerOn()) { SetLightBits(LightBits & ~2); UE_LOG(LogTemp, Log, TEXT("JEEBGLIMMER %s off (cast again)"), *GetName()); return true; }
	if (Magic < 1.f) { if (Skills) Skills->Say(2.f, FColor::Orange, TEXT("Not enough magic for Glimmer.")); return false; }
	SetLightBits(LightBits | 2);
	UE_LOG(LogTemp, Log, TEXT("JEEBGLIMMER %s on, magic %.0f, drain %.2f a second"), *GetName(), Magic, JeebTune::V().GlimmerDrain);
	return true;
}

bool AKnightCharacter::SmithTorchNow()
{
	if (!HasAuthority() || !Skills) return false;
	const auto& TW = JeebTune::V();
	const int32 NCoal = FMath::Max(0, FMath::RoundToInt(TW.TorchCoal)), NLogs = FMath::Max(0, FMath::RoundToInt(TW.TorchLogs)), Makes = FMath::Max(1, FMath::RoundToInt(TW.TorchMakes));
	const int32 NeedLv = FMath::Max(1, FMath::RoundToInt(TW.TorchSmithLevel));
	if (Skills->GetLevel(ESkillType::Smithing) < NeedLv) { ShopNote(FString::Printf(TEXT("You need Smithing level %d."), NeedLv)); return false; }
	if (Skills->GetItemCount(EItemType::Coal) < NCoal || Skills->GetItemCount(EItemType::Logs) < NLogs) { ShopNote(TEXT("You don't have the materials.")); return false; }
	if (!Skills->CanAdd(JeebItems::IdTorch, Makes)) { ShopNote(TEXT("Your pack is full.")); return false; }
	if (NCoal > 0) Skills->RemoveItems(JeebItems::MaterialId(EItemType::Coal), NCoal);
	if (NLogs > 0) Skills->RemoveItems(JeebItems::MaterialId(EItemType::Logs), NLogs);
	Skills->AddInvItem(JeebItems::IdTorch, Makes);
	Skills->AddXP(ESkillType::Smithing, FMath::Max(0, FMath::RoundToInt(TW.TorchSmithXP)));
	ShopNote(FString::Printf(TEXT("Smithed %d torches. They are in your pack (Tab); L lights one."), Makes));
	UE_LOG(LogTemp, Log, TEXT("JEEBTORCH %s smithed %d torches from %d coal and %d logs: %d in the pack"), *GetName(), Makes, NCoal, NLogs, Skills->CountItem(JeebItems::IdTorch));
	return true;
}

void AKnightCharacter::TickLights(float Dt)
{
#if !UE_BUILD_SHIPPING
	TickLightTest(Dt);
#endif
	if (HasAuthority() && LightBits != 0)
	{
		uint8 B = LightBits;
		if (B & 1)
		{
			if (BlocksTorch(SelectedKind()))
			{
				B &= ~1;
				if (Skills) Skills->Say(2.5f, FColor(255, 200, 120), TEXT("You put out your torch to take up a two-handed weapon."));
				UE_LOG(LogTemp, Log, TEXT("JEEBTORCH %s put out: kind %d in hand needs both hands"), *GetName(), SelectedKind());
			}
			else
			{
				TorchBurnLeft -= Dt;
				if (TorchBurnLeft <= 0.f)
				{
					TorchBurnLeft = 0.f; B &= ~1;
					if (Skills) Skills->Say(3.f, FColor::Orange, Skills->CountItem(JeebItems::IdTorch) > 0 ? TEXT("Your torch has burnt out. L lights another.") : TEXT("Your torch has burnt out."));
					UE_LOG(LogTemp, Log, TEXT("JEEBTORCH %s burnt out, %d left in the pack"), *GetName(), Skills ? Skills->CountItem(JeebItems::IdTorch) : 0);
				}
			}
		}
		if (B & 2)
		{
			const int32 K = SelectedKind();
			if (!bIsWizard || (K != JeebItems::K_Wand && K != JeebItems::K_Staff) || bOnBroom)
			{ B &= ~2; UE_LOG(LogTemp, Log, TEXT("JEEBGLIMMER %s out: no wand or staff in hand"), *GetName()); }
			else
			{
				Magic = FMath::Max(0.f, Magic - JeebTune::V().GlimmerDrain * Dt);
				MagicUsedAt = GetWorld()->GetTimeSeconds();   // (the bar does not refill while it is lit)
				if (Magic <= 0.f) { B &= ~2; if (Skills) Skills->Say(2.5f, FColor::Orange, TEXT("Your Glimmer fades: no magic left.")); UE_LOG(LogTemp, Log, TEXT("JEEBGLIMMER %s out of magic"), *GetName()); }
			}
		}
		SetLightBits(B);
	}
	// the flicker, the same on the flame's height and the light (every drawing machine; LightPhase differs per player)
	if (LightBits != 0 && GetNetMode() != NM_DedicatedServer)
	{
		if (LightPhase == 0.f) LightPhase = FMath::FRandRange(1.f, 100.f);
		const float T = GetWorld()->GetTimeSeconds() + LightPhase;
		const float W = 0.55f * FMath::Sin(T * 9.3f) + 0.3f * FMath::Sin(T * 15.1f + 1.7f) + 0.15f * FMath::Sin(T * 23.7f + 0.6f);
		LightFlick = 1.f + FMath::Clamp(JeebTune::V().TorchFlicker, 0.f, 0.6f) * W;
	}
}

void AKnightCharacter::UpdateLightComps()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	const bool bTorch = IsTorchLit(), bGlim = IsGlimmerOn();
	auto Absolute = [](USceneComponent* C) { C->SetUsingAbsoluteLocation(true); C->SetUsingAbsoluteRotation(true); C->SetUsingAbsoluteScale(true); };
	auto MakeMesh = [&](UStaticMesh* SM, UMaterialInterface* Mat) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(GetRootComponent());
		Absolute(C);
		C->SetStaticMesh(SM);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetMobility(EComponentMobility::Movable);
		if (Mat) C->SetMaterial(0, Mat);
		C->RegisterComponent();
		return C;
	};
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* FireMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Effects/M_TorchFire.M_TorchFire"));
	UMaterialInterface* OrbMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Effects/M_GlimmerOrb.M_GlimmerOrb"));
	if (bTorch && !TorchModel)
	{
		UStaticMesh* M = JeebProps::Load(TEXT("/Game/Props/SM_Torch.SM_Torch"));
		bTorchMeshFallback = !M;
		if (!M) M = JeebProps::Load(TEXT("/Game/Filler/SM_TorchUnlitTar.SM_TorchUnlitTar"));   // (the filler torch, until the Meshy one is imported: Tools/BuildProps.ps1 Torch)
		TorchModel = M ? JeebProps::MakeStatic(this, GetRootComponent(), M) : nullptr;
		if (TorchModel)
		{
			Absolute(TorchModel);
			if (!bTorchMeshFallback)
				if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Props/MI_Torch.MI_Torch")))
					if (UMaterialInstanceDynamic* MI = TorchModel->CreateDynamicMaterialInstance(0, Base))
						for (const TCHAR* Kind : { TEXT("Base"), TEXT("MR"), TEXT("Normal") })
							if (UTexture* Tex = LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("/Game/Props/T_Torch_%s.T_Torch_%s"), Kind, Kind)))
								MI->SetTextureParameterValue(*FString::Printf(TEXT("%sTex"), Kind), Tex);
		}
		for (int32 i = 0; i < NumCards; ++i)
		{
			UStaticMeshComponent* C = MakeMesh(Plane, nullptr);
			C->SetTranslucentSortPriority(i);   // (the core drawn over the outer flame)
			if (FireMat)
				if (UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(FireMat, this))
				{
					MI->SetVectorParameterValue(TEXT("Tint"), Cards[i].Tint);
					MI->SetScalarParameterValue(TEXT("Core"), Cards[i].Core);
					MI->SetScalarParameterValue(TEXT("Seed"), FMath::FRandRange(0.f, 50.f));
					MI->SetScalarParameterValue(TEXT("Speed"), Cards[i].Speed);
					MI->SetScalarParameterValue(TEXT("Intensity"), Cards[i].Bright);
					C->SetMaterial(0, MI);
					FlameMids.Add(MI);
				}
			FlameCards.Add(C);
		}
		if (CVarTorchEmbers.GetValueOnGameThread() != 0)
		{
			TorchEmbers = NewObject<UInstancedStaticMeshComponent>(this);
			TorchEmbers->SetupAttachment(GetRootComponent()); Absolute(TorchEmbers);
			TorchEmbers->SetCollisionEnabled(ECollisionEnabled::NoCollision); TorchEmbers->SetCastShadow(false);
			TorchEmbers->SetStaticMesh(Sphere);
			if (OrbMat) if (UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(OrbMat, this))
			{ MI->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.f, 0.45f, 0.08f)); MI->SetScalarParameterValue(TEXT("Intensity"), 40.f); TorchEmbers->SetMaterial(0, MI); }
			TorchEmbers->RegisterComponent();
			TorchEmbers->SetWorldTransform(FTransform::Identity);
			for (int32 i = 0; i < NumEmbers; ++i) TorchEmbers->AddInstance(FTransform(FVector::ZeroVector), true);
			TorchEmbers->SetHasPerInstancePrevTransforms(true);   // (motion vectors: no TSR smear, as the pipe's puffs and the fireflies)
			TorchSmoke = NewObject<UInstancedStaticMeshComponent>(this);
			TorchSmoke->SetupAttachment(GetRootComponent()); Absolute(TorchSmoke);
			TorchSmoke->SetCollisionEnabled(ECollisionEnabled::NoCollision); TorchSmoke->SetCastShadow(false);
			TorchSmoke->SetStaticMesh(Sphere);
			if (UMaterialInterface* Puff = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Wizard/M_PipePuff.M_PipePuff")))
				if (UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(Puff, this))
				{ MI->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.16f, 0.15f, 0.15f)); MI->SetScalarParameterValue(TEXT("Opacity"), 0.1f); TorchSmoke->SetMaterial(0, MI); }
			TorchSmoke->RegisterComponent();
			TorchSmoke->SetWorldTransform(FTransform::Identity);
			for (int32 i = 0; i < NumPuffs; ++i) TorchSmoke->AddInstance(FTransform(FVector::ZeroVector), true);
			TorchSmoke->SetHasPerInstancePrevTransforms(true);
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBTORCH drawn for %s: model %s, flame %s"), *GetName(), TorchModel ? (bTorchMeshFallback ? TEXT("the filler torch (SM_Torch missing: Tools/BuildProps.ps1 Torch)") : TEXT("SM_Torch")) : TEXT("MISSING"),
			FireMat ? TEXT("M_TorchFire") : TEXT("MISSING (Tools/LightMaterials.ps1)"));
	}
	if (!bTorch && TorchModel)
	{
		TorchModel->DestroyComponent(); TorchModel = nullptr;
		for (UStaticMeshComponent* C : FlameCards) if (C) C->DestroyComponent();
		FlameCards.Reset(); FlameMids.Reset();
		if (TorchEmbers) { TorchEmbers->DestroyComponent(); TorchEmbers = nullptr; }
		if (TorchSmoke) { TorchSmoke->DestroyComponent(); TorchSmoke = nullptr; }
	}
	if (bGlim && !GlimmerOrb)
	{
		GlimmerOrb = MakeMesh(Sphere, nullptr);
		GlimmerCore = MakeMesh(Sphere, nullptr);
		GlimmerCore->SetTranslucentSortPriority(1);
		if (OrbMat)
		{
			if (UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(OrbMat, this)) { MI->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.62f, 0.8f, 1.f)); MI->SetScalarParameterValue(TEXT("Intensity"), 2.5f); GlimmerOrb->SetMaterial(0, MI); }
			if (UMaterialInstanceDynamic* MI = UMaterialInstanceDynamic::Create(OrbMat, this)) { MI->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.95f, 0.98f, 1.f)); MI->SetScalarParameterValue(TEXT("Intensity"), 9.f); GlimmerCore->SetMaterial(0, MI); }
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBGLIMMER drawn for %s (%s)"), *GetName(), OrbMat ? TEXT("M_GlimmerOrb") : TEXT("MISSING: Tools/LightMaterials.ps1"));
	}
	if (!bGlim && GlimmerOrb)
	{
		GlimmerOrb->DestroyComponent(); GlimmerOrb = nullptr;
		if (GlimmerCore) { GlimmerCore->DestroyComponent(); GlimmerCore = nullptr; }
	}
	if ((bTorch || bGlim) && !CarryLight)
	{
		CarryLight = NewObject<UPointLightComponent>(this);
		CarryLight->SetupAttachment(GetRootComponent());
		Absolute(CarryLight);
		CarryLight->SetCastShadows(false);   // (cheap: one shadowless light per player, only while lit)
		CarryLight->SetMobility(EComponentMobility::Movable);
		CarryLight->MaxDrawDistance = 9000.f; CarryLight->MaxDistanceFadeRange = 1500.f;
		CarryLight->SetSourceRadius(4.f);
		CarryLight->RegisterComponent();
	}
	if (!bTorch && !bGlim && CarryLight) { CarryLight->DestroyComponent(); CarryLight = nullptr; }
	if (CarryLight)
	{
		const auto& TW = JeebTune::V();
		CarryLight->SetLightColor(bTorch ? FLinearColor(1.f, 0.56f, 0.24f) : FLinearColor(0.72f, 0.84f, 1.f));
		CarryLight->SetAttenuationRadius(bTorch ? TW.TorchRadius : TW.GlimmerRadius);
	}
	PlaceLights();
}

void AKnightCharacter::PlaceLights()
{
	if (LightBits == 0 || GetNetMode() == NM_DedicatedServer || !Anim) return;
	const bool bHeld = TorchInHand();
	const bool bHand = bHeld && !bInvisible;   // (invisible: the torch, its flame and its light are hidden too, KnightCharacterInvis.cpp)
	if (WizardBookHeld && WizardBookHeld->IsVisible() == bHeld) WizardBookHeld->SetVisibility(!bHeld);   // (the wizard's book, ball or pipe away while he holds a torch)
	if (WizardPipeSmoke && WizardPipeSmoke->IsVisible() == bHeld) WizardPipeSmoke->SetVisibility(!bHeld);
	if (CarryLight) CarryLight->SetVisibility(!bInvisible);
	if (GlimmerOrb) { GlimmerOrb->SetVisibility(!bInvisible); if (GlimmerCore) GlimmerCore->SetVisibility(!bInvisible); }
	const float T = GetWorld()->GetTimeSeconds() + LightPhase;
	const auto& TW = JeebTune::V();
	FVector LightAt = GetActorLocation();
	float LightI = 0.f;
	// ---- the torch ----
	if (TorchModel)
	{
		const FVector Hand = Anim->BoneAt(JeebAnim::LHand), Fore = Anim->BoneAt(JeebAnim::LFore);
		FVector Fwd = (Hand - Fore).GetSafeNormal2D();
		if (Fwd.IsNearlyZero()) Fwd = GetActorForwardVector();
		const FQuat Frame = FRotationMatrix::MakeFromXZ(Fwd, FVector::UpVector).ToQuat();
		FVector At; FRotator R; float Sc;
		ParseHold(At, R, Sc);
		if (bIsTroll) Sc *= 1.2f;   // (his big fist)
		const FQuat Q = Frame * R.Quaternion();
		const FVector Grip = Hand + Frame.RotateVector(At);
		TorchModel->SetVisibility(bHand);
		TorchModel->SetWorldTransform(FTransform(Q, Grip, FVector(bTorchMeshFallback ? Sc * 0.6f : Sc)));
		const FVector Up = Q.GetUpVector();
		const FVector Foot = Grip + Up * (CVarTorchFlameUp.GetValueOnGameThread() * Sc);
		// the cards face the camera (this machine's, a look only), standing straight up whatever the torch's lean: flames rise
		FVector Cam = Foot + GetActorForwardVector() * 300.f;
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
		FVector ToCam = (Cam - Foot).GetSafeNormal2D();
		if (ToCam.IsNearlyZero()) ToCam = FVector::ForwardVector;
		const FQuat CardQ = FRotationMatrix::MakeFromZY(ToCam, -FVector::UpVector).ToQuat() * FQuat(FVector::UpVector, FMath::DegreesToRadians(CVarTorchCardSpin.GetValueOnGameThread()));
		const float Size = CVarTorchFlameSize.GetValueOnGameThread() * Sc;
		const float Bright = CVarTorchFlameBright.GetValueOnGameThread();
		for (int32 i = 0; i < FlameCards.Num(); ++i)
		{
			UStaticMeshComponent* C = FlameCards[i];
			if (!C) continue;
			C->SetVisibility(bHand);
			const float H = Cards[i].H * Size * (1.f + (LightFlick - 1.f) * 1.6f * Cards[i].Flick);
			const float Wd = Cards[i].W * Size * (1.f - (LightFlick - 1.f) * 0.5f * Cards[i].Flick);
			const FVector Sway = FVector(FMath::Sin(T * 2.3f + i), FMath::Cos(T * 1.9f + i * 2.f), 0.f) * (0.6f * Size);
			C->SetWorldTransform(FTransform(CardQ, Foot + FVector::UpVector * (H * 0.42f) + Sway + ToCam * (0.3f * i), FVector(Wd / 100.f, H / 100.f, 1.f)));
			if (FlameMids.IsValidIndex(i)) FlameMids[i]->SetScalarParameterValue(TEXT("Intensity"), Cards[i].Bright * Bright);
		}
		const float Dt = FMath::Max(GetWorld()->GetDeltaSeconds(), 0.0001f);
		if (TorchEmbers)
		{
			TorchEmbers->SetVisibility(bHand);
			TArray<FTransform> Now, Prev; Now.SetNum(NumEmbers); Prev.SetNum(NumEmbers);
			for (int32 i = 0; i < NumEmbers; ++i)
			{
				auto EmberAt = [&](float Tm, float& U)
				{
					const float Rate = 1.f / (1.1f + 0.13f * i);
					U = FMath::Fmod(Tm * Rate + float(i) / NumEmbers, 1.f);
					const float Seed = i * 12.9898f;
					const FVector P = Foot + FVector(0.f, 0.f, 6.f + U * 55.f * Size)
						+ FVector(FMath::Sin(Tm * 3.1f + Seed) * (2.f + 9.f * U), FMath::Cos(Tm * 2.7f + Seed * 1.3f) * (2.f + 9.f * U), 0.f) * Size;
					const float S = 0.0075f * Size * (U < 0.8f ? 1.f : (1.f - U) / 0.2f) * (0.6f + 0.4f * FMath::Frac(Seed));
					return FTransform(FQuat::Identity, P, FVector(FMath::Max(S, 0.0005f)));
				};
				float U0, U1;
				Prev[i] = EmberAt(T - Dt, U0); Now[i] = EmberAt(T, U1);
				if (U1 < U0) Prev[i] = Now[i];   // (a new ember starting at the bottom has no trail)
			}
			TorchEmbers->BatchUpdateInstancesTransforms(0, Now, Prev, true, true, true);
		}
		if (TorchSmoke)
		{
			TorchSmoke->SetVisibility(bHand);
			TArray<FTransform> Now, Prev; Now.SetNum(NumPuffs); Prev.SetNum(NumPuffs);
			for (int32 i = 0; i < NumPuffs; ++i)
			{
				auto PuffAt = [&](float Tm, float& U)
				{
					U = FMath::Fmod(Tm / 2.6f + float(i) / NumPuffs, 1.f);
					const FVector P = Foot + FVector(0.f, 0.f, (30.f + U * 60.f) * Size) + FVector(FMath::Sin(Tm * 0.9f + i) * 4.f * U, FMath::Cos(Tm * 0.8f + i) * 4.f * U, 0.f) * Size;
					const float S = (0.04f + 0.1f * U) * Size * (U < 0.75f ? FMath::Min(1.f, U * 5.f) : (1.f - U) / 0.25f);
					return FTransform(FQuat::Identity, P, FVector(FMath::Max(S, 0.001f)));
				};
				float U0, U1;
				Prev[i] = PuffAt(T - Dt, U0); Now[i] = PuffAt(T, U1);
				if (U1 < U0) Prev[i] = Now[i];
			}
			TorchSmoke->BatchUpdateInstancesTransforms(0, Now, Prev, true, true, true);
		}
		LightAt = bHand ? Foot + FVector::UpVector * 18.f * Size : Hand + FVector::UpVector * 20.f;
		LightI = TW.TorchLight * LightFlick;
	}
	// ---- Glimmer's orb, at the wand's tip or the staff's orb ----
	if (GlimmerOrb)
	{
		const int32 K = SelectedKind();
		const bool bTip = K == JeebItems::K_Wand || K == JeebItems::K_Staff;
		const FVector Tip = bTip && !bStowed ? CastStartPoint(K) + FVector(0.f, 0.f, K == JeebItems::K_Staff ? 16.f : 4.f) : (WristR ? WristR->GetComponentLocation() + FVector(0.f, 0.f, 25.f) : GetActorLocation() + FVector(0.f, 0.f, 60.f));
		const float Pulse = 1.f + 0.05f * FMath::Sin(T * 2.2f);   // (a slow breath, not a flicker: TSR)
		const float S = CVarGlimmerSize.GetValueOnGameThread() * Pulse;
		GlimmerOrb->SetWorldTransform(FTransform(FQuat::Identity, Tip, FVector(0.12f * S)));
		if (GlimmerCore) GlimmerCore->SetWorldTransform(FTransform(FQuat::Identity, Tip, FVector(0.045f * S)));
		const float B = CVarGlimmerBright.GetValueOnGameThread();
		if (UMaterialInstanceDynamic* MI = Cast<UMaterialInstanceDynamic>(GlimmerOrb->GetMaterial(0))) MI->SetScalarParameterValue(TEXT("Intensity"), 2.5f * B);
		if (GlimmerCore) if (UMaterialInstanceDynamic* MI = Cast<UMaterialInstanceDynamic>(GlimmerCore->GetMaterial(0))) MI->SetScalarParameterValue(TEXT("Intensity"), 9.f * B);
		if (!TorchModel) { LightAt = Tip; LightI = TW.GlimmerLight * Pulse; }   // (one light per player: the torch's when both are lit)
	}
	if (CarryLight)
	{
		CarryLight->SetWorldLocation(LightAt);
		CarryLight->SetIntensity(LightI);
	}
}

#if !UE_BUILD_SHIPPING
// Tests. Single player pictures: jeeb.Test.Light (1 torch, 2 Glimmer, 3 both) on your own character with no item or magic needed, and
// jeeb.Test.LightCrowd N (frame cost: N knights with lit torches 5 m ahead). Online (Tools/light_net.sh): -LightNetTest on the server: once two players
// are in, the first (a knight) is given coal and logs and smiths torches (SmithTorchNow, as the smithy's button does past its NearTrader check), lights
// one (ToggleTorch, as L does), and the torch is made to burn out in 4 s; the second (a wizard, -PlayAs=Wizard) gets Glimmer on, then his magic is set
// low so it goes out on its own. JEEBLIGHTTEST PASS/FAIL on the server; JEEBLIGHTSEE on the players' machines says what each saw of the other.
void AKnightCharacter::TickLightTest(float Dt)
{
	if (HasAuthority() && IsLocallyControlled())
	{
		static int32 LastSet = -1;
		const int32 Want = CVarTestLight.GetValueOnGameThread();
		if (Want >= 0 && Want != LastSet)
		{
			LastSet = Want;
			if ((Want & 1) && TorchBurnLeft <= 0.f) TorchBurnLeft = FMath::Max(1.f, JeebTune::V().TorchBurnSeconds);
			if ((Want & 1) && Skills && BlocksTorch(SelectedKind())) { Skills->SetSelected(0); if (BlocksTorch(SelectedKind())) Skills->Hotbar[0] = 0; }   // (pictures: a free hand)
			SetLightBits(uint8(Want & 3));
			UE_LOG(LogTemp, Log, TEXT("JEEBLIGHT test: torch %d glimmer %d"), IsTorchLit() ? 1 : 0, IsGlimmerOn() ? 1 : 0);
		}
		if (IsGlimmerOn() && Want >= 2) Magic = FMath::Max(Magic, 50.f);   // (pictures: keep it lit)
		static int32 LastCrowd = 0; static TArray<TWeakObjectPtr<AKnightCharacter>> Crowd;
		const int32 WantCrowd = CVarLightCrowd.GetValueOnGameThread();
		if (WantCrowd != LastCrowd)
		{
			LastCrowd = WantCrowd;
			for (const TWeakObjectPtr<AKnightCharacter>& C : Crowd) if (C.IsValid()) C->Destroy();
			Crowd.Reset();
			AActor* VT = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetViewTarget() : nullptr;
			if (VT && WantCrowd > 0)
			{
				const FRotator Yaw(0.f, VT->GetActorRotation().Yaw, 0.f);
				for (int32 i = 0; i < WantCrowd; ++i)
				{
					FVector At = VT->GetActorLocation() + Yaw.Vector() * 500.f + FRotator(0.f, Yaw.Yaw + 90.f, 0.f).Vector() * (i - (WantCrowd - 1) * 0.5f) * 120.f;
					At.Z = VT->GetActorLocation().Z + 20.f;
					FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					if (AKnightCharacter* C = GetWorld()->SpawnActor<AKnightCharacter>(GetClass(), At, Yaw + FRotator(0.f, 180.f, 0.f), SP)) { C->TorchBurnLeft = 9999.f; C->SetLightBits(1); Crowd.Add(C); }
				}
				UE_LOG(LogTemp, Log, TEXT("JEEBLIGHTCROWD %d knights with lit torches ahead of the view"), Crowd.Num());
			}
		}
	}
	static const bool bSrv = FParse::Param(FCommandLine::Get(), TEXT("LightNetTest"));
	if (!bSrv || !HasAuthority() || !GetController() || !Skills) return;
	static TArray<TWeakObjectPtr<AKnightCharacter>> Order;
	Order.RemoveAll([](const TWeakObjectPtr<AKnightCharacter>& K) { return !K.IsValid(); });
	const int32 TestRole = Order.AddUnique(this);
	LightTestT += Dt;
	if (Order.Num() < 2 || LightTestT < 10.f) return;
	auto Log = [](bool bOk, const FString& S) { UE_LOG(LogTemp, Log, TEXT("JEEBLIGHTTEST %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *S); };
	if (TestRole == 0)
	{
		if (LightTestStep == 0)
		{
			for (int32 s = 0; s < 4 && s < Skills->Slots.Num(); ++s) Skills->Slots[s] = FInvSlot();   // (room: test characters start with a full practice pack)
			Skills->AddInvItem(JeebItems::MaterialId(EItemType::Coal), 1); Skills->AddInvItem(JeebItems::MaterialId(EItemType::Logs), 1);
			const int32 T0 = Skills->CountItem(JeebItems::IdTorch), C0 = Skills->GetItemCount(EItemType::Coal), L0 = Skills->GetItemCount(EItemType::Logs);
			const bool bMade = SmithTorchNow();
			const int32 Makes = FMath::Max(1, FMath::RoundToInt(JeebTune::V().TorchMakes));
			Log(bMade && Skills->CountItem(JeebItems::IdTorch) == T0 + Makes && Skills->GetItemCount(EItemType::Coal) == C0 - 1 && Skills->GetItemCount(EItemType::Logs) == L0 - 1,
				FString::Printf(TEXT("smithed: torches %d -> %d, coal %d -> %d, logs %d -> %d"), T0, Skills->CountItem(JeebItems::IdTorch), C0, Skills->GetItemCount(EItemType::Coal), L0, Skills->GetItemCount(EItemType::Logs)));
			if (BlocksTorch(SelectedKind())) Skills->SetSelected(0);
			const int32 T1 = Skills->CountItem(JeebItems::IdTorch);
			const bool bLit = ToggleTorch();
			Log(bLit && IsTorchLit() && Skills->CountItem(JeebItems::IdTorch) == T1 - 1, FString::Printf(TEXT("lit: one torch used (%d left), %.0f s to burn"), Skills->CountItem(JeebItems::IdTorch), TorchBurnLeft));
			LightTestStep = 1; LightTestT = 10.f;
		}
		else if (LightTestStep == 1 && LightTestT > 22.f) { TorchBurnLeft = 4.f; LightTestStep = 2; LightTestT = 22.f; UE_LOG(LogTemp, Log, TEXT("JEEBLIGHTTEST burn set to 4 s")); }
		else if (LightTestStep == 2 && !IsTorchLit()) { Log(LightTestT > 25.5f && LightTestT < 27.f, FString::Printf(TEXT("burnt out after %.1f s (wanted 4)"), LightTestT - 22.f)); LightTestStep = 3; }
	}
	else if (TestRole == 1)
	{
		if (LightTestStep == 0)
		{
			Magic = MagicCap();
			const int32 K = SelectedKind();
			Log(bIsWizard, FString::Printf(TEXT("second player is a wizard (%d), holding kind %d"), bIsWizard ? 1 : 0, K));
			if (K != JeebItems::K_Wand && K != JeebItems::K_Staff)
				for (int32 s = 0; s < JeebItems::NumHotbar; ++s)
					if (JeebItems::IsGear(Skills->Hotbar[s]) && (JeebItems::KindOf(Skills->Hotbar[s]) == JeebItems::K_Wand || JeebItems::KindOf(Skills->Hotbar[s]) == JeebItems::K_Staff)) { Skills->SetSelected(s); break; }
			Log(ToggleGlimmer() && IsGlimmerOn(), TEXT("Glimmer on"));
			LightTestStep = 1;
		}
		else if (LightTestStep == 1 && LightTestT > 22.f) { Magic = 2.f; LightTestStep = 2; LightTestT = 22.f; UE_LOG(LogTemp, Log, TEXT("JEEBLIGHTTEST wizard's magic set to 2")); }
		else if (LightTestStep == 2 && !IsGlimmerOn())
		{
			const float Want = 2.f / FMath::Max(JeebTune::V().GlimmerDrain, 0.01f);
			Log(FMath::Abs((LightTestT - 22.f) - Want) < 1.f, FString::Printf(TEXT("Glimmer went out at 0 magic after %.1f s (wanted %.1f)"), LightTestT - 22.f, Want));
			LightTestStep = 3;
		}
		if (LightTestStep == 3) { static bool bDone = false; if (!bDone) { bDone = true; UE_LOG(LogTemp, Log, TEXT("JEEBLIGHTTEST done")); } }
	}
}
#endif
