#include "PvpSigns.h"
#include "PvpZones.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "JeebWater.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

APvpSigns::APvpSigns()
{
	PrimaryActorTick.bCanEverTick = !UE_BUILD_SHIPPING;
	PrimaryActorTick.bStartWithTickEnabled = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	SetReplicates(false);
}

void APvpSigns::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebPvpTest"))) SetActorTickEnabled(true);
#endif
	if (IsNetMode(NM_DedicatedServer)) return;   // (nothing to see on a server)
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* ColorMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	if (!Cube || !ColorMat) { UE_LOG(LogTemp, Warning, TEXT("JEEBPVP signs: no cube or colour material")); return; }
	auto Make = [&](const FLinearColor& Col)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetupAttachment(GetRootComponent());
		C->SetMobility(EComponentMobility::Static);
		C->SetStaticMesh(Cube);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetCullDistances(0, 60000);
		C->RegisterComponent();
		if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, ColorMat)) M->SetVectorParameterValue(TEXT("Color"), Col);
		return C;
	};
	UInstancedStaticMeshComponent* Wood = Make(FLinearColor(0.07f, 0.045f, 0.03f));
	UInstancedStaticMeshComponent* Red = Make(FLinearColor(0.55f, 0.03f, 0.03f));
	UInstancedStaticMeshComponent* White = Make(FLinearColor(0.9f, 0.88f, 0.82f));
	int32 Count = 0;
	const TArray<JeebPvp::FZone>& Zones = JeebPvp::Zones();
	for (int32 zi = 0; zi < Zones.Num(); ++zi)
	{
		const JeebPvp::FZone& Z = Zones[zi];
		if (Z.SignEvery < 500.f) continue;
		// the four sides: a start, the way along, and the way out (yaw), each clipped to the map
		struct FSide { FVector2D From, Along; float Len, Yaw; };
		const FSide Sides[4] = {
			{ FVector2D(Z.Min.X, Z.Min.Y), FVector2D(0.f, 1.f), Z.Max.Y - Z.Min.Y, 180.f },
			{ FVector2D(Z.Max.X, Z.Min.Y), FVector2D(0.f, 1.f), Z.Max.Y - Z.Min.Y, 0.f },
			{ FVector2D(Z.Min.X, Z.Min.Y), FVector2D(1.f, 0.f), Z.Max.X - Z.Min.X, -90.f },
			{ FVector2D(Z.Min.X, Z.Max.Y), FVector2D(1.f, 0.f), Z.Max.X - Z.Min.X, 90.f } };
		for (const FSide& S : Sides)
		{
			const FVector2D Out(FMath::Cos(FMath::DegreesToRadians(S.Yaw)), FMath::Sin(FMath::DegreesToRadians(S.Yaw)));
			for (float D = 0.f; D <= S.Len; D += Z.SignEvery)
			{
				const FVector2D P = S.From + S.Along * D + Out * 300.f;
				if (P.X < JeebWorld::MinX + 1500.f || P.X > JeebWorld::MaxX - 1500.f || P.Y < JeebWorld::MinY + 1500.f || P.Y > JeebWorld::MaxY - 1500.f) continue;   // (off the map)
				if (JeebPvp::ZoneAt(P.X, P.Y) != INDEX_NONE) continue;   // (inside another zone: no line there)
				const float G = JeebTerrain::HeightAt(P.X, P.Y);
				float Surf = -1.0e9f;
				if (JeebWater::SurfaceAt(P.X, P.Y, Surf) && Surf > G - 30.f) continue;   // (in the water)
				const FRotator Yaw(0.f, S.Yaw, 0.f);
				const FVector At(P.X, P.Y, G);
				auto Put = [&](UInstancedStaticMeshComponent* C, const FVector& Local, const FRotator& Rot, const FVector& Scale)
				{ C->AddInstance(FTransform(Yaw.Quaternion() * Rot.Quaternion(), At + Yaw.RotateVector(Local), Scale), true); };
				Put(Wood, FVector(0.f, 0.f, 140.f), FRotator::ZeroRotator, FVector(0.14f, 0.14f, 2.9f));       // the post
				Put(Wood, FVector(0.f, 0.f, 255.f), FRotator::ZeroRotator, FVector(0.10f, 1.75f, 0.12f));     // a frame bar above the board
				Put(Red, FVector(0.f, 0.f, 215.f), FRotator::ZeroRotator, FVector(0.08f, 1.6f, 0.9f));        // the board, 160 x 90
				Put(White, FVector(0.f, 0.f, 215.f), FRotator(0.f, 0.f, 45.f), FVector(0.13f, 0.95f, 0.09f)); // the cross
				Put(White, FVector(0.f, 0.f, 215.f), FRotator(0.f, 0.f, -45.f), FVector(0.13f, 0.95f, 0.09f));
				Put(White, FVector(0.f, 0.f, 183.f), FRotator::ZeroRotator, FVector(0.13f, 1.45f, 0.06f));    // a stripe along the foot of the board
				++Count;
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBPVP %d warning signs round %d zone(s)"), Count, Zones.Num());
}
