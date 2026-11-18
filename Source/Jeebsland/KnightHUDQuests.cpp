// Quests on the screen (2026-11-18): the marks over quest givers' heads, the tracker under the frame rate, the "QUEST COMPLETE" title,
// the J key, the QUESTS tab of the Escape screen and the marker on the world map. Everything here only reads the character's quest
// state (UQuestComponent), which the server sends to its owner; the only thing a click changes is which quest this screen tracks.
#include "KnightHUD.h"
#include "KnightCharacter.h"
#include "QuestComponent.h"
#include "QuestData.h"
#include "Npc.h"
#include "Inventory.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"

using namespace JeebQuests;

namespace
{
	const FLinearColor QGold(1.f, 0.84f, 0.32f);
	const FLinearColor QIvory(0.96f, 0.93f, 0.86f);
	const FLinearColor QGrey(0.62f, 0.63f, 0.68f);
}

void AKnightHUD::DrawQuestHUD(AKnightCharacter* Knight, APlayerController* PC)
{
	UQuestComponent* Qc = Knight ? Knight->GetQuests() : nullptr;
	if (!Qc || !PC) return;
	UFont* Font = GEngine->GetMediumFont();

	// J opens the quest log (the Escape screen on its QUESTS tab) and closes it again
	if (!bChatOpen && !bLookTyping && PC->WasInputKeyJustPressed(EKeys::J))
	{
		if (Knight->IsSettingsOpen() && SettingsTab == 6) Knight->ToggleSettings();
		else if (Knight->IsSettingsOpen()) SettingsTab = 6;
		else if (!Knight->IsUIOpen() || Knight->IsDialogueOpen()) { Knight->ToggleSettings(); if (Knight->IsSettingsOpen()) OpenQuestLog(); }
	}

	if (Knight->IsUIOpen()) return;

	// A mark over each quest giver within 30 m: "!" a quest to offer, "?" gold something to hand in, "?" grey one of theirs under way
	const FVector Me = Knight->GetActorLocation();
	const float Bob = FMath::Sin(float(FPlatformTime::Seconds()) * 2.4f) * 4.f;
	for (TActorIterator<ANpc> It(GetWorld()); It; ++It)
	{
		if (!It->IsQuestGiver()) continue;
		const UQuestComponent::EMark M = Qc->MarkFor(It->GetPersonKey());
		if (M == UQuestComponent::EMark::None) continue;
		const float D = FVector::Dist2D(It->GetActorLocation(), Me);
		float A = FMath::Clamp((3000.f - D) / 600.f, 0.f, 1.f);
		if (A <= 0.f) continue;
		FVector2D SP;
		// (45 cm over the top of the body shown, ANpc::HeadTopZ: it floated about a metre up, review 2026-11-18)
		const FVector At = It->GetActorLocation();
		if (!PC->ProjectWorldLocationToScreen(FVector(At.X, At.Y, It->HeadTopZ() + 45.f), SP)) continue;
		A *= UnderTitle(SP.Y);   // (fades under "QUEST COMPLETE" and the other titles)
		if (A <= 0.01f) continue;
		const float S = FMath::Clamp(900.f / FMath::Max(D, 300.f), 0.75f, 2.2f);   // (smaller far away, never tiny)
		const bool bDim = M == UQuestComponent::EMark::Busy;
		const FString G = M == UQuestComponent::EMark::Offer ? TEXT("!") : TEXT("?");
		const FLinearColor C = bDim ? FLinearColor(0.75f, 0.76f, 0.8f, 0.8f * A) : FLinearColor(QGold.R, QGold.G, QGold.B, A);
		float TW = 0.f, TH = 0.f;
		HDTextSize(G, TW, TH, Font, 1.6f * S);
		const float Y = SP.Y - TH + Bob * S * 0.5f;
		DrawCircle(SP.X, Y + TH * 0.5f, TH * 0.62f, FLinearColor(0.04f, 0.05f, 0.08f, 0.45f * A));
		HDText(G, FLinearColor(0.f, 0.f, 0.f, 0.8f * A), SP.X - TW * 0.5f + 2.f, Y + 2.f, Font, 1.6f * S);
		HDText(G, C, SP.X - TW * 0.5f, Y, Font, 1.6f * S);
	}

	// The tracker: the tracked quest's name and its current step, top right under the frame rate
	const FName Key = Qc->GetTracked();
	const FQuestProgress* P = Qc->Find(Key);
	const FQuestDef* Q = P ? FindQuest(Key) : nullptr;
	if (!Q) return;
	const float W = 360.f, X = Canvas->ClipX - W - 20.f, Y = 52.f;
	const FString Line = Qc->StepLine(*P);
	float LW = 0.f, LH = 0.f;
	HDTextSize(Line, LW, LH, Font, 1.0f);
	const float Lines = FMath::Max(1.f, FMath::CeilToFloat(LW / (W - 28.f)));
	const float H = 46.f + Lines * (LH + 2.f) + 24.f;
	Tile(X, Y, W, H, FLinearColor(0.05f, 0.07f, 0.12f), 0.42f, true);
	WindowTitle(Q->Title, X + 14.f, Y + 8.f, 1.15f);
	const float BH = Wrapped(Line, X + 14.f, Y + 42.f, W - 28.f, 1.0f, QIvory);
	HDText(TEXT("J  quest log"), FLinearColor(0.7f, 0.72f, 0.78f, 0.85f), X + 14.f, Y + 46.f + BH, Font, 0.8f);
}

