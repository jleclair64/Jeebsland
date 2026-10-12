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

float ADayNight::Phase = 0.f;
float ADayNight::SunElevation = 54.f;
float ADayNight::Dusk = 0.f;
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
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) { Fog = *It; break; }

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
	Clock += DeltaSeconds;
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

	if (AExponentialHeightFog* F = Fog.Get())
	{
		F->GetComponent()->SetFogInscatteringColor(FMath::Lerp(FLinearColor(0.42f, 0.30f, 0.34f), FLinearColor(0.60f, 0.57f, 0.58f), High));
	}
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
