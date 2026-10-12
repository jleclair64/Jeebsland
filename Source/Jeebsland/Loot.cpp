#include "Loot.h"
#include "Messages.h"
#include "Terrain.h"
#include "Inventory.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"

ALootDrop::ALootDrop()
{
	PrimaryActorTick.bCanEverTick = true;
	Origin = CreateDefaultSubobject<USceneComponent>(TEXT("Origin"));
	SetRootComponent(Origin);

	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto Make = [&](const TCHAR* Name)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Origin);
		C->SetStaticMesh(CubeMesh.Object);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		return C;
	};
	Body = Make(TEXT("Body"));
	Flap = Make(TEXT("Flap"));
	Tie = Make(TEXT("Tie"));
	Glow = Make(TEXT("Glow"));
}

void ALootDrop::Setup(ELootLook InLook, const TArray<FInvSlot>& InItems, int32 InIngots)
{
	Look = InLook;
	Items = InItems;
	Ingots = InIngots;
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	auto Paint = [&](UStaticMeshComponent* C, const FLinearColor& Col, const FVector& Loc, const FVector& Size, float Yaw)
	{
		C->SetRelativeLocation(Loc);
		C->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
		C->SetRelativeScale3D(Size / 100.f);
		if (UMaterialInstanceDynamic* Mid = C->CreateDynamicMaterialInstance(0, BaseMat)) Mid->SetVectorParameterValue(TEXT("Color"), Col);
	};
	switch (Look)
	{
	case ELootLook::Meat:
		Paint(Body, FLinearColor(0.85f, 0.32f, 0.34f), FVector(0, 0, 11), FVector(46, 28, 20), 20.f);
		Paint(Flap, FLinearColor(0.97f, 0.86f, 0.78f), FVector(0, 0, 22), FVector(40, 10, 4), 20.f);
		Paint(Tie, FLinearColor(0.80f, 0.28f, 0.30f), FVector(26, 20, 8), FVector(18, 14, 14), -10.f);
		break;
	case ELootLook::Beast:
		Paint(Body, FLinearColor(0.34f, 0.21f, 0.11f), FVector(0, 0, 7), FVector(64, 46, 14), 15.f);
		Paint(Flap, FLinearColor(0.90f, 0.86f, 0.74f), FVector(8, 6, 17), FVector(44, 7, 6), 70.f);
		Paint(Tie, FLinearColor(0.90f, 0.86f, 0.74f), FVector(-8, -6, 17), FVector(34, 7, 6), -20.f);
		break;
	case ELootLook::Pack:
	default:
	{
		// A big burlap sack: a round, slumped body, a gathered neck tied off with cord, and the cloth flaring above the tie
		UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
		if (Ball) { Body->SetStaticMesh(Ball); Tie->SetStaticMesh(Ball); }
		if (Cone) Flap->SetStaticMesh(Cone);
		Paint(Body, FLinearColor(0.50f, 0.38f, 0.22f), FVector(0, 0, 42), FVector(92, 84, 88), 0.f);    // the sack itself
		Paint(Tie, FLinearColor(0.22f, 0.13f, 0.06f), FVector(0, 0, 90), FVector(30, 30, 9), 0.f);      // the cord round its neck
		Paint(Flap, FLinearColor(0.46f, 0.35f, 0.20f), FVector(0, 0, 100), FVector(34, 34, 28), 180.f); // the gathered top, flaring out
		Flap->SetRelativeRotation(FRotator(180.f, 0.f, 0.f));   // the cone stands upside down: narrow at the tie, wide at the top
		break;
	}
	}
	GlowBase = FVector(0.f, 0.f, Look == ELootLook::Pack ? 128.f : 78.f);
	Paint(Glow, FLinearColor(4.f, 3.f, 0.6f), GlowBase, FVector(9, 9, 9), 0.f);
	SetLifeSpan(240.f);
}

void ALootDrop::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	Glow->SetRelativeLocation(GlowBase + FVector(0.f, 0.f, 6.f * FMath::Sin(Clock * 3.f)));
	Glow->SetRelativeRotation(FRotator(0.f, Clock * 120.f, 0.f));
}

