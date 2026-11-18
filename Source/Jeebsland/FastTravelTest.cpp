// A test of fast travel online (test builds only, never Shipping): run a world server and one client, both with -JeebTravelTest.
// The server plays the checks on the knight (the same DoTravel the player's request reaches) and logs every answer (JEEBTRAVELTEST):
//  - a new character knows only Oak Hill; travel to Steele Creek is refused; to Oak Hill, where he stands, is refused
//  - put inside Steele Creek: it is discovered (the client logs the title)
//  - a travel cancelled by moving, a travel cancelled by a blow, a travel refused for combat
//  - then waits for the client's REAL request (ServerTravel from the map's click path: UTravelComponent::Ask), watches the channel, the
//    jump to a free spot in Oak Hill, and a second travel refused for the cooldown; saves the character
//  - a second client run with the same account checks the towns are still known (and with the character file deleted, that the
//    account file alone brings them back: a second character on the account)
// The client takes pictures of the map with its travel markers and the confirm box, and of the count (Saved/Shots/travel_*.png).
#include "FastTravel.h"
#include "SkillComponent.h"
#include "KnightCharacter.h"
#include "KnightHUD.h"
#include "KnightGameMode.h"
#include "KnightPlayerController.h"
#include "SteeleCreek.h"
#include "Terrain.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#if !UE_BUILD_SHIPPING

