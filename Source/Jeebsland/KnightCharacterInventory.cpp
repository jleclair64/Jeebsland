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

// (these ask the server: see Inv in KnightCharacterNet.cpp)
void AKnightCharacter::EquipFromInventory(int32 Idx) { Inv(EInvOp::Equip, Idx); }
void AKnightCharacter::UnequipHotbar(int32 H) { Inv(EInvOp::UnequipHot, H); }
void AKnightCharacter::UnequipWorn(int32 Kind, bool bSecondRing) { Inv(EInvOp::UnequipWorn, Kind, bSecondRing ? 1 : 0); }

// R (2026-11-03): the bar is the hand slot and the bow slot; R swaps between them (never to an empty one)
void AKnightCharacter::CycleHotbar(int32 Dir)
{
	if (!Skills || IsUIOpen() || bClimbing || InClimbMove()) return;
	const int32 Other = Skills->Selected == 0 ? 1 : 0;
	if (Skills->Hotbar[Other] != 0) SelectHotbar(Other);
}

void AKnightCharacter::SelectHotbar(int32 H)
{
	if (!Skills || H < 0 || H >= JeebItems::NumHotbar || Skills->Selected == H || Skills->Hotbar[H] == 0) return;   // (an empty slot is never taken in hand: the server refuses it too)
	// The hand changes here straight away; online the server is told and follows
	if (HasAuthority()) Skills->SetSelected(H);
	else
	{
		Skills->Selected = H;
		Skills->LocalSelectTime = FPlatformTime::Seconds();
		Inv(EInvOp::Select, H);
	}
	CancelDraw();   // (a drawn bow is let down without shooting)
	bAttacking = false;
	bCharging = false;
	RefreshEquipment();
}

// Gathering needs the right tool in hand (2026-11-03: no more picking it off the bar for you; equip it from the pack).
// Called on the server by the resource; better tools work faster.
bool AKnightCharacter::PrepareTool(ESkillType Skill, float& OutTimeMult)
{
	OutTimeMult = 1.f;
	if (!Skills) return true;
	int32 Need;
	const TCHAR* Name;
	const TCHAR* Verb;
	if (Skill == ESkillType::Mining) { Need = JeebItems::K_Pickaxe; Name = TEXT("pickaxe"); Verb = TEXT("mine"); }
	else if (Skill == ESkillType::Woodcutting) { Need = JeebItems::K_Axe; Name = TEXT("axe"); Verb = TEXT("chop trees"); }
	else if (Skill == ESkillType::Fishing) { Need = JeebItems::K_Rod; Name = TEXT("fishing pole"); Verb = TEXT("fish"); }
	else return true;
	const int32 Id = Skills->Hotbar[0];
	if (Skills->Selected != 0 || !JeebItems::IsGear(Id) || JeebItems::KindOf(Id) != Need)
	{
		Skills->Say(3.f, FColor::Orange, FString::Printf(TEXT("Hold a %s to %s: equip one from your pack (Tab)."), Name, Verb));
		return false;
	}
	OutTimeMult = JeebItems::ToolTimeMult(JeebItems::VariantOf(Id));
	return true;
}

// ---------------- Dropping things on the ground ----------------
void AKnightCharacter::DropBag(const TArray<FInvSlot>& Items, int32 Ingots)
{
	if (!HasAuthority()) return;   // drops are made by the server
	const FVector Where = GetActorLocation() + GetActorForwardVector() * 130.f;
	const FVector Ground(Where.X, Where.Y, JeebTerrain::HeightAt(Where.X, Where.Y));
	// Things dropped in the same spot don't pile up through each other: next to a sack they go into it, and once three or
	// more loose things lie close together they are gathered into one sack
	{
		constexpr float Near = 170.f;
		ALootDrop* Sack = nullptr;
		TArray<ALootDrop*> Loose;
		for (TActorIterator<ALootDrop> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), Ground) > Near || It->IsActorBeingDestroyed()) continue;
			if (It->IsSack()) Sack = *It;
			else if (It->IsItemLook()) Loose.Add(*It);
		}
		if (Sack) { Sack->AddLoose(Items, Ingots); return; }
		if (Loose.Num() >= 2)
		{
			TArray<FInvSlot> All = Items;
			int32 AllIngots = Ingots;
			FVector Mid = Ground;
			for (ALootDrop* L : Loose)
			{
				for (const FInvSlot& S : L->GetItems()) if (S.Id != 0 && S.Count > 0) All.Add(S);
				AllIngots += L->GetIngots();
				Mid += L->GetActorLocation();
			}
			Mid /= float(Loose.Num() + 1);
			for (ALootDrop* L : Loose) L->Destroy();
			const FVector At(Mid.X, Mid.Y, JeebTerrain::HeightAt(Mid.X, Mid.Y));
			if (ALootDrop* Drop = GetWorld()->SpawnActor<ALootDrop>(At, FRotator::ZeroRotator)) Drop->Setup(ELootLook::Pack, All, AllIngots);
			if (Skills) Skills->Say(2.5f, FColor(255, 220, 140), TEXT("The things here are gathered into a sack."));
			return;
		}
	}
	if (ALootDrop* Drop = GetWorld()->SpawnActor<ALootDrop>(Ground, FRotator::ZeroRotator)) Drop->Setup(Items.Num() <= 1 ? ELootLook::Item : ELootLook::Pack, Items, Ingots);   // one thing: show the thing itself
}

// (the server takes the things out of the pack and makes the bag: see DoInv)
void AKnightCharacter::DropFromPack(int32 SlotIdx, int32 Count) { Inv(EInvOp::DropPack, SlotIdx, Count); }
void AKnightCharacter::DropIngots(int32 Count) { Inv(EInvOp::DropIngots, Count); }
void AKnightCharacter::DropFromHotbar(int32 H) { Inv(EInvOp::DropHot, H); }

