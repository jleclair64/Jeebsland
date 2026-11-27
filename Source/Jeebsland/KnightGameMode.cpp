#include "KnightGameMode.h"
#include "FastTravel.h"
#include "KnightGameState.h"
#include "KnightCharacter.h"
#include "KnightPlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "KnightHUD.h"
#include "JeebAccounts.h"
#include "JeebChat.h"
#include "JeebSave.h"
#include "JeebGameInstance.h"
#include "Terrain.h"
#include "OakHill.h"
#include "HitBoxes.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameSession.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "TownLabDirector.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerResponse.h"
#include "HttpServerRequest.h"

AKnightGameMode::AKnightGameMode()
{
	DefaultPawnClass = AKnightCharacter::StaticClass();
	HUDClass = AKnightHUD::StaticClass();
	GameStateClass = AKnightGameState::StaticClass();
	PlayerControllerClass = AKnightPlayerController::StaticClass();
}

bool AKnightGameMode::IsOnline() const
{
	return GetNetMode() == NM_DedicatedServer || GetNetMode() == NM_ListenServer;
}

int32 AKnightGameMode::WorldPort() const
{
	return GetWorld() ? GetWorld()->URL.Port : 7777;
}

int32 AKnightGameMode::MaxPlayers() const
{
	if (const UJeebGameInstance* GI = GetGameInstance<UJeebGameInstance>())
		for (const FJeebWorld& W : GI->Worlds) if (W.Port == WorldPort()) return W.MaxPlayers;
	return 200;
}

int32 AKnightGameMode::PlayersOnline() const
{
	return OnlineKeys.Num();
}

namespace JeebEconTest { void StartRace(AKnightGameMode* GM); }   // (EconomyTest.cpp: -EconRace, two servers racing for the same Market listings)

void AKnightGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (!IsOnline()) return;
	JeebEconTest::StartRace(this);
	GetWorldTimerManager().SetTimer(AutosaveTimer, this, &AKnightGameMode::AutosaveAll, 60.f, true);
	StartStatus();
	UE_LOG(LogTemp, Log, TEXT("Jeebsland world up on port %d (status on %d)"), WorldPort(), WorldPort() + UJeebGameInstance::StatusPortOffset);
}

void AKnightGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	// The world is shutting down: everyone's character is saved and their "logged in" marks cleared
	if (IsOnline())
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (AKnightPlayerController* PC = Cast<AKnightPlayerController>(It->Get()))
			{
				SaveCharacter(PC);
				if (PC->bLoggedIn) JeebAccounts::ReleaseOnline(PC->AccountKey, WorldPort());
			}
		}
	}
	if (bStatusUp) FHttpServerModule::Get().StopAllListeners();
	Super::EndPlay(Reason);
}

void AKnightGameMode::StartStatus()
{
	// A tiny web page on Port + 100 that answers "<players> <max>" (the starting screen asks every few seconds)
	TSharedPtr<IHttpRouter> Router = FHttpServerModule::Get().GetHttpRouter(WorldPort() + UJeebGameInstance::StatusPortOffset, true);
	if (!Router.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("Jeebsland: could not open the status port")); return; }
	TWeakObjectPtr<AKnightGameMode> Self(this);
	Router->BindRoute(FHttpPath(TEXT("/status")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([Self](const FHttpServerRequest&, const FHttpResultCallback& Done)
		{
			const FString Body = Self.IsValid() ? FString::Printf(TEXT("%d %d"), Self->PlayersOnline(), Self->MaxPlayers()) : FString(TEXT("0 0"));
			Done(FHttpServerResponse::Create(Body, TEXT("text/plain")));
			return true;
		}));
	FHttpServerModule::Get().StartAllListeners();
	bStatusUp = true;
}

void AKnightGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (UJeebGameInstance::InMenu(this)) return;                 // the starting screen: no knight
	if (IsOnline() && !NewPlayer->IsLocalController()) return;  // online: not until they log in (TryLogin)
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AKnightGameMode::TryLogin(AKnightPlayerController* PC, const FString& Name, const FString& Password, bool bCreate, bool bDev)
{
	if (!PC || PC->bLoggedIn || !IsOnline()) return;
	auto Refuse = [&](const FString& Why)
	{
		UE_LOG(LogTemp, Log, TEXT("Jeebsland: login refused for \"%s\": %s"), *Name, *Why);
		PC->ClientLoginResult(false, Why);
		// (the player's machine leaves by itself; if it doesn't, it is shown the door after a moment)
		TWeakObjectPtr<AKnightPlayerController> W(PC);
		FTimerHandle T;
		GetWorldTimerManager().SetTimer(T, FTimerDelegate::CreateLambda([this, W, Why]()
		{
			if (W.IsValid() && !W->bLoggedIn && GameSession) GameSession->KickPlayer(W.Get(), FText::FromString(Why));
		}), 3.f, false);
	};
	if (++PC->LoginTries > 3) return;   // (one login per connection; a few repeats are ignored)

	FString Display = Name.TrimStartAndEnd();
	const FString Key = JeebAccounts::KeyOf(Display);
	if (bDev)
	{
		// Test accounts, only for a server started by the editor's Play button
		if (GetWorld()->WorldType != EWorldType::PIE) { Refuse(TEXT("Test logins only work in the editor.")); return; }
	}
	else
	{
		const FString Bad = JeebAccounts::CheckName(Display);
		if (!Bad.IsEmpty()) { Refuse(Bad); return; }
		if (JeebChat::IsBanned(Key)) { Refuse(TEXT("This account has been banned.")); return; }

		// Too many wrong passwords for this name lately: wait a while
		TArray<double>& Fails = FailedTries.FindOrAdd(Key);
		const double Now = FPlatformTime::Seconds();
		Fails.RemoveAll([Now](double T) { return Now - T > 300.0; });
		if (Fails.Num() >= 5) { Refuse(TEXT("Too many wrong passwords. Wait a few minutes and try again.")); return; }

		if (bCreate)
		{
			FString Err;
			if (!JeebAccounts::Create(Display, Password, Err)) { Refuse(Err); return; }
			UE_LOG(LogTemp, Log, TEXT("Jeebsland: new account %s"), *Key);
		}
		else if (!JeebAccounts::Verify(Display, Password, Display))
		{
			Fails.Add(Now);
			Refuse(TEXT("Wrong name or password."));
			return;
		}
	}

	if (OnlineKeys.Contains(Key) || !JeebAccounts::ClaimOnline(Key, WorldPort())) { Refuse(TEXT("That account is already logged in.")); return; }
	if (OnlineKeys.Num() >= MaxPlayers()) { JeebAccounts::ReleaseOnline(Key, WorldPort()); Refuse(TEXT("This world is full. Try another one.")); return; }

	// Welcome in: load the character (a new one starts with the starting kit) and give them their knight
	OnlineKeys.Add(Key);
	PC->bLoggedIn = true;
	PC->AccountKey = Key;
	if (PC->PlayerState) PC->PlayerState->SetPlayerName(Display);
	PC->PendingSave = UJeebSave::ReadFile(JeebAccounts::CharacterPath(Key));
	UE_LOG(LogTemp, Log, TEXT("Jeebsland: %s: %s"), *Key, PC->PendingSave ? TEXT("saved character loaded") : TEXT("new character"));
	if (PC->ClassChoice < 0 && PC->PendingSave && PC->PendingSave->SavedClass >= 0 && PC->PendingSave->SavedClass <= 2) PC->ClassChoice = (int8)PC->PendingSave->SavedClass;   // (back as the class you left as)
	PC->ClientLoginResult(true, FString());
	RestartPlayer(PC);
	if (AKnightCharacter* Knight = Cast<AKnightCharacter>(PC->GetPawn()))
	{
		if (PC->PendingSave && PC->PendingSave->ApplyTo(Knight->GetSkills())) Knight->RefreshEquipment();
		if (Knight->GetTravel()) Knight->GetTravel()->LoadAccount(Key);   // (the towns this account has visited, shared by its characters: FastTravel.cpp)
		// TESTING ONLY: accounts listed in DefaultGame.ini ([Jeebsland.Testing] +MaxLevelAccounts=name) get every skill at the cap (29)
		// when they log in, to try all the gear. Remove the line there to turn it off.
		TArray<FString> MaxLevelAccounts;
		if (GConfig) GConfig->GetArray(TEXT("Jeebsland.Testing"), TEXT("MaxLevelAccounts"), MaxLevelAccounts, GGameIni);
		bool bEditorMax = false;   // editor Play on the owner's PC (its "Tester" accounts) counts too, when MaxLevelInEditor is on
		if (GConfig) GConfig->GetBool(TEXT("Jeebsland.Testing"), TEXT("MaxLevelInEditor"), bEditorMax, GGameIni);
		bEditorMax = bEditorMax && GetWorld() && GetWorld()->IsPlayInEditor();
		if ((bEditorMax || MaxLevelAccounts.ContainsByPredicate([&](const FString& A) { return A.Equals(Key, ESearchCase::IgnoreCase); })) && Knight->GetSkills())
		{
			Knight->GetSkills()->SetAllMaxLevel();
			Knight->GetSkills()->Say(5.f, FColor(255, 215, 90), TEXT("Test account: every skill set to level 99."));
			UE_LOG(LogTemp, Log, TEXT("Jeebsland: %s is a max-level test account"), *Key);
		}
	}
	PC->PendingSave = nullptr;
	SaveCharacter(PC);   // (a new character gets its file straight away)
	UE_LOG(LogTemp, Log, TEXT("Jeebsland: %s logged in (%d on this world)"), *Key, OnlineKeys.Num());
}

