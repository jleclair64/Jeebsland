#include "KnightHUD.h"
#include "JeebGameInstance.h"
#include "JeebAccounts.h"
#include "KnightPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"

// ---------------------------------------------------------------------------------------------------
// Typing: an invisible Slate widget holds the keyboard while the starting screen is up and hands every character and key to
// the HUD, which draws the fields itself (so they look like the rest of the glass UI). Letters arrive as the keyboard layout
// types them, so any keyboard works.
// ---------------------------------------------------------------------------------------------------
class SJeebTyping : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SJeebTyping) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, AKnightHUD* InHud)
	{
		Hud = InHud;
		ChildSlot[SNullWidget::NullWidget];
	}
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyChar(const FGeometry&, const FCharacterEvent& E) override
	{
		if (Hud.IsValid()) Hud->TypeChar(E.GetCharacter());
		return FReply::Handled();
	}
	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override
	{
		return (Hud.IsValid() && Hud->TypeKey(E.GetKey())) ? FReply::Handled() : FReply::Unhandled();
	}

private:
	TWeakObjectPtr<AKnightHUD> Hud;
};

void AKnightHUD::TypeChar(TCHAR C)
{
	if (C < 32 || C == 127) return;   // (control characters: Backspace, Tab and Enter come through TypeKey)
	FString* F = TypeFocus == 0 ? &TypeName : TypeFocus == 1 ? &TypePass : TypeFocus == 2 ? &TypePass2 : &TypeAddr;
	const int32 MaxLen = TypeFocus == 0 ? 12 : TypeFocus == 3 ? 64 : 64;
	if (F->Len() < MaxLen) F->AppendChar(C);
	LocalNote.Reset();
}

bool AKnightHUD::TypeKey(const FKey& Key)
{
	const int32 NumFields = bCreateMode ? 3 : 2;
	auto Next = [&](int32 Dir)
	{
		// Tab walks name -> password (-> again) -> server address
		static const int32 OrderLogin[3] = { 0, 1, 3 };
		static const int32 OrderNew[4] = { 0, 1, 2, 3 };
		const int32* Order = bCreateMode ? OrderNew : OrderLogin;
		const int32 N = NumFields + 1;
		int32 At = 0;
		for (int32 i = 0; i < N; ++i) if (Order[i] == TypeFocus) At = i;
		TypeFocus = Order[((At + Dir) % N + N) % N];
	};
	if (Key == EKeys::BackSpace)
	{
		FString* F = TypeFocus == 0 ? &TypeName : TypeFocus == 1 ? &TypePass : TypeFocus == 2 ? &TypePass2 : &TypeAddr;
		if (F->Len() > 0) F->LeftChopInline(1);
		return true;
	}
	if (Key == EKeys::Tab) { Next(FSlateApplication::Get().GetModifierKeys().IsShiftDown() ? -1 : 1); return true; }
	if (Key == EKeys::Enter) { bTypeEnter = true; return true; }
	return Key != EKeys::Escape;   // (let Escape through)
}

void AKnightHUD::HideTyping()
{
	if (!Typing.IsValid()) return;
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Typing.ToSharedRef());
	Typing.Reset();
}

void AKnightHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	HideTyping();
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------------------------------
// No knight on screen: the starting screen, or (online) a moment while the server lets us in
// ---------------------------------------------------------------------------------------------------
void AKnightHUD::DrawNoKnight()
{
	if (UJeebGameInstance::InMenu(this)) { DrawStartScreen(); return; }
	HideTyping();
	if (GetNetMode() != NM_Client) return;
	UJeebGameInstance* GI = GetGameInstance<UJeebGameInstance>();
	UFont* Font = GEngine->GetMediumFont();
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	const FString Line = FString::Printf(TEXT("Entering %s..."), GI ? *GI->WorldName(GI->ChosenWorld) : TEXT("the world"));
	float TW = 0.f, TH = 0.f;
	HDTextSize(Line, TW, TH, Font, 1.6f);
	const float W = TW + 80.f, H = TH + 44.f, X = (Canvas->ClipX - W) * 0.5f, Y = Canvas->ClipY * 0.45f;
	Panel(X, Y, W, H);
	HDText(Line, FLinearColor(1.f, 0.9f, 0.65f), X + 40.f, Y + 22.f, Font, 1.6f);
}

