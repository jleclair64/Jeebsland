#pragma once

#include "CoreMinimal.h"

// Where the shield and the left arm sit when the shield is carried at rest (worked out once, in KnightCharacterBody.cpp).
// Shared by the body code (HoldShield) and the movement tick (the left elbow).
struct FShieldRest
{
	FTransform InArm;
	float ElbowPitch = 85.f, ElbowYaw = 15.f, ElbowRoll = 0.f;
};

const FShieldRest& ShieldRest();
