#include "FastTravel.h"
#include "WorldMap.h"
#include "KnightCharacter.h"
#include "KnightGameMode.h"
#include "SkillComponent.h"
#include "JeebTuning.h"
#include "JeebWater.h"
#include "Terrain.h"
#include "OakHill.h"
#include "OpenWorld.h"
#include "SteeleCreek.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// ---------------------------------------------------------------------------------------------------
// The town table
// ---------------------------------------------------------------------------------------------------
const TArray<JeebTravel::FTown>& JeebTravel::Towns()
{
	static const TArray<FTown> T = []()
	{
		const FVector2D VC = JeebTerrain::VillageCenter();
		const float VH = JeebTerrain::VillageHalf();
		TArray<FTown> A;
		//          name               radius  home   open   arrival anchor (cm)                                                          yaw
		A.Add({ TEXT("Blackrock"),     3200.f, false, true,  FVector2D(VC.X + VH - 450.f, VC.Y),                                          180.f });   // the east gate, just inside, facing the town
		A.Add({ TEXT("Oak Hill"),      6500.f, true,  true,  FVector2D::ZeroVector,                                                       0.f });     // (AKnightGameMode::OakWakeSpot: the plaza's arc)
		A.Add({ TEXT("Steele Creek"),  8000.f, false, true,  JeebSteele::ToWorld(JeebSteele::SpawnE, JeebSteele::SpawnN),                 JeebSteele::SpawnYaw });   // the forge yard, facing the smithy
		// The four open-world sites (2026-11-22): open for fast travel on their placeholder site (JeebOpen::Sites: the middle; arrival there, facing the placeholder house). When a town is built on one, only its row may need a better anchor.
		for (const TCHAR* N : { TEXT("Silver Point"), TEXT("Anchorage"), TEXT("Birch Grove"), TEXT("Palmer Square") })
		{
			FVector2D Anchor = FVector2D::ZeroVector; float Yaw = 0.f;
			for (const JeebOpen::FSite& S : JeebOpen::Sites())
				if (FCString::Strcmp(S.Name, N) == 0)
				{
					const FVector2D ToHouse = (S.House - S.Pos).GetSafeNormal();   // (a few metres short of the middle, looking at the house)
					Anchor = S.Pos - ToHouse * 500.f; Yaw = FMath::RadiansToDegrees(FMath::Atan2(ToHouse.Y, ToHouse.X));
				}
			const bool bBirch = FCString::Strcmp(N, TEXT("Birch Grove")) == 0;   // (fast travel into it is allowed, though it lies in PvP 2: its walls are safe)
			A.Add({ N, bBirch ? 8000.f : 6000.f, false, !Anchor.IsZero(), Anchor, Yaw });
		}
		return A;
	}();
	return T;
}

int32 JeebTravel::Count() { return FMath::Min(Towns().Num(), 32); }

int32 JeebTravel::IndexOf(const FString& Name)
{
	for (int32 i = 0; i < Count(); ++i) if (Name.Equals(Towns()[i].Name, ESearchCase::IgnoreCase)) return i;
	return -1;
}

FVector2D JeebTravel::Centre(int32 Index)
{
	if (Index < 0 || Index >= Count()) return FVector2D::ZeroVector;
	const FString N = Towns()[Index].Name;
	if (N == TEXT("Blackrock")) return JeebTerrain::VillageCenter();
	if (N == TEXT("Oak Hill")) return FVector2D(JeebOakHill::PosX, JeebOakHill::PosY);
	if (N == TEXT("Steele Creek")) return FVector2D(JeebSteele::PosX, JeebSteele::PosY);
	for (const JeebOpen::FSite& S : JeebOpen::Sites()) if (N == S.Name) return S.Pos;
	for (const JeebWorld::FTown& W : JeebWorld::Towns()) if (N == W.Name) return W.Pos;
	return FVector2D::ZeroVector;
}

