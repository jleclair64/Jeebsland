#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UPoseableMeshComponent;
class UMaterialInstanceDynamic;
namespace JeebHit { struct FCapsule; struct FShieldWedge; }

// The knight's body (2026-11-05: the only knight in the game). Code, not an Anim Blueprint: every frame it looks at what the knight is
// doing (FInput, filled in by whoever owns him: the player's knight or the pen knight), picks clips from Mixamo (Tools/knight_clip_retarget.py
// put them on his skeleton) and blends them. It runs on every machine, the server too, because the hit boxes and hurt boxes are read from
// the pose it makes. Only players' machines also write it to a mesh (MakeMesh).
//
// How it keeps things from looking choppy:
//   - Walking, running and sprinting in every direction share one phase, timed by distance so the planted foot doesn't slide.
//   - Every switch (run to swing, swing to roll, roll to fall ...) is an inertial blend: the new pose starts at once, and the
//     difference from the old pose, with the speed it was moving at, dies away on a spring (Gears of War's trick).
//   - Moves are time-warped clips: each move's clip times are tied to the game's own timing (wind-up, hit frames, recovery) in
//     Content/Data/KnightMoves.csv.
//
// The clips and the skeleton come from Content/Data/KnightClips.bin (FClipLib), a plain file baked from the imported assets, because a cooked
// server has no animation data of its own. The editor rewrites it by itself whenever the imported clips are newer than it.
namespace JeebAnim
{
	enum EBone : int32 { Hips, Spine, Spine1, Spine2, Neck, Head, LShoulder, LArm, LFore, LHand, RShoulder, RArm, RFore, RHand, LUp, LLeg, LFoot, LToe, RUp, RLeg, RFoot, RToe, B_Count };

	struct FClip
	{
		FString Name;
		int32 NumFrames = 0;
		float Len = 0.f;                // seconds from the first frame to the last
		TArray<FQuat> Rot;              // NumFrames x bones
		TArray<FVector> HipPos;         // NumFrames
		float FootSpeed = 0.f;          // how fast the planted foot moves back under him (cm/s): the speed this clip fits
		float MarkL = 0.f, MarkR = 0.f; // when each foot is under him (s): the phase's two marks
	};

	// A pose: every bone's turn from its parent, and where the hips are
	struct FPose
	{
		TArray<FQuat> Rot;
		FVector Hip = FVector::ZeroVector;
	};

	// One row of Content/Data/KnightMoves.csv
	struct FMove
	{
		FString Clip;
		float ClipStart = 0.f, ClipHit = 0.f, ClipFollow = 0.f, ClipEnd = 0.f;   // seconds in the clip
		float LegsFull = 1.f;        // 1 = the whole body plays the clip; 0 = the arms and chest only, whatever the legs are doing
		float Blend = 0.15f;         // the inertial blend into it (s)
		float HipLock = 0.f;         // 1 = none of the clip's forward travel: the capsule does the travelling
		float SpineKeep = 0.f;       // how much of the chest's turn stays with what he was doing: tames a clip's big wind-round
	};

	// The armour pieces: each is one material slot of the suit, shown and tinted by itself
	enum EPiece : int32 { Piece_Chest, Piece_Legs, Piece_Helmet, Piece_Arms, Piece_Shoulders, Piece_Count };

	// The skeleton, the clips and where gear sits on them: the same for every knight, made once
	struct FClipLib
	{
		static const FClipLib& Get();
		FClipLib();

		bool bReady = false;
		int32 Bone[B_Count];
		TArray<FString> BoneName;
		TArray<int32> Parents;
		TArray<FVector> RefPos;
		TArray<FQuat> RefRot;
		TArray<int32> UpperMask;   // per bone: 1 above the waist
		TMap<FString, FClip*> Clips;
		TArray<TUniquePtr<FClip>> ClipStore;
		TMap<FString, FMove> Moves;

