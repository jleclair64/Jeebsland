#include "DayNight.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Components/VolumetricCloudComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SkyAtmosphereComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"

// The picture's look (2026-11-12, owner: "more professional, Breath of the Wild meets Overwatch"). Console variables, so a
// test run can try other numbers with -ExecCmds="jeeb.Look.Sat 1.1" without a build; the defaults are the chosen look.
namespace JeebLook
{
	static TAutoConsoleVariable<int32> On(TEXT("jeeb.Look"), 1, TEXT("1: the 2026-11-12 grade, haze, AO and contact shadows; 0: the look before it (test pictures)"));
	static TAutoConsoleVariable<float> Sat(TEXT("jeeb.Look.Sat"), 1.1f, TEXT("Overall colour saturation at noon (eases to 1.0 at the low sun, which is golden enough)"));
	static TAutoConsoleVariable<float> Contrast(TEXT("jeeb.Look.Contrast"), 1.0f, TEXT("Overall contrast (the old painterly grade had 0.9)"));
	static TAutoConsoleVariable<float> Warm(TEXT("jeeb.Look.Warm"), 0.04f, TEXT("Warmth in the highlights: red up and blue down by this much"));
	static TAutoConsoleVariable<float> ShadowCool(TEXT("jeeb.Look.ShadowCool"), 0.012f, TEXT("How much the shadows are lifted, a little blue (0.012 was the old lift)"));
	static TAutoConsoleVariable<float> Bloom(TEXT("jeeb.Look.Bloom"), 0.8f, TEXT("Bloom intensity (was 0.95 on every pixel)"));
	static TAutoConsoleVariable<float> BloomThreshold(TEXT("jeeb.Look.BloomThreshold"), -1.f, TEXT("Bloom threshold: -1 every pixel blooms a little; above 0 only what is brighter blooms"));
	static TAutoConsoleVariable<float> Aerial(TEXT("jeeb.Look.Aerial"), 16.f, TEXT("Sky atmosphere aerial perspective distance scale: far hills fade toward the sky's blue (engine default 1)"));
	static TAutoConsoleVariable<float> HazeBlue(TEXT("jeeb.Look.HazeBlue"), 1.f, TEXT("0: the old warm grey height fog; 1: a soft blue-grey haze at noon, a warm peach-lilac at the low sun"));
	static TAutoConsoleVariable<float> HazeStart(TEXT("jeeb.Look.HazeStart"), 2500.f, TEXT("Height fog start distance in cm (was 600): the town around you stays clear"));
	static TAutoConsoleVariable<float> SunGlow(TEXT("jeeb.Look.SunGlow"), 1.f, TEXT("The warm glow in the haze toward the sun (directional inscattering), strongest at the low sun"));
	static TAutoConsoleVariable<float> AO(TEXT("jeeb.Look.AO"), 0.6f, TEXT("Ambient occlusion strength (where it is on: r.AmbientOcclusionLevels, set by ApplyGraphics)"));
	static TAutoConsoleVariable<float> AORadius(TEXT("jeeb.Look.AORadius"), 100.f, TEXT("Ambient occlusion radius in cm"));
	static TAutoConsoleVariable<float> Contact(TEXT("jeeb.Look.Contact"), 0.f, TEXT("The sun's screen-space contact shadow length (share of the screen; 0 off: at 0.03 it cost 0.07 ms and barely showed, 2026-11-13)"));
	static TAutoConsoleVariable<float> AerialStart(TEXT("jeeb.Look.AerialStart"), 0.35f, TEXT("Where the aerial perspective haze begins, in km (engine default 0.1)"));
	static TAutoConsoleVariable<float> Exposure(TEXT("jeeb.Look.Exposure"), 0.35f, TEXT("Exposure bias at noon, in stops (the old fixed 0.35)"));
	static TAutoConsoleVariable<float> LowLift(TEXT("jeeb.Look.LowLift"), 0.8f, TEXT("Extra exposure at the low sun, in stops, so the golden hour glows instead of going murky"));
	static TAutoConsoleVariable<float> Vignette(TEXT("jeeb.Look.Vignette"), 0.4f, TEXT("Darkening toward the picture's corners (engine default 0.4)"));

	// (-OldLook: the look before 2026-11-12 everywhere, also ApplyGraphics' settings, for before/after test pictures)
	bool Old()
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("OldLook"));
		return bOld || On.GetValueOnGameThread() == 0;
#endif
	}
}

