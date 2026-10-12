#include "Cottage.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ACottage::ACottage()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CubeMesh = CubeFinder.Object;

	// One box: centre, full size (cm), colour, whether you bump into it, optional rotation
		auto Box = [&](FVector Loc, FVector Size, FLinearColor Color, bool bCollide, FRotator Rot = FRotator::ZeroRotator, bool bCameraOnly = false)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Part%d"), Parts.Num()));
		C->SetupAttachment(Root);
		C->SetStaticMesh(CubeMesh);
		C->SetRelativeLocation(Loc);
		C->SetRelativeRotation(Rot);
		C->SetRelativeScale3D(Size / 100.f);
				C->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (bCameraOnly)
		{
			// Roofs and gable ends are solid to the camera (it cannot slide in through the ceiling), but never get in your way
			C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			C->SetCollisionResponseToAllChannels(ECR_Ignore);
			C->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		}
		Parts.Add(TPair<UStaticMeshComponent*, FLinearColor>(C, Color));
	};

	const FLinearColor Plaster(0.80f, 0.72f, 0.55f);
	const FLinearColor Timber(0.20f, 0.11f, 0.05f);
	const FLinearColor Thatch(0.62f, 0.48f, 0.20f);
	const FLinearColor ThatchDark(0.45f, 0.33f, 0.13f);
	const FLinearColor Stone(0.42f, 0.40f, 0.38f);
	const FLinearColor Wood(0.38f, 0.23f, 0.10f);
	const FLinearColor Glass(0.08f, 0.12f, 0.18f);

	const float Z0 = 10.f;   // top of the floor
	const float WH = 280.f;  // wall height
	const float T = 30.f;    // wall thickness

	// ---- Floor and walls (700 x 500 room; the doorway is 170 wide and 230 tall) ----
	Box(FVector(0, 0, 5), FVector(720, 520, 10), Wood, true);
	Box(FVector(-335, 0, Z0 + WH / 2),        FVector(T, 500, WH),  Plaster, true); // back
	Box(FVector(0, -235, Z0 + WH / 2),        FVector(700, T, WH),  Plaster, true); // left
	Box(FVector(0, 235, Z0 + WH / 2),         FVector(700, T, WH),  Plaster, true); // right
	Box(FVector(335, -167.5f, Z0 + WH / 2),   FVector(T, 165, WH),  Plaster, true); // front left of door
	Box(FVector(335, 167.5f, Z0 + WH / 2),    FVector(T, 165, WH),  Plaster, true); // front right of door
	Box(FVector(335, 0, Z0 + 255),            FVector(T, 170, 50),  Plaster, true); // above the door
	Box(FVector(385, 0, 2.5f),                FVector(70, 170, 5),  Stone, true);   // doorstep

	// ---- Dark timber framing ----
	for (int32 sx = -1; sx <= 1; sx += 2)
	{
		for (int32 sy = -1; sy <= 1; sy += 2)
		{
			Box(FVector(sx * 350.f, sy * 250.f, Z0 + WH / 2 + 5), FVector(28, 28, WH + 10), Timber, false); // corner posts
		}
	}
	for (int32 s = -1; s <= 1; s += 2)
	{
		Box(FVector(0, s * 252.f, Z0 + WH - 15), FVector(724, 8, 30), Timber, false); // top beams on the long walls
		const float Xs[] = { -300.f, -85.f, 85.f, 300.f };
		for (float X : Xs)
		{
			Box(FVector(X, s * 252.f, Z0 + 125), FVector(14, 8, 250), Timber, false);  // studs
		}
		Box(FVector(0, s * 252.f, Z0 + 20), FVector(724, 8, 40), Stone, false);        // stone base band
		Box(FVector(352, s * 167.f, Z0 + 125), FVector(8, 14, 250), Timber, false);    // front studs
		Box(FVector(352, s * 167.5f, Z0 + 20), FVector(8, 165, 40), Stone, false);     // front stone base
	}
	Box(FVector(-352, 0, Z0 + WH - 15), FVector(8, 524, 30), Timber, false); // back top beam
	Box(FVector(352, 0, Z0 + WH - 15), FVector(8, 524, 30), Timber, false);  // front top beam
	Box(FVector(-352, 0, Z0 + 20), FVector(8, 524, 40), Stone, false);       // back stone base
	for (float Y : { -150.f, -50.f, 50.f, 150.f })
	{
		Box(FVector(-352, Y, Z0 + 125), FVector(8, 14, 250), Timber, false);   // back studs
	}

	// ---- Doorway: frame, and a door swung open outside ----
	for (int32 s = -1; s <= 1; s += 2)
	{
		Box(FVector(350, s * 92.f, Z0 + 120), FVector(22, 14, 240), Timber, false);
	}
	Box(FVector(350, 0, Z0 + 246), FVector(22, 198, 14), Timber, false);

	// The door itself: hinged on the left of the doorway, swings outward. E opens and closes it.
	DoorPivot = CreateDefaultSubobject<USceneComponent>(TEXT("DoorPivot"));
	DoorPivot->SetupAttachment(Root);
	DoorPivot->SetRelativeLocation(FVector(337, -85, Z0));
	auto DoorBox = [&](FVector Loc, FVector Size, FLinearColor Color) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("DoorPart%d"), Parts.Num()));
		C->SetupAttachment(DoorPivot);
		C->SetStaticMesh(CubeMesh);
		C->SetRelativeLocation(Loc);
		C->SetRelativeScale3D(Size / 100.f);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Parts.Add(TPair<UStaticMeshComponent*, FLinearColor>(C, Color));
		return C;
	};
	DoorBox(FVector(0, 85, 110), FVector(8, 170, 220), Wood);
	DoorBox(FVector(0, 85, 150), FVector(10, 170, 8), Timber);      // cross boards
	DoorBox(FVector(0, 85, 60), FVector(10, 170, 8), Timber);
	DoorBox(FVector(0, 150, 105), FVector(16, 10, 10), FLinearColor(0.9f, 0.7f, 0.15f));   // handle
	LockMesh = DoorBox(FVector(8, 140, 85), FVector(8, 22, 28), FLinearColor(1.0f, 0.75f, 0.1f));   // padlock (shown only while locked)
	LockMesh->SetVisibility(false);
	DoorSolid = CreateDefaultSubobject<UBoxComponent>(TEXT("DoorSolid"));
	DoorSolid->SetupAttachment(DoorPivot);
	DoorSolid->SetBoxExtent(FVector(6, 85, 110));
	DoorSolid->SetRelativeLocation(FVector(0, 85, 110));
	DoorSolid->SetCollisionProfileName(TEXT("BlockAll"));
	DoorSolid->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// The stash of ingots (a crate in the back corner), only visible in locked huts
	{
		auto LootBox = [&](FVector Loc, FVector Size, FLinearColor Color)
		{
			UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Loot%d"), LootMeshes.Num()));
			C->SetupAttachment(Root);
			C->SetStaticMesh(CubeMesh);
			C->SetRelativeLocation(Loc);
			C->SetRelativeScale3D(Size / 100.f);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetVisibility(false);
			Parts.Add(TPair<UStaticMeshComponent*, FLinearColor>(C, Color));
			LootMeshes.Add(C);
		};
		LootBox(FVector(-270, 170, Z0 + 20), FVector(70, 70, 40), Wood);
		for (int32 k = 0; k < 6; ++k)
		{
			const int32 Row = k / 3, Col = k % 3;
			LootBox(FVector(-295 + Col * 24, 170 + (Row == 1 ? 8 : -8), Z0 + 46 + Row * 12), FVector(22, 12, 10), FLinearColor(1.0f, 0.78f, 0.12f));
		}
	}

	// ---- Windows (outside and inside), two on each long wall ----
	auto Window = [&](float X, float Side)
	{
		for (float Y : { Side * 255.f, Side * 215.f })
		{
			Box(FVector(X, Y, Z0 + 150), FVector(96, 10, 96), Timber, false); // frame
			Box(FVector(X, Y, Z0 + 150), FVector(70, 12, 70), Glass, false);  // glass
			Box(FVector(X, Y, Z0 + 150), FVector(6, 14, 70), Timber, false);  // bars
			Box(FVector(X, Y, Z0 + 150), FVector(70, 14, 6), Timber, false);
		}
		// flower box under the window, outside
		Box(FVector(X, Side * 268.f, Z0 + 92), FVector(84, 22, 18), Wood, false);
		Box(FVector(X, Side * 268.f, Z0 + 104), FVector(78, 18, 8), FLinearColor(0.10f, 0.40f, 0.10f), false);
		Box(FVector(X - 20, Side * 268.f, Z0 + 111), FVector(14, 12, 8), FLinearColor(0.85f, 0.25f, 0.40f), false);
		Box(FVector(X + 5, Side * 268.f, Z0 + 111), FVector(14, 12, 8), FLinearColor(0.95f, 0.85f, 0.30f), false);
		Box(FVector(X + 28, Side * 268.f, Z0 + 111), FVector(14, 12, 8), FLinearColor(0.85f, 0.25f, 0.40f), false);
	};
	for (float X : { -170.f, 170.f })
	{
		Window(X, -1.f);
		Window(X, 1.f);
	}

	// ---- Gable ends: stepped fill under the sloped roof (front and back) ----
	for (int32 k = 0; k < 5; ++k)
	{
		const float Width = 500.f * (1.f - (k + 0.5f) / 5.f);
		const float Z = Z0 + WH + 17.5f + 35.f * k;
				Box(FVector(335, 0, Z),  FVector(T, Width, 35), Plaster, false, FRotator::ZeroRotator, true);
		Box(FVector(-335, 0, Z), FVector(T, Width, 35), Plaster, false, FRotator::ZeroRotator, true);
	}

	// ---- Thatched roof: two slopes meeting at a ridge, with an overhang ----
	Box(FVector(0, 150, 372),  FVector(790, 366, 22), Thatch, false, FRotator(0.f, 0.f, 35.f), true);
	Box(FVector(0, -150, 372), FVector(790, 366, 22), Thatch, false, FRotator(0.f, 0.f, -35.f), true);
	Box(FVector(0, 0, 489), FVector(800, 44, 24), ThatchDark, false, FRotator::ZeroRotator, true); // ridge

	// ---- Chimney ----
	Box(FVector(-368, 0, 290), FVector(56, 130, 560), Stone, false, FRotator::ZeroRotator, true);
	Box(FVector(-368, 0, 575), FVector(72, 150, 20), FLinearColor(0.30f, 0.29f, 0.28f), false);

	// ---- Inside: hearth, table, stools, bed, rug ----
	Box(FVector(-298, 0, Z0 + 45), FVector(44, 160, 90), Stone, true);              // hearth
	Box(FVector(-275, 0, Z0 + 36), FVector(2, 90, 62), FLinearColor(0.01f, 0.01f, 0.01f), false); // fireplace opening
	Box(FVector(-278, 0, Z0 + 12), FVector(4, 40, 10), FLinearColor(1.0f, 0.45f, 0.08f), false);  // embers

	Box(FVector(60, 60, Z0 + 76), FVector(150, 95, 8), Wood, true);                   // table top
	for (int32 sx = -1; sx <= 1; sx += 2)
	{
		for (int32 sy = -1; sy <= 1; sy += 2)
		{
			Box(FVector(60 + sx * 62.f, 60 + sy * 38.f, Z0 + 36), FVector(8, 8, 72), Wood, false);
		}
	}
	Box(FVector(60, 135, Z0 + 20),  FVector(38, 38, 40), Wood, false);                // stools
	Box(FVector(60, -15, Z0 + 20),  FVector(38, 38, 40), Wood, false);

	Box(FVector(-210, -165, Z0 + 15), FVector(180, 90, 30), Wood, true);               // bed frame
	Box(FVector(-210, -165, Z0 + 36), FVector(172, 82, 12), FLinearColor(0.85f, 0.80f, 0.68f), false); // mattress
	Box(FVector(-285, -165, Z0 + 47), FVector(30, 60, 10), FLinearColor(0.92f, 0.90f, 0.85f), false);  // pillow
	Box(FVector(-170, -165, Z0 + 44), FVector(100, 84, 6), FLinearColor(0.55f, 0.10f, 0.10f), false);  // blanket
	Box(FVector(20, -70, Z0 + 0.8f), FVector(220, 150, 1.6f), FLinearColor(0.45f, 0.15f, 0.12f), false); // rug

	// ---- Lantern on a post by the door ----
	Box(FVector(395, 125, Z0 + 100), FVector(6, 6, 200), Timber, false);
	Box(FVector(395, 125, Z0 + 205), FVector(20, 20, 28), FLinearColor(1.0f, 0.78f, 0.35f), false);

	// ---- Warm light inside ----
	UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("InteriorLight"));
	Light->SetupAttachment(Root);
	Light->SetRelativeLocation(FVector(0, 0, 230));
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensity(120.f);
	Light->SetLightColor(FLinearColor(1.0f, 0.72f, 0.42f));
	Light->SetAttenuationRadius(700.f);
	Light->SetCastShadows(false); // many huts now, so their lights stay cheap
}

