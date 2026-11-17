#include "KnightGameState.h"
#include "Palette.h"
#include "GameFramework/HUD.h"
#include "WaterLook.h"
#include "GameFramework/GameUserSettings.h"
#include "KnightCharacter.h"
#include "GearWall.h"
#include "FillerLineup.h"
#include "TreeGroves.h"
#include "TownLabLineup.h"
#include "RHIStats.h"
#include "RenderTimer.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GrassField.h"
#include "WorldTerrain.h"
#include "Hills.h"
#include "OakHillTown.h"
#include "OakHillLife.h"
#include "OakHill.h"
#include "TownLab.h"
#include "TownLabTown.h"
#include "TownLabDirector.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Meadow.h"
#include "MiningRock.h"
#include "Inventory.h"
#include "ChoppableTree.h"
#include "Forest.h"
#include "AutumnDecor.h"
#include "Fill.h"
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
#include "OakHill.h"
#include "Cat.h"
#include "Fireflies.h"
#include "KnightHUD.h"
#include "Terrain.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "DynamicRHI.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
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
#include "Scarecrow.h"
#include "FoeKnight.h"
#include "OakForest.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"


AKnightGameState::AKnightGameState()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AKnightGameState::BeginPlay()
{
	Super::BeginPlay();
	JeebPalette::ApplyToWorld(GetWorld());   // the world palette's colours and knobs into this world's MPC_JeebPalette (Content/Data/Palette.ini)
	BuildWorld();
}

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarTestHorse(TEXT("jeeb.Test.Horse"), 1, TEXT("TEMPORARY test setting: 1 (default) puts a horse beside the start in single player at the next load, 0 leaves it out. Never online, never in Shipping."), ECVF_Default);
#endif

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

	JeebTownLab::SetActive(GetWorld()->GetMapName().Contains(TEXT("TownLab")));   // (the TownLab test map: the rebuilt Oak Hill beside the real one, TownLab.h; Field never)
	UWorld* World = GetWorld();
	JeebTerrain::ExtraLadders().Reset();   // (MoveLab adds its own below; none in the real world)
	JeebHills::Prepare();    // the forested hills round Oak Hill (Hills.h): worked out once, here on the game thread before anything asks for a height; logs JEEBHILLS
	JeebHills::LogStats();

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
		Fog->FogCutoffDistance = 400000.f; // the faraway sky (about 5 km out) is not hidden by haze; the whole world is (since 2026-10-30)
	}

	// The MoveLab map (a copy of Field) is a small test area for working on moves: only a flat floor with a 1 m grid and two
	// training dummies, so Play starts in seconds. The knight is the same knight as in the game: nothing to carry over.
	if (World->GetMapName().Contains(TEXT("MoveLab")))
	{
		// (the sun and its light, as in the game, but three times as fast so the light can be judged quickly: owner, 2026-11-06)
		if (ADayNight* LabSun = World->SpawnActor<ADayNight>(FVector::ZeroVector, FRotator::ZeroRotator)) LabSun->ClockSpeed = 3.f;
		BuildMoveLab();
		JeebGrassLineup(World);   // (test only, -GrassLineup: the Fab grass packs' types in rows, Meadow.cpp)
		if (GEngine) GEngine->bEnableOnScreenDebugMessages = false;
		return;
	}

	// The land (hills and grass)
		World->SpawnActor<AGrassField>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->SpawnActor<AMeadow>(FVector::ZeroVector, FRotator::ZeroRotator);   // the storybook-grass test plot
	World->SpawnActor<AWorldTerrain>(FVector::ZeroVector, FRotator::ZeroRotator);   // the whole world round it (WorldMap.h): mountains, rivers, coast, sea
	World->SpawnActor<AOakHillTown>(FVector::ZeroVector, FRotator::ZeroRotator);    // Oak Hill's plan checks (JEEBOAK), and the old town itself only with -OldOakHill or in TownLab (OakHill.h)
	if (JeebTownLab::RealTown())   // Oak Hill (2026-11-14): the rebuilt town at the real plaza (TownLab.h, TownLabTown.cpp)
		if (ATownLabTown* Town = World->SpawnActorDeferred<ATownLabTown>(ATownLabTown::StaticClass(), FTransform::Identity))
		{
			Town->bRealSite = true;
			Town->FinishSpawning(FTransform::Identity);
		}
	if (JeebOakHill::Enabled()) World->SpawnActor<AOakHillLife>(FVector::ZeroVector, FRotator::ZeroRotator);   // chimney smoke, glow at the low sun, shop lamps (visual only; clients)
	if (JeebTownLab::Active())
	{
		World->SpawnActor<ATownLabTown>(FVector::ZeroVector, FRotator::ZeroRotator);   // (TownLab: the rebuilt Oak Hill 300 m east)
		World->SpawnActor<ATownLab>(FVector::ZeroVector, FRotator::ZeroRotator);       // (its switches and measuring tour)
	}
#if !UE_BUILD_SHIPPING
	else if (FCString::Strifind(FCommandLine::Get(), TEXT("LabTour")))
		World->SpawnActor<ATownLab>(FVector::ZeroVector, FRotator::ZeroRotator);   // (-LabTour in Field: the measuring tour of the real town, open land and Blackrock)
#endif

	// The cottage, with its door facing the starting point
	const FVector CottageSpot = JeebTerrain::CottagePos();
	World->SpawnActor<ACottage>(
		FVector(CottageSpot.X, CottageSpot.Y, JeebTerrain::HeightAt(CottageSpot.X, CottageSpot.Y)),
		FRotator(0.f, (-CottageSpot).Rotation().Yaw, 0.f));

	// Every rock holds one kind of ore for good (2026-10-30, the tier redo). The mix is fixed and shuffled from a fixed seed,
	// so every machine builds the same world: lots of tin and iron, fewer silver and gold, and only a few gems.
	TArray<EItemType> OreMix;
	{
		auto Put = [&](EItemType T, int32 N) { for (int32 i = 0; i < N; ++i) OreMix.Add(T); };
		Put(EItemType::TinOre, 13); Put(EItemType::IronOre, 11);
		Put(EItemType::SilverOre, 5); Put(EItemType::GoldOre, 3); Put(EItemType::Emerald, 2); Put(EItemType::Diamond, 1);
		FRandomStream Shuffle(777);
		for (int32 i = OreMix.Num() - 1; i > 0; --i) OreMix.Swap(i, Shuffle.RandRange(0, i));
		// the rare ones first: a few scatter spots get skipped (roads, fences), and those must never be the only gem rocks
		auto Rare = [](EItemType T) { return T == EItemType::SilverOre || T == EItemType::GoldOre || JeebItems::IsGem(T); };
		OreMix.StableSort([&](EItemType A, EItemType B) { return Rare(A) && !Rare(B); });
	}
	int32 NextOre = 0;
	auto SpawnRock = [&](float X, float Y, EItemType Ore)
	{
		// (its turn from its own spot, so every machine builds the same rock: the global random number differed per machine)
		const FTransform Xf(FRotator(0.f, FRandomStream(FMath::RoundToInt(X) * 7919 + FMath::RoundToInt(Y) * 104729).FRandRange(0.f, 360.f), 0.f), FVector(X, Y, JeebTerrain::HeightAt(X, Y) - 8.f));
		if (AMiningRock* Rock = World->SpawnActorDeferred<AMiningRock>(AMiningRock::StaticClass(), Xf))
		{
			Rock->SetOre(Ore);
			Rock->FinishSpawning(Xf);
		}
	};

	// A few tin rocks near the starting point (what the starting kit's level needs)...
	SpawnRock(650.f, -100.f, EItemType::TinOre);
	SpawnRock(900.f, -300.f, EItemType::TinOre);
	SpawnRock(1150.f, 100.f, EItemType::TinOre);

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
		SpawnRock(X, Y, OreMix[NextOre++ % OreMix.Num()]);
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

	// Your horse (TEMPORARY test setting, owner 2026-11-16): single player and editor Play only, never on a server or an online client (horses aren't shared online),
	// never in Shipping. It waits on open ground beside the start (Oak Hill's plaza, or the old spot elsewhere); T calls it, E beside it mounts. `jeeb.Test.Horse 0` leaves it out.
