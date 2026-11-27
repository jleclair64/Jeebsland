// Online tests of trade and the Market (2026-11-26, Docs/EconomyPlan.md; Tools/economy_net.sh). Non-Shipping only.
//   -EconTest     (server, one world, two players econa and econb): the players' own machines ask to trade with E (they stand face to face), then the
//                 server drives both sides through the same request functions the RPCs run, with cheat attempts, a full pack, a real swap, walking
//                 away, and the Market on one world (list, a wrong price, a buy, the seller's pay, cancel, the listing limit).
//   -EconSeller   (server): the first player lists an iron sword for 100 and writes its id to Saved/Market/test_listing.txt, then logs nothing more.
//   -EconBuyer    (server, another world): the first player buys that listing.
//   -EconPayday   (server): the seller logs in again: their pay (95) arrives by mail.
//   -EconRace=1|2 (two servers): world 1 writes 400 listings; at the moment in Saved/Market/race_go.txt both worlds claim all of them as fast as they
//                 can; every listing must go to exactly one world.
//   -EconDo       (a player's machine): asks to trade with the player in front of it, and logs every trade window it is sent (JEEBECONDO).
// The server logs JEEBECONTEST PASS / FAIL lines and JEEBECONTEST DONE.
#include "KnightCharacter.h"
#include "KnightPlayerController.h"
#include "KnightGameMode.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "JeebEconomy.h"
#include "OakHill.h"
#include "Terrain.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Algo/Reverse.h"
#include "GameFramework/PlayerController.h"

using namespace JeebItems;

