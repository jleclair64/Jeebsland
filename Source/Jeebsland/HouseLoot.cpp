#include "HouseLoot.h"
#include "HouseInterior.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "QuestData.h"
#include "Npc.h"
#include "Stealth.h"
#include "Guards.h"
#include "JeebTuning.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogJeebLoot, Log, All);

namespace JeebHouseLoot
{
	namespace
	{
		TArray<FLootBox>& Boxes() { static TArray<FLootBox> B; return B; }

		struct FLootRow { int32 Tier = 0; int32 Id = 0; int32 Min = 1, Max = 1; float Weight = 1.f; };   // Id 0 = ingots
		const TArray<FLootRow>& Table()
		{
			static const TArray<FLootRow> Rows = []()
			{
				TArray<FLootRow> Out;
				TArray<FString> Lines;
				const FString Path = FPaths::ProjectContentDir() / TEXT("Data/HouseLoot.csv");
				if (!FFileHelper::LoadFileToStringArray(Lines, *Path)) { UE_LOG(LogJeebLoot, Error, TEXT("Jeebsland loot: cannot read %s"), *Path); return Out; }
				for (int32 n = 1; n < Lines.Num(); ++n)
				{
					const FString L = Lines[n].TrimStartAndEnd();
					if (L.IsEmpty() || L.StartsWith(TEXT("#"))) continue;
					TArray<FString> C; L.ParseIntoArray(C, TEXT(","), false);
					if (C.Num() < 5) continue;
					FLootRow R;
					R.Tier = FCString::Atoi(*C[0]);
					const FString Name = C[1].TrimStartAndEnd();
					if (Name.Equals(TEXT("Ingots"), ESearchCase::IgnoreCase)) R.Id = 0;
					else
					{
						R.Id = JeebQuests::ItemFromName(Name);
						if (R.Id <= 0 || JeebItems::ItemName(R.Id).IsEmpty()) { UE_LOG(LogJeebLoot, Error, TEXT("Jeebsland loot: HouseLoot.csv line %d: no item called \"%s\""), n + 1, *Name); continue; }
						if (JeebItems::IsVaulted(R.Id)) { UE_LOG(LogJeebLoot, Log, TEXT("HouseLoot.csv line %d: %s is vaulted (off in the game), left out"), n + 1, *Name); continue; }
					}
					R.Min = FMath::Max(1, FCString::Atoi(*C[2]));
					R.Max = FMath::Max(R.Min, FCString::Atoi(*C[3]));
					R.Weight = FMath::Max(0.f, FCString::Atof(*C[4]));
					Out.Add(R);
				}
				UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT table: %d rows"), Out.Num());
				return Out;
			}();
			return Rows;
		}

		// What a container gives: drawn from its tier (1 thing at tier 0, 2 at tiers 1-2, 3 at tier 3)
		void Roll(int32 Tier, TArray<FInvSlot>& OutItems, int32& OutIngots)
		{
			OutIngots = 0;
			TArray<const FLootRow*> Pool;
			float Total = 0.f;
			for (const FLootRow& R : Table()) if (R.Tier == Tier && R.Weight > 0.f) { Pool.Add(&R); Total += R.Weight; }
			if (Pool.Num() == 0) return;
			const int32 Draws = Tier == 0 ? 1 : Tier >= 3 ? 3 : 2;
			for (int32 d = 0; d < Draws; ++d)
			{
				float P = FMath::FRand() * Total;
				const FLootRow* Pick = Pool.Last();
				for (const FLootRow* R : Pool) { P -= R->Weight; if (P <= 0.f) { Pick = R; break; } }
				const int32 N = FMath::RandRange(Pick->Min, Pick->Max);
				if (Pick->Id == 0) { OutIngots += N; continue; }
				if (FInvSlot* Have = OutItems.FindByPredicate([&](const FInvSlot& S) { return S.Id == Pick->Id; })) Have->Count += N;
				else OutItems.Add(FInvSlot(Pick->Id, N));
			}
		}