struct FTravelTestDriver
{
	static void Log(const FString& S) { UE_LOG(LogTemp, Log, TEXT("JEEBTRAVELTEST %s"), *S); }
	static FString State(const UTravelComponent* C)
	{
		FString S;
		for (int32 i = 0; i < JeebTravel::Count(); ++i) if (C->IsVisited(i)) S += FString::Printf(TEXT(" %s"), JeebTravel::Towns()[i].Name);
		return FString::Printf(TEXT("visited:%s (mask %u)"), *S, C->VisitedMask);
	}
	static void Shot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("travel_%s.png"), Name), true, false);
		Log(FString::Printf(TEXT("picture travel_%s"), Name));
	}
	static void PutIn(AKnightCharacter* K, int32 Town, float Dx = 0.f)
	{
		const FVector2D A = JeebTravel::Towns()[Town].Anchor.IsZero() ? JeebTravel::Centre(Town) : JeebTravel::Towns()[Town].Anchor;
		K->TeleportTo(FVector(A.X + Dx, A.Y, JeebTerrain::HeightAt(A.X, A.Y) + 130.f), K->GetActorRotation(), false, true);
	}

	static void Server(UTravelComponent* C, float Dt)
	{
		AKnightCharacter* K = C->Knight();
		if (!K) return;
		C->TestClock += Dt;
		static TMap<TWeakObjectPtr<UTravelComponent>, float> NextAt;
		float& Next = NextAt.FindOrAdd(C);
		if (C->TestClock < Next) return;
		auto Wait = [&](float S) { Next = C->TestClock + S; };
		const int32 Oak = JeebTravel::IndexOf(TEXT("Oak Hill")), Steele = JeebTravel::IndexOf(TEXT("Steele Creek")), Black = JeebTravel::IndexOf(TEXT("Blackrock"));
		auto InOak = [&]() { return JeebTravel::TownAt(FVector2D(K->GetActorLocation())) == Oak; };
		switch (C->TestStage)
		{
		case 0:
			if (C->TestClock < 4.f) return;
			Log(FString::Printf(TEXT("server start: at %.0f %.0f, %s"), K->GetActorLocation().X, K->GetActorLocation().Y, *State(C)));
			if (C->IsVisited(Steele)) { Log(TEXT("RELOAD: this account came back knowing Steele Creek (PASS if expected)")); C->TestStage = 1000; return; }
			C->DoTravel(Steele);   // refused: never been
			C->DoTravel(Oak);      // refused: already here
			C->TestStage = 1; Wait(1.f); return;
		case 1:
			Log(TEXT("putting the knight in Steele Creek's forge yard"));
			PutIn(K, Steele);
			C->TestStage = 2; Wait(3.f); return;
		case 2:
			Log(FString::Printf(TEXT("%s: after 3 s in Steele Creek, %s"), C->IsVisited(Steele) ? TEXT("PASS") : TEXT("FAIL"), *State(C)));
			C->DoTravel(Oak);   // starts a channel
			Log(FString::Printf(TEXT("%s: channel started (to Oak Hill)"), C->ChanTown == Oak ? TEXT("PASS") : TEXT("FAIL")));
			C->TestStage = 3; Wait(1.2f); return;
		case 3:
			PutIn(K, Steele, 300.f);   // walked away (in the test: moved)
			C->TestStage = 4; Wait(1.5f); return;
		case 4:
			Log(FString::Printf(TEXT("%s: moving cancelled the travel (channel %d)"), C->ChanTown < 0 ? TEXT("PASS") : TEXT("FAIL"), C->ChanTown));
			C->TestStage = 41; Wait(1.5f); return;
		case 41:
			C->DoTravel(Oak);
			Log(FString::Printf(TEXT("%s: second channel started"), C->ChanTown == Oak ? TEXT("PASS") : TEXT("FAIL")));
			C->TestStage = 5; Wait(1.2f); return;
		case 5:
			K->Hurt(1, nullptr, true);   // a blow
			C->TestStage = 6; Wait(0.5f); return;
		case 6:
			Log(FString::Printf(TEXT("%s: a blow cancelled the travel (channel %d)"), C->ChanTown < 0 ? TEXT("PASS") : TEXT("FAIL"), C->ChanTown));
			C->DoTravel(Oak);   // refused: in combat
			Log(FString::Printf(TEXT("%s: no channel while in combat (channel %d)"), C->ChanTown < 0 ? TEXT("PASS") : TEXT("FAIL"), C->ChanTown));
			C->TestStage = 7; Wait(1.f); return;
		case 7:
			Log(TEXT("waiting for the client's real request (ServerTravel) once the 10 s of combat are over"));
			C->TestStage = 8; return;
		case 8:
			if (C->ChanTown >= 0) { Log(TEXT("PASS: the client's request arrived and the channel began")); C->TestStage = 9; return; }
			if (C->TestClock > 60.f) { Log(TEXT("FAIL: the client's request never arrived")); C->TestStage = 1000; }
			return;
		case 9:
			if (C->ChanTown >= 0) return;
			// the channel ended: jumped, or cancelled
			Log(FString::Printf(TEXT("%s: after the channel the knight is at %.0f %.0f %.0f, in Oak Hill: %d"), InOak() ? TEXT("PASS") : TEXT("FAIL"), K->GetActorLocation().X, K->GetActorLocation().Y, K->GetActorLocation().Z, InOak() ? 1 : 0));
			{
				FHitResult Hit;
				const FVector P = K->GetActorLocation();
				const bool bGround = K->GetWorld()->LineTraceSingleByChannel(Hit, P, P - FVector(0.f, 0.f, 400.f), ECC_Visibility);
				Log(FString::Printf(TEXT("ground %.0f cm under the knight's middle (should be about 90-130)"), bGround ? P.Z - Hit.ImpactPoint.Z : -1.f));
			}
			C->DoTravel(Steele);   // refused: the cooldown
			C->TestStage = 10; Wait(1.f); return;
		case 10:
		{
			APawn* Pawn = Cast<APawn>(C->GetOwner());
			AKnightPlayerController* PC = Pawn ? Cast<AKnightPlayerController>(Pawn->GetController()) : nullptr;
			if (AKnightGameMode* GM = C->GetWorld()->GetAuthGameMode<AKnightGameMode>()) GM->SaveCharacter(PC);
			Log(FString::Printf(TEXT("saved; %s; Blackrock known: %d"), *State(C), C->IsVisited(Black) ? 1 : 0));
			C->TestStage = 11; Wait(4.f); return;
		}
		case 11:
			Log(FString::Printf(TEXT("4 s later the knight is at %.0f %.0f %.0f"), K->GetActorLocation().X, K->GetActorLocation().Y, K->GetActorLocation().Z));
			C->TestStage = 1000; return;
		default: return;
		}
	}

	static void Client(UTravelComponent* C, float Dt)
	{
		AKnightCharacter* K = C->Knight();
		APlayerController* PC = K ? Cast<APlayerController>(K->GetController()) : nullptr;
		if (!K || !PC) return;
		AKnightHUD* Hud = Cast<AKnightHUD>(PC->GetHUD());
		C->TestClock += Dt;
		static float Next = 0.f, DiscAt = -1.f;
		if (C->TestClock < Next) return;
		auto Wait = [&](float S) { Next = C->TestClock + S; };
		const int32 Oak = JeebTravel::IndexOf(TEXT("Oak Hill")), Steele = JeebTravel::IndexOf(TEXT("Steele Creek"));
		switch (C->TestStage)
		{
		case 0:
			if (C->TestClock < 8.f) return;
			Log(FString::Printf(TEXT("client start: at %.0f %.0f, %s"), K->GetActorLocation().X, K->GetActorLocation().Y, *State(C)));
			if (C->IsVisited(Steele)) { Log(TEXT("client RELOAD: the towns that reached this machine: ") + State(C)); C->TestStage = 60; Wait(2.f); return; }
			C->TestStage = 1; return;
		case 1:
			if (!C->IsVisited(Steele)) { if (C->TestClock > 40.f) { Log(TEXT("FAIL: the client never learned Steele Creek")); C->TestStage = 60; } return; }
			DiscAt = C->TestClock;
			Log(FString::Printf(TEXT("client: learned Steele Creek, %s; title \"%s\""), *State(C), *C->DiscoveredTitle));
			C->TestStage = 2; Wait(1.f); return;
		case 2: Shot(TEXT("discovered")); C->TestStage = 3; Wait(1.f); return;
		case 3: K->ToggleSettings(); C->TestStage = 4; Wait(1.5f); return;
		case 4: if (Hud) Hud->TestAskTravel(-1); Shot(TEXT("map")); C->TestStage = 5; Wait(1.f); return;
		case 5: if (Hud) Hud->TestAskTravel(Oak); C->TestStage = 6; Wait(1.f); return;
		case 6: Shot(TEXT("confirm")); C->TestStage = 7; Wait(1.f); return;
		case 7: if (Hud) Hud->TestAskTravel(-1); if (K->IsSettingsOpen()) K->ToggleSettings(); C->TestStage = 8; return;
		case 8:
			if (C->TestClock < DiscAt + 18.f) return;
			Log(TEXT("client: asking to travel to Oak Hill (Ask -> ServerTravel)"));
			C->Ask(Oak);
			C->TestStage = 9; Wait(1.5f); return;
		case 9:
		{
			int32 T = -1; float Left = 0.f;
			Log(FString::Printf(TEXT("client: channelling %d to town %d, %.1f s left"), C->Channelling(T, Left) ? 1 : 0, T, Left));
			Shot(TEXT("count"));
			C->TestStage = 10; Wait(1.f); return;
		}
		case 10:
		{
			if (C->ClBlackUntil < 0.0 && C->TestClock < DiscAt + 40.f) return;   // (arrival: ClientTravelArrived sets it)
			Log(FString::Printf(TEXT("client: arrived? at %.0f %.0f %.0f; fade %.2f"), K->GetActorLocation().X, K->GetActorLocation().Y, K->GetActorLocation().Z, C->ClFade));
			C->TestStage = 11; Wait(3.f); return;
		}
		case 11:
			Log(FString::Printf(TEXT("client: 3 s after the jump at %.0f %.0f %.0f; fade %.2f; %s"), K->GetActorLocation().X, K->GetActorLocation().Y, K->GetActorLocation().Z, C->ClFade, *State(C)));
			Shot(TEXT("arrived"));
			C->TestStage = 60; Wait(2.f); return;
		case 60: Log(TEXT("client: quitting")); PC->ConsoleCommand(TEXT("quit")); C->TestStage = 61; return;
		default: return;
		}
	}
};

void UTravelComponent::TickTest(float Dt)
{
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("JeebTravelTest"));
	if (!bTest || !GetOwner()) return;
	const ENetMode NM = GetOwner()->GetNetMode();
	const APawn* P = Cast<APawn>(GetOwner());
	if (NM == NM_DedicatedServer && HasAuth()) FTravelTestDriver::Server(this, Dt);
	else if (NM == NM_Client && P && P->IsLocallyControlled()) FTravelTestDriver::Client(this, Dt);
}

#endif
