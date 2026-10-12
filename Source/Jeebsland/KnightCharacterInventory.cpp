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

void AKnightCharacter::EquipFromInventory(int32 Idx)
{
	if (!Skills) return;
	if (Skills->EquipFromSlot(Idx)) RefreshEquipment();
}

void AKnightCharacter::UnequipHotbar(int32 H)
{
	if (!Skills) return;
	if (Skills->UnequipHotbar(H)) RefreshEquipment();
	else if (GEngine) JeebMsg::Post(2.f, FColor::Orange, TEXT("No room in your inventory."));
}

void AKnightCharacter::UnequipWorn(int32 Kind, bool bSecondRing)
{
	if (!Skills) return;
	if (Skills->UnequipWorn(Kind, bSecondRing)) RefreshEquipment();
	else if (GEngine) JeebMsg::Post(2.f, FColor::Orange, TEXT("No room in your inventory."));
}

void AKnightCharacter::CycleHotbar(int32 Dir)
{
	if (!Skills || IsUIOpen() || bClimbing) return;
	for (int32 Step = 1; Step <= 5; ++Step)
	{
		const int32 Next = ((Skills->Selected + Dir * Step) % 5 + 5) % 5;
		if (Skills->Hotbar[Next] != 0) { SelectHotbar(Next); return; }
	}
}

void AKnightCharacter::SelectHotbar(int32 H)
{
	if (!Skills || H < 0 || H > 4 || Skills->Selected == H) return;
	Skills->Selected = H;
	bAttacking = false;
	bCharging = false;
	RefreshEquipment();
}

bool AKnightCharacter::PrepareTool(ESkillType Skill, float& OutTimeMult)
{
	OutTimeMult = 1.f;
	if (!Skills) return true;
	int32 Need;
	const TCHAR* Name;
	if (Skill == ESkillType::Mining) { Need = JeebItems::K_Pickaxe; Name = TEXT("pickaxe"); }
	else if (Skill == ESkillType::Woodcutting) { Need = JeebItems::K_Axe; Name = TEXT("axe"); }
	else if (Skill == ESkillType::Fishing) { Need = JeebItems::K_Rod; Name = TEXT("fishing pole"); }
	else return true;

	int32 Best = -1, BestTier = -1;
	for (int32 i = 0; i < 5; ++i)
	{
		const int32 Id = Skills->Hotbar[i];
		if (!JeebItems::IsGear(Id) || JeebItems::KindOf(Id) != Need) continue;
		// Keep the one already in hand if it is a tool of this kind
		const int32 Tier = JeebItems::VariantOf(Id) + (i == Skills->Selected ? 100 : 0);
		if (Tier > BestTier) { BestTier = Tier; Best = i; }
	}
	if (Best < 0)
	{
		if (GEngine) JeebMsg::Post(3.f, FColor::Orange, FString::Printf(TEXT("You need a %s on your weapon bar. Buy or smith one."), Name));
		return false;
	}
	if (Skills->Selected != Best) SelectHotbar(Best);
	OutTimeMult = JeebItems::ToolTimeMult(JeebItems::VariantOf(Skills->Hotbar[Best]));
	return true;
}

// ---------------- Dropping things on the ground ----------------
void AKnightCharacter::DropBag(const TArray<FInvSlot>& Items, int32 Ingots)
{
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
			JeebMsg::Post(2.5f, FColor(255, 220, 140), TEXT("The things here are gathered into a sack."));
			return;
		}
	}
	if (ALootDrop* Drop = GetWorld()->SpawnActor<ALootDrop>(Ground, FRotator::ZeroRotator)) Drop->Setup(Items.Num() <= 1 ? ELootLook::Item : ELootLook::Pack, Items, Ingots);   // one thing: show the thing itself
}

void AKnightCharacter::DropFromPack(int32 SlotIdx, int32 Count)
{
	if (!Skills || !Skills->Slots.IsValidIndex(SlotIdx)) return;
	FInvSlot& S = Skills->Slots[SlotIdx];
	if (S.Id == 0 || S.Count <= 0) return;
	const int32 N = FMath::Clamp(Count, 1, S.Count);
	TArray<FInvSlot> Items;
	FInvSlot D; D.Id = S.Id; D.Count = N;
	Items.Add(D);
	S.Count -= N;
	if (S.Count <= 0) { S.Id = 0; S.Count = 0; }
	DropBag(Items, 0);
	RefreshEquipment();
}

void AKnightCharacter::DropIngots(int32 Count)
{
	if (!Skills) return;
	const int32 N = FMath::Clamp(Count, 0, Skills->GetIngots());
	if (N <= 0 || !Skills->SpendIngots(N)) return;
	DropBag(TArray<FInvSlot>(), N);
}

void AKnightCharacter::DropFromHotbar(int32 H)
{
	if (!Skills || H < 0 || H >= 5 || Skills->Hotbar[H] == 0) return;
	TArray<FInvSlot> Items;
	FInvSlot D; D.Id = Skills->Hotbar[H]; D.Count = 1;
	Items.Add(D);
	Skills->Hotbar[H] = 0;
	DropBag(Items, 0);
	RefreshEquipment();
}