void AKnightGameMode::SaveCharacter(AKnightPlayerController* PC)
{
	if (!PC || !PC->bLoggedIn || !IsOnline()) return;
	AKnightCharacter* Knight = Cast<AKnightCharacter>(PC->GetPawn());
	if (!Knight || !Knight->GetSkills()) return;
	if (UJeebSave* S = UJeebSave::Capture(Knight->GetSkills(), Knight))
	{
		if (!UJeebSave::WriteFile(S, JeebAccounts::CharacterPath(PC->AccountKey)))
			UE_LOG(LogTemp, Error, TEXT("Jeebsland: could not save %s"), *PC->AccountKey);
	}
}

void AKnightGameMode::SwapPawnClass(AKnightPlayerController* PC, int32 Class, const FTransform& At)
{
	AKnightCharacter* Old = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
	if (!Old) return;
	UJeebSave* Keep = nullptr;
	if (IsOnline() && PC->bLoggedIn && Old->GetSkills())
	{
		SaveCharacter(PC);
		Keep = UJeebSave::Capture(Old->GetSkills(), Old);
	}
	PC->ClassChoice = (int8)FMath::Clamp(Class, 0, 2);
	PC->UnPossess();
	Old->Destroy();
	RestartPlayerAtTransform(PC, At);
	AKnightCharacter* NewK = Cast<AKnightCharacter>(PC->GetPawn());
	if (NewK && Keep)
	{
		if (Keep->ApplyTo(NewK->GetSkills())) NewK->RefreshEquipment();
		if (NewK->GetTravel()) NewK->GetTravel()->LoadAccount(PC->AccountKey);
		SaveCharacter(PC);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBSWAP server: %s is now %s (%s)"), PC->PlayerState ? *PC->PlayerState->GetPlayerName() : TEXT("?"),
		Class == 1 ? TEXT("troll") : Class == 2 ? TEXT("wizard") : TEXT("knight"), NewK ? *NewK->GetName() : TEXT("NO PAWN"));
}

void AKnightGameMode::AutosaveAll()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AKnightPlayerController* PC = Cast<AKnightPlayerController>(It->Get());
		if (!PC || !PC->bLoggedIn) continue;
		SaveCharacter(PC);
		JeebAccounts::RefreshOnline(PC->AccountKey, WorldPort());
	}
}

