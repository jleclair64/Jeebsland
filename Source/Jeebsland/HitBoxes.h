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
	struct FShieldWedge
	{
		FVector Apex = FVector::ZeroVector;     // his centre (home plate), at the capsule's middle height
		FVector Fwd = FVector::ForwardVector;   // straight ahead (flat)
		float Radius = 100.f, HalfAngle = 45.f; // cm, degrees
		float ZLow = -96.f, ZHigh = 80.f;       // from his feet to above the helm, relative to the apex
	};
	inline FShieldWedge KnightShield(const FVector& Centre, const FVector& Forward) { FShieldWedge W; W.Apex = Centre; W.Fwd = Forward.GetSafeNormal2D(); return W; }
	bool TouchWedge(const FCapsule& Hit, const FShieldWedge& W);
	// How far along a hit box's sweep this frame (0 = where it was last frame, 1 = where it is now) it first touches the wedge,
	// or any of the hurt boxes; above 1: it doesn't. Whichever comes first is what the blow meets (a blade that reaches the
	// body before the shield hits the body, owner 2026-10-31).
	float FirstTouchWedge(const FCapsule& Hit, const FShieldWedge& W);
	float FirstTouch(const FCapsule& Hit, const TArray<FCapsule>& Hurt);
	void AddWedgeMesh(const FShieldWedge& W, const FLinearColor& Col, TArray<FVector>& V, TArray<FVector>& N, TArray<int32>& I, TArray<FLinearColor>& Colors);
	bool ShieldOf(const UHealthComponent* H, FShieldWedge& Out);   // anything with health whose shield is up (UHealthComponent::GatherShield)
}
