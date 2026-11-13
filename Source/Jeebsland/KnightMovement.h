#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "KnightMovement.generated.h"

// Free climbing (Breath of the Wild style) and the mantle (Fortnite style), 2026-11-12. The knight's own CharacterMovementComponent: both are
// custom movement modes run inside Unreal's movement prediction, so the player's machine predicts them and the SERVER runs the same physics
// from the same moves (the keys' direction, Space, the let-go key) and corrects position, speed, mode and the climbing state below. The server
// never takes the player's word for where he is (unlike the ladder, KnightCharacterMovement.cpp, which is unchanged and separate).
//
// What can be climbed (one rule, Climbable): a surface whose component carries the tag JeebClimb (the world's ground, the original square's
// ground, MoveLab's climbing rock: hills and rock only), too steep to walk (its normal under the walkable limit, about 45 degrees) and not an
// overhang of more than 15 degrees. What can be mantled: a component tagged JeebMantle (fences, low walls, crates) whose top is MantleMin to
// MantleMax (Tuning.ini [Climb]) above his feet.
//
// Climbing stamina is a bar from 0 to 1; how long it lasts comes from the Climbing level (AKnightCharacter::ClimbSeconds, Tuning.ini [Climb]).
// It refills on the ground. Climbing gives Climbing XP for new height (server only). There is no fall damage in the game (owner).
namespace JeebClimb
{
	enum EMode : uint8 { None = 0, Climb = 1, Jump = 2, ClimbUp = 3, MantleOver = 4, MantleOnto = 5 };
	static const FName ClimbTag(TEXT("JeebClimb"));
	static const FName MantleTag(TEXT("JeebMantle"));
	constexpr float WallGap = 4.f;   // cm between the capsule and the rock while on it
}

// The server's climbing state, sent back inside a movement correction so the player's machine replays from the server's numbers, not its own
struct FKnightMoveResponse : public FCharacterMoveResponseDataContainer
{
	float Stamina = 1.f;
	float RefillWait = 0.f;
	bool bExhausted = false, bNoRegrab = false;
	FVector WallNormal = FVector::BackwardVector;
	FVector ActFrom = FVector::ZeroVector, ActTo = FVector::ZeroVector, ActDir = FVector::ZeroVector;
	float ActT = 0.f, ActDur = 0.f, ActTop = 0.f, ActCarry = 0.f, ActHold = 0.f;
	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap) override;
};