#if !UE_BUILD_SHIPPING
	if (GetNetMode() == NM_Standalone && CVarTestHorse.GetValueOnGameThread() != 0)
	{
		const bool bOakStart = JeebOakHill::Enabled() && !GetWorld()->GetMapName().Contains(TEXT("MoveLab"));
		const FVector2D HorseAt = bOakStart ? FVector2D(JeebOakHill::SpawnX + 450.f, JeebOakHill::SpawnY) : FVector2D(650.f, 420.f);
		AHorse* H = World->SpawnActor<AHorse>(FVector(HorseAt.X, HorseAt.Y, JeebTerrain::HeightAt(HorseAt.X, HorseAt.Y)), FRotator(0.f, bOakStart ? JeebOakHill::SpawnYaw + 180.f : 200.f, 0.f));
		UE_LOG(LogTemp, Display, TEXT("JEEBHORSE spawned at %.0f,%.0f (%s)"), HorseAt.X, HorseAt.Y, H ? TEXT("ok") : TEXT("failed"));
	}
#endif
	{
		const FVector2D HP(650.f, 420.f);
		// (the horse itself is spawned above; the smoke cloud stays where the old spot was)
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
	JeebGroves::Build(World, Forest);   // GROVES (TEMPORARY test setting, 2026-11-15): sample tree groves east of Oak Hill; non-Shipping, `jeeb.Test.Groves 0` leaves them out (TreeGroves.h)
	World->SpawnActor<AOakForest>(FVector::ZeroVector, FRotator::ZeroRotator);   // Oak Hill's trees and the forest 500 m round it, with its roads, logs, stumps and boulders (OakForest.h; after AForest, which resets the canopy list; on every machine: the server builds the trunks' solids)

	// Pumpkins, hay, scarecrows, lamps, a graveyard, floating candles and falling leaves
	World->SpawnActor<AAutumnDecor>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->SpawnActor<AWorldFill>(FVector::ZeroVector, FRotator::ZeroRotator);   // the filler props (Fill.h; after everything they keep clear of, on every machine: the server builds their solids)
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

	SetupShotMode();

	// Game messages are drawn by the HUD; keep the engine's debug text off the screen
	if (GEngine) GEngine->bEnableOnScreenDebugMessages = false;

	// Online: the server's rock and tree counts may have arrived before the world was built
	if (!HasAuthority()) { OnRep_Nodes(); OnRep_Doors(); }
}

// Test-only foliage mask for frame-cost runs (-ShotEyes' 7th field; foliage 2026-11-14): 1 hides the near trees (trunks, branches, leaves,
// needles), 2 the far stand-ins, 4 takes every tree's shadow away, 8 hides all the grass (meadow and the original square). 0 restores.
static void JeebFoliageMask(UWorld* W, int32 Mask)
{
	static TMap<TWeakObjectPtr<UPrimitiveComponent>, TPair<bool, bool>> Orig;   // (visible, casts shadow) as built
	for (TActorIterator<AForest> It(W); It; ++It)
	{
		TArray<UInstancedStaticMeshComponent*> Comps; It->GetComponents(Comps);
		for (UInstancedStaticMeshComponent* C : Comps)
		{
			if (!Orig.Contains(C)) Orig.Add(C, TPair<bool, bool>(C->IsVisible(), bool(C->CastShadow)));
			const TPair<bool, bool> O = Orig[C];
			const bool bFar = C->GetName().StartsWith(TEXT("Far"));
			C->SetVisibility(O.Key && !(Mask & (bFar ? 2 : 1)));
			C->SetCastShadow(O.Value && !(Mask & 4));
		}
	}
	for (TActorIterator<AMeadow> It(W); It; ++It) It->SetActorHiddenInGame((Mask & 8) != 0);
	for (TActorIterator<AGrassField> It(W); It; ++It) It->SetActorHiddenInGame((Mask & 8) != 0);
	// (32, read by the -ShotEyes code: the sun creeps on every frame, as in play)
	// 512: no cached sun shadows at all (r.Shadow.CSMCaching 0), to see what the cache saves
	if (IConsoleVariable* Cache = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.CSMCaching")))
		Cache->Set((Mask & 512) ? 0 : 1, ECVF_SetByConsole);
}

// (JEEBCENSUS, test only: the shown primitives within 300 m of a spot, by the class of what owns them, with how many cast shadows and
// how many instances: where a spot's draws come from)
static void JeebCensus(UWorld* W, const FVector& At)
{
	struct FRow { int32 N = 0, Shadow = 0; double Instances = 0; };
	TMap<FString, FRow> Rows;
	int32 Total = 0;
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* C = *It;
		if (C->GetWorld() != W || !C->IsRegistered() || !C->IsVisible() || !C->SceneProxy) continue;
		const AActor* O = C->GetOwner();
		if (O && O->IsHidden()) continue;
		if (FVector::Dist(C->Bounds.Origin, At) - C->Bounds.SphereRadius > 30000.f) continue;
		FRow& R = Rows.FindOrAdd(FString::Printf(TEXT("%s/%s"), O ? *O->GetClass()->GetName() : TEXT("-"), *C->GetClass()->GetName()));
		++R.N; ++Total;
		if (C->CastShadow) ++R.Shadow;
		if (const UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(C)) R.Instances += I->GetInstanceCount();
	}
	Rows.ValueSort([](const FRow& A, const FRow& B) { return A.N > B.N; });
	FString Line;
	int32 k = 0;
	for (const TPair<FString, FRow>& P : Rows) { if (k++ >= 16) break; Line += FString::Printf(TEXT(" | %s %d (shadow %d, inst %.0f)"), *P.Key, P.Value.N, P.Value.Shadow, P.Value.Instances); }
	UE_LOG(LogTemp, Display, TEXT("JEEBCENSUS %d primitives within 300 m%s"), Total, *Line);
}

