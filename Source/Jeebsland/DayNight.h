#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DayNight.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class APostProcessVolume;
class UDirectionalLightComponent;

// The sun's daily round: noon -> dusk -> midnight -> dawn -> noon, LegSeconds for each leg.
// Drives the sun (colour, height, strength), a soft moonlight at night, the sky light, the haze colour and the exposure.
// Other actors (stars, moon, lamps) read the shared values below.
UCLASS()
class ADayNight : public AActor
{
	GENERATED_BODY()

public:
	ADayNight();
	virtual void Tick(float DeltaSeconds) override;

	// One leg of the day (noon to dusk, and so on). 60 s to preview; set to 1200 for 20-minute legs.
	static constexpr float LegSeconds = 60.f;

	// Shared state, updated every frame
	static float Phase;          // 0 = noon, 0.25 = dusk, 0.5 = midnight, 0.75 = dawn
	static float SunElevation;   // degrees above the horizon (negative at night)
	static float Night;          // 0 = broad day, 1 = full night
	static FVector SunDir;       // unit vector toward the sun
	static FVector MoonDir;      // unit vector toward the moon (it hangs low in the south-west)
	static float FixedPhase;     // >= 0 freezes the clock at that phase (used for test screenshots)

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() UDirectionalLightComponent* MoonLight = nullptr;
	UPROPERTY() APostProcessVolume* Post = nullptr;
	TWeakObjectPtr<ADirectionalLight> Sun;
	TWeakObjectPtr<ASkyLight> Sky;
	TWeakObjectPtr<AExponentialHeightFog> Fog;
	float Clock = 0.f;
	float RecaptureTimer = 0.f;
	bool bSkyRealTime = false;
};
