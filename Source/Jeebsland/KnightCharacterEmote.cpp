#include "KnightCharacter.h"
#include "KnightAnimator.h"
#include "JeebEmotes.h"
#include "Messages.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// ---------------------------------------------------------------------------------------------------
// Emotes (2026-11-18; JeebEmotes.h). Wave, bow, cheer, clap, dance, sit, point and salute, from Mixamo.
//   The player's machine asks (RequestEmote -> ServerEmote). The server checks him (alive, standing still on the ground, not
//   attacking, climbing, swimming, stunned, guarding, drawing, drinking, working, riding or seated; at most one ask every
//   0.5 s), starts it and sets RepEmote, which every other machine (the owner's too) plays on arrival.
//   Every machine ends it itself (TickEmote) when its time is up or the moment he moves, jumps, attacks, is hit, rolls, guards ...,
//   so it stops at once wherever he is seen; the server's own end goes out in RepEmote as well, so no copy keeps playing.
// Log: JEEBEMOTE (start and stop, with the machine's role) on every machine.
// ---------------------------------------------------------------------------------------------------

namespace
{
	constexpr float EmoteStillSpeed = 60.f;   // (cm/s) faster than this is moving: no emote, and one playing ends
	constexpr double EmoteAskGap = 0.5;       // (s) the server's limit between asks (reliable RPC; RepEmote goes to everyone)

	const TCHAR* RoleName(const AKnightCharacter* K)
	{
		if (K->GetNetMode() == NM_Standalone) return TEXT("solo");
		if (K->HasAuthority()) return TEXT("server");
		return K->IsLocallyControlled() ? TEXT("owner") : TEXT("other");
	}

	float EmoteLength(int32 Id)
	{
		const JeebEmotes::FDef* D = JeebEmotes::Get(Id);
		if (!D || D->Loops <= 0) return 0.f;
		const JeebAnim::FMove* M = JeebAnim::FClipLib::Get().Moves.Find(D->Row);
		const float Len = M ? FMath::Max(M->ClipEnd - M->ClipStart, 0.05f) : 2.f;
		return Len * D->Loops;
	}
}

bool AKnightCharacter::CanEmote(FString* Why) const
{
	const TCHAR* No = nullptr;
	const UCharacterMovementComponent* M = GetCharacterMovement();
	if (Health <= 0.f) No = TEXT("You can't do that now.");
	else if (!M || M->IsFalling()) No = TEXT("Stand on the ground first.");
	else if (InClimbMove() || bClimbing) No = TEXT("Not while climbing or swimming.");
	else if (bAttacking || StunLeft > 0.f || bKnockedDown || bRolling || bSliding) No = TEXT("Not now.");
	else if (bSitting || bMounted) No = TEXT("Stand up first.");
	else if (bDrinking || bSwinging || bFishing || Bow != EBow::None || bBlocking || bCrouched) No = TEXT("Not now.");
	else if (GetVelocity().Size2D() > EmoteStillSpeed) No = TEXT("Stand still first.");
	if (No && Why) *Why = No;
	return No == nullptr;
}

bool AKnightCharacter::EmoteCancelled(FString& Why) const
{
	const UCharacterMovementComponent* M = GetCharacterMovement();
	if (Health <= 0.f) Why = TEXT("down");
	else if (!M || M->IsFalling()) Why = TEXT("jumped or fell");
	else if (bAttacking) Why = TEXT("attacked");
	else if (StunLeft > 0.f || bKnockedDown) Why = TEXT("hit");
	else if (GetWorld() && GetWorld()->GetTimeSeconds() - LastHurtTime < FMath::Min(EmoteT, 0.5f)) Why = TEXT("hit");   // (any blow since the emote began, not only one that stuns)
	else if (bRolling || bSliding) Why = TEXT("rolled");
	else if (InClimbMove() || bClimbing) Why = TEXT("climbing or swimming");
	else if (bSitting || bMounted) Why = TEXT("seated");
	else if (bDrinking || bSwinging || bFishing) Why = TEXT("busy");
	else if (Bow != EBow::None || bBlocking || bCrouched) Why = TEXT("guard, bow or crouch");
	else if (GetVelocity().Size2D() > EmoteStillSpeed) Why = TEXT("moved");
	else return false;
	return true;
}