void AKnightHUD::DrawTextField(float X, float Y, float W, const TCHAR* Label, const FString& Value, int32 Field, bool bSecret)
{
	UFont* Font = GEngine->GetMediumFont();
	const float H = 46.f;
	HDText(Label, FLinearColor(0.8f, 0.82f, 0.88f), X + 4.f, Y, Font, 1.0f);
	const float BY = Y + 24.f;
	const bool bFocus = TypeFocus == Field;
	const bool bOver = Over(X, BY, W, H);
	Tile(X, BY, W, H, bFocus ? FLinearColor(0.95f, 0.75f, 0.35f) : FLinearColor(0.75f, 0.8f, 0.9f), bFocus ? 0.34f : (bOver ? 0.26f : 0.18f));
	if (bOver && bUIClick) TypeFocus = Field;
	FString Shown = bSecret ? FString::ChrN(Value.Len(), TEXT('*')) : Value;
	if (bFocus && FMath::FloorToInt(GetWorld()->GetRealTimeSeconds() * 2.f) % 2 == 0) Shown += TEXT("|");
	// long text shows its end
	float TW = 0.f, TH = 0.f;
	HDTextSize(Shown, TW, TH, Font, 1.3f);
	while (TW > W - 28.f && Shown.Len() > 1) { Shown.RightChopInline(1); HDTextSize(Shown, TW, TH, Font, 1.3f); }
	HDText(Shown, FLinearColor::White, X + 14.f, BY + (H - TH) * 0.5f, Font, 1.3f);
}

void AKnightHUD::DrawStartScreen()
{
	UJeebGameInstance* GI = GetGameInstance<UJeebGameInstance>();
	APlayerController* PC = GetOwningPlayerController();
	if (!GI || !PC) return;
	UFont* Font = GEngine->GetMediumFont();
	if (!bTypeInit)
	{
		bTypeInit = true;
		TypeName = GI->LoginName;
		TypeAddr = GI->ServerAddress;
		bCreateMode = GI->bCreateAccount;
		PickedWorld = FMath::Clamp(GI->ChosenWorld, 0, FMath::Max(0, GI->Worlds.Num() - 1));
		TypeFocus = TypeName.IsEmpty() ? 0 : 1;
	}
	// Keep the keyboard on our typing widget (a click on the screen would otherwise take it)
	if (!Typing.IsValid() && GEngine->GameViewport)
	{
		SAssignNew(Typing, SJeebTyping, this);
		GEngine->GameViewport->AddViewportWidgetContent(Typing.ToSharedRef(), 50);
	}
	if (Typing.IsValid() && FSlateApplication::IsInitialized() && !Typing->HasKeyboardFocus()) FSlateApplication::Get().SetKeyboardFocus(Typing, EFocusCause::SetDirectly);
	// the world list asks the address being typed
	if (!GI->bJoining && TypeAddr.TrimStartAndEnd() != GI->ServerAddress && TypeFocus != 3 && !TypeAddr.TrimStartAndEnd().IsEmpty())
	{
		GI->ServerAddress = TypeAddr.TrimStartAndEnd();
		for (FJeebWorldStatus& S : GI->Status) S = FJeebWorldStatus();
	}
	GI->PollWorlds();

	const float CX = Canvas->ClipX, CY = Canvas->ClipY;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.25f), 0.f, 0.f, CX, CY);
	NorseText(TEXT("JEEBSLAND"), CY * 0.08f, CY * 0.085f, 1.f);

	const float PW = 470.f, Gap = 30.f, PH = 520.f;
	const float LX = CX * 0.5f - PW - Gap * 0.5f, RX = CX * 0.5f + Gap * 0.5f, PY = CY * 0.25f;

	// ---- Left: who you are ----
	Panel(LX, PY, PW, PH);
	{
		const float BW = (PW - 60.f) * 0.5f;
		if (Btn(LX + 24.f, PY + 22.f, BW, 40.f, TEXT("Log in"), true, !bCreateMode, 1.1f)) { bCreateMode = false; if (TypeFocus == 2) TypeFocus = 1; LocalNote.Reset(); }
		if (Btn(LX + 36.f + BW, PY + 22.f, BW, 40.f, TEXT("New account"), true, bCreateMode, 1.1f)) { bCreateMode = true; LocalNote.Reset(); }
		float Y = PY + 86.f;
		DrawTextField(LX + 24.f, Y, PW - 48.f, TEXT("Name"), TypeName, 0, false); Y += 86.f;
		DrawTextField(LX + 24.f, Y, PW - 48.f, TEXT("Password"), TypePass, 1, true); Y += 86.f;
		if (bCreateMode) { DrawTextField(LX + 24.f, Y, PW - 48.f, TEXT("Password again"), TypePass2, 2, true); Y += 86.f; }
		const FString Hint = bCreateMode ? TEXT("Names: 3-12 letters, numbers and spaces. Passwords: 6 or more characters.")
		                                 : TEXT("New here? Pick \"New account\" and choose a name.");
		Wrapped(Hint, LX + 26.f, Y + 6.f, PW - 52.f, 0.9f, FLinearColor(0.7f, 0.72f, 0.78f));
		const FString Note = !LocalNote.IsEmpty() ? LocalNote : GI->MenuMessage;
		if (!Note.IsEmpty()) Wrapped(Note, LX + 26.f, PY + PH - 70.f, PW - 52.f, 1.05f, FLinearColor(1.f, 0.55f, 0.45f));
	}

	// ---- Right: which world ----
	Panel(RX, PY, PW, PH);
	{
		HDText(TEXT("Choose a world"), FLinearColor(1.f, 0.88f, 0.6f), RX + 26.f, PY + 26.f, Font, 1.35f);
		float Y = PY + 74.f;
		for (int32 i = 0; i < GI->Worlds.Num(); ++i)
		{
			const FJeebWorldStatus St = GI->Status.IsValidIndex(i) ? GI->Status[i] : FJeebWorldStatus();
			const bool bPicked = PickedWorld == i;
			const bool bOver = Over(RX + 24.f, Y, PW - 48.f, 58.f);
			Tile(RX + 24.f, Y, PW - 48.f, 58.f, bPicked ? FLinearColor(0.95f, 0.7f, 0.3f) : FLinearColor(0.7f, 0.78f, 0.9f), bPicked ? 0.42f : (bOver ? 0.28f : 0.16f));
			if (bOver && bUIClick) { PickedWorld = i; LocalNote.Reset(); }
			HDText(GI->Worlds[i].Name, FLinearColor::White, RX + 44.f, Y + 16.f, Font, 1.3f);
			const FString Count = St.bUp ? FString::Printf(TEXT("%d / %d players"), St.Players, St.Max) : FString(TEXT("Offline"));
			float TW = 0.f, TH = 0.f;
			HDTextSize(Count, TW, TH, Font, 1.0f);
			const FLinearColor CountCol = !St.bUp ? FLinearColor(0.6f, 0.6f, 0.62f) : (St.Players >= St.Max ? FLinearColor(1.f, 0.5f, 0.4f) : FLinearColor(0.6f, 1.f, 0.65f));
			HDText(Count, CountCol, RX + PW - 44.f - TW, Y + 20.f, Font, 1.0f);
			Y += 68.f;
		}
		DrawTextField(RX + 24.f, PY + PH - 196.f, PW - 48.f, TEXT("Server address"), TypeAddr, 3, false);
		if (GI->bJoining)
		{
			const FString J = FString::Printf(TEXT("Joining %s..."), *GI->WorldName(GI->ChosenWorld));
			float TW = 0.f, TH = 0.f;
			HDTextSize(J, TW, TH, Font, 1.4f);
			HDText(J, FLinearColor(1.f, 0.9f, 0.6f), RX + (PW - TW) * 0.5f, PY + PH - 84.f, Font, 1.4f);
		}
		else if (Btn(RX + 24.f, PY + PH - 96.f, PW - 48.f, 66.f, bCreateMode ? TEXT("CREATE AND PLAY") : TEXT("PLAY"), true, true, 1.6f) || bTypeEnter) TryPlay();
	}
	bTypeEnter = false;
	DrawTip();
}