int32 JeebTravel::TownAt(const FVector2D& P)
{
	int32 Best = -1;
	float BestD = 1.0e12f;
	for (int32 i = 0; i < Count(); ++i)
	{
		if (!Towns()[i].bOpen) continue;
		const float D = FVector2D::Distance(P, Centre(i));
		if (D < Towns()[i].Radius && D < BestD) { Best = i; BestD = D; }
	}
	return Best;
}

FVector JeebTravel::ArrivalSpot(UWorld* World, int32 Index, const AActor* Self, float& OutYaw)
{
	const FTown& T = Towns()[Index];
	if (FString(T.Name) == TEXT("Oak Hill") && JeebOakHill::Enabled()) return AKnightGameMode::OakWakeSpot(World, Self, OutYaw);
	const FVector2D Anchor = T.Anchor.IsZero() ? Centre(Index) : T.Anchor;
	OutYaw = T.Yaw;
	auto Ground = [](const FVector2D& P) { return FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y) + 130.f); };
	if (!World) return Ground(Anchor);
	// the anchor, then rings of 6, 12 and 18 spots 1.7 m apart round it
	TArray<FVector2D> Spots;
	Spots.Add(Anchor);
	for (int32 Ring = 1; Ring <= 3; ++Ring)
		for (int32 k = 0; k < 6 * Ring; ++k)
		{
			const float A = FMath::DegreesToRadians(360.f * k / (6 * Ring) + 17.f * Ring);
			Spots.Add(Anchor + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 170.f * Ring);
		}
	FCollisionQueryParams Q(SCENE_QUERY_STAT(TravelArrival), false, Self);
	for (const FVector2D& S : Spots)
	{
		bool bFree = true;
		for (TActorIterator<AKnightCharacter> It(World); It; ++It)
			if (*It != Self && FVector2D::DistSquared(FVector2D(It->GetActorLocation()), S) < 150.f * 150.f) { bFree = false; break; }
		if (!bFree) continue;
		const FVector At = Ground(S);
		float Surf = 0.f;
		if (JeebWater::DepthAt(FVector(S.X, S.Y, At.Z - 130.f), Surf) > 10.f) continue;   // (not in the water)
		if (World->OverlapBlockingTestByChannel(At + FVector(0.f, 0.f, 20.f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(45.f, 90.f), Q)) continue;
		return At;
	}
	return Ground(Anchor);
}

// ---------------------------------------------------------------------------------------------------
// The component
// ---------------------------------------------------------------------------------------------------
UTravelComponent::UTravelComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UTravelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UTravelComponent, VisitedMask, COND_OwnerOnly);
}

bool UTravelComponent::HasAuth() const { const AActor* O = GetOwner(); return O && O->HasAuthority(); }
AKnightCharacter* UTravelComponent::Knight() const { return Cast<AKnightCharacter>(GetOwner()); }

void UTravelComponent::BeginPlay()
{
	Super::BeginPlay();
	bool bTick = HasAuth();
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("JeebTravelTest"))) bTick = true;
#endif
	SetComponentTickEnabled(bTick);
	if (HasAuth())
		for (int32 i = 0; i < JeebTravel::Count(); ++i)
			if (JeebTravel::Towns()[i].bHome && JeebTravel::Towns()[i].bOpen) VisitedMask |= 1u << i;   // (Oak Hill is always known)
}

void UTravelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (HasAuth()) TickServer(DeltaTime);
#if !UE_BUILD_SHIPPING
	// TEMPORARY test setting (owner, 2026-11-21): every open town is known for fast travel; -NoTestTowns turns it off. Remove before real play.
	static const bool bTestTowns = !FParse::Param(FCommandLine::Get(), TEXT("NoTestTowns"));
	if (bTestTowns && HasAuth())
		for (int32 I = 0; I < JeebTravel::Towns().Num() && I < 32; ++I)
			if (JeebTravel::Towns()[I].bOpen) VisitedMask |= 1u << I;
	TickTest(DeltaTime);
#endif
}

