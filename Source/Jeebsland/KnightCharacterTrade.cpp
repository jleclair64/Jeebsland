// Player-to-player trade and the Market's server side (2026-11-26, Docs/EconomyPlan.md). The server decides everything here; the player's machine
// only asks (the Server RPCs) and draws the views it is sent (ClientTradeView, ClientMarketRows: KnightHUDTrade.cpp).
#include "KnightCharacter.h"
#include "KnightPlayerController.h"
#include "KnightGameMode.h"
#include "KnightHUD.h"
#include "JeebEconomy.h"
#include "JeebRecords.h"
#include "JeebSave.h"
#include "JeebProps.h"
#include "Npc.h"
#include "Inventory.h"

#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace JeebItems;

namespace
{
	constexpr float TradeAskReach = 300.f;     // E: a player in front of you this close
	constexpr float TradeAskSlack = 150.f;     // (the server's check allows for lag)
	constexpr float TradeKeepReach = 800.f;    // walking further apart ends the trade
	constexpr float TradeSwapReach = 600.f;    // the swap itself needs you this close
	constexpr int32 TradeMaxItems = 12;
	constexpr double TradeAskLife = 30.0;      // seconds a request stays open

	FString NameOf(const AKnightCharacter* K)
	{
		const APlayerState* PS = K ? K->GetPlayerState() : nullptr;
		return PS && !PS->GetPlayerName().IsEmpty() ? PS->GetPlayerName() : FString(TEXT("Someone"));
	}
	FString KeyOf(AKnightCharacter* K) { FString Key, Name; JeebRecords::WhoIs(K, Key, Name); return Key; }
	bool IsPlayer(const AKnightCharacter* K) { return K && Cast<APlayerController>(K->GetController()) != nullptr; }
	FString ItemsText(const TArray<FInvSlot>& Items, int32 Ingots)
	{
		TArray<FString> P;
		for (const FInvSlot& S : Items)
			P.Add(FString::Printf(TEXT("%s x%d%s"), *ItemName(S.Id), S.Count, S.IsSized() ? *FString::Printf(TEXT(" (%s)"), *JeebProps::FishSizeText(S.Id, S.Size)) : TEXT("")));
		if (Ingots > 0) P.Add(FString::Printf(TEXT("%d ingots"), Ingots));
		return P.Num() ? FString::Join(P, TEXT(", ")) : FString(TEXT("nothing"));
	}
	void TradeLog(UWorld* W, const FString& Line)
	{
		const int32 Port = (W && W->GetNetMode() != NM_Standalone) ? W->URL.Port : 0;
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Trades") / FString::Printf(TEXT("trades_%d.log"), Port);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S ")) + Line + LINE_TERMINATOR, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
		UE_LOG(LogTemp, Log, TEXT("JEEBTRADE %s"), *Line);
	}
	bool Alive(const AKnightCharacter* K) { return K && IsValid(K) && K->GetHealth() > 0.f; }
}

// ------------------------------------------------------------------------------------------------ this machine
AKnightCharacter* AKnightCharacter::TradeTargetInReach() const
{
	if (GetNetMode() == NM_Standalone) return nullptr;
	const FVector Me = GetActorLocation();
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();
	AKnightCharacter* Best = nullptr;
	float BestD = TradeAskReach;
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
	{
		AKnightCharacter* K = *It;
		if (K == this || !K->GetPlayerState()) continue;
		const FVector To = K->GetActorLocation() - Me;
		const float D = To.Size2D();
		if (D < BestD && FMath::Abs(To.Z) < 200.f && FVector::DotProduct(Fwd, To.GetSafeNormal2D()) > 0.5f) { BestD = D; Best = K; }
	}
	return Best;
}

void AKnightCharacter::AskTrade(AKnightCharacter* Other)
{
	if (Other && Other != this) ServerTradeAsk(Other);
}

void AKnightCharacter::ClientTradeView_Implementation(const FTradeView& V)
{
	const bool bWas = TradeView.bOpen;
	TradeView = V;
	if (V.bOpen && !bWas)
	{
		// the trade window takes the screen: every other window closes
		CloseDialogue();
		if (bShopOpen) CloseShop();
		if (bSmithOpen) CloseSmith();
		if (bBankOpen) CloseBank();
		bInventoryOpen = bSkillsOpen = false;
		GetCharacterMovement()->StopMovementImmediately();
	}
	if (V.bOpen != bWas) RefreshUIMode();
}

