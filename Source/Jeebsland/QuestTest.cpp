// A test of the quest system online (test builds only, never Shipping): run a world server and one client, both with -JeebQuestTest.
//  - The client (a fresh test account) walks through Marta's offer on its own, clicking to the end, so "I'll help" goes to the server
//    through the real request (ServerQuest), and takes pictures (Saved/Shots/quest_*.png): the offer, the tracker, the quest log.
//  - The server waits for that quest to arrive, then plays the rest itself on the knight: items put in the pack, the knight moved beside
//    each person in turn, the same requests the player's machine would send (DoQuest), the pen knight's yield hook (OnFoeBeaten) and a
//    ruin reached (the visit check). It logs every step (JEEBQUESTTEST) and saves the character at the end.
//  - A second client run with the same account checks the save: the server logs what it restored and the client what reached it.
#include "QuestComponent.h"
#include "QuestPeople.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Npc.h"
#include "ForestRuins.h"
#include "KnightCharacter.h"
#include "KnightHUD.h"
#include "KnightGameMode.h"
#include "KnightPlayerController.h"
#include "Terrain.h"
#include "OakHill.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#if !UE_BUILD_SHIPPING

using namespace JeebQuests;

struct FQuestTestDriver
{
	static void Log(const FString& S) { UE_LOG(LogTemp, Log, TEXT("JEEBQUESTTEST %s"), *S); }

	static FString State(const UQuestComponent* C)
	{
		FString S;
		for (const FQuestProgress& P : C->Quests) S += FString::Printf(TEXT(" %s(step %d, count %d%s)"), *P.Key.ToString(), P.Step, P.Count, P.bDone ? TEXT(", done") : TEXT(""));
		const USkillComponent* Sk = C->Skills();
		if (Sk) S += FString::Printf(TEXT(" | ingots %d, woodcutting %d xp, mining %d xp, fishing %d xp, attack %d xp, climbing %d xp, potions %d healing %d swiftness"), Sk->GetIngots(),
			Sk->GetXPTotal(ESkillType::Woodcutting), Sk->GetXPTotal(ESkillType::Mining), Sk->GetXPTotal(ESkillType::Fishing), Sk->GetXPTotal(ESkillType::Attack), Sk->GetXPTotal(ESkillType::Climbing),
			Sk->CountItem(JeebItems::PotionId(1)), Sk->CountItem(JeebItems::PotionId(0)));
		return S;
	}

	static void MoveBeside(UQuestComponent* C, FName Person)
	{
		AActor* Me = C->GetOwner();
		const ANpc* N = AQuestPeople::Find(C->GetWorld(), Person);
		if (!Me || !N) { Log(FString::Printf(TEXT("FAIL: no %s to stand beside"), *Person.ToString())); return; }
		const FVector2D At = FVector2D(N->GetActorLocation()) + (FVector2D(JeebOakHill::PosX, JeebOakHill::PosY) - FVector2D(N->GetActorLocation())).GetSafeNormal() * 150.f;
		Me->TeleportTo(FVector(At.X, At.Y, N->GetActorLocation().Z + 20.f), Me->GetActorRotation(), false, true);
	}

