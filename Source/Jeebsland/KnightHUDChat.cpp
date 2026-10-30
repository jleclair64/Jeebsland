#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "KnightPlayerController.h"
#include "JeebChat.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"

// ---------------------------------------------------------------------------------------------------
// Player chat. Enter opens the typing line (lower left), Enter sends, Escape cancels.
//   hello                  said aloud: players within 35 m hear it, and it shows over your head
//   /w hello  (/world)     said to the whole world
//   /mute Name  /unmute Name  /mutelist    hide (or show again) what one player says; remembered on this machine
//   /help                  these commands
// The server cleans, filters and relays everything (AKnightPlayerController::ServerChat).
// ---------------------------------------------------------------------------------------------------
namespace
{
	constexpr float LogW = 520.f;        // width of the chat box
	constexpr int32 MaxShown = 9;        // lines shown at once
	constexpr float FadeAfter = 14.f;    // seconds a line stays up when the typing line is closed
	constexpr float ChatScale = 1.0f;
}

void AKnightHUD::WrapChat(const FString& Text, float MaxW, float S, TArray<FString>& Out) const
{
	UFont* Font = GEngine->GetMediumFont();
	TArray<FString> Words;
	Text.ParseIntoArray(Words, TEXT(" "), true);
	FString Line;
	for (const FString& W : Words)
	{
		const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
		float TW = 0.f, TH = 0.f;
		HDTextSize(Try, TW, TH, Font, S);
		if (TW > MaxW && !Line.IsEmpty()) { Out.Add(Line); Line = W; }
		else Line = Try;
	}
	if (!Line.IsEmpty()) Out.Add(Line);
}

void AKnightHUD::AddChatLine(const FString& From, const FString& Text, bool bWorld, AActor* Speaker)
{
	if (!From.IsEmpty() && JeebChat::LocalMuted(From)) return;   // a player you muted
	FChatLine L;
	L.From = From; L.Text = Text; L.bWorld = bWorld;
	L.Born = FPlatformTime::Seconds();
	ChatLog.Add(L);
	if (ChatLog.Num() > 60) ChatLog.RemoveAt(0, ChatLog.Num() - 60);
	// what is said aloud also shows over the speaker's head for a few seconds
	if (!From.IsEmpty() && !bWorld && Speaker)
	{
		ChatBubbles.RemoveAll([&](const FChatBubble& B) { return B.Who.Get() == Speaker || !B.Who.IsValid(); });
		FChatBubble B;
		B.Who = Speaker; B.Text = Text; B.Until = L.Born + 4.0 + Text.Len() * 0.06;
		ChatBubbles.Add(B);
	}
}

void AKnightHUD::OpenChat()
{
	bChatOpen = true;
	ChatText.Reset();
	EnsureTyping();
}