// ------------------------------------------------------------------------------------------------ the server: trade
bool AKnightCharacter::TradeRate()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastTradeOp < 0.1) return false;
	LastTradeOp = Now;
	return true;
}

void AKnightCharacter::ServerTradeAsk_Implementation(AKnightCharacter* Other)
{
	if (!Skills || !TradeRate()) return;
	const double Now = FPlatformTime::Seconds();
	if (!IsPlayer(this) || !IsPlayer(Other) || Other == this || !Alive(this) || !Alive(Other)) return;
	if (TradeWith.IsValid()) { Skills->Say(3.f, FColor::Orange, TEXT("You are already trading.")); return; }
	if (Other->TradeWith.IsValid()) { Skills->Say(3.f, FColor::Orange, FString::Printf(TEXT("%s is busy trading."), *NameOf(Other))); return; }
	if (FVector::Dist2D(GetActorLocation(), Other->GetActorLocation()) > TradeAskReach + TradeAskSlack) { Skills->Say(3.f, FColor::Orange, TEXT("Stand closer to trade.")); return; }
	if (Other->TradeAskTo.Get() == this && Now - Other->TradeAskAt < TradeAskLife)
	{
		// both asked: the trade opens for both
		for (AKnightCharacter* K : { this, Other })
		{
			K->TradeAskTo = nullptr;
			K->TradeSlots.Reset(); K->TradeItems.Reset(); K->TradeIngots = 0;
			K->bTradeAccepted = false; K->TradeStage = 0; K->TradeVersion = 1;
		}
		TradeWith = Other;
		Other->TradeWith = this;
		TradeLog(GetWorld(), FString::Printf(TEXT("OPEN %s (%s) with %s (%s)"), *NameOf(this), *KeyOf(this), *NameOf(Other), *KeyOf(Other)));
		TradeSend();
		return;
	}
	TradeAskTo = Other;
	TradeAskAt = Now;
	Skills->Say(4.f, FColor(200, 220, 255), FString::Printf(TEXT("You ask %s to trade."), *NameOf(Other)));
	if (Other->Skills) Other->Skills->Say(8.f, FColor(200, 220, 255), FString::Printf(TEXT("%s wants to trade. Face them and press E to trade."), *NameOf(this)));
	UE_LOG(LogTemp, Log, TEXT("JEEBTRADE ASK %s -> %s"), *NameOf(this), *NameOf(Other));
}

void AKnightCharacter::TradeSend()
{
	AKnightCharacter* P = TradeWith.Get();
	if (!P) return;
	for (AKnightCharacter* K : { this, P })
	{
		AKnightCharacter* O = K == this ? P : this;
		FTradeView V;
		V.bOpen = true;
		V.Partner = NameOf(O);
		V.Mine = K->TradeItems; V.MineSlots = K->TradeSlots; V.Theirs = O->TradeItems;
		V.MyIngots = K->TradeIngots; V.TheirIngots = O->TradeIngots;
		V.Stage = K->TradeStage; V.bIAccepted = K->bTradeAccepted; V.bTheyAccepted = O->bTradeAccepted; V.Version = K->TradeVersion;
		K->ClientTradeView(V);
	}
}

void AKnightCharacter::TradeBump()
{
	AKnightCharacter* P = TradeWith.Get();
	if (!P) return;
	const int32 V = FMath::Max(TradeVersion, P->TradeVersion) + 1;
	for (AKnightCharacter* K : { this, P }) { K->TradeVersion = V; K->bTradeAccepted = false; }
	TradeSend();
}

