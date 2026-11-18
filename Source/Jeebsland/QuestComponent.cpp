#include "QuestComponent.h"
#include "QuestPeople.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Npc.h"
#include "FoeKnight.h"
#include "FoeCamps.h"
#include "ForestRuins.h"
#include "Terrain.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

using namespace JeebQuests;

UQuestComponent::UQuestComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UQuestComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UQuestComponent, Quests, COND_OwnerOnly);
}

void UQuestComponent::BeginPlay()
{
	Super::BeginPlay();
	bool bTick = HasAuth();   // (the server counts the places reached)
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebQuestTest"))) bTick = true;
#endif
	SetComponentTickEnabled(bTick);
}

bool UQuestComponent::HasAuth() const
{
	const AActor* O = GetOwner();
	return O && O->HasAuthority();
}

USkillComponent* UQuestComponent::Skills() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<USkillComponent>() : nullptr;
}

const FQuestProgress* UQuestComponent::Find(FName Key) const
{
	for (const FQuestProgress& P : Quests) if (P.Key == Key) return &P;
	return nullptr;
}

// ---------------------------------------------------------------------------------------------------
// Questions
// ---------------------------------------------------------------------------------------------------
bool UQuestComponent::MeetsNeeds(const FQuestDef& Q, FString* OutWhy, bool bQuestsOnly) const
{
	const USkillComponent* Sk = Skills();
	for (const FNeed& N : Q.Needs)
	{
		bool bOk;
		if (!N.Quest.IsNone()) { const FQuestProgress* P = Find(N.Quest); bOk = P && P->bDone; }
		else bOk = bQuestsOnly || (Sk && Sk->GetLevel(N.Skill) >= N.Level);
		if (!bOk) { if (OutWhy) *OutWhy = NeedText(N); return false; }
	}
	return true;
}

UQuestComponent::EState UQuestComponent::StateOf(const FQuestDef& Q) const
{
	if (const FQuestProgress* P = Find(Q.Key)) return P->bDone ? EState::Done : EState::Active;
	return MeetsNeeds(Q) ? EState::Available : EState::Locked;
}

void UQuestComponent::StepProgress(const FQuestProgress& P, int32& OutHave, int32& OutNeed) const
{
	OutHave = 0; OutNeed = 1;
	const FQuestDef* Q = FindQuest(P.Key);
	if (!Q || P.bDone || !Q->Steps.IsValidIndex(P.Step)) return;
	const FStepDef& S = Q->Steps[P.Step];
	if (S.Type == EStep::Bring) { const USkillComponent* Sk = Skills(); OutHave = Sk ? Sk->CountItem(S.ItemId) : 0; OutNeed = S.Count; }
	else if (S.Type == EStep::Kill) { OutHave = P.Count; OutNeed = S.Count; }
}

FString UQuestComponent::StepLine(const FQuestProgress& P) const
{
	const FQuestDef* Q = FindQuest(P.Key);
	if (!Q) return FString();
	if (P.bDone) return TEXT("Finished.");
	if (!Q->Steps.IsValidIndex(P.Step)) return FString();
	const FStepDef& S = Q->Steps[P.Step];
	int32 Have, Need;
	StepProgress(P, Have, Need);
	if (S.Type == EStep::Bring && Have >= Need) return FString::Printf(TEXT("You have %d %s: take them to %s."), Need, *JeebItems::ItemName(S.ItemId), *PersonName(S.At));
	if (S.Type == EStep::Bring || S.Type == EStep::Kill) return FString::Printf(TEXT("%s  (%d/%d)"), *S.Log, FMath::Min(Have, Need), Need);
	return S.Log;
}

