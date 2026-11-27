// The trade window and the bank's MARKET tab (2026-11-26, Docs/EconomyPlan.md). Drawing and asking only: the server decides (KnightCharacterTrade.cpp).
#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "JeebProps.h"
#include "JeebEconomy.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Framework/Application/SlateApplication.h"

using namespace JeebItems;

namespace
{
	const FLinearColor Gold(1.f, 0.85f, 0.3f), Grey(0.75f, 0.75f, 0.75f), Good(0.6f, 1.f, 0.6f);
	FString WithSize(const FInvSlot& S) { return S.IsSized() ? FString::Printf(TEXT("%s (%s)"), *ItemName(S.Id), *JeebProps::FishSizeText(S.Id, S.Size)) : ItemName(S.Id); }
}

// ------------------------------------------------------------------------------------------------ typing (a price, a search, ingots)
void AKnightHUD::MktTypeChar(TCHAR C)
{
	FString* T = MktField == 1 ? &MktSearchText : MktField == 2 ? &MktPriceText : &TradeIngotText;
	if (MktField == 1) { if (C >= 32 && C != 127 && T->Len() < 24) T->AppendChar(C); }
	else if (C >= '0' && C <= '9' && T->Len() < 8) T->AppendChar(C);
}

bool AKnightHUD::MktTypeKey(const FKey& Key)
{
	FString* T = MktField == 1 ? &MktSearchText : MktField == 2 ? &MktPriceText : &TradeIngotText;
	if (Key == EKeys::BackSpace) { if (T->Len() > 0) T->LeftChopInline(1); return true; }
	if (Key == EKeys::Enter || Key == EKeys::Escape || Key == EKeys::Tab)
	{
		const int32 Field = MktField;
		MktField = 0;
		HideTyping();
		if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
		AKnightCharacter* K = Cast<AKnightCharacter>(GetOwningPawn());
		if (K && Key == EKeys::Enter)
		{
			if (Field == 1) { MktPage = 0; MarketAsk(K); }
			if (Field == 3 && K->IsTradeOpen()) K->ServerTradeIngots(FCString::Atoi(*TradeIngotText));
		}
	}
	return true;
}

// A text box: click to type in it (Field), Enter to finish
void AKnightHUD::MktBox(float X, float Y, float W, int32 Field, const FString& Text, const TCHAR* Empty)
{
	UFont* Font = GEngine->GetMediumFont();
	const bool bOn = MktField == Field;
	Tile(X, Y, W, 34.f, FLinearColor(0.8f, 0.82f, 0.9f), bOn ? 0.4f : (Over(X, Y, W, 34.f) ? 0.3f : 0.2f));
	if (bOn) Outline(X - 1.f, Y - 1.f, W + 2.f, 36.f, 8.f, FLinearColor(1.f, 0.8f, 0.3f, 1.f));
	const bool bCaret = bOn && FMath::Fmod(FPlatformTime::Seconds(), 1.0) < 0.5;
	HDText(Text.IsEmpty() && !bOn ? FString(Empty) : Text + (bCaret ? TEXT("|") : TEXT("")), Text.IsEmpty() && !bOn ? FLinearColor(1.f, 1.f, 1.f, 0.4f) : FLinearColor::White, X + 8.f, Y + 7.f, Font, 0.95f);
	if (Over(X, Y, W, 34.f) && bUIClick) { MktField = Field; EnsureTyping(); }
}