void AKnightCharacter::TradeEnd(const FString& Why)
{
	AKnightCharacter* P = TradeWith.Get();
	for (AKnightCharacter* K : { this, P })
	{
		if (!K) continue;
		K->TradeWith = nullptr;
		K->TradeSlots.Reset(); K->TradeItems.Reset(); K->TradeIngots = 0; K->bTradeAccepted = false; K->TradeStage = 0;
		if (IsValid(K) && K->GetController()) K->ClientTradeView(FTradeView());
		if (K->Skills && !Why.IsEmpty()) K->Skills->Say(4.f, FColor::Orange, Why);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBTRADE END %s / %s: %s"), *NameOf(this), P ? *NameOf(P) : TEXT("-"), *Why);
}

void AKnightCharacter::ServerTradeOffer_Implementation(int32 PackSlot, bool bAdd)
{
	AKnightCharacter* P = TradeWith.Get();
	if (!Skills || !P || !TradeRate()) return;
	if (TradeStage != 0) { Skills->Say(3.f, FColor::Orange, TEXT("The offer is locked now. Cancel the trade to change it.")); TradeSend(); return; }
	const int32 At = TradeSlots.IndexOfByKey(PackSlot);
	if (bAdd)
	{
		if (At != INDEX_NONE || !Skills->Slots.IsValidIndex(PackSlot) || Skills->Slots[PackSlot].Id == 0 || Skills->Slots[PackSlot].Count <= 0) { TradeSend(); return; }
		if (TradeSlots.Num() >= TradeMaxItems) { Skills->Say(3.f, FColor::Orange, FString::Printf(TEXT("At most %d things in one trade."), TradeMaxItems)); return; }
		TradeSlots.Add(PackSlot);
		TradeItems.Add(Skills->Slots[PackSlot]);
	}
	else
	{
		if (At == INDEX_NONE) { TradeSend(); return; }
		TradeSlots.RemoveAt(At);
		TradeItems.RemoveAt(At);
	}
	TradeBump();
}

void AKnightCharacter::ServerTradeIngots_Implementation(int32 Amount)
{
	AKnightCharacter* P = TradeWith.Get();
	if (!Skills || !P || !TradeRate()) return;
	if (TradeStage != 0) { Skills->Say(3.f, FColor::Orange, TEXT("The offer is locked now. Cancel the trade to change it.")); TradeSend(); return; }
	TradeIngots = FMath::Clamp(Amount, 0, Skills->GetIngots());
	TradeBump();
}

void AKnightCharacter::ServerTradeCancel_Implementation()
{
	if (TradeWith.IsValid()) TradeEnd(FString::Printf(TEXT("%s cancelled the trade."), *NameOf(this)));
}

void AKnightCharacter::ServerTradeAccept_Implementation(int32 Version)
{
	AKnightCharacter* P = TradeWith.Get();
	if (!Skills || !P || !TradeRate()) return;
	if (Version != TradeVersion) { UE_LOG(LogTemp, Log, TEXT("JEEBTRADE %s accepted version %d, the trade is at %d: refused"), *NameOf(this), Version, TradeVersion); Skills->Say(3.f, FColor::Orange, TEXT("The offer changed. Have another look.")); TradeSend(); return; }
	if (FVector::Dist2D(GetActorLocation(), P->GetActorLocation()) > TradeSwapReach) { Skills->Say(3.f, FColor::Orange, TEXT("Stand closer to trade.")); return; }
	bTradeAccepted = true;
	if (!P->bTradeAccepted) { TradeSend(); return; }
	if (TradeStage == 0)
	{
		// both accepted the offer: on to the confirm screen (the offers are locked from here)
		TradeStage = P->TradeStage = 1;
		TradeBump();
		return;
	}
	TradeTry();
}

bool AKnightCharacter::TradeTry()
{
	AKnightCharacter* P = TradeWith.Get();
	if (!P || !Skills || !P->Skills) return false;
	AKnightCharacter* Side[2] = { this, P };
	// every offered slot must still hold exactly what was offered (id, size) and at least as many; no slot twice; the ingots still there
	for (AKnightCharacter* K : Side)
	{
		const USkillComponent* Sk = K->Skills;
		TSet<int32> Seen;
		for (int32 i = 0; i < K->TradeSlots.Num(); ++i)
		{
			const int32 Si = K->TradeSlots[i];
			const FInvSlot& Want = K->TradeItems[i];
			bool bOk = Sk->Slots.IsValidIndex(Si) && !Seen.Contains(Si);
			if (bOk) { const FInvSlot& Have = Sk->Slots[Si]; bOk = Have.Id == Want.Id && Have.Size == Want.Size && Have.Count >= Want.Count && Want.Count > 0; }
			Seen.Add(Si);
			if (!bOk)
			{
				TradeLog(GetWorld(), FString::Printf(TEXT("REFUSED %s's offer no longer matches their pack (slot %d, %s x%d)"), *NameOf(K), Si, *ItemName(Want.Id), Want.Count));
				TradeEnd(FString::Printf(TEXT("Trade cancelled: %s's offer changed."), *NameOf(K)));
				return false;
			}
		}
		if (K->TradeIngots < 0 || K->TradeIngots > Sk->GetIngots()) { TradeEnd(FString::Printf(TEXT("Trade cancelled: %s doesn't have those ingots."), *NameOf(K))); return false; }
	}
	if (FVector::Dist2D(GetActorLocation(), P->GetActorLocation()) > TradeSwapReach) { TradeEnd(TEXT("Trade cancelled: you walked too far apart.")); return false; }
	// both new packs worked out on copies: what leaves first, then what comes in; every piece must fit
	TArray<FInvSlot> NewPack[2];
	for (int32 s = 0; s < 2; ++s)
	{
		AKnightCharacter* K = Side[s];
		NewPack[s] = K->Skills->Slots;
		for (int32 i = 0; i < K->TradeSlots.Num(); ++i)
		{
			FInvSlot& Sl = NewPack[s][K->TradeSlots[i]];
			Sl.Count -= K->TradeItems[i].Count;
			if (Sl.Count <= 0) Sl = FInvSlot();
		}
	}
	for (int32 s = 0; s < 2; ++s)
	{
		const AKnightCharacter* From = Side[1 - s];
		for (const FInvSlot& It : From->TradeItems)
			if (USkillComponent::PutIntoArr(NewPack[s], It.Id, It.Count, It.Size) > 0)
			{
				TradeLog(GetWorld(), FString::Printf(TEXT("REFUSED no room in %s's pack"), *NameOf(Side[s])));
				for (AKnightCharacter* K : Side) { K->bTradeAccepted = false; }
				Side[s]->Skills->Say(4.f, FColor::Orange, TEXT("Your pack has no room for all of that."));
				Side[1 - s]->Skills->Say(4.f, FColor::Orange, FString::Printf(TEXT("%s's pack has no room for all of that."), *NameOf(Side[s])));
				TradeSend();
				return false;
			}
	}
	// the swap: both packs and both ingot counts change here, together (the server does nothing else in between)
	const int32 IngA = TradeIngots, IngB = P->TradeIngots;
	const FString Line = FString::Printf(TEXT("DONE %s (%s) gave [%s]; %s (%s) gave [%s]"), *NameOf(this), *KeyOf(this), *ItemsText(TradeItems, IngA), *NameOf(P), *KeyOf(P), *ItemsText(P->TradeItems, IngB));
	Skills->Slots = NewPack[0];
	P->Skills->Slots = NewPack[1];
	Skills->SpendIngots(IngA); P->Skills->SpendIngots(IngB);
	Skills->AddIngots(IngB); P->Skills->AddIngots(IngA);
	TradeLog(GetWorld(), Line);
	SaveEconomy();
	P->SaveEconomy();
	TradeEnd(FString());
	Skills->Say(4.f, FColor(120, 230, 120), TEXT("Trade complete."));
	P->Skills->Say(4.f, FColor(120, 230, 120), TEXT("Trade complete."));
	return true;
}

void AKnightCharacter::TickTrade(float Dt)
{
	if (AKnightCharacter* P = TradeWith.Get())
	{
		if (!IsValid(P) || P->TradeWith.Get() != this || !Alive(P) || !Alive(this)) TradeEnd(TEXT("The trade was cancelled."));
		else if (FVector::Dist2D(GetActorLocation(), P->GetActorLocation()) > TradeKeepReach) TradeEnd(TEXT("Trade cancelled: you walked too far apart."));
	}
	else if (TradeWith.IsStale(true)) TradeEnd(TEXT("The other player left."));
	// Market mail (a sale's pay, a returned item): every 5 s for the player on this machine's world
	MailCheckT -= Dt;
	if (MailCheckT <= 0.f && Skills && IsPlayer(this))
	{
		MailCheckT = 5.f;
		const AKnightPlayerController* PC = Cast<AKnightPlayerController>(GetController());
		if (GetNetMode() == NM_Standalone || (PC && PC->bLoggedIn))
			if (JeebMarket::DeliverMail(this, KeyOf(this))) SaveEconomy();
	}
}

void AKnightCharacter::SaveEconomy()
{
	if (!HasAuthority() || !Skills) return;
	if (GetNetMode() == NM_Standalone)
	{
		static const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("JeebShot"));
		if (!bShot) UJeebSave::Save(Skills);
		return;
	}
	if (AKnightGameMode* GM = GetWorld()->GetAuthGameMode<AKnightGameMode>()) GM->SaveCharacter(Cast<AKnightPlayerController>(GetController()));
}

