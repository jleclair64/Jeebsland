#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DayNight.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class APostProcessVolume;

// A midnight sun, like June above the Arctic Circle: the sun circles the sky, high at noon and skimming the
// horizon at the bottom of its loop (the golden dusk), and never sets. Drives the sun's height, colour and
// strength, the sky light and the haze colour. Other actors (stars, moon, clouds) read the shared values below.
UCLASS()
class ADayNight : public AActor
{
	GENERATED_BODY()

public:
	ADayNight();
	virtual void Tick(float DeltaSeconds) override;

	// Noon to the low golden point (and the same back). 60 s to preview; set to 1200 for 20 minutes.
	static constexpr float HalfLoopSeconds = 60.f;

	// Shared state, updated every frame
	static float Phase;          // 0 = noon, 0.5 = the sun's lowest point
	static float SunElevation;   // degrees above the horizon (6 to 54)
	static float Dusk;           // 0 = broad day, 1 = the sun down by the horizon
	static FVector SunDir;       // unit vector toward the sun
	static FVector MoonDir;      // unit vector toward the moon (it hangs low in the south-west)
	static float FixedPhase;     // >= 0 freezes the clock at that phase (used for test screenshots)

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() APostProcessVolume* Post = nullptr;
	TWeakObjectPtr<ADirectionalLight> Sun;
	TWeakObjectPtr<ASkyLight> Sky;
	TWeakObjectPtr<AExponentialHeightFog> Fog;
	float Clock = 0.f;
	float RecaptureTimer = 0.f;
	bool bSkyRealTime = false;
};
