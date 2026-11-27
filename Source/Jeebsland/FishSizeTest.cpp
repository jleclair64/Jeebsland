// Online test of fish sizes in the pack (2026-11-26, Docs/FishingPlan.md "Fish in the pack"; Tools/fishsize_net.sh). Non-Shipping only.
// The server (-FishSizeNetTest) puts the first player on a river bank with a rod, and the player's own machine (-FishSizeDo) casts and reels in for real
// (the bite sets off its click: KnightCharacterNet.cpp ClientFishBite) until two sized fish are in the pack. Then the server stands him at the store, at the
// bank and so on, and his machine, going by what it sees in its own pack and bank, sells one fish, banks the other, takes it out, drops it and picks it up.
// The server logs JEEBFISHSIZETEST PASS/FAIL at every step; the machine logs JEEBFISHSIZEDO (what it sees).
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Pond.h"
#include "Loot.h"
#include "OakHill.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "JeebProps.h"
#include "JeebSave.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerController.h"

#if !UE_BUILD_SHIPPING
namespace
{
	FString PackText(const TArray<FInvSlot>& Arr)
	{
		FString Out;
		for (int32 i = 0; i < Arr.Num(); ++i)
			if (JeebProps::IsFishItem(Arr[i].Id) && Arr[i].Count > 0) Out += FString::Printf(TEXT("[%d: %s x%d size %d] "), i, *JeebItems::ItemName(Arr[i].Id), Arr[i].Count, Arr[i].Size);
		return Out.IsEmpty() ? FString(TEXT("(no fish)")) : Out;
	}
	TArray<int32> SizedSizes(const TArray<FInvSlot>& Arr) { TArray<int32> R; for (const FInvSlot& S : Arr) if (S.IsSized() && JeebProps::IsFishItem(S.Id)) R.Add(S.Size); R.Sort(); return R; }
	int32 FirstSized(const TArray<FInvSlot>& Arr) { for (int32 i = 0; i < Arr.Num(); ++i) if (Arr[i].IsSized() && JeebProps::IsFishItem(Arr[i].Id)) return i; return -1; }
	AFishingSpot* FirstRiverSpot(UWorld* W) { for (TActorIterator<AFishingSpot> It(W); It; ++It) if (It->IsRiverSpot()) return *It; return nullptr; }
	const JeebOakHill::FCounter* CounterFor(JeebOakHill::EShop S) { for (const JeebOakHill::FCounter& C : JeebOakHill::Counters()) if (C.Shop == S) return &C; return nullptr; }
	void Stand(AKnightCharacter* K, const FVector2D& P, float Yaw)
	{
		K->TeleportTo(FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) + 120.f), FRotator(0.f, Yaw, 0.f), false, true);
		if (APlayerController* PC = Cast<APlayerController>(K->GetController())) PC->SetControlRotation(FRotator(-5.f, Yaw, 0.f));
	}
}
#endif