float ADayNight::Phase = 0.f;
float ADayNight::SunElevation = 54.f;
float ADayNight::Dusk = 0.f;
FLinearColor ADayNight::TrailLight = FLinearColor::White;
FVector ADayNight::SunDir = FVector::UpVector;
FVector ADayNight::MoonDir = FRotator(15.f, 205.f, 0.f).Vector();
float ADayNight::FixedPhase = -1.f;

ADayNight::ADayNight()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ADayNight::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		Sun = *It;
		if (UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
		{
			L->LightSourceAngle = 4.f;   // a bigger sun disc: shadow edges soften with distance from what casts them (branch shadows blur)
			L->MarkRenderStateDirty();
		}
		break;
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		Sky = *It;
		bSkyRealTime = It->GetLightComponent()->bRealTimeCapture;
		break;
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		Fog = *It;
		const UExponentialHeightFogComponent* FC = It->GetComponent();
		OldDirGlow = FC->DirectionalInscatteringLuminance;
		OldDirExponent = FC->DirectionalInscatteringExponent;
		OldDirStart = FC->DirectionalInscatteringStartDistance;
		break;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
		if (USkyAtmosphereComponent* SA = It->FindComponentByClass<USkyAtmosphereComponent>())
		{
			SA->SetMobility(EComponentMobility::Movable);   // (a static one would refuse the haze changes at run time)
			Atmosphere = SA;
			OldAerialStart = SA->AerialPerspectiveStartDepth;
			UE_LOG(LogTemp, Log, TEXT("JEEBLOOK sky atmosphere found, aerial perspective scale %.2f"), SA->AerialPespectiveViewDistanceScale);
			break;
		}

	// The sky's volumetric clouds: a lower, deeper layer, so they tower up in big billowing heaps lit by the sun
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (UVolumetricCloudComponent* VC = It->FindComponentByClass<UVolumetricCloudComponent>())
		{
			VC->SetLayerBottomAltitude(1.6f);   // km
			VC->SetLayerHeight(5.f);
			VC->SetTracingMaxDistance(60.f);
			if (UMaterialInterface* Parent = VC->GetMaterial())
			{
				CloudMat = UMaterialInstanceDynamic::Create(Parent, this);
				VC->SetMaterial(CloudMat);
				BaseCoverage = CloudMat->K2_GetScalarParameterValue(TEXT("Cloud_GlobalCoverage"));
				BaseDensity = CloudMat->K2_GetScalarParameterValue(TEXT("Cloud_GlobalDensity"));
				UE_LOG(LogTemp, Log, TEXT("Clouds: base coverage %.3f, density %.3f"), BaseCoverage, BaseDensity);
			}
			WeatherRand.Initialize(FMath::RandRange(1, 1000000));
			break;
		}
	}

	// One unbounded post-process volume: the whole land a touch brighter
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Post = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (Post)
	{
		Post->bUnbound = true;
		Post->Priority = 10.f;
		Post->Settings.bOverride_AutoExposureBias = true;
		Post->Settings.AutoExposureBias = 0.35f;
		Post->Settings.bOverride_BloomIntensity = true;   // a gentle glow round bright edges: softer, cosier light
		Post->Settings.BloomIntensity = 0.95f;
		// A painterly grade: a little less contrast, and shadows lifted a touch (barely cool) instead of falling to black
		Post->Settings.bOverride_ColorContrast = true;
		Post->Settings.ColorContrast = FVector4(0.9f, 0.9f, 0.9f, 1.f);
		Post->Settings.bOverride_ColorOffsetShadows = true;
		Post->Settings.ColorOffsetShadows = FVector4(0.012f, 0.012f, 0.016f, 0.f);
	}
}