bool AKnightCharacter::EmoteStows() const
{
	if (const JeebEmotes::FDef* D = JeebEmotes::Get(EmoteId)) return D->bStow;
	// (2026-11-18, motion review: the gear came back while his arms were still overhead, or across his knees as he rose from the sit.
	// It stays on his back until the body has blended back to standing, unless his hands are wanted at once: a swing, the shield
	// raised, the bow, a drink or a tool give it back on that frame, as climbing's end does.)
	if (EmoteStowLeft <= 0.f) return false;
	return !(bAttacking || bBlocking || bBlockHeld || Bow != EBow::None || bDrinking || bSwinging || bFishing || bClimbing || bMounted);
}

void AKnightCharacter::RequestEmote(int32 Id)
{
	const JeebEmotes::FDef* D = JeebEmotes::Get(Id);
	if (Id != 0 && !D) return;
	if (D && Id == EmoteId && D->Loops == 0) Id = 0;   // (/sit or /dance again: stop)
	FString Why;
	if (Id != 0 && !CanEmote(&Why))
	{
		if (IsLocallyControlled()) JeebMsg::Post(2.5f, FColor::Yellow, Why);
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastEmoteAsk < 0.3) return;
	LastEmoteAsk = Now;
	ServerEmote(static_cast<uint8>(Id));   // (single player: runs here at once)
}

void AKnightCharacter::ServerEmote_Implementation(uint8 Id)
{
	if (Id == 0) { if (EmoteId) StopEmote(TEXT("asked")); return; }
	if (!JeebEmotes::Get(Id)) return;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastServerEmote < EmoteAskGap) return;
	FString Why;
	if (!CanEmote(&Why)) { UE_LOG(LogTemp, Log, TEXT("JEEBEMOTE %s refused %s: %s"), RoleName(this), JeebEmotes::Get(Id)->Name, *Why); return; }
	LastServerEmote = Now;
	StartEmote(Id);
}

void AKnightCharacter::StartEmote(uint8 Id)
{
	const JeebEmotes::FDef* D = JeebEmotes::Get(Id);
	if (!D) return;
	EmoteId = Id;
	EmoteT = 0.f;
	EmoteStowLeft = 0.f;
	EmoteFor = EmoteLength(Id);
	if (HasAuthority()) RepEmote = static_cast<uint16>(((((RepEmote >> 8) + 1) & 0xFF) << 8) | Id);
	UE_LOG(LogTemp, Log, TEXT("JEEBEMOTE %s %s start %s (%s)"), RoleName(this), *GetName(), D->Name, EmoteFor > 0.f ? *FString::Printf(TEXT("%.2f s"), EmoteFor) : TEXT("until ended"));
}

void AKnightCharacter::StopEmote(const TCHAR* Why)
{
	if (!EmoteId) return;
	const JeebEmotes::FDef* D = JeebEmotes::Get(EmoteId);
	UE_LOG(LogTemp, Log, TEXT("JEEBEMOTE %s %s stop %s after %.2f s: %s"), RoleName(this), *GetName(), D ? D->Name : TEXT("?"), EmoteT, Why);
	// (the gear stays on his back for the animator's blend out of the emote: KnightAnimator::Update, keys 100+)
	EmoteStowLeft = D && D->bStow ? (EmoteId == 6 ? 0.7f : GetVelocity().Size2D() > EmoteStillSpeed ? 0.3f : 0.45f) : 0.f;
	EmoteId = 0;
	EmoteT = 0.f;
	if (HasAuthority()) RepEmote = static_cast<uint16>(((((RepEmote >> 8) + 1) & 0xFF) << 8));
}

void AKnightCharacter::OnRep_Emote()
{
	if (HasAuthority()) return;
	const uint8 Id = static_cast<uint8>(RepEmote & 0xFF);
	if (Id) StartEmote(Id);
	else StopEmote(TEXT("the server ended it"));
}

void AKnightCharacter::TickEmote(float Dt)
{
	TickEmoteTests(Dt);
	if (!EmoteId) { EmoteStowLeft = FMath::Max(EmoteStowLeft - Dt, 0.f); return; }
	EmoteT += Dt;
	FString Why;
	if (EmoteCancelled(Why)) StopEmote(*Why);
	else if (EmoteFor > 0.f && EmoteT >= EmoteFor) StopEmote(TEXT("done"));
}