// ---- visited towns ----
void UTravelComponent::MarkVisited(int32 Town, bool bAnnounce)
{
	if (Town < 0 || Town >= JeebTravel::Count() || IsVisited(Town)) return;
	VisitedMask |= 1u << Town;
	UE_LOG(LogTemp, Log, TEXT("JEEBTRAVEL %s: discovered %s"), *GetOwner()->GetName(), JeebTravel::Towns()[Town].Name);
	SaveAccount();
	if (bAnnounce) ClientDiscovered(uint8(Town));
}

void UTravelComponent::ClientDiscovered_Implementation(uint8 Town)
{
	if (Town >= JeebTravel::Count()) return;
	DiscoveredTitle = JeebTravel::Towns()[Town].Name;
	DiscoveredTime = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("JEEBTRAVEL client: DISCOVERED %s title shown"), *DiscoveredTitle);
}

void UTravelComponent::Capture(TArray<FString>& OutNames) const
{
	OutNames.Reset();
	for (int32 i = 0; i < JeebTravel::Count(); ++i) if (IsVisited(i)) OutNames.Add(JeebTravel::Towns()[i].Name);
}

void UTravelComponent::Restore(const TArray<FString>& Names, bool bFromOldSave)
{
	for (const FString& N : Names) { const int32 I = JeebTravel::IndexOf(N); if (I >= 0 && JeebTravel::Towns()[I].bOpen) VisitedMask |= 1u << I; }
	if (bFromOldSave)
	{
		// a character saved before fast travel: the towns whose middle its map fog shows explored are the ones it has obviously been to
		const USkillComponent* Sk = GetOwner() ? GetOwner()->FindComponentByClass<USkillComponent>() : nullptr;
		if (Sk)
			for (int32 i = 0; i < JeebTravel::Count(); ++i)
			{
				const FVector2D C = JeebTravel::Centre(i);
				if (JeebTravel::Towns()[i].bOpen && Sk->IsExplored(C.X, C.Y)) VisitedMask |= 1u << i;
			}
	}
	for (int32 i = 0; i < JeebTravel::Count(); ++i)
		if (JeebTravel::Towns()[i].bHome && JeebTravel::Towns()[i].bOpen) VisitedMask |= 1u << i;
}

static FString TownsFile(const FString& Key) { return FPaths::ProjectSavedDir() / TEXT("Accounts") / (Key + TEXT(".towns")); }

void UTravelComponent::LoadAccount(const FString& Key)
{
	AccountKey = Key;
	TArray<FString> Lines;
	if (FFileHelper::LoadFileToStringArray(Lines, *TownsFile(Key)))
		for (const FString& L : Lines) { const int32 I = JeebTravel::IndexOf(L.TrimStartAndEnd()); if (I >= 0 && JeebTravel::Towns()[I].bOpen) VisitedMask |= 1u << I; }
	UE_LOG(LogTemp, Log, TEXT("JEEBTRAVEL %s: the account knows %d towns (mask %u)"), *Key, FMath::CountBits(VisitedMask), VisitedMask);
	SaveAccount();   // (the file always holds the union of the account's file and the character's save)
}

void UTravelComponent::SaveAccount() const
{
	if (AccountKey.IsEmpty()) return;   // (single player: the town list rides in the character's save only)
	TArray<FString> Names;
	Capture(Names);
	FFileHelper::SaveStringToFile(FString::Join(Names, TEXT("\n")), *TownsFile(AccountKey));
}

