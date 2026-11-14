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

	// Chat: the player's machine asks (ServerChat); the server cleans, filters and relays the message to everyone who should hear
	// it (players within JeebChat::NearbyRange, or the whole world for bWorld); each machine shows it (AKnightHUD::AddChatLine).
	// An empty From is a note from the game (shown only to that player).
	UFUNCTION(Server, Reliable) void ServerChat(const FString& Text, bool bWorld);
	UFUNCTION(Client, Reliable) void ClientChat(const FString& From, const FString& Text, bool bWorld, AActor* Speaker);
	double LastChatTime = -10.0;

	// Server side
	bool bLoggedIn = false;
	FString AccountKey;            // which account (file name) this player is
	UPROPERTY() UJeebSave* PendingSave = nullptr;   // the character being loaded into the knight about to appear
	double ConnectTime = 0.0;
	int32 LoginTries = 0;

	// This machine: waiting for the server to say yes
	bool IsLoggingIn() const { return bSentLogin && !bLoginDone; }

	// TEMPORARY test kit (owner, 2026-11-13; JeebTestKit.h): the server sets bTestKit for testers and only its owner sees it; the
	// Tab screen then shows the test items and the set-every-skill box. Every request is checked again on the server.
	UPROPERTY(Replicated) bool bTestKit = false;
	UFUNCTION(Server, Reliable) void ServerTestGive(int32 Id, bool bWear);   // a full stack into the pack; bWear also wears or holds it
	UFUNCTION(Server, Reliable) void ServerTestLevels(int32 Level);          // every skill to this level (1 to 99)
	UFUNCTION(Server, Reliable) void ServerTestEmptyPack();                  // clears the 24 pack slots (not the ingots, worn gear or the bar)
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	bool bSentLogin = false;
	bool bLoginDone = false;
	bool bMenuSetup = false;
	UPROPERTY() ACameraActor* MenuCam = nullptr;
	float MenuClock = 0.f;
	void SendLogin();
	double LastTestAsk = -10.0, LastTestGate = -10.0;   // (server: the test kit's rate limit, and when the gate was last read)
	bool TestGate();                                    // (server: may this player use the test kit? also refreshes bTestKit)
};
