#pragma once

#include "CoreMinimal.h"

class USceneComponent;
class UHealthComponent;

// Hit boxes and hurt boxes, set up the way Super Smash Bros. Ultimate does it (owner, 2026-10-31):
//  - HURT boxes are capsules fixed to the body's joints (head, torso, upper arms, forearms, thighs, shins), so they follow the
//    pose every frame. A blow lands only where a hit box touches one.
//  - HIT boxes are spheres on the weapon, live only during the move's ACTIVE frames. Each frame a hit box is swept from where it
//    was the frame before (a capsule), so a fast swing can't pass through anyone between two frames.
//  - Each hit box has an ID; when several touch the same target on the same frame, the lowest ID wins (its damage counts).
//    A move hits each target only once.
//  - The view: hurt boxes yellow, live hit boxes red, see-through and drawn over the body. H turns it on and off; MoveLab
//    starts with it on.
namespace JeebHit
{
	struct FCapsule
	{
		FVector A = FVector::ZeroVector, B = FVector::ZeroVector;   // the two ends (equal: a sphere)
		float R = 10.f;
	};

	// One hit box of a move: a sphere Along cm out along the blade from the grip
	struct FHitSphere { int32 Id; float Along; float Radius; float DamageMul; };

	// Closest distance between two line segments
	float SegmentDistance(const FVector& P0, const FVector& P1, const FVector& Q0, const FVector& Q1);
	inline bool Touch(const FCapsule& X, const FCapsule& Y) { return SegmentDistance(X.A, X.B, Y.A, Y.B) <= X.R + Y.R; }

	// A knight's hurt boxes (the player's and the pen knight's share the skeleton, KnightDesign.h)
	struct FKnightJoints
	{
		const USceneComponent* Upper = nullptr; const USceneComponent* Head = nullptr;
		const USceneComponent* ArmL = nullptr; const USceneComponent* ArmR = nullptr;
		const USceneComponent* ElbowL = nullptr; const USceneComponent* ElbowR = nullptr; const USceneComponent* WristR = nullptr;
		const USceneComponent* HipL = nullptr; const USceneComponent* HipR = nullptr;
		const USceneComponent* KneeL = nullptr; const USceneComponent* KneeR = nullptr;
		const USceneComponent* AnkleL = nullptr; const USceneComponent* AnkleR = nullptr;
	};
	void KnightHurt(const FKnightJoints& J, TArray<FCapsule>& Out);

	// Anything with health: its own hurt boxes (UHealthComponent::GatherHurt), or an upright capsule round its hit centre
	void HurtOf(const UHealthComponent* H, TArray<FCapsule>& Out);

	// Whether the boxes are drawn (H flips it; until then: on in MoveLab, off in the game)
	bool ShowBoxes(const UWorld* World);
	bool InMoveLab(const UWorld* World);   // the move test area (players' kicks can hit each other only there, until PvP exists)
	void ToggleShow(const UWorld* World);

	// Adds a capsule to a mesh being built (the view), in one see-through colour
	void AddCapsuleMesh(const FCapsule& C, const FLinearColor& Col, TArray<FVector>& V, TArray<FVector>& N, TArray<int32>& I, TArray<FLinearColor>& Colors);

	// A raised SHIELD (owner, 2026-10-31): a 90-degree wedge in front of the body, like a little baseball diamond with home plate
	// at his centre and walls standing up from the two foul lines (45 degrees either side of straight ahead) and along the
	// outfield fence (an arc Radius out). Checked BEFORE the hurt boxes, as Smash checks shields first: a hit box that touches
	// the wedge is blocked, even if it touches a hurt box on the same frame. Blows from outside it reach the body.
	// The shield itself (owner, 2026-10-31, second pass): only a thin curved band of the wedge, at the distance the raised shield
	// really is from his centre (measured: its face 56 - 83 cm out), still spanning the 90 degrees; his body stands well inside
	// it. Seen from the front it is an arch: straight sides from his feet up to ArchFrom, then curving in (a half ellipse) to
	// the top of his helm in the middle.
	struct FShieldWedge
	{
		FVector Apex = FVector::ZeroVector;     // his centre (home plate), at the capsule's middle height
		FVector Fwd = FVector::ForwardVector;   // straight ahead (flat)
		float Inner = 58.f, Radius = 78.f;      // the band, from his centre (cm)
		float HalfAngle = 45.f;                 // degrees either side of straight ahead
		float ZLow = -96.f, ZHigh = 68.f;       // from his feet to the top of his helm, relative to the apex
		float ArchFrom = -12.f;                 // where the sides start curving in to the top (about his belt)
	};
	// TopZ: the top of his helm (world height; HelmTop), or below the apex for the default height
	inline FShieldWedge KnightShield(const FVector& Centre, const FVector& Forward, float TopZ = -1.0e9f)
	{
		FShieldWedge W; W.Apex = Centre; W.Fwd = Forward.GetSafeNormal2D();
		if (TopZ > Centre.Z) W.ZHigh = TopZ - float(Centre.Z);
		return W;
	}
	float HelmTop(const USceneComponent* Head);   // the top of a knight's helm hurt box (world height), as posed now (crouching lowers it)
	bool TouchWedge(const FCapsule& Hit, const FShieldWedge& W);
	// How far along a hit box's sweep this frame (0 = where it was last frame, 1 = where it is now) it first touches the wedge,
	// or any of the hurt boxes; above 1: it doesn't. Whichever comes first is what the blow meets (a blade that reaches the
	// body before the shield hits the body, owner 2026-10-31).
	float FirstTouchWedge(const FCapsule& Hit, const FShieldWedge& W);
	float FirstTouch(const FCapsule& Hit, const TArray<FCapsule>& Hurt);
	void AddWedgeMesh(const FShieldWedge& W, const FLinearColor& Col, TArray<FVector>& V, TArray<FVector>& N, TArray<int32>& I, TArray<FLinearColor>& Colors);
	bool ShieldOf(const UHealthComponent* H, FShieldWedge& Out);   // anything with health whose shield is up (UHealthComponent::GatherShield)

	// What a clean hit does to whoever it lands on, fighting-game style (2026-10-31): hitstun (Stun s of being unable to act,
	// cut out of any move) with a push away from the attacker, or a launch (the low sweep: up into the air and a little back,
	// bent by the victim's DI, landing on his back as after a kick)
	struct FReact
	{
		float Stun = 0.f;     // seconds
		float Push = 0.f;     // cm/s away from the attacker
		bool bLaunch = false;
	};
	// The low sweep's launch: up and a little away, turned up to DIAngle degrees toward the direction the victim holds (Smash's
	// DI: only the part of it square to the launch counts). Away and DI are flat; DI zero = no DI.
	constexpr float LaunchUp = 450.f, LaunchBack = 150.f, DIAngle = 18.f;   // (about 1 m up at the top, nearly 1 s in the air)
	FVector LaunchVelocity(const FVector& Away, const FVector& DI);
}