#if !UE_BUILD_SHIPPING
namespace
{
	void ELog(const FString& S) { UE_LOG(LogTemp, Log, TEXT("%s"), *S); }
	int32 Passes = 0, Fails = 0;
	void Check(bool bOk, const FString& What) { ELog(FString::Printf(TEXT("JEEBECONTEST %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); (bOk ? Passes : Fails)++; }
	const JeebOakHill::FCounter* BankCounter() { for (const JeebOakHill::FCounter& C : JeebOakHill::Counters()) if (C.Shop == JeebOakHill::EShop::Bank) return &C; return nullptr; }
	void Stand(AKnightCharacter* K, const FVector2D& P, float Yaw)
	{
		K->TeleportTo(FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) + 120.f), FRotator(0.f, Yaw, 0.f), false, true);
		if (APlayerController* PC = Cast<APlayerController>(K->GetController())) { PC->SetControlRotation(FRotator(-5.f, Yaw, 0.f)); PC->ClientSetRotation(FRotator(-5.f, Yaw, 0.f), true); }   // (the player's machine turns too: it owns its view)
	}
	AKnightCharacter* ByKey(UWorld* W, const TCHAR* Key)
	{
		for (TActorIterator<AKnightCharacter> It(W); It; ++It)
			if (const AKnightPlayerController* PC = Cast<AKnightPlayerController>(It->GetController())) if (PC->bLoggedIn && PC->AccountKey == Key) return *It;
		return nullptr;
	}
	AKnightCharacter* FirstPlayer(UWorld* W)
	{
		for (TActorIterator<AKnightCharacter> It(W); It; ++It) if (const AKnightPlayerController* PC = Cast<AKnightPlayerController>(It->GetController())) if (PC->bLoggedIn) return *It;
		return nullptr;
	}
	void SetIngots(USkillComponent* S, int32 N) { S->SpendIngots(S->GetIngots()); S->AddIngots(N); }
	int32 Count(const USkillComponent* S, int32 Id, int32 Size = -1) { int32 N = 0; for (const FInvSlot& Sl : S->Slots) if (Sl.Id == Id && (Size < 0 || Sl.Size == Size)) N += Sl.Count; return N; }
	int32 SlotOf(const USkillComponent* S, int32 Id) { for (int32 i = 0; i < S->Slots.Num(); ++i) if (S->Slots[i].Id == Id) return i; return -1; }
	FString TestFile() { return JeebMarket::Dir() / TEXT("test_listing.txt"); }
}
#endif

void AKnightCharacter::TickEconomyTest(float Dt)
{
#if !UE_BUILD_SHIPPING
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("EconTest"));
	static const bool bSeller = FParse::Param(FCommandLine::Get(), TEXT("EconSeller"));
	static const bool bBuyer = FParse::Param(FCommandLine::Get(), TEXT("EconBuyer"));
	static const bool bPayday = FParse::Param(FCommandLine::Get(), TEXT("EconPayday"));
	static const bool bDo = FParse::Param(FCommandLine::Get(), TEXT("EconDo"));
	if (!Skills) return;

	// ------------------------------------------------------------------ a player's machine
	if (bDo && !HasAuthority() && IsLocallyControlled())
	{
		EconTestT += Dt;
		static float AskT = 0.f; static int32 LastV = -1; static bool bWasOpen = false;
		AskT += Dt;
		static int32 Asks = 0;
		if (!IsTradeOpen() && AskT > 2.f && Asks < 3)
		{
			AskT = 0.f;
			if (AKnightCharacter* O = TradeTargetInReach()) { ++Asks; ELog(TEXT("JEEBECONDO asking the player in front of me to trade (E)")); AskTrade(O); }
			else if (FMath::Fmod(EconTestT, 20.f) < 2.1f)
				for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It) if (*It != this)
					ELog(FString::Printf(TEXT("JEEBECONDO no one in reach: %s at %.0f cm, facing dot %.2f, player state %d"), *It->GetName(), FVector::Dist2D(It->GetActorLocation(), GetActorLocation()),
						FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), (It->GetActorLocation() - GetActorLocation()).GetSafeNormal2D()), It->GetPlayerState() ? 1 : 0));
		}
		if (TradeView.bOpen != bWasOpen || (TradeView.bOpen && TradeView.Version != LastV))
		{
			bWasOpen = TradeView.bOpen; LastV = TradeView.Version;
			FString Mine, Theirs;
			for (const FInvSlot& S : TradeView.Mine) Mine += FString::Printf(TEXT("%s x%d size %d; "), *ItemName(S.Id), S.Count, S.Size);
			for (const FInvSlot& S : TradeView.Theirs) Theirs += FString::Printf(TEXT("%s x%d size %d; "), *ItemName(S.Id), S.Count, S.Size);
			ELog(FString::Printf(TEXT("JEEBECONDO window %s with %s v%d stage %d accepted %d/%d; mine [%s] +%d ingots; theirs [%s] +%d ingots; my ingots %d"), TradeView.bOpen ? TEXT("open") : TEXT("closed"), *TradeView.Partner, TradeView.Version, TradeView.Stage,
				TradeView.bIAccepted, TradeView.bTheyAccepted, *Mine, TradeView.MyIngots, *Theirs, TradeView.TheirIngots, Skills->GetIngots()));
		}
		return;
	}
	if (!HasAuthority() || !(bTest || bSeller || bBuyer || bPayday)) return;
	UWorld* W = GetWorld();
	static double LastFrame = -1.0;
	if (W->GetTimeSeconds() == LastFrame) return;   // (one driver step a frame, whichever player ticks first)
	LastFrame = W->GetTimeSeconds();
	static int32 Stage = 0; static float StageT = 0.f; static float T = 0.f;
	T += Dt; StageT += Dt;
	auto Next = [&]() { ++Stage; StageT = 0.f; };
	auto Done = [&]() { ELog(FString::Printf(TEXT("JEEBECONTEST DONE: %d passed, %d failed"), Passes, Fails)); Stage = 9999; };
	if (Stage >= 9999) return;
	const JeebOakHill::FCounter* Bank = BankCounter();
	if (!Bank) { Check(false, TEXT("a bank counter")); Done(); return; }

	// ------------------------------------------------------------------ cross-world: seller, buyer, payday
	if (bSeller || bBuyer || bPayday)
	{
		AKnightCharacter* P = FirstPlayer(W);
		if (!P || T < 8.f) return;
		USkillComponent* S = P->Skills;
		const FString Key = Cast<AKnightPlayerController>(P->GetController())->AccountKey;
		if (Stage == 0)
		{
			Stand(P, Bank->At, Bank->Facing);
			if (bSeller) { for (FInvSlot& Sl : S->Slots) Sl = FInvSlot(); S->Slots[0] = FInvSlot(GearId(K_Sword, 1), 1); SetIngots(S, 500); }
			if (bBuyer) SetIngots(S, 500);
			ELog(FString::Printf(TEXT("JEEBECONTEST %s on port %d: at the bank, ingots %d"), *Key, W->URL.Port, S->GetIngots()));
			Next(); return;
		}
		if (Stage == 1 && StageT > 2.f)
		{
			if (bSeller)
			{
				P->LastMarketOp = -10.0;
				P->ServerMarketList_Implementation(0, GearId(K_Sword, 1), 100);
				FString Id;
				for (const JeebMarket::FListing& L : JeebMarket::All(true)) if (L.SellerKey == Key) Id = L.Id;
				Check(!Id.IsEmpty() && S->Slots[0].Id == 0, FString::Printf(TEXT("listed on world %d: %s, the sword left the pack (escrow)"), W->URL.Port, *Id));
				FFileHelper::SaveStringToFile(Id, *TestFile());
				Done(); return;
			}
			if (bBuyer)
			{
				FString Id; FFileHelper::LoadFileToString(Id, *TestFile()); Id.TrimStartAndEndInline();
				JeebMarket::FListing L;
				const bool bSeen = JeebMarket::Read(Id, L);   // (read first: never in the same expression that prints it)
				Check(bSeen && L.Price == 100, FString::Printf(TEXT("world %d sees the listing made on another world (%s, %s for %d by %s)"), W->URL.Port, *Id, *ItemName(L.ItemId), L.Price, *L.SellerName));
				P->LastMarketOp = -10.0;
				P->ServerMarketBuy_Implementation(Id, 100);
				Check(Count(S, GearId(K_Sword, 1)) == 1 && S->GetIngots() == 400, FString::Printf(TEXT("bought it on world %d: sword in the pack, ingots %d (expect 400)"), W->URL.Port, S->GetIngots()));
				Check(!JeebMarket::Read(Id, L), TEXT("the listing is gone from the market"));
				Done(); return;
			}
			if (bPayday)
			{
				// (the mail is checked every 5 s; it may have come already)
				if (StageT < 12.f) return;
				Check(S->GetIngots() == 595, FString::Printf(TEXT("the seller, back on world %d, was paid by mail: ingots %d (expect 500 + 100 - 5 tax = 595)"), W->URL.Port, S->GetIngots()));
				Done(); return;
			}
		}
		return;
	}

	// ------------------------------------------------------------------ one world: trade and the market
	AKnightCharacter* A = ByKey(W, TEXT("econa"));
	AKnightCharacter* B = ByKey(W, TEXT("econb"));
	if (!A || !B) { if (T > 200.f) { Check(false, TEXT("both players logged in")); Done(); } return; }
	USkillComponent* SA = A->Skills; USkillComponent* SB = B->Skills;
	const int32 Sword = GearId(K_Sword, 1), Wand = GearId(K_Wand, 0), Logs = MaterialId(EItemType::Logs), Trout = MaterialId(EItemType::Trout), Bass = MaterialId(EItemType::Bass);
	auto Ready = [&]() { return StageT > 0.4f; };   // (the trade requests allow one every 0.1 s per player)
	auto Free = [&]() { A->LastTradeOp = B->LastTradeOp = -10.0; A->LastMarketOp = B->LastMarketOp = -10.0; };
	auto Setup = [&]()
	{
		for (USkillComponent* S : { SA, SB }) { for (FInvSlot& Sl : S->Slots) Sl = FInvSlot(); SetIngots(S, 500); }
		SA->Slots[0] = FInvSlot(Sword, 1); SA->Slots[1] = FInvSlot(Logs, 50); SA->Slots[2] = FInvSlot(Trout, 1, 305);
		SB->Slots[0] = FInvSlot(Wand, 1); SB->Slots[1] = FInvSlot(Bass, 1, 180);
	};
	switch (Stage)
	{
	case 0:
		if (T < 10.f) return;
		Setup();
		{
			const FVector2D P(Bank->At), Dir(FMath::Cos(FMath::DegreesToRadians(Bank->Facing + 90.f)), FMath::Sin(FMath::DegreesToRadians(Bank->Facing + 90.f)));
			Stand(A, P - Dir * 80.f, Bank->Facing + 90.f);
			Stand(B, P + Dir * 80.f, Bank->Facing - 90.f);
		}
		ELog(TEXT("JEEBECONTEST start: econa (iron sword, 50 logs, trout 30.5 in, 500 ingots) and econb (tin wand, bass 18.0 in, 500 ingots) face to face at the bank"));
		Next(); return;
	case 1:   // their own machines ask (E), each to the other
		if (!A->TradeWith.IsValid()) { if (StageT > 40.f) { Check(false, TEXT("the trade opened when both pressed E")); Done(); } return; }
		Check(A->TradeWith.Get() == B && B->TradeWith.Get() == A, TEXT("both players asked (E, from their own machines): the trade window opened for both"));
		Next(); return;
	case 2:   // cheat: offer an empty slot, a slot out of range
		if (!Ready()) return; Free();
		A->ServerTradeOffer_Implementation(10, true);
		A->LastTradeOp = -10.0; A->ServerTradeOffer_Implementation(99, true);
		Check(A->TradeSlots.Num() == 0, TEXT("offering an empty pack slot and one that doesn't exist is refused"));
		Next(); return;
	case 3:   // the real offers
		if (!Ready()) return; Free();
		A->ServerTradeOffer_Implementation(0, true); A->LastTradeOp = -10.0;
		A->ServerTradeOffer_Implementation(2, true); A->LastTradeOp = -10.0;
		A->ServerTradeIngots_Implementation(100);
		B->ServerTradeOffer_Implementation(0, true); B->LastTradeOp = -10.0;
		B->ServerTradeOffer_Implementation(1, true); B->LastTradeOp = -10.0;
		B->ServerTradeIngots_Implementation(99999);
		Check(B->TradeIngots == 500, FString::Printf(TEXT("offering more ingots than you have is held to what you have (%d)"), B->TradeIngots));
		B->LastTradeOp = -10.0; B->ServerTradeIngots_Implementation(30);
		Check(A->TradeSlots.Num() == 2 && B->TradeSlots.Num() == 2 && A->TradeIngots == 100 && B->TradeIngots == 30, TEXT("offers: econa sword + trout + 100 ingots, econb wand + bass + 30 ingots"));
		Next(); return;
	case 4:   // cheat: accept an old version; an accept is cleared by a change
		if (!Ready()) return; Free();
		A->ServerTradeAccept_Implementation(A->TradeVersion - 1);
		Check(!A->bTradeAccepted, TEXT("accepting an offer version you were not shown is refused"));
		A->LastTradeOp = -10.0; A->ServerTradeAccept_Implementation(A->TradeVersion);
		B->ServerTradeOffer_Implementation(1, false);   // econb takes the bass back after econa accepted
		Check(!A->bTradeAccepted && A->TradeStage == 0, TEXT("changing an offer after the other side accepted clears their accept"));
		B->LastTradeOp = -10.0; B->ServerTradeOffer_Implementation(1, true);
		Next(); return;
	case 5:   // both accept: confirm screen, offers locked
		if (!Ready()) return; Free();
		A->ServerTradeAccept_Implementation(A->TradeVersion);
		B->ServerTradeAccept_Implementation(B->TradeVersion);
		Check(A->TradeStage == 1 && B->TradeStage == 1 && !A->bTradeAccepted, TEXT("both accepted: on to the confirm screen"));
		Free();
		A->ServerTradeOffer_Implementation(1, true); A->LastTradeOp = -10.0;
		A->ServerTradeIngots_Implementation(0); A->LastTradeOp = -10.0;
		B->ServerTradeOffer_Implementation(0, false);
		Check(A->TradeSlots.Num() == 2 && A->TradeIngots == 100 && B->TradeSlots.Num() == 2, TEXT("after both accepted, changing the offer is refused (add logs, take away ingots, take back the wand)"));
		Next(); return;
	case 6:   // cheat: the trout leaves econa's pack behind the trade's back (dropped); then both confirm
		if (!Ready()) return; Free();
		A->DoInv(EInvOp::DropPack, 2, 1, 0, nullptr, FVector::ZeroVector);
		A->ServerTradeAccept_Implementation(A->TradeVersion);
		B->ServerTradeAccept_Implementation(B->TradeVersion);
		Check(!A->TradeWith.IsValid() && Count(SA, Sword) == 1 && Count(SB, Wand) == 1 && SA->GetIngots() == 500 && SB->GetIngots() == 500,
			TEXT("trading an item you no longer have: the trade is cancelled and nothing moves (sword, wand and ingots where they were)"));
		Next(); return;
	case 7:   // again with a full pack on econb's side
		if (!Ready()) return; Free();
		Setup();
		A->ServerTradeAsk_Implementation(B); B->ServerTradeAsk_Implementation(A);
		Check(A->TradeWith.Get() == B, TEXT("a new trade opens"));
		for (int32 i = 2; i < SB->Slots.Num(); ++i) SB->Slots[i] = FInvSlot(Logs, StackSize(Logs) > 1 ? StackSize(Logs) : 1);   // econb's pack: full of full log stacks
		Free();
		A->ServerTradeOffer_Implementation(0, true); A->LastTradeOp = -10.0; A->ServerTradeOffer_Implementation(1, true); A->LastTradeOp = -10.0; A->ServerTradeOffer_Implementation(2, true);
		B->ServerTradeOffer_Implementation(0, true);
		Next(); return;
	case 8:
		if (!Ready()) return; Free();
		A->ServerTradeAccept_Implementation(A->TradeVersion); B->ServerTradeAccept_Implementation(B->TradeVersion);
		Free();
		A->ServerTradeAccept_Implementation(A->TradeVersion); B->ServerTradeAccept_Implementation(B->TradeVersion);
		Check(A->TradeWith.IsValid() && Count(SA, Sword) == 1 && Count(SB, Wand) == 1, TEXT("a pack without room: the swap is refused, nothing moves, the trade stays open"));
		for (int32 i = 2; i < SB->Slots.Num(); ++i) SB->Slots[i] = FInvSlot();   // (room again)
		Next(); return;
	case 9:   // the real swap
		if (!Ready()) return; Free();
		A->ServerTradeCancel_Implementation();
		Check(!A->TradeWith.IsValid() && !B->TradeWith.IsValid(), TEXT("cancel closes the trade for both"));
		Free();
		A->ServerTradeAsk_Implementation(B); B->ServerTradeAsk_Implementation(A);
		Free();
		A->ServerTradeOffer_Implementation(0, true); A->LastTradeOp = -10.0; A->ServerTradeOffer_Implementation(1, true); A->LastTradeOp = -10.0; A->ServerTradeOffer_Implementation(2, true); A->LastTradeOp = -10.0;
		A->ServerTradeIngots_Implementation(100);
		B->ServerTradeOffer_Implementation(0, true); B->LastTradeOp = -10.0; B->ServerTradeOffer_Implementation(1, true); B->LastTradeOp = -10.0; B->ServerTradeIngots_Implementation(30);
		Free();
		A->ServerTradeAccept_Implementation(A->TradeVersion); B->ServerTradeAccept_Implementation(B->TradeVersion);
		Free();
		A->ServerTradeAccept_Implementation(A->TradeVersion); B->ServerTradeAccept_Implementation(B->TradeVersion);
		Check(!A->TradeWith.IsValid(), TEXT("both confirmed: the trade is done"));
		Check(Count(SA, Wand) == 1 && Count(SA, Bass, 180) == 1 && Count(SA, Sword) == 0 && Count(SA, Logs) == 0 && Count(SA, Trout) == 0 && SA->GetIngots() == 430,
			FString::Printf(TEXT("econa now has the wand and the 18.0 in bass (size kept), no sword, logs or trout, ingots %d (expect 430)"), SA->GetIngots()));
		Check(Count(SB, Sword) == 1 && Count(SB, Logs) == 50 && Count(SB, Trout, 305) == 1 && Count(SB, Wand) == 0 && Count(SB, Bass) == 0 && SB->GetIngots() == 570,
			FString::Printf(TEXT("econb now has the sword, 50 logs and the 30.5 in trout (size kept), ingots %d (expect 570)"), SB->GetIngots()));
		Next(); return;
	case 10:   // walking away ends a trade
		if (!Ready()) return; Free();
		A->ServerTradeAsk_Implementation(B); B->ServerTradeAsk_Implementation(A);
		Stand(B, FVector2D(B->GetActorLocation()) + FVector2D(1200.f, 0.f), 0.f);
		Next(); return;
	case 11:
		if (StageT < 1.f) return;
		Check(!A->TradeWith.IsValid() && !B->TradeWith.IsValid(), TEXT("walking 12 m apart ends the trade for both"));
		Stand(B, Bank->At, Bank->Facing); Stand(A, Bank->At + FVector2D(0.f, 120.f), Bank->Facing);
		Next(); return;
	case 12:   // the Market: econa lists the wand; a wrong price is refused; econb buys it; econa is paid by mail
	{
		if (StageT < 2.f) return; Free();
		const int32 Ws = SlotOf(SA, Wand);
		A->ServerMarketList_Implementation(Ws, Wand, 100);
		FString Id;
		for (const JeebMarket::FListing& L : JeebMarket::All(true)) if (L.SellerKey == TEXT("econa") && L.ItemId == Wand) Id = L.Id;
		Check(!Id.IsEmpty() && Count(SA, Wand) == 0, FString::Printf(TEXT("econa listed the wand for 100 (%s); it left the pack"), *Id));
		const int32 IngB = SB->GetIngots(), IngA = SA->GetIngots();
		B->ServerMarketBuy_Implementation(Id, 50); B->LastMarketOp = -10.0;
		Check(Count(SB, Wand) == 0 && SB->GetIngots() == IngB, TEXT("buying at a price other than the listed one is refused"));
		A->ServerMarketBuy_Implementation(Id, 100); A->LastMarketOp = -10.0;
		Check(Count(SA, Wand) == 0 && SA->GetIngots() == IngA, TEXT("buying your own listing is refused"));
		B->ServerMarketBuy_Implementation(Id, 100); B->LastMarketOp = -10.0;
		Check(Count(SB, Wand) == 1 && SB->GetIngots() == IngB - 100, FString::Printf(TEXT("econb bought the wand: ingots %d -> %d"), IngB, SB->GetIngots()));
		B->ServerMarketBuy_Implementation(Id, 100);
		Check(Count(SB, Wand) == 1 && SB->GetIngots() == IngB - 100, TEXT("buying it a second time is refused (already sold)"));
		Next(); return;
	}
	case 13:
		if (StageT < 7.f) return;   // (the mail is delivered every 5 s)
		Check(SA->GetIngots() == 430 + 95, FString::Printf(TEXT("econa was paid by mail: ingots %d (expect 430 + 95)"), SA->GetIngots()));
		Next(); return;
	case 14:   // cancel keeps a fish's size; the listing limit
	{
		if (!Ready()) return; Free();
		const int32 Bs = SlotOf(SA, Bass);
		A->ServerMarketList_Implementation(Bs, Bass, 40); A->LastMarketOp = -10.0;
		FString Id;
		for (const JeebMarket::FListing& L : JeebMarket::All(true)) if (L.SellerKey == TEXT("econa") && L.ItemId == Bass) Id = L.Id;
		Check(!Id.IsEmpty() && Count(SA, Bass) == 0, TEXT("econa listed the bass"));
		B->ServerMarketCancel_Implementation(Id); B->LastMarketOp = -10.0;
		Check(Count(SB, Bass) == 0 && !Id.IsEmpty() && JeebMarket::All(true).ContainsByPredicate([&](const JeebMarket::FListing& L) { return L.Id == Id; }), TEXT("cancelling someone else's listing is refused"));
		A->ServerMarketCancel_Implementation(Id); A->LastMarketOp = -10.0;
		Check(Count(SA, Bass, 180) == 1, TEXT("econa cancelled it: the bass is back in the pack with its 18.0 in size"));
		for (FInvSlot& Sl : SA->Slots) Sl = FInvSlot();
		for (int32 i = 0; i < 12; ++i) SA->Slots[i] = FInvSlot(Logs, 1);
		int32 Listed = 0;
		for (int32 i = 0; i < 12; ++i) { A->ServerMarketList_Implementation(i, Logs, 5); A->LastMarketOp = -10.0; }
		for (const JeebMarket::FListing& L : JeebMarket::All(true)) if (L.SellerKey == TEXT("econa")) ++Listed;
		Check(Listed == JeebMarket::MaxListings && Count(SA, Logs) == 12 - JeebMarket::MaxListings, FString::Printf(TEXT("the listing limit: %d listed, the rest refused and kept (%d logs left)"), Listed, Count(SA, Logs)));
		const TArray<JeebMarket::FListing> Now = JeebMarket::All(true);   // (a copy: each cancel re-reads the market)
		for (const JeebMarket::FListing& L : Now) if (L.SellerKey == TEXT("econa")) { A->ServerMarketCancel_Implementation(L.Id); A->LastMarketOp = -10.0; }
		Check(Count(SA, Logs) == 12, TEXT("all cancelled, the logs back"));
		Next(); return;
	}
	case 15:   // shops by town (the server's rule)
		Check(JeebShops::Sells(JeebShops::T_OakHill, Sword) && !JeebShops::Sells(JeebShops::T_SilverPoint, Sword) && JeebShops::Sells(JeebShops::T_SilverPoint, Wand) && !JeebShops::Sells(JeebShops::T_OakHill, Wand)
			&& JeebShops::Sells(JeebShops::T_SteeleCreek, GearId(K_Club, 0)) && !JeebShops::Sells(JeebShops::T_OakHill, GearId(K_Club, 0)) && JeebShops::Sells(JeebShops::T_SilverPoint, GearId(K_Pickaxe, 0)) && JeebShops::Sells(JeebShops::T_SteeleCreek, ArrowId(A_Wooden)),
			TEXT("shops by town: sword at Oak Hill not Silver Point, wand at Silver Point not Oak Hill, club at Steele Creek not Oak Hill, pickaxe and wooden arrows everywhere"));
		Check(JeebShops::TownAt(JeebOakHill::PosX, JeebOakHill::PosY) == JeebShops::T_OakHill && JeebShops::TownAt(Bank->At.X, Bank->At.Y) == JeebShops::T_OakHill, TEXT("Oak Hill's shops are in Oak Hill"));
		Done(); return;
	default: return;
	}
