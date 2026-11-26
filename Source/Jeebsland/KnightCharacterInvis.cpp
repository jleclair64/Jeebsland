// The invisibility potion (2026-11-26, owner: "knight nearly transparent with a slight air distortion, like other games").
// The server decides: drinking (Potions.csv row "invisible") calls StartInvisible, which sets the replicated bInvisible for InvisSeconds
// (Tuning.ini [Invisibility]); attacking, casting, shooting, throwing or dying ends it early (EndInvisible, called from StartAttack,
// FireArrow, ThrowRock and Die). Every machine but a dedicated server swaps every mesh on the knight (suit, gear, held items, cape, broom,
// trail ribbons) to one translucent refracting material, /Game/Effects/M_Invisible (Tools/invis_material.py): nearly see-through, a glassy
// rim and a gentle ripple in what is behind him. It writes motion vectors (translucent velocity) and says it has pixel animation, so TSR
// keeps no ghost copies. Lights and particle effects on him are hidden meanwhile. The drinker sees himself a little more (InvisSelfOpacity).
// Enemies: Stealth.cpp and AFoeKnight::FindTarget notice an invisible player only within InvisSeeRange.
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "JeebTuning.h"
#include "Inventory.h"
#include "Components/MeshComponent.h"
#include "Components/LightComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarInvisAsOther(TEXT("jeeb.Invis.AsOther"), 0,
	TEXT("Test pictures: 1 = show your own invisible knight as other players see him (fainter), not the drinker's own view"));
static TAutoConsoleVariable<float> CVarTestInvis(TEXT("jeeb.Test.Invis"), -1.f,
	TEXT("Single player test pictures (-ShotEyesSet): N > 0 turns your knight invisible for N seconds, 0 ends it, -1 leaves it"));
#endif

float AKnightCharacter::GetInvisLeft() const
{
	if (!bInvisible || !GetWorld()) return 0.f;
	return FMath::Max(0.f, InvisUntil - GetWorld()->GetTimeSeconds());
}

void AKnightCharacter::StartInvisible(float Seconds)
{
	if (!HasAuthority() || Seconds <= 0.f || Health <= 0.f) return;
	const bool bWas = bInvisible;
	bInvisible = true;
	InvisUntil = GetWorld()->GetTimeSeconds() + Seconds;
	ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("JEEBINVIS %s turns invisible for %.0f s%s"), *GetName(), Seconds, bWas ? TEXT(" (again: the time starts over)") : TEXT(""));
	if (Skills) Skills->Say(3.f, FColor(190, 220, 255), FString::Printf(TEXT("You fade from sight for %.0f seconds. Attacking ends it."), Seconds));
}

void AKnightCharacter::EndInvisible(const TCHAR* Why)
{
	if (!HasAuthority() || !bInvisible) return;
	bInvisible = false;
	ForceNetUpdate();
	const float Left = InvisUntil - GetWorld()->GetTimeSeconds();
	InvisUntil = -1.f;
	UE_LOG(LogTemp, Log, TEXT("JEEBINVIS %s visible again: %s (%.1f s were left)"), *GetName(), Why, FMath::Max(0.f, Left));
	if (Skills && Health > 0.f) Skills->Say(2.5f, FColor(190, 220, 255), FString::Printf(TEXT("You are visible again (%s)."), Why));
}

void AKnightCharacter::OnRep_Invisible()
{
	if (!bInvisible) InvisUntil = -1.f;
	InvisScanT = 0.f;   // (the look follows at once in TickInvisible)
}

void AKnightCharacter::TickInvisible(float Dt)
{
	UWorld* W = GetWorld();
	if (!W) return;
	if (HasAuthority() && bInvisible && W->GetTimeSeconds() >= InvisUntil) EndInvisible(TEXT("the potion wore off"));
#if !UE_BUILD_SHIPPING
	TickInvisTest(Dt);
#endif
	if (IsNetMode(NM_DedicatedServer)) return;
	if (bInvisible != bInvisShown) ShowInvisible(bInvisible);
	else if (bInvisShown)
	{
		InvisScanT -= Dt;
		if (InvisScanT <= 0.f) ShowInvisible(true);   // (gear rebuilt meanwhile: a new weapon, a refit, the broom, a trail)
	}
}