// "QUEST COMPLETE", for a few seconds after the server says so. Drawn after the world's marks, name tags and the E prompt (which
// fade where it covers them, KnightHUD.h UnderTitle): their circles and letters sat on its subtitle (review 2026-11-18)
void AKnightHUD::DrawQuestTitle(AKnightCharacter* Knight)
{
	UQuestComponent* Qc = Knight ? Knight->GetQuests() : nullptr;
	if (!Qc) return;
	const float Age = float(FPlatformTime::Seconds() - Qc->DoneTime);
	if (Age >= 0.f && Age < 4.5f && !Qc->DoneTitle.IsEmpty())
	{
		const float A = FMath::Clamp(FMath::Min(Age / 0.5f, (4.5f - Age) / 1.2f), 0.f, 1.f);
		const float H = FMath::Clamp(Canvas->ClipY * 0.042f, 26.f, 54.f);
		TitleBanner(TEXT("QUEST COMPLETE"), Canvas->ClipY * 0.2f, H, A);
		TitleTextAt(Qc->DoneTitle, -1.f, Canvas->ClipY * 0.2f + H * 1.55f, H * 0.55f, A * 0.9f);
	}
}

void AKnightHUD::DrawQuestMapMark(AKnightCharacter* Knight, const FVector2D& C, float Size)
{
	const float Pulse = 1.f + 0.12f * FMath::Sin(float(FPlatformTime::Seconds()) * 3.f);
	auto Diamond = [&](float R, const FLinearColor& Col) { Poly({ C + FVector2D(0.f, -R), C + FVector2D(R, 0.f), C + FVector2D(0.f, R), C + FVector2D(-R, 0.f) }, Col); };
	Diamond(12.f * Size * Pulse, FLinearColor(0.f, 0.f, 0.f, 0.8f));
	Diamond(9.f * Size * Pulse, QGold);
	Diamond(4.f * Size, FLinearColor(0.45f, 0.25f, 0.05f));
}

