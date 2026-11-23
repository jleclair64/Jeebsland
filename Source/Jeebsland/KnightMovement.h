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
// overhang of more than 15 degrees. What can be mantled (2026-11-16, owner: "any object just too high for the knight to jump over, but not so high
// that it looks weird"): any solid he can't walk through whose top is MantleMin to MantleMax (Tuning.ini [Climb]) above his feet, decided from its
// own collision (MantleCandidate: not a pawn, not moving, not climbable rock, no taller than he can get over, not a post, standing on the ground),
// so new props work by themselves. JeebNoMantle on a component or its actor opts it out; JeebMantle marks one in by hand (MoveLab's test fences).
//
// Climbing stamina is a bar from 0 to 1; how long it lasts comes from the Climbing level (AKnightCharacter::ClimbSeconds, Tuning.ini [Climb]).
// It refills on the ground. Climbing gives Climbing XP for new height (server only). There is no fall damage in the game (owner).
namespace JeebClimb
{
	enum EMode : uint8 { None = 0, Climb = 1, Jump = 2, ClimbUp = 3, MantleOver = 4, MantleOnto = 5, Swim = 6, Drown = 7 };   // (6, 7: swimming, 2026-11-13: KnightMovementSwim.cpp)
	static const FName ClimbTag(TEXT("JeebClimb"));
	static const FName MantleTag(TEXT("JeebMantle"));
	static const FName NoMantleTag(TEXT("JeebNoMantle"));
	constexpr float WallGap = 4.f;   // cm between the capsule and the rock while on it
	// Climbable ground (2026-11-20, owner: "trigger the climbing ... near terrain like this riverbank that's too steep to walk up"): the tag, or the
	// town grounds that carry the river's banks (ATownLabTown's TownGround, AOakHillTown's OakHillGround; built alike on every machine, by name so
	// the world-building files stay untouched). Buildings, pads' stones, walls and props are separate components and never count.
	inline bool IsClimbGround(const UPrimitiveComponent* C)
	{
		if (!C) return false;
		if (C->ComponentHasTag(ClimbTag)) return true;
		static const FName TownGround(TEXT("TownGround")), OakGround(TEXT("OakHillGround"));
		return C->GetFName() == TownGround || C->GetFName() == OakGround;
	}
	constexpr float GrabMinRise = 60.f;   // the steep ground ahead has to rise at least this far over his feet (MantleMin): no grabbing small banks and pad edges
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
	float SwimStamina = 1.f, Breath = 1.f, SwimRefillWait = 0.f, DrownT = 0.f;   // (swimming)
	bool bHeadUnder = false;
	FVector ActEdge = FVector::ZeroVector; float ActFar = 0.f;   // (the mantle's near top edge and far edge: the path over the top)
	float StrafeSprint = 1.f, AirMul = 1.f;   // (the running speed's eased shares: UKnightMovement::MoveStrafeSprint, MoveAirMul)
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
	bool InWater() const { return IsCustom(JeebClimb::Swim) || IsCustom(JeebClimb::Drown); }   // swimming (or drowning): KnightMovementSwim.cpp
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
	bool MantleNearby() const;                       // anything he might mantle within reach (a cheap first check: one overlap, no traces)
	// Whether this solid (an instanced one: its instance Item) is something he may mantle, judged from its own collision and size, no traces (cheap
	// enough for every impact). bFace: the side he runs into, which must also stand on the ground at his feet (not a beam or a deck).
	bool MantleCandidate(const UPrimitiveComponent* C, int32 Item, float FeetZ, bool bFace) const;
	int32 Grabs = 0;      // (tests: grabs this machine has made, -BankClimb checks no regrab)
	int32 NetFixes = 0;   // (tests: corrections this machine has taken from the server, log JEEBNETFIX)
	mutable FString MantleWhy;   // (the log: why the last plan found no mantle)
	// The server drops him off the wall (a clean hit, a knockdown, death); the correction carries it to his machine
	void ForceDrop(bool bAll = false);   // bAll: a mantle too (death moves him away)