		// gear
		FVector BladeInHand = FVector::ForwardVector;
		FTransform RightGrip = FTransform::Identity;    // the weapon joint (KnightDesign's wrist frame) in the right hand's frame
		FTransform LeftGrip = FTransform::Identity;     // ... and the left hand's (KnightDesign's left elbow frame): a second blade
		FTransform ShieldAtRest = FTransform::Identity, ShieldAtGuard = FTransform::Identity, ShieldAtBlock = FTransform::Identity;   // the shield in the left forearm's frame: hanging, strapped on a raised forearm, held up to guard
		FVector ShieldInGuard = FVector(45.f, -26.f, 112.f);   // where the raised shield's face is, from between his feet (game frame)
		FTransform BowAtRest = FTransform::Identity, BowAtAim = FTransform::Identity;           // the bow's hold in the left hand's frame
		FTransform QuiverOnHips = FTransform::Identity;
		FTransform BeltFlaskOnHips = FTransform::Identity;
		FTransform CrossInHand = FTransform::Identity;
		FTransform LeftOnCross = FTransform::Identity;  // the left hand on the crossbow's stock, in the crossbow's frame (as the aiming clip holds it)
		FTransform DrinkInHand = FTransform::Identity;
		FQuat ChestRest = FQuat::Identity;              // the chest bone's turn at rest (game frame)
		FQuat ShieldArm[4];                             // the left arm's pose holding a shield (LShoulder, LArm, LFore, LHand)
		FVector MouthInHead = FVector::ZeroVector;      // his visor's mouth, in the head bone's frame
		// Where a tool's head is at its clip's blow, from between his feet in his own frame (forward, right, up)
		FVector ToolContact(bool bChop, int32 Kind, float Share = 1.f) const;   // (Share: how far through the blow, 0 tool up, 1 the end of it)
		// A chopping or mining clip holds a long two-handed weapon: the tool runs from the left fist through the right. Turns the hands so, by W, and returns how far the tool rolls in the fist so that its edge leads.
		float ToolPose(FPose& P, float W, int32 Kind) const;
		void ReachTo(FPose& P, int32 Upper, int32 Mid, int32 End, const FVector& Target, const FVector& ElbowHint) const;
		void BothHandsOnGrip(FPose& P, float W, float Below) const;
		void CrossLeftHand(FPose& P, float W) const;   // the left fist onto the crossbow's stock, by W (owner 2026-11-11: held in both hands always)
		FVector ToolStrike(const FTransform& Grip, int32 Kind) const;   // the working point of the tool's edge, given where WristR is
		FVector ToolEdgeInHand = FVector::ZeroVector;       // the way a tool's edge points, in the hand bone's frame
		FQuat ToolEdgeTurn = FQuat::Identity;

		const FClip* Find(const TCHAR* Name) const { const FClip* const* F = Clips.Find(Name); return F ? *F : nullptr; }

		// sampling and blending
		void Sample(const FClip& C, float T, FPose& Out, bool bLoop) const;
		float PhaseTime(const FClip& C, float P) const;
		void Accum(const FPose& P, float W, FPose& Sum, float& SumW) const;
		void Finish(FPose& Sum, float SumW) const;
		FQuat ParentRot(const FPose& P, int32 B) const;
		void TurnInMesh(FPose& P, int32 B, const FQuat& Q) const;
		FVector BonePos(const FPose& P, int32 B, FQuat* OutRot = nullptr) const;
		void Overlay(FPose& Base, const FPose& Over, float Upper, float Lower) const;
		FTransform BindInGame(const FPose& P, int32 B) const;   // where bone B sits in the game's frame (forward +X, right +Y, up +Z; origin between his feet)

	private:
		bool LoadBaked();
		void Finalize();
		void Measure(FClip& C);
		void LoadMoves();
		void SolveGear();
#if WITH_EDITOR
		bool BakeFromAssets();
		const FClip* LoadClipAsset(const FString& Name, const TCHAR* Folder, const TArray<FName>& Names);
		void SaveBaked() const;
		bool BakeIsStale() const;
#endif
	};

	// What he is swinging, as the animator sees it
	enum class EMove : uint8 { None, Jab1, Jab2, SmashCharge, SmashSwing, SmashEnd, Kick, Sweep, Dash, Rebound, Thrust, Cast };