void ACottage::BeginPlay()
{
	Super::BeginPlay();

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (const TPair<UStaticMeshComponent*, FLinearColor>& Part : Parts)
	{
		if (UMaterialInstanceDynamic* M = Part.Key->CreateDynamicMaterialInstance(0, Base))
		{
			M->SetVectorParameterValue(TEXT("Color"), Part.Value);
		}
	}
}

// ---------------------------------------------------------------- doors and locks
void ACottage::Configure(int32 InLockLevel, int32 InLoot)
{
	LockLevel = InLockLevel;
	bLocked = LockLevel > 0;
	LootAmount = InLoot;
	bLootThere = bLocked && LootAmount > 0;
	ShowLootAndLock();
}

void ACottage::ShowLootAndLock()
{
	if (LockMesh) LockMesh->SetVisibility(bLocked);
	for (UStaticMeshComponent* M : LootMeshes) if (M) M->SetVisibility(bLootThere);
}

FVector ACottage::DoorPos() const
{
	return GetActorTransform().TransformPosition(FVector(380.f, 0.f, 10.f));
}

FVector ACottage::LootPos() const
{
	return GetActorTransform().TransformPosition(FVector(-250.f, 150.f, 10.f));
}

void ACottage::Unlock()
{
	bLocked = false;
	bOpen = true;
	ShowLootAndLock();
}

