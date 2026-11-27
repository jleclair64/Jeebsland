// The game-wide Market and which town sells what (2026-11-26, Docs/EconomyPlan.md). Server only, except JeebShops and the category helpers.
// Every world server on the host shares Saved/Market/:
//   Live/<id>.json        one file per listing (written to a side file, then moved in)
//   Done/<id>.json        a listing that was bought or cancelled: moved there by whoever claimed it (MoveFileW without replace: exactly one
//                         mover wins, even when two world servers race for the same listing)
//   Mail/<key>/<id>.json  ingots or an item waiting for an account (a sale's pay, a cancelled listing that did not fit); delivered by the world
//                         the account plays on, and its id kept in the character (MailDone) before the file goes, so nothing is paid twice
//   Log/market_<port>.log what happened (each world its own file)
#include "JeebEconomy.h"
#include "KnightCharacter.h"
#include "Inventory.h"
#include "JeebProps.h"
#include "WorldMap.h"
#include "Terrain.h"
#include "OakHill.h"
#include "SteeleCreek.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

using namespace JeebItems;

// ------------------------------------------------------------------------------------------------ shops by town
namespace JeebShops
{
	ETown TownAt(float X, float Y)
	{
		struct FC { ETown T; FVector2D P; };
		TArray<FC> C;
		C.Add({ T_Blackrock, JeebTerrain::VillageCenter() });
		for (const JeebWorld::FTown& Tw : JeebWorld::Towns())
		{
			const FString N = Tw.Name;
			const ETown T = N == TEXT("Oak Hill") ? T_OakHill : N == TEXT("Silver Point") ? T_SilverPoint : N == TEXT("Steele Creek") ? T_SteeleCreek : T_Other;
			C.Add({ T, T == T_OakHill ? FVector2D(JeebOakHill::PosX, JeebOakHill::PosY) : T == T_SteeleCreek ? FVector2D(JeebSteele::PosX, JeebSteele::PosY) : Tw.Pos });
		}
		ETown Best = T_Other; double BestD = 1e30;
		for (const FC& E : C) { const double D = FVector2D::DistSquared(E.P, FVector2D(X, Y)); if (D < BestD) { BestD = D; Best = E.T; } }
		return Best;
	}
	const TCHAR* TownName(ETown T)
	{
		switch (T) { case T_OakHill: return TEXT("Oak Hill"); case T_Blackrock: return TEXT("Blackrock"); case T_SilverPoint: return TEXT("Silver Point"); case T_SteeleCreek: return TEXT("Steele Creek"); default: return TEXT("the wilds"); }
	}
	int32 ShopClass(int32 Id)
	{
		if (Id == ArrowId(A_Wooden)) return 0;
		return ItemClass(Id);
	}
	bool Sells(ETown T, int32 Id)
	{
		const int32 C = ShopClass(Id);
		if (C == 0) return true;
		if (C == 1) return T == T_OakHill || T == T_Blackrock;
		if (C == 3) return T == T_SilverPoint;
		if (C == 2) return T == T_SteeleCreek;
		return false;
	}
}