	static void Server(UQuestComponent* C, float Dt)
	{
		USkillComponent* Sk = C->Skills();
		if (!Sk) return;
		C->TestClock += Dt;
		static TMap<TWeakObjectPtr<UQuestComponent>, float> NextAt;   // (per character; test only)
		float& Next = NextAt.FindOrAdd(C);
		if (C->TestClock < Next) return;
		auto Wait = [&](float S) { Next = C->TestClock + S; };
		auto Step = [&](const TCHAR* What) { Log(FString::Printf(TEXT("[%d] %s ->%s"), C->TestStage, What, *State(C))); ++C->TestStage; Wait(1.f); };
		const FName Kindling(TEXT("Kindling"));
		switch (C->TestStage)
		{
		case 0:
			if (C->TestClock < 3.f) return;
			if (C->Quests.Num() > 0) { Log(FString::Printf(TEXT("server reload: this character came back with%s"), *State(C))); C->TestStage = 1000; return; }
			Log(FString::Printf(TEXT("server start: a new character at %s; waiting for the client's accept"), *C->GetOwner()->GetActorLocation().ToString()));
			C->TestStage = 1; return;
		case 1:
			if (!C->Find(Kindling) && C->TestClock < 40.f) return;
			if (!C->Find(Kindling)) { Log(TEXT("FAIL: the client's accept never arrived; taking it here")); MoveBeside(C, TEXT("Marta")); C->DoQuest(UQuestComponent::EOp::Accept, Kindling); }
			else Log(TEXT("PASS: the client's request (ServerQuest Accept Kindling) arrived and was accepted"));
			Step(TEXT("Kindling accepted")); return;
		case 2: MoveBeside(C, TEXT("Marta")); C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Marta")); Step(TEXT("talked to Marta with no logs (should still be step 0)")); return;
		case 3: Sk->AddItem(EItemType::Logs, 5); C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Marta")); Step(TEXT("5 logs handed to Marta (Kindling should be done, logs gone)")); Log(FString::Printf(TEXT("logs left in the pack: %d"), Sk->GetItemCount(EItemType::Logs))); return;
		case 4: MoveBeside(C, TEXT("Pell")); C->DoQuest(UQuestComponent::EOp::Accept, TEXT("Iron")); Step(TEXT("asked Pell for Iron (should be refused: needs Hearth and Mining 10)")); return;
		case 5: MoveBeside(C, TEXT("Marta")); C->DoQuest(UQuestComponent::EOp::Accept, TEXT("Hearth")); Step(TEXT("Hearth accepted")); return;
		case 6: C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Pell")); Step(TEXT("talked to Pell from Marta's side (should be refused: too far)")); return;
		case 7: MoveBeside(C, TEXT("Pell")); C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Pell")); Step(TEXT("talked to Pell (Hearth step 1)")); return;
		case 8: Sk->AddItem(EItemType::TinOre, 6); C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Pell")); Step(TEXT("6 tin ore to Pell (Hearth step 2)")); return;
		case 9: MoveBeside(C, TEXT("Marta")); C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Marta")); Step(TEXT("back to Marta (Hearth done)")); return;
		case 10: MoveBeside(C, TEXT("Nan")); C->DoQuest(UQuestComponent::EOp::Accept, TEXT("Supper")); Step(TEXT("Supper accepted")); return;
		case 11: Sk->AddItem(EItemType::Perch, 4); C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Nan")); Step(TEXT("4 perch to Nan")); return;
		case 12: MoveBeside(C, TEXT("Corin")); C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Corin")); Step(TEXT("pie to Corin")); return;
		case 13: MoveBeside(C, TEXT("Nan")); C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Nan")); Step(TEXT("back to Nan (Supper done, 2 healing potions)")); return;
		case 14: MoveBeside(C, TEXT("Corin")); C->DoQuest(UQuestComponent::EOp::Accept, TEXT("Sparring")); Step(TEXT("Sparring accepted")); return;
		case 15: UQuestComponent::OnFoeBeaten(C->GetOwner(), TEXT("PenKnight")); Step(TEXT("the pen knight yields once (1/2)")); return;
		case 16: UQuestComponent::OnFoeBeaten(C->GetOwner(), TEXT("PenKnight")); Step(TEXT("the pen knight yields twice (step 2)")); return;
		case 17: C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Corin")); Step(TEXT("told Corin (Sparring done)")); return;
		case 18: MoveBeside(C, TEXT("Wil")); C->DoQuest(UQuestComponent::EOp::Accept, TEXT("Ruins")); Step(TEXT("Ruins accepted")); return;
		case 19:
		{
			FVector2D Ruin = FVector2D::ZeroVector;
			for (TActorIterator<AForestRuins> It(C->GetWorld()); It; ++It) if (It->Spots().Num() > 0) Ruin = FVector2D(It->Spots()[0].X, It->Spots()[0].Y);
			if (Ruin.IsZero()) { Log(TEXT("FAIL: no forest ruins on the server")); C->TestStage = 21; return; }
			const FVector2D At = Ruin + FVector2D(800.f, 0.f);
			C->GetOwner()->TeleportTo(FVector(At.X, At.Y, JeebTerrain::HeightAt(At.X, At.Y) + 120.f), C->GetOwner()->GetActorRotation(), false, true);
			Log(FString::Printf(TEXT("moved to 8 m from the ruin at %.0f %.0f"), Ruin.X, Ruin.Y));
			++C->TestStage; Wait(2.f); return;
		}
		case 20: Step(TEXT("after 2 s by the ruin (Ruins should be on step 1)")); return;
		case 21: MoveBeside(C, TEXT("Wil")); C->DoQuest(UQuestComponent::EOp::Talk, TEXT("Wil")); Step(TEXT("told Wil (Ruins done)")); return;
		case 22:
		{
			APawn* Pawn = Cast<APawn>(C->GetOwner());
			AKnightPlayerController* PC = Pawn ? Cast<AKnightPlayerController>(Pawn->GetController()) : nullptr;
			if (AKnightGameMode* GM = C->GetWorld()->GetAuthGameMode<AKnightGameMode>()) GM->SaveCharacter(PC);
			int32 Done = 0;
			for (const FQuestProgress& P : C->Quests) Done += P.bDone ? 1 : 0;
			Log(FString::Printf(TEXT("%s: %d of 5 quests done; saved ->%s"), Done == 5 ? TEXT("PASS") : TEXT("FAIL"), Done, *State(C)));
			C->TestStage = 1000; return;
		}
		default: return;
		}
	}