UQuestComponent::EMark UQuestComponent::MarkFor(FName Person) const
{
	EMark Best = EMark::None;
	for (const FQuestDef& Q : JeebQuests::Quests())
	{
		const FQuestProgress* P = Find(Q.Key);
		if (P && !P->bDone && Q.Steps.IsValidIndex(P->Step))
		{
			const FStepDef& S = Q.Steps[P->Step];
			if ((S.Type == EStep::Talk || S.Type == EStep::Bring) && S.At == Person)
			{
				int32 Have, Need;
				StepProgress(*P, Have, Need);
				if (S.Type == EStep::Talk || Have >= Need) return EMark::HandIn;
				Best = EMark::Busy;
			}
			else if (Q.Giver == Person && Best == EMark::None) Best = EMark::Busy;
		}
		else if (!P && Q.Giver == Person && MeetsNeeds(Q) && Best != EMark::Busy) Best = EMark::Offer;
	}
	return Best;
}

FName UQuestComponent::GetTracked() const
{
	if (const FQuestProgress* P = Find(Tracked)) if (!P->bDone) return Tracked;
	for (int32 i = Quests.Num() - 1; i >= 0; --i) if (!Quests[i].bDone && FindQuest(Quests[i].Key)) return Quests[i].Key;
	return NAME_None;
}

