#pragma once

#include "CoreMinimal.h"

class AKnightCharacter;
class UPoseableMeshComponent;
class USkeletalMesh;
class UAnimSequence;

// The new suit's animator (2026-11-05, MoveLab only). Code, not an Anim Blueprint: every frame it looks at what the knight is doing
// (the same state the old key poses read), picks clips from Mixamo (Tools/knight_clip_retarget.py put them on his skeleton), blends
// them, and writes the bones to a poseable mesh. Nothing here changes the game: the server and every rule still run off the old
// joints; this is what the player sees when the suit is on.
//
// How it keeps things from looking choppy:
//   - Walking, running and sprinting in every direction share one phase, timed by distance so the planted foot doesn't slide.
//   - Every switch (run to swing, swing to roll, roll to fall ...) is an inertial blend: the new pose starts at once, and the
//     difference from the old pose, with the speed it was moving at, dies away on a spring (Gears of War's trick).
//   - Moves are time-warped clips: each move's clip times are tied to the game's own timing (wind-up, hit frames, recovery) in
//     Content/Data/KnightMoves.csv, read when the suit is first put on.
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
}

class FKnightAnimator
{
public:
	bool Init(UPoseableMeshComponent* InComp, USkeletalMesh* Mesh);
	void Update(float Dt, AKnightCharacter* Me);
	void PlaceGear(AKnightCharacter* Me);   // sword, shield, bow and quiver onto this frame's bones (after Update)
	void ReleaseGear(AKnightCharacter* Me); // the suit is off: the joints are the old pose's again

	// The body's extra turn away from the capsule (rolls go the way he is rolling): degrees of yaw
	float BodyYaw() const { return YawNow; }
	float FloorOffset() const { return 0.f; }

	void TickGallery(AKnightCharacter* Me);   // test pictures (-SuitGallery)
	TArray<UPoseableMeshComponent*> GalleryComps;
	class UTextRenderComponent* GalleryLabel = nullptr;
	int32 GalleryTick = 0;

	// Test pictures: hold a named clip instead of what the knight is doing (-SuitClip=Name)
	FString ForcedClip;
	float ForcedPhase = -1.f;

private:
	using FPose = JeebAnim::FPose;
	using FClip = JeebAnim::FClip;

	// clips
	const FClip* LoadClip(const FString& Name, const TCHAR* Folder = TEXT("/Game/NewSuit/Clips/A_"));
	void Measure(FClip& C);
	const FClip* Get(const TCHAR* Name) const { const FClip* const* F = Clips.Find(Name); return F ? *F : nullptr; }
	void LoadMoves();

	// sampling and blending
	void Sample(const FClip& C, float T, FPose& Out, bool bLoop) const;
	float PhaseTime(const FClip& C, float P) const;
	void Accum(const FPose& P, float W, FPose& Sum, float& SumW) const;
	void Finish(FPose& Sum, float SumW) const;
	FQuat ParentRot(const FPose& P, int32 Bone) const;
	void TurnInMesh(FPose& P, int32 Bone, const FQuat& Q) const;
	FVector BonePos(const FPose& P, int32 Bone, FQuat* OutRot = nullptr) const;
	void Overlay(FPose& Base, const FPose& Over, float Upper, float Lower) const;

	// the layers
	void Locomotion(float Dt, AKnightCharacter* Me, FPose& Out);
	void Overlays(float Dt, AKnightCharacter* Me, FPose& B);
	void Inertialize(const FPose& Target, FPose& Out, float Dt);
	void StartBlend(const FPose& Target, float Seconds, float HandSeconds = 0.16f);
	void KeepTipUp(FPose& P, AKnightCharacter* Me) const;
	int32 ChooseAction(float Dt, AKnightCharacter* Me, FPose& B, float& OutBlend);
	float MoveClipTime(const JeebAnim::FMove& M, float U, float UHit, float UFollow) const;
	bool PlayAt(const TCHAR* MoveName, float T, float Speed, FPose& InOut, float* OutBlend);

	UPoseableMeshComponent* Comp = nullptr;
	int32 Bone[JeebAnim::B_Count];
	TArray<int32> Parents;
	TArray<FVector> RefPos;
	TArray<FQuat> RefRot;
	TArray<int32> UpperMask;   // per bone: 1 above the waist
	TMap<FString, FClip*> Clips;
	TArray<TUniquePtr<FClip>> ClipStore;
	TMap<FString, JeebAnim::FMove> Moves;

	FPose Scratch, ScratchSum, Base, Over, Final, PrevFinal;
	bool bHavePrev = false;
	float PrevDt = 1.f / 60.f;

	// inertial blend
	TArray<FVector> OffRot, OffRotV;
	FVector OffHip = FVector::ZeroVector, OffHipV = FVector::ZeroVector;
	float BlendT = 0.f, BlendLambda = 20.f, BlendLambdaHand = 20.f;
	bool bBlending = false;
	int32 StateKey = -1;

	// locomotion
	float Phase = 0.f;
	float SmSpeed = 0.f;
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
	bool bJumpRunning = false;
	bool bRollBack = false;
	float CombatT = 0.f;         // seconds left on guard after a fight
	bool bOnGuard = false;
	float PlayUpper = 1.f, PlayLower = 1.f;   // the next PlayAt's weights
	bool bLegSaved = false;
	FVector KneeRel = FVector::ZeroVector, AnkleRel = FVector::ZeroVector;

	// gear
	FVector BladeInHand = FVector::ForwardVector;
	FTransform RightGrip = FTransform::Identity;    // the old wrist's frame, in the new right hand's
	FTransform ShieldAtRest = FTransform::Identity, ShieldAtGuard = FTransform::Identity;   // the shield in the new left forearm's frame
	FTransform BowAtRest = FTransform::Identity, BowAtAim = FTransform::Identity;           // the bow's hold in the new left hand's frame
	FTransform QuiverOnHips = FTransform::Identity;
	bool bGearReady = false;
	bool bQuiverSaved = false;
	FTransform QuiverRel = FTransform::Identity;
	FVector WristRel = FVector::ZeroVector;
	FQuat WristRelRot = FQuat::Identity;
	bool bWristSaved = false;
	void SolveGear(AKnightCharacter* Me);
	FTransform BindInGame(const FPose& P, int32 B) const;
	void HideOldBody(AKnightCharacter* Me, bool bHide);
	float LogT = 0.f;
};
