#pragma once

// Tiring, shared by every huntable animal (2026-11-24, owner: "after being chased a few times an animal runs slower and flees a shorter
// distance, recovering over time"). A member of the animal's server-side mind: Chase() every time it bolts, Tick(Dt) every update,
// SpeedMul() / FleeMul() read where it runs. Numbers: Tuning.ini [Animals] AnimalTire*, AnimalTired*.

#include "CoreMinimal.h"
#include "JeebTuning.h"

struct FAnimalTire
{
	float Tired = 0.f;   // 0 fresh .. 1 worn out

	void Chase() { Tired = FMath::Min(1.f, Tired + FMath::Max(0.f, JeebTune::V().AnimalTirePerChase)); }
	void Tick(float Dt) { Tired = FMath::Max(0.f, Tired - Dt / FMath::Max(1.f, JeebTune::V().AnimalTireRecoverSecs)); }
	void Reset() { Tired = 0.f; }
	float SpeedMul() const { return FMath::Lerp(1.f, FMath::Clamp(JeebTune::V().AnimalTiredSpeedMul, 0.1f, 1.f), Tired); }
	float FleeMul() const { return FMath::Lerp(1.f, FMath::Clamp(JeebTune::V().AnimalTiredFleeMul, 0.1f, 1.f), Tired); }
};
