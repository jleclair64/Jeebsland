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

float ADayNight::Phase = 0.f;
float ADayNight::SunElevation = 60.f;
float ADayNight::Night = 0.f;
FVector ADayNight::SunDir = FVector::UpVector;
FVector ADayNight::MoonDir = FRotator(15.f, 205.f, 0.f).Vector();
float ADayNight::FixedPhase = -1.f;

ADayNight::ADayNight()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	// A faint blue moonlight for the night. It must not light the sky itself, or night would look like day.
	MoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("MoonLight"));
	MoonLight->SetupAttachment(RootComponent);
	MoonLight->SetMobility(EComponentMobility::Movable);
	MoonLight->SetAtmosphereSunLight(false);
	MoonLight->SetLightColor(FLinearColor(0.55f, 0.66f, 1.0f));
	MoonLight->SetIntensity(0.f);
	MoonLight->SpecularScale = 0.f;
	MoonLight->SetCastShadows(true);
}

void ADayNight::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	for (TActorIterator<ADirectionalLight> It(World); It; ++It) { Sun = *It; break; }
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		Sky = *It;
		bSkyRealTime = It->GetLightComponent()->bRealTimeCapture;
		break;
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It) { Fog = *It; break; }

	// One unbounded post-process volume: exposure that lifts a little in the day and more at night (like eyes adjusting)
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Post = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (Post)
	{
		Post->bUnbound = true;
		Post->Priority = 10.f;
		Post->Settings.bOverride_AutoExposureBias = true;
	}

	MoonLight->SetWorldRotation((-MoonDir).Rotation());
}

void ADayNight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	Phase = FixedPhase >= 0.f ? FixedPhase : FMath::Fmod(Clock / (4.f * LegSeconds), 1.f);

	// The sun: high at noon, sets at about phase 0.27, rises again at about 0.73
	const float A = 2.f * PI * Phase;
	SunElevation = 55.f * FMath::Cos(A) + 8.f;
	const float Azimuth = -10.f + 45.f * FMath::Sin(A);   // it swings round to the south-west as it sets
	SunDir = FRotator(SunElevation, Azimuth, 0.f).Vector();

	const float E = SunElevation;
	const float Day = FMath::SmoothStep(-6.f, 10.f, E);            // 1 in daylight
	Night = 1.f - FMath::SmoothStep(-12.f, 3.f, E);                 // 1 once the sky is properly dark
	const float High = FMath::SmoothStep(8.f, 45.f, E);             // 0 at the golden hour, 1 at noon

	if (ADirectionalLight* S = Sun.Get())
	{
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(S->GetLightComponent());
		S->SetActorRotation(FRotator(-E, Azimuth + 180.f, 0.f));
		const float Intensity = 3.6f * FMath::SmoothStep(-3.f, 8.f, E) + 4.4f * High;   // 8 at noon, 3.6 in the golden hour
		const FLinearColor Golden(1.0f, 0.60f, 0.32f), Ember(1.0f, 0.45f, 0.20f), Noon(1.0f, 0.94f, 0.84f);
		const FLinearColor Col = FMath::Lerp(FMath::Lerp(Ember, Golden, FMath::SmoothStep(0.f, 10.f, E)), Noon, High);
		L->SetIntensity(Intensity);
		L->SetLightColor(Col);
		L->SetVisibility(E > -4.f);
	}

	MoonLight->SetIntensity(0.45f * Night);
	MoonLight->SetVisibility(Night > 0.02f);

	if (ASkyLight* SL = Sky.Get())
	{
		USkyLightComponent* C = SL->GetLightComponent();
		const FLinearColor DayTint(0.88f, 0.88f, 0.96f), DuskTint(0.70f, 0.68f, 0.88f), NightTint(0.55f, 0.62f, 0.95f);
		C->SetLightColor(FMath::Lerp(FMath::Lerp(NightTint, DuskTint, Day), DayTint, High));
		C->SetIntensity(1.25f + 0.45f * High + 0.6f * Night);
		if (!bSkyRealTime)
		{
			RecaptureTimer += DeltaSeconds;
			if (RecaptureTimer > 1.f) { RecaptureTimer = 0.f; C->RecaptureSky(); }
		}
	}

	if (AExponentialHeightFog* F = Fog.Get())
	{
		const FLinearColor DayFog(0.60f, 0.57f, 0.58f), DuskFog(0.42f, 0.30f, 0.34f), NightFog(0.03f, 0.035f, 0.06f);
		F->GetComponent()->SetFogInscatteringColor(FMath::Lerp(FMath::Lerp(NightFog, DuskFog, Day), DayFog, High));
	}

	if (Post)
	{
		Post->Settings.AutoExposureBias = 0.35f + 1.6f * Night;
	}
}