	// ---- Unreal's movement ----
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void PhysFlying(float DeltaTime, int32 Iterations) override;   // (the wizard's broom: KnightCharacterBroom.cpp)
	virtual void PhysicsRotation(float DeltaTime) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual float GetMaxSpeed() const override;
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.f, const FVector& MoveDelta = FVector::ZeroVector) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;

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
	FVector ActEdge = FVector::ZeroVector;   // a mantle: the top of the fence or block's near edge, where the hands go (and, since 2026-11-18 night, where his feet must clear; travels in corrections)
	float ActFar = 0.f;                      // a vault: how far past ActFrom (along ActDir) the obstacle's far edge is: he stays up over the top until his feet are past it
	bool FindMantleEdge(const FVector& Center, const FVector& Fwd, FVector& OutEdge, float* OutFar = nullptr, FVector* OutFace = nullptr) const;
	static float MantleFarPast(const FVector& From, const FVector& Edge, const FVector& Face, uint8 Mode, float Far);   // (others' machines: the same edge, found as the mantle starts)

	// ---- swimming (2026-11-13, KnightMovementSwim.cpp; numbers in Tuning.ini [Swim]) ----
	// The water comes only from JeebWater::DepthAt (the drawn water has no collision). Deep water at his feet (SwimEnterDepth) turns walking or
	// falling into the swim; standing room (SwimExitDepth) turns it back. Inputs, carried by the saved move: Ctrl (bWantsLetGo, FLAG_Custom_0)
	// dives, Shift (bWantsFast, FLAG_Custom_1) swims fast, Space (bWantsRise, FLAG_Custom_2) rises. The swimming bar or the breath running out
	// is drowning (mode Drown): he sinks, and the SERVER alone ends it with AKnightCharacter::Die once DrownT reaches DrownSinkTime.
	bool bWantsFast = false, bWantsRise = false;
	float SwimStamina = 1.f;          // 0..1, lasts AKnightCharacter::SwimSeconds(level) of normal swimming; refills only on dry ground
	float Breath = 1.f;               // 0..1, drains with his head under water (BreathSeconds), refills with it out
	float SwimRefillWait = 0.f;
	float DrownT = 0.f;               // seconds drowning
	bool bHeadUnder = false;          // (with a little hysteresis)
	float WaterDepth = 0.f;           // cm of water over his feet at the start of this move (0: none); WaterSurface its level
	float WaterSurface = -1.0e9f;
	float ServerWaterWall = 0.f;      // (server) seconds of the server's own clock he has spent in the water this time
	float ServerWaterSim = 0.f;       // (server) seconds his moves have swum or sunk this time: the backstop below drains whatever his moves fall behind the clock
	void ResetWater();                // full bars, not drowning (death, respawn)
	void ServerWaterBackstop(float Dt);   // (server, every frame) a player in the water whose moves stop arriving still runs out of breath and drowns
	bool DrownedOut() const;          // drowned long enough: the server's cue for Die()
	static float WadeMul(float Depth); // walking speed share in water this deep at his feet

	// ---- his running speed and the push that starts him, worked out inside each move (2026-11-18 night) ----
	// Online, a client starting to run from a standstill took small corrections. The cause: the knight's Tick set MaxAcceleration
	// (WalkAccel below walking speed, SprintAccel above) and MaxWalkSpeed (strafe mode's sideways and backward shares, read from the LAST
	// move's push) once per frame. The server runs several of a client's moves between two of its own frames, so its second move of a
	// start-up still had WalkAccel (30000) where the client had already switched to SprintAccel (2048): the engine's analog share
	// (push / MaxAcceleration) came out 0.07 on the server, so the server braked him and corrected the client. Now both are worked out
	// here, from the move's own velocity, push, facing and Shift (FLAG_Custom_1), so the client and the server agree move by move. The
	// knight's Tick only publishes the slow-changing parts (gear, potions, stances) in SpeedRule.
	struct FSpeedRule
	{
		bool bByMove = false;      // the speed is the plain run / sprint (no roll, horse, stance, crouch, guard, smash, bow, drink): GetMaxSpeed works it out
		bool bStrafe = false;      // facing the camera (strafe mode, no shield stance): sideways 90%, back 75%, the sprint only near straight ahead
		bool bSprintOk = false;    // he may sprint if Shift is held (not riding, crouched, guarding or out of breath)
		bool bAccelRule = false;   // on foot, not rolling or sliding: below walking speed he gets there at once (WalkAccel)
		float Walk = 500.f, Sprint = 750.f;
		float K = 1.f;             // potion x gear speed x armour weight
		float WalkCapBase = 500.f; // walking speed x armour weight (the start-up rule's limit, before the air share)
		float AirMul = 0.69f;      // the share of speed in the air
	};
	FSpeedRule SpeedRule;
	float MoveStrafeSprint = 1.f;  // how much of the sprint applies (eases in as he turns toward straight ahead); advanced per move
	float MoveAirMul = 1.f;        // AirMul in the air, easing back to 1 after landing; advanced per move
	float MoveCap(const FVector& Dir) const;   // the run / sprint speed for a push along Dir (before wading)
	float AccelRuleNow() const;                // WalkAccel or SprintAccel from the velocity now
	virtual float GetMaxAcceleration() const override;
	virtual void ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds) override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;

