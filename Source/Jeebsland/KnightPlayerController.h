#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "KnightPlayerController.generated.h"

class UJeebSave;
class ACameraActor;

// One per player. Online it carries the login: once connected, the player's machine sends the name and password it was given on
// the starting screen (ServerLogin); the server checks them (AKnightGameMode::TryLogin) and only then gives the player a knight,
// loaded from their saved character. On the starting screen (no server) it flies a slow camera round Blackrock.
UCLASS()
class AKnightPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AKnightPlayerController();
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PawnLeavingGame() override;   // a player leaving: their character is saved before the knight goes

	UFUNCTION(Server, Reliable) void ServerLogin(const FString& Name, const FString& Password, bool bCreate, bool bDev);
	UFUNCTION(Client, Reliable) void ClientLoginResult(bool bOk, const FString& Message);

	// Server side
	bool bLoggedIn = false;
	FString AccountKey;            // which account (file name) this player is
	UPROPERTY() UJeebSave* PendingSave = nullptr;   // the character being loaded into the knight about to appear
	double ConnectTime = 0.0;
	int32 LoginTries = 0;

	// This machine: waiting for the server to say yes
	bool IsLoggingIn() const { return bSentLogin && !bLoginDone; }

private:
	bool bSentLogin = false;
	bool bLoginDone = false;
	bool bMenuSetup = false;
	UPROPERTY() ACameraActor* MenuCam = nullptr;
	float MenuClock = 0.f;
	void SendLogin();
};
