#pragma once

#include "CoreMinimal.h"

// Player chat helpers. The SERVER cleans, filters and relays every message (AKnightPlayerController::ServerChat); each player's
// machine keeps its own mute list and hides what the muted players say.
//   Saved/Moderation.txt   (on the server, edited by the owner; re-read every few seconds, no restart needed)
//       mute <account name>    that account can still play but its messages are dropped
//       ban <account name>     that account cannot log in (and is shown the door if it is online when the line is added: next login)
//       # a comment
//   Saved/ChatMuted.txt    (on a player's machine) the names that player muted with /mute
namespace JeebChat
{
	constexpr int32 MaxLength = 120;        // characters in one message
	constexpr float NearbyRange = 3500.f;   // 35 m: who hears an ordinary message
	constexpr double MinGap = 0.8;          // seconds between one player's messages

	// Trims, drops control characters, squeezes repeated spaces and cuts the message to MaxLength
	FString Clean(const FString& In);
	// Swear filter: bad words (and the usual disguises, like sh1t or f u c k typed with digits) become asterisks
	FString Censor(const FString& In);

	// The owner's moderation file
	bool IsMuted(const FString& AccountKey);
	bool IsBanned(const FString& AccountKey);

	// This player's own mute list
	bool LocalMuted(const FString& Name);
	void SetLocalMuted(const FString& Name, bool bMuted);
	TArray<FString> LocalMuteList();
}