private:
	bool bAccelFrozen = false;     // (inside a move: the start-up rule is decided once, from the move's starting velocity)
	float FrozenAccel = 0.f;
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
	bool PlanMantle(const FVector& Center, const FVector& Fwd, bool bInAir, uint8& OutMode, FVector& OutTo, float& OutTop, FVector& OutFace, FVector* OutEdge = nullptr, float* OutFar = nullptr) const;
	bool RoomFor(const FVector& Center) const;
	void StartScripted(uint8 Mode, const FVector& To, float Dur, float Top, const FVector& Dir);
	void PhysClimb(float Dt);
	void PhysClimbJump(float Dt);
	void PhysScripted(float Dt);
	bool HugWall(float Dt, float InUp, const FVector& OldLoc);   // keeps him WallGap off the rock and follows its slope; false when he left it
	void FaceWall(float Dt);
	void GiveXP();
	void FlushClimbXP();                     // (server) the Climbing XP earned on this wall, in one go as he leaves it: the skill pop-up shows once, not every 25 cm
	int32 ClimbXPPending = 0;
	void LogClimb(const TCHAR* What) const;
	// swimming
	void SenseWater();                       // WaterDepth / WaterSurface at his feet, now
	void StartSwim();
	void PhysSwim(float Dt);
	void PhysDrown(float Dt);
	void SwimDrain(float Dt, float Share);   // the bar and the breath, for one stretch of time
	void StartDrown(const TCHAR* Why);
	bool LeaveWaterIfStanding(float FloorZ, bool bFloorOk);
	void SwimXP(bool bFlush);                // (server) metres swum -> Swimming XP, in lumps
	FVector2D SwimLastXY = FVector2D::ZeroVector;
	float SwimMetres = 0.f;
	float SwimLogT = 0.f;
	void LogSwim(const TCHAR* What, float Extra = 0.f) const;
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
	bool bSavedFast = false, bSavedRise = false;   // (swimming: Shift and Space held; on foot Shift is the sprint)
	float SavedStrafeSprint = 1.f, SavedAirMul = 1.f;   // (at the move's start: a merged move goes back to them)
	float SavedAccel = 0.f;                              // (the start-up push this move was scaled with: moves with different ones are never merged)
	virtual void Clear() override;
	virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation) override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void SetInitialPosition(ACharacter* C) override;
	virtual void PrepMoveFor(ACharacter* C) override;
};

class FNetworkPredictionData_Client_Knight : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;
	FNetworkPredictionData_Client_Knight(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {}
	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FSavedMove_Knight()); }
};