#endif
}

// ------------------------------------------------------------------ two servers race for the same listings (-EconRace=1|2; called by the game mode)
namespace JeebEconTest
{
	void StartRace(AKnightGameMode* GM)
	{
#if !UE_BUILD_SHIPPING
		int32 Who = 0;
		if (!GM || !FParse::Value(FCommandLine::Get(), TEXT("EconRace="), Who)) return;
		const FString IdsFile = JeebMarket::Dir() / TEXT("race_ids.txt"), GoFile = JeebMarket::Dir() / TEXT("race_go.txt");
		if (Who == 1)
		{
			TArray<FString> Ids;
			for (int32 i = 0; i < 400; ++i) { FString Id; if (JeebMarket::Add(GM->GetWorld(), TEXT("racer"), TEXT("Racer"), FInvSlot(MaterialId(EItemType::Logs), 1), 10 + i, Id)) Ids.Add(Id); }
			FFileHelper::SaveStringToFile(FString::Join(Ids, TEXT("\n")), *IdsFile);
			ELog(FString::Printf(TEXT("JEEBECONRACE world 1 wrote %d listings"), Ids.Num()));
		}
		FTimerHandle H;
		GM->GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([Who, IdsFile, GoFile]()
		{
			static bool bRan = false;
			if (bRan) return;
			FString Go; if (!FFileHelper::LoadFileToString(Go, *GoFile)) return;
			const int64 At = FCString::Atoi64(*Go);
			auto NowMs = []() { const FDateTime N = FDateTime::UtcNow(); return N.ToUnixTimestamp() * 1000 + N.GetMillisecond(); };
			if (NowMs() < At - 500) return;
			while (NowMs() < At) {}   // (both worlds start on the same millisecond)
			bRan = true;
			FString Text; FFileHelper::LoadFileToString(Text, *IdsFile);
			TArray<FString> Ids; Text.ParseIntoArrayLines(Ids);
			if (Who == 2) Algo::Reverse(Ids);   // (world 2 starts from the other end: the two meet in the middle, where they fight over the same listings)
			TArray<FString> Won;
			const int64 T0 = NowMs();
			for (const FString& Id : Ids) if (JeebMarket::Claim(Id.TrimStartAndEnd(), Who == 1 ? TEXT("race1") : TEXT("race2"))) Won.Add(Id.TrimStartAndEnd());
			ELog(FString::Printf(TEXT("JEEBECONRACE world %d claimed from %lld to %lld ms"), Who, T0 % 100000, NowMs() % 100000));
			ELog(FString::Printf(TEXT("JEEBECONRACE world %d won %d of %d: %s"), Who, Won.Num(), Ids.Num(), *FString::Join(Won, TEXT(","))));
		}), 0.005f, true);
#endif
	}
}
