#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DayNight.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class APostProcessVolume;
class UMaterialInstanceDynamic;

namespace JeebLook { bool Old(); }   // true: the picture's look before 2026-11-12 (-OldLook or jeeb.Look 0, test pictures only)

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
	float ClockSpeed = 1.f;   // how fast this sun runs (MoveLab sets 3, to see the light change quickly; the world stays at 1)

	// Shared state, updated every frame
	static float Phase;          // 0 = noon, 0.5 = the sun's lowest point
	static float SunElevation;   // degrees above the horizon (11 to 41)
	static float Dusk;           // 0 = broad day, 1 = the sun down by the horizon
	static FVector SunDir;       // unit vector toward the sun
	static FLinearColor TrailLight;   // how glowing things that make their own light (sword and arrow trails) should look right now: white at noon, dimmer and warmer as the sun sinks
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
	TWeakObjectPtr<class USkyAtmosphereComponent> Atmosphere;
	float Clock = 0.f;
	FLinearColor OldDirGlow = FLinearColor::Black;   // the map's own sun glow in the haze (for -OldLook)
	float OldDirExponent = 4.f, OldDirStart = 10000.f, OldAerialStart = 0.1f;
	float ContactNow = -1.f;   // the sun's contact shadow length last set (it is only re-sent when it changes)

	// The picture's look (2026-11-12): the colour grade, the haze toward far hills, ambient occlusion and the sun's contact
	// shadows, eased with the sun's height; its numbers are the jeeb.Look.* console variables in DayNight.cpp
	void TickLook(float High);
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

	// How the HIGH volumetric clouds are lit and sampled (2026-11-13, owner's reference: big billowing sunlit heaps, warm light
	// through them, soft blue-grey undersides): the jeeb.Cloud.* console variables in DayNight.cpp, re-sent only when they change
	TWeakObjectPtr<class UVolumetricCloudComponent> CloudComp;
	float CloudBaseScale = 0.f, CloudBaseFill = 0.f;   // the cloud material's own Layout_CloudGlobalScale and FillScatterIntensity (0: it has none)
	FString CloudSent;
	void ApplyCloudLook();

	// LOW has no volumetric clouds (r.VolumetricCloud 0): a ring of painted cumulus cards far out round the camera instead (one
	// mesh, one draw), shown only while the volumetric clouds are off
	UPROPERTY() class UProceduralMeshComponent* LowClouds = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* LowCloudMat = nullptr;
	void BuildLowClouds();
	void TickLowClouds(float High);
};
