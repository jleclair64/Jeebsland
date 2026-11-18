// Fast travel on the screen (2026-11-18): the travel markers on the world map, the "Travel to Oak Hill?" box, the count while he stands
// still, the "DISCOVERED" title and the fade to black round the jump. It only reads the knight's travel state (UTravelComponent, which the
// server sends to its owner) and asks (UTravelComponent::Ask); the server decides everything (FastTravel.cpp).
#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "FastTravel.h"
#include "JeebTuning.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"

namespace
{
	const FLinearColor TGold(1.f, 0.84f, 0.32f);
	// where a world point (cm) is on the map now
	FVector2D MapPoint(const FVector2D& P, const FVector2D& View, float Size, float Zoom, float X, float Y, float S)
	{
		const float Span = Size / Zoom;
		return FVector2D(X + S * 0.5f + (P.Y - View.Y) / Span * S, Y + S * 0.5f - (P.X - View.X) / Span * S);
	}
}

// The visited town whose marker is under this screen point (within 11 px), or -1
int32 AKnightHUD::TravelMarkerAt(AKnightCharacter* Knight, float X, float Y, float S, const FVector2D& Mouse) const
{
	const UTravelComponent* Tv = Knight ? Knight->GetTravel() : nullptr;
	if (!Tv) return -1;
	int32 Best = -1;
	float BestD = 11.f;
	for (int32 i = 0; i < JeebTravel::Count(); ++i)
	{
		if (!Tv->IsVisited(i) || !JeebTravel::Towns()[i].bOpen) continue;
		const FVector2D At = MapPoint(JeebTravel::Centre(i), MapView, MapSize, MapZoom, X, Y, S);
		if (At.X < X || At.X > X + S || At.Y < Y || At.Y > Y + S) continue;
		const float D = FVector2D::Distance(At, Mouse);
		if (D < BestD) { Best = i; BestD = D; }
	}
	return Best;
}

// A gold ring on every visited town; the one under the cursor lights up and says it can be travelled to
void AKnightHUD::DrawTravelMarkers(AKnightCharacter* Knight, float X, float Y, float S, bool bLive)
{
	UTravelComponent* Tv = Knight ? Knight->GetTravel() : nullptr;
	if (!Tv) return;
	TravelMapFrame = GFrameCounter;
	const FVector2D Mouse(UIMouseX, UIMouseY);
	const int32 Hot = (bLive && TravelAsk < 0) ? TravelMarkerAt(Knight, X, Y, S, Mouse) : -1;
	const int32 Here = JeebTravel::TownAt(FVector2D(Knight->GetActorLocation()));
	for (int32 i = 0; i < JeebTravel::Count(); ++i)
	{
		if (!Tv->IsVisited(i) || !JeebTravel::Towns()[i].bOpen) continue;
		const FVector2D At = MapPoint(JeebTravel::Centre(i), MapView, MapSize, MapZoom, X, Y, S);
		if (At.X < X + 4.f || At.X > X + S - 4.f || At.Y < Y + 4.f || At.Y > Y + S - 4.f) continue;
		const bool bHot = i == Hot, bHere = i == Here;
		const float R = bHot ? 9.5f : 7.5f;
		DrawCircle(At.X, At.Y, R + 1.5f, FLinearColor(0.f, 0.f, 0.f, 0.7f));
		DrawAnnulus(At.X, At.Y, R, bHere ? FLinearColor(0.8f, 0.85f, 0.95f) : TGold);
		DrawCircle(At.X, At.Y, bHot ? 4.5f : 3.f, bHere ? FLinearColor(0.8f, 0.85f, 0.95f) : TGold);
		if (bHot) SetTip({ FString(JeebTravel::Towns()[i].Name), bHere ? FString(TEXT("You are here")) : FString(TEXT("Click to fast travel")) });
	}
}