// Tests (not in Shipping):
//   -JeebShot -ShotMove=emote<name> (emotewave ...) plays one; -ShotMove=emoteall plays every emote in turn, the looping ones for
//   -EmoteHold= seconds (4.5), with -EmoteGap= seconds standing between (1.5). Each start and stop is logged with its time since the
//   move began (JEEBEMOTE shot ...): with -benchmark -fps=30 picture n of -ShotEvery=k is at n*k/30 s.
//   -EmoteTest=<name>_<at>_<moveAt> (online, on a client): asks for the emote <at> s after his knight is his, then walks forward for a
//   second from <moveAt> s (0: no walk), to see the start and the cancel on the other machines' logs.
void AKnightCharacter::TickEmoteTests(float Dt)
{
#if !UE_BUILD_SHIPPING
	static FString ShotMove;
	static const bool bShot = FParse::Param(FCommandLine::Get(), TEXT("JeebShot")) && FParse::Value(FCommandLine::Get(), TEXT("ShotMove="), ShotMove) && ShotMove.StartsWith(TEXT("emote"));
	if (bShot && bShotFace && IsLocallyControlled())
	{
		static float Clock = -1.f;
		static int32 Seq = 0;          // which emote of the list is next
		static float NextAt = 0.3f;    // when the next thing happens
		static bool bPlaying = false;
		static float Hold = 4.5f, Gap = 1.5f;
		static const bool bRead = [] { FParse::Value(FCommandLine::Get(), TEXT("EmoteHold="), Hold); FParse::Value(FCommandLine::Get(), TEXT("EmoteGap="), Gap); return true; }();
		(void)bRead;
		static TArray<int32> List;
		if (Clock < 0.f)
		{
			Clock = 0.f;
			const FString Which = ShotMove.Mid(5);
			if (Which == TEXT("all")) for (int32 i = 1; i < JeebEmotes::Num(); ++i) List.Add(i);
			else if (const int32 Id = JeebEmotes::FindByName(Which)) List.Add(Id);
		}
		else Clock += Dt;
		if (bPlaying && !EmoteId) { bPlaying = false; NextAt = Clock + Gap; UE_LOG(LogTemp, Log, TEXT("JEEBEMOTE shot ended at %.2f"), Clock); }
		if (bPlaying && EmoteId && EmoteFor <= 0.f && EmoteT >= Hold) { UE_LOG(LogTemp, Log, TEXT("JEEBEMOTE shot stop asked at %.2f"), Clock); RequestEmote(0); }
		if (!bPlaying && Seq < List.Num() && Clock >= NextAt)
		{
			LastEmoteAsk = -10.0; LastServerEmote = -10.0;
			UE_LOG(LogTemp, Log, TEXT("JEEBEMOTE shot %s at %.2f"), JeebEmotes::Get(List[Seq])->Name, Clock);
			RequestEmote(List[Seq++]);
			bPlaying = EmoteId != 0;
			if (!bPlaying) NextAt = Clock + Gap;
		}
	}
	static FString Test;
	static const bool bTest = FParse::Value(FCommandLine::Get(), TEXT("EmoteTest="), Test, false);
	if (bTest && IsLocallyControlled() && !HasAuthority())
	{
		static float Clock = 0.f, At = 10.f, MoveAt = 0.f;
		static int32 Id = 0;
		static bool bAsked = false;
		static const bool bParsed = [] { TArray<FString> P; Test.ParseIntoArray(P, TEXT("_")); if (P.Num() > 0) Id = JeebEmotes::FindByName(P[0]); if (P.Num() > 1) At = FCString::Atof(*P[1]); if (P.Num() > 2) MoveAt = FCString::Atof(*P[2]); return true; }();
		(void)bParsed;
		Clock += Dt;
		if (!bAsked && Clock >= At) { bAsked = true; UE_LOG(LogTemp, Log, TEXT("JEEBEMOTE test asks for %d at %.1f"), Id, Clock); RequestEmote(Id); }
		if (MoveAt > 0.f && Clock >= MoveAt && Clock < MoveAt + 1.f) AddMovementInput(GetActorForwardVector(), 1.f);
	}
#endif
}
