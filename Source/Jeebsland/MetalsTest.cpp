// Online test of the skill cap, the metals, Knight's Armor and vaulting (2026-11-27; Tools/metals_net.sh). Non-Shipping only.
//   -MetalsTest  (server, one world, two players mtla and mtlb): mtla stands at Oak Hill's smithy and smelts bars through the same request the RPC
//                runs (DoInv EInvOp::Smelt): bronze at Smithing 1, steel refused at 1 and made at 10, a sword smithed from bars, a vaulted gold sword
//                refused. XP stops at 29. mtlb's character is put through the save loader with an old save (tiered Heavy and Light knight armour,
//                skills far past 29): it must load as Knight's Armor at level 29. A thousand hours of store stock and two thousand satchels: nothing vaulted.
//   -MetalsDo    (a player's machine): logs what its own pack and worn gear show once the server is done (JEEBMETALDO), to see it replicated.
// The server logs JEEBMETALTEST PASS / FAIL lines and JEEBMETALTEST DONE.
#include "KnightCharacter.h"
#include "KnightPlayerController.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Store.h"
#include "JeebSave.h"
#include "OakHill.h"
#include "Terrain.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

using namespace JeebItems;

#if !UE_BUILD_SHIPPING
namespace
{
	void MLog(const FString& S) { UE_LOG(LogTemp, Log, TEXT("%s"), *S); }
	int32 MPasses = 0, MFails = 0;
	void MCheck(bool bOk, const FString& What) { MLog(FString::Printf(TEXT("JEEBMETALTEST %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What)); (bOk ? MPasses : MFails)++; }
	AKnightCharacter* MByKey(UWorld* W, const TCHAR* Key)
	{
		for (TActorIterator<AKnightCharacter> It(W); It; ++It)
			if (const AKnightPlayerController* PC = Cast<AKnightPlayerController>(It->GetController())) if (PC->bLoggedIn && PC->AccountKey == Key) return *It;
		return nullptr;
	}
	int32 MCount(const USkillComponent* S, int32 Id) { int32 N = 0; for (const FInvSlot& Sl : S->Slots) if (Sl.Id == Id) N += Sl.Count; return N; }
}
#endif

void AKnightCharacter::TickMetalsTest(float Dt)
{
#if !UE_BUILD_SHIPPING
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("MetalsTest"));
	static const bool bDo = FParse::Param(FCommandLine::Get(), TEXT("MetalsDo"));
	if (!Skills) return;
	if (bDo && !HasAuthority() && IsLocallyControlled())
	{
		static float DoT = 0.f; static int32 Said = 0;
		DoT += Dt;
		if (DoT > 20.f * (Said + 1) && Said < 6)
		{
			++Said;
			FString Pack, Worn;
			for (const FInvSlot& S : Skills->Slots) if (S.Id) Pack += FString::Printf(TEXT("%s x%d; "), *ItemName(S.Id), S.Count);
			for (int32 K = K_Helmet; K <= K_Boots; ++K) Worn += FString::Printf(TEXT("%s; "), Skills->Worn[K] ? *ItemName(Skills->Worn[K]) : TEXT("-"));
			MLog(FString::Printf(TEXT("JEEBMETALDO my levels: Mining %d, Smithing %d; worn [%s]; pack [%s]"), Skills->GetLevel(ESkillType::Mining), Skills->GetLevel(ESkillType::Smithing), *Worn, *Pack));
		}
		return;
	}
	if (!bTest || !HasAuthority()) return;
	UWorld* W = GetWorld();
	static double LastFrame = -1.0;
	if (W->GetTimeSeconds() == LastFrame) return;
	LastFrame = W->GetTimeSeconds();
	static int32 Stage = 0; static float T = 0.f, StageT = 0.f;
	T += Dt; StageT += Dt;
	if (Stage >= 9999) return;
	auto Done = [&]() { MLog(FString::Printf(TEXT("JEEBMETALTEST DONE: %d passed, %d failed"), MPasses, MFails)); Stage = 9999; };
	AKnightCharacter* A = MByKey(W, TEXT("mtla"));
	AKnightCharacter* B = MByKey(W, TEXT("mtlb"));
	if (!A || !B) { if (T > 240.f) { MCheck(false, TEXT("both players logged in")); Done(); } return; }
	USkillComponent* SA = A->Skills; USkillComponent* SB = B->Skills;
	const int32 Cu = MaterialId(EItemType::CopperOre), Sn = MaterialId(EItemType::TinOre), Fe = MaterialId(EItemType::IronOre), Co = MaterialId(EItemType::Coal);
	const int32 BBar = MaterialId(EItemType::BronzeBar), SBar = MaterialId(EItemType::SteelBar);

	if (Stage == 0)
	{
		if (StageT < 8.f) return;
		const JeebOakHill::FCounter* Smith = nullptr;
		for (const JeebOakHill::FCounter& C : JeebOakHill::Counters()) if (C.Shop == JeebOakHill::EShop::Smith) { Smith = &C; break; }
		if (!Smith) { MCheck(false, TEXT("Oak Hill has a smithy counter")); Done(); return; }
		A->TeleportTo(FVector(Smith->At.X, Smith->At.Y, JeebTerrain::HeightAt(Smith->At.X, Smith->At.Y) + 120.f), FRotator(0.f, Smith->Facing, 0.f), false, true);
		for (FInvSlot& Sl : SA->Slots) Sl = FInvSlot();
		SA->SetAllLevels(1);
		SA->Slots[0] = FInvSlot(Cu, 3); SA->Slots[1] = FInvSlot(Sn, 3); SA->Slots[2] = FInvSlot(Fe, 2); SA->Slots[3] = FInvSlot(Co, 2);
		MLog(FString::Printf(TEXT("JEEBMETALTEST mtla at the smithy (%.0f %.0f), Smithing %d, 3 copper, 3 tin, 2 iron, 2 coal"), Smith->At.X, Smith->At.Y, SA->GetLevel(ESkillType::Smithing)));
		++Stage; StageT = 0.f; return;
	}
	if (Stage == 1 && StageT > 2.f)
	{
		A->DoInv(EInvOp::Smelt, int32(EItemType::BronzeBar), 1, 0, nullptr, FVector::ZeroVector);
		MCheck(MCount(SA, BBar) == 1 && MCount(SA, Cu) == 2 && MCount(SA, Sn) == 2, FString::Printf(TEXT("smelt 1 bronze bar at Smithing 1: %d bars, %d copper, %d tin left (expect 1, 2, 2)"), MCount(SA, BBar), MCount(SA, Cu), MCount(SA, Sn)));
		const int32 XP1 = SA->GetXPTotal(ESkillType::Smithing);
		A->DoInv(EInvOp::Smelt, int32(EItemType::BronzeBar), 28, 0, nullptr, FVector::ZeroVector);
		MCheck(MCount(SA, BBar) == 3 && MCount(SA, Cu) == 0 && MCount(SA, Sn) == 0 && SA->GetXPTotal(ESkillType::Smithing) > XP1, FString::Printf(TEXT("smelt all: %d bronze bars, the ores used up, Smithing XP %d -> %d"), MCount(SA, BBar), XP1, SA->GetXPTotal(ESkillType::Smithing)));
		A->DoInv(EInvOp::Smelt, int32(EItemType::SteelBar), 1, 0, nullptr, FVector::ZeroVector);
		MCheck(MCount(SA, SBar) == 0 && MCount(SA, Fe) == 2 && MCount(SA, Co) == 2, TEXT("steel at Smithing 1 is refused and uses nothing"));
		A->DoInv(EInvOp::Smelt, int32(EItemType::Logs), 1, 0, nullptr, FVector::ZeroVector);
		MCheck(MCount(SA, MaterialId(EItemType::Logs)) == 0, TEXT("a material that is not a bar cannot be smelted"));
		SA->SetAllLevels(10);
		A->DoInv(EInvOp::Smelt, int32(EItemType::SteelBar), 28, 0, nullptr, FVector::ZeroVector);
		MCheck(MCount(SA, SBar) == 2 && MCount(SA, Fe) == 0 && MCount(SA, Co) == 0, FString::Printf(TEXT("steel at Smithing 10: %d steel bars from 2 iron and 2 coal"), MCount(SA, SBar)));
		// a bronze sword from the bars (Kinds.csv Mass 4 x Tiers.csv Mat1Share 0.5 = 2 bars)
		const int32 Sword = GearId(K_Sword, T_Tin);
		A->DoInv(EInvOp::Smith, K_Sword, T_Tin, 0, nullptr, FVector::ZeroVector);
		MCheck(MCount(SA, Sword) == 1 && MCount(SA, BBar) == 1 && ItemName(Sword) == TEXT("Bronze Sword"), FString::Printf(TEXT("smithed a %s from bronze bars: %d in the pack, %d bars left (expect 1, 1)"), *ItemName(Sword), MCount(SA, Sword), MCount(SA, BBar)));
		// a vaulted rank cannot be smithed, even with every level and the ore
		SA->SetAllLevels(USkillComponent::MaxLevel);
		SA->AddInvItem(MaterialId(EItemType::GoldOre), 20);
		A->DoInv(EInvOp::Smith, K_Sword, T_Gold, 0, nullptr, FVector::ZeroVector);
		MCheck(MCount(SA, GearId(K_Sword, T_Gold)) == 0 && MCount(SA, MaterialId(EItemType::GoldOre)) == 20, TEXT("a gold sword (vaulted) is refused at the smithy; the gold ore stays"));
		// knight armour from the smithy is Knight's Armor whatever is asked for
		SA->AddInvItem(MaterialId(EItemType::SilverBar), 20);
		A->DoInv(EInvOp::Smith, K_Helmet, T_Tin, AS_Heavy, nullptr, FVector::ZeroVector);
		MCheck(MCount(SA, KnightArmourId(K_Helmet)) == 1, FString::Printf(TEXT("a helmet asked for as tin Heavy comes out as %s"), *ItemName(KnightArmourId(K_Helmet))));
		++Stage; StageT = 0.f; return;
	}
	if (Stage == 2)
	{
		// ---- the cap: XP stops at 29 ----
		SA->SetAllLevels(28);
		SA->AddXP(ESkillType::Mining, 50000000);
		const int32 Cap = USkillComponent::MaxXP(ESkillType::Mining);
		MCheck(SA->GetLevel(ESkillType::Mining) == 29 && SA->GetXPTotal(ESkillType::Mining) == Cap, FString::Printf(TEXT("50 million XP at 28: level %d, XP %d (the cap is %d)"), SA->GetLevel(ESkillType::Mining), SA->GetXPTotal(ESkillType::Mining), Cap));
		SA->AddXP(ESkillType::Mining, 500);
		MCheck(SA->GetXPTotal(ESkillType::Mining) == Cap, TEXT("more XP at 29 is not counted"));
		MLog(FString::Printf(TEXT("JEEBMETALTEST XP to reach: 10 = %d, 20 = %d, 29 = %d (Attack); old 99 was about 4.7 million"), USkillComponent::XPForLevel(ESkillType::Attack, 10), USkillComponent::XPForLevel(ESkillType::Attack, 20), USkillComponent::XPForLevel(ESkillType::Attack, 29)));
		MLog(FString::Printf(TEXT("JEEBMETALTEST bow draw at 1 / 10 / 29: %.2f / %.2f / %.2f s; climb at 29 %.1f s, max health %d"), AKnightCharacter::BowDrawSeconds(1), AKnightCharacter::BowDrawSeconds(10), AKnightCharacter::BowDrawSeconds(29), AKnightCharacter::ClimbSeconds(29), 100 + FMath::RoundToInt((USkillComponent::PowerLevel(29) - 1.f) * 3.f)));

		// ---- an old save: tiered knight armour, a vaulted gold sword, skills far past 29 ----
		if (UJeebSave* S = UJeebSave::Capture(SB, B))
		{
			auto Raw = [](int32 Kind, int32 Style, int32 Tier) { return IdGearBase + Kind * 100 + Style * 10 + Tier; };   // (written as the game wrote them before Knight's Armor)
			S->Worn.SetNum(USkillComponent::NumWornKinds);
			S->Worn[K_Helmet] = Raw(K_Helmet, AS_Heavy, T_Gold);
			S->Worn[K_Chest] = Raw(K_Chest, AS_Classic, T_Diamond);
			S->Worn[K_Shoulders] = Raw(K_Shoulders, AS_Light, T_Tin);
			S->Worn[K_Gauntlets] = Raw(K_Gauntlets, AS_Light, T_Iron);
			S->Worn[K_Boots] = Raw(K_Boots, AS_Heavy, T_Emerald);
			S->SlotIds.SetNum(JeebItems::NumSlots); S->SlotCounts.SetNum(JeebItems::NumSlots);
			S->SlotIds[0] = Raw(K_Chest, AS_Light, T_Silver); S->SlotCounts[0] = 1;
			S->SlotIds[1] = GearId(K_Sword, T_Gold); S->SlotCounts[1] = 1;
			for (int32& X : S->XP) X = 50000000;
			const bool bApplied = S->ApplyTo(SB);
			B->RefreshEquipment();
			bool bAllKA = true;
			for (int32 K = K_Helmet; K <= K_Boots; ++K) bAllKA = bAllKA && SB->Worn[K] == KnightArmourId(K);
			MCheck(bApplied && bAllKA && SB->Slots[0].Id == KnightArmourId(K_Chest), FString::Printf(TEXT("an old save's gold heavy, diamond classic, tin and iron light, emerald heavy armour loads as Knight's Armor (%s, %s ...); the light chest in the pack too"), *ItemName(SB->Worn[K_Helmet]), *ItemName(SB->Worn[K_Chest])));
			MCheck(SB->Slots[1].Id == GearId(K_Sword, T_Gold) && IsVaulted(SB->Slots[1].Id), TEXT("a vaulted gold sword in an old save is kept (vaulted only stops new ones)"));
			int32 Over = 0;
			for (int32 i = 0; i < USkillComponent::NumSkills; ++i) if (SB->GetLevel(static_cast<ESkillType>(i)) != 29 || SB->GetXPTotal(static_cast<ESkillType>(i)) != USkillComponent::MaxXP(static_cast<ESkillType>(i))) ++Over;
			MCheck(Over == 0, FString::Printf(TEXT("its skills (50 million XP each) load at 29: %d off"), Over));
			FString Why;
			MCheck(CanEquip(SB, KnightArmourId(K_Chest), &Why) && ArmourProtect(FullVariant(KnightArmourId(K_Chest))) > 0, FString::Printf(TEXT("Knight's Armor: wearable, stops %d%% (old steel classic stopped %d%%), %s"), ArmourProtect(FullVariant(KnightArmourId(K_Chest))), FMath::RoundToInt((1.f - TierRow(T_Iron).ArmourTakes) * 100.f), *WeightText(PieceWeight(K_Chest, FullVariant(KnightArmourId(K_Chest))))));
		}
		else MCheck(false, TEXT("captured mtlb's character"));

		// ---- vaulted things are never sold or rolled ----
		int32 VaultedWares = 0, Wares = 0, KA = 0, Bronze = 0;
		const int64 H0 = JeebStore::HourNow();
		for (int64 H = H0; H < H0 + 1000; ++H)
			for (const JeebStore::FWare& Wr : JeebStore::RollStock(H)) { ++Wares; if (IsVaulted(Wr.Id)) ++VaultedWares; if (IsKnightArmour(Wr.Id)) ++KA; if (IsGear(Wr.Id) && !IsAccessoryKind(KindOf(Wr.Id)) && VariantOf(Wr.Id) == T_Tin) ++Bronze; }
		MCheck(VaultedWares == 0 && Wares > 0, FString::Printf(TEXT("1000 hours of store stock: %d wares, %d vaulted (expect 0), %d Knight's Armor, %d bronze"), Wares, VaultedWares, KA, Bronze));
		int32 VaultedBags = 0;
		FRandomStream R(1234);
		for (int32 i = 0; i < 2000; ++i) { int32 Id = 0, N = 0; JeebStore::RollGamble(R, Id, N); if (IsVaulted(Id)) ++VaultedBags; }
		MCheck(VaultedBags == 0, FString::Printf(TEXT("2000 mystery satchels: %d vaulted"), VaultedBags));
		MCheck(IsVaulted(GearId(K_Sword, T_Gold)) && IsVaulted(GearId(K_Katana, T_Tin)) && IsVaulted(MaterialId(EItemType::Onyx)) && !IsVaulted(GearId(K_Sword, T_Silver)) && !IsVaulted(PotionId(4)) && !IsVaulted(BBar),
			TEXT("vaulted: gold sword, katana, onyx; in game: silver sword, every potion, bronze bars"));
		++Stage; StageT = 0.f; return;
	}
	if (Stage == 3 && StageT > 45.f) Done();   // (time for the players' machines to log what they were sent)
#endif
}