void ACottage::SetDoorState(bool bInOpen, bool bInLocked)
{
	const bool bLockChanged = bInLocked != bLocked;
	bOpen = bInOpen;
	bLocked = bInLocked;
	if (bLockChanged) ShowLootAndLock();
}

void ACottage::ToggleDoor()
{
	if (bLocked) return;
	bOpen = !bOpen;
	if (!bOpen && LockLevel > 0)
	{
		bLocked = true;      // shutting a hut with a lock locks it again
		ShowLootAndLock();
	}
}

int32 ACottage::TakeLoot()
{
	if (!HasLoot()) return 0;
	bLootThere = false;
	LootRespawn = 300.f;   // five minutes
	ShowLootAndLock();
	return LootAmount;
}

void ACottage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Target = bOpen ? -100.f : 0.f;
	if (!FMath::IsNearlyEqual(DoorAngle, Target, 0.1f))
	{
		DoorAngle = FMath::FInterpTo(DoorAngle, Target, DeltaSeconds, 5.f);
		DoorPivot->SetRelativeRotation(FRotator(0.f, DoorAngle, 0.f));
		DoorSolid->SetCollisionEnabled(FMath::Abs(DoorAngle) < 25.f ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	if (LootAmount > 0 && !bLootThere && bLocked)
	{
		LootRespawn -= DeltaSeconds;
		if (LootRespawn <= 0.f) { bLootThere = true; ShowLootAndLock(); }
	}
}
