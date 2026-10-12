#pragma once

#include "CoreMinimal.h"

// Player accounts, kept by the SERVER on its own disk (never on players' machines):
//   Saved/Accounts/<key>.acct     the account: display name, and the password as a salted PBKDF2-SHA256 hash (never the password)
//   Saved/Characters/<key>.sav    the character (UJeebSave)
//   Saved/Characters/<key>.online "this account is logged in on world <port>": stops the same account being on two worlds at
//                                 once (both worlds on one PC share these folders). Refreshed every minute; ignored once stale.
// The key is the name in lower case with spaces as underscores, so "Sir Jeeb" and "sir jeeb" are the same account.
// Later this moves behind an account service with a database; the rest of the game only uses the functions below.
namespace JeebAccounts
{
	// Names: 3 to 12 letters, digits and single spaces (not at either end). Returns an empty string if fine, otherwise why not.
	FString CheckName(const FString& Name);
	FString CheckPassword(const FString& Password);   // 6 to 64 characters
	FString KeyOf(const FString& Name);

	bool Exists(const FString& Name);
	// Makes a new account. False (OutError says why) if the name is taken or not allowed.
	bool Create(const FString& Name, const FString& Password, FString& OutError);
	// True if the account exists and the password matches; OutDisplayName is the name as it was first written
	bool Verify(const FString& Name, const FString& Password, FString& OutDisplayName);

	FString CharacterPath(const FString& Key);

	// The "logged in" marks. Claim fails if another world has a fresh mark for this account.
	bool ClaimOnline(const FString& Key, int32 WorldPort);
	void RefreshOnline(const FString& Key, int32 WorldPort);
	void ReleaseOnline(const FString& Key, int32 WorldPort);
}
