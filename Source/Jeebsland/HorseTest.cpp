// The horses' online test (2026-11-22, agent horse): -JeebHorseTest on a world server and two players' machines (Tools/horse_net.sh).
// The server walks the first player through the stablemaster's quest (bring, tame a wild horse, hand in), then calls the horse, rides it,
// lets the second player ride it, calls it again (the second player is put down), follows, catches up from far away, tries to hurt it
// outside a PvP zone, and fells it (it goes home and rests). Every step logs JEEBHORSETEST PASS or FAIL; the players' machines log
// what reaches them (JEEBHORSECLIENT) and take a few pictures (Saved/Shots/horse_*.png). Nothing here is in Shipping.
#include "Horse.h"

#if !UE_BUILD_SHIPPING
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "QuestComponent.h"
#include "QuestData.h"
#include "QuestPeople.h"
#include "Npc.h"
#include "HealthComponent.h"
#include "FoeData.h"
#include "JeebTuning.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

struct FHorseTestDriver
{
	static void Log(const FString& S) { UE_LOG(LogTemp, Log, TEXT("JEEBHORSETEST %s"), *S); }
	static void Place(AKnightCharacter* K, const FVector2D& P, const FVector2D& LookAt)
	{
		if (!K) return;
		const FRotator R(0.f, FMath::RadiansToDegrees(FMath::Atan2(LookAt.Y - P.Y, LookAt.X - P.X)), 0.f);
		K->TeleportTo(FVector(P.X, P.Y, JeebFoes::Ground(P.X, P.Y) + 120.f), R, false, true);
		if (APlayerController* PC = Cast<APlayerController>(K->GetController())) PC->ClientSetRotation(FRotator(-8.f, R.Yaw, 0.f));
	}
	static int32 Step(AKnightCharacter* K)
	{
		const FQuestProgress* P = K && K->GetQuests() ? K->GetQuests()->Find(JeebHorse::QuestKey) : nullptr;
		return P ? (P->bDone ? 100 : int32(P->Step)) : -1;
	}
	static float Dist(const AActor* A, const AActor* B) { return (A && B) ? FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) / 100.f : -1.f; }

	static void Server(AStable* St, float Dt)
	{
		static TWeakObjectPtr<AKnightCharacter> P1, P2;
		static TWeakObjectPtr<AHorse> Horse;
		static float Clock = 0.f, NextAt = 0.f, StageT = 0.f;
		static int32 Stage = 0, Passes = 0, Fails = 0, Sub = 0, Hp0 = 0;
		static FVector2D Was;
		Clock += Dt; StageT += Dt;
		if (Clock < NextAt || Stage >= 1000) return;
		UWorld* W = St->GetWorld();
		auto Next = [&](float Wait) { ++Stage; Sub = 0; StageT = 0.f; NextAt = Clock + Wait; };
		auto Check = [&](bool bOk, const FString& What) { Log(FString::Printf(TEXT("%s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); (bOk ? Passes : Fails)++; };
		AKnightCharacter* K = P1.Get();
		AKnightCharacter* K2 = P2.Get();
		AHorse* H = Horse.Get();
		const ANpc* Bram = AQuestPeople::Find(W, TEXT("Bram"));
		const FVector2D Yard(St->GetActorLocation());
		auto ByBram = [&](AKnightCharacter* Who, float Side)
		{
			if (!Bram || !Who) return;
			const FVector2D B(Bram->GetActorLocation()), Fw(Bram->GetActorForwardVector());
			Place(Who, B + Fw * 150.f + FVector2D(-Fw.Y, Fw.X) * Side, B);
		};
		if (Stage > 0 && !K) { Log(TEXT("STOPPED: the first player left")); Stage = 1000; return; }
		if (Stage > 2 && Stage < 12 && !H) { Log(TEXT("STOPPED: the test horse is gone")); Stage = 900; return; }
		switch (Stage)
		{
		case 0:
		{
			TArray<AKnightCharacter*> Ks;
			for (TActorIterator<AKnightCharacter> It(W); It; ++It) if (It->GetController()) Ks.Add(*It);
			if ((Ks.Num() < 2 && Clock < 150.f) || Ks.Num() == 0) { NextAt = Clock + 1.f; StageT = 0.f; return; }
			if (StageT < 8.f) return;
			P1 = Ks[0]; P2 = Ks.Num() > 1 ? Ks[1] : nullptr;
			int32 Wild = 0; for (TActorIterator<AHorse> It(W); It; ++It) Wild += It->IsWild() ? 1 : 0;
			Log(FString::Printf(TEXT("start: %d players, stable %s at %.0f %.0f, Bram %s, %d wild horses"), Ks.Num(), *St->GetKey().ToString(), Yard.X, Yard.Y, Bram ? TEXT("here") : TEXT("MISSING"), Wild));
			Check(Wild > 0 && Bram != nullptr, TEXT("the stable has wild horses and its stablemaster"));
			if (!Bram) { Stage = 900; return; }
			K = Ks[0];
			Check(!K->GetRider()->bOwnsHorse && !K->GetRider()->MyHorse(), TEXT("a new character owns no horse"));
			K->GetQuests()->Quests.RemoveAll([](const FQuestProgress& Q) { return Q.Key == JeebHorse::QuestKey; });
			ByBram(K, 0.f); ByBram(P2.Get(), 250.f);
			Next(2.f); return;
		}
		case 1:   // the quest: accepted, three things brought
		{
			UQuestComponent* Q = K->GetQuests();
			USkillComponent* S = K->GetSkills();
			if (Sub == 0)
			{
				for (int32 i = S->Slots.Num() - 1; i >= FMath::Max(0, S->Slots.Num() - 6); --i) S->Slots[i] = FInvSlot();   // (room for the three things and the saddle: the test pack starts full)
				S->AddInvItem(JeebQuests::ItemFromName(TEXT("Logs")), 4);
				S->AddInvItem(JeebQuests::ItemFromName(TEXT("TinOre")), 2);
				const bool bHide = S->AddInvItem(JeebQuests::ItemFromName(TEXT("Hide")), 1);
				Log(FString::Printf(TEXT("pack: %d logs, %d tin, hide added %d"), S->CountItem(JeebQuests::ItemFromName(TEXT("Logs"))), S->CountItem(JeebQuests::ItemFromName(TEXT("TinOre"))), bHide ? 1 : 0));
				Q->ServerQuest_Implementation(uint8(UQuestComponent::EOp::Accept), JeebHorse::QuestKey);
			}
			else if (Sub <= 3) Q->ServerQuest_Implementation(uint8(UQuestComponent::EOp::Talk), FName(TEXT("Bram")));
			Log(FString::Printf(TEXT("quest: after ask %d the step is %d"), Sub, Step(K)));
			if (++Sub <= 3) { NextAt = Clock + 1.5f; return; }
			Check(Step(K) == 3, FString::Printf(TEXT("accepted and all three things brought: on the tame step (step %d)"), Step(K)));
			Next(0.5f); return;
		}
		case 2:   // tame: on a wild horse for HorseTameSecs
		{
			AHorse* Wild = nullptr; float Best = 1.0e9f;
			for (TActorIterator<AHorse> It(W); It; ++It) if (It->IsWild() && !It->IsDown() && !It->HasRider()) { const float D = Dist(*It, K); if (D < Best) { Best = D; Wild = *It; } }
			if (!Wild) { Check(false, TEXT("a wild horse to tame")); Stage = 900; return; }
			const FVector2D Hp(Wild->GetActorLocation());
			Place(K, Hp + FVector2D(Wild->GetActorRightVector()) * 150.f, Hp);
			K->ServerHorse_Implementation(AKnightCharacter::HO_Mount, Wild);
			Horse = Wild;
			Check(K->IsMounted() && K->GetRideHorse() == Wild && Wild->GetRider() == K, FString::Printf(TEXT("the first player climbs on wild %s"), *Wild->GetName()));
			Next(JeebTune::V().HorseTameSecs + 1.5f); return;
		}
		case 3:
		{
			Check(H && H->TamedBy.Get() == K && Step(K) == 4, FString::Printf(TEXT("riding it %.0f s calms it: tamed by the player, quest on step %d"), JeebTune::V().HorseTameSecs, Step(K)));
			K->ServerHorse_Implementation(AKnightCharacter::HO_Dismount, nullptr);
			Check(!K->IsMounted() && H && !H->HasRider(), TEXT("E gets off"));
			ByBram(K, 0.f);
			Next(1.5f); return;
		}
		case 4:   // hand in: the horse is yours, with the saddle and Riding XP
		{
			const int32 XP0 = K->GetSkills()->GetXPTotal(ESkillType::Riding);
			K->GetQuests()->ServerQuest_Implementation(uint8(UQuestComponent::EOp::Talk), FName(TEXT("Bram")));
			AHorse* Mine = K->GetRider()->MyHorse();
			const int32 Saddles = K->GetSkills()->CountItem(JeebHorse::SaddleId());
			Check(Step(K) == 100 && K->GetRider()->bOwnsHorse && Mine == H && H && !H->IsWild() && H->GetOwnerKnight() == K,
				FString::Printf(TEXT("quest finished: the calmed horse is the player's own (%s), owner set, no longer wild"), Mine ? *Mine->GetName() : TEXT("none")));
			Check(Saddles >= 1 && K->GetSkills()->GetXPTotal(ESkillType::Riding) >= XP0 + 300, FString::Printf(TEXT("reward: %d saddle(s) in the pack, Riding XP %d -> %d"), Saddles, XP0, K->GetSkills()->GetXPTotal(ESkillType::Riding)));
			Next(1.f); return;
		}
		case 5:   // tap T from 60 m: it comes running
		{
			if (Sub == 0)
			{
				const FVector2D Hp(H->GetActorLocation());
				FVector2D To = Hp + (FVector2D(JeebHorse::Stables()[0].Wild) - Hp).GetSafeNormal() * 6000.f;
				Place(K, To, Hp);
				Place(K2, To + FVector2D(300.f, 300.f), Hp);
				K->ServerHorse_Implementation(AKnightCharacter::HO_Call, nullptr);
				Log(FString::Printf(TEXT("call: the horse is %.0f m away"), Dist(H, K)));
				Sub = 1; NextAt = Clock + 1.f; return;
			}
			if (Dist(H, K) > 6.f && StageT < 25.f) { NextAt = Clock + 1.f; return; }
			Check(Dist(H, K) <= 6.f, FString::Printf(TEXT("T: the horse ran to its owner in %.0f s (now %.1f m away)"), StageT, Dist(H, K)));
			Next(1.f); return;
		}
		case 6:   // the owner rides; then the second player rides it
		{
			if (Sub == 0)
			{
				Place(K, FVector2D(H->GetActorLocation()) + FVector2D(H->GetActorRightVector()) * 150.f, FVector2D(H->GetActorLocation()));
				K->ServerHorse_Implementation(AKnightCharacter::HO_Mount, H);
				Check(K->IsMounted() && H->GetRider() == K, TEXT("the owner rides their horse"));
				Sub = 1; NextAt = Clock + 5.f; return;   // (the players' machines take pictures while he sits on it)
			}
			K->ServerHorse_Implementation(AKnightCharacter::HO_Dismount, nullptr);
			Place(K, FVector2D(H->GetActorLocation()) + FVector2D(1200.f, 0.f), FVector2D(H->GetActorLocation()));
			Place(K2, FVector2D(H->GetActorLocation()) - FVector2D(H->GetActorRightVector()) * 150.f, FVector2D(H->GetActorLocation()));
			K2->ServerHorse_Implementation(AKnightCharacter::HO_Mount, H);
			Check(K2 && K2->IsMounted() && H->GetRider() == K2, TEXT("another player may ride your horse"));
			Next(5.f); return;
		}
		case 7:   // the owner taps T: the other rider is put down and the horse comes
		{
			if (Sub == 0)
			{
				K->ServerHorse_Implementation(AKnightCharacter::HO_Call, nullptr);
				Check(K2 && !K2->IsMounted() && !H->HasRider(), TEXT("the owner whistles: the other rider is put down at once"));
				Sub = 1; NextAt = Clock + 1.f; return;
			}
			if (Dist(H, K) > 6.f && StageT < 20.f) { NextAt = Clock + 1.f; return; }
			Check(Dist(H, K) <= 6.f, FString::Printf(TEXT("and it comes to its owner (%.1f m)"), Dist(H, K)));
			Next(0.5f); return;
		}
		case 8:   // hold T: it follows; walk 35 m (in steps) and it keeps up
		{
			if (Sub == 0)
			{
				K->ServerHorse_Implementation(AKnightCharacter::HO_Follow, nullptr);
				Check(H->IsFollowing(), TEXT("holding T: the horse follows"));
				Was = FVector2D(K->GetActorLocation());
			}
			if (Sub < 5)
			{
				const FVector2D Dir = (Yard - Was).GetSafeNormal();
				Place(K, Was + Dir * 700.f * (Sub + 1), Was + Dir * 9000.f);
				++Sub; NextAt = Clock + 2.5f; return;
			}
			if (Dist(H, K) > 9.f && StageT < 25.f) { NextAt = Clock + 1.f; return; }
			Check(Dist(H, K) <= 9.f, FString::Printf(TEXT("it kept up over 35 m (%.1f m behind)"), Dist(H, K)));
			K->ServerHorse_Implementation(AKnightCharacter::HO_Follow, nullptr);
			Check(!H->IsFollowing(), TEXT("holding T again: it stays put"));
			Next(1.f); return;
		}
		case 9:   // far away (250 m): T and it appears near you (the catch-up)
		{
			const FVector2D Hp(H->GetActorLocation());
			const FVector2D Away = Hp + (Hp - Yard).GetSafeNormal() * 25000.f;
			Place(K, Away, Hp);
			NextAt = Clock + 1.f;
			if (Sub++ == 0) return;
			K->ServerHorse_Implementation(AKnightCharacter::HO_Call, nullptr);
			Check(Dist(H, K) < 20.f, FString::Printf(TEXT("from %.0f m T makes it appear beside you (%.1f m)"), FVector2D::Distance(Hp, FVector2D(K->GetActorLocation())) / 100.f, Dist(H, K)));
			Next(4.f); return;
		}
		case 10:   // outside a PvP zone nothing hurts it
		{
			Hp0 = H->Hp->Cur;
			H->Hp->ApplyDamage(40, K2, false);
			H->Hp->ApplyDamage(40, K, false, true);
			Check(H->Hp->Cur == Hp0 && !JeebPvp_Test(W, H), FString::Printf(TEXT("players' blows outside a PvP zone do nothing (%d/%d)"), H->Hp->Cur, H->Hp->Max));
			Next(0.5f); return;
		}
		case 11:   // it falls (forced: there are no PvP zones yet): back at its stable, resting, and T can't call it
		{
			if (Sub == 0)
			{
				H->Hp->Cur = 0;
				H->Die(K2);
				Check(H->IsDown(), TEXT("a fallen horse is down"));
				Sub = 1; NextAt = Clock + 7.f; return;
			}
			const float Home = FVector2D::Distance(FVector2D(H->GetActorLocation()), H->GetStable() ? H->GetStable()->StallSpot() : Yard) / 100.f;
			Check(!H->IsDown() && Home < 3.f && H->RestLeft() > 0.f && H->Hp->Cur == H->Hp->Max, FString::Printf(TEXT("it is back at its stable (%.1f m from the stall), healed, resting %.0f s"), Home, H->RestLeft()));
			const FVector2D At(H->GetActorLocation());
			K->ServerHorse_Implementation(AKnightCharacter::HO_Call, nullptr);
			Check(FVector2D::Distance(At, FVector2D(H->GetActorLocation())) < 1.f, TEXT("while it rests, T doesn't bring it"));
			Next(0.5f); return;
		}
		case 12:
			Log(FString::Printf(TEXT("DONE: %d passed, %d failed"), Passes, Fails));
			Stage = 1000; return;
		case 900:
			Log(FString::Printf(TEXT("STOPPED early: %d passed, %d failed"), Passes, Fails));
			Stage = 1000; return;
		default: return;
		}
	}
	static bool JeebPvp_Test(UWorld* W, AHorse* H);

	// A player's machine: what reaches it, and a picture when someone rides a horse near it
	static void Client(AStable* St, float Dt)
	{
		static float Clock = 0.f, NextLog = 0.f, QuitAt = -1.f, ShotAt = -1.f;
		static FString ShotName;
		static TSet<FString> Taken;
		Clock += Dt;
		APlayerController* PC = St->GetWorld()->GetFirstPlayerController();   // (test pictures and logs only: this machine's own view)
		AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
		if (QuitAt < 0.f) { float S = 300.f; FParse::Value(FCommandLine::Get(), TEXT("HorseTestClientTime="), S); QuitAt = S; }
		if (Clock > QuitAt) { if (PC) PC->ConsoleCommand(TEXT("quit")); QuitAt = 1.0e9f; return; }
		if (!K) return;
		static TMap<TWeakObjectPtr<AHorse>, FString> Seen;
		AHorse* Ridden = nullptr;
		FString Near;
		int32 N = 0;
		for (TActorIterator<AHorse> It(St->GetWorld()); It; ++It)
		{
			AHorse* H = *It; ++N;
			const FString Now = FString::Printf(TEXT("%s%s%s, rider %s, owner %s, hp %d/%d"), H->IsWild() ? TEXT("wild") : TEXT("owned"), H->IsSaddled() ? TEXT(" saddled") : TEXT(""), H->IsDown() ? TEXT(" DOWN") : TEXT(""),
				H->GetRider() ? *H->GetRider()->GetName() : TEXT("none"), H->GetOwnerKnight() ? *H->GetOwnerKnight()->GetName() : TEXT("none"), H->Hp->Cur, H->Hp->Max);
			FString& Was = Seen.FindOrAdd(H);
			if (Now != Was) { UE_LOG(LogTemp, Log, TEXT("JEEBHORSECLIENT %s: %s at %.0f %.0f (%.1f m from me)"), *H->GetName(), *Now, H->GetActorLocation().X, H->GetActorLocation().Y, Dist(H, K)); Was = Now; }
			if (H->HasRider() && Dist(H, K) < 25.f) Ridden = H;
			if (Dist(H, K) < 60.f) Near += FString::Printf(TEXT(" [%s %.1f m%s]"), *H->GetName(), Dist(H, K), H->HasRider() ? TEXT(" ridden") : TEXT(""));
		}
		// pictures: this machine's camera turns to the ridden horse; the picture comes a second later
		if (ShotAt > 0.f)
		{
			if (Ridden) PC->SetControlRotation(((Ridden->GetActorLocation() + FVector(0.f, 0.f, 120.f)) - (K->GetActorLocation() + FVector(0.f, 0.f, 150.f))).Rotation());
			if (Clock >= ShotAt)
			{
				ShotAt = -1.f;
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("horse_%s_%s.png"), *ShotName, *K->GetName()), false, false);
				UE_LOG(LogTemp, Log, TEXT("JEEBHORSECLIENT picture horse_%s"), *ShotName);
			}
		}
		else if (Ridden)
		{
			const FString Name = Ridden->GetRider() == K ? (Ridden->IsWild() ? TEXT("me_wild") : TEXT("me_own")) : (Ridden->IsWild() ? TEXT("other_wild") : TEXT("other_own"));
			if (!Taken.Contains(Name)) { Taken.Add(Name); ShotName = Name; ShotAt = Clock + 1.2f; }
		}
		if (Clock < NextLog) return;
		NextLog = Clock + 2.f;
		UE_LOG(LogTemp, Log, TEXT("JEEBHORSECLIENT t=%.0f %s at %.0f %.0f mounted %d (%s), owns horse %d: %d horses sent here;%s"), Clock, *K->GetName(), K->GetActorLocation().X, K->GetActorLocation().Y,
			K->IsMounted() ? 1 : 0, K->GetRideHorse() ? *K->GetRideHorse()->GetName() : TEXT("-"), K->GetRider() && K->GetRider()->bOwnsHorse ? 1 : 0, N, *Near);
	}
};

#include "PvpZones.h"
bool FHorseTestDriver::JeebPvp_Test(UWorld* W, AHorse* H) { return JeebPvp::IsInPvpZone(W, H->GetActorLocation()); }

void JeebHorseTestTick(AStable* St, float Dt)
{
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("JeebHorseTest"));
	if (!bTest || !St || St != AStable::Find(St->GetWorld(), JeebHorse::Stables().Num() ? JeebHorse::Stables()[0].Key : NAME_None)) return;
	if (St->GetNetMode() == NM_DedicatedServer) FHorseTestDriver::Server(St, Dt);
	else if (St->GetNetMode() == NM_Client) FHorseTestDriver::Client(St, Dt);
}
#endif