// Checks what can be checked here (the server checks it all again), then sets off for the chosen world
void AKnightHUD::TryPlay()
{
	UJeebGameInstance* GI = GetGameInstance<UJeebGameInstance>();
	if (!GI || GI->bJoining) return;
	const FString Name = TypeName.TrimStartAndEnd();
	FString Bad = JeebAccounts::CheckName(Name);
	if (Bad.IsEmpty() && TypePass.IsEmpty()) Bad = TEXT("Type your password.");
	if (Bad.IsEmpty() && bCreateMode) Bad = JeebAccounts::CheckPassword(TypePass);
	if (Bad.IsEmpty() && bCreateMode && TypePass != TypePass2) Bad = TEXT("The two passwords don't match.");
	if (Bad.IsEmpty() && TypeAddr.TrimStartAndEnd().IsEmpty()) Bad = TEXT("Type the server's address (127.0.0.1 on this PC).");
	if (!Bad.IsEmpty()) { LocalNote = Bad; return; }
	GI->LoginName = Name;
	GI->LoginPassword = TypePass;
	GI->bCreateAccount = bCreateMode;
	GI->ServerAddress = TypeAddr.TrimStartAndEnd();
	for (FJeebWorldStatus& S : GI->Status) S = FJeebWorldStatus();   // (the address may have changed)
	GI->JoinWorld(PickedWorld);
}