		FString PlayerName(const AKnightCharacter* K)
		{
			const APlayerState* PS = K ? K->GetPlayerState() : nullptr;
			return PS ? PS->GetPlayerName() : (K ? K->GetName() : FString(TEXT("?")));
		}

		int32 FreeSlots(const USkillComponent* Sk)
		{
			int32 N = 0;
			for (const FInvSlot& S : Sk->Slots) if (S.Id == 0 || S.Count <= 0) ++N;
			return N;
		}

		// (server) the checks every request shares; null (and a message) if refused
		FLootBox* Checked(AKnightCharacter* K, const FVector& At, const TCHAR* What)
		{
			USkillComponent* Sk = K ? K->GetSkills() : nullptr;
			if (!Sk || !K->HasAuthority()) return nullptr;
			const float Now = K->GetWorld()->GetTimeSeconds();
			FLootBox* B = FindAt(K->GetWorld(), At);
			if (!B) { UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s %s: refused, no container there"), *PlayerName(K), What); return nullptr; }
			const FVector Me = K->GetActorLocation();
			if (FVector::Dist2D(Me, B->At) > JeebTune::V().LootReach + 60.f || FMath::Abs(Me.Z - B->At.Z) > 250.f)
			{
				UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s %s %s: refused, too far (%.0f cm)"), *PlayerName(K), What, *B->Key, FVector::Dist(Me, B->At));
				return nullptr;
			}
			if (Now < K->BoxAskUntil) return nullptr;   // (one request a second)
			K->BoxAskUntil = Now + 1.f;
			if (const double* Bar = B->BarredUntil.Find(K); Bar && Now < *Bar)
			{
				Sk->Say(3.f, FColor::Orange, TEXT("Someone is watching. Better leave it for now."));
				UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s %s %s: refused, barred"), *PlayerName(K), What, *B->Key);
				return nullptr;
			}
			if (Now < B->RefillAt)
			{
				const int32 Min = FMath::CeilToInt((B->RefillAt - Now) / 60.0);
				Sk->Say(3.f, FColor::Orange, FString::Printf(TEXT("The %s is empty. Someone has been here (more in about %d min)."), KindName(B->Kind), Min));
				UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s %s %s: refused, empty for %.0f s more"), *PlayerName(K), What, *B->Key, B->RefillAt - Now);
				return nullptr;
			}
			if (FreeSlots(Sk) < 1)
			{
				Sk->Say(3.f, FColor::Orange, TEXT("Your pack is full."));
				UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s %s %s: refused, pack full"), *PlayerName(K), What, *B->Key);
				return nullptr;
			}
			return B;
		}

		// (server) the container's things into his pack; it is empty until it refills
		void Give(AKnightCharacter* K, FLootBox& B)
		{
			USkillComponent* Sk = K->GetSkills();
			TArray<FInvSlot> Items;
			int32 Ingots = 0;
			Roll(B.Tier, Items, Ingots);
			JeebGuards::NoteStolen(K, Items, Ingots);   // (a guard who catches him takes it back: Guards.h)
			B.RefillAt = K->GetWorld()->GetTimeSeconds() + JeebTune::V().LootRefillSeconds;
			TArray<FString> Got, Left;
			if (Ingots > 0) { Sk->AddIngots(Ingots); Got.Add(FString::Printf(TEXT("%d ingots"), Ingots)); }
			for (const FInvSlot& S : Items)
			{
				if (!Sk->AddInvItem(S.Id, S.Count)) { Left.Add(JeebItems::ItemName(S.Id)); continue; }   // (one free slot was checked; more things can still not fit)
				Got.Add(S.Count > 1 ? FString::Printf(TEXT("%d %s"), S.Count, *JeebItems::ItemName(S.Id)) : JeebItems::ItemName(S.Id));
			}
			const FString List = Got.Num() ? FString::Join(Got, TEXT(", ")) : FString(TEXT("nothing worth taking"));
			Sk->Say(4.f, FColor::Green, FString::Printf(TEXT("In the %s: %s.%s"), KindName(B.Kind), *List, Left.Num() ? *FString::Printf(TEXT(" No room for the %s: you leave it."), *FString::Join(Left, TEXT(", "))) : TEXT("")));
			UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s took from %s (lock %d, tier %d): %s"), *PlayerName(K), *B.Key, B.Lock, B.Tier, *List);
		}
	}

