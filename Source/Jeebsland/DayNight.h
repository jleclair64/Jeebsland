#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DayNight.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class APostProcessVolume;
class UMaterialInstanceDynamic;

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
	static float SunElevation;   // degrees above the horizon (11 to 41)
	static float Dusk;           // 0 = broad day, 1 = the sun down by the horizon
	static FVector SunDir;       // unit vector toward the sun
	static FVector MoonDir;      // unit vector toward the moon (it hangs low in the south-west)
	static float FixedPhase;     // >= 0 freezes the clock at that phase (used for test screenshots)
	static constexpr float CloudWindSpeed = 1.f;   // how fast the clouds drift; raise it if they crawl, lower it if they race

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

	// Sky weather: the cloud layer drifts overhead and its cover wanders between a clear sky, scattered cloud and heavy cloud
	UPROPERTY() UMaterialInstanceDynamic* CloudMat = nullptr;
	float BaseCoverage = 0.5f, BaseDensity = 1.f;
	float CloudCover = 1.f;        // multiplier on the original look: 0 = clear, 1 = as it was, 1.5 = about 50% more cloud
	float CloudTarget = 1.f;
	float CloudHold = 20.f;        // seconds until the next change of weather
	FRandomStream WeatherRand;
	void TickClouds(float DeltaSeconds);
};