FString ALootDrop::Describe() const
{
	FString Out;
	for (const FInvSlot& S : Items)
	{
		if (S.Id == 0 || S.Count <= 0) continue;
		const FString Nm = JeebItems::IsMaterial(S.Id) ? FString(JeebItems::MaterialName(JeebItems::MaterialType(S.Id))) : JeebItems::ItemName(S.Id);
		if (!Out.IsEmpty()) Out += TEXT(", ");
		Out += FString::Printf(TEXT("%s x%d"), *Nm, S.Count);
	}
	if (Ingots > 0)
	{
		if (!Out.IsEmpty()) Out += TEXT(", ");
		Out += FString::Printf(TEXT("%d ingots"), Ingots);
	}
	if (Out.IsEmpty()) Out = TEXT("nothing");
	const TCHAR* What = (Look == ELootLook::Pack) ? TEXT("the pack") : TEXT("the loot");
	return FString::Printf(TEXT("Press E to take %s (%s)"), What, *Out);
}

void ALootDrop::TakeAll(AActor* Taker)
{
	USkillComponent* Sk = Taker ? Taker->FindComponentByClass<USkillComponent>() : nullptr;
	if (!Sk) return;
	bool bLeft = false;
	FString Got;
	for (FInvSlot& S : Items)
	{
		if (S.Id == 0 || S.Count <= 0) continue;
		if (Sk->CanAdd(S.Id, S.Count) && Sk->AddInvItem(S.Id, S.Count))
		{
			const FString Nm = JeebItems::IsMaterial(S.Id) ? FString(JeebItems::MaterialName(JeebItems::MaterialType(S.Id))) : JeebItems::ItemName(S.Id);
			if (!Got.IsEmpty()) Got += TEXT(", ");
			Got += FString::Printf(TEXT("%s x%d"), *Nm, S.Count);
			S.Id = 0; S.Count = 0;
		}
		else bLeft = true;
	}
	if (Ingots > 0)
	{
		Sk->AddIngots(Ingots);
		if (!Got.IsEmpty()) Got += TEXT(", ");
		Got += FString::Printf(TEXT("%d ingots"), Ingots);
		Ingots = 0;
	}
	if (GEngine)
	{
		if (!Got.IsEmpty()) JeebMsg::Post(3.f, FColor::Yellow, FString::Printf(TEXT("You take: %s"), *Got));
		if (bLeft) JeebMsg::Post(3.f, FColor::Orange, TEXT("Your pack is full. Some is left behind."));
	}
	if (!bLeft) Destroy();
}

static ALootDrop* SpawnDrop(UWorld* World, const FVector& Where, ELootLook Look, const TArray<FInvSlot>& Items, int32 Ingots)
{
	if (!World) return nullptr;
	// Settle it a little to one side so a pile of drops does not hide itself
	const float X = Where.X + FMath::FRandRange(-40.f, 40.f), Y = Where.Y + FMath::FRandRange(-40.f, 40.f);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALootDrop* D = World->SpawnActor<ALootDrop>(FVector(X, Y, JeebTerrain::HeightAt(X, Y) + 2.f), FRotator::ZeroRotator, P);
	if (D) D->Setup(Look, Items, Ingots);
	return D;
}

ALootDrop* ALootDrop::SpawnPork(UWorld* World, const FVector& Where)
{
	TArray<FInvSlot> L;
	L.Add(FInvSlot{ JeebItems::MaterialId(EItemType::Bacon), 2 });
	return SpawnDrop(World, Where, ELootLook::Meat, L, 0);
}

ALootDrop* ALootDrop::SpawnBeast(UWorld* World, const FVector& Where)
{
	TArray<FInvSlot> L;
	L.Add(FInvSlot{ JeebItems::MaterialId(EItemType::Hide), FMath::RandRange(1, 2) });
	L.Add(FInvSlot{ JeebItems::MaterialId(EItemType::Bones), FMath::RandRange(1, 3) });
	return SpawnDrop(World, Where, ELootLook::Beast, L, 0);
}