// ------------------------------------------------------------------------------------------------ the trade window
void AKnightHUD::DrawTradeScreen(AKnightCharacter* Knight)
{
	USkillComponent* Sk = Knight->GetSkills();
	const FTradeView& V = Knight->TradeView;
	UFont* Font = GEngine->GetMediumFont();
	const float PW = 1160.f, PH = 660.f;
	const float PX = (Canvas->ClipX - PW) * 0.5f, PY = (Canvas->ClipY - PH) * 0.5f;
	Panel(PX, PY, PW, PH);
	WindowTitle(FString::Printf(TEXT("TRADE WITH %s"), *V.Partner.ToUpper()), PX + 30.f, PY + 14.f, 1.6f);
	if (CloseBtn(PX, PY, PW)) { Knight->ServerTradeCancel(); return; }
	const bool bLocked = V.Stage != 0;

	// ----- your pack (click to offer a whole stack)
	const float SS = 62.f, G = 6.f;
	const float GX = PX + 30.f, GY = PY + 100.f;
	HDText(bLocked ? TEXT("YOUR PACK") : TEXT("YOUR PACK  (click to offer)"), Gold, GX, PY + 66.f, Font, 1.0f);
	for (int32 i = 0; i < NumSlots && i < Sk->Slots.Num(); ++i)
	{
		const float X = GX + (i % 4) * (SS + G), Y = GY + (i / 4) * (SS + G);
		const FInvSlot S = Sk->Slots[i];
		const bool bOffered = V.MineSlots.Contains(i);
		const bool bOver = Over(X, Y, SS, SS);
		Tile(X, Y, SS, SS, bOver && !bLocked ? FLinearColor(0.85f, 0.93f, 1.f) : FLinearColor(0.6f, 0.72f, 0.88f), bOver ? 0.36f : 0.2f);
		if (S.Id == 0) continue;
		DrawIconIn(S.Id, X + 6.f, Y + 5.f, SS - 12.f, X, Y, SS, SS);
		if (StackSize(S.Id) > 1) HDText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, X + SS - 26.f, Y + SS - 20.f, Font, 0.85f);
		if (bOffered) FillRR(X, Y, SS, SS, 8.f, [&](float) { return FLinearColor(0.f, 0.f, 0.f, 0.55f); });
		if (bOver)
		{
			ItemTip(S.Id, S.Size);
			if (bUIClick && !bLocked) Knight->ServerTradeOffer(i, !bOffered);
		}
	}

	// ----- the two offers
	auto Offer = [&](float X0, const TCHAR* Title, const TArray<FInvSlot>& Items, int32 Ingots, bool bMine, bool bAccepted)
	{
		HDText(Title, bMine ? Gold : FLinearColor(0.7f, 0.85f, 1.f), X0, PY + 66.f, Font, 1.0f);
		const float W = 4 * (SS + G) - G;
		Tile(X0 - 6.f, GY - 6.f, W + 12.f, 3 * (SS + G) + 64.f, bAccepted ? FLinearColor(0.3f, 0.9f, 0.4f) : FLinearColor(0.5f, 0.55f, 0.65f), bAccepted ? 0.3f : 0.12f, false);
		for (int32 k = 0; k < 12; ++k)
		{
			const float X = X0 + (k % 4) * (SS + G), Y = GY + (k / 4) * (SS + G);
			Tile(X, Y, SS, SS, FLinearColor(0.6f, 0.72f, 0.88f), 0.16f);
			if (!Items.IsValidIndex(k)) continue;
			const FInvSlot& S = Items[k];
			DrawIconIn(S.Id, X + 6.f, Y + 5.f, SS - 12.f, X, Y, SS, SS);
			if (S.Count > 1) HDText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, X + SS - 26.f, Y + SS - 20.f, Font, 0.85f);
			if (Over(X, Y, SS, SS))
			{
				ItemTip(S.Id, S.Size);
				if (bMine && bUIClick && !bLocked && V.MineSlots.IsValidIndex(k)) Knight->ServerTradeOffer(V.MineSlots[k], false);
			}
		}
		const float IY = GY + 3 * (SS + G) + 8.f;
		DrawIngot(X0, IY + 2.f, 28.f);
		if (bMine && !bLocked)
		{
			if (MktField != 3) TradeIngotText = Ingots > 0 ? FString::FromInt(Ingots) : FString();
			MktBox(X0 + 36.f, IY, W - 36.f, 3, TradeIngotText, TEXT("Ingots: click, type, Enter"));
		}
		else HDText(FString::Printf(TEXT("%d ingots"), Ingots), Gold, X0 + 36.f, IY + 6.f, Font, 1.0f);
		if (bAccepted) HDText(bMine ? TEXT("You accepted") : TEXT("They accepted"), Good, X0, IY + 44.f, Font, 0.95f);
	};
	Offer(PX + 330.f, TEXT("YOU GIVE"), V.Mine, V.MyIngots, true, V.bIAccepted);
	Offer(PX + 330.f + 4 * (SS + G) + 50.f, *FString::Printf(TEXT("%s GIVES"), *V.Partner.ToUpper()), V.Theirs, V.TheirIngots, false, V.bTheyAccepted);

	// ----- status and buttons
	const float BY = PY + PH - 120.f;
	FString Status;
	if (!bLocked) Status = V.bIAccepted ? FString::Printf(TEXT("Waiting for %s to accept."), *V.Partner) : V.bTheyAccepted ? FString::Printf(TEXT("%s has accepted. Accept if you are happy."), *V.Partner) : FString(TEXT("Offer things from your pack, then both ACCEPT. Any change clears both accepts."));
	else Status = V.bIAccepted ? FString::Printf(TEXT("Waiting for %s to confirm."), *V.Partner) : FString(TEXT("Last look: the offers are locked. CONFIRM to make the trade."));
	HDText(Status, FLinearColor::White, PX + 30.f, BY, Font, 1.0f);
	if (bLocked)
	{
		auto Sum = [](const TArray<FInvSlot>& Items, int32 Ing) { TArray<FString> P; for (const FInvSlot& S : Items) P.Add(FString::Printf(TEXT("%s x%d"), *WithSize(S), S.Count)); if (Ing > 0) P.Add(FString::Printf(TEXT("%d ingots"), Ing)); return P.Num() ? FString::Join(P, TEXT(", ")) : FString(TEXT("nothing")); };
		Wrapped(TEXT("You give: ") + Sum(V.Mine, V.MyIngots), PX + 30.f, BY + 26.f, PW - 330.f, 0.85f, Gold);
		Wrapped(TEXT("You get: ") + Sum(V.Theirs, V.TheirIngots), PX + 30.f, BY + 52.f, PW - 330.f, 0.85f, FLinearColor(0.7f, 0.85f, 1.f));
	}
	if (Btn(PX + PW - 290.f, PY + PH - 64.f, 140.f, 40.f, bLocked ? TEXT("CONFIRM") : TEXT("ACCEPT"), !V.bIAccepted, false, 1.1f)) Knight->ServerTradeAccept(V.Version);
	if (Btn(PX + PW - 140.f, PY + PH - 64.f, 110.f, 40.f, TEXT("CANCEL"), true, false, 1.1f)) Knight->ServerTradeCancel();
}

