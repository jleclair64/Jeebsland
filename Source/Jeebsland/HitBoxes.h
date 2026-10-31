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
	void ToggleShow(const UWorld* World);

	// Adds a capsule to a mesh being built (the view), in one see-through colour
	void AddCapsuleMesh(const FCapsule& C, const FLinearColor& Col, TArray<FVector>& V, TArray<FVector>& N, TArray<int32>& I, TArray<FLinearColor>& Colors);
}
