#include "Loot.h"
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
		Paint(Body, FLinearColor(0.30f, 0.20f, 0.10f), FVector(0, 0, 28), FVector(48, 38, 56), 0.f);
		Paint(Flap, FLinearColor(0.22f, 0.14f, 0.07f), FVector(0, 0, 58), FVector(50, 40, 12), 0.f);
		Paint(Tie, FLinearColor(0.95f, 0.72f, 0.12f), FVector(25, 0, 40), FVector(4, 10, 14), 0.f);
		break;
	}
	GlowBase = FVector(0.f, 0.f, 78.f);
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
		if (!Got.IsEmpty()) GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, FString::Printf(TEXT("You take: %s"), *Got));
		if (bLeft) GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange, TEXT("Your pack is full. Some is left behind."));
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
