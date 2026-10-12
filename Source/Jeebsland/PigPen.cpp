#include "PigPen.h"
#include "FoeKnight.h"
#include "Terrain.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

APigPen::APigPen()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	Post = MakeISM(TEXT("Post"), CylMesh.Object,  FLinearColor(0.28f, 0.16f, 0.07f));
	Rail = MakeISM(TEXT("Rail"), CubeMesh.Object, FLinearColor(0.34f, 0.20f, 0.09f));
	Hay  = MakeISM(TEXT("Hay"),  CubeMesh.Object, FLinearColor(0.80f, 0.62f, 0.20f));
	Mud  = MakeISM(TEXT("Mud"),  CylMesh.Object,  FLinearColor(0.16f, 0.09f, 0.04f));
}

UInstancedStaticMeshComponent* APigPen::MakeISM(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color)
{
	UInstancedStaticMeshComponent* C = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	C->SetupAttachment(RootComponent);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->SetCastShadow(false);
	C->bAffectDynamicIndirectLighting = false;
	C->bAffectDistanceFieldLighting = false;
	Tints.Add(TPair<UInstancedStaticMeshComponent*, FLinearColor>(C, Color));
	return C;
}

void APigPen::AddSolid(const FVector& Center, const FVector& HalfExtent)
{
	UBoxComponent* B = NewObject<UBoxComponent>(this);
	B->SetupAttachment(RootComponent);
	B->SetBoxExtent(HalfExtent);
	B->SetRelativeLocation(Center);
	B->SetCollisionProfileName(TEXT("BlockAll"));
	B->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	B->RegisterComponent();
}

void APigPen::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	for (const TPair<UInstancedStaticMeshComponent*, FLinearColor>& T : Tints)
	{
		if (UMaterialInstanceDynamic* M = T.Key->CreateDynamicMaterialInstance(0, Base))
		{
			M->SetVectorParameterValue(TEXT("Color"), T.Value);
		}
	}

	const FVector2D C = JeebTerrain::PenCenter();
	const FVector2D H = JeebTerrain::PenHalf();
	const float GateHalf = 140.f; // a 280 wide gap in the south fence (toward the start)
	auto Ground = [](float X, float Y) { return JeebTerrain::HeightAt(X, Y); };

	TMap<UInstancedStaticMeshComponent*, TArray<FTransform>> Batch;
	auto Put = [&](UInstancedStaticMeshComponent* Comp, const FTransform& T) { Batch.FindOrAdd(Comp).Add(T); };

	// One fence run from A to B (optionally skipping a gap in the middle)
	auto Fence = [&](FVector2D A, FVector2D B, bool bGap)
	{
		const FVector2D D = (B - A);
		const float Len = D.Size();
		const FVector2D Dir = D / Len;
		const FQuat Q = FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.f).Quaternion();
		auto Piece = [&](float U0, float U1)
		{
			const float L = U1 - U0;
			if (L < 10.f) return;
			const FVector2D Mid = A + Dir * ((U0 + U1) * 0.5f);
			const float Z = Ground(Mid.X, Mid.Y);
			Put(Rail, FTransform(Q, FVector(Mid.X, Mid.Y, Z + 50.f),  FVector(L / 100.f, 0.07f, 0.12f)));
			Put(Rail, FTransform(Q, FVector(Mid.X, Mid.Y, Z + 100.f), FVector(L / 100.f, 0.07f, 0.12f)));
			AddSolid(FVector(Mid.X, Mid.Y, Z + 80.f), FVector(L * 0.5f, 12.f, 80.f)); // knee-high wall to the knight, so no sneaking through
		};
		if (bGap)
		{
			const float M = Len * 0.5f;
			Piece(0.f, M - GateHalf);
			Piece(M + GateHalf, Len);
		}
		else
		{
			Piece(0.f, Len);
		}
		for (float U = 0.f; U <= Len + 1.f; U += 130.f)
		{
			if (bGap && FMath::Abs(U - Len * 0.5f) < GateHalf - 10.f) continue;
			const FVector2D PP = A + Dir * U;
			const float Z = Ground(PP.X, PP.Y);
			Put(Post, FTransform(FQuat::Identity, FVector(PP.X, PP.Y, Z + 62.f), FVector(0.14f, 0.14f, 1.24f)));
		}
		if (bGap)
		{
			for (int32 s = -1; s <= 1; s += 2)
			{
				const FVector2D GP = A + Dir * (Len * 0.5f + s * GateHalf);
				const float Z = Ground(GP.X, GP.Y);
				Put(Post, FTransform(FQuat::Identity, FVector(GP.X, GP.Y, Z + 85.f), FVector(0.22f, 0.22f, 1.7f)));
			}
		}
	};
	Fence(C + FVector2D(-H.X, -H.Y), C + FVector2D( H.X, -H.Y), true);  // south, with the gate gap
	Fence(C + FVector2D( H.X, -H.Y), C + FVector2D( H.X,  H.Y), false); // east
	Fence(C + FVector2D( H.X,  H.Y), C + FVector2D(-H.X,  H.Y), false); // north
	Fence(C + FVector2D(-H.X,  H.Y), C + FVector2D(-H.X, -H.Y), false); // west

	// Mud wallow, hay bales and a feeding trough
	{
		const float Zm = Ground(C.X - 350.f, C.Y + 150.f);
		Put(Mud, FTransform(FQuat::Identity, FVector(C.X - 350.f, C.Y + 150.f, Zm + 1.5f), FVector(3.6f, 2.6f, 0.03f)));
		for (int32 i = 0; i < 3; ++i)
		{
			const FVector2D HP(C.X + 450.f + 70.f * (i % 2), C.Y + 330.f - 80.f * i);
			Put(Hay, FTransform(FRotator(0.f, 20.f * i, 0.f).Quaternion(), FVector(HP.X, HP.Y, Ground(HP.X, HP.Y) + 25.f), FVector(0.9f, 0.6f, 0.5f)));
		}
		const float Zt = Ground(C.X + 300.f, C.Y - 300.f);
		Put(Rail, FTransform(FQuat::Identity, FVector(C.X + 300.f, C.Y - 300.f, Zt + 20.f), FVector(2.0f, 0.5f, 0.4f)));
		Put(Hay,  FTransform(FQuat::Identity, FVector(C.X + 300.f, C.Y - 300.f, Zt + 42.f), FVector(1.8f, 0.36f, 0.06f)));
	}

	for (TPair<UInstancedStaticMeshComponent*, TArray<FTransform>>& Pair : Batch)
	{
		Pair.Key->AddInstances(Pair.Value, false, false, false);
	}

	// No more pigs: the pen is now a sparring ring, kept by a knight in iron (see FoeKnight.cpp)
	if (AFoeKnight* Foe = GetWorld()->SpawnActor<AFoeKnight>(FVector(C.X, C.Y, Ground(C.X, C.Y) + 98.f), FRotator(0.f, -90.f, 0.f)))
	{
		Foe->Setup(C, H);
	}
}