// ------------------------------------------------------------------------------------------------ the server: the Market
void AKnightCharacter::MarketAnswer(bool bFresh)
{
	if (bFresh) JeebMarket::All(true);
	TArray<FMarketRow> Rows; int32 Total = 0;
	JeebMarket::Query(KeyOf(this), MarketCategory, MarketSearch, MarketPage, bMarketMine, Rows, Total);
	ClientMarketRows(Rows, Total, MarketPage, bMarketMine);
}

void AKnightCharacter::ClientMarketRows_Implementation(const TArray<FMarketRow>& Rows, int32 Total, int32 Page, bool bMine)
{
	MarketRows = Rows; MarketTotal = Total; MarketPage = Page; bMarketMine = bMine;
}

void AKnightCharacter::ServerMarketQuery_Implementation(int32 Category, const FString& Search, int32 Page, bool bMine)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastMarketOp < 0.2) return;
	LastMarketOp = Now;
	if (!NearTrader(static_cast<uint8>(ENpcRole::Banker))) return;
	MarketCategory = FMath::Clamp(Category, 0, JeebMarket::C_Num - 1);
	MarketSearch = Search.Left(30);
	MarketPage = FMath::Clamp(Page, 0, 999);
	bMarketMine = bMine;
	MarketAnswer(false);
}