// Right-click another player while the cursor is out (Tab): ask them to trade
void AKnightHUD::TradeRightClick(AKnightCharacter* Knight, APlayerController* PC)
{
	if (!bUIRightClick || !PC || !Knight->IsPanelCursor() || Knight->IsUIOpen() || !bUIHaveMouse) return;
	FVector O, D;
	if (!PC->DeprojectScreenPositionToWorld(UIMouseX, UIMouseY, O, D)) return;
	AKnightCharacter* Best = nullptr;
	float BestOff = 80.f;
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == Knight || !It->GetPlayerState()) continue;
		const FVector P = It->GetActorLocation();
		if (FVector::Dist(P, Knight->GetActorLocation()) > 450.f) continue;
		const float Along = FVector::DotProduct(P - O, D);
		if (Along < 0.f) continue;
		const float Off = FVector::Dist(O + D * Along, P);
		if (Off < BestOff) { BestOff = Off; Best = *It; }
	}
	if (Best) Knight->AskTrade(Best);
}

// ------------------------------------------------------------------------------------------------ the bank's MARKET tab
void AKnightHUD::MarketAsk(AKnightCharacter* Knight)
{
	MktAskAt = FPlatformTime::Seconds();
	Knight->ServerMarketQuery(MktCategory, MktSearchText, MktPage, MktMode == 1);
}