// ---- the server's tick: towns reached, and the channel ----
void UTravelComponent::TickServer(float Dt)
{
	AKnightCharacter* K = Knight();
	if (!K || !GetWorld()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	VisitClock += Dt;
	if (VisitClock >= 0.5f)
	{
		VisitClock = 0.f;
		MarkVisited(JeebTravel::TownAt(FVector2D(K->GetActorLocation())), true);
	}
	if (ChanTown >= 0)
	{
		const FString Why = K->TravelBlocker();
		if (!Why.IsEmpty()) { CancelChannel(*Why); return; }
		if (FVector::Dist2D(K->GetActorLocation(), ChanPos) > 60.f) { CancelChannel(TEXT("You moved.")); return; }
		if (K->GetLastHurtTime() >= ChanStart || LastDealt >= ChanStart) { CancelChannel(TEXT("You were in combat.")); return; }
		if (Now >= ChanEnd) Arrive();
	}
}

void UTravelComponent::NoteCombat(AActor* By)
{
	if (By && !By->IsA<APawn>()) By = By->GetInstigator() ? (AActor*)By->GetInstigator() : By->GetOwner();   // (an arrow: whoever shot it)
	UTravelComponent* C = By ? By->FindComponentByClass<UTravelComponent>() : nullptr;
	if (C && C->GetWorld()) C->LastDealt = C->GetWorld()->GetTimeSeconds();
}

// ---- asking ----
void UTravelComponent::Ask(int32 Town)
{
	if (Town < 0 || Town >= JeebTravel::Count()) return;
	if (HasAuth()) DoTravel(Town);
	else ServerTravel(uint8(Town));
}

void UTravelComponent::ServerTravel_Implementation(uint8 Town)
{
	if (Town >= JeebTravel::Count()) return;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastAsk < 0.4) return;   // (nobody clicks faster than this)
	AskTimes.RemoveAll([Now](double T) { return Now - T > 10.0; });
	if (AskTimes.Num() >= 8) return;   // (more than 8 asks in 10 s: ignored)
	AskTimes.Add(Now);
	LastAsk = Now;
	DoTravel(Town);
}

bool UTravelComponent::Refuse(const FString& Why) const
{
	UE_LOG(LogTemp, Log, TEXT("JEEBTRAVEL %s: refused: %s"), GetOwner() ? *GetOwner()->GetName() : TEXT("?"), *Why);
	if (const AKnightCharacter* K = Knight()) if (USkillComponent* Sk = K->GetSkills()) Sk->Say(4.f, FColor::Orange, Why);
	return false;
}

void UTravelComponent::DoTravel(int32 Town)
{
	AKnightCharacter* K = Knight();
	if (!HasAuth() || !K || !GetWorld() || Town < 0 || Town >= JeebTravel::Count()) return;
	const JeebTravel::FTown& T = JeebTravel::Towns()[Town];
	const double Now = GetWorld()->GetTimeSeconds();
	const JeebTune::FValues& Tn = JeebTune::V();
	if (!T.bOpen) { Refuse(FString::Printf(TEXT("You can't travel to %s yet."), T.Name)); return; }
	if (!IsVisited(Town)) { Refuse(FString::Printf(TEXT("You haven't been to %s yet."), T.Name)); return; }
	if (ChanTown >= 0) { Refuse(TEXT("You are already travelling.")); return; }
	if (JeebTravel::TownAt(FVector2D(K->GetActorLocation())) == Town) { Refuse(FString::Printf(TEXT("You are already in %s."), T.Name)); return; }
	const FString Busy = K->TravelBlocker();
	if (!Busy.IsEmpty()) { Refuse(Busy); return; }
	const double SinceCombat = Now - FMath::Max<double>(K->GetLastHurtTime(), LastDealt);
	if (SinceCombat < Tn.TravelCombat) { Refuse(FString::Printf(TEXT("You were in combat a moment ago. Wait %d more seconds."), FMath::CeilToInt(Tn.TravelCombat - SinceCombat))); return; }
	if (Now < CooldownUntil) { Refuse(FString::Printf(TEXT("You can travel again in %d seconds."), FMath::CeilToInt(CooldownUntil - Now))); return; }
	// PvP areas (none yet; JeebTravel::PvpHook): never out of one, never into one
	if (JeebTravel::PvpHook(FVector2D(K->GetActorLocation()))) { Refuse(TEXT("You can't fast travel out of a PvP area.")); return; }
	if (JeebTravel::PvpHook(T.Anchor.IsZero() ? JeebTravel::Centre(Town) : T.Anchor)) { Refuse(TEXT("You can't fast travel into a PvP area.")); return; }

	ChanTown = Town;
	ChanStart = Now;
	ChanEnd = Now + Tn.TravelChannel;
	ChanPos = K->GetActorLocation();
	UE_LOG(LogTemp, Log, TEXT("JEEBTRAVEL %s: channelling to %s for %.1f s"), *GetOwner()->GetName(), T.Name, Tn.TravelChannel);
	ClientTravelStart(uint8(Town), Tn.TravelChannel);
}