void AKnightHUD::CloseChat()
{
	bChatOpen = false;
	ChatText.Reset();
	HideTyping();
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

void AKnightHUD::ChatChar(TCHAR C)
{
	if (C < 32 || C == 127) return;   // (Enter, Backspace and Escape come through ChatKey)
	if (ChatText.Len() < JeebChat::MaxLength) ChatText.AppendChar(C);
}

bool AKnightHUD::ChatKey(const FKey& Key)
{
	if (Key == EKeys::BackSpace) { if (ChatText.Len() > 0) ChatText.LeftChopInline(1); }
	else if (Key == EKeys::Enter) SubmitChat();
	else if (Key == EKeys::Escape) CloseChat();
	return true;   // (while typing, no key reaches the game)
}

void AKnightHUD::SubmitChat()
{
	const FString Said = ChatText.TrimStartAndEnd();
	CloseChat();
	if (Said.IsEmpty()) return;
	AKnightPlayerController* KPC = Cast<AKnightPlayerController>(GetOwningPlayerController());
	if (!KPC) return;

	if (Said.StartsWith(TEXT("/")))
	{
		FString Cmd = Said, Arg;
		Said.Split(TEXT(" "), &Cmd, &Arg);
		Cmd = Cmd.ToLower();
		Arg.TrimStartAndEndInline();
		if (Cmd == TEXT("/w") || Cmd == TEXT("/world"))
		{
			if (!Arg.IsEmpty()) KPC->ServerChat(Arg, true);
			else ChatNote(TEXT("Say something after /w to tell the whole world."));
		}
		else if (Cmd == TEXT("/mute") || Cmd == TEXT("/unmute"))
		{
			if (Arg.IsEmpty()) ChatNote(TEXT("Type a name, like /mute Sir Jeeb"));
			else
			{
				const bool bMute = Cmd == TEXT("/mute");
				JeebChat::SetLocalMuted(Arg, bMute);
				ChatNote(bMute ? FString::Printf(TEXT("%s is muted: you will not see what they say."), *Arg) : FString::Printf(TEXT("%s is no longer muted."), *Arg));
			}
		}
		else if (Cmd == TEXT("/mutelist"))
		{
			const TArray<FString> M = JeebChat::LocalMuteList();
			ChatNote(M.Num() == 0 ? FString(TEXT("You have not muted anyone.")) : TEXT("Muted: ") + FString::Join(M, TEXT(", ")));
		}
		else ChatNote(TEXT("Chat: just type to talk to people nearby. /w message = tell the whole world. /mute Name, /unmute Name, /mutelist."));
		return;
	}
	KPC->ServerChat(Said, false);
}

void AKnightHUD::DrawChat(AKnightCharacter* Knight, APlayerController* PC)
{
	if (!Canvas || !GEngine || !PC) return;
	UFont* Font = GEngine->GetMediumFont();
	const double Now = FPlatformTime::Seconds();

	// Enter opens the typing line (not while a window, the drop box or a conversation is up)
	if (!bChatOpen && !Knight->IsUIOpen() && !bDropOpen && PC->WasInputKeyJustPressed(EKeys::Enter)) OpenChat();
	if (bChatOpen && Knight->IsUIOpen()) CloseChat();
	if (bChatOpen) EnsureTyping();   // (a click on the screen would take the keyboard away)

	// Speech over heads
	ChatBubbles.RemoveAll([&](const FChatBubble& B) { return !B.Who.IsValid() || Now > B.Until; });
	for (const FChatBubble& B : ChatBubbles)
	{
		const AActor* Who = B.Who.Get();
		FVector2D SP;
		if (!Who || !PC->ProjectWorldLocationToScreen(Who->GetActorLocation() + FVector(0.f, 0.f, 135.f), SP)) continue;
		const float A = FMath::Clamp(float((B.Until - Now) / 0.5), 0.f, 1.f);
		TArray<FString> Lines;
		WrapChat(B.Text, 300.f, 1.0f, Lines);
		float LH = 0.f, W = 0.f;
		for (const FString& L : Lines) { float TW, TH; HDTextSize(L, TW, TH, Font, 1.0f); W = FMath::Max(W, TW); LH = FMath::Max(LH, TH); }
		const float H = Lines.Num() * (LH + 2.f);
		const float X = SP.X - W * 0.5f, Y = SP.Y - H - 12.f;
		Panel(X - 10.f, Y - 5.f, W + 20.f, H + 10.f, 0.85f * A);
		for (int32 i = 0; i < Lines.Num(); ++i)
			HDText(Lines[i], FLinearColor(1.f, 1.f, 1.f, A), X, Y + i * (LH + 2.f), Font, 1.0f);
	}

	// The chat box, lower left, just above the skill popup. Lines fade away unless the typing line is open.
	const float X0 = 24.f;
	float LineH = 0.f, DummyW = 0.f;
	HDTextSize(TEXT("Ag"), DummyW, LineH, Font, ChatScale);
	const float Step = LineH + 3.f;
	const float InputH = LineH + 18.f;
	const float BottomY = Canvas->ClipY - 150.f;   // where the newest line ends
	float Y = BottomY - (bChatOpen ? InputH + 8.f : 0.f);

	// Collect what to show, newest first, until the lines run out
	struct FShown { FString Text; FLinearColor Col; };
	TArray<FShown> Rows;   // bottom to top
	for (int32 i = ChatLog.Num() - 1; i >= 0 && Rows.Num() < MaxShown + (bChatOpen ? 4 : 0); --i)
	{
		const FChatLine& L = ChatLog[i];
		const float Age = float(Now - L.Born);
		float A = 1.f;
		if (!bChatOpen)
		{
			if (Age > FadeAfter) break;   // (older lines are older still)
			A = FMath::Clamp((FadeAfter - Age) / 3.f, 0.f, 1.f);
		}
		FLinearColor Col = L.From.IsEmpty() ? FLinearColor(1.f, 0.88f, 0.4f) : (L.bWorld ? FLinearColor(0.55f, 0.85f, 1.f) : FLinearColor(1.f, 1.f, 1.f));
		Col.A = A;
		const FString Full = L.From.IsEmpty() ? L.Text : FString::Printf(TEXT("%s%s: %s"), L.bWorld ? TEXT("[World] ") : TEXT(""), *L.From, *L.Text);
		TArray<FString> Parts;
		WrapChat(Full, LogW - 24.f, ChatScale, Parts);
		for (int32 p = Parts.Num() - 1; p >= 0; --p) Rows.Add({ Parts[p], Col });   // (a wrapped message is added last line first)
		if (Rows.Num() >= MaxShown + (bChatOpen ? 4 : 0)) break;
	}
	const int32 Count = FMath::Min(Rows.Num(), MaxShown + (bChatOpen ? 4 : 0));
	if (Count > 0)
	{
		const float H = Count * Step + 12.f;
		const float Alpha = bChatOpen ? 0.75f : FMath::Clamp(Rows[0].Col.A, 0.f, 1.f) * 0.5f;
		bGreyGlass = true;
		Panel(X0 - 8.f, Y - H + 4.f, LogW, H, Alpha);
		bGreyGlass = false;
		for (int32 r = 0; r < Count; ++r)
		{
			const float RY = Y - (r + 1) * Step;
			HDText(Rows[r].Text, FLinearColor(0.f, 0.f, 0.f, Rows[r].Col.A * 0.7f), X0 + 6.f + 1.f, RY + 1.f, Font, ChatScale);
			HDText(Rows[r].Text, Rows[r].Col, X0 + 6.f, RY, Font, ChatScale);
		}
	}

	// The typing line
	if (bChatOpen)
	{
		const float IY = BottomY - InputH;
		Tile(X0 - 8.f, IY, LogW, InputH, FLinearColor(0.95f, 0.75f, 0.35f), 0.30f);
		FString Shown = ChatText;
		if (FMath::FloorToInt(GetWorld()->GetRealTimeSeconds() * 2.f) % 2 == 0) Shown += TEXT("|");
		float TW = 0.f, TH = 0.f;
		HDTextSize(Shown, TW, TH, Font, ChatScale);
		while (TW > LogW - 36.f && Shown.Len() > 1) { Shown.RightChopInline(1); HDTextSize(Shown, TW, TH, Font, ChatScale); }   // long text shows its end
		HDText(Shown.IsEmpty() ? FString(TEXT("Say something...")) : Shown, ChatText.IsEmpty() ? FLinearColor(1.f, 1.f, 1.f, 0.4f) : FLinearColor::White, X0 + 6.f, IY + (InputH - TH) * 0.5f, Font, ChatScale);
	}
}