// Test screenshot mode: -JeebShot -ShotName=x -ShotPhase=0.2 -ShotPos=X,Y -ShotYaw=deg -ShotPitch=deg (-ShotMove=jab ...)
void AKnightGameState::SetupShotMode()
{
	// Where players appear is chosen by the game mode (AKnightGameMode::SpawnDefaultPawnFor); this only moves the camera's
	// knight for a test screenshot
	FVector2D Start(0.f, 0.f);
	const TCHAR* Cmd = FCommandLine::Get();
	if (!FParse::Param(Cmd, TEXT("JeebShot"))) return;
	bShotMode = true;
	float Ph = -1.f, Yaw = 0.f, Pitch = -10.f;
	FParse::Value(Cmd, TEXT("ShotPhase="), Ph);
	FParse::Value(Cmd, TEXT("ShotYaw="), Yaw);
	FParse::Value(Cmd, TEXT("ShotPitch="), Pitch);
	FParse::Value(Cmd, TEXT("ShotName="), ShotName);
	FParse::Value(Cmd, TEXT("ShotMove="), ShotMove);
	FParse::Value(Cmd, TEXT("ShotFacing="), ShotFacing);
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

// The MoveLab test area: a flat 40 x 40 m floor at the height of the game's starting point, with a darker line every metre
// and a bright one every 5 m (to judge steps and reach), a still sparring knight 3 m ahead and straw dummies 3 m to each side
void AKnightGameState::BuildMoveLab()
{
	UWorld* World = GetWorld();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	const float Z = JeebTerrain::HeightAt(0.f, 0.f);
	AActor* Floor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector(0.f, 0.f, Z), FRotator::ZeroRotator);
	if (!Floor || !Cube) return;
	USceneComponent* Root = NewObject<USceneComponent>(Floor);
	Floor->SetRootComponent(Root);
	Root->RegisterComponent();
	auto Box = [&](const FVector& Centre, const FVector& Size, const FLinearColor& Col, bool bCollide)
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(Floor);
		M->SetStaticMesh(Cube);
		M->SetupAttachment(Root);
		M->SetRelativeLocation(Centre);
		M->SetRelativeScale3D(Size / 100.f);   // (the basic cube is 1 m)
		M->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		M->SetCollisionResponseToAllChannels(ECR_Block);
		M->SetCastShadow(bCollide);
		M->RegisterComponent();
		if (Base)
			if (UMaterialInstanceDynamic* Mi = M->CreateDynamicMaterialInstance(0, Base)) Mi->SetVectorParameterValue(TEXT("Color"), Col);
	};
	Box(FVector(0.f, 0.f, -50.f), FVector(4000.f, 4000.f, 100.f), FLinearColor(0.32f, 0.34f, 0.30f), true);   // the floor (top at Z)
	for (int32 i = -20; i <= 20; ++i)
	{
		const bool bFive = (i % 5) == 0;
		const FLinearColor C = bFive ? FLinearColor(0.75f, 0.72f, 0.55f) : FLinearColor(0.18f, 0.19f, 0.17f);
		const float W = bFive ? 4.f : 2.f;
		Box(FVector(i * 100.f, 0.f, 0.3f), FVector(W, 4000.f, 0.6f), C, false);
		Box(FVector(0.f, i * 100.f, 0.3f), FVector(4000.f, W, 0.6f), C, false);
	}
	// A still sparring knight straight ahead of where you start (the pen knight's body, standing still: for hit boxes and hurt
	// boxes, HitBoxes.h), a second one 3.5 m to the right of him (as you look from the start) holding his shield up (the shield's
	// wedge, and its guard), both facing where you start, and a straw dummy to either side of the start
	if (World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		float Ahead = 300.f;
		FParse::Value(FCommandLine::Get(), TEXT("LabKnight="), Ahead);   // (test pictures: -LabKnight=150 stands him within reach)
		const bool bGuardFirst = FParse::Param(FCommandLine::Get(), TEXT("LabGuardFirst"));   // (test pictures: the guarding one straight ahead)
		float KnightYaw = 180.f;
		FParse::Value(FCommandLine::Get(), TEXT("LabKnightYaw="), KnightYaw);   // (test pictures: 0 turns their backs to you)
		for (int32 g = 0; g < 2; ++g)
		{
			const float Y = ((g == 1) != bGuardFirst) ? 350.f : 0.f;
			const FTransform Xf(FRotator(0.f, KnightYaw, 0.f), FVector(Ahead, Y, Z + 97.f));
			if (AFoeKnight* K = World->SpawnActorDeferred<AFoeKnight>(AFoeKnight::StaticClass(), Xf))
			{
				K->bStill = true;
				K->bStillGuard = (g == 1);
				K->StillZ = Z + 97.f;   // (the flat test floor: the open world's ground under him is higher or lower)
				K->Setup(FVector2D(Ahead, Y), FVector2D(100.f, 100.f));
				K->FinishSpawning(Xf);
			}
		}
	}
	for (const FVector2D& P : { FVector2D(0.f, -300.f), FVector2D(0.f, 300.f) })
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("LabNoDummies"))) break;   // (test pictures: a clear run to either side)
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FTransform Xf(FRotator(0.f, (FVector(-P.X, -P.Y, 0.f)).Rotation().Yaw, 0.f), FVector(P.X, P.Y, Z));
		if (AScarecrow* D = World->SpawnActorDeferred<AScarecrow>(AScarecrow::StaticClass(), Xf))
		{
			D->bDummy = true;
			D->FinishSpawning(Xf);
		}
	}
	// A tree and a rock to work on, ahead of the start to either side (every machine builds its own, as in the real world)
	{
		FActorSpawnParameters SP;
		SP.bDeferConstruction = true;
		if (AForest* LabWoods = World->SpawnActor<AForest>(AForest::StaticClass(), FTransform::Identity, SP))
		{
			LabWoods->bNoWoods = true;
			LabWoods->FinishSpawning(FTransform::Identity);
			FRandomStream TreeRand(99);
			LabWoods->AddTree(ETreeKind::Oak, FVector(150.f, -800.f, Z - 20.f), 1.f, TreeRand);
		}
		const FTransform RockAt(FRotator(0.f, 40.f, 0.f), FVector(150.f, 800.f, Z - 8.f));
		if (AMiningRock* Rock = World->SpawnActorDeferred<AMiningRock>(AMiningRock::StaticClass(), RockAt))
		{
			Rock->SetOre(EItemType::TinOre);
			Rock->FinishSpawning(RockAt);
		}
	}
	// A ladder up a tower, and a pond to fish in (owner, 2026-11-06, for testing climbing and fishing). Both sit east of the gear
	// wall's far board, out past its ends (north +Y for the tower, south -Y for the pond), well clear of the X = 0 line the films run along.
	// They use the real mechanics: the climb code looks the ladder up in JeebTerrain::ExtraLadders (exact heights, since this floor is
	// not the open world's ground) and the pond is made of real AFishingSpot nodes, like the village pond.
	{
		const float TX = 1000.f, TY = 1650.f, TowerH = 360.f;   // the tower: 3 x 3 m, its deck TowerH up
		Box(FVector(TX, TY, TowerH * 0.5f), FVector(300.f, 300.f, TowerH), FLinearColor(0.45f, 0.42f, 0.38f), true);
		Box(FVector(TX, TY, TowerH + 4.f), FVector(316.f, 316.f, 8.f), FLinearColor(0.36f, 0.22f, 0.10f), true);   // a wooden deck cap
		const float LX = TX - 150.f - 5.f;   // the ladder stands against the tower's -X face (facing the start side), Out = -X
		for (int32 sd = -1; sd <= 1; sd += 2) Box(FVector(LX, TY + sd * 28.f, (TowerH + 30.f) * 0.5f), FVector(6.f, 6.f, TowerH + 30.f), FLinearColor(0.30f, 0.18f, 0.08f), false);
		for (float Rz = 30.f; Rz < TowerH + 10.f; Rz += 38.f) Box(FVector(LX, TY, Rz), FVector(6.f, 56.f, 5.f), FLinearColor(0.30f, 0.18f, 0.08f), false);
		JeebTerrain::FLadder L;
		L.Pos = FVector2D(LX, TY);
		L.Out = FVector2D(-1.f, 0.f);
		L.Top = Z + TowerH + 8.f;   // (the cap's top)
		L.Bottom = Z;
		L.bAbsolute = true;
		JeebTerrain::ExtraLadders().Add(L);

		// The pond: a stone rim and a disc of water on the floor (nothing to bump into), with fishing spots on it
		UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		const float PX = 1000.f, PY = -1600.f, PondR = 330.f;
		auto Disc = [&](float Radius, float Thick, float Zc, const FLinearColor& Col) -> UStaticMeshComponent*
		{
			if (!Cyl) return nullptr;
			UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(Floor);
			M->SetStaticMesh(Cyl);
			M->SetupAttachment(Root);
			M->SetRelativeLocation(FVector(PX, PY, Zc));
			M->SetRelativeScale3D(FVector(2.f * Radius / 100.f, 2.f * Radius / 100.f, Thick / 100.f));
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			M->SetCastShadow(false);
			M->RegisterComponent();
			if (Base)
				if (UMaterialInstanceDynamic* Mi = M->CreateDynamicMaterialInstance(0, Base)) Mi->SetVectorParameterValue(TEXT("Color"), Col);
			return M;
		};
		Disc(PondR + 30.f, 2.f, 1.f, FLinearColor(0.40f, 0.38f, 0.34f));
		// (2026-11-13) the water is see-through now (WaterLook.h) and only a few centimetres deep here, so a dark pond bed under it
		Disc(PondR - 4.f, 1.f, 2.4f, FLinearColor(0.025f, 0.06f, 0.065f));
		if (UStaticMeshComponent* W = Disc(PondR, 3.f, 3.f, FLinearColor(0.06f, 0.24f, 0.36f))) JeebWaterLook::Apply(W, 0, FLinearColor(0.06f, 0.24f, 0.36f));
		FActorSpawnParameters FP;
		FP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 k = 0; k < 4; ++k)
		{
			const float A = FMath::DegreesToRadians(45.f + 90.f * k);
			World->SpawnActor<AFishingSpot>(FVector(PX + 200.f * FMath::Cos(A), PY + 200.f * FMath::Sin(A), Z + 5.f), FRotator::ZeroRotator, FP);
		}
		World->SpawnActor<AFishingSpot>(FVector(PX, PY, Z + 5.f), FRotator::ZeroRotator, FP);
	}
	// Climbing and the mantle (2026-11-12, KnightMovement.h): a rock to climb, east of the start between the pond and the knights, and two fences
	// and a stone block to mantle, south of the tower. The rock: a flat top 2.5 x 2.5 m, 5 m up; its west face (toward the start) sheer,
	// the south one 70 degrees, the north one 55, the east one 40 (a slope he can walk back down). Tagged JeebClimb; the fences and block JeebMantle.
	{
		const float RH = 500.f;
		const FVector T0(1150.f, -900.f, RH), T1(1400.f, -900.f, RH), T2(1400.f, -650.f, RH), T3(1150.f, -650.f, RH);
		const FVector B0(1150.f, -1082.f, 0.f), B1(1996.f, -1082.f, 0.f), B2(1996.f, -300.f, 0.f), B3(1150.f, -300.f, 0.f);
		const FVector Mid(1450.f, -700.f, 150.f);
		UProceduralMeshComponent* Rock = NewObject<UProceduralMeshComponent>(Floor);
		Rock->SetupAttachment(Root);
		Rock->bUseComplexAsSimpleCollision = true;
		Rock->RegisterComponent();
		const TArray<TArray<FVector>> Faces = { { T0, T1, T2, T3 }, { B0, T0, T3, B3 }, { B0, B1, T1, T0 }, { B1, B2, T2, T1 }, { B2, B3, T3, T2 } };
		for (int32 f = 0; f < Faces.Num(); ++f)
		{
			const TArray<FVector>& Q = Faces[f];
			const FVector C = (Q[0] + Q[1] + Q[2] + Q[3]) * 0.25f;
			FVector Nf = FVector::CrossProduct(Q[1] - Q[0], Q[2] - Q[0]).GetSafeNormal();
			if (FVector::DotProduct(Nf, C - Mid) < 0.f) Nf = -Nf;   // (outward)
			TArray<int32> Tri = { 0, 1, 2, 0, 2, 3 };
			// (front faces as Unreal counts them: the cross product points into the rock, as in WorldTerrain)
			if (FVector::DotProduct(FVector::CrossProduct(Q[1] - Q[0], Q[2] - Q[0]), Nf) > 0.f) { Swap(Tri[1], Tri[2]); Swap(Tri[4], Tri[5]); }
			TArray<FVector> Nm = { Nf, Nf, Nf, Nf };
			TArray<FVector2D> UV = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
			Rock->CreateMeshSection_LinearColor(f, Q, Tri, Nm, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), true);
			if (Base)
				if (UMaterialInstanceDynamic* Mi = Rock->CreateDynamicMaterialInstance(f, Base))
					Mi->SetVectorParameterValue(TEXT("Color"), f == 0 ? FLinearColor(0.30f, 0.40f, 0.22f) : FLinearColor(0.44f, 0.41f, 0.37f));
		}
		Rock->SetCollisionProfileName(TEXT("BlockAll"));
		Rock->ComponentTags.Add(FName(TEXT("JeebClimb")));

		// The fences: posts and two rails, with one thin solid board standing in for them (the mantle reads it), 1 m and 1.35 m tall
		auto Solid = [&](const FVector& Centre, const FVector& Size, bool bVisible, const FLinearColor& Col) -> UStaticMeshComponent*
		{
			UStaticMeshComponent* Mc = NewObject<UStaticMeshComponent>(Floor);
			Mc->SetStaticMesh(Cube);
			Mc->SetupAttachment(Root);
			Mc->SetRelativeLocation(Centre);
			Mc->SetRelativeScale3D(Size / 100.f);
			Mc->SetCollisionEnabled(bVisible ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
			Mc->SetCollisionResponseToAllChannels(ECR_Block);
			Mc->SetCastShadow(bVisible);
			Mc->SetVisibility(bVisible);
			Mc->RegisterComponent();
			if (bVisible && Base)
				if (UMaterialInstanceDynamic* Mi = Mc->CreateDynamicMaterialInstance(0, Base)) Mi->SetVectorParameterValue(TEXT("Color"), Col);
			return Mc;
		};
		const FLinearColor Wood(0.36f, 0.22f, 0.11f);
		for (const FVector& F : { FVector(300.f, 600.f, 100.f), FVector(620.f, 900.f, 135.f) })   // (from Y, to Y, height)
		{
			const float FX = 1000.f, Len = F.Y - F.X, H = F.Z;
			for (float Py = F.X; Py <= F.Y + 1.f; Py += Len / FMath::Max(1.f, FMath::RoundToFloat(Len / 150.f)))
				Solid(FVector(FX, Py, H * 0.5f), FVector(10.f, 10.f, H), true, Wood);
			for (const float Rz : { 0.42f, 0.88f })
				Solid(FVector(FX, (F.X + F.Y) * 0.5f, H * Rz), FVector(5.f, Len, 11.f), true, Wood);
			UStaticMeshComponent* Board = Solid(FVector(FX, (F.X + F.Y) * 0.5f, H * 0.5f), FVector(8.f, Len, H), false, Wood);
			Board->ComponentTags.Add(FName(TEXT("JeebMantle")));
		}
		// A stone block 1.2 m tall to mantle onto
		UStaticMeshComponent* Block = Solid(FVector(1000.f, 1150.f, 60.f), FVector(200.f, 200.f, 120.f), true, FLinearColor(0.50f, 0.48f, 0.44f));
		Block->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Block->ComponentTags.Add(FName(TEXT("JeebMantle")));
	}
	// Oak Hill's test house in three versions, side by side on a patch of grass past the floor's +Y edge (to the right of the start, clear of the gear wall's
	// boards), doors toward the floor (owner, 2026-11-06: to compare them in the game; Tools\BuildBuildings.ps1). The models are 12 m long and drawn 1.2 times
	// that (14.4 m), so the door clears the knight (owner: at 12 m it looked short for him); a box stands in for the walls and a sign says which is which.
	{
		const float HouseScale = 1.2f;
		Box(FVector(0.f, 2900.f, -50.f), FVector(5000.f, 1800.f, 100.f), FLinearColor(0.20f, 0.30f, 0.14f), true);
		struct FHouse { const TCHAR* Name; const TCHAR* Sign; float X; };
		const FHouse Houses[] = {
			{ TEXT("OakHillHouse_Standard"), TEXT("STANDARD: 14,000 triangles"), -1700.f },
			{ TEXT("OakHillHouse_300k"), TEXT("REDUCED: 300,000, Nanite"), 0.f },
			{ TEXT("OakHillHouse_Detailed"), TEXT("DETAILED: 2.4 million, Nanite"), 1700.f },
		};
		for (const FHouse& H : Houses)
		{
			UStaticMesh* HouseMesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Buildings/SM_%s.SM_%s"), H.Name, H.Name));
			if (!HouseMesh) continue;
			UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(Floor);
			M->SetStaticMesh(HouseMesh);
			M->SetupAttachment(Root);
			M->SetRelativeLocation(FVector(H.X, 2700.f, 0.f));
			M->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));   // (the model's door side, its +Y, toward the floor)
			M->SetRelativeScale3D(FVector(HouseScale));          // (the wall box below follows)
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			M->RegisterComponent();
			UBoxComponent* Walls = NewObject<UBoxComponent>(Floor);
			Walls->SetupAttachment(M);
			Walls->SetRelativeLocation(FVector(-17.f, -53.f, 200.f));
			Walls->SetBoxExtent(FVector(580.f, 346.f, 200.f));
			Walls->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Walls->SetCollisionResponseToAllChannels(ECR_Block);
			Walls->RegisterComponent();
			UTextRenderComponent* Sign = NewObject<UTextRenderComponent>(Floor);
			Sign->SetupAttachment(Root);
			Sign->SetRelativeLocation(FVector(H.X, 2130.f, 1100.f));
			Sign->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
			Sign->SetHorizontalAlignment(EHTA_Center);
			Sign->SetWorldSize(70.f);
			Sign->SetTextRenderColor(FColor::White);
			Sign->SetText(FText::FromString(H.Sign));
			Sign->SetCastShadow(false);
			Sign->RegisterComponent();
		}
	}
	// Oak Hill's filler props in labelled rows on a grass yard past the floor's -Y edge, for the owner to look at (2026-11-13; FillerLineup.cpp, /Game/Filler)
	JeebFiller::BuildYard(World, Floor, Root, Z);
	// TownLab's two rebuilt buildings beside the originals (2026-11-14, test switch -TownLabBuildings; TownLabLineup.h)
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("TownLabBuildings"))) JeebTownLab::BuildLineup(World, Floor, Root, Z);
#endif
	// Imported meshes in a row past the floor's +X edge, for test pictures (-LabMeshes=/Game/a+/Game/b+...; static or skeletal; foliage 2026-11-15)
	{
		FString List;
		TArray<FString> Paths;
		if (FParse::Value(FCommandLine::Get(), TEXT("LabMeshes="), List, false) && List.ParseIntoArray(Paths, TEXT("+")) > 0)
		{
			float Gap = 1400.f, AtX = 3200.f;
			FParse::Value(FCommandLine::Get(), TEXT("LabMeshGap="), Gap);
			FParse::Value(FCommandLine::Get(), TEXT("LabMeshX="), AtX);
			for (int32 i = 0; i < Paths.Num(); ++i)
			{
				const FVector At(AtX, (i - (Paths.Num() - 1) * 0.5f) * Gap, -1.f);
				UObject* Asset = LoadObject<UObject>(nullptr, *Paths[i]);
				if (USkeletalMesh* Sk = Cast<USkeletalMesh>(Asset))
				{
					USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(Floor);
					C->SetSkeletalMeshAsset(Sk);
					C->SetupAttachment(Root);
					C->SetRelativeLocation(At);
					C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					C->RegisterComponent();
				}
				else if (UStaticMesh* St = Cast<UStaticMesh>(Asset))
				{
					UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Floor);
					C->SetStaticMesh(St);
					C->SetupAttachment(Root);
					C->SetRelativeLocation(At);
					C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					C->RegisterComponent();
				}
				UE_LOG(LogTemp, Display, TEXT("JEEBLABMESH %s %s at %s"), *Paths[i], Asset ? *Asset->GetClass()->GetName() : TEXT("MISSING"), *At.ToString());
			}
		}
	}
	// The gear wall behind the start (every machine builds its own; pegs go by number): every armour piece, shield and weapon, potion, ring, amulet and gatherable
	World->SpawnActor<AGearWall>(AGearWall::StaticClass(), FVector(-700.f, 0.f, Z), FRotator::ZeroRotator);
	SetupShotMode();
}

void AKnightGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AKnightGameState, NodeCharges);
	DOREPLIFETIME(AKnightGameState, Doors);
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

void AKnightGameState::SyncDoor(ACottage* Hut)
{
	if (!Hut || !HasAuthority() || GetNetMode() == NM_Standalone) return;
	const FVector L = Hut->GetActorLocation();
	FDoorState* D = Doors.FindByPredicate([&](const FDoorState& S) { return FVector::DistSquared2D(S.Loc, L) < 9.f; });
	if (!D) { D = &Doors.AddDefaulted_GetRef(); D->Loc = L; }
	D->bOpen = Hut->IsDoorOpen();
	D->bLocked = Hut->IsLocked();
}

void AKnightGameState::OnRep_Doors()
{
	for (const FDoorState& S : Doors)
		for (TActorIterator<ACottage> It(GetWorld()); It; ++It)
			if (FVector::DistSquared2D(It->GetActorLocation(), S.Loc) < 9.f) { It->SetDoorState(S.bOpen, S.bLocked); break; }
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

// (test pictures, 2026-11-15: -ShotEyesSet=<eye>:<cvar>=<value>;...: a console variable set as that eye of -ShotEyes starts, to try placements
// and looks in one run; <eye> '*' is set once at the start, in a film too. wear=<tier> puts on a full suit of that tier. Log JEEBEYESET.)
static void ShotEyesSet(APlayerController* PC, int32 EyeIdx)
{
	static TArray<FString> Sets;
	static const bool bSets = []() { FString V; if (!FParse::Value(FCommandLine::Get(), TEXT("ShotEyesSet="), V, false)) return false; V.ParseIntoArray(Sets, TEXT(";")); return true; }();
	(void)bSets;
	for (const FString& Set : Sets)
	{
		FString Eye, Rest, Name, Val;
		if (!Set.Split(TEXT(":"), &Eye, &Rest) || !Rest.Split(TEXT("="), &Name, &Val)) continue;
		if (EyeIdx < 0 ? Eye != TEXT("*") : (Eye == TEXT("*") || FCString::Atoi(*Eye) != EyeIdx)) continue;
		if (Name == TEXT("wear"))
		{
			AKnightCharacter* K = Cast<AKnightCharacter>(PC ? PC->GetPawn() : nullptr);
			if (USkillComponent* S = K ? K->GetSkills() : nullptr)
			{
				for (int32 Kd = JeebItems::K_Helmet; Kd <= JeebItems::K_Boots; ++Kd) S->Worn[Kd] = JeebItems::ArmourId(Kd, 0, FMath::Clamp(FCString::Atoi(*Val), 0, JeebItems::NumTiers - 1));
				S->OnGearChanged.Broadcast();
				UE_LOG(LogTemp, Display, TEXT("JEEBEYESET eye %d wear tier %s"), EyeIdx, *Val);
			}
			continue;
		}
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(*Name)) { V->Set(*Val, ECVF_SetByConsole); UE_LOG(LogTemp, Display, TEXT("JEEBEYESET eye %d %s = %s"), EyeIdx, *Name, *Val); }
		else UE_LOG(LogTemp, Warning, TEXT("JEEBEYESET no console variable %s"), *Name);
	}
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
		{ static bool bStarSets = false; if (!bStarSets && ShotClock > 1.f) { bStarSets = true; ShotEyesSet(PC, -1); } }   // (-ShotEyesSet's '*' entries)
		// (test pictures, where -ShotPos doesn't move him: -ShotAt=X,Y stands him there once the world has settled, on the ground, or on the floor's height in MoveLab)
		static bool bShotAtDone = false;
		FString ShotAt, ShotAtX, ShotAtY;
		if (!bShotAtDone && ShotClock > 2.f && FParse::Value(FCommandLine::Get(), TEXT("ShotAt="), ShotAt, false) && ShotAt.Split(TEXT(","), &ShotAtX, &ShotAtY))
		{
			bShotAtDone = true;
			const float AtX = FCString::Atof(*ShotAtX), AtY = FCString::Atof(*ShotAtY);
			const bool bLab = GetWorld()->GetMapName().Contains(TEXT("MoveLab"));
			if (APawn* P = PC ? PC->GetPawn() : nullptr) P->SetActorLocation(FVector(AtX, AtY, (bLab ? JeebTerrain::HeightAt(0.f, 0.f) : JeebTerrain::HeightAt(AtX, AtY)) + 100.f));
		}
		// (-ShotMount: at 3.5 s the test horse is brought beside him and he mounts it, for riding pictures; log JEEBHORSE)
		static bool bShotMountDone = false;
		if (!bShotMountDone && ShotClock > 3.5f && FParse::Param(FCommandLine::Get(), TEXT("ShotMount")))
		{
			bShotMountDone = true;
			AKnightCharacter* Me = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
			AHorse* Mount = nullptr;
			for (TActorIterator<AHorse> It(GetWorld()); It; ++It) { Mount = *It; break; }
			if (Me && Mount)
			{
				Mount->SetActorLocation(Me->GetActorLocation() + FVector(0.f, 0.f, -96.f));
				Me->MountHorse(Mount);
			}
			UE_LOG(LogTemp, Display, TEXT("JEEBHORSE ShotMount: knight %d horse %d mounted %d"), Me ? 1 : 0, Mount ? 1 : 0, Me && Me->IsMounted() ? 1 : 0);
		}
		// (-ShotMount -ShotDismount=6.6: a whole ride, logged as JEEBHORSE lines, 2026-11-16. Strafe off, he walks north (1.2-2.8 s) and
		// his facing is logged; strafe on (the default) for the mount at 3.5 s; he rides forward (3.7 s) and gallops (5 s), the top
		// speed logged; he gets off at the given time, strafe off, and walks north, east, south and west, 0.9 s each from 7 s, his
		// facing logged at the end of each. Facing and walking direction should match before and after the ride.)
		float DismountAt = 0.f;
		if (FParse::Param(FCommandLine::Get(), TEXT("ShotMount")) && FParse::Value(FCommandLine::Get(), TEXT("ShotDismount="), DismountAt))
		if (AKnightCharacter* Me = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr)
		{
			static int32 Logged = 0;   // a bit per logged step
			static float TopSpeed = 0.f;
			auto Report = [&](int32 Bit, const TCHAR* What, float WantYaw)
			{
				if (Logged & Bit) return;
				Logged |= Bit;
				const FVector V = Me->GetVelocity();
				const float VelYaw = V.Rotation().Yaw;
				UE_LOG(LogTemp, Display, TEXT("JEEBHORSE %s: walking toward %.0f, moving %.0f at %.0f cm/s, facing %.1f (off by %.1f), turns to movement %d, mounted %d"),
					What, WantYaw, VelYaw, V.Size2D(), Me->GetActorRotation().Yaw, FMath::Abs(FMath::FindDeltaAngleDegrees(Me->GetActorRotation().Yaw, VelYaw)), Me->OrientsToMovement() ? 1 : 0, Me->IsMounted() ? 1 : 0);
			};
			const float T = ShotClock;
			if (T > 1.f && T < 3.f) Me->SetStrafeForTest(false);
			if (T > 1.2f && T < 2.8f) Me->AddMovementInput(FVector(1.f, 0.f, 0.f), 1.f);
			if (T >= 2.75f) Report(1, TEXT("before the ride"), 0.f);
			if (T > 3.f && T < 3.2f) Me->SetStrafeForTest(true);
			if (Me->IsMounted() && T > 3.7f && T < DismountAt)
			{
				if (T > 5.f) Me->SprintForTest(true);
				Me->DoMove(0.f, 1.f);
				TopSpeed = FMath::Max(TopSpeed, float(Me->GetVelocity().Size2D()));
				if (T > 4.9f && !(Logged & 2)) { Logged |= 2; UE_LOG(LogTemp, Display, TEXT("JEEBHORSE riding: %.0f cm/s (top so far %.0f), max %.0f"), Me->GetVelocity().Size2D(), TopSpeed, Me->GetCharacterMovement()->MaxWalkSpeed); }
			}
			if (T >= DismountAt && Me->IsMounted())
			{
				UE_LOG(LogTemp, Display, TEXT("JEEBHORSE galloping: %.0f cm/s (top %.0f), max %.0f; getting off"), Me->GetVelocity().Size2D(), TopSpeed, Me->GetCharacterMovement()->MaxWalkSpeed);
				Me->SprintForTest(false);
				Me->DismountHorse();
			}
			if (T > DismountAt + 0.1f) Me->SetStrafeForTest(false);
			static const float Dirs[4] = { 0.f, 90.f, 180.f, -90.f };
			static const TCHAR* Names[4] = { TEXT("after the ride, north"), TEXT("after the ride, east"), TEXT("after the ride, south"), TEXT("after the ride, west") };
			for (int32 i = 0; i < 4; ++i)
			{
				const float From = 7.f + 0.9f * i;
				if (T > From && T < From + 0.9f)
				{
					Me->AddMovementInput(FRotator(0.f, Dirs[i], 0.f).Vector(), 1.f);
					if (T > From + 0.85f) Report(4 << i, Names[i], Dirs[i]);
				}
			}
		}
		// (-ShotCmds=a~1;b~2: console commands at 3 s, after the graphics settings are applied ('~' a space, ';' between), so a film
		// can be taken with one setting changed; shadow check 2026-11-15)
		static bool bShotCmdsDone = false;
		FString ShotCmds;
		if (!bShotCmdsDone && ShotClock > 3.f && FParse::Value(FCommandLine::Get(), TEXT("ShotCmds="), ShotCmds, false))
		{
			bShotCmdsDone = true;
			TArray<FString> Cmds; ShotCmds.Replace(TEXT("~"), TEXT(" ")).ParseIntoArray(Cmds, TEXT(";"));
			for (const FString& C : Cmds) { GEngine->Exec(GetWorld(), *C); UE_LOG(LogTemp, Display, TEXT("JEEBSHOTCMD %s"), *C); }
		}
		// (-ShotEye=X_Y_Z: a free camera there, looking along -ShotYaw / -ShotPitch, for pictures from the air: Oak Hill, 2026-11-11)
		static bool bShotEyeDone = false;
		FString ShotEye;
		if (!bShotEyeDone && ShotClock > 2.f && PC && FParse::Value(FCommandLine::Get(), TEXT("ShotEye="), ShotEye, false))
		{
			bShotEyeDone = true;
			TArray<FString> Parts;
			ShotEye.ParseIntoArray(Parts, TEXT("_"));
			if (Parts.Num() == 3)
				if (ACameraActor* Eye = GetWorld()->SpawnActor<ACameraActor>(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])), ShotRot))
				{
					float Fov = 70.f;
					FParse::Value(FCommandLine::Get(), TEXT("ShotFov="), Fov);
					Eye->GetCameraComponent()->SetFieldOfView(Fov);
					Eye->GetCameraComponent()->bConstrainAspectRatio = false;
					PC->SetViewTarget(Eye);
					if (FParse::Param(FCommandLine::Get(), TEXT("ShotNoHud")) && PC->GetHUD()) PC->GetHUD()->bShowHUD = false;   // (-ShotNoHud: close-ups with nothing over them)
				}
		}
		if (!ShotMove.IsEmpty())
		{
			// A move, picture by picture
			AKnightCharacter* K = Cast<AKnightCharacter>(PC ? PC->GetPawn() : nullptr);
			if (K && ShotClock > 7.f && ShotMoveClock < 0.f)
			{
				ShotMoveClock = 0.f; K->ShotPlayMove(ShotMove, ShotRot.Yaw + ShotFacing);
				if (FParse::Param(FCommandLine::Get(), TEXT("ShotNoHud")) && PC && PC->GetHUD()) PC->GetHUD()->bShowHUD = false;   // (a film with nothing over him)
			}
			// (with -ShotEyes too, 2026-11-15: the move only sets him up, e.g. -ShotMove=bowidle for the bow in hand, and the eyes take the pictures)
			static const bool bEyesToo = FCString::Strifind(FCommandLine::Get(), TEXT("ShotEyes=")) != nullptr;
			if (ShotMoveClock >= 0.f && !bEyesToo)
			{
				ShotMoveClock += DeltaSeconds;
				static float ShotSeconds = 2.6f;   // (-ShotSeconds=6: a longer series, for a chain of moves)
				static const bool bReadSeconds = FParse::Value(FCommandLine::Get(), TEXT("ShotSeconds="), ShotSeconds);
				static const int32 ShotEvery = []() { int32 V = 2; FParse::Value(FCommandLine::Get(), TEXT("ShotEvery="), V); return FMath::Max(V, 1); }();   // (-ShotEvery=1: every frame, for a quick move)
				if (ShotMoveClock < ShotSeconds && (ShotFrame++ % ShotEvery) == 0)
					FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("%s_%03d.png"), *ShotName, ShotFrame / 2), false, false);
				if (ShotMoveClock > ShotSeconds + 0.2f && PC) PC->ConsoleCommand(TEXT("quit"));
			}
		}

		// (test pictures: -ShotWear=5 puts on a full suit of that tier, -ShotPanel opens the Tab windows; JEEBWEIGHT logs the slow)
		static const bool bShotPanel = FParse::Param(FCommandLine::Get(), TEXT("ShotPanel"));
		static int32 ShotWear = -1;
		static bool bShotDressed = false;
		if (!bShotDressed && ShotClock > 6.f)
			if (AKnightCharacter* K = Cast<AKnightCharacter>(PC ? PC->GetPawn() : nullptr))
			{
				bShotDressed = true;
				USkillComponent* S = K->GetSkills();
				// (-ShotStyle=0/1/2 picks classic, light or heavy for that suit; -ShotMix wears one of each style across the slots;
				// -ShotBare takes the armour off, showing the chain mail)
				int32 ShotStyle = 0;
				FParse::Value(FCommandLine::Get(), TEXT("ShotStyle="), ShotStyle);
				const bool bMix = FParse::Param(FCommandLine::Get(), TEXT("ShotMix"));
				if (S && FParse::Value(FCommandLine::Get(), TEXT("ShotWear="), ShotWear))
					for (int32 Kd = JeebItems::K_Helmet; Kd <= JeebItems::K_Boots; ++Kd)
						S->Worn[Kd] = JeebItems::ArmourId(Kd, bMix ? (Kd - JeebItems::K_Helmet) % JeebItems::NumArmourStyles : ShotStyle, FMath::Clamp(ShotWear, 0, JeebItems::NumTiers - 1));
				if (S && FParse::Param(FCommandLine::Get(), TEXT("ShotBare")))
					for (int32 Kd = JeebItems::K_Helmet; Kd <= JeebItems::K_Boots; ++Kd) S->Worn[Kd] = 0;
				FString ShotOff;   // (-ShotOff=<digits> takes pieces off, counting from the helmet: 0 helmet, 1 chest, 2 shoulders, 3 gauntlets, 4 boots; -ShotOff=12 takes two. To see a cut edge or a gap)
				if (S && FParse::Value(FCommandLine::Get(), TEXT("ShotOff="), ShotOff))
					for (const TCHAR C : ShotOff)
						if (C >= TEXT('0') && JeebItems::K_Helmet + (C - TEXT('0')) <= JeebItems::K_Boots) S->Worn[JeebItems::K_Helmet + (C - TEXT('0'))] = 0;
				int32 ShotShield = -1;   // (-ShotShield=<0 kite, 1 round, 2 tower>)
				if (S && FParse::Value(FCommandLine::Get(), TEXT("ShotShield="), ShotShield) && ShotShield >= 0) S->Worn[JeebItems::K_Shield] = JeebItems::ShieldId(ShotShield, FMath::Max(ShotWear, 0));
				int32 ShotAmulet = -1;   // (-ShotAmulet=<row among the amulets in Accessories.csv>: wears it, on its chain)
				if (S && FParse::Value(FCommandLine::Get(), TEXT("ShotAmulet="), ShotAmulet) && ShotAmulet >= 0) S->Worn[JeebItems::K_Amulet] = JeebItems::GearId(JeebItems::K_Amulet, ShotAmulet);
				if (S) S->OnGearChanged.Broadcast();   // (the knight redraws its armour)
				if (bShotPanel) K->SetPanelCursor(true);
				if (S) UE_LOG(LogTemp, Display, TEXT("JEEBWEIGHT kg=%.2f speed x%.3f"), JeebItems::TotalWeight(S), JeebItems::WeightSpeedMul(S));
			}
