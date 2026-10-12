#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "JeebGameInstance.generated.h"

// One world server players can pick on the starting screen. Listed in Config/DefaultGame.ini.
USTRUCT()
struct FJeebWorld
{
	GENERATED_BODY()
	UPROPERTY(Config) FString Name;
	UPROPERTY(Config) int32 Port = 7777;          // the game port; the world answers "how many players?" on Port + StatusPortOffset
	UPROPERTY(Config) int32 MaxPlayers = 200;     // logins are refused once this many are on (friends-over-the-limit comes later)
};

// What the starting screen knows about a world right now
struct FJeebWorldStatus
{
	bool bUp = false;          // answered recently
	bool bAsked = false;       // a question is on its way
	int32 Players = 0;
	int32 Max = 0;
	double LastAsk = -100.0;
	double LastReply = -100.0;
};

// Lives for the whole run of the game (it survives changing maps and joining or leaving a world). Holds the world list, the
// name and password typed on the starting screen (in memory only: the password is never written anywhere on this machine),
// and the message to show when we are sent back to the starting screen.
UCLASS(Config = Game)
class UJeebGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	static constexpr int32 StatusPortOffset = 100;

	UPROPERTY(Config) TArray<FJeebWorld> Worlds;
	TArray<FJeebWorldStatus> Status;

	// The starting screen is shown in a world of our own (not connected to a server). Single player in the editor, the
	// -JeebOffline switch and the -JeebShot screenshot mode skip it and play straight away, offline.
	static bool InMenu(const UObject* WorldContext);

	// Typed on the starting screen
	FString ServerAddress = TEXT("127.0.0.1");
	FString LoginName;
	FString LoginPassword;
	bool bCreateAccount = false;
	int32 ChosenWorld = 0;
	bool bJoining = false;      // on our way to a world (until logged in or sent back)
	FString MenuMessage;        // why we are back on the starting screen ("Wrong name or password.")

	// Asks each world how many players it has (every few seconds while the starting screen is up)
	void PollWorlds();
	// Goes to the chosen world; the player controller logs in once connected (AKnightPlayerController)
	void JoinWorld(int32 Index);
	// Back to the starting screen (leaving the world), showing Why
	void ReturnToMenu(const FString& Why);
	void RememberChoices();   // the server address and name (never the password) for next time
	FString WorldName(int32 Index) const { return Worlds.IsValidIndex(Index) ? Worlds[Index].Name : FString(TEXT("the world")); }

private:
	void OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
};
