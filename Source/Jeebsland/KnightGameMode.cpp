#include "KnightGameMode.h"
#include "KnightGameState.h"
#include "KnightCharacter.h"
#include "KnightPlayerController.h"
#include "KnightHUD.h"
#include "JeebAccounts.h"
#include "JeebSave.h"
#include "JeebGameInstance.h"
#include "Terrain.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameSession.h"
#include "TimerManager.h"
#include "EngineUtils.h"
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

void AKnightGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (!IsOnline()) return;
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
	PC->ClientLoginResult(true, FString());
	RestartPlayer(PC);
	if (AKnightCharacter* Knight = Cast<AKnightCharacter>(PC->GetPawn()))
	{
		if (PC->PendingSave && PC->PendingSave->ApplyTo(Knight->GetSkills())) Knight->RefreshEquipment();
		// TESTING ONLY: accounts listed in DefaultGame.ini ([Jeebsland.Testing] +MaxLevelAccounts=name) get every skill at 99
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

APawn* AKnightGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	FActorSpawnParameters Params;
	Params.Instigator = GetInstigator();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;   // players are never saved into the map

	// A saved character comes back where it logged out (if that spot is sensible)
	const AKnightPlayerController* KPC = Cast<AKnightPlayerController>(NewPlayer);
	if (KPC && KPC->PendingSave && KPC->PendingSave->bHasPos)
	{
		const FVector P = KPC->PendingSave->Pos;
		const float Ground = JeebTerrain::HeightAt(P.X, P.Y);
		if (FMath::Abs(P.X) < 1.0e6f && FMath::Abs(P.Y) < 1.0e6f && P.Z > Ground - 200.f && P.Z < Ground + 5000.f)
		{
			const FVector Loc(P.X, P.Y, FMath::Max(P.Z, Ground + 100.f) + 30.f);
			return GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), Loc, FRotator(0.f, KPC->PendingSave->Yaw, 0.f), Params);
		}
	}

	// The first player stands at the old starting point; the next ones on a 2 m ring round it, 60 degrees apart
	FVector2D At(0.f, 0.f);
	if (SpawnCount > 0)
	{
		const float A = FMath::DegreesToRadians(60.f * (SpawnCount - 1));
		At = FVector2D(FMath::Cos(A), FMath::Sin(A)) * 200.f;
	}
	SpawnCount = (SpawnCount + 1) % 7;
	const FVector Loc(At.X, At.Y, JeebTerrain::HeightAt(At.X, At.Y) + 130.f);
	return GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), Loc, FRotator::ZeroRotator, Params);
}