bool UQuestComponent::Objective(FVector2D& OutAt, FString& OutWhat) const
{
	const FQuestProgress* P = Find(GetTracked());
	const FQuestDef* Q = P ? FindQuest(P->Key) : nullptr;
	if (!Q || P->bDone || !Q->Steps.IsValidIndex(P->Step)) return false;
	const FStepDef& S = Q->Steps[P->Step];
	const UWorld* W = GetWorld();
	auto PersonAt = [&](FName Who)
	{
		if (const ANpc* N = AQuestPeople::Find(W, Who)) OutAt = FVector2D(N->GetActorLocation());
		else if (const FPersonDef* D = FindPerson(Who)) OutAt = D->Pos;
		else return false;
		OutWhat = PersonName(Who);
		return true;
	};
	int32 Have, Need;
	StepProgress(*P, Have, Need);
	if (S.Type == EStep::Talk || (S.Type == EStep::Bring && Have >= Need)) return PersonAt(S.At);
	if (!S.Mark.IsEmpty())
	{
		if (FindPerson(FName(*S.Mark))) return PersonAt(FName(*S.Mark));
		if (S.Mark.Equals(TEXT("ruin"), ESearchCase::IgnoreCase)) {}   // (the nearest ruin, below)
		else if (MarkSpot(S.Mark, OutAt)) { OutWhat = Q->Title; return true; }
	}
	if (S.Type == EStep::Kill && !S.Target.Equals(TEXT("PenKnight"), ESearchCase::IgnoreCase))   // (bandits and the like: the nearest camp that has one, FoeCamps.h)
	{
		const FVector2D From = GetOwner() ? FVector2D(GetOwner()->GetActorLocation()) : FVector2D::ZeroVector;
		if (AFoeCamps::NearestCamp(W, S.Target, From, OutAt, OutWhat)) return true;
	}
	if (S.Type == EStep::Kill)
	{
		for (TActorIterator<AFoeKnight> It(W); It; ++It)
			if (!It->bStill && !It->IsBandit()) { OutAt = FVector2D(It->GetActorLocation()); OutWhat = TEXT("Sparring knight"); return true; }
		OutAt = JeebTerrain::PenCenter(); OutWhat = TEXT("Sparring knight");
		return true;
	}
	if (S.Type == EStep::Visit || S.Mark.Equals(TEXT("ruin"), ESearchCase::IgnoreCase))
	{
		if (S.Type == EStep::Visit && !S.bRuin) { OutAt = S.Place; OutWhat = Q->Title; return true; }
		const FVector2D From = GetOwner() ? FVector2D(GetOwner()->GetActorLocation()) : FVector2D::ZeroVector;
		float Best = 1e18f;
		for (TActorIterator<AForestRuins> It(W); It; ++It)
			for (const FVector4& R : It->Spots())
				if (FVector2D::DistSquared(FVector2D(R.X, R.Y), From) < Best) { Best = FVector2D::DistSquared(FVector2D(R.X, R.Y), From); OutAt = FVector2D(R.X, R.Y); }
		OutWhat = TEXT("Old ruin");
		return Best < 1e18f;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------
// Conversations (the player's machine)
// ---------------------------------------------------------------------------------------------------
TArray<FString> UQuestComponent::MakeTalk(const ANpc* Npc)
{
	TArray<FString> Lines;
	PendingOffer = NAME_None;
	if (!Npc) return Lines;
	const FName K = Npc->GetPersonKey();
	auto RewardLine = [](const FQuestDef& Q) { return FString::Printf(TEXT("Quest complete: %s.  You get %s."), *Q.Title, *RewardText(Q)); };

	// 1. A step of one of your quests happens here: handed in now (the server checks and does it), or a word about what is missing
	for (int32 Pass = 0; Pass < 2; ++Pass)
		for (const FQuestDef& Q : JeebQuests::Quests())
		{
			const FQuestProgress* P = Find(Q.Key);
			if (!P || P->bDone || !Q.Steps.IsValidIndex(P->Step)) continue;
			const FStepDef& S = Q.Steps[P->Step];
			if (!((S.Type == EStep::Talk || S.Type == EStep::Bring) && S.At == K)) continue;
			int32 Have, Need;
			StepProgress(*P, Have, Need);
			const bool bReady = S.Type == EStep::Talk || Have >= Need;
			if (Pass == 0 && bReady)
			{
				Lines = S.Lines;
				if (P->Step + 1 >= Q.Steps.Num()) { Lines.Append(Q.Done); Lines.Add(RewardLine(Q)); }
				if (Lines.Num() == 0) Lines.Add(TEXT("Thank you."));
				Ask(EOp::Talk, K);
				return Lines;
			}
			if (Pass == 1 && !bReady)
			{
				Lines = S.Waiting;
				if (Lines.Num() == 0) Lines.Add(S.Log);
				Lines.Add(FString::Printf(TEXT("(You have %d of %d %s.)"), Have, Need, *JeebItems::ItemName(S.ItemId)));
				return Lines;
			}
		}
	// 2. A quest to offer
	for (const FQuestDef& Q : JeebQuests::Quests())
		if (Q.Giver == K && StateOf(Q) == EState::Available)
		{
			PendingOffer = Q.Key;
			Lines = Q.Offer;
			Lines.Add(FString::Printf(TEXT("Quest: %s.  Reward: %s.    [ E ] I'll help.    [ Tab ] Not now."), *Q.Title, *RewardText(Q)));
			return Lines;
		}
	// 3. A quest of theirs under way somewhere else
	for (const FQuestDef& Q : JeebQuests::Quests())
		if (const FQuestProgress* P = Find(Q.Key))
			if (Q.Giver == K && !P->bDone)
			{
				Lines = Q.Reminder;
				Lines.Add(FString::Printf(TEXT("(%s: %s)"), *Q.Title, *StepLine(*P)));
				return Lines;
			}
	// 4. A quest they would give if a skill were higher (only once its earlier quests are done)
	for (const FQuestDef& Q : JeebQuests::Quests())
	{
		FString Why;
		if (Q.Giver == K && StateOf(Q) == EState::Locked && Q.NotYet.Num() > 0 && MeetsNeeds(Q, nullptr, true) && !MeetsNeeds(Q, &Why))
		{
			Lines = Q.NotYet;
			Lines.Add(FString::Printf(TEXT("(%s needs %s.)"), *Q.Title, *Why));
			return Lines;
		}
	}
	// 5. Just a word
	if (const FPersonDef* D = FindPerson(K); D && D->Greeting.Num() > 0) Lines.Add(D->Greeting[GreetIdx++ % D->Greeting.Num()]);
	else Lines.Add(TEXT("Hello there."));
	return Lines;
}

void UQuestComponent::EndTalk(bool bAgreed)
{
	if (bAgreed && !PendingOffer.IsNone()) Ask(EOp::Accept, PendingOffer);
	PendingOffer = NAME_None;
}

// ---------------------------------------------------------------------------------------------------
// Requests: the server decides
// ---------------------------------------------------------------------------------------------------
void UQuestComponent::Ask(EOp Op, FName Key)
{
	if (HasAuth()) DoQuest(Op, Key);
	else ServerQuest(static_cast<uint8>(Op), Key);
}

void UQuestComponent::ServerQuest_Implementation(uint8 Op, FName Key)
{
	if (Op > static_cast<uint8>(EOp::Talk)) return;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastAsk < 0.2) return;   // (a talk takes several clicks; nobody asks faster than this)
	LastAsk = Now;
	DoQuest(static_cast<EOp>(Op), Key);
}

bool UQuestComponent::NearPerson(FName Person) const
{
	const AActor* Me = GetOwner();
	if (!Me) return false;
	FVector2D At;
	if (const ANpc* N = AQuestPeople::Find(GetWorld(), Person)) At = FVector2D(N->GetActorLocation());   // (the server's own copy of the person)
	else if (const FPersonDef* D = FindPerson(Person)) At = D->Pos;
	else return false;
	return FVector2D::Distance(At, FVector2D(Me->GetActorLocation())) < TalkReach;
}

bool UQuestComponent::RewardFits(const FQuestDef& Q) const
{
	const USkillComponent* Sk = Skills();
	if (!Sk) return false;
	if (Q.RewardItems.Num() == 0) return true;
	// (each item checked alone; a reward of several different items rarely meets an almost full pack)
	for (const FInvSlot& I : Q.RewardItems) if (!Sk->CanAdd(I.Id, I.Count)) return false;
	return true;
}

void UQuestComponent::DoQuest(EOp Op, FName Key)
{
	USkillComponent* Sk = Skills();
	if (!HasAuth() || !Sk) return;
	const FString Who = GetOwner()->GetName();
	if (Op == EOp::Accept)
	{
		const FQuestDef* Q = FindQuest(Key);
		if (!Q || Find(Key)) return;
		if (!NearPerson(Q->Giver)) { UE_LOG(LogTemp, Log, TEXT("JEEBQUEST %s: accept %s refused: not beside %s"), *Who, *Key.ToString(), *Q->Giver.ToString()); return; }
		FString Why;
		if (!MeetsNeeds(*Q, &Why))
		{
			Sk->Say(3.f, FColor::Orange, FString::Printf(TEXT("%s needs %s."), *Q->Title, *Why));
			UE_LOG(LogTemp, Log, TEXT("JEEBQUEST %s: accept %s refused: needs %s"), *Who, *Key.ToString(), *Why);
			return;
		}
		FQuestProgress P;
		P.Key = Key;
		Quests.Add(P);
		Sk->Say(5.f, FColor(255, 215, 120), FString::Printf(TEXT("New quest: %s.  %s"), *Q->Title, *StepLine(P)));
		UE_LOG(LogTemp, Log, TEXT("JEEBQUEST %s: accepted %s"), *Who, *Key.ToString());
		return;
	}

	// Talking to a person: the first of your quests whose current step is with them, and ready, is done now
	for (const FQuestDef& Q : JeebQuests::Quests())
	{
		FQuestProgress* P = Quests.FindByPredicate([&](const FQuestProgress& X) { return X.Key == Q.Key; });
		if (!P || P->bDone || !Q.Steps.IsValidIndex(P->Step)) continue;
		const FStepDef& S = Q.Steps[P->Step];
		if (!((S.Type == EStep::Talk || S.Type == EStep::Bring) && S.At == Key)) continue;
		if (!NearPerson(Key)) { UE_LOG(LogTemp, Log, TEXT("JEEBQUEST %s: talk to %s refused: not beside them"), *Who, *Key.ToString()); return; }
		if (S.Type == EStep::Bring && Sk->CountItem(S.ItemId) < S.Count) continue;   // (not enough yet: maybe another quest's step here is ready)
		const bool bLast = P->Step + 1 >= Q.Steps.Num();
		if (bLast && !RewardFits(Q)) { Sk->Say(4.f, FColor::Orange, TEXT("Make room in your pack for your reward first.")); return; }
		if (S.Type == EStep::Bring) Sk->RemoveItems(S.ItemId, S.Count);
		Advance(*P, Q);
		return;
	}
}

void UQuestComponent::Advance(FQuestProgress& P, const FQuestDef& Q)
{
	UE_LOG(LogTemp, Log, TEXT("JEEBQUEST %s: %s step %d of %d done"), *GetOwner()->GetName(), *Q.Key.ToString(), P.Step + 1, Q.Steps.Num());
	++P.Step;
	P.Count = 0;
	if (P.Step >= Q.Steps.Num()) { Finish(P, Q); return; }
	if (USkillComponent* Sk = Skills()) Sk->Say(5.f, FColor(255, 215, 120), FString::Printf(TEXT("%s: %s"), *Q.Title, *StepLine(P)));
}

void UQuestComponent::Finish(FQuestProgress& P, const FQuestDef& Q)
{
	USkillComponent* Sk = Skills();
	P.bDone = true;
	P.Step = uint8(FMath::Min(Q.Steps.Num(), 255));
	if (!Sk) return;
	for (const TPair<ESkillType, int32>& X : Q.RewardXP) Sk->AddXP(X.Key, X.Value);
	Sk->AddIngots(Q.RewardIngots);
	for (const FInvSlot& I : Q.RewardItems)
		if (!Sk->AddInvItem(I.Id, I.Count)) Sk->Say(4.f, FColor::Orange, FString::Printf(TEXT("No room for %s: your pack is full."), *JeebItems::ItemName(I.Id)));
	Sk->Say(6.f, FColor(255, 215, 90), FString::Printf(TEXT("Quest complete: %s!  You get %s."), *Q.Title, *RewardText(Q)));
	UE_LOG(LogTemp, Log, TEXT("JEEBQUEST %s: finished %s, reward %s (now %d ingots)"), *GetOwner()->GetName(), *Q.Key.ToString(), *RewardText(Q), Sk->GetIngots());
	ClientQuestDone(Q.Key);
}

void UQuestComponent::ClientQuestDone_Implementation(FName Key)
{
	const FQuestDef* Q = FindQuest(Key);
	DoneTitle = Q ? Q->Title : Key.ToString();
	DoneTime = FPlatformTime::Seconds();
}

void UQuestComponent::OnFoeBeaten(AActor* By, const TCHAR* Foe)
{
	if (By && !By->IsA<APawn>()) By = By->GetInstigator() ? (AActor*)By->GetInstigator() : By->GetOwner();   // (an arrow: whoever shot it)
	UQuestComponent* C = By ? By->FindComponentByClass<UQuestComponent>() : nullptr;
	if (!C || !C->HasAuth()) return;
	for (const FQuestDef& Q : JeebQuests::Quests())
	{
		FQuestProgress* P = C->Quests.FindByPredicate([&](const FQuestProgress& X) { return X.Key == Q.Key; });
		if (!P || P->bDone || !Q.Steps.IsValidIndex(P->Step)) continue;
		const FStepDef& S = Q.Steps[P->Step];
		if (S.Type != EStep::Kill || !S.Target.Equals(Foe, ESearchCase::IgnoreCase)) continue;
		++P->Count;
		UE_LOG(LogTemp, Log, TEXT("JEEBQUEST %s: %s beaten for %s (%d/%d)"), *By->GetName(), Foe, *Q.Key.ToString(), P->Count, S.Count);
		if (P->Count >= S.Count) C->Advance(*P, Q);
		else if (USkillComponent* Sk = C->Skills()) Sk->Say(3.f, FColor(255, 215, 120), FString::Printf(TEXT("%s: %s"), *Q.Title, *C->StepLine(*P)));
	}
}

void UQuestComponent::TickVisits()
{
	const AActor* Me = GetOwner();
	if (!Me) return;
	const FVector2D At(Me->GetActorLocation());
	for (const FQuestDef& Q : JeebQuests::Quests())
	{
		FQuestProgress* P = Quests.FindByPredicate([&](const FQuestProgress& X) { return X.Key == Q.Key; });
		if (!P || P->bDone || !Q.Steps.IsValidIndex(P->Step)) continue;
		const FStepDef& S = Q.Steps[P->Step];
		if (S.Type != EStep::Visit) continue;
		bool bThere = false;
		if (S.bRuin)
		{
			for (TActorIterator<AForestRuins> It(GetWorld()); It && !bThere; ++It)
				for (const FVector4& R : It->Spots())
					if (FVector2D::Distance(FVector2D(R.X, R.Y), At) < S.Radius) { bThere = true; break; }
		}
		else bThere = FVector2D::Distance(S.Place, At) < S.Radius;
		if (!bThere) continue;
		if (S.Lines.Num() > 0) if (USkillComponent* Sk = Skills()) Sk->Say(5.f, FColor(200, 230, 255), S.Lines[0]);
		Advance(*P, Q);
	}
}

void UQuestComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (HasAuth())
	{
		VisitClock += DeltaTime;
		if (VisitClock >= 0.5f) { VisitClock = 0.f; TickVisits(); }
	}
#if !UE_BUILD_SHIPPING
	TickTest(DeltaTime);
#endif
}