// Swaps (bOn) or puts back every material on this knight and what is attached to him. Called again every 0.1 s while invisible, so a piece
// built or repainted meanwhile is caught; a slot the game repainted is remembered anew.
void AKnightCharacter::ShowInvisible(bool bOn)
{
	InvisScanT = 0.1f;
	const auto& T = JeebTune::V();
	TArray<AActor*> Actors{ this };
	GetAttachedActors(Actors, false, true);
	if (bOn)
	{
		if (!InvisMID)
		{
			UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Effects/M_Invisible.M_Invisible"));
			if (!Base) { UE_LOG(LogTemp, Warning, TEXT("JEEBINVIS no /Game/Effects/M_Invisible (run Tools/InvisMaterial.ps1): the knight stays as he is")); bInvisShown = true; InvisScanT = 1.0e9f; return; }
			InvisMID = UMaterialInstanceDynamic::Create(Base, this);
			UE_LOG(LogTemp, Log, TEXT("JEEBINVIS %s shown invisible on this machine (%s view)"), *GetName(), IsLocallyControlled() ? TEXT("the drinker's") : TEXT("another player's"));
		}
		bool bSelf = IsLocallyControlled();
#if !UE_BUILD_SHIPPING
		if (CVarInvisAsOther.GetValueOnGameThread() != 0) bSelf = false;
#endif
		InvisMID->SetScalarParameterValue(TEXT("Opacity"), bSelf ? T.InvisSelfOpacity : T.InvisOpacity);
		InvisMID->SetScalarParameterValue(TEXT("Edge"), bSelf ? T.InvisSelfEdge : T.InvisEdge);
		InvisMID->SetScalarParameterValue(TEXT("Refraction"), T.InvisRefraction);
		InvisMID->SetScalarParameterValue(TEXT("Shimmer"), T.InvisShimmer);
		int32 NewSlots = 0;
		for (AActor* A : Actors)
		{
			if (!A || (A != this && A->IsA<AKnightCharacter>())) continue;
			TInlineComponentArray<UPrimitiveComponent*> Comps(A);
			for (UPrimitiveComponent* P : Comps)
			{
				if (UMeshComponent* M = Cast<UMeshComponent>(P))
				{
					for (int32 i = 0; i < M->GetNumMaterials(); ++i)
					{
						UMaterialInterface* Was = M->GetMaterial(i);
						if (Was == InvisMID) continue;
						FInvisSlot* S = InvisSaved.FindByPredicate([&](const FInvisSlot& E) { return E.Comp.Get() == M && E.Slot == i; });
						if (S) S->Mat = Was; else InvisSaved.Add({ M, i, Was });
						if (Was) InvisKeep.AddUnique(Was);
						M->SetMaterial(i, InvisMID);
						++NewSlots;
					}
				}
			}
			// lights and particle effects on him (the wizard's glows, the pipe's smoke): hidden meanwhile (hidden-in-game, which the game's
			// own SetVisibility calls never touch)
			TInlineComponentArray<USceneComponent*> Scene(A);
			for (USceneComponent* C : Scene)
			{
				if (!(C->IsA<ULightComponent>() || C->IsA<UFXSystemComponent>()) || C->bHiddenInGame) continue;
				C->SetHiddenInGame(true);
				InvisHiddenFx.Add(C);
			}
		}
		if (!bInvisShown || NewSlots > 0)
			UE_LOG(LogTemp, Log, TEXT("JEEBINVIS %s swapped %d material slots (%d remembered, %d lights/effects hidden)"), *GetName(), NewSlots, InvisSaved.Num(), InvisHiddenFx.Num());
		bInvisShown = true;
		return;
	}
	// back: every slot still showing the invisible material gets what it had
	int32 Back = 0;
	for (const FInvisSlot& S : InvisSaved)
	{
		UMeshComponent* M = S.Comp.Get();
		if (M && S.Slot >= 0 && S.Slot < M->GetNumMaterials() && M->GetMaterial(S.Slot) == InvisMID) { M->SetMaterial(S.Slot, S.Mat); ++Back; }
	}
	for (const TWeakObjectPtr<USceneComponent>& C : InvisHiddenFx) if (C.IsValid()) C->SetHiddenInGame(false);
	UE_LOG(LogTemp, Log, TEXT("JEEBINVIS %s visible again on this machine: %d material slots put back, %d lights/effects shown"), *GetName(), Back, InvisHiddenFx.Num());
	InvisSaved.Reset();
	InvisKeep.Reset();
	InvisHiddenFx.Reset();
	bInvisShown = false;
}