	static void Shot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("quest_%s.png"), Name), true, false);
		Log(FString::Printf(TEXT("picture quest_%s"), Name));
	}

	static void Client(UQuestComponent* C, float Dt)
	{
		AKnightCharacter* K = Cast<AKnightCharacter>(C->GetOwner());
		APlayerController* PC = K ? Cast<APlayerController>(K->GetController()) : nullptr;
		if (!K || !PC) return;
		AKnightHUD* Hud = Cast<AKnightHUD>(PC->GetHUD());
		C->TestClock += Dt;
		static float Next = 0.f;
		if (C->TestClock < Next) return;
		auto Wait = [&](float S) { Next = C->TestClock + S; };
		const FName Kindling(TEXT("Kindling"));
		switch (C->TestStage)
		{
		case 0:
			if (C->TestClock < 10.f) return;
			if (C->Find(Kindling)) { Log(FString::Printf(TEXT("client reload: the quests that reached this machine:%s"), *State(C))); C->TestStage = 50; Wait(1.f); return; }
			if (ANpc* Marta = AQuestPeople::Find(C->GetWorld(), TEXT("Marta")))
			{
				Log(FString::Printf(TEXT("client: Marta stands %.0f cm away; her mark: %d"), FVector::Dist2D(Marta->GetActorLocation(), K->GetActorLocation()), int32(C->MarkFor(TEXT("Marta")))));
				K->StartDialogue(Marta);
			}
			else { Log(TEXT("FAIL: no Marta on the client")); C->TestStage = 60; return; }
			C->TestStage = 1; Wait(1.5f); return;
		case 1: Shot(TEXT("talk")); C->TestStage = 2; Wait(0.5f); return;
		case 2:
		{
			FString Name, Line; bool bLast = false;
			if (!K->GetDialogue(Name, Line, bLast)) { Log(TEXT("client: the offer is over; I'll help was sent")); C->TestStage = 3; Wait(1.f); return; }
			Log(FString::Printf(TEXT("client: %s says \"%s\""), *Name, *Line));
			if (bLast && C->TestStage == 2) { Shot(TEXT("offer")); C->TestStage = 102; Wait(1.f); return; }
			K->AdvanceDialogue(); Wait(0.4f); return;
		}
		case 102: K->AdvanceDialogue(); C->TestStage = 2; Wait(0.4f); return;
		case 3:
			if (!C->Find(Kindling) && C->TestClock < 40.f) return;
			Log(FString::Printf(TEXT("client: after accepting:%s; tracker: %s"), *State(C), C->Find(Kindling) ? *C->StepLine(*C->Find(Kindling)) : TEXT("-")));
			C->TestStage = 4; Wait(1.5f); return;
		case 4: Shot(TEXT("hud")); C->TestStage = 5; Wait(1.f); return;
		case 5: K->ToggleSettings(); if (Hud) Hud->OpenQuestLog(); C->TestStage = 6; Wait(1.5f); return;
		case 6: Shot(TEXT("log")); C->TestStage = 7; Wait(1.f); return;
		case 7: if (K->IsSettingsOpen()) K->ToggleSettings(); C->TestStage = 8; return;
		case 8:
		{
			int32 Done = 0;
			for (const FQuestProgress& P : C->Quests) Done += P.bDone ? 1 : 0;
			if (Done < 5 && C->TestClock < 150.f) return;
			Log(FString::Printf(TEXT("client: %d quests done:%s"), Done, *State(C)));
			K->ToggleSettings(); if (Hud) Hud->OpenQuestLog();
			C->TestStage = 9; Wait(1.5f); return;
		}
		case 9: Shot(TEXT("log_done")); C->TestStage = 60; Wait(2.f); return;
		case 50: Log(FString::Printf(TEXT("client reload done:%s"), *State(C))); C->TestStage = 60; Wait(2.f); return;
		case 60: Log(TEXT("client: quitting")); PC->ConsoleCommand(TEXT("quit")); C->TestStage = 61; return;
		default: return;
		}
	}
};

void UQuestComponent::TickTest(float Dt)
{
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("JeebQuestTest"));
	if (!bTest || !GetOwner()) return;
	const ENetMode NM = GetOwner()->GetNetMode();
	const APawn* P = Cast<APawn>(GetOwner());
	if (NM == NM_DedicatedServer && HasAuth()) FQuestTestDriver::Server(this, Dt);
	else if (NM == NM_Client && P && P->IsLocallyControlled()) FQuestTestDriver::Client(this, Dt);
}

#endif
