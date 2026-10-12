#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KnightGameMode.generated.h"

class AKnightPlayerController;

// The server's rules: plays as the knight, and decides where each player appears. Lives ONLY on the server (or in a
// single-player game); the world itself is built by AKnightGameState, which every machine has.
//
// Online, a player gets a knight only after logging in (TryLogin): the name and password are checked against the account files
// (JeebAccounts), their saved character is loaded into the knight, and it is saved again every minute, when they leave and when
// the world shuts down. On the starting screen (no server) nobody gets a knight at all.
UCLASS()
class AKnightGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKnightGameMode();

	void TryLogin(AKnightPlayerController* PC, const FString& Name, const FString& Password, bool bCreate, bool bDev);
	void SaveCharacter(AKnightPlayerController* PC);   // writes this player's character to the server's disk
	int32 PlayersOnline() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Logout(AController* Exiting) override;
	// Online the knight only comes after the login; on the starting screen it never comes
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	// Every player starts on the meadow by the cottage, spread round a small ring so two never land on each other; a saved
	// character appears where it logged out
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

private:
	int32 SpawnCount = 0;
	bool IsOnline() const;          // a real server (not single player, not the starting screen)
	int32 WorldPort() const;
	int32 MaxPlayers() const;
	void AutosaveAll();
	FTimerHandle AutosaveTimer;
	TSet<FString> OnlineKeys;       // accounts logged in on this world
	TMap<FString, TArray<double>> FailedTries;   // wrong passwords per account, to slow down guessing
	bool bStatusUp = false;
	void StartStatus();             // answers "how many players?" for the starting screen's world list
};