void AKnightGameMode::Logout(AController* Exiting)
{
	// (their character was saved just before their knight left: AKnightPlayerController::PawnLeavingGame)
	if (AKnightPlayerController* PC = Cast<AKnightPlayerController>(Exiting))
	{
		if (PC->bLoggedIn)
		{
			OnlineKeys.Remove(PC->AccountKey);
			JeebAccounts::ReleaseOnline(PC->AccountKey, WorldPort());
			UE_LOG(LogTemp, Log, TEXT("Jeebsland: %s logged out (%d on this world)"), *PC->AccountKey, OnlineKeys.Num());
			PC->bLoggedIn = false;
		}
	}
	Super::Logout(Exiting);
}


// ---- Oak Hill's wake-up spots (see the header)
FVector AKnightGameMode::OakWakeSpot(UWorld* World, const AActor* Self, float& OutYaw)
{
	const FVector2D Oak(JeebOakHill::PosX, JeebOakHill::PosY);
	const FVector2D Home(JeebOakHill::SpawnX, JeebOakHill::SpawnY);
	auto YawTo = [&](const FVector2D& From) { const FVector2D D = Oak - From; return FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)); };
	auto Ground = [](const FVector2D& P) { return FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) + 130.f); };
	if (!World) { OutYaw = JeebOakHill::SpawnYaw; return Ground(Home); }
	// preference: the home spot, then alternately either side of it along each row (12 degrees apart, the second row 6 degrees off and 1 m further out)
	TArray<FVector2D> Spots;
	for (int32 Row = 0; Row < 2; ++Row)
		for (int32 k = 0; k < 11; ++k)
		{
			const float Deg = (Row ? 6.f : 0.f) + 12.f * ((k + 1) / 2) * ((k & 1) ? 1.f : -1.f);
			const float R = 1000.f + 100.f * Row, A = FMath::DegreesToRadians(Deg);
			Spots.Add(Oak + FVector2D(R * FMath::Sin(A), -R * FMath::Cos(A)));   // (0 degrees: due west of the oak, the home spot; X is north, Y east)
		}
	FCollisionQueryParams Q(SCENE_QUERY_STAT(OakWake), false, Self);
	for (const FVector2D& S : Spots)
	{
		bool bFree = true;
		for (TActorIterator<AKnightCharacter> It(World); It; ++It)
			if (*It != Self && FVector2D::DistSquared(FVector2D(It->GetActorLocation()), S) < 150.f * 150.f) { bFree = false; break; }
		if (!bFree) continue;
		const FVector At = Ground(S);
		// open ground: nothing solid (a prop, a wall, a trunk) where a knight would stand, a little above the ground
		if (World->OverlapBlockingTestByChannel(At + FVector(0.f, 0.f, 20.f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(45.f, 90.f), Q)) continue;
		OutYaw = YawTo(S);
		UE_LOG(LogTemp, Log, TEXT("JEEBSPAWN Oak Hill spot %d of %d at %.0f %.0f yaw %.0f (%s)"), int32(&S - Spots.GetData()), Spots.Num(), S.X, S.Y, OutYaw, Self ? *Self->GetName() : TEXT("a new character"));
		return At;
	}
	OutYaw = JeebOakHill::SpawnYaw;
	return Ground(Home);
}
APawn* AKnightGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	FActorSpawnParameters Params;
	Params.Instigator = GetInstigator();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;   // players are never saved into the map

	// (TownLab, the test map: always between its two towns, looking at both)
	if (GetWorld()->GetMapName().Contains(TEXT("TownLab")))
	{
		float LabYaw = 0.f;
		const FVector LabAt = ATownLab::SpawnPoint(LabYaw) + FVector(200.f * (SpawnCount++ % 7), 0.f, 0.f);
		return GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), LabAt, FRotator(0.f, LabYaw, 0.f), Params);
	}
	// A saved character comes back where it logged out (if that spot is sensible)
	const AKnightPlayerController* KPC = Cast<AKnightPlayerController>(NewPlayer);
	const bool bLab = JeebHit::InMoveLab(GetWorld());
	const bool bOak = !bLab && JeebOakHill::Enabled();
	// TESTING (owner 2026-11-12): [Jeebsland.Testing] StartAtOakHill=True in DefaultGame.ini starts every login at Oak Hill, wherever it logged out
	static const bool bAlwaysOak = []() { bool v = false; if (GConfig) GConfig->GetBool(TEXT("Jeebsland.Testing"), TEXT("StartAtOakHill"), v, GGameIni); return v; }();