void UQuestComponent::OnRep_Quests()
{
	for (const FQuestProgress& P : Quests)
		if (!KnownKeys.Contains(P.Key) && !P.bDone) Tracked = P.Key;   // (a quest just taken is the one tracked)
	KnownKeys.Reset();
	for (const FQuestProgress& P : Quests) KnownKeys.Add(P.Key);
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebQuestTest")))
	{
		FString S;
		for (const FQuestProgress& P : Quests) S += FString::Printf(TEXT(" %s(step %d, count %d%s)"), *P.Key.ToString(), P.Step, P.Count, P.bDone ? TEXT(", done") : TEXT(""));
		UE_LOG(LogTemp, Log, TEXT("JEEBQUEST client sees%s"), *S);
	}
#endif
}

// ---------------------------------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------------------------------
void UQuestComponent::Capture(TArray<FString>& OutKeys, TArray<int32>& OutSteps, TArray<int32>& OutCounts, TArray<uint8>& OutDone) const
{
	for (const FQuestProgress& P : Quests)
	{
		OutKeys.Add(P.Key.ToString());
		OutSteps.Add(P.Step);
		OutCounts.Add(P.Count);
		OutDone.Add(P.bDone ? 1 : 0);
	}
}

void UQuestComponent::Restore(const TArray<FString>& Keys, const TArray<int32>& Steps, const TArray<int32>& Counts, const TArray<uint8>& Done)
{
	if (!HasAuth()) return;
	Quests.Reset();
	FString Report;
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		const FQuestDef* Q = FindQuest(FName(*Keys[i]));
		if (!Q) { UE_LOG(LogTemp, Warning, TEXT("JEEBQUEST a saved quest \"%s\" is no longer in Quests.csv; left out"), *Keys[i]); continue; }
		if (Find(Q->Key)) continue;
		FQuestProgress P;
		P.Key = Q->Key;
		P.bDone = Done.IsValidIndex(i) && Done[i] != 0;
		P.Step = uint8(FMath::Clamp(Steps.IsValidIndex(i) ? Steps[i] : 0, 0, P.bDone ? FMath::Min(Q->Steps.Num(), 255) : Q->Steps.Num() - 1));
		P.Count = FMath::Max(0, Counts.IsValidIndex(i) ? Counts[i] : 0);
		Quests.Add(P);
		Report += FString::Printf(TEXT(" %s(step %d, count %d%s)"), *P.Key.ToString(), P.Step, P.Count, P.bDone ? TEXT(", done") : TEXT(""));
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBQUEST restored %d quests:%s"), Quests.Num(), *Report);
}