#if !UE_BUILD_SHIPPING
		{   // (-ShotLookPanel: opens the Escape screen on the LOOK tab at 6.5 s; the tab then moves a slider and saves and loads a preset itself, log JEEBLOOK)
			static const bool bShotLookPanel = FParse::Param(FCommandLine::Get(), TEXT("ShotLookPanel"));
			static bool bLookOpened = false;
			if (bShotLookPanel && !bLookOpened && ShotClock > 6.5f)
				if (AKnightCharacter* K = Cast<AKnightCharacter>(PC ? PC->GetPawn() : nullptr))
				{
					bLookOpened = true;
					K->ToggleSettings();
					if (AKnightHUD* H = Cast<AKnightHUD>(PC->GetHUD())) H->ShotOpenLook();
				}
		}
#endif
		if (bShotDressed && ShotClock > 8.f && ShotClock < 8.1f)
			if (ACharacter* C = Cast<ACharacter>(PC ? PC->GetPawn() : nullptr))
				UE_LOG(LogTemp, Display, TEXT("JEEBWEIGHT MaxWalkSpeed=%.1f"), C->GetCharacterMovement()->MaxWalkSpeed);
		// (JEEBGPU, 2026-11-14: a still's median GPU and whole-frame time from 4.5 to 8.8 s, after the view has moved and before the
		// screenshot's hitch, so a before/after frame-cost check needs no profiler: compare runs of the same view in the same copy)
		if (ShotMove.IsEmpty())
		{
			static TArray<float> GpuMs, FrameMs, Draws, Prims;   // (draws and triangles drawn per frame: GNumDrawCallsRHI, 2026-11-14)
			static bool bGpuLogged = false;
			if (ShotClock > 4.5f && ShotClock < 8.8f)
			{
				GpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
				FrameMs.Add(DeltaSeconds * 1000.f);
				Draws.Add(float(GNumDrawCallsRHI[0])); Prims.Add(float(GNumPrimitivesDrawnRHI[0]));
			}
			else if (ShotClock >= 8.8f && !bGpuLogged && GpuMs.Num() > 0)
			{
				bGpuLogged = true;
				GpuMs.Sort(); FrameMs.Sort(); Draws.Sort(); Prims.Sort();
				UE_LOG(LogTemp, Display, TEXT("JEEBGPU gpu %.2f ms, frame %.2f ms, draws %.0f, triangles %.0f (medians of %d frames)"), GpuMs[GpuMs.Num() / 2], FrameMs[FrameMs.Num() / 2], Draws[Draws.Num() / 2], Prims[Prims.Num() / 2], GpuMs.Num());
			}
		}
		// (-ShotEyes=X_Y_Z_Yaw_Pitch+X_Y_Z_Yaw_Pitch+...: several free-camera stills in one run, <ShotName>_<n>.png, 2 s apart from
		// 9 s on, then quit; TownLab buildings 2026-11-14: one run instead of one per view)
		static TArray<FString> ShotEyes;
		static const bool bEyes = []() { FString V; if (!FParse::Value(FCommandLine::Get(), TEXT("ShotEyes="), V, false)) return false; V.ParseIntoArray(ShotEyes, TEXT("+")); return ShotEyes.Num() > 0; }();
		if (bEyes && PC)
		{
			bShotTaken = true;   // (no single still, and the quit below waits for the last)
			static int32 EyeIdx = -1;
			static ACameraActor* EyeCam = nullptr;
			static bool EyeFollowOn = false;
			static FVector EyeFollowOffset = FVector::ZeroVector;
			static int32 ShotIdx = -1;
			static uint64 EyeMovedFrame = 0;
			// (-ShotEyesStart / -ShotEyesGap: when the first eye goes and how long each stays; 2 and 8 keep one eye through JEEBGPU's 4.5-8.8 s)
			static float EyeStart = 7.f, EyeGap = 2.f;
			static const bool bEyeTimes = []() { FParse::Value(FCommandLine::Get(), TEXT("ShotEyesStart="), EyeStart); FParse::Value(FCommandLine::Get(), TEXT("ShotEyesGap="), EyeGap); EyeGap = FMath::Max(EyeGap, 1.6f); return true; }();
			(void)bEyeTimes;
			const int32 Want = ShotClock < EyeStart ? -1 : FMath::FloorToInt((ShotClock - EyeStart) / EyeGap);
			// (JEEBEYE, foliage 2026-11-14: each eye's medians of frame, game, render thread, GPU, draws and triangles, from 2 s after the move
			// to the end of its turn, so one run measures many spots; -ShotEyesNoPics takes no pictures)
			static TArray<float> EF, EGm, ER, EGpu, ED, EP;
			static float EyeSunBase = -1.f;
			static const bool bNoPics = FParse::Param(FCommandLine::Get(), TEXT("ShotEyesNoPics"));
			const float InEye = ShotClock - EyeStart - EyeGap * EyeIdx;
			if (EyeIdx >= 0 && EyeIdx < ShotEyes.Num() && InEye > 2.0f && InEye < EyeGap - 0.15f)
			{
				EF.Add(DeltaSeconds * 1000.f); EGm.Add(FPlatformTime::ToMilliseconds(GGameThreadTime)); ER.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
				EGpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())); ED.Add(float(GNumDrawCallsRHI[0])); EP.Add(float(GNumPrimitivesDrawnRHI[0]));
			}
			if (Want > EyeIdx && EyeIdx >= 0 && EF.Num() > 0)
			{
				if (FParse::Param(FCommandLine::Get(), TEXT("ShotCensus")) && EyeCam) JeebCensus(GetWorld(), EyeCam->GetActorLocation());
				auto Med = [](TArray<float>& A) { A.Sort(); return A[A.Num() / 2]; };
				UE_LOG(LogTemp, Display, TEXT("JEEBEYE %d %s frame %.2f game %.2f render %.2f gpu %.2f draws %.0f tris %.0f (%d frames)"), EyeIdx, EyeIdx < ShotEyes.Num() ? *ShotEyes[EyeIdx] : TEXT("?"),
					Med(EF), Med(EGm), Med(ER), Med(EGpu), Med(ED), Med(EP), EF.Num());
				EF.Reset(); EGm.Reset(); ER.Reset(); EGpu.Reset(); ED.Reset(); EP.Reset();
			}
			// (mask 32: the sun creeps on every frame, as in play: a frozen sun lets the cached sun shadows skip redrawing what stands still,
			// which makes shadows look almost free in a test still)
			if (EyeSunBase >= 0.f) { static uint32 Tick32 = 0; ADayNight::FixedPhase = EyeSunBase + 0.00004f * float(++Tick32 % 64); }
			if (EyeCam && EyeFollowOn) if (const APawn* Me = PC->GetPawn()) EyeCam->SetActorLocation(Me->GetActorLocation() + EyeFollowOffset);   // (a close-up keeps up with him: jeeb.Test.Walk)
			if (!bNoPics && EyeIdx >= 0 && EyeIdx < ShotEyes.Num() && ShotIdx < EyeIdx && ShotClock - EyeStart - EyeGap * EyeIdx > 1.4f && GFrameCounter > EyeMovedFrame + 30)   // (1.4 s and 30 frames after the move: settled, even after a long frame regrowing the grass)
			{
				ShotIdx = EyeIdx;
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("%s_%d.png"), *ShotName, EyeIdx), true, false);
			}
			if (Want > EyeIdx)
			{
				EyeIdx = Want;
				EyeMovedFrame = GFrameCounter;
				TArray<FString> P;
				if (EyeIdx < ShotEyes.Num()) ShotEyes[EyeIdx].ParseIntoArray(P, TEXT("_"));
				if (P.Num() >= 5)
				{
					// (optional 6th: the sun's phase there, 0 noon, 0.5 the low sun; 7th: a test mask, see JeebFoliageMask)
					if (P.Num() >= 6) ADayNight::FixedPhase = FCString::Atof(*P[5]);
					// (8th, 2026-11-15: the meadow's grass for this eye, jeeb.Grass.Pack 0 ours, 1 Grass Library, 2 Open World Foliage)
					if (P.Num() >= 8 && FCString::Atoi(*P[7]) >= 0)   // (-1: leave it)
						if (IConsoleVariable* Pack = IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.Grass.Pack")))
							Pack->Set(FCString::Atoi(*P[7]), ECVF_SetByConsole);
					// (9th, shadow check 2026-11-15: console commands for this eye, ';' between commands and '~' for a space, e.g.
					// r.Shadow.FilterMethod~0;r.ScreenPercentage~100; they stay set for later eyes, so each eye names every toggle it relies on)
					if (P.Num() >= 9)
					{
						TArray<FString> Cmds; P[8].Replace(TEXT("~"), TEXT(" ")).ParseIntoArray(Cmds, TEXT(";"));
						for (const FString& Cmd : Cmds) { GEngine->Exec(GetWorld(), *Cmd); UE_LOG(LogTemp, Display, TEXT("JEEBEYECMD %d %s"), EyeIdx, *Cmd); }
					}
					JeebFoliageMask(GetWorld(), P.Num() >= 7 ? FCString::Atoi(*P[6]) : 0);
					EyeSunBase = ((P.Num() >= 7 ? FCString::Atoi(*P[6]) : 0) & 32) ? ADayNight::FixedPhase : -1.f;
					const float Ex = FCString::Atof(*P[0]), Ey = FCString::Atof(*P[1]);   // (Z: above the ground there, or above the floor in MoveLab)
					FVector L(Ex, Ey, FCString::Atof(*P[2]) + (GetWorld()->GetMapName().Contains(TEXT("MoveLab")) ? JeebTerrain::HeightAt(0.f, 0.f) : JeebTerrain::HeightAt(Ex, Ey)));
					const FRotator R(FCString::Atof(*P[4]), FCString::Atof(*P[3]), 0.f);
					if (APawn* Me = PC->GetPawn())   // (he stands under the eye, hidden, so the grass grows round the view as round a player there)
					{
						// (mask 1024, 2026-11-15: he stands in view instead, 3.5 m ahead of the eye with his back to it, for a third-person look)
						const bool bShow = P.Num() >= 7 && (FCString::Atoi(*P[6]) & 1024);
						// (9th and 10th, 2026-11-15: he turns that many degrees from the eye's way (0 his back to it, 180 facing it, 90 his right side to it),
						// and stands that many cm ahead (350); for close-ups of his gear)
						const float Turn = P.Num() >= 9 ? FCString::Atof(*P[8]) : 0.f;
						const float Ahead = P.Num() >= 10 ? FCString::Atof(*P[9]) : 350.f;
						const FVector Fw = FRotator(0.f, R.Yaw, 0.f).Vector();
						const float Ax = bShow ? Ex + Fw.X * Ahead : Ex, Ay = bShow ? Ey + Fw.Y * Ahead : Ey;
						Me->SetActorHiddenInGame(!bShow);
						const float Gz = GetWorld()->GetMapName().Contains(TEXT("MoveLab")) ? L.Z - FCString::Atof(*P[2]) : JeebTerrain::HeightAt(Ax, Ay);
						if (bShow && P.Num() >= 10) L.Z = Gz + FCString::Atof(*P[2]);   // (a close-up: the eye's height is above his ground, not its own)
						Me->SetActorLocation(FVector(Ax, Ay, Gz + 100.f), false, nullptr, ETeleportType::TeleportPhysics);
						EyeFollowOn = bShow && P.Num() >= 10;
						EyeFollowOffset = L - Me->GetActorLocation();
						if (bShow)
						{
							const FRotator Face(0.f, R.Yaw + Turn, 0.f);
							Me->SetActorRotation(Face);
							ShotRot.Yaw = Face.Yaw;   // (the control rotation, set every frame above, agrees, so he doesn't turn back)
							if (AController* Ctl = Me->GetController()) Ctl->SetControlRotation(Face);
							if (AKnightCharacter* K = Cast<AKnightCharacter>(Me)) K->ShotFaceYaw = Face.Yaw;
						}
					}
					ShotEyesSet(PC, EyeIdx);
					if (!EyeCam) EyeCam = GetWorld()->SpawnActor<ACameraActor>(L, R);
					if (EyeCam)
					{
						EyeCam->SetActorLocationAndRotation(L, R);
						float Fov = 70.f;
						FParse::Value(FCommandLine::Get(), TEXT("ShotFov="), Fov);
						EyeCam->GetCameraComponent()->SetFieldOfView(Fov);
						EyeCam->GetCameraComponent()->bConstrainAspectRatio = false;
						PC->SetViewTarget(EyeCam);
						if (PC->GetHUD()) PC->GetHUD()->bShowHUD = false;
					}
				}
			}
			if (EyeIdx > ShotEyes.Num()) PC->ConsoleCommand(TEXT("quit"));
		}
		else if (ShotMove.IsEmpty() && ShotClock > 9.f && !bShotTaken && !FCString::Strifind(FCommandLine::Get(), TEXT("LabTour")))
		{
			bShotTaken = true;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / (ShotName + TEXT(".png")), true, false);
		}
		if (ShotMove.IsEmpty() && ShotClock > 11.f && PC && !bEyes && !FCString::Strifind(FCommandLine::Get(), TEXT("LabTour")))   // (TownLab's tour quits by itself)   // (a film quits by its own clock, above, so a long -ShotSeconds is not cut off)
		{
			UE_LOG(LogTemp, Log, TEXT("JEEBSUN phase %.3f after %.1f s"), ADayNight::Phase, ShotClock);   // (how far the sun went: phase 1 is a whole loop)
			if (const APawn* Me = PC->GetPawn()) UE_LOG(LogTemp, Log, TEXT("JEEBSHOT knight at %.1f %.1f %.1f"), Me->GetActorLocation().X, Me->GetActorLocation().Y, Me->GetActorLocation().Z);   // (where -ShotAt left him, e.g. how close walls let him stand)
			PC->ConsoleCommand(TEXT("quit"));
		}
	}

	if (!FogActor.IsValid()) return;
	APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!P) return;
	const FVector L = P->GetActorLocation();
	const float Woods = FMath::Clamp((JeebTerrain::WoodsDensity(L.X, L.Y) - 0.45f) / 0.4f, 0.f, 1.f);
	const float Grave = 1.f - FMath::Clamp(FVector2D::Distance(FVector2D(L.X, L.Y), FVector2D(JeebTerrain::GraveyardPos())) / 1100.f, 0.f, 1.f);
	const float Target = 0.010f + 0.007f * Woods + 0.016f * Grave;
	FogNow = FMath::FInterpTo(FogNow, Target, DeltaSeconds, 0.6f);
	FogActor->GetComponent()->SetFogDensity(FogNow * FogScale);
}