UCLASS()
class UKnightMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	UKnightMovement();

	// ---- what the rest of the game asks ----
	bool IsCustom(uint8 M) const { return MovementMode == MOVE_Custom && CustomMovementMode == M; }
	uint8 ClimbMode() const { return MovementMode == MOVE_Custom ? CustomMovementMode : uint8(JeebClimb::None); }
	bool OnWall() const { return IsCustom(JeebClimb::Climb) || IsCustom(JeebClimb::Jump); }   // the climb itself (and its jump)
	bool InClimbMove() const { return MovementMode == MOVE_Custom && CustomMovementMode >= JeebClimb::Climb && CustomMovementMode <= JeebClimb::MantleOnto; }
	bool InMantle() const { return IsCustom(JeebClimb::MantleOver) || IsCustom(JeebClimb::MantleOnto); }
	float ActionShare() const { return ActDur > 0.f ? FMath::Clamp(ActT / ActDur, 0.f, 1.f) : 0.f; }
	float ScriptedHold() const;   // the stand on top at the end of a ledge pull-up or a climb onto, from tuning (inside ActDur; the move keeps its own copy, ActHold)
	// The wall's own directions, from its normal: up along the face, and to his right as he faces it
	static FVector WallUp(const FVector& N);
	static FVector WallRight(const FVector& N);
	// Whether this hit is something he may climb (the one rule above)
	bool Climbable(const FHitResult& H) const;
	// A sphere (20 cm) swept against the world's static and moving objects (never pawns)
	bool ProbeWall(const FVector& From, const FVector& Dir, float Dist, FHitResult& Hit) const;

	// ---- inputs (the player's machine sets them; the saved move carries them to the server) ----
	bool bWantsLetGo = false;     // Ctrl held (FLAG_Custom_0): on the wall he lets go, and he never grabs while it is held
	void QueueClimbJump() { bJumpQueued = true; }   // a fresh Space press on the wall (AKnightCharacter::CheckJumpInput, inside the move)
	void AskJump() { bJumpAsked = true; }           // a fresh Space press on the ground near something mantleable: mantle, or else jump, decided in the move
	bool MantleNearby() const;                       // anything marked JeebMantle within reach (a cheap first check)
	// The server drops him off the wall (a clean hit, a knockdown, death); the correction carries it to his machine
	void ForceDrop(bool bAll = false);   // bAll: a mantle too (death moves him away)

	// ---- Unreal's movement ----
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void PhysicsRotation(float DeltaTime) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual float GetMaxSpeed() const override;
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.f, const FVector& MoveDelta = FVector::ZeroVector) override;

	// The climbing state (simulated on the server and predicted on the player's machine; the server's travels back in corrections)
	float ClimbStamina = 1.f;
	bool bClimbExhausted = false;
	bool bNoRegrab = false;          // let go or ran out: no grabbing again until he has landed
	FVector WallNormal = FVector::BackwardVector;
	float WallDist = 0.f;            // capsule centre to the rock's plane (cm), for the pose
	float RefillWait = 0.f;
	FVector ActFrom = FVector::ZeroVector, ActTo = FVector::ZeroVector, ActDir = FVector::ZeroVector;   // a scripted move (jump, climb-up, mantle)
	float ActT = 0.f, ActDur = 0.f, ActTop = 0.f, ActCarry = 0.f;
	float ActHold = 0.f;   // the stand at the end of this scripted move, taken from tuning as it starts (it travels in corrections, so a reloaded Tuning.ini can't split the machines)
	FVector ActEdge = FVector::ZeroVector;   // a mantle: the top of the fence or block's near edge, where the hands go (the pose only; not part of the move)
	bool FindMantleEdge(const FVector& Center, const FVector& Fwd, FVector& OutEdge) const;   // (others' machines: the same edge, found as the mantle starts)

private:
	bool bJumpQueued = false, bJumpAsked = false;
	bool bTouchRock = false, bTouchMantle = false;   // (the last move ran into climbable rock, or a fence: only then are the grab and the air mantle looked for)
	float ClimbBestZ = 0.f, XPCarry = 0.f;   // (server) the highest he has got on this wall, and XP not yet given
	float LogT = 0.f;
	FKnightMoveResponse KnightResponse;
	class AKnightCharacter* Knight() const;
	float DrainPerSecond() const;            // 1 / seconds of climbing up, by his Climbing level
	bool TryGrab();
	void StartClimb(const FVector& Normal);
	void DropOff(bool bExhausted, float Push);
	bool TryLedge();
	bool PlanLedge(const FVector& Center, const FVector& N, FVector& OutTo, float& OutTop) const;
	bool TryMantle(bool bInAir);
	bool PlanMantle(const FVector& Center, const FVector& Fwd, bool bInAir, uint8& OutMode, FVector& OutTo, float& OutTop, FVector& OutFace, FVector* OutEdge = nullptr) const;
	bool RoomFor(const FVector& Center) const;
	void StartScripted(uint8 Mode, const FVector& To, float Dur, float Top, const FVector& Dir);
	void PhysClimb(float Dt);
	void PhysClimbJump(float Dt);
	void PhysScripted(float Dt);
	bool HugWall(float Dt, float InUp, const FVector& OldLoc);   // keeps him WallGap off the rock and follows its slope; false when he left it
	void FaceWall(float Dt);
	void GiveXP();
	void LogClimb(const TCHAR* What) const;
};

// The saved move: the let-go key, and refuses to merge moves across a change of climbing state
class FSavedMove_Knight : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;
	bool bSavedLetGo = false;
	uint8 SavedMode = 0;
	bool bSavedExhausted = false;
	bool bSavedRefilling = false;
	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;
};

class FNetworkPredictionData_Client_Knight : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;
	FNetworkPredictionData_Client_Knight(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {}
	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FSavedMove_Knight()); }
};