	// What the knight is doing this frame. Everything here must be known on every machine (the server and other players' too): worked
	// out from replicated state and each machine's own timers, never from the camera or a held key.
	struct FInput
	{
		FVector Vel = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		bool bFalling = false;
		float StunLeft = 0.f;
		bool bKnockedDown = false;
		int32 KnockPhase = 0;       // 0 lying, 1 getting up
		bool bRolling = false;
		float RollU = 0.f;          // 0..1 through the roll
		float RollYaw = 0.f;        // the way he rolls, from the way he faces (degrees)
		EMove Move = EMove::None;
		float MoveU = 0.f;          // 0..1 through the swing
		float SmashCharge = 0.f;
		float SmashSwingU = 0.f, SmashEndU = 0.f;
		float DashFrom = 0.3f, DashTo = 0.6f;   // the dash's hit frames as shares of the move
		float ReboundU = 0.f;
		float LegYaw = 0.f;
		float CrouchAlpha = 0.f, BlockAlpha = 0.f, BowAimAlpha = 0.f;
		int32 BowMode = 0;          // 0 lowering, 1 drawing, 2 just loosed
		float BowT = 0.f;
		float BowDrawFor = 1.f;     // how long a full draw takes with what is in his hand
		int32 Kind = -1;            // what is in his hand (JeebItems kind)
		float AimPitch = 0.f;
		bool bClimbing = false; float ClimbPhase = 0.f;
		bool bSitting = false;
		bool bSliding = false;
		bool bFishing = false;
		bool bDrinking = false; float DrinkU = 0.f;   // a potion on its way down: 0..1
		bool bSwinging = false; bool bSideways = false; float SwingU = 0.f;   // a tool at work: chopping or mining
		bool bShotLog = false;      // test pictures: log JEEBANIM
		bool bShieldArm = false;    // a shield on his left arm, not on his back: the forearm holds it out in front
	};
}

class FKnightAnimator
{
public:
	FKnightAnimator();
	bool IsReady() const { return L.bReady; }

	// The mesh, on players' machines only: its bones follow Update. Null if the suit is missing.
	UPoseableMeshComponent* MakeMesh(AActor* Owner, USceneComponent* Parent);
	UPoseableMeshComponent* Mesh() const { return Comp; }
	// Where he is now, between poses: when poses are throttled, the hurt and shield boxes read the last pose at his current place
	void MoveTo(const FVector& Feet, float ActorYaw);

	// A worn piece: Tier < 0 takes it off (the slot's plates vanish), else tinted for that tier
	void Wear(int32 Piece, int32 Tier);

	void Update(float Dt, const JeebAnim::FInput& In, const FVector& Feet, float ActorYaw);

	// This frame's pose, in the world
	FTransform BoneWorld(int32 B) const;
	FVector BoneAt(int32 B) const { return BoneWorld(B).GetLocation(); }
	FTransform RightGripWorld() const;   // WristR: the weapon's joint
	FTransform LeftGripWorld() const { return L.LeftGrip * BoneWorld(JeebAnim::LHand); }
	FTransform ChestWorld() const;     // at the chest bone; its axes are the actor's when he stands at rest
	FTransform ShieldWorld(float BlockAlpha) const;
	FTransform BowWorld(float AimAlpha) const;
	FTransform QuiverWorld() const { return L.QuiverOnHips * BoneWorld(JeebAnim::Hips); }
	FTransform BeltFlaskWorld() const { return L.BeltFlaskOnHips * BoneWorld(JeebAnim::Hips); }
	FTransform CrossWorld() const { return L.CrossInHand * BoneWorld(JeebAnim::RHand); }
	FTransform DrinkWorld() const;     // the open hand and its bottle, the cork aimed at his mouth
	FVector MouthWorld() const { return BoneWorld(JeebAnim::Head).TransformPosition(L.MouthInHead); }
	FVector ToolHeadWorld(int32 Kind) const;   // where the head of the tool in his hand is
	void ToolHands(float& OutOffHaft, float& OutFromButt) const;   // (test log) how far the left fist is from the haft, and how far up the haft from its butt
	int32 WornTier(int32 Piece) const { return WornTiers[Piece]; }   // (-1: nothing on)
	float Phase01() const { return Phase; }   // where the stride is: 0 the left foot under him, 0.5 the right

