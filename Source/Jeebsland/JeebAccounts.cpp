#include "JeebAccounts.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "Containers/StringConv.h"

// OpenSSL does the password hashing (PBKDF2 with SHA-256) and makes the random salt
#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
THIRD_PARTY_INCLUDES_END
#undef UI

namespace
{
	constexpr int32 Iterations = 100000;   // slow on purpose: guessing passwords from a stolen file takes far longer
	constexpr int32 SaltBytes = 16;
	constexpr int32 HashBytes = 32;
	constexpr int64 StaleSeconds = 150;    // an "online" mark not refreshed for this long belongs to a world that stopped

	FString AccountDir() { return FPaths::ProjectSavedDir() / TEXT("Accounts"); }
	FString CharacterDir() { return FPaths::ProjectSavedDir() / TEXT("Characters"); }
	FString AccountPath(const FString& Key) { return AccountDir() / (Key + TEXT(".acct")); }
	FString OnlinePath(const FString& Key) { return CharacterDir() / (Key + TEXT(".online")); }

	bool Hash(const FString& Password, const TArray<uint8>& Salt, int32 Iter, TArray<uint8>& Out)
	{
		const FTCHARToUTF8 Pw(*Password);
		Out.SetNumZeroed(HashBytes);
		return PKCS5_PBKDF2_HMAC(Pw.Get(), Pw.Length(), Salt.GetData(), Salt.Num(), Iter, EVP_sha256(), HashBytes, Out.GetData()) == 1;
	}

	struct FAccount { FString Name; TArray<uint8> Salt; TArray<uint8> Hash; int32 Iter = 0; };

	bool Load(const FString& Key, FAccount& Out)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *AccountPath(Key))) return false;
		for (const FString& L : Lines)
		{
			FString K, V;
			if (!L.Split(TEXT("="), &K, &V)) continue;
			if (K == TEXT("name")) Out.Name = V;
			else if (K == TEXT("salt")) { Out.Salt.SetNumZeroed(V.Len() / 2); HexToBytes(V, Out.Salt.GetData()); }
			else if (K == TEXT("hash")) { Out.Hash.SetNumZeroed(V.Len() / 2); HexToBytes(V, Out.Hash.GetData()); }
			else if (K == TEXT("iter")) Out.Iter = FCString::Atoi(*V);
		}
		return Out.Salt.Num() == SaltBytes && Out.Hash.Num() == HashBytes && Out.Iter > 0;
	}

	int64 Now() { return FDateTime::UtcNow().ToUnixTimestamp(); }
}

FString JeebAccounts::CheckName(const FString& Name)
{
	if (Name.Len() < 3 || Name.Len() > 12) return TEXT("Names are 3 to 12 characters long.");
	if (Name.StartsWith(TEXT(" ")) || Name.EndsWith(TEXT(" ")) || Name.Contains(TEXT("  "))) return TEXT("Names can't start or end with a space, or have two in a row.");
	for (TCHAR C : Name)
	{
		const bool bOk = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == ' ';
		if (!bOk) return TEXT("Names use only letters, numbers and spaces.");
	}
	return FString();
}

FString JeebAccounts::CheckPassword(const FString& Password)
{
	if (Password.Len() < 6) return TEXT("Passwords are at least 6 characters.");
	if (Password.Len() > 64) return TEXT("Passwords are at most 64 characters.");
	return FString();
}

FString JeebAccounts::KeyOf(const FString& Name)
{
	return Name.ToLower().Replace(TEXT(" "), TEXT("_"));
}

bool JeebAccounts::Exists(const FString& Name)
{
	return CheckName(Name).IsEmpty() && IFileManager::Get().FileExists(*AccountPath(KeyOf(Name)));
}

bool JeebAccounts::Create(const FString& Name, const FString& Password, FString& OutError)
{
	OutError = CheckName(Name);
	if (OutError.IsEmpty()) OutError = CheckPassword(Password);
	if (!OutError.IsEmpty()) return false;
	const FString Key = KeyOf(Name);
	if (IFileManager::Get().FileExists(*AccountPath(Key))) { OutError = TEXT("That name is taken."); return false; }

	TArray<uint8> Salt, H;
	Salt.SetNumZeroed(SaltBytes);
	if (RAND_bytes(Salt.GetData(), SaltBytes) != 1 || !Hash(Password, Salt, Iterations, H)) { OutError = TEXT("The server could not make the account. Try again."); return false; }

	FString Text;
	Text += TEXT("version=1\n");
	Text += FString::Printf(TEXT("name=%s\n"), *Name);
	Text += FString::Printf(TEXT("created=%s\n"), *FDateTime::UtcNow().ToIso8601());
	Text += FString::Printf(TEXT("iter=%d\n"), Iterations);
	Text += FString::Printf(TEXT("salt=%s\n"), *BytesToHex(Salt.GetData(), Salt.Num()));
	Text += FString::Printf(TEXT("hash=%s\n"), *BytesToHex(H.GetData(), H.Num()));
	IFileManager::Get().MakeDirectory(*AccountDir(), true);
	if (!FFileHelper::SaveStringToFile(Text, *AccountPath(Key))) { OutError = TEXT("The server could not save the account. Try again."); return false; }
	return true;
}

bool JeebAccounts::Verify(const FString& Name, const FString& Password, FString& OutDisplayName)
{
	if (!CheckName(Name).IsEmpty() || Password.IsEmpty() || Password.Len() > 64) return false;
	FAccount A;
	if (!Load(KeyOf(Name), A)) return false;
	TArray<uint8> H;
	if (!Hash(Password, A.Salt, A.Iter, H)) return false;
	if (CRYPTO_memcmp(H.GetData(), A.Hash.GetData(), HashBytes) != 0) return false;   // (compares in constant time)
	OutDisplayName = A.Name.IsEmpty() ? Name : A.Name;
	return true;
}

FString JeebAccounts::CharacterPath(const FString& Key)
{
	IFileManager::Get().MakeDirectory(*CharacterDir(), true);
	return CharacterDir() / (Key + TEXT(".sav"));
}

bool JeebAccounts::ClaimOnline(const FString& Key, int32 WorldPort)
{
	FString Old;
	if (FFileHelper::LoadFileToString(Old, *OnlinePath(Key)))
	{
		FString PortS, TimeS;
		if (Old.TrimStartAndEnd().Split(TEXT(" "), &PortS, &TimeS))
		{
			const int64 When = FCString::Atoi64(*TimeS);
			if (FCString::Atoi(*PortS) != WorldPort && Now() - When < StaleSeconds) return false;   // on another world right now
		}
	}
	RefreshOnline(Key, WorldPort);
	return true;
}

void JeebAccounts::RefreshOnline(const FString& Key, int32 WorldPort)
{
	IFileManager::Get().MakeDirectory(*CharacterDir(), true);
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("%d %lld"), WorldPort, Now()), *OnlinePath(Key));
}

void JeebAccounts::ReleaseOnline(const FString& Key, int32 WorldPort)
{
	FString Old;
	if (FFileHelper::LoadFileToString(Old, *OnlinePath(Key)) && FCString::Atoi(*Old) != WorldPort) return;   // (another world's mark)
	IFileManager::Get().Delete(*OnlinePath(Key), false, true, true);
}