ALootDrop* ALootDrop::SpawnPack(UWorld* World, const FVector& Where, USkillComponent* From)
{
	if (!From) return nullptr;
	TArray<FInvSlot> L;
	for (FInvSlot& S : From->Slots)
	{
		if (S.Id != 0 && S.Count > 0) { L.Add(S); S.Id = 0; S.Count = 0; }
	}
	const int32 Gold = From->GetIngots();
	if (Gold > 0) From->SpendIngots(Gold);
	return SpawnDrop(World, Where, ELootLook::Pack, L, Gold);
}

void ALootDrop::SetupSack(const TArray<FInvSlot>& InItems, const TArray<uint8>& InWhere, int32 InIngots)
{
	Setup(ELootLook::Pack, InItems, InIngots);
	Where = InWhere;
	Where.SetNumZeroed(Items.Num());
	SetLifeSpan(600.f);   // a death sack waits ten minutes
}

bool ALootDrop::TakeOne(int32 Index, AActor* Taker)
{
	USkillComponent* Sk = Taker ? Taker->FindComponentByClass<USkillComponent>() : nullptr;
	if (!Sk || !Items.IsValidIndex(Index) || Items[Index].Id == 0) return false;
	FInvSlot& S = Items[Index];
	if (!Sk->CanAdd(S.Id, S.Count) || !Sk->AddInvItem(S.Id, S.Count))
	{
		JeebMsg::Post(2.5f, FColor::Orange, TEXT("Your pack is full."));
		return false;
	}
	S.Id = 0; S.Count = 0;
	bool bAnyLeft = Ingots > 0;
	for (const FInvSlot& I : Items) if (I.Id != 0) bAnyLeft = true;
	if (!bAnyLeft) Destroy();
	return true;
}

bool ALootDrop::EquipTo(int32 Index, AActor* Taker, int32 Hot, int32 Kind, bool bSecond)
{
	USkillComponent* Sk = Taker ? Taker->FindComponentByClass<USkillComponent>() : nullptr;
	if (!Sk || !Items.IsValidIndex(Index) || Items[Index].Id == 0 || !JeebItems::IsGear(Items[Index].Id)) return false;
	const int32 Id = Items[Index].Id, IK = JeebItems::KindOf(Id);
	int32* Target = nullptr;
	if (Hot >= 0)
	{
		if (Hot >= 5 || !JeebItems::IsWeaponKind(IK)) return false;   // only weapons go on the bar
		Target = &Sk->Hotbar[Hot];
	}
	else
	{
		const bool bMisc = (Kind == JeebItems::K_Ring || Kind == JeebItems::K_Amulet);
		if (bMisc ? !JeebItems::IsAccessoryKind(IK) : IK != Kind) return false;   // it has to belong in that slot
		Target = Sk->WornSlotPtr(Kind, bSecond);
	}
	if (!Target) return false;
	const int32 Old = *Target;
	*Target = Id;
	if (Old == 0 || (Sk->CanAdd(Old, 1) && Sk->AddInvItem(Old, 1))) { Items[Index].Id = 0; Items[Index].Count = 0; }
	else { Items[Index].Id = Old; Items[Index].Count = 1; }   // pack full: they trade places
	bool bAnyLeft = Ingots > 0;
	for (const FInvSlot& I : Items) if (I.Id != 0) bAnyLeft = true;
	if (!bAnyLeft) Destroy();
	return true;
}

void ALootDrop::TakeIngots(AActor* Taker)
{
	USkillComponent* Sk = Taker ? Taker->FindComponentByClass<USkillComponent>() : nullptr;
	if (!Sk || Ingots <= 0) return;
	Sk->AddIngots(Ingots);
	Ingots = 0;
	bool bAnyLeft = false;
	for (const FInvSlot& I : Items) if (I.Id != 0) bAnyLeft = true;
	if (!bAnyLeft) Destroy();
}