void AKnightHUD::DrawMarketTab(AKnightCharacter* Knight, float PX, float PY, float PW, float PH)
{
	USkillComponent* Sk = Knight->GetSkills();
	UFont* Font = GEngine->GetMediumFont();
	const double Now = FPlatformTime::Seconds();
	if (Now - MktAskAt > 5.0) MarketAsk(Knight);   // (kept fresh while it is open: other worlds sell things too)

	// modes
	static const TCHAR* Modes[3] = { TEXT("BROWSE"), TEXT("MY LISTINGS"), TEXT("SELL") };
	for (int32 m = 0; m < 3; ++m)
		if (Btn(PX + 30.f + m * 160.f, PY + 62.f, 150.f, 34.f, Modes[m], true, MktMode == m, 1.0f) && MktMode != m) { MktMode = m; MktPage = 0; MarketAsk(Knight); }
	{
		const FString Bal = FString::Printf(TEXT("%d"), Sk->GetIngots());
		float TW = 0.f, TH = 0.f;
		HDTextSize(Bal, TW, TH, Font, 1.3f);
		HDText(Bal, Gold, PX + PW - 90.f - TW, PY + 66.f, Font, 1.3f);
		DrawIngot(PX + PW - 82.f, PY + 62.f, 32.f);
	}
	const float TopY = PY + 112.f;

	if (MktMode == 2)
	{
		// ----- sell: pick a pack item, type a price, list it
		HDText(TEXT("YOUR PACK  (click the thing to sell; the whole stack goes)"), Gold, PX + 30.f, TopY, Font, 1.0f);
		const float SS = 66.f, G = 6.f, GX = PX + 30.f, GY = TopY + 30.f;
		for (int32 i = 0; i < NumSlots && i < Sk->Slots.Num(); ++i)
		{
			const float X = GX + (i % 6) * (SS + G), Y = GY + (i / 6) * (SS + G);
			const FInvSlot S = Sk->Slots[i];
			const bool bOver = Over(X, Y, SS, SS);
			Tile(X, Y, SS, SS, MktSellSlot == i ? FLinearColor(1.f, 0.85f, 0.4f) : bOver ? FLinearColor(0.85f, 0.93f, 1.f) : FLinearColor(0.6f, 0.72f, 0.88f), MktSellSlot == i ? 0.5f : bOver ? 0.36f : 0.2f);
			if (S.Id == 0) continue;
			DrawIconIn(S.Id, X + 6.f, Y + 5.f, SS - 12.f, X, Y, SS, SS);
			if (StackSize(S.Id) > 1) HDText(FString::Printf(TEXT("%d"), S.Count), FLinearColor::White, X + SS - 26.f, Y + SS - 20.f, Font, 0.85f);
			if (bOver) { ItemTip(S.Id, S.Size); if (bUIClick) MktSellSlot = i; }
		}
		const float RX = GX + 6 * (SS + G) + 30.f, RW = PX + PW - 30.f - RX;
		if (Sk->Slots.IsValidIndex(MktSellSlot) && Sk->Slots[MktSellSlot].Id != 0)
		{
			const FInvSlot S = Sk->Slots[MktSellSlot];
			DrawIconIn(S.Id, RX, GY, 80.f, RX, GY, 80.f, 80.f);
			Wrapped(FString::Printf(TEXT("%s%s"), *WithSize(S), S.Count > 1 ? *FString::Printf(TEXT("  x%d"), S.Count) : TEXT("")), RX + 92.f, GY + 6.f, RW - 92.f, 1.0f, FLinearColor::White);
			HDText(TEXT("Price for all of it:"), Grey, RX, GY + 100.f, Font, 0.95f);
			MktBox(RX, GY + 126.f, 200.f, 2, MktPriceText, TEXT("click, type a price"));
			const int32 Price = FCString::Atoi(*MktPriceText);
			if (Price > 0) HDText(FString::Printf(TEXT("You get %d when it sells (%d%% tax: %d)."), Price - JeebMarket::Tax(Price), JeebMarket::TaxPercent, JeebMarket::Tax(Price)), Grey, RX, GY + 170.f, Font, 0.9f);
			if (Btn(RX, GY + 204.f, 240.f, 40.f, Price > 0 ? *FString::Printf(TEXT("LIST FOR %d"), Price) : TEXT("LIST"), Price > 0, false, 1.1f))
			{
				Knight->ServerMarketList(MktSellSlot, S.Id, Price);
				MktSellSlot = -1; MktPriceText.Reset();
			}
		}
		else HDText(TEXT("Pick something from your pack."), Grey, RX, GY, Font, 1.0f);
		HDText(FString::Printf(TEXT("Up to %d things for sale at once. Buyers on every world see them. You are paid even when you are away."), JeebMarket::MaxListings), Grey, PX + 30.f, PY + PH - 70.f, Font, 0.9f);
	}
	else
	{
		// ----- browse (categories and a search) or my listings
		float ListX = PX + 30.f;
		if (MktMode == 0)
		{
			for (int32 c = 0; c < JeebMarket::C_Num; ++c)
				if (Btn(PX + 30.f, TopY + 40.f + c * 40.f, 170.f, 34.f, JeebMarket::CategoryName(c), true, MktCategory == c, 0.9f) && MktCategory != c) { MktCategory = c; MktPage = 0; MarketAsk(Knight); }
			ListX = PX + 220.f;
			MktBox(ListX, TopY, 300.f, 1, MktSearchText, TEXT("Search: click, type, Enter"));
			if (!MktSearchText.IsEmpty() && Btn(ListX + 310.f, TopY, 80.f, 34.f, TEXT("CLEAR"), true, false, 0.9f)) { MktSearchText.Reset(); MktPage = 0; MarketAsk(Knight); }
		}
		const bool bReady = Knight->bMarketMine == (MktMode == 1);
		const float RowH = 34.f, LY = TopY + 44.f, LW = PX + PW - 30.f - ListX;
		HDText(TEXT("ITEM"), Grey, ListX + 44.f, LY - 2.f, Font, 0.8f);
		HDText(TEXT("PRICE"), Grey, ListX + LW - 380.f, LY - 2.f, Font, 0.8f);
		HDText(TEXT("SELLER"), Grey, ListX + LW - 250.f, LY - 2.f, Font, 0.8f);
		const TArray<FMarketRow>& Rows = Knight->MarketRows;
		if (!bReady) HDText(TEXT("Looking..."), Grey, ListX, LY + 24.f, Font, 1.0f);
		else if (Rows.Num() == 0) HDText(MktMode == 1 ? TEXT("You have nothing for sale. Use SELL to list something.") : TEXT("Nothing for sale here yet."), Grey, ListX, LY + 24.f, Font, 1.0f);
		for (int32 r = 0; bReady && r < Rows.Num(); ++r)
		{
			const FMarketRow& M = Rows[r];
			const float Y = LY + 20.f + r * (RowH + 4.f);
			const bool bOver = Over(ListX, Y, LW, RowH);
			Tile(ListX, Y, LW, RowH, bOver ? FLinearColor(0.85f, 0.93f, 1.f) : FLinearColor(0.6f, 0.72f, 0.88f), bOver ? 0.3f : 0.16f, false);
			DrawIcon(M.ItemId, ListX + 4.f, Y + 2.f, 32.f);
			const FInvSlot S(M.ItemId, M.Count, M.Size);
			HDText(FString::Printf(TEXT("%s%s"), *WithSize(S), M.Count > 1 ? *FString::Printf(TEXT("  x%d"), M.Count) : TEXT("")), FLinearColor::White, ListX + 44.f, Y + 8.f, Font, 0.9f);
			DrawIngot(ListX + LW - 380.f, Y + 6.f, 22.f);
			HDText(FString::FromInt(M.Price), Sk->GetIngots() >= M.Price || M.bMine ? Gold : FLinearColor(0.9f, 0.3f, 0.25f), ListX + LW - 352.f, Y + 8.f, Font, 0.95f);
			HDText(M.bMine ? FString(TEXT("you")) : M.Seller.Left(16), Grey, ListX + LW - 250.f, Y + 8.f, Font, 0.9f);
			if (M.bMine) { if (Btn(ListX + LW - 104.f, Y + 2.f, 100.f, RowH - 4.f, TEXT("CANCEL"), true, false, 0.9f)) Knight->ServerMarketCancel(M.Id); }
			else if (Btn(ListX + LW - 104.f, Y + 2.f, 100.f, RowH - 4.f, TEXT("BUY"), Sk->GetIngots() >= M.Price, false, 0.9f)) Knight->ServerMarketBuy(M.Id, M.Price);
			if (bOver && !Over(ListX + LW - 104.f, Y, 104.f, RowH)) ItemTip(M.ItemId, M.Size);
		}
		const int32 Pages = FMath::Max(1, (Knight->MarketTotal + JeebMarket::PageRows - 1) / JeebMarket::PageRows);
		const float PgY = LY + 20.f + JeebMarket::PageRows * (RowH + 4.f) + 4.f;
		if (Btn(ListX, PgY, 90.f, 32.f, TEXT("< PREV"), MktPage > 0, false, 1.0f)) { MktPage = FMath::Max(0, MktPage - 1); MarketAsk(Knight); }
		if (Btn(ListX + 100.f, PgY, 90.f, 32.f, TEXT("NEXT >"), MktPage < Pages - 1, false, 1.0f)) { MktPage = FMath::Min(Pages - 1, MktPage + 1); MarketAsk(Knight); }
		HDText(FString::Printf(TEXT("Page %d / %d   %d for sale"), MktPage + 1, Pages, Knight->MarketTotal), Grey, ListX + 210.f, PgY + 6.f, Font, 0.95f);
	}
	if (GetWorld()->GetTimeSeconds() < ShopMsgUntil) HDText(ShopMsg, Good, PX + 30.f, PY + PH - 34.f, Font, 1.0f);
	else HDText(TEXT("The Market: every world's players buy and sell here. Press E to close."), FLinearColor(0.7f, 0.7f, 0.7f), PX + 30.f, PY + PH - 34.f, Font, 1.0f);
}