	bool IsContainerMesh(const FString& Mesh, EBoxKind& OutKind)
	{
		const FString N = FPaths::GetBaseFilename(Mesh);
		if (N == TEXT("SM_Int_Chest") || N == TEXT("SM_Chest_001")) { OutKind = EBoxKind::Chest; return true; }
		if (N == TEXT("SM_Closet_001")) { OutKind = EBoxKind::Cupboard; return true; }
		if (N == TEXT("SM_Crate") || N == TEXT("SM_CrateNoLid") || N == TEXT("SM_CrateOpen") || N.StartsWith(TEXT("SM_Box_00")) || N == TEXT("SM_Int_PotionCrate")) { OutKind = EBoxKind::Crate; return true; }
		return false;
	}

	const TCHAR* KindName(EBoxKind K) { return K == EBoxKind::Chest ? TEXT("chest") : K == EBoxKind::Cupboard ? TEXT("cupboard") : TEXT("crate"); }

	void AddBox(AHouseInterior* House, const FString& PlotId, int32 N, EBoxKind Kind, const FTransform& Xf, const FVector& RoomMid, float FloorWorldZ)
	{
		FLootBox B;
		B.House = House;
		B.Key = FString::Printf(TEXT("%s#%d"), *PlotId, N);
		B.Kind = Kind;
		B.At = Xf.GetLocation();
		B.Top = B.At + FVector(0.f, 0.f, Kind == EBoxKind::Cupboard ? 120.f : 65.f);
		const FVector In = (RoomMid - B.At).GetSafeNormal2D();
		B.Stand = FVector(B.At.X + In.X * 150.f, B.At.Y + In.Y * 150.f, FloorWorldZ + 100.f);
		// The lock: a hash of its house and number, the same on every machine. Chests mostly locked, cupboards sometimes, crates rarely.
		const uint32 H = FCrc::StrCrc32(*B.Key);
		const int32 LockedPct = Kind == EBoxKind::Chest ? 75 : Kind == EBoxKind::Cupboard ? 40 : 15;
		if (int32(H % 100u) < LockedPct)
		{
			static const int32 Levels[] = { 1, 1, 1, 5, 5, 10, 10, 15, 20, 20, 25, 29 };   // (the low ones commoner; the skill cap is 29 since 2026-11-26)
			B.Lock = Levels[(H / 100u) % UE_ARRAY_COUNT(Levels)];
		}
		B.Tier = B.Lock == 0 ? 0 : B.Lock < 10 ? 1 : B.Lock < 20 ? 2 : 3;
		Boxes().Add(B);
		UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT box %s %s at %.0f %.0f %.0f: lock %d, tier %d"), *B.Key, KindName(Kind), B.At.X, B.At.Y, B.At.Z, B.Lock, B.Tier);
	}

	void RemoveHouse(const AHouseInterior* House)
	{
		Boxes().RemoveAll([House](const FLootBox& B) { return !B.House.IsValid() || B.House.Get() == House; });
	}

	FLootBox* FindAt(const UWorld* World, const FVector& At)
	{
		for (FLootBox& B : Boxes())
			if (B.House.IsValid() && B.House->GetWorld() == World && FVector::Dist(B.At, At) < 5.f) return &B;
		return nullptr;
	}

	int32 NumBoxes(const UWorld* World)
	{
		int32 N = 0;
		for (const FLootBox& B : Boxes()) if (B.House.IsValid() && B.House->GetWorld() == World) ++N;
		return N;
	}

