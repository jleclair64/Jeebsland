#pragma once

#include "CoreMinimal.h"
#include "SkillComponent.h"
#include "JeebEconomy.generated.h"

class UWorld;
class AKnightCharacter;

// The economy (2026-11-26, Docs/EconomyPlan.md): player-to-player trade, the game-wide Market at every bank, and which town's shop sells what.
// The server decides everything; the client asks (AKnightCharacter::ServerTrade* / ServerMarket*) and draws what comes back.

// What one side of a trade shows (sent down to both players whenever the trade changes)
USTRUCT()
struct FTradeView
{
	GENERATED_BODY()
	UPROPERTY() bool bOpen = false;
	UPROPERTY() FString Partner;
	UPROPERTY() TArray<FInvSlot> Mine;     // what I offer (Id, Count, Size)
	UPROPERTY() TArray<int32> MineSlots;   // the pack slot each of mine comes from
	UPROPERTY() TArray<FInvSlot> Theirs;
	UPROPERTY() int32 MyIngots = 0;
	UPROPERTY() int32 TheirIngots = 0;
	UPROPERTY() uint8 Stage = 0;           // 0 offering (ACCEPT), 1 confirming (CONFIRM)
	UPROPERTY() bool bIAccepted = false;
	UPROPERTY() bool bTheyAccepted = false;
	UPROPERTY() int32 Version = 0;         // goes up with every change; an accept must name the version it saw
};

// One listing as the Market tab shows it
USTRUCT()
struct FMarketRow
{
	GENERATED_BODY()
	UPROPERTY() FString Id;
	UPROPERTY() int32 ItemId = 0;
	UPROPERTY() int32 Count = 0;
	UPROPERTY() int32 Size = 0;
	UPROPERTY() int32 Price = 0;
	UPROPERTY() FString Seller;
	UPROPERTY() bool bMine = false;
};

namespace JeebShops
{
	// Which town a shop stands in, by the nearest town centre (the old village site counts as Blackrock)
	enum ETown : int32 { T_OakHill, T_Blackrock, T_SilverPoint, T_SteeleCreek, T_Other };
	ETown TownAt(float X, float Y);
	const TCHAR* TownName(ETown T);
	// The class a shop treats an item as: JeebItems::ItemClass (0 everyone, 1 knight, 2 troll, 3 wizard), but the plain wooden arrow is everyone's
	int32 ShopClass(int32 Id);
	// May a shop in this town sell it? Everyone's goods everywhere; knight goods at Oak Hill and Blackrock, wizard goods at Silver Point, troll goods at Steele Creek
	bool Sells(ETown T, int32 Id);
}

namespace JeebMarket
{
	constexpr int32 MaxListings = 10;     // per account
	constexpr int32 TaxPercent = 5;       // kept by the market when an item sells
	constexpr int32 MaxPrice = 10000000;
	constexpr int32 PageRows = 12;
	inline int32 Tax(int32 Price) { return FMath::Max(0, Price) * TaxPercent / 100; }

	enum ECategory : int32 { C_All, C_Weapons, C_Armour, C_Materials, C_Fish, C_Food, C_Ammo, C_Wizard, C_Troll, C_Other, C_Num };
	const TCHAR* CategoryName(int32 C);
	bool InCategory(int32 Id, int32 C);

	struct FListing { FString Id, SellerKey, SellerName; int32 ItemId = 0, Count = 0, Size = 0, Price = 0; int64 Time = 0; };

	// (server) The listings now on the market (every world's), re-read from disk at most every 2 s unless bFresh
	const TArray<FListing>& All(bool bFresh = false);
	int32 CountFor(const FString& SellerKey);
	// (server) Writes a new listing; false if it could not be written
	bool Add(UWorld* World, const FString& SellerKey, const FString& SellerName, const FInvSlot& Item, int32 Price, FString& OutId);
	// (server) Reads one listing straight from disk (false: gone)
	bool Read(const FString& Id, FListing& Out);
	// (server) Takes a listing off the market for good: true for exactly one caller, even across world servers (the file is moved, never copied)
	bool Claim(const FString& Id, const TCHAR* Why);
	// (server) Sends ingots and/or an item to an account, online or not, on any world: delivered by DeliverMail on the world they play on
	bool SendMail(const FString& ToKey, int32 Ingots, const FInvSlot& Item, const FString& Note);
	// (server) Delivers this player's waiting mail: ingots to the account, items to the bank (else the pack). Saves nothing itself; returns true if anything came
	bool DeliverMail(AKnightCharacter* Knight, const FString& Key);
	// (server) One line in Saved/Market/Log/market_<port>.log
	void Log(UWorld* World, const FString& Line);
	// (server) What the Market tab shows: category, search text, page; bMine = only this account's own listings
	void Query(const FString& Key, int32 Category, const FString& Search, int32 Page, bool bMine, TArray<FMarketRow>& Out, int32& OutTotal);
	// (test) the folder, so tests can use their own
	FString Dir();
}