// The "Travel to Oak Hill?" box, over everything else on the screen
void AKnightHUD::DrawTravelConfirm(AKnightCharacter* Knight)
{
	if (TravelAsk < 0) return;
	UTravelComponent* Tv = Knight ? Knight->GetTravel() : nullptr;
	if (!Tv || TravelAsk >= JeebTravel::Count() || !Tv->IsVisited(TravelAsk)) { TravelAsk = -1; return; }
	UFont* Font = GEngine->GetMediumFont();
	const float W = 460.f, H = 214.f, X = (Canvas->ClipX - W) * 0.5f, Y = (Canvas->ClipY - H) * 0.5f;
	bGreyGlass = true;
	FillRR(X, Y, W, H, 26.f, [&](float) { return FLinearColor(0.05f, 0.05f, 0.055f, 0.96f); });
	Panel(X, Y, W, H);
	bGreyGlass = false;
	AddWindowRect(X, Y, W, H);
	const FString Title = FString::Printf(TEXT("Travel to %s?"), JeebTravel::Towns()[TravelAsk].Name);
	float TW = 0.f, TH = 0.f;
	HDTextSize(Title, TW, TH, Font, 1.5f);
	HDText(Title, FLinearColor(1.f, 0.9f, 0.6f), X + (W - TW) * 0.5f, Y + 20.f, Font, 1.5f);
	const FString Sub = FString::Printf(TEXT("You stand still for %d seconds, then jump. Moving, fighting or a blow cancels it."), FMath::RoundToInt(JeebTune::V().TravelChannel));
	Wrapped(Sub, X + 34.f, Y + 74.f, W - 68.f, 1.0f, FLinearColor(0.78f, 0.8f, 0.88f));
	const float BY = Y + H - 62.f, BW = 150.f, BH = 40.f;
	const bool bGo = Btn(X + 40.f, BY, BW, BH, TEXT("Travel"), true, true, 1.2f);
	const bool bNo = Btn(X + W - 40.f - BW, BY, BW, BH, TEXT("Cancel"), true, false, 1.2f);
	APlayerController* PC = GetOwningPlayerController();
	if (bNo || (PC && PC->WasInputKeyJustPressed(EKeys::BackSpace))) { TravelAsk = -1; return; }
	if (bGo || (PC && (PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::SpaceBar))))
	{
		const int32 Town = TravelAsk;
		TravelAsk = -1;
		Tv->Ask(Town);
		// (out of the screens, so he stands still and the count shows)
		if (Knight->IsSettingsOpen()) Knight->ToggleSettings();
		else if (Knight->IsInventoryOpen()) Knight->ToggleInventory();
	}
}

// The count while he stands still, the "DISCOVERED" title (called with the other title lines)
void AKnightHUD::DrawTravelHUD(AKnightCharacter* Knight, APlayerController* PC)
{
	UTravelComponent* Tv = Knight ? Knight->GetTravel() : nullptr;
	if (!Tv) return;
	if (TravelAsk >= 0 && GFrameCounter > TravelMapFrame + 2) TravelAsk = -1;   // (the map is gone from the screen: so is the question)
	UFont* Font = GEngine->GetMediumFont();
	{
		const float Age = float(FPlatformTime::Seconds() - Tv->DiscoveredTime);
		if (Age >= 0.f && Age < 5.f && !Tv->DiscoveredTitle.IsEmpty())
		{
			const float A = FMath::Clamp(FMath::Min(Age / 0.5f, (5.f - Age) / 1.2f), 0.f, 1.f);
			const float H = FMath::Clamp(Canvas->ClipY * 0.042f, 26.f, 54.f);
			TitleBanner(TEXT("DISCOVERED"), Canvas->ClipY * 0.27f, H, A);
			TitleTextAt(Tv->DiscoveredTitle, -1.f, Canvas->ClipY * 0.27f + H * 1.55f, H * 0.7f, A * 0.95f);
		}
	}
	int32 Town = -1;
	float Left = 0.f;
	if (Tv->Channelling(Town, Left) && Town < JeebTravel::Count())
	{
		const FString T = FString::Printf(TEXT("Travelling to %s...  %d"), JeebTravel::Towns()[Town].Name, FMath::Max(1, FMath::CeilToInt(Left)));
		float TW = 0.f, TH = 0.f;
		HDTextSize(T, TW, TH, Font, 1.7f);
		const float CX = Canvas->ClipX * 0.5f, CY = Canvas->ClipY * 0.3f;
		Tile(CX - TW * 0.5f - 22.f, CY - 10.f, TW + 44.f, TH + 20.f, FLinearColor(0.05f, 0.07f, 0.12f), 0.55f);
		HDText(T, TGold, CX - TW * 0.5f, CY, Font, 1.7f);
	}
}

// The black over the whole screen round the jump; called last, over everything
void AKnightHUD::DrawTravelFade(AKnightCharacter* Knight)
{
	UTravelComponent* Tv = Knight ? Knight->GetTravel() : nullptr;
	if (!Tv) return;
	const float A = Tv->TickFade(FMath::Min(GetWorld()->GetDeltaSeconds(), 0.1f));
	if (A > 0.003f) DrawRect(FLinearColor(0.f, 0.f, 0.f, FMath::Clamp(A, 0.f, 1.f)), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
}