	// Combat: his hurt boxes as posed now, his helm's top, his raised shield's band, the kicking leg
	void GatherHurt(TArray<JeebHit::FCapsule>& Out) const;
	float HelmTopZ() const;
	JeebHit::FShieldWedge ShieldWedge(const FVector& Centre, const FVector& Forward) const;

	void TickGallery(AActor* Owner, const FVector& Floor);   // test pictures (-SuitGallery)
	TArray<UPoseableMeshComponent*> GalleryComps;
	class UTextRenderComponent* GalleryLabel = nullptr;
	int32 GalleryTick = 0;
	TArray<const JeebAnim::FClip*> GalleryOrder;

	// Test pictures: hold a named clip instead of what the knight is doing (-SuitClip=Name)
	FString ForcedClip;
	float ForcedPhase = -1.f;

private:
	using FPose = JeebAnim::FPose;
	using FClip = JeebAnim::FClip;
	using FInput = JeebAnim::FInput;

	const JeebAnim::FClipLib& L;
	// (short names for the library's data and sampling: the same for every knight)
	const int32* Bone;
	const TArray<int32>& Parents;
	const TArray<FVector>& RefPos;
	const TArray<FQuat>& RefRot;
	const TMap<FString, JeebAnim::FMove>& Moves;
	const FClip* Get(const TCHAR* Name) const { return L.Find(Name); }
	void Sample(const FClip& C, float T, FPose& Out, bool bLoop) const { L.Sample(C, T, Out, bLoop); }
	float PhaseTime(const FClip& C, float P) const { return L.PhaseTime(C, P); }
	void Accum(const FPose& P, float W, FPose& Sum, float& SumW) const { L.Accum(P, W, Sum, SumW); }
	void Finish(FPose& Sum, float SumW) const { L.Finish(Sum, SumW); }
	void TurnInMesh(FPose& P, int32 B, const FQuat& Q) const { L.TurnInMesh(P, B, Q); }
	FVector BonePos(const FPose& P, int32 B, FQuat* OutRot = nullptr) const { return L.BonePos(P, B, OutRot); }
	void Overlay(FPose& Into, const FPose& O, float Upper, float Lower) const { L.Overlay(Into, O, Upper, Lower); }

	// the layers
	void Locomotion(float Dt, const FInput& In, FPose& Out);
	void Overlays(float Dt, const FInput& In, FPose& B);
	void Inertialize(const FPose& Target, FPose& Out, float Dt);
	void StartBlend(const FPose& Target, float Seconds, float HandSeconds = 0.16f, float LegSeconds = 0.f);
	void KeepTipUp(FPose& P, const FInput& In, float Dt);
	void GreatGripLine(FPose& P, const FInput& In, float Dt);
	void LevelCut(FPose& B, int32 Kind, float W, float HandWant, float TipWant, float Dt);
	void ToolHeadClear(FPose& P, const FInput& In) const;
	void LiftDraggedFeet(FPose& P, const FInput& In, float Dt);
	void ExtendArm(FPose& P, float Deg, float W) const;
	void TwoHandTool(FPose& P, const FInput& In, float Dt);
	int32 ChooseAction(float Dt, const FInput& In, FPose& B, float& OutBlend);
	float MoveClipTime(const JeebAnim::FMove& M, float U, float UHit, float UFollow) const;
	bool PlayAt(const TCHAR* MoveName, float T, float Speed, FPose& InOut, float* OutBlend);

	UPoseableMeshComponent* Comp = nullptr;
	class USkeletalMeshComponent* UnderComp = nullptr;  // the mail always under the armour, so plate never looks hollow: follows Comp's bones
	class USkeletalMeshComponent* BareComp = nullptr;   // the mail body under empty slots: follows Comp's bones (made when a slot is first empty)
	bool bBare[JeebAnim::Piece_Count] = {};
	int32 WornTiers[JeebAnim::Piece_Count] = { -1, -1, -1, -1, -1 };
	void ShowBare(int32 Piece, bool bShow);
	int32 PieceMat[JeebAnim::Piece_Count];
	UMaterialInstanceDynamic* PieceMid[JeebAnim::Piece_Count];
	FTransform ToWorld = FTransform::Identity;      // the game's frame in the world (feet, facing)