void UTravelComponent::CancelChannel(const TCHAR* Why)
{
	if (ChanTown < 0) return;
	UE_LOG(LogTemp, Log, TEXT("JEEBTRAVEL %s: travel cancelled: %s"), *GetOwner()->GetName(), Why);
	ChanTown = -1;
	ClientTravelCancel();
	if (const AKnightCharacter* K = Knight()) if (USkillComponent* Sk = K->GetSkills()) Sk->Say(3.f, FColor::Orange, FString::Printf(TEXT("Travel cancelled. %s"), Why));
}

void UTravelComponent::Arrive()
{
	AKnightCharacter* K = Knight();
	const int32 Town = ChanTown;
	ChanTown = -1;
	if (!K || Town < 0) return;
	float Yaw = 0.f;
	const FVector Dest = JeebTravel::ArrivalSpot(GetWorld(), Town, K, Yaw);
	if (JeebTravel::PvpHook(FVector2D(Dest))) { ClientTravelCancel(); Refuse(TEXT("You can't fast travel into a PvP area.")); return; }
	const FVector From = K->GetActorLocation();
	K->TravelPlaced(Dest, Yaw);
	CooldownUntil = GetWorld()->GetTimeSeconds() + JeebTune::V().TravelCooldown;
	UE_LOG(LogTemp, Log, TEXT("JEEBTRAVEL %s: travelled to %s: from %.0f %.0f to %.0f %.0f %.0f yaw %.0f (%.0f m)"), *GetOwner()->GetName(), JeebTravel::Towns()[Town].Name,
		From.X, From.Y, Dest.X, Dest.Y, Dest.Z, Yaw, FVector::Dist2D(From, Dest) / 100.f);
	ClientTravelArrived(Dest, Yaw);
}

// ---- the player's machine ----
void UTravelComponent::ClientTravelStart_Implementation(uint8 Town, float Seconds)
{
	ClChanTown = Town;
	ClChanEnd = FPlatformTime::Seconds() + Seconds;
}

void UTravelComponent::ClientTravelCancel_Implementation()
{
	ClChanTown = -1;
	ClChanEnd = 0.0;
}

void UTravelComponent::ClientTravelArrived_Implementation(FVector_NetQuantize Dest, float Yaw)
{
	if (!HasAuth()) if (AKnightCharacter* K = Knight()) K->TravelPlaced(Dest, Yaw);   // (the server's own copy was placed already)
	ClChanTown = -1;
	ClChanEnd = 0.0;
	ClBlackUntil = FPlatformTime::Seconds() + 0.5;   // (a moment of black for the world round the new place to be there, then it fades up)
	UE_LOG(LogTemp, Log, TEXT("JEEBTRAVEL client: arrived at %.0f %.0f %.0f"), Dest.X, Dest.Y, Dest.Z);
}

bool UTravelComponent::Channelling(int32& OutTown, float& OutLeft) const
{
	const double Now = FPlatformTime::Seconds();
	if (ClChanTown < 0 || ClChanEnd <= 0.0 || Now > ClChanEnd + 1.0) return false;
	OutTown = ClChanTown;
	OutLeft = float(FMath::Max(0.0, ClChanEnd - Now));
	return true;
}

float UTravelComponent::TickFade(float Dt)
{
	const double Now = FPlatformTime::Seconds();
	if (ClChanEnd > 0.0 && Now > ClChanEnd + 4.0) { ClChanEnd = 0.0; ClChanTown = -1; }   // (the arrival never came: don't hold the screen black)
	const bool bBlack = (ClChanEnd > 0.0 && Now >= ClChanEnd - 0.35) || Now < ClBlackUntil;
	ClFade = bBlack ? FMath::Min(1.f, ClFade + Dt / 0.35f) : FMath::Max(0.f, ClFade - Dt / 0.5f);
	return ClFade;
}