#if !UE_BUILD_SHIPPING
// jeeb.Invis [seconds]: single player test pictures (the server's own call; 0 ends it)
static FAutoConsoleCommandWithWorldAndArgs CmdInvis(TEXT("jeeb.Invis"), TEXT("Single player, test builds: jeeb.Invis [seconds, default 60; 0 ends it] turns your knight invisible as the potion does."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;   // (a local test command: the one player)
		AKnightCharacter* K = PC ? Cast<AKnightCharacter>(PC->GetPawn()) : nullptr;
		if (!K || !K->HasAuthority()) return;
		const float S = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 60.f;
		if (S > 0.f) K->StartInvisible(S); else K->EndInvisible(TEXT("test command"));
	}));

// Online test (Tools/invis_net.sh): -InvisNetTest on the server, -InvisNetDo on the players' machines. The server puts two invisibility potions
// in the first player's potion slot; that player's machine drinks one (the real key path, DrinkPotion -> the server), the state must last
// InvisSeconds and end on its own; then it drinks the second and swings, which must end it at once. The other player's machine logs what it
// sees of the drinker (JEEBINVISSEE). The server logs JEEBINVISTEST PASS/FAIL.
void AKnightCharacter::TickInvisTest(float Dt)
{
	if (HasAuthority() && IsLocallyControlled())
	{
		static float LastSet = -1.f;
		const float Want = CVarTestInvis.GetValueOnGameThread();
		if (Want != LastSet) { LastSet = Want; if (Want > 0.f) StartInvisible(Want); else if (Want == 0.f) EndInvisible(TEXT("test switch")); }
	}
	static const bool bSrv = FParse::Param(FCommandLine::Get(), TEXT("InvisNetTest"));
	static const bool bDo = FParse::Param(FCommandLine::Get(), TEXT("InvisNetDo"));
	if (!bSrv && !bDo) return;
	UWorld* W = GetWorld();
	const float Now = W->GetTimeSeconds();
	static TArray<TWeakObjectPtr<AKnightCharacter>> Order;
	if (bSrv && HasAuthority() && GetController())
	{
		Order.RemoveAll([](const TWeakObjectPtr<AKnightCharacter>& K) { return !K.IsValid(); });
		const int32 TestRole = Order.AddUnique(this);
		InvisTestT += Dt;
		static float StartedAt = -1.f;
		if (TestRole != 0 || !Skills) return;
		const bool bTwo = Order.Num() >= 2;
		if (InvisTestStep == 0 && bTwo && InvisTestT > 8.f)
		{
			int32 Type = -1;
			for (int32 t = 0; t < JeebItems::NumPotions(); ++t) if (JeebItems::PotionRow(t).Effect == JeebItems::PE_Invis) Type = t;
			if (Type < 0) { UE_LOG(LogTemp, Log, TEXT("JEEBINVISTEST FAIL no invisible row in Potions.csv")); InvisTestStep = 99; return; }
			Skills->PotionSlot = FInvSlot(JeebItems::PotionId(Type), 2);
			for (float& R : PotionReadyAt) R = 0.f;
			InvisTestStep = 1;
			UE_LOG(LogTemp, Log, TEXT("JEEBINVISTEST drinker %s given 2 x %s"), *GetName(), JeebItems::PotionName(Type));
		}
		if (InvisTestStep == 1 && bInvisible) { InvisTestStep = 2; StartedAt = Now; UE_LOG(LogTemp, Log, TEXT("JEEBINVISTEST PASS drank: invisible, %.1f s to go"), GetInvisLeft()); }
		if (InvisTestStep == 2 && !bInvisible)
		{
			const float Lasted = Now - StartedAt, Want = JeebTune::V().InvisSeconds;
			UE_LOG(LogTemp, Log, TEXT("JEEBINVISTEST %s ended on its own after %.2f s (wanted %.0f)"), FMath::Abs(Lasted - Want) < 0.5f ? TEXT("PASS") : TEXT("FAIL"), Lasted, Want);
			InvisTestStep = 3; for (float& R : PotionReadyAt) R = 0.f;
		}
		if (InvisTestStep == 3 && bInvisible) { InvisTestStep = 4; StartedAt = Now; UE_LOG(LogTemp, Log, TEXT("JEEBINVISTEST PASS drank the second")); }
		if (InvisTestStep == 4 && !bInvisible)
		{
			const float Lasted = Now - StartedAt;
			UE_LOG(LogTemp, Log, TEXT("JEEBINVISTEST %s the swing ended it after %.2f s"), Lasted < JeebTune::V().InvisSeconds - 2.f ? TEXT("PASS") : TEXT("FAIL"), Lasted);
			InvisTestStep = 5;
			UE_LOG(LogTemp, Log, TEXT("JEEBINVISTEST done"));
		}
		return;
	}
	if (!bDo || HasAuthority()) return;
	if (IsLocallyControlled())
	{
		// the drinker's machine: drink whenever an invisibility potion is in the slot and it is not on; swing 4 s into the second one
		InvisTestT += Dt;
		const FInvSlot& P = Skills ? Skills->PotionSlot : FInvSlot();
		const bool bHasInvis = P.Id != 0 && JeebItems::IsPotion(P.Id) && JeebItems::PotionRow(JeebItems::PotionType(P.Id)).Effect == JeebItems::PE_Invis;
		if (InvisTestStep == 0 && bHasInvis && !bInvisible && !bDrinking)
		{
			UE_LOG(LogTemp, Log, TEXT("JEEBINVISDO drinking (%d in the slot)"), P.Count);
			DrinkPotion();
			InvisTestStep = 1;
			InvisTestT = 0.f;
		}
		if (InvisTestStep == 1 && bInvisible) { InvisTestStep = 2; UE_LOG(LogTemp, Log, TEXT("JEEBINVISDO invisible, HUD says %.1f s left"), GetInvisLeft()); }
		if (InvisTestStep == 2 && !bInvisible) { UE_LOG(LogTemp, Log, TEXT("JEEBINVISDO visible again (first), HUD %.1f s"), GetInvisLeft()); InvisTestStep = 21; }
		if (InvisTestStep == 21 && bHasInvis && !bInvisible && !bDrinking) { UE_LOG(LogTemp, Log, TEXT("JEEBINVISDO drinking the second")); DrinkPotion(); InvisTestStep = 3; InvisTestT = 0.f; }
		if (InvisTestStep == 3 && bInvisible) { InvisTestStep = 4; InvisTestT = 0.f; }
		if (InvisTestStep == 4 && InvisTestT > 4.f) { UE_LOG(LogTemp, Log, TEXT("JEEBINVISDO swinging")); StartAttack(EAtkStyle::Slash, 1, 0.f); InvisTestStep = 5; }
		if (InvisTestStep == 5 && !bInvisible) { UE_LOG(LogTemp, Log, TEXT("JEEBINVISDO visible again (swing)")); InvisTestStep = 6; }
		return;
	}
	// another player's machine: what it sees of this knight
	const bool bSwapped = bInvisShown && InvisSaved.Num() > 0;
	if (bInvisible != (InvisTestStep == 1))
	{
		InvisTestStep = bInvisible ? 1 : 0;
		UE_LOG(LogTemp, Log, TEXT("JEEBINVISSEE other %s %s at %.2f s (material slots swapped %d)"), *GetName(), bInvisible ? TEXT("turned invisible") : TEXT("visible again"), Now, InvisSaved.Num());
	}
	InvisTestT += Dt;
	if (bInvisible && int32(InvisTestT / 3.f) != int32((InvisTestT - Dt) / 3.f))
		UE_LOG(LogTemp, Log, TEXT("JEEBINVISSEE other %s still invisible, shown %d, slots %d, hidden fx %d"), *GetName(), bSwapped ? 1 : 0, InvisSaved.Num(), InvisHiddenFx.Num());
}
#endif
