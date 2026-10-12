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

void AKnightCharacter::RefreshUIMode()
{
	const bool bOpen = IsUIOpen() || bPanelCursor;   // the side panel's cursor works like an open screen for the mouse
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetShowMouseCursor(bOpen);
		PC->ResetIgnoreLookInput();
		if (bOpen) PC->SetIgnoreLookInput(true);
		if (bOpen)
		{
			FInputModeGameAndUI Mode;
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
		}
		else
		{
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
}

void AKnightCharacter::ToggleSettings()
{
	bSettingsOpen = !bSettingsOpen;
	if (bSettingsOpen) { bInventoryOpen = false; bBankOpen = false; bSkillsOpen = false; bShopOpen = false; bSmithOpen = false; CloseDialogue(); }
	RefreshUIMode();
	if (!bSettingsOpen && GConfig)
	{
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void AKnightCharacter::ToggleSkills()
{
	if (bSettingsOpen || bShopOpen || bSmithOpen || bDialogueOpen || bBankOpen) return;
	bInventoryOpen = false;
	bSkillsOpen = !bSkillsOpen;
	GetCharacterMovement()->StopMovementImmediately();
	RefreshUIMode();
}

void AKnightCharacter::ShowPackTab(bool bSkills)
{
	if (!bInventoryOpen && !bSkillsOpen) return;
	bInventoryOpen = !bSkills;
	bSkillsOpen = bSkills;
	RefreshUIMode();
}

void AKnightCharacter::ToggleInventory()
{
	if (bSettingsOpen) return;
	if (bSkillsOpen) { bSkillsOpen = false; RefreshUIMode(); return; }
	if (bDialogueOpen) { CloseDialogue(); return; }
	if (bShopOpen) { CloseShop(); return; }
	if (bSmithOpen) { CloseSmith(); return; }
	if (bBankOpen) { CloseBank(); return; }
	bInventoryOpen = !bInventoryOpen;
	RefreshUIMode();
}

void AKnightCharacter::OpenShop()
{
	if (bSettingsOpen || bShopOpen) return;
	bInventoryOpen = false;
	bSmithOpen = false;
	bShopOpen = true;
	GetCharacterMovement()->StopMovementImmediately();
	RefreshUIMode();
}

void AKnightCharacter::CloseShop()
{
	bShopOpen = false;
	if (ANpc* N = TradeNpc.Get()) N->SetTalking(false);
	RefreshUIMode();
}

void AKnightCharacter::OpenBank()
{
	if (bSettingsOpen || bBankOpen) return;
	bInventoryOpen = false;
	bShopOpen = false;
	bSmithOpen = false;
	bSkillsOpen = false;
	bBankOpen = true;
	GetCharacterMovement()->StopMovementImmediately();
	RefreshUIMode();
}

void AKnightCharacter::CloseBank()
{
	bBankOpen = false;
	if (ANpc* N = TradeNpc.Get()) N->SetTalking(false);
	RefreshUIMode();
}

void AKnightCharacter::OpenSmith()
{
	if (bSettingsOpen || bSmithOpen) return;
	bInventoryOpen = false;
	bShopOpen = false;
	bSmithOpen = true;
	GetCharacterMovement()->StopMovementImmediately();
	RefreshUIMode();
}

void AKnightCharacter::CloseSmith()
{
	bSmithOpen = false;
	if (ANpc* N = TradeNpc.Get()) N->SetTalking(false);
	RefreshUIMode();
}

void AKnightCharacter::StartDialogue(ANpc* Npc)
{
	if (!Npc || IsUIOpen()) return;
	TradeNpc = Npc;
	int32 Lore = -1;
	DialogueLines = Npc->MakeConversation(&Lore);
	if (Lore >= 0) Inv(EInvOp::Lore, Lore);
	DialogueIdx = 0;
	bDialogueOpen = true;
	bCrouched = false;
	bBlocking = false;
	Npc->SetTalking(true);
	GetCharacterMovement()->StopMovementImmediately();
	// Turn to face them
	const FVector To = Npc->GetActorLocation() - GetActorLocation();
	SetActorRotation(FRotator(0.f, FVector(To.X, To.Y, 0.f).Rotation().Yaw, 0.f));
	RefreshUIMode();
}

void AKnightCharacter::AdvanceDialogue()
{
	if (!bDialogueOpen) return;
	++DialogueIdx;
	if (DialogueIdx < DialogueLines.Num()) return;

	// The conversation is over: on to the trade screen if they have one
	bDialogueOpen = false;
	ANpc* Npc = TradeNpc.Get();
	if (Npc && Npc->OffersTrade())
	{
		if (Npc->GetRole() == ENpcRole::Blacksmith) OpenSmith();
		else if (Npc->GetRole() == ENpcRole::Banker) OpenBank();
		else OpenShop();
	}
	else
	{
		if (Npc) Npc->SetTalking(false);
		RefreshUIMode();
	}
}

void AKnightCharacter::CloseDialogue()
{
	if (!bDialogueOpen) return;
	bDialogueOpen = false;
	if (ANpc* N = TradeNpc.Get()) N->SetTalking(false);
	RefreshUIMode();
}

bool AKnightCharacter::GetDialogue(FString& OutName, FString& OutLine, bool& bOutLast) const
{
	if (!bDialogueOpen || !DialogueLines.IsValidIndex(DialogueIdx)) return false;
	const ANpc* N = TradeNpc.Get();
	OutName = N ? N->GetNpcName() : FString();
	OutLine = DialogueLines[DialogueIdx];
	bOutLast = (DialogueIdx == DialogueLines.Num() - 1);
	return true;
}

void AKnightCharacter::ApplyGraphics()
{
	if (!GEngine || !GetWorld()) return;
	// Only your own knight sets the graphics (other players' knights on your screen must not reset them), never on a server
	if (!IsLocallyControlled() || IsNetMode(NM_DedicatedServer)) return;
	bGraphicsApplied = true;
	UWorld* W = GetWorld();
	const bool bLowest = GraphicsLevel <= 0;
	auto Set = [&](const TCHAR* Var, int32 V) { GEngine->Exec(W, *FString::Printf(TEXT("%s %d"), Var, V)); };
	// Plain sky light, no Lumen (the old "Fancy" level was removed 2026-10-12 for speed and size; see CLAUDE.md to bring it
	// back). Screen-space reflections on Low, none on Lowest.
	Set(TEXT("r.DynamicGlobalIlluminationMethod"), 0);
	Set(TEXT("r.ReflectionMethod"), bLowest ? 0 : 2);
	Set(TEXT("r.VolumetricFog"), 0);
	// Lowest, for weak PCs: no volumetric clouds, ordinary (cheaper) shadow maps instead of virtual ones, short shadow range,
	// cheap FXAA edge smoothing instead of TSR, and simpler post-processing (no ambient occlusion)
	Set(TEXT("r.VolumetricCloud"), bLowest ? 0 : 1);
	Set(TEXT("r.Shadow.Virtual.Enable"), 0);   // standard shadow maps on both levels (virtual ones overloaded in the pine woods)
	Set(TEXT("sg.ShadowQuality"), bLowest ? 2 : 3);   // (1 made the shadows flicker: too coarse, with no softening, for the swaying leaves and the moving sun)
	Set(TEXT("r.AntiAliasingMethod"), bLowest ? 1 : 4);
	Set(TEXT("sg.PostProcessQuality"), bLowest ? 0 : 3);
	Set(TEXT("sg.EffectsQuality"), bLowest ? 0 : 3);
	Set(TEXT("r.ScreenPercentage"), FMath::RoundToInt(RenderScale * 100.f));
}

void AKnightCharacter::SetGraphicsLevel(int32 Level)
{
	GraphicsLevel = FMath::Clamp(Level, 0, 1);
	ApplyGraphics();
}

void AKnightCharacter::SetRenderScale(float Value)
{
	const float New = FMath::Clamp(Value, 0.35f, 1.0f);
	if (FMath::IsNearlyEqual(New, RenderScale, 0.005f)) return;
	RenderScale = New;
	if (GConfig) GConfig->SetFloat(TEXT("Jeebsland"), TEXT("RenderScale"), RenderScale, GGameUserSettingsIni);
	ApplyGraphics();
}

void AKnightCharacter::SetLookSensitivity(float Value)
{
	LookSensitivity = FMath::Clamp(Value, 0.05f, 2.0f);
	if (GConfig)
	{
		GConfig->SetFloat(TEXT("Jeebsland"), TEXT("LookSensitivity"), LookSensitivity, GGameUserSettingsIni);
	}
}

void AKnightCharacter::OnInventoryKey()
{
	// A screen is open (shop, smithy, bank, a conversation, the skills board): Tab closes it, as it always has
	if (IsUIOpen())
	{
		if (!bSettingsOpen) ToggleInventory();
		return;
	}
	// Otherwise Tab frees the mouse cursor to use the side panel, or puts it away again
	bPanelCursor = !bPanelCursor;
	RefreshUIMode();
}

void AKnightCharacter::SetPanelAlways(bool bOn)
{
	bPanelAlways = bOn;
	if (GConfig) GConfig->SetBool(TEXT("Jeebsland"), TEXT("PanelAlways"), bPanelAlways, GGameUserSettingsIni);
}

// ---------------- Looting a sack ----------------
void AKnightCharacter::OpenLoot(ALootDrop* Drop)
{
	if (!Drop) return;
	LootTarget = Drop;
	SetPanelCursor(true);   // the cursor comes out, like Tab, so you can take things
}
