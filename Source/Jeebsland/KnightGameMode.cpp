#include "KnightGameMode.h"
#include "GameFramework/GameUserSettings.h"
#include "KnightCharacter.h"
#include "GrassField.h"
#include "Meadow.h"
#include "MiningRock.h"
#include "ChoppableTree.h"
#include "Forest.h"
#include "AutumnDecor.h"
#include "Paths.h"
#include "Village.h"
#include "PigPen.h"
#include "Moon.h"
#include "DayNight.h"
#include "NightSky.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerController.h"
#include "BatSwarm.h"
#include "Cottage.h"
#include "Pond.h"
#include "TreeFort.h"
#include "Castle.h"
#include "Horse.h"
#include "SmokeCloud.h"
#include "KnightHUD.h"
#include "Terrain.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"

AKnightGameMode::AKnightGameMode()
{
	DefaultPawnClass = AKnightCharacter::StaticClass();
	HUDClass = AKnightHUD::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

// Light haze everywhere, a little thicker among dense trees and over the graveyard
void AKnightGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Test screenshots (launch with -JeebShot): aim the camera, wait for things to settle, save, quit
	if (bShotMode)
	{
		ShotClock += DeltaSeconds;
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC) PC->SetControlRotation(ShotRot);
		if (ShotClock > 9.f && !bShotTaken)
		{
			bShotTaken = true;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / (ShotName + TEXT(".png")), true, false);
		}
		if (ShotClock > 11.f && PC) PC->ConsoleCommand(TEXT("quit"));
	}

	if (!FogActor.IsValid()) return;
	APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!P) return;
	const FVector L = P->GetActorLocation();
	const float Woods = FMath::Clamp((JeebTerrain::WoodsDensity(L.X, L.Y) - 0.45f) / 0.4f, 0.f, 1.f);
	const float Grave = 1.f - FMath::Clamp(FVector2D::Distance(FVector2D(L.X, L.Y), FVector2D(JeebTerrain::GraveyardPos())) / 1100.f, 0.f, 1.f);
	const float Target = 0.010f + 0.007f * Woods + 0.016f * Grave;
	FogNow = FMath::FInterpTo(FogNow, Target, DeltaSeconds, 0.6f);
	FogActor->GetComponent()->SetFogDensity(FogNow);
}

void AKnightGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Always full screen (in a standalone or packaged game; the editor's own Play-in-Editor window keeps its own size)
	if (!GIsEditor)
	{
		if (UGameUserSettings* GS = UGameUserSettings::GetGameUserSettings())
		{
			GS->SetFullscreenMode(EWindowMode::Fullscreen);
			GS->ApplySettings(false);
		}
	}

	UWorld* World = GetWorld();

	// Cosy autumn evening: low golden sun, cool dusky shadows, soft haze
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		It->SetActorRotation(FRotator(-17.f, 215.f, 0.f)); // low in the sky
		Sun->SetLightColor(FLinearColor(1.0f, 0.62f, 0.34f));
		Sun->SetIntensity(3.5f);
		Sun->SpecularScale = 0.f; // no shiny sun glare on the grass
		Sun->MarkRenderStateDirty();
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		USkyLightComponent* Sky = It->GetLightComponent();
		Sky->SetLightColor(FLinearColor(0.66f, 0.66f, 0.86f));   // a little lighter and less blue, so shade is dim but not black
		Sky->SetIntensity(1.25f);
		Sky->RecaptureSky();
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		UExponentialHeightFogComponent* Fog = It->GetComponent();
		Fog->SetFogInscatteringColor(FLinearColor(0.42f, 0.30f, 0.34f));
		Fog->SetFogDensity(0.026f);   // thick enough that the far hills and grass melt into haze
		Fog->SetStartDistance(600.f);
		FogActor = *It;
		Fog->FogCutoffDistance = 16000.f; // the faraway moon is not hidden by haze
	}

	// The land (hills and grass)
		World->SpawnActor<AGrassField>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->SpawnActor<AMeadow>(FVector::ZeroVector, FRotator::ZeroRotator);   // the storybook-grass test plot

	// The cottage, with its door facing the starting point
	const FVector CottageSpot = JeebTerrain::CottagePos();
	World->SpawnActor<ACottage>(
		FVector(CottageSpot.X, CottageSpot.Y, JeebTerrain::HeightAt(CottageSpot.X, CottageSpot.Y)),
		FRotator(0.f, (-CottageSpot).Rotation().Yaw, 0.f));

	auto SpawnRock = [&](float X, float Y)
	{
		World->SpawnActor<AMiningRock>(
			FVector(X, Y, JeebTerrain::HeightAt(X, Y) - 8.f),
			FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
	};

	// A few copper rocks near the starting point...
	SpawnRock(650.f, -100.f);
	SpawnRock(900.f, -300.f);
	SpawnRock(1150.f, 100.f);

	// ...and plenty more scattered around the hills
	FRandomStream Rand(4242);
	for (int32 i = 0; i < 35; ++i)
	{
		const float Angle = Rand.FRandRange(0.f, 2.f * PI);
		const float Dist = FMath::Lerp(900.f, 8500.f, FMath::Sqrt(Rand.FRand()));
		const float X = FMath::Cos(Angle) * Dist;
		const float Y = FMath::Sin(Angle) * Dist;
		if (FVector2D::Distance(FVector2D(X, Y), FVector2D(CottageSpot.X, CottageSpot.Y)) < 800.f) continue;
		if (JeebTerrain::TreeBlocked(X, Y)) continue; // no rocks on roads, in the village or on hut plots
		if (JeebTerrain::PropBlocked(X, Y, 120.f)) continue; // nor against fences
		SpawnRock(X, Y);
	}

	// Dirt roads and the walled village with its huts
	World->SpawnActor<APaths>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->SpawnActor<AVillage>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->SpawnActor<APigPen>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->SpawnActor<ADayNight>(FVector::ZeroVector, FRotator::ZeroRotator);   // the sun's daily round (drives the lights below)
	World->SpawnActor<AMoon>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->SpawnActor<ANightSky>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->SpawnActor<ABatSwarm>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->SpawnActor<AFishingPond>(FVector::ZeroVector, FRotator::ZeroRotator);

	// The castle, with its moat, bridge and spire
	if (ACastle* Keep = World->SpawnActor<ACastle>(FVector::ZeroVector, FRotator::ZeroRotator)) Keep->Build();

	// A fort up in the middle redwood of every grove
	{
		const TArray<JeebTerrain::FFort>& Forts = JeebTerrain::Forts();
		
		for (int32 i = 0; i < Forts.Num(); ++i)
		{
			if (AFort* F = World->SpawnActor<AFort>(FVector::ZeroVector, FRotator::ZeroRotator)) F->Build(Forts[i]);
		}
	}

	// Your horse, waiting near the start (T calls it)
	{
		const FVector2D HP(650.f, 420.f);
		World->SpawnActor<AHorse>(FVector(HP.X, HP.Y, JeebTerrain::HeightAt(HP.X, HP.Y)), FRotator(0.f, 200.f, 0.f));
		// A cloud of purple poison smoke a few metres from the horse, to see how a hazard looks and feels
		World->SpawnActor<ASmokeCloud>(FVector(HP.X + 380.f, HP.Y, JeebTerrain::HeightAt(HP.X + 380.f, HP.Y)), FRotator::ZeroRotator);
	}

	// The woods (thousands of trees, a redwood grove) plus a few trees close to the start
	AForest* Forest = World->SpawnActor<AForest>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (Forest)
	{
		FRandomStream TreeRand(99);
		const FVector TreeSpots[] = { FVector(-500, -800, 0), FVector(400, 900, 0), FVector(1400, -600, 0) };
		for (const FVector& Spot : TreeSpots)
		{
			const float S = TreeRand.FRandRange(0.9f, 1.3f);
			Forest->AddTree(ETreeKind::Oak, FVector(Spot.X, Spot.Y, JeebTerrain::HeightAt(Spot.X, Spot.Y) - 20.f * S), S, TreeRand);
		}
	}

	// Pumpkins, hay, scarecrows, lamps, a graveyard, floating candles and falling leaves
	World->SpawnActor<AAutumnDecor>(FVector::ZeroVector, FRotator::ZeroRotator);

	// Make sure the knight starts standing on the meadow
	FVector2D Start(0.f, 0.f);

	// Test screenshot mode: -JeebShot -ShotName=x -ShotPhase=0.2 -ShotPos=X,Y -ShotYaw=deg -ShotPitch=deg
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("JeebShot")))
	{
		bShotMode = true;
		float Ph = -1.f, Yaw = 0.f, Pitch = -10.f;
		FParse::Value(Cmd, TEXT("ShotPhase="), Ph);
		FParse::Value(Cmd, TEXT("ShotYaw="), Yaw);
		FParse::Value(Cmd, TEXT("ShotPitch="), Pitch);
		FParse::Value(Cmd, TEXT("ShotName="), ShotName);
		FString PosS;
		if (FParse::Value(Cmd, TEXT("ShotPos="), PosS))
		{
			FString A, B;
			if (PosS.Split(TEXT(","), &A, &B)) Start = FVector2D(FCString::Atof(*A), FCString::Atof(*B));
		}
		ADayNight::FixedPhase = Ph;
		ShotRot = FRotator(Pitch, Yaw, 0.f);
	}
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Player->SetActorLocation(FVector(Start.X, Start.Y, JeebTerrain::HeightAt(Start.X, Start.Y) + 130.f));
	}

	// Game messages are drawn by the HUD; keep the engine's debug text off the screen
	if (GEngine) GEngine->bEnableOnScreenDebugMessages = false;
}