	FPose Scratch, ScratchSum, Base, Over, Final, PrevFinal;
	bool bHavePrev = false;
	float PrevDt = 1.f / 60.f;

	// inertial blend
	TArray<FVector> OffRot, OffRotV;
	FVector OffHip = FVector::ZeroVector, OffHipV = FVector::ZeroVector;
	float BlendT = 0.f, BlendLambda = 20.f, BlendLambdaHand = 20.f, BlendLambdaLegs = 20.f;
	float TipHeading = 0.f, SweepHeading = 0.f, SweepAt = -10.f;   // (which way the blade points across the floor: KeepTipUp, the low sweep)
	bool bBlending = false;
	int32 StateKey = -1;

	// locomotion
	float Phase = 0.f;
	float SmSpeed = 0.f;
	float LegsW = 1.f, LegsAt = -10.f, PlayDt = 0.f;   // (a move's legs mode changes slowly: PlayAt)
	float FootLift[2] = { 0.f, 0.f }, LiftWindow = 0.f;
	FVector FootPrev[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float DirF = 1.f, DirR = 0.f;
	bool bMoving = false;
	bool bPhaseLeft = true;
	FQuat LocoHand = FQuat::Identity;
	bool bLocoHand = false;
	float IdleT = 0.f;
	float Time = 0.f;
	float YawNow = 0.f;

	// the action running now
	float ActionT = 0.f;
	int32 LastActionKey = 0;
	float PrevStun = 0.f;
	int32 HitPick = 0;
	float LandT = 10.f;
	float LandFor = 0.f;
	float AirT = 0.f;
	float ShakePhase = 0.f;  // the charged smash's tremble
	float ShieldArmW = 0.f;  // how far the left arm is in its shield pose
	float CutHeading = 0.f, CutAt = -10.f;   // (which way the blade points across the floor: LevelCut)
	float JabStepT = 0.f;    // (seconds left of a stance change between the two jabs: the moving boot lifts higher, LiftDraggedFeet)
	float GreatW = 0.f, GreatLineW = 0.f;   // the big sword: how far the left hand is on the grip, and how far the blade follows the line through his fists
	float TwoHandW = 0.f;    // how far the left hand is on the tool's haft
	float CrossW = 0.f;      // how far the left hand is on the crossbow's stock
	float FistW[2] = { 0.f, 0.f };   // how far each hand (0 right, 1 left) is closed into a fist: the morph targets FistR / FistL (Tools/knight_fist.py)
	void SetFists(const FInput& In, float Dt);
	float ToolRoll = 0.f;    // how far the tool has rolled in the fist about its haft (radians)
	int32 ToolKind = 0;      // the tool being worked with
	float AirLegs = 1.f;     // how fast the stride goes: half its pace in the air
	float AirShare = 0.f;    // how far the knees have come up in the air
	float FallShare = 0.f;   // how far into the falling pose (long falls)
	float PeakFall = 0.f;
	bool bWasAir = false;
	bool bLandHard = false;
	float LandStrength = 1.f;    // how far the knees take this landing, by how fast he came down
	int32 LastBowKey = 0;
	float PlayLift = 0.f;        // raises the next PlayAt's hips (cm): the floor clips sink his boots
	int32 HitSerial = 0;
	bool bWasDown = false;
	bool bFallPushed = false;
	float RiseLeft = 0.f;
	bool bRollBack = false;
	float CombatT = 0.f;         // seconds left on guard after a fight
	bool bOnGuard = false;
	float PlayUpper = 1.f, PlayLower = 1.f;   // the next PlayAt's weights
	float PlayKeepOffArm = 0.f;   // the next PlayAt leaves this share of the left arm as it was (clips made for an empty off hand fling it, and he has a shield on it)
	FVector LogPrevL = FVector::ZeroVector, LogPrevR = FVector::ZeroVector;
};