// ------------------------------------------------------------------------------------------------ the market
namespace JeebMarket
{
	const TCHAR* CategoryName(int32 C)
	{
		static const TCHAR* N[C_Num] = { TEXT("All"), TEXT("Weapons"), TEXT("Armour"), TEXT("Materials"), TEXT("Fish"), TEXT("Food & potions"), TEXT("Arrows & rocks"), TEXT("Wizard"), TEXT("Troll"), TEXT("Other") };
		return (C >= 0 && C < C_Num) ? N[C] : TEXT("?");
	}
	namespace
	{
		bool IsFood(int32 Id) { if (IsPotion(Id)) return true; if (!IsMaterial(Id)) return false; const EItemType T = MaterialType(Id); return T == EItemType::Bacon || T == EItemType::Venison || T == EItemType::GameMeat; }
		bool IsWeaponId(int32 Id) { return IsGear(Id) && IsWeaponKind(KindOf(Id)); }
		bool IsArmourId(int32 Id) { return IsGear(Id) && (IsArmorKind(KindOf(Id)) || KindOf(Id) == K_Shield); }
		bool IsMat(int32 Id) { return IsMaterial(Id) && !JeebProps::IsFishItem(Id) && !IsFood(Id); }
	}
	bool InCategory(int32 Id, int32 C)
	{
		switch (C)
		{
		case C_All: return true;
		case C_Weapons: return IsWeaponId(Id);
		case C_Armour: return IsArmourId(Id);
		case C_Materials: return IsMat(Id);
		case C_Fish: return JeebProps::IsFishItem(Id);
		case C_Food: return IsFood(Id);
		case C_Ammo: return IsArrow(Id) || IsRock(Id);
		case C_Wizard: return JeebShops::ShopClass(Id) == 3;
		case C_Troll: return JeebShops::ShopClass(Id) == 2;
		case C_Other: return !IsWeaponId(Id) && !IsArmourId(Id) && !IsMat(Id) && !JeebProps::IsFishItem(Id) && !IsFood(Id) && !IsArrow(Id) && !IsRock(Id);
		default: return false;
		}
	}

	FString Dir() { return FPaths::ProjectSavedDir() / TEXT("Market"); }
	namespace
	{
		FString LiveDir() { return Dir() / TEXT("Live"); }
		FString DoneDir() { return Dir() / TEXT("Done"); }
		FString MailDir(const FString& Key) { return Dir() / TEXT("Mail") / Key; }
		int32 PortOf(UWorld* W) { return (W && W->GetNetMode() != NM_Standalone) ? W->URL.Port : 0; }
		bool SafeId(const FString& Id) { if (Id.IsEmpty() || Id.Len() > 80) return false; for (TCHAR C : Id) if (!FChar::IsAlnum(C) && C != '_' && C != '-') return false; return true; }
		FString NewId(int32 Port) { return FString::Printf(TEXT("%lld_%d_%s"), FDateTime::UtcNow().ToUnixTimestamp(), Port, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12)); }
		int32 LastPort = 0;

