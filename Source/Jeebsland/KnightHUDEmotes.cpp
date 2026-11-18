#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "JeebEmotes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// ---------------------------------------------------------------------------------------------------
// The emote wheel (2026-11-18; JeebEmotes.h, KnightCharacterEmote.cpp). B opens it in the middle of the screen with the cursor out:
// eight liquid-glass buttons round a small glass disc. A click plays that emote (again on a looping one stops it) and closes the
// wheel; B, a right-click, a click outside it, or any screen opening closes it too. Chat does the same: /wave, /bow, /cheer, /clap,
// /dance, /sit, /point, /salute; /emotes lists them.
// ---------------------------------------------------------------------------------------------------

bool AKnightHUD::EmoteChat(const FString& Cmd)
{
	const FString Name = Cmd.Mid(1);
	if (Name == TEXT("emotes") || Name == TEXT("emote"))
	{
		ChatNote(FString::Printf(TEXT("Emotes: %s (again to stop the dance or the sit), or press B for the wheel. Moving ends one."), *JeebEmotes::CommandList()));
		return true;
	}
	const int32 Id = JeebEmotes::FindByName(Name);
	if (!Id) return false;
	if (AKnightCharacter* K = Cast<AKnightCharacter>(GetOwningPawn())) K->RequestEmote(Id);
	return true;
}

void AKnightHUD::CloseEmoteWheel(AKnightCharacter* Knight)
{
	if (!bEmoteWheel) return;
	bEmoteWheel = false;
	if (bEmoteCursor && Knight) Knight->SetPanelCursor(false);
	bEmoteCursor = false;
}

void AKnightHUD::DrawEmoteWheel(AKnightCharacter* Knight, APlayerController* PC)
{
	if (!Canvas || !GEngine || !PC || !Knight) return;
	const bool bTyping = bChatOpen || bLevelTyping || bLookTyping || bDropOpen;
	if (!bEmoteWheel)
	{
		bool bShotOpen = false;
#if !UE_BUILD_SHIPPING
		static const bool bShotWheel = FParse::Param(FCommandLine::Get(), TEXT("JeebShot")) && FParse::Param(FCommandLine::Get(), TEXT("ShotEmoteWheel"));   // (test pictures: the wheel open)
		static bool bShotWheelDone = false;
		if (bShotWheel && !bShotWheelDone && GetWorld()->GetTimeSeconds() > 8.f) { bShotWheelDone = true; bShotOpen = true; }
#endif
		if (bShotOpen || (!bTyping && !Knight->IsUIOpen() && PC->WasInputKeyJustPressed(EKeys::B)))
		{
			bEmoteWheel = true;
			bEmoteCursor = !Knight->IsPanelCursor();   // (put the cursor away again on closing only if the wheel brought it out)
			if (bEmoteCursor) Knight->SetPanelCursor(true);
		}
		return;
	}
	if (Knight->IsUIOpen() || bTyping || (PC->WasInputKeyJustPressed(EKeys::B)) || bUIRightClick || (bEmoteCursor && !Knight->IsPanelCursor()))
	{
		CloseEmoteWheel(Knight);
		return;
	}

	UFont* Font = GEngine->GetMediumFont();
	const float S = FMath::Clamp(Canvas->ClipY / 1080.f, 0.7f, 2.f);
	const float Cx = Canvas->ClipX * 0.5f, Cy = Canvas->ClipY * 0.5f;
	const float R = 165.f * S, BW = 128.f * S, BH = 44.f * S, Disc = 118.f * S;
	const int32 N = JeebEmotes::Num() - 1;

	// the disc in the middle: what it is and how to close it
	Panel(Cx - Disc * 0.5f, Cy - Disc * 0.5f, Disc, Disc, 0.95f);
	{
		float W = 0.f, H = 0.f;
		const FString T = TEXT("EMOTES");
		HDTextSize(T, W, H, Font, 1.15f * S);
		HDText(T, FLinearColor(1.f, 0.9f, 0.6f), Cx - W * 0.5f, Cy - H - 2.f * S, Font, 1.15f * S);
		const FString Hint = TEXT("B to close");
		HDTextSize(Hint, W, H, Font, 0.8f * S);
		HDText(Hint, FLinearColor(0.8f, 0.85f, 0.9f, 0.85f), Cx - W * 0.5f, Cy + 4.f * S, Font, 0.8f * S);
	}

	bool bHit = false;
	for (int32 i = 0; i < N; ++i)
	{
		const JeebEmotes::FDef* D = JeebEmotes::Get(i + 1);
		const float A = -PI * 0.5f + 2.f * PI * i / N;   // (the first at the top, then round clockwise)
		const float X = Cx + FMath::Cos(A) * R - BW * 0.5f, Y = Cy + FMath::Sin(A) * R * 0.86f - BH * 0.5f;
		const bool bOn = Knight->EmoteNow() == i + 1;
		if (Over(X, Y, BW, BH)) bHit = true;
		if (Btn(X, Y, BW, BH, D->Label, true, bOn, 1.05f * S))
		{
			Knight->RequestEmote(i + 1);
			CloseEmoteWheel(Knight);
			return;
		}
	}
	// a click anywhere else closes it
	if (bUIClick && !bHit && !Over(Cx - Disc * 0.5f, Cy - Disc * 0.5f, Disc, Disc)) CloseEmoteWheel(Knight);
}