void AKnightCharacter::TickFishSizeTest(float Dt)
{
#if !UE_BUILD_SHIPPING
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("FishSizeNetTest"));
	static const bool bDo = FParse::Param(FCommandLine::Get(), TEXT("FishSizeDo"));
	if (!Skills || (!bTest && !bDo)) return;
	FishTestT += Dt;
	auto Log = [](const FString& S) { UE_LOG(LogTemp, Log, TEXT("%s"), *S); };
	// ------------------------------------------------------------------ the server's side
	if (bTest && HasAuthority() && GetController())
	{
		static int32 Stage = 0; static float StageT = 0.f; static TArray<int32> Caught; static int32 Ingots0 = 0, Passes = 0, Fails = 0;
		static TWeakObjectPtr<AKnightCharacter> Who;
		if (!Who.IsValid()) Who = this;
		if (Who.Get() != this) return;
		StageT += Dt;
		auto Next = [&]() { ++Stage; StageT = 0.f; };
		auto Check = [&](bool bOk, const FString& What) { Log(FString::Printf(TEXT("JEEBFISHSIZETEST %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); (bOk ? Passes : Fails)++; };
		UWorld* W = GetWorld();
		switch (Stage)
		{
		case 0:   // the bank of a river, a rod, an empty pack and bank
		{
			if (FishTestT < 6.f) return;
			AFishingSpot* Spot = FirstRiverSpot(W);
			if (!Spot) { Log(TEXT("JEEBFISHSIZETEST FAIL: no river spot")); Stage = 900; return; }
			for (FInvSlot& S : Skills->Slots) S = FInvSlot();
			for (FInvSlot& S : Skills->Bank) S = FInvSlot();
			Skills->Hotbar[0] = JeebItems::GearId(JeebItems::K_Rod, 0); Skills->SetSelected(0);
			RefreshEquipment();
			const FVector2D Sp(Spot->GetActorLocation());
			FVector2D Near = Sp; float Best = 1.0e12f;
			for (const JeebWorld::FRiver& R : JeebWorld::Rivers()) for (const FVector2D& P : R.Pts) { const float D = FVector2D::DistSquared(P, Sp); if (D < Best) { Best = D; Near = P; } }
			const FVector2D Out = (Sp - Near).GetSafeNormal();
			Stand(this, Sp + Out * 350.f, FMath::RadiansToDegrees(FMath::Atan2(-Out.Y, -Out.X)));
			Log(FString::Printf(TEXT("JEEBFISHSIZETEST start: at the river spot %.0f %.0f, pack and bank emptied, rod in hand"), Sp.X, Sp.Y));
			Next(); return;
		}
		case 1:   // two real catches
		{
			const TArray<int32> Z = SizedSizes(Skills->Slots);
			if (Z.Num() < 2) { if (StageT > 170.f) { Check(false, TEXT("two fish were caught in time")); Stage = 900; } return; }
			Caught = Z;
			Log(FString::Printf(TEXT("JEEBFISHSIZETEST server pack: %s"), *PackText(Skills->Slots)));
			Check(Z.Num() == 2 && Z[0] > 0 && Z[1] > 0, FString::Printf(TEXT("two caught fish are two slots, each with its own size (%d and %d tenths of an inch)"), Z[0], Z[1]));
			int32 N = 0; for (const FInvSlot& S : Skills->Slots) if (JeebProps::IsFishItem(S.Id)) N += S.Count;
			Check(N == 2, FString::Printf(TEXT("they did not stack (%d fish)"), N));
			if (const JeebOakHill::FCounter* C = CounterFor(JeebOakHill::EShop::Store)) { Ingots0 = Skills->GetIngots(); Stand(this, C->At, C->Facing); Log(TEXT("JEEBFISHSIZETEST at the store counter")); Next(); }
			else { Check(false, TEXT("a store counter")); Stage = 900; }
			return;
		}
		case 2:   // the machine sells one fish: one slot goes, the other keeps its size
		{
			const TArray<int32> Z = SizedSizes(Skills->Slots);
			if (Z.Num() == 2) { if (StageT > 40.f) { Check(false, TEXT("one fish sold")); Stage = 900; } return; }
			Check(Z.Num() == 1 && Caught.Contains(Z[0]), FString::Printf(TEXT("one fish sold, the other still has its own size %d"), Z.Num() ? Z[0] : -1));
			Check(Skills->GetIngots() > Ingots0, FString::Printf(TEXT("paid for it: ingots %d -> %d"), Ingots0, Skills->GetIngots()));
			if (const JeebOakHill::FCounter* C = CounterFor(JeebOakHill::EShop::Bank)) { Caught = Z; Stand(this, C->At, C->Facing); Log(TEXT("JEEBFISHSIZETEST at the bank counter")); Next(); }
			else { Check(false, TEXT("a bank counter")); Stage = 900; }
			return;
		}
		case 3:   // banked
		{
			if (SizedSizes(Skills->Bank).Num() == 0) { if (StageT > 40.f) { Check(false, TEXT("the fish was banked")); Stage = 900; } return; }
			const TArray<int32> B = SizedSizes(Skills->Bank);
			Check(B.Num() == 1 && B[0] == Caught[0] && SizedSizes(Skills->Slots).Num() == 0, FString::Printf(TEXT("banked with its size kept (%d), pack empty of fish"), B[0]));
			Next(); return;
		}
		case 4:   // taken out again
		{
			if (SizedSizes(Skills->Slots).Num() == 0) { if (StageT > 40.f) { Check(false, TEXT("the fish was withdrawn")); Stage = 900; } return; }
			const TArray<int32> Z = SizedSizes(Skills->Slots);
			Check(Z.Num() == 1 && Z[0] == Caught[0] && SizedSizes(Skills->Bank).Num() == 0, FString::Printf(TEXT("withdrawn with its size kept (%d), bank empty of fish"), Z[0]));
			Next(); return;
		}
		case 5:   // dropped on the ground: the drop carries the size
		{
			if (SizedSizes(Skills->Slots).Num() == 1) { if (StageT > 40.f) { Check(false, TEXT("the fish was dropped")); Stage = 900; } return; }
			ALootDrop* Drop = nullptr; for (TActorIterator<ALootDrop> It(W); It; ++It) Drop = *It;
			bool bOk = false; int32 Sz = 0;
			if (Drop) for (const FInvSlot& S : Drop->GetItems()) if (S.IsSized()) { bOk = true; Sz = S.Size; }
			Check(bOk && Sz == Caught[0], FString::Printf(TEXT("the dropped fish keeps its size (%d) and is not in the pack (%d fish left)"), Sz, SizedSizes(Skills->Slots).Num()));
			Next(); return;
		}
		case 6:   // picked up again
		{
			if (SizedSizes(Skills->Slots).Num() == 0) { if (StageT > 40.f) { Check(false, TEXT("the fish was picked up")); Stage = 900; } return; }
			const TArray<int32> Z = SizedSizes(Skills->Slots);
			Check(Z.Num() == 1 && Z[0] == Caught[0], FString::Printf(TEXT("picked up again with its size kept (%d)"), Z[0]));
						Next(); return;
		}
		case 7:   // an old save (no sizes in it) loads, its fish a plain stack; a new save round-trips its sizes
		{
			UJeebSave* Old = UJeebSave::Capture(Skills, this);
			if (!Old) { Check(false, TEXT("a save to start from")); Stage = 900; return; }
			const int32 Keep = Caught[0];
			UJeebSave* New = UJeebSave::Capture(Skills, this);
			Old->SlotSizes.Empty(); Old->BankSizes.Empty();   // (as the game wrote saves before: the arrays were not there)
			Old->SlotIds[0] = JeebItems::MaterialId(EItemType::Trout); Old->SlotCounts[0] = 7;
			const FString Dir = FPaths::ProjectSavedDir() / TEXT("Characters/fishsize_old.sav");
			UJeebSave::WriteFile(Old, Dir);
			UJeebSave* Back = UJeebSave::ReadFile(Dir);
			bool bApplied = Back && Back->ApplyTo(Skills);
			Check(bApplied && Skills->Slots[0].Id == JeebItems::MaterialId(EItemType::Trout) && Skills->Slots[0].Count == 7 && Skills->Slots[0].Size == 0, FString::Printf(TEXT("an old save (no sizes) loads: 7 trout as one stack with no size (count %d size %d)"), Skills->Slots[0].Count, Skills->Slots[0].Size));
			const FString Dir2 = FPaths::ProjectSavedDir() / TEXT("Characters/fishsize_new.sav");
			UJeebSave::WriteFile(New, Dir2);
			UJeebSave* Back2 = UJeebSave::ReadFile(Dir2);
			bApplied = Back2 && Back2->ApplyTo(Skills);
			Check(bApplied && SizedSizes(Skills->Slots).Num() == 1 && SizedSizes(Skills->Slots)[0] == Keep, FString::Printf(TEXT("a new save written to disk and read back keeps the size (%d)"), Keep));
			IFileManager::Get().Delete(*Dir); IFileManager::Get().Delete(*Dir2);
			FString Real;
			if (FParse::Value(FCommandLine::Get(), TEXT("OldSavePath="), Real))
			{
				UJeebSave* R = UJeebSave::ReadFile(Real);
				Check(R && R->SlotSizes.Num() == 0 && R->ApplyTo(Skills), FString::Printf(TEXT("the real old save %s (written before sizes) loads"), *Real));
			}
			Log(FString::Printf(TEXT("JEEBFISHSIZETEST DONE: %d passed, %d failed"), Passes, Fails));
			Stage = 1000; return;
		}
		case 900: Log(FString::Printf(TEXT("JEEBFISHSIZETEST DONE: %d passed, %d failed (stopped early)"), Passes, Fails)); Stage = 1000; return;
		}
		return;
	}
	// ------------------------------------------------------------------ the player's own machine
	if (bDo && IsLocallyControlled() && !HasAuthority())
	{
		static float Next = 0.f; static bool bSold = false, bBanked = false, bOut = false, bDropped = false, bTook = false;
		static int32 LastSig = -1;
		if (FishTestT < Next) return;
		Next = FishTestT + 2.5f;
		const FString Pk = PackText(Skills->Slots), Bk = PackText(Skills->Bank);
		const int32 Sig = Pk.Len() * 31 + Bk.Len();
		if (Sig != LastSig) { LastSig = Sig; Log(FString::Printf(TEXT("JEEBFISHSIZEDO pack: %s | bank: %s"), *Pk, *Bk)); }
		const FVector2D Me(GetActorLocation());
		const int32 Packed = SizedSizes(Skills->Slots).Num(), Banked = SizedSizes(Skills->Bank).Num();
		const JeebOakHill::FCounter* Store = CounterFor(JeebOakHill::EShop::Store);
		const JeebOakHill::FCounter* Bank = CounterFor(JeebOakHill::EShop::Bank);
		if (!bSold && Packed < 2)
		{
			if (AFishingSpot* Spot = FirstRiverSpot(GetWorld())) if (FVector2D::Distance(Me, FVector2D(Spot->GetActorLocation())) < 700.f) Inv(EInvOp::Gather, 0, 0, 0, nullptr, Spot->GetActorLocation());
		}
		else if (!bSold && Packed == 2 && Store && FVector2D::Distance(Me, Store->At) < 300.f)
		{
			const int32 I = FirstSized(Skills->Slots);
			Log(FString::Printf(TEXT("JEEBFISHSIZEDO selling slot %d"), I));
			Inv(EInvOp::SellSlot, I, Skills->Slots[I].Id); bSold = true;
		}
		else if (bSold && !bBanked && Packed == 1 && Bank && FVector2D::Distance(Me, Bank->At) < 300.f)
		{
			Log(TEXT("JEEBFISHSIZEDO depositing")); Inv(EInvOp::BankDeposit, FirstSized(Skills->Slots)); bBanked = true;
		}
		else if (bBanked && !bOut && Banked == 1)
		{
			int32 I = -1; for (int32 i = 0; i < Skills->Bank.Num(); ++i) if (Skills->Bank[i].IsSized()) I = i;
			Log(FString::Printf(TEXT("JEEBFISHSIZEDO withdrawing bank slot %d"), I)); Inv(EInvOp::BankWithdraw, I); bOut = true;
		}
		else if (bOut && !bDropped && Packed == 1)
		{
			Log(TEXT("JEEBFISHSIZEDO dropping")); Inv(EInvOp::DropPack, FirstSized(Skills->Slots), 1); bDropped = true;
		}
		else if (bDropped && !bTook && Packed == 0)
		{
			ALootDrop* Best = nullptr; float BD = 1.0e9f;
			for (TActorIterator<ALootDrop> It(GetWorld()); It; ++It) { const float D = FVector::Dist(It->GetActorLocation(), GetActorLocation()); if (D < BD) { BD = D; Best = *It; } }
			if (Best) { bool bSz = false; for (const FInvSlot& S : Best->GetItems()) bSz |= S.IsSized(); Log(FString::Printf(TEXT("JEEBFISHSIZEDO sees the drop %.0f cm away: %s (sized %d)"), BD, *Best->Describe(), bSz ? 1 : 0)); Inv(EInvOp::LootTakeAll, 0, 0, 0, Best); bTook = true; }
		}
	}
#endif
}