void ADayNight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds * ClockSpeed;
	TickClouds(DeltaSeconds);
	Phase = FixedPhase >= 0.f ? FixedPhase : FMath::Fmod(Clock / (2.f * HalfLoopSeconds), 1.f);

	// A midnight sun: it circles the whole sky once a day, high at noon (phase 0) and skimming low over the
	// horizon at the bottom of its loop (phase 0.5), where the light is the old golden dusk. It never sets.
	const float A = 2.f * PI * Phase;
	SunElevation = 26.f + 15.f * FMath::Cos(A);     // 41 degrees at noon, 11 at the low point
	const float Azimuth = 35.f + 360.f * (Phase - 0.5f);   // at the low point it sits opposite the moon
	SunDir = FRotator(SunElevation, Azimuth, 0.f).Vector();

	const float E = SunElevation;
	const float High = FMath::SmoothStep(14.f, 41.f, E);   // 0 at the golden low point, 1 at noon
	Dusk = 1.f - FMath::SmoothStep(13.f, 24.f, E);        // 1 while the sun is near the horizon (stars come out)
	// The trails are drawn with their own light, so in the low sun they glowed against the darker world (owner, 2026-11-01): they
	// follow the sunlight instead, about 40% as bright and a soft gold at the low point, full white at noon
	TrailLight = FMath::Lerp(FLinearColor(0.42f, 0.34f, 0.27f), FLinearColor::White, High);

	if (ADirectionalLight* S = Sun.Get())
	{
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(S->GetLightComponent());
		S->SetActorRotation(FRotator(-E, Azimuth + 180.f, 0.f));
		const FLinearColor Ember(1.0f, 0.50f, 0.26f), Golden(1.0f, 0.62f, 0.34f), Noon(1.0f, 0.94f, 0.84f);
		const FLinearColor Low = FMath::Lerp(Ember, Golden, FMath::SmoothStep(11.f, 20.f, E));   // a little redder right down by the horizon
		L->SetIntensity(3.5f + 2.7f * High);   // 6.2 at noon: bright, but not glaring
		L->SetLightColor(FMath::Lerp(Low, Noon, High));
	}

	if (ASkyLight* SL = Sky.Get())
	{
		USkyLightComponent* C = SL->GetLightComponent();
		C->SetLightColor(FMath::Lerp(FLinearColor(0.66f, 0.66f, 0.86f), FLinearColor(0.88f, 0.88f, 0.96f), High));
		C->SetIntensity(1.7f + 0.5f * High);   // generous sky light: shade stays soft and readable, never black
		if (!bSkyRealTime)
		{
			RecaptureTimer += DeltaSeconds;
			if (RecaptureTimer > 1.f) { RecaptureTimer = 0.f; C->RecaptureSky(); }
		}
	}

	TickLook(High);
}

// The picture's look, each frame (2026-11-12): one colour grade over everything, a haze that turns far hills soft blue (the
// sky atmosphere's aerial perspective plus a cooler height fog that starts further out), a warm glow in the haze toward a low
// sun, ambient occlusion's strength and the sun's contact shadows. -OldLook / jeeb.Look 0 gives the look before it.
void ADayNight::TickLook(float High)
{
	using namespace JeebLook;
	const bool bOld = Old();
	UExponentialHeightFogComponent* FC = Fog.IsValid() ? Fog->GetComponent() : nullptr;
	const FLinearColor OldHaze = FMath::Lerp(FLinearColor(0.42f, 0.30f, 0.34f), FLinearColor(0.60f, 0.57f, 0.58f), High);
	if (bOld)
	{
		if (FC) { FC->SetFogInscatteringColor(OldHaze); FC->SetStartDistance(600.f); FC->SetDirectionalInscatteringColor(OldDirGlow); FC->SetDirectionalInscatteringExponent(OldDirExponent); FC->SetDirectionalInscatteringStartDistance(OldDirStart); }
		if (USkyAtmosphereComponent* SA = Atmosphere.Get()) { SA->SetAerialPespectiveViewDistanceScale(1.f); SA->SetAerialPerspectiveStartDepth(OldAerialStart); }
		if (Post)
		{
			FPostProcessSettings& S = Post->Settings;
			S.BloomIntensity = 0.95f;
			S.bOverride_BloomThreshold = false;
			S.ColorContrast = FVector4(0.9f, 0.9f, 0.9f, 1.f);
			S.ColorOffsetShadows = FVector4(0.012f, 0.012f, 0.016f, 0.f);
			S.bOverride_ColorSaturation = S.bOverride_ColorGainHighlights = false;
			S.bOverride_AmbientOcclusionIntensity = S.bOverride_AmbientOcclusionRadius = false;
			S.AutoExposureBias = 0.35f;
			S.bOverride_VignetteIntensity = false;
		}
		if (ContactNow != 0.f) if (ADirectionalLight* SL = Sun.Get()) if (ULightComponent* L = SL->GetLightComponent()) { L->ContactShadowLength = 0.f; L->MarkRenderStateDirty(); ContactNow = 0.f; }
		return;
	}

	if (FC)
	{
		// Noon: a light, only slightly cool grey leaning to the sky, so far hills fade toward the sky; the low sun: a warm peach-lilac that sits under the gold
		const FLinearColor NewHaze = FMath::Lerp(FLinearColor(0.60f, 0.46f, 0.40f), FLinearColor(0.54f, 0.58f, 0.66f), High);
		FC->SetFogInscatteringColor(FMath::Lerp(OldHaze, NewHaze, FMath::Clamp(HazeBlue.GetValueOnGameThread(), 0.f, 1.f)));
		FC->SetStartDistance(HazeStart.GetValueOnGameThread());
		// Looking toward the sun the haze takes on its colour, most at the low sun (the golden hour)
		FLinearColor SunCol = FLinearColor::White;
		if (ADirectionalLight* SL = Sun.Get()) SunCol = SL->GetLightComponent()->GetLightColor();
		FC->SetDirectionalInscatteringColor(SunCol * (SunGlow.GetValueOnGameThread() * FMath::Lerp(0.3f, 0.1f, High)));
		FC->SetDirectionalInscatteringExponent(6.f);
		FC->SetDirectionalInscatteringStartDistance(4000.f);
	}
	if (USkyAtmosphereComponent* SA = Atmosphere.Get())
	{
		SA->SetAerialPespectiveViewDistanceScale(Aerial.GetValueOnGameThread());
		SA->SetAerialPerspectiveStartDepth(AerialStart.GetValueOnGameThread());
	}

	if (Post)
	{
		FPostProcessSettings& S = Post->Settings;
		const float Sa = FMath::Lerp(1.f, Sat.GetValueOnGameThread(), High);
		const float Co = Contrast.GetValueOnGameThread(), Wa = Warm.GetValueOnGameThread(), Sh = ShadowCool.GetValueOnGameThread();
		S.bOverride_ColorSaturation = true;
		S.ColorSaturation = FVector4(1.f, 1.f, 1.f, Sa);
		S.ColorContrast = FVector4(Co, Co, Co, 1.f);
		S.ColorOffsetShadows = FVector4(Sh * 0.9f, Sh, Sh * 1.4f, 0.f);
		S.bOverride_ColorGainHighlights = true;
		S.ColorGainHighlights = FVector4(1.f + Wa, 1.f + 0.3f * Wa, 1.f - Wa, 1.f);
		S.BloomIntensity = Bloom.GetValueOnGameThread();
		const float Th = BloomThreshold.GetValueOnGameThread();
		S.bOverride_BloomThreshold = Th >= 0.f;
		S.BloomThreshold = Th;
		S.bOverride_AmbientOcclusionIntensity = S.bOverride_AmbientOcclusionRadius = true;
		S.AmbientOcclusionIntensity = AO.GetValueOnGameThread();
		S.AmbientOcclusionRadius = AORadius.GetValueOnGameThread();
		S.AutoExposureBias = Exposure.GetValueOnGameThread() + LowLift.GetValueOnGameThread() * (1.f - High);
		S.bOverride_VignetteIntensity = true;
		S.VignetteIntensity = Vignette.GetValueOnGameThread();
	}

	const float C = Contact.GetValueOnGameThread();
	if (C != ContactNow)
		if (ADirectionalLight* SL = Sun.Get())
			if (ULightComponent* L = SL->GetLightComponent()) { L->ContactShadowLength = C; L->MarkRenderStateDirty(); ContactNow = C; }
}

