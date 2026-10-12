#include "KnightGameState.h"
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
#include "Cat.h"
#include "Fireflies.h"
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
#include "Net/UnrealNetwork.h"
#include "ResourceNode.h"
#include "Store.h"
#include "Combat.h"


AKnightGameState::AKnightGameState()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AKnightGameState::BeginPlay()
{
	Super::BeginPlay();
	BuildWorld();
}

void AKnightGameState::BuildWorld()
{
	// Nothing on a dedicated server is ever seen: skip the purely visual sky and creatures there
	const bool bServerOnly = IsNetMode(NM_DedicatedServer);

	// Always full screen (in a standalone or packaged game; the editor's own Play-in-Editor window keeps its own size)
	if (!GIsEditor && !bServerOnly)
	{
		if (UGameUserSettings* GS = UGameUserSettings::GetGameUserSettings())
		{
			GS->SetFullscreenMode(EWindowMode::WindowedFullscreen);   // a borderless window over the whole screen
			GS->SetScreenResolution(GS->GetDesktopResolution());       // at the monitor's own native resolution
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
	if (!bServerOnly) World->SpawnActor<AMoon>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!bServerOnly) World->SpawnActor<ANightSky>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!bServerOnly) World->SpawnActor<ABatSwarm>(FVector::ZeroVector, FRotator::ZeroRotator);
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
		// (no horse for now: horses aren't shared online yet. One game for everyone: single player has none either.)
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
	if (!bServerOnly) World->SpawnActor<AFireflies>(FVector::ZeroVector, FRotator::ZeroRotator);   // swarms that drift in and out

	// Three cats that roam near the town: an orange tabby, a black cat and a grey-and-white one
	{
		const FVector2D VC = JeebTerrain::VillageCenter();
		struct FCatDef { FVector2D Off; FLinearColor Fur, Belly; };
		const FCatDef Cats[3] = {
			{ FVector2D(2400.f, 500.f),   FLinearColor(0.72f, 0.36f, 0.10f), FLinearColor(0.95f, 0.85f, 0.7f) },
			{ FVector2D(-600.f, 2500.f),  FLinearColor(0.04f, 0.04f, 0.05f), FLinearColor(0.08f, 0.08f, 0.09f) },
			{ FVector2D(-1200.f, -2500.f), FLinearColor(0.45f, 0.45f, 0.48f), FLinearColor(0.93f, 0.93f, 0.93f) },
		};
		for (int32 i = 0; i < 3; ++i)
		{
			const FVector2D H = VC + Cats[i].Off;
			if (ACat* C = World->SpawnActor<ACat>(FVector(H.X, H.Y, JeebTerrain::HeightAt(H.X, H.Y)), FRotator(0.f, 120.f * i, 0.f))) C->Setup(H, 900.f, Cats[i].Fur, Cats[i].Belly, 77 + i * 31);
		}
	}

	// Where players appear is chosen by the game mode (AKnightGameMode::SpawnDefaultPawnFor); this only moves the camera's
	// knight for a test screenshot
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
		if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			Player->SetActorLocation(FVector(Start.X, Start.Y, JeebTerrain::HeightAt(Start.X, Start.Y) + 130.f));
		}
	}

	// Game messages are drawn by the HUD; keep the engine's debug text off the screen
	if (GEngine) GEngine->bEnableOnScreenDebugMessages = false;

	// Online: the server's rock and tree counts may have arrived before the world was built
	if (!HasAuthority()) OnRep_Nodes();
}

void AKnightGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AKnightGameState, NodeCharges);
	DOREPLIFETIME(AKnightGameState, StoreHour);
	DOREPLIFETIME(AKnightGameState, StoreLeft);
}

void AKnightGameState::SetNodeCharges(const FVector& Loc, int32 Charges, int32 Max)
{
	if (!HasAuthority() || GetNetMode() == NM_Standalone) return;
	const FVector Q(FMath::RoundToFloat(Loc.X), FMath::RoundToFloat(Loc.Y), FMath::RoundToFloat(Loc.Z));
	const int32 Found = NodeCharges.IndexOfByPredicate([&](const FNodeCharge& N) { return FVector::DistSquared(N.Loc, Q) < 4.f; });
	if (Charges >= Max) { if (Found != INDEX_NONE) NodeCharges.RemoveAtSwap(Found); return; }
	if (Found != INDEX_NONE) NodeCharges[Found].Charges = Charges;
	else { FNodeCharge N; N.Loc = Q; N.Charges = Charges; NodeCharges.Add(N); }
}

void AKnightGameState::OnRep_Nodes()
{
	for (TActorIterator<AResourceNode> It(GetWorld()); It; ++It)
	{
		const FVector L = It->GetActorLocation();
		int32 C = -1;   // -1: not listed, so full
		for (const FNodeCharge& N : NodeCharges) if (FVector::DistSquared2D(N.Loc, L) < 9.f && FMath::Abs(N.Loc.Z - L.Z) < 3.f) { C = N.Charges; break; }
		It->ApplyCharges(C);
	}
}

void JeebCombat::SendToAll(const UWorld* World, const FVector& Loc, int32 Amount, int32 Type)
{
	if (AKnightGameState* GS = World ? World->GetGameState<AKnightGameState>() : nullptr) GS->MulticastNumber(Loc, Amount, static_cast<uint8>(Type));
}

void AKnightGameState::MulticastNumber_Implementation(FVector_NetQuantize Loc, int32 Amount, uint8 Type)
{
	if (!IsNetMode(NM_DedicatedServer)) JeebCombat::AddLocal(GetWorld(), Loc, Amount, Type);
}

void AKnightGameState::SyncStore()
{
	if (!HasAuthority()) return;
	const TArray<JeebStore::FWare>& W = JeebStore::Stock();
	StoreHour = JeebStore::HourNow();
	StoreLeft.SetNum(W.Num());
	for (int32 i = 0; i < W.Num(); ++i) StoreLeft[i] = W[i].Stock;
}

void AKnightGameState::ApplyStore(UWorld* World)
{
	const AKnightGameState* GS = World ? World->GetGameState<AKnightGameState>() : nullptr;
	if (!GS || GS->HasAuthority()) return;
	TArray<JeebStore::FWare>& W = JeebStore::Stock();
	if (GS->StoreHour != JeebStore::HourNow() || GS->StoreLeft.Num() != W.Num()) return;   // nobody has bought anything this hour
	for (int32 i = 0; i < W.Num(); ++i) W[i].Stock = GS->StoreLeft[i];
}

// Light haze everywhere, a little thicker among dense trees and over the graveyard
void AKnightGameState::Tick(float DeltaSeconds)
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
