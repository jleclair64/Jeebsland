#include "PvpSigns.h"
#include "PvpZones.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "JeebWater.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
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
	// (2026-11-27, "plain PvP signs": now a painted wooden board on two posts, a dark trim round it, a red diamond with two crossed swords
	// in steel and gold on each face, and the zone's name painted in cream under them, read from both sides)
	UInstancedStaticMeshComponent* Wood = Make(FLinearColor(0.21f, 0.11f, 0.05f));      // (rich brown, the board)
	UInstancedStaticMeshComponent* Dark = Make(FLinearColor(0.06f, 0.035f, 0.02f));      // (posts and trim)
	UInstancedStaticMeshComponent* Red = Make(FLinearColor(0.50f, 0.05f, 0.04f));
	UInstancedStaticMeshComponent* Steel = Make(FLinearColor(0.55f, 0.58f, 0.63f));
	UInstancedStaticMeshComponent* Gold = Make(FLinearColor(0.75f, 0.52f, 0.14f));
	Steel->SetCullDistances(0, 20000); Gold->SetCullDistances(0, 20000);
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
				// (sign's own frame: +X out of the zone, +Y along the line, +Z up; the board 200 x 110 cm, its middle 185 cm up)
				const float BZ = 185.f;
				for (const float y : { -90.f, 90.f })
				{
					Put(Dark, FVector(0.f, y, 135.f), FRotator(0.f, 0.f, y > 0.f ? 1.5f : -1.f), FVector(0.13f, 0.13f, 2.7f));   // the posts, a hair off true
					Put(Dark, FVector(0.f, y, 272.f), FRotator::ZeroRotator, FVector(0.16f, 0.16f, 0.06f));                       // their caps
				}
				Put(Wood, FVector(0.f, 0.f, BZ), FRotator::ZeroRotator, FVector(0.07f, 2.0f, 1.1f));          // the board
				Put(Dark, FVector(0.f, 0.f, BZ + 57.f), FRotator::ZeroRotator, FVector(0.10f, 2.12f, 0.07f)); // trim, top and bottom
				Put(Dark, FVector(0.f, 0.f, BZ - 57.f), FRotator::ZeroRotator, FVector(0.10f, 2.12f, 0.07f));
				Put(Dark, FVector(0.f, -103.f, BZ), FRotator::ZeroRotator, FVector(0.10f, 0.07f, 1.1f));     // and the ends
				Put(Dark, FVector(0.f, 103.f, BZ), FRotator::ZeroRotator, FVector(0.10f, 0.07f, 1.1f));
				for (const float Fc : { 1.f, -1.f })   // (both faces)
				{
					const FVector Mark(Fc * 4.f, 0.f, BZ + 20.f);
					Put(Red, Mark, FRotator(0.f, 0.f, 45.f), FVector(0.02f, 0.46f, 0.46f));   // the red diamond behind the swords
					for (const float Tilt : { 38.f, -38.f })
					{
						const FQuat RQ = FRotator(0.f, 0.f, Tilt).Quaternion();
						auto Bit = [&](UInstancedStaticMeshComponent* C, float Along, float Across, const FVector& Size)
						{ Put(C, Mark + FVector(Fc * 1.5f, 0.f, 0.f) + RQ.RotateVector(FVector(0.f, Across, Along)), RQ.Rotator(), Size); };
						Bit(Steel, 6.f, 0.f, FVector(0.02f, 0.055f, 0.50f));    // the blade, point up
						Bit(Steel, 31.5f, 0.f, FVector(0.02f, 0.03f, 0.03f));  // (a narrower tip)
						Bit(Gold, -19.f, 0.f, FVector(0.025f, 0.20f, 0.035f)); // the cross-guard
						Bit(Dark, -25.f, 0.f, FVector(0.025f, 0.035f, 0.10f)); // the grip
						Bit(Gold, -31.f, 0.f, FVector(0.03f, 0.055f, 0.045f)); // the pommel
					}
					UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
					T->SetupAttachment(GetRootComponent());
					T->SetMobility(EComponentMobility::Static);
					T->SetWorldLocationAndRotation(At + Yaw.RotateVector(FVector(Fc * 4.5f, 0.f, BZ - 33.f)), (Yaw.Quaternion() * FRotator(0.f, Fc > 0.f ? 0.f : 180.f, 0.f).Quaternion()).Rotator());
					T->SetHorizontalAlignment(EHTA_Center);
					T->SetVerticalAlignment(EVRTA_TextCenter);
					T->SetWorldSize(Z.Title.Len() > 14 ? 13.f : 16.f);
					T->SetTextRenderColor(FColor(240, 222, 178));
					T->SetText(FText::FromString(Z.Title));
					T->SetCastShadow(false);
					T->LDMaxDrawDistance = 6000.f;
					T->CachedMaxDrawDistance = 6000.f;
					T->RegisterComponent();
				}
				++Count;
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBPVP %d warning signs round %d zone(s)"), Count, Zones.Num());
}
