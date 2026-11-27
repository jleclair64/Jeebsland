// The stone circles' noon event on the screen (owner, 2026-11-26; StoneCircle.h, Docs/StoneCirclePlan.md). Every machine works it out
// from the shared server clock, so every player sees the same thing at the same moment:
//  - at the game day's dawn: a title "THE <CIRCLE> STIRS" with the zone under it, and a line in the messages; then a small reminder line
//    at the top of the screen until the countdown;
//  - from CountdownSeconds before high noon: a countdown at the top of the screen;
//  - at high noon: "THE <CIRCLE> HAS WOKEN" for a few seconds.
#include "KnightHUD.h"
#include "StoneCircle.h"
#include "Messages.h"
#include "Engine/Canvas.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AKnightHUD::DrawStones()
{
	JeebStones::FEvent E;
	double T = 0.0;
	if (!JeebStones::EventNow(GetWorld(), E, &T) || !JeebStones::Circles().IsValidIndex(E.Circle)) return;
	const JeebStones::FCircle& C = JeebStones::Circles()[E.Circle];
	const float Now = GetWorld()->GetTimeSeconds();
	static const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("ShotStoneHud"));   // (test pictures: the dawn title stays up)
	const FString Name = C.Name.ToUpper();
	const FLinearColor Teal(0.55f, 0.95f, 0.9f, 1.f), Ice(0.7f, 0.86f, 1.f, 1.f);
	const FLinearColor Tint = C.bFrost ? Ice : Teal;
	const float H = FMath::Clamp(Canvas->ClipY * 0.044f, 26.f, 56.f);

	if (StoneToldNoon != E.Noon && T >= E.Dawn && T < E.Noon)
	{
		StoneToldNoon = E.Noon; StoneToldT = Now;
		JeebMsg::Post(12.f, FColor(150, 240, 225), FString::Printf(TEXT("%s stirs in %s: it wakes at high noon. The first to take its gift keeps it."), *C.Name, *C.Zone));
		UE_LOG(LogTemp, Log, TEXT("JEEBSTONE hud: announced %s at server %.2f (noon %.1f)%s"), *C.Name, T, E.Noon, E.bTest ? TEXT(" test") : TEXT(""));
	}
	if (StoneWokeNoon != E.Noon && T >= E.Noon && T < E.End)
	{
		StoneWokeNoon = E.Noon; StoneWokeT = Now;
		JeebMsg::Post(10.f, FColor(150, 240, 225), FString::Printf(TEXT("%s has woken. Its gift lies on the centre stone."), *C.Name));
		UE_LOG(LogTemp, Log, TEXT("JEEBSTONE hud: woke %s at server %.2f (noon %.1f)"), *C.Name, T, E.Noon);
	}

	// the dawn title (6 s)
	const float Told = Now - StoneToldT;
	if (StoneToldNoon == E.Noon && (Told < 6.5f || bShot) && T < E.CountFrom)
	{
		const float A = bShot ? 1.f : FMath::Clamp(FMath::Min(Told / 0.8f, (6.5f - Told) / 1.4f), 0.f, 1.f);
		TitleTint = Tint;
		TitleBanner(FString::Printf(TEXT("%s STIRS"), *Name), Canvas->ClipY * 0.22f, H, A);
		TitleTextAt(FString::Printf(TEXT("IT WAKES AT HIGH NOON IN %s"), *C.Zone.ToUpper()), -1.f, Canvas->ClipY * 0.22f + H * 1.75f, H * 0.42f, A);
		TitleTint.Reset();
		return;
	}
	// the reminder line until the countdown
	if (T < E.CountFrom)
	{
		TitleTint = Tint;
		TitleTextAt(FString::Printf(TEXT("%s WAKES AT HIGH NOON  -  %s"), *Name, *C.Zone.ToUpper()), -1.f, 84.f, 15.f, 0.85f);
		TitleTint.Reset();
		return;
	}
	// the countdown
	if (T < E.Noon)
	{
		const int32 Left = FMath::CeilToInt(float(E.Noon - T));
		if (Left != StoneCountLog && (Left % 30 == 0 || Left <= 3))
		{
			StoneCountLog = Left;
			UE_LOG(LogTemp, Log, TEXT("JEEBSTONE hud: countdown %d s at server %.2f"), Left, T);
		}
		const float Pulse = Left <= 10 ? 0.75f + 0.25f * FMath::Sin(Now * 8.f) : 1.f;
		TitleTint = Tint;
		TitleTextAt(FString::Printf(TEXT("%s WAKES IN %d:%02d"), *Name, Left / 60, Left % 60), -1.f, 80.f, FMath::Clamp(Canvas->ClipY * 0.03f, 20.f, 38.f), Pulse);
		TitleTextAt(C.Zone.ToUpper(), -1.f, 80.f + FMath::Clamp(Canvas->ClipY * 0.03f, 20.f, 38.f) * 1.6f, 14.f, 0.8f);
		TitleTint.Reset();
		return;
	}
	// the wake (6 s)
	const float Woke = Now - StoneWokeT;
	if (StoneWokeNoon == E.Noon && Woke < 6.5f)
	{
		const float A = FMath::Clamp(FMath::Min(Woke / 0.5f, (6.5f - Woke) / 1.4f), 0.f, 1.f);
		TitleTint = Tint;
		TitleBanner(FString::Printf(TEXT("%s HAS WOKEN"), *Name), Canvas->ClipY * 0.22f, H, A);
		TitleTextAt(TEXT("ITS GIFT LIES ON THE CENTRE STONE"), -1.f, Canvas->ClipY * 0.22f + H * 1.75f, H * 0.42f, A);
		TitleTint.Reset();
	}
}