// The clouds drift slowly overhead, and the weather changes every few minutes: sometimes a clear sky, sometimes about
// 50% more cloud than the old fixed look, and sometimes something in between. Changes ease in over about a minute.
void ADayNight::TickClouds(float DeltaSeconds)
{
	if (!CloudMat) return;

	// A steady breeze moves the whole cloud pattern (the direction wanders a little over the minutes)
	const float Heading = 0.6f + 0.4f * FMath::Sin(Clock * 0.003f);
	CloudMat->SetVectorParameterValue(TEXT("WindVelocity"), FLinearColor(FMath::Cos(Heading), FMath::Sin(Heading), 0.f, 1.f));
	CloudMat->SetScalarParameterValue(TEXT("WindMultiplier"), CloudWindSpeed);

	CloudHold -= DeltaSeconds;
	if (CloudHold <= 0.f)
	{
		const float Pick = WeatherRand.FRand();
		if (Pick < 0.28f)      CloudTarget = WeatherRand.FRandRange(0.f, 0.12f);    // a clear sky
		else if (Pick < 0.64f) CloudTarget = WeatherRand.FRandRange(0.5f, 1.1f);    // scattered cloud, about the old look
		else                   CloudTarget = WeatherRand.FRandRange(1.35f, 1.5f);   // heavy: about 50% more cloud
		CloudHold = WeatherRand.FRandRange(90.f, 210.f);
	}
	CloudCover = FMath::FInterpTo(CloudCover, CloudTarget, DeltaSeconds, 0.04f);

	// Cover moves the amount of sky the clouds fill; density thins them to wisps (and nothing) toward a clear sky
	CloudMat->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), BaseCoverage + 0.5f * (CloudCover - 1.f));
	CloudMat->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), BaseDensity * FMath::Clamp(0.15f + 0.85f * CloudCover, 0.f, 1.3f));
}