void AKnightCharacter::ServerMarketList_Implementation(int32 PackSlot, int32 ItemId, int32 Price)
{
	const double Now = FPlatformTime::Seconds();
	if (!Skills || Now - LastMarketOp < 0.25) return;
	LastMarketOp = Now;
	if (!NearTrader(static_cast<uint8>(ENpcRole::Banker))) { ShopNote(TEXT("List things at a bank.")); return; }
	if (TradeWith.IsValid()) { ShopNote(TEXT("Finish your trade first.")); return; }
	if (!Skills->Slots.IsValidIndex(PackSlot) || Skills->Slots[PackSlot].Id == 0 || Skills->Slots[PackSlot].Id != ItemId) { ShopNote(TEXT("That slot changed. Pick it again.")); return; }
	if (Price < 1 || Price > JeebMarket::MaxPrice) { ShopNote(TEXT("Type a price first.")); return; }
	FString Key, Name;
	JeebRecords::WhoIs(this, Key, Name);
	if (JeebMarket::CountFor(Key) >= JeebMarket::MaxListings) { ShopNote(FString::Printf(TEXT("You already have %d things for sale. Cancel one first."), JeebMarket::MaxListings)); return; }
	const FInvSlot Item = Skills->Slots[PackSlot];
	Skills->Slots[PackSlot] = FInvSlot();   // (into escrow: out of the pack before the listing exists)
	FString Id;
	if (!JeebMarket::Add(GetWorld(), Key, Name, Item, Price, Id)) { Skills->Slots[PackSlot] = Item; ShopNote(TEXT("The market could not take it. Try again.")); return; }
	JeebMarket::Log(GetWorld(), FString::Printf(TEXT("LIST %s by %s (%s): %s x%d size %d for %d"), *Id, *Name, *Key, *ItemName(Item.Id), Item.Count, Item.Size, Price));
	SaveEconomy();
	ShopNote(FString::Printf(TEXT("Listed %s for %d ingots. You get %d when it sells."), *ItemName(Item.Id), Price, Price - JeebMarket::Tax(Price)), 5.f);
	MarketAnswer(true);
}