#if !UE_BUILD_SHIPPING
	// TESTING (2026-11-18): -StartAtSteele on the command line, or [Jeebsland.Testing] StartAtSteeleCreek=True, starts every login in
	// Steele Creek's forge yard (off by default; never in a shipped game)
	static const bool bAlwaysSteele = []() { bool v = FParse::Param(FCommandLine::Get(), TEXT("StartAtSteele")); if (!v && GConfig) GConfig->GetBool(TEXT("Jeebsland.Testing"), TEXT("StartAtSteeleCreek"), v, GGameIni); return v; }();
	if (!bLab && bAlwaysSteele && JeebSteele::Enabled())
	{
		const FVector2D S = JeebSteele::ToWorld(JeebSteele::SpawnE, JeebSteele::SpawnN) + FVector2D(0.f, 200.f * (SpawnCount++ % 7));
		return GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), FVector(S.X, S.Y, JeebTerrain::HeightAt(S.X, S.Y) + 130.f), FRotator(0.f, JeebSteele::SpawnYaw, 0.f), Params);
	}
#endif
	if (KPC && KPC->PendingSave && KPC->PendingSave->bHasPos && !(bOak && bAlwaysOak))
	{
		const FVector P = KPC->PendingSave->Pos;
		const float Ground = JeebTerrain::HeightAt(P.X, P.Y);
		if (FMath::Abs(P.X) < 1.0e6f && FMath::Abs(P.Y) < 1.0e6f && P.Z > Ground - 200.f && P.Z < Ground + 5000.f)
		{
			const FVector Loc(P.X, P.Y, FMath::Max(P.Z, Ground + 100.f) + 30.f);
			return GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), Loc, FRotator(0.f, KPC->PendingSave->Yaw, 0.f), Params);
		}
	}

	// Oak Hill (the real rule since 2026-11-18): the plaza by the big oak, facing it, the next free spot along its arc when others stand there.
	// The old starting point in MoveLab: the next ones on a 2 m ring round it, 60 degrees apart
	if (bOak)
	{
		float OakYaw = JeebOakHill::SpawnYaw;
		const FVector OakAt = OakWakeSpot(GetWorld(), nullptr, OakYaw);
		return GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), OakAt, FRotator(0.f, OakYaw, 0.f), Params);
	}
	const FVector2D Base = bOak ? FVector2D(JeebOakHill::SpawnX, JeebOakHill::SpawnY) : FVector2D(0.f, 0.f);
	FVector2D At = Base;
	if (SpawnCount > 0)
	{
		const float A = FMath::DegreesToRadians(60.f * (SpawnCount - 1));
		At = Base + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 200.f;
	}
	SpawnCount = (SpawnCount + 1) % 7;
	const FVector Loc(At.X, At.Y, JeebTerrain::HeightAt(At.X, At.Y) + 130.f);
	return GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), Loc, FRotator(0.f, bOak ? JeebOakHill::SpawnYaw : 0.f, 0.f), Params);
}

UClass* AKnightGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	const AKnightPlayerController* PC = Cast<AKnightPlayerController>(InController);
	bool bTroll = false, bWizard = false;
	if (PC && PC->ClassChoice >= 0) { bTroll = PC->ClassChoice == 1; bWizard = PC->ClassChoice == 2; }
	else if (InController && InController->IsLocalController())
	{
		FString As;
		bTroll = FParse::Value(FCommandLine::Get(), TEXT("PlayAs="), As) && As.Equals(TEXT("Troll"), ESearchCase::IgnoreCase);
		bWizard = As.Equals(TEXT("Wizard"), ESearchCase::IgnoreCase);
	}
	if (bWizard) return AWizardCharacter::StaticClass();   // (the playable wizard, 2026-11-21)
	return bTroll ? ATrollCharacter::StaticClass() : Super::GetDefaultPawnClassForController_Implementation(InController);
}
