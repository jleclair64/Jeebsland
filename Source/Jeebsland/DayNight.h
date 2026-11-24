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

// The day and night (2026-11-17): the sun circles the sky once a loop, 41 degrees up at noon and 25 below the horizon at
// midnight (until 2026-11-17 a midnight sun that never set: 41 to 11). While it is down a cool moonlight (a second
// directional light, the sky atmosphere's light 1) takes over, with the night's own lift in exposure, sky light and a
// blue grade (the jeeb.Night.* console variables), so the night stays readable, never pitch black. Drives the sun's height,
// colour and strength, the sky light and the haze colour. Other actors (stars, moon, clouds) read the shared values below.
UCLASS()
class ADayNight : public AActor
{
	GENERATED_BODY()

public:
	ADayNight();
	virtual void Tick(float DeltaSeconds) override;

	// Noon to the low golden point (and the same back). 60 s to preview; set to 1200 for 20 minutes.
	static constexpr float HalfLoopSeconds = 600.f;   // a full loop of the sun every 20 minutes (owner, 2026-11-15; was 2 minutes)
	float ClockSpeed = 1.f;   // how fast this sun runs (MoveLab sets 3, to see the light change quickly; the world stays at 1)
	float SunSpeedExtra = 0.f;   // local extra clock from the LOOK tab's sun-speed slider (jeeb.Sun.Speed)

	// Shared state, updated every frame
	static float Phase;          // 0 = noon, 0.5 = the sun's lowest point
	static float SunElevation;   // degrees above the horizon (-25 at midnight to 41 at noon; jeeb.Sun.Low)
	static float Dusk;           // 0 = broad day, 1 = the sun low by the horizon or below it (lamps, lit windows; 1 all night)
	static float Night;          // 0 = the sun up, 1 = full night (the sun 10 degrees down or more): moonlight, the night grade, stars
	static FVector SunDir;       // unit vector toward the sun
	static FLinearColor TrailLight;   // how glowing things that make their own light (sword and arrow trails) should look right now: white at noon, dimmer and warmer as the sun sinks
	static FVector MoonDir;      // unit vector toward the moon (30 degrees up in the south-south-west; the moonlight comes from there)
	static float FixedPhase;     // >= 0 freezes the clock at that phase (used for test screenshots)
	static float IndoorBias;     // (2026-11-22) extra exposure in stops while the camera is inside a walk-in house (AHouseInterior; 0 outside)
	static constexpr float CloudWindSpeed = 1.f;   // how fast the clouds drift; raise it if they crawl, lower it if they race

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY() APostProcessVolume* Post = nullptr;
	// The sun's glint on polished armour (2026-11-15): the world's sun gives no shine (no glare on the grass), so this second sun,
	// shine only, no shadows, lights only lighting channel 1 (the knight's plate); jeeb.Armour.Glint is its strength
	UPROPERTY() class UDirectionalLightComponent* Glint = nullptr;
	// The moonlight (2026-11-17): a second directional light, the sky atmosphere's light 1 (so it lights the night sky and clouds a
	// faint blue, and the sky light gathers that as the night's ambient), its disc hidden (the moon is AMoon's picture). It fades in
	// only once the sun's light on the ground is out and takes over the shadows, so there is never more than one shadow caster.
	UPROPERTY() class UDirectionalLightComponent* Moonlight = nullptr;
	float SunBaseDiffuse = 1.f, SunBaseSpecular = 1.f;   // the map sun's own diffuse and specular scales
	int32 SunShadowNow = -1, MoonShadowNow = -1;          // shadows last set on the sun and the moon (only re-sent when they change)
	int32 SunShownNow = -1;                                // the sun light's visibility last set (hidden in deep night: no light pass)
	void TickMoon(float MoonW);
	TWeakObjectPtr<ADirectionalLight> Sun;
	TWeakObjectPtr<ASkyLight> Sky;
	TWeakObjectPtr<AExponentialHeightFog> Fog;
	TWeakObjectPtr<class USkyAtmosphereComponent> Atmosphere;
	float Clock = 0.f;
	FLinearColor OldDirGlow = FLinearColor::Black;   // the map's own sun glow in the haze (for -OldLook)
	float OldDirExponent = 4.f, OldDirStart = 10000.f, OldAerialStart = 0.1f;
	float ContactNow = -1.f;   // the sun's contact shadow length last set (it is only re-sent when it changes)
	float DfNow = -1.f;        // the sun's distance field shadow reach last set, m (jeeb.Shadow.DF)
	float FarNow = -1.f;      // the sun's far shadow cascade reach last set, m (jeeb.Shadow.Far)
	int32 ShaftsNow = -1;      // the sun's light shafts last set (jeeb.Look.Shafts)
	void SetShafts(int32 On);

	// The picture's look (2026-11-12): the colour grade, the haze toward far hills, ambient occlusion and the sun's contact
	// shadows, eased with the sun's height; its numbers are the jeeb.Look.* console variables in DayNight.cpp
	void TickLook(float High);
	float RecaptureTimer = 0.f;
	bool bSkyRealTime = false;
	bool bSkyLowerBlack = false;   // the map's own sky light setting, for jeeb.Sky.Ground 0
	float SkyGroundNow = -1.f;

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