void AKnightCharacter::ServerMarketBuy_Implementation(const FString& Id, int32 Price)
{
	const double Now = FPlatformTime::Seconds();
	if (!Skills || Now - LastMarketOp < 0.25) return;
	LastMarketOp = Now;
	if (!NearTrader(static_cast<uint8>(ENpcRole::Banker))) { ShopNote(TEXT("Buy things at a bank.")); return; }
	FString Key, Name;
	JeebRecords::WhoIs(this, Key, Name);
	JeebMarket::FListing L;
	if (!JeebMarket::Read(Id, L)) { ShopNote(TEXT("That has already sold.")); MarketAnswer(true); return; }
	if (L.SellerKey == Key) { ShopNote(TEXT("That's your own. Cancel it in MY LISTINGS.")); return; }
	if (L.Price != Price) { ShopNote(TEXT("The price changed. Have another look.")); MarketAnswer(true); return; }
	if (Skills->GetIngots() < L.Price) { ShopNote(TEXT("Not enough ingots.")); return; }
	TArray<FInvSlot> BankCopy = Skills->Bank;
	const bool bPack = Skills->CanAdd(L.ItemId, L.Count, L.Size);
	if (!bPack && USkillComponent::PutIntoArr(BankCopy, L.ItemId, L.Count, L.Size) > 0) { ShopNote(TEXT("Your pack and bank are full.")); return; }
	Skills->SpendIngots(L.Price);
	if (!JeebMarket::Claim(L.Id, TEXT("sold")))
	{
		Skills->AddIngots(L.Price);   // (another buyer, maybe on another world, got it first)
		JeebMarket::Log(GetWorld(), FString::Printf(TEXT("LOST %s: %s (%s) was too late, refunded %d"), *L.Id, *Name, *Key, L.Price));
		ShopNote(TEXT("Someone bought that just before you. Your ingots are back."));
		MarketAnswer(true);
		return;
	}
	if (bPack) Skills->AddInvItem(L.ItemId, L.Count, L.Size);
	else Skills->Bank = BankCopy;
	const int32 Tax = JeebMarket::Tax(L.Price);
	JeebMarket::SendMail(L.SellerKey, L.Price - Tax, FInvSlot(), FString::Printf(TEXT("Market: your %s sold to %s for %d ingots (%d tax). %d ingots added."), *ItemName(L.ItemId), *Name, L.Price, Tax, L.Price - Tax));
	JeebMarket::Log(GetWorld(), FString::Printf(TEXT("SOLD %s: %s x%d size %d from %s (%s) to %s (%s) for %d, tax %d"), *L.Id, *ItemName(L.ItemId), L.Count, L.Size, *L.SellerName, *L.SellerKey, *Name, *Key, L.Price, Tax));
	SaveEconomy();
	ShopNote(FString::Printf(TEXT("Bought %s for %d ingots. It is in your %s."), *ItemName(L.ItemId), L.Price, bPack ? TEXT("pack") : TEXT("bank")), 5.f);
	MarketAnswer(true);
}

void AKnightCharacter::ServerMarketCancel_Implementation(const FString& Id)
{
	const double Now = FPlatformTime::Seconds();
	if (!Skills || Now - LastMarketOp < 0.25) return;
	LastMarketOp = Now;
	if (!NearTrader(static_cast<uint8>(ENpcRole::Banker))) return;
	FString Key, Name;
	JeebRecords::WhoIs(this, Key, Name);
	JeebMarket::FListing L;
	if (!JeebMarket::Read(Id, L) || L.SellerKey != Key) { ShopNote(TEXT("That has already sold.")); MarketAnswer(true); return; }
	if (!JeebMarket::Claim(L.Id, TEXT("cancelled"))) { ShopNote(TEXT("That has just sold.")); MarketAnswer(true); return; }
	TArray<FInvSlot> BankCopy = Skills->Bank;
	FString Where = TEXT("pack");
	if (Skills->CanAdd(L.ItemId, L.Count, L.Size)) Skills->AddInvItem(L.ItemId, L.Count, L.Size);
	else if (USkillComponent::PutIntoArr(BankCopy, L.ItemId, L.Count, L.Size) == 0) { Skills->Bank = BankCopy; Where = TEXT("bank"); }
	else { JeebMarket::SendMail(Key, 0, FInvSlot(L.ItemId, L.Count, L.Size), FString::Printf(TEXT("Market: your %s is back in your bank."), *ItemName(L.ItemId))); Where = TEXT("bank, once it has room"); }
	JeebMarket::Log(GetWorld(), FString::Printf(TEXT("CANCEL %s by %s (%s): %s x%d size %d back to the %s"), *L.Id, *Name, *Key, *ItemName(L.ItemId), L.Count, L.Size, *Where));
	SaveEconomy();
	ShopNote(FString::Printf(TEXT("Taken off the market. It is in your %s."), *Where), 5.f);
	MarketAnswer(true);
}