	FLootBox* InReach(const AActor* Knight)
	{
		if (!Knight) return nullptr;
		const FVector Me = Knight->GetActorLocation();
		const FVector Fwd = Knight->GetActorForwardVector().GetSafeNormal2D();
		const float Reach = JeebTune::V().LootReach;
		FLootBox* Best = nullptr;
		float BestD = Reach;
		for (FLootBox& B : Boxes())
		{
			if (!B.House.IsValid() || B.House->GetWorld() != Knight->GetWorld()) continue;
			const FVector To = B.At - Me;
			const float D = To.Size2D();
			if (D >= BestD || FMath::Abs(To.Z) > 200.f) continue;
			if (D > 60.f && FVector::DotProduct(Fwd, To.GetSafeNormal2D()) < 0.f) continue;   // (roughly in front of him)
			BestD = D; Best = &B;
		}
		return Best;
	}

	void Search(AKnightCharacter* K, const FVector& At)
	{
		FLootBox* B = Checked(K, At, TEXT("search"));
		if (!B) return;
		if (B->Lock > 0) { K->GetSkills()->Say(2.f, FColor::Orange, TEXT("It's locked.")); return; }
		Give(K, *B);
	}

	void StartPick(AKnightCharacter* K, const FVector& At)
	{
		if (K && K->GetWorldTimerManager().IsTimerActive(K->BoxPickTimer)) return;   // (already picking)
		FLootBox* B = Checked(K, At, TEXT("pick"));
		if (!B) return;
		USkillComponent* Sk = K->GetSkills();
		if (B->Lock <= 0) { Give(K, *B); return; }
		const int32 Lv = Sk->GetLevel(ESkillType::Lockpicking);
		if (Lv < B->Lock)
		{
			Sk->Say(3.f, FColor::Orange, FString::Printf(TEXT("The lock is too fine for you. Lockpicking level %d needed."), B->Lock));
			UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s pick %s: refused, level %d < %d"), *PlayerName(K), *B->Key, Lv, B->Lock);
			return;
		}
		const float Secs = FMath::Max(0.2f, JeebTune::V().LootPickSeconds);
		K->BoxPickAt = B->At;
		K->BoxAskUntil = K->GetWorld()->GetTimeSeconds() + Secs;   // (no new request until it is done)
		K->GetWorldTimerManager().SetTimer(K->BoxPickTimer, K, &AKnightCharacter::FinishBoxPick, Secs, false);
		K->ClientLockPick(Secs);
		UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s pick %s: started (level %d, lock %d)"), *PlayerName(K), *B->Key, Lv, B->Lock);
	}

#if !UE_BUILD_SHIPPING
	namespace
	{
		struct FNetTest
		{
			TWeakObjectPtr<AKnightCharacter> A, B;
			float Clock = 0.f, AAt = -1.f, BAt = -1.f;
			bool bAMoved = false, bBMoved = false, bAGot = false, bDone = false;
			int32 AItems0 = -1, ATries = 0, BTries = 0;
			float NextTry = 0.f;
			int32 Phase = 0;
		};
		FNetTest GT;
		int32 PackCount(const USkillComponent* Sk)
		{
			int32 N = 0;
			if (Sk) for (const FInvSlot& S : Sk->Slots) if (S.Id) N += S.Count;
			return N;
		}
		// The test's chest: the first locked container (the same on every machine); and one in a different house (the far one)
		FLootBox* TestBox(const UWorld* W) { for (FLootBox& B : Boxes()) if (B.House.IsValid() && B.House->GetWorld() == W && B.Lock > 0) return &B; return nullptr; }
		FLootBox* FarBox(const UWorld* W, const FLootBox* Near)
		{
			for (FLootBox& B : Boxes()) if (B.House.IsValid() && B.House->GetWorld() == W && B.House != Near->House && FVector::Dist(B.At, Near->At) > 1000.f) return &B;
			return nullptr;
		}
	}
#endif