void AKnightHUD::DrawQuestLog(AKnightCharacter* Knight, float PX, float PY, float PW, float PH)
{
	UQuestComponent* Qc = Knight ? Knight->GetQuests() : nullptr;
	if (!Qc) return;
	UFont* Font = GEngine->GetMediumFont();
	const TArray<FQuestDef>& All = Quests();

	// The quests in four groups: under way, offered, finished, not open yet
	TArray<const FQuestDef*> Groups[4];
	for (const FQuestDef& Q : All)
	{
		switch (Qc->StateOf(Q))
		{
		case UQuestComponent::EState::Active: Groups[0].Add(&Q); break;
		case UQuestComponent::EState::Available: Groups[1].Add(&Q); break;
		case UQuestComponent::EState::Done: Groups[2].Add(&Q); break;
		default: Groups[3].Add(&Q); break;
		}
	}
	if (!FindQuest(QuestSel))
	{
		QuestSel = Qc->GetTracked();
		for (int32 g = 0; g < 4 && QuestSel.IsNone(); ++g) if (Groups[g].Num() > 0) QuestSel = Groups[g][0]->Key;
	}

	int32 Done = Groups[2].Num();
	HDText(FString::Printf(TEXT("Quests finished:  %d / %d      Oak Hill's people need a hand. Look for a  !  over their heads."), Done, All.Num()),
		FLinearColor(0.8f, 0.78f, 0.7f), PX + 30.f, PY + 56.f, Font, 1.0f);

	// List
	static const TCHAR* GroupNames[4] = { TEXT("UNDER WAY"), TEXT("OFFERED"), TEXT("FINISHED"), TEXT("NOT YET") };
	const float LX = PX + 30.f, LW = 330.f, RowH = 32.f;
	float Y = PY + 92.f;
	for (int32 g = 0; g < 4; ++g)
	{
		if (Groups[g].Num() == 0) continue;
		HDText(GroupNames[g], FLinearColor(0.7f, 0.74f, 0.82f), LX + 4.f, Y, Font, 0.85f);
		Y += 22.f;
		for (const FQuestDef* Q : Groups[g])
		{
			if (Y > PY + PH - 90.f) break;
			const bool bSel = QuestSel == Q->Key;
			const bool bOver = Over(LX, Y, LW, RowH - 4.f);
			Tile(LX, Y, LW, RowH - 4.f, bSel ? FLinearColor(1.f, 0.78f, 0.35f) : FLinearColor(0.6f, 0.72f, 0.88f), bSel ? 0.5f : (bOver ? 0.34f : 0.16f));
			const FLinearColor TC = g == 0 ? FLinearColor(1.f, 0.92f, 0.65f) : g == 1 ? QIvory : g == 2 ? FLinearColor(0.6f, 0.78f, 0.6f) : FLinearColor(0.48f, 0.48f, 0.5f);
			HDText(Q->Title, TC, LX + 10.f, Y + 5.f, Font, 1.05f);
			if (g == 0 && Qc->GetTracked() == Q->Key) HDText(TEXT("tracked"), FLinearColor(1.f, 0.85f, 0.4f, 0.85f), LX + LW - 70.f, Y + 7.f, Font, 0.8f);
			if (bOver && bUIClick) QuestSel = Q->Key;
			Y += RowH;
		}
		Y += 8.f;
	}
	if (All.Num() == 0) HDText(TEXT("No quests could be read (Content/Data/Quests.csv)."), QGrey, LX, PY + 92.f, Font, 1.0f);

	// Reading pane
	const float RX = PX + 390.f, RY = PY + 92.f, RW = PW - 390.f - 30.f;
	Tile(RX - 10.f, RY - 6.f, RW + 20.f, PH - 92.f - 60.f, FLinearColor(0.1f, 0.14f, 0.22f), 0.45f, false);
	const FQuestDef* Q = FindQuest(QuestSel);
	if (!Q) { HDText(TEXT("Choose a quest to read about it."), FLinearColor(0.5f, 0.5f, 0.5f), RX, RY, Font, 1.1f); return; }
	const FQuestProgress* P = Qc->Find(Q->Key);
	const UQuestComponent::EState St = Qc->StateOf(*Q);
	WindowTitle(Q->Title, RX, RY, 1.5f);
	const FPersonDef* Giver = FindPerson(Q->Giver);
	HDText(FString::Printf(TEXT("from %s"), Giver ? *Giver->Name : *Q->Giver.ToString()), FLinearColor(0.65f, 0.65f, 0.7f), RX, RY + 40.f, Font, 0.95f);
	float TY = RY + 74.f;
	TY += Wrapped(Q->Summary, RX, TY, RW, 1.05f, QIvory) + 16.f;

	if (St == UQuestComponent::EState::Locked)
	{
		HDText(TEXT("Not open to you yet. It needs:"), QGrey, RX, TY, Font, 1.0f);
		TY += 28.f;
		for (const FNeed& N : Q->Needs) { TY += Wrapped(FString(TEXT("-  ")) + NeedText(N), RX + 10.f, TY, RW - 10.f, 1.0f, QGrey) + 4.f; }
	}
	else if (St == UQuestComponent::EState::Available)
	{
		TY += Wrapped(FString::Printf(TEXT("Talk to %s in Oak Hill to take it on."), *PersonName(Q->Giver)), RX, TY, RW, 1.0f, QGold) + 8.f;
	}
	else if (P)
	{
		// The steps so far: the finished ones dim, the current one bright (later ones stay a surprise)
		const int32 Last = P->bDone ? Q->Steps.Num() - 1 : FMath::Min<int32>(P->Step, Q->Steps.Num() - 1);
		for (int32 s = 0; s <= Last && Q->Steps.IsValidIndex(s); ++s)
		{
			const bool bNow = !P->bDone && s == P->Step;
			const FString L = bNow ? Qc->StepLine(*P) : Q->Steps[s].Log;
			TY += Wrapped(FString(bNow ? TEXT(">  ") : TEXT("-  ")) + L, RX + 4.f, TY, RW - 4.f, 1.0f, bNow ? QGold : FLinearColor(0.55f, 0.6f, 0.58f)) + 6.f;
		}
		if (P->bDone) TY += Wrapped(TEXT("Finished. Thank you."), RX + 4.f, TY + 4.f, RW - 4.f, 1.0f, FLinearColor(0.6f, 0.82f, 0.6f)) + 10.f;
	}
	TY += 10.f;
	TY += Wrapped(FString::Printf(TEXT("Reward:  %s"), *RewardText(*Q)), RX, TY, RW, 1.0f, FLinearColor(0.85f, 0.82f, 0.7f)) + 10.f;

	if (St == UQuestComponent::EState::Active)
	{
		const bool bTracked = Qc->GetTracked() == Q->Key;
		if (Btn(RX, PY + PH - 104.f, 160.f, 38.f, bTracked ? TEXT("TRACKED") : TEXT("TRACK"), !bTracked, bTracked, 1.1f)) Qc->Tracked = Q->Key;
	}
	HDText(TEXT("Press J or Esc to close."), FLinearColor(0.8f, 0.8f, 0.8f), PX + 30.f, PY + PH - 40.f, Font, 1.0f);
}