		bool WriteJson(const FString& Path, const TSharedRef<FJsonObject>& O)
		{
			FString Text;
			FJsonSerializer::Serialize(O, TJsonWriterFactory<>::Create(&Text));
			const FString Tmp = Path + TEXT(".tmp");
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			if (!FFileHelper::SaveStringToFile(Text, *Tmp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
			return IFileManager::Get().Move(*Path, *Tmp, true, true, false, true);
		}
		TSharedPtr<FJsonObject> ReadJson(const FString& Path)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Path, FFileHelper::EHashOptions::None, FILEREAD_Silent)) return nullptr;
			TSharedPtr<FJsonObject> O;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O)) return nullptr;
			return O;
		}
		bool FromJson(const TSharedPtr<FJsonObject>& O, FListing& L)
		{
			if (!O) return false;
			L.Id = O->GetStringField(TEXT("id"));
			L.SellerKey = O->GetStringField(TEXT("seller"));
			L.SellerName = O->GetStringField(TEXT("name"));
			L.ItemId = int32(O->GetNumberField(TEXT("item")));
			L.Count = int32(O->GetNumberField(TEXT("count")));
			L.Size = int32(O->GetNumberField(TEXT("size")));
			L.Price = int32(O->GetNumberField(TEXT("price")));
			L.Time = int64(O->GetNumberField(TEXT("t")));
			return SafeId(L.Id) && L.ItemId != 0 && L.Count > 0 && L.Price > 0;
		}
		TArray<FListing> Cache;
		double CacheAt = -100.0;
	}

	const TArray<FListing>& All(bool bFresh)
	{
		const double Now = FPlatformTime::Seconds();
		if (!bFresh && Now - CacheAt < 2.0) return Cache;
		CacheAt = Now;
		Cache.Reset();
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(LiveDir() / TEXT("*.json")), true, false);
		for (const FString& F : Files)
		{
			FListing L;
			if (FromJson(ReadJson(LiveDir() / F), L) && FPaths::GetBaseFilename(F) == L.Id) Cache.Add(L);
		}
		return Cache;
	}

	int32 CountFor(const FString& SellerKey)
	{
		int32 N = 0;
		for (const FListing& L : All(true)) if (L.SellerKey == SellerKey) ++N;
		return N;
	}

	bool Add(UWorld* World, const FString& SellerKey, const FString& SellerName, const FInvSlot& Item, int32 Price, FString& OutId)
	{
		LastPort = PortOf(World);
		OutId = NewId(LastPort);
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), OutId);
		O->SetStringField(TEXT("seller"), SellerKey);
		O->SetStringField(TEXT("name"), SellerName);
		O->SetNumberField(TEXT("item"), Item.Id);
		O->SetNumberField(TEXT("count"), Item.Count);
		O->SetNumberField(TEXT("size"), Item.Size);
		O->SetNumberField(TEXT("price"), Price);
		O->SetNumberField(TEXT("t"), double(FDateTime::UtcNow().ToUnixTimestamp()));
		const bool bOk = WriteJson(LiveDir() / (OutId + TEXT(".json")), O);
		CacheAt = -100.0;
		return bOk;
	}

	bool Read(const FString& Id, FListing& Out)
	{
		if (!SafeId(Id)) return false;
		return FromJson(ReadJson(LiveDir() / (Id + TEXT(".json"))), Out) && Out.Id == Id;
	}

	bool Claim(const FString& Id, const TCHAR* Why)
	{
		if (!SafeId(Id)) return false;
		const FString Src = LiveDir() / (Id + TEXT(".json"));
		const FString Dst = DoneDir() / FString::Printf(TEXT("%s.%s.json"), *Id, Why);
		IFileManager::Get().MakeDirectory(*DoneDir(), true);
		// (no replace, no retry: if another server moved it first, the source is gone and this fails at once)
		const bool bOk = IFileManager::Get().Move(*Dst, *Src, false, true, false, true);
		CacheAt = -100.0;
		return bOk;
	}

	bool SendMail(const FString& ToKey, int32 Ingots, const FInvSlot& Item, const FString& Note)
	{
		if (ToKey.IsEmpty() || (Ingots <= 0 && Item.Id == 0)) return false;
		const FString Id = NewId(LastPort);
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), Id);
		O->SetNumberField(TEXT("ingots"), FMath::Max(0, Ingots));
		O->SetNumberField(TEXT("item"), Item.Id);
		O->SetNumberField(TEXT("count"), Item.Count);
		O->SetNumberField(TEXT("size"), Item.Size);
		O->SetStringField(TEXT("note"), Note);
		const bool bOk = WriteJson(MailDir(ToKey) / (Id + TEXT(".json")), O);
		if (!bOk) UE_LOG(LogTemp, Error, TEXT("JEEBMARKET could not write mail %s for %s (%d ingots, item %d x%d)"), *Id, *ToKey, Ingots, Item.Id, Item.Count);
		return bOk;
	}

	bool DeliverMail(AKnightCharacter* Knight, const FString& Key)
	{
		USkillComponent* Sk = Knight ? Knight->GetSkills() : nullptr;
		if (!Sk || Key.IsEmpty() || !SafeId(Key.Replace(TEXT("."), TEXT("_")))) return false;
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(MailDir(Key) / TEXT("*.json")), true, false);
		if (Files.Num() == 0) return false;
		Files.Sort();
		bool bAny = false;
		for (const FString& F : Files)
		{
			const FString Path = MailDir(Key) / F;
			const FString Id = FPaths::GetBaseFilename(F);
			if (Sk->MailDone.Contains(Id)) { IFileManager::Get().Delete(*Path, false, true, true); continue; }   // (paid already: the file outlived a crash)
			const TSharedPtr<FJsonObject> O = ReadJson(Path);
			if (!O) continue;
			const int32 Ingots = FMath::Clamp(int32(O->GetNumberField(TEXT("ingots"))), 0, MaxPrice);
			const int32 ItemId = int32(O->GetNumberField(TEXT("item")));
			const int32 Count = int32(O->GetNumberField(TEXT("count")));
			const int32 Size = int32(O->GetNumberField(TEXT("size")));
			const FString Note = O->GetStringField(TEXT("note"));
			if (ItemId != 0 && Count > 0)
			{
				// to the bank, else the pack; neither has room: it waits (tried again every few seconds)
				TArray<FInvSlot> Bank = Sk->Bank, Pack = Sk->Slots;
				if (USkillComponent::PutIntoArr(Bank, ItemId, Count, Size) == 0) Sk->Bank = Bank;
				else if (USkillComponent::PutIntoArr(Pack, ItemId, Count, Size) == 0) Sk->Slots = Pack;
				else
				{
					if (!Sk->bMailFullSaid) { Sk->Say(8.f, FColor::Orange, TEXT("Market: something is waiting for you, but your bank and pack are full.")); Sk->bMailFullSaid = true; }
					continue;
				}
			}
			Sk->AddIngots(Ingots);
			Sk->MailDone.Add(Id);
			if (Sk->MailDone.Num() > 200) Sk->MailDone.RemoveAt(0, Sk->MailDone.Num() - 200);
			IFileManager::Get().Delete(*Path, false, true, true);
			if (!Note.IsEmpty()) Sk->Say(10.f, FColor(120, 230, 120), Note);
			Log(Knight->GetWorld(), FString::Printf(TEXT("MAIL %s %s: %d ingots, item %d x%d size %d (%s)"), *Key, *Id, Ingots, ItemId, Count, Size, *Note));
			bAny = true;
		}
		if (bAny) Sk->bMailFullSaid = false;
		return bAny;
	}

	void Log(UWorld* World, const FString& Line)
	{
		const int32 Port = World ? PortOf(World) : LastPort;
		const FString Path = Dir() / TEXT("Log") / FString::Printf(TEXT("market_%d.log"), Port);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S ")) + Line + LINE_TERMINATOR, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
		UE_LOG(LogTemp, Log, TEXT("JEEBMARKET %s"), *Line);
	}

	void Query(const FString& Key, int32 Category, const FString& Search, int32 Page, bool bMine, TArray<FMarketRow>& Out, int32& OutTotal)
	{
		Out.Reset();
		TArray<const FListing*> Hits;
		const FString S = Search.TrimStartAndEnd();
		for (const FListing& L : All())
		{
			if (bMine ? L.SellerKey != Key : !InCategory(L.ItemId, Category)) continue;
			if (!bMine && !S.IsEmpty() && !ItemName(L.ItemId).Contains(S)) continue;
			Hits.Add(&L);
		}
		// cheapest each first (the price of one), then the oldest
		Hits.Sort([](const FListing& A, const FListing& B) { const double PA = double(A.Price) / FMath::Max(1, A.Count), PB = double(B.Price) / FMath::Max(1, B.Count); return PA != PB ? PA < PB : A.Time < B.Time; });
		OutTotal = Hits.Num();
		const int32 From = FMath::Max(0, Page) * PageRows;
		for (int32 i = From; i < Hits.Num() && Out.Num() < PageRows; ++i)
		{
			const FListing& L = *Hits[i];
			FMarketRow R;
			R.Id = L.Id; R.ItemId = L.ItemId; R.Count = L.Count; R.Size = L.Size; R.Price = L.Price; R.Seller = L.SellerName; R.bMine = L.SellerKey == Key;
			Out.Add(R);
		}
	}
}