	void TestTick(AHouseInterior* House, float Dt)
	{
#if !UE_BUILD_SHIPPING
		// Pictures, single player (-LootShot): the knight stands before each of the first -LootShots=N containers (8) in turn, a picture
		// of each with the HUD (Saved/Shots/loot_<house>_<n>.png), then picks the first locked one and takes the bar (loot_pickbar.png)
		static const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("LootShot"));
		if (bShot && House && House->GetNetMode() == NM_Standalone)
		{
			TArray<FLootBox*> All;
			for (FLootBox& B : Boxes()) if (B.House.IsValid() && B.House->GetWorld() == House->GetWorld()) All.Add(&B);
			if (All.Num() == 0 || All[0]->House.Get() != House) return;
			static int32 NShots = []() { int32 V = 8; FParse::Value(FCommandLine::Get(), TEXT("LootShots="), V); return V; }();
			static float Clock = 0.f;
			static int32 Step = -1;
			Clock += Dt;
			APlayerController* Pc = House->GetWorld()->GetFirstPlayerController();
			AKnightCharacter* Me = Pc ? Cast<AKnightCharacter>(Pc->GetPawn()) : nullptr;
			if (!Me || Clock < 10.f) return;
			const int32 S = FMath::FloorToInt((Clock - 10.f) / 5.f);
			const float In = Clock - 10.f - S * 5.f;
			const int32 N = FMath::Min(NShots, All.Num());
			FLootBox* Locked = nullptr;
			for (FLootBox* B : All) if (B->Lock > 0) { Locked = B; break; }
			FLootBox* B = S < N ? All[S] : (S == N ? Locked : nullptr);
			if (!B) { if (S > N) Pc->ConsoleCommand(TEXT("quit")); return; }
			if (S != Step)
			{
				Step = S;
				Me->SetActorLocation(B->Stand, false, nullptr, ETeleportType::TeleportPhysics);
				const float Yaw = (B->At - B->Stand).GetSafeNormal2D().Rotation().Yaw;
				Me->SetActorRotation(FRotator(0.f, Yaw, 0.f));
				Pc->SetControlRotation(FRotator(-35.f, Yaw + 30.f, 0.f));   // (a little to the side, so the knight does not hide it)
				UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTSHOT %s (%s, lock %d)"), *B->Key, KindName(B->Kind), B->Lock);
				if (S == N && Me->GetSkills()) { Me->GetSkills()->SetAllLevels(USkillComponent::MaxLevel); }
			}
			static int32 Took = -1, Picked = -1;
			if (S == N && In > 3.f && Picked != S) { Picked = S; Me->Inv(EInvOp::BoxPick, 0, 0, 0, nullptr, B->At); }
			const float ShotAt = S == N ? 3.8f : 3.f;
			if (In > ShotAt && Took != S)
			{
				Took = S;
				const FString Name = S == N ? FString(TEXT("loot_pickbar")) : FString::Printf(TEXT("loot_%s"), *B->Key.Replace(TEXT("#"), TEXT("_")));
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / (Name + TEXT(".png")), true, false);
			}
			return;
		}
		static const bool bServer = FParse::Param(FCommandLine::Get(), TEXT("LootNetTest"));
		static const bool bA = FParse::Param(FCommandLine::Get(), TEXT("LootNetA"));
		static const bool bB = FParse::Param(FCommandLine::Get(), TEXT("LootNetB"));
		if (!(bServer || bA || bB) || !House || House->GetNetMode() == NM_Standalone) return;
		UWorld* W = House->GetWorld();
		FLootBox* T = TestBox(W);
		if (!T || T->House.Get() != House) return;   // (one house runs it)
		FLootBox* Far = FarBox(W, T);
		GT.Clock += Dt;
		if (bServer)
		{
			for (TActorIterator<AKnightCharacter> It(W); It; ++It)
			{
				if (!It->GetController() || !It->GetSkills()) continue;
				if (!GT.A.IsValid() && GT.B.Get() != *It) { GT.A = *It; GT.AAt = GT.Clock; It->GetSkills()->SetAllLevels(USkillComponent::MaxLevel); UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTTEST player A is %s"), *PlayerName(*It)); }
				else if (!GT.B.IsValid() && GT.A.Get() != *It) { GT.B = *It; GT.BAt = GT.Clock; It->GetSkills()->SetAllLevels(USkillComponent::MaxLevel); UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTTEST player B is %s"), *PlayerName(*It)); }
			}
			auto Put = [&](AKnightCharacter* K)
			{
				K->SetActorLocation(T->Stand, false, nullptr, ETeleportType::TeleportPhysics);
				const float Yaw = (T->At - T->Stand).GetSafeNormal2D().Rotation().Yaw;
				K->SetActorRotation(FRotator(0.f, Yaw, 0.f));
				if (AController* C = K->GetController()) C->SetControlRotation(FRotator(-15.f, Yaw, 0.f));
			};
			if (GT.A.IsValid() && !GT.bAMoved && GT.Clock > GT.AAt + 8.f) { GT.bAMoved = true; Put(GT.A.Get()); UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTTEST A moved before %s (lock %d)"), *T->Key, T->Lock); }
			const bool bEmpty = W->GetTimeSeconds() < T->RefillAt;
			if (GT.A.IsValid() && GT.AItems0 < 0 && GT.bAMoved) GT.AItems0 = PackCount(GT.A->GetSkills()) + GT.A->GetSkills()->GetIngots();
			if (bEmpty && !GT.bAGot && GT.A.IsValid())
			{
				GT.bAGot = true;
				const int32 Now = PackCount(GT.A->GetSkills()) + GT.A->GetSkills()->GetIngots();
				UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTTEST %s A's pick opened %s: pack and ingots %d -> %d"), Now > GT.AItems0 ? TEXT("PASS") : TEXT("FAIL"), *T->Key, GT.AItems0, Now);
			}
			if (GT.bAGot && GT.B.IsValid() && !GT.bBMoved && GT.Clock > GT.BAt + 8.f) { GT.bBMoved = true; Put(GT.B.Get()); UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTTEST B moved before %s"), *T->Key); }
			return;
		}
		// The players' machines
		APlayerController* Pc = W->GetFirstPlayerController();
		AKnightCharacter* Me = Pc ? Cast<AKnightCharacter>(Pc->GetPawn()) : nullptr;
		if (!Me || GT.bDone) return;
		if (GT.Clock > 150.f) { GT.bDone = true; Pc->ConsoleCommand(TEXT("quit")); return; }
		if (FVector::Dist2D(Me->GetActorLocation(), T->Stand) > 150.f || GT.Clock < GT.NextTry) return;
		GT.NextTry = GT.Clock + 3.f;
		const int32 Have = PackCount(Me->GetSkills()) + (Me->GetSkills() ? Me->GetSkills()->GetIngots() : 0);
		if (GT.AItems0 < 0) GT.AItems0 = Have;
		if (bA)
		{
			if (Have > GT.AItems0) { UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTDO A: my pack and ingots went %d -> %d"), GT.AItems0, Have); GT.bDone = true; return; }
			if (GT.ATries++ < 10) { Me->Inv(EInvOp::BoxPick, 0, 0, 0, nullptr, T->At); UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTDO A: pick try %d at %s, pack %d"), GT.ATries, *T->Key, Have); }
		}
		else
		{
			if (GT.BTries < 2) { Me->Inv(EInvOp::BoxPick, 0, 0, 0, nullptr, T->At); UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTDO B: pick try at the emptied %s, pack %d"), *T->Key, Have); }
			else if (GT.BTries == 2 && Far) { Me->Inv(EInvOp::BoxPick, 0, 0, 0, nullptr, Far->At); UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTDO B: pick try at %s, %.0f m away"), *Far->Key, FVector::Dist(Me->GetActorLocation(), Far->At) / 100.f); }
			else if (GT.BTries == 3) Me->Inv(EInvOp::BoxSearch, 0, 0, 0, nullptr, Far ? Far->At : T->At);
			else { UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOTDO B: done, pack and ingots %d -> %d"), GT.AItems0, Have); GT.bDone = true; }
			++GT.BTries;
		}
#endif
	}
}

// ---------------- the knight's side ----------------

void AKnightCharacter::FinishBoxPick()
{
	using namespace JeebHouseLoot;
	USkillComponent* Sk = GetSkills();
	FLootBox* B = FindAt(GetWorld(), BoxPickAt);
	if (!Sk || !B || !HasAuthority()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Me = GetActorLocation();
	if (FVector::Dist2D(Me, B->At) > JeebTune::V().LootReach + 60.f || InClimbMove())
	{
		Sk->Say(2.f, FColor::Orange, TEXT("You stop picking the lock."));
		UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s pick %s: walked away"), *PlayerName(this), *B->Key);
		ClientLockPick(0.f);
		return;
	}
	if (Now < B->RefillAt) { Sk->Say(3.f, FColor::Orange, FString::Printf(TEXT("Someone emptied the %s first."), KindName(B->Kind))); return; }
	const int32 Lv = Sk->GetLevel(ESkillType::Lockpicking);
	const float Chance = FMath::Clamp(0.35f + 0.06f * (Lv - B->Lock), 0.35f, 0.95f);   // (the cottages' rule, KnightCharacterNet.cpp PickLock)
	if (FMath::FRand() < Chance)
	{
		Sk->AddXP(ESkillType::Lockpicking, 15 + 6 * B->Lock);
		UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s pick %s: success (chance %.2f)"), *PlayerName(this), *B->Key, Chance);
		Give(this, *B);
		return;
	}
	Sk->AddXP(ESkillType::Lockpicking, 3);
	// Heard? Halved while crouched, and Stealth takes it down to a quarter at 99
	const float Hear = JeebTune::V().LootAlertChance * (IsCrouchedNow() ? 0.5f : 1.f) * FMath::Lerp(1.f, 0.25f, USkillComponent::PowerBlend(Sk->GetLevel(ESkillType::Stealth)));
	ANpc* Heard = nullptr;
	if (FMath::FRand() < Hear)
	{
		float BestD = JeebTune::V().LootAlertRange;
		for (TActorIterator<ANpc> It(GetWorld()); It; ++It)
		{
			const float D = FVector::Dist(It->GetActorLocation(), Me);
			if (D < BestD) { BestD = D; Heard = *It; }
		}
	}
	if (Heard)
	{
		B->BarredUntil.Add(this, Now + JeebTune::V().LootBarredSeconds);
		Sk->Say(4.f, FColor::Red, FString::Printf(TEXT("%s hears the lock rattle: \"Hey! Hands off that!\""), *Heard->GetNpcName()));
		for (const TWeakObjectPtr<UStealthSense>& S : UStealthSense::All())
			if (S.IsValid() && S->GetOwner() && FVector::Dist(S->GetOwner()->GetActorLocation(), Me) < JeebTune::V().LootAlertRange) S->ForceAlert(this);
		UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s pick %s: failed, heard by %s"), *PlayerName(this), *B->Key, *Heard->GetNpcName());
		JeebGuards::RaiseAlarm(this, Heard);   // (they shout, the townsfolk near them look, a guard comes: Guards.h)
	}
	else
	{
		Sk->Say(2.f, FColor::Orange, TEXT("The pick slips. Try again."));
		UE_LOG(LogJeebLoot, Log, TEXT("JEEBLOOT %s pick %s: failed (chance %.2f)"), *PlayerName(this), *B->Key, Chance);
	}
}

void AKnightCharacter::ClientLockPick_Implementation(float Seconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Seconds <= 0.f) { LockPickEnd = -100.f; return; }
	LockPickStart = Now;
	LockPickEnd = Now + Seconds;
}
