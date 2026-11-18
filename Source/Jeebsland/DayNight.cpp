#include "DayNight.h"
#include "Palette.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
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
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

// The picture's look (2026-11-12, owner: "more professional, Breath of the Wild meets Overwatch"). Console variables, so a
// test run can try other numbers with -ExecCmds="jeeb.Look.Sat 1.1" without a build; the defaults are the chosen look.
namespace JeebLook
{
	static TAutoConsoleVariable<int32> On(TEXT("jeeb.Look"), 1, TEXT("1: the 2026-11-12 grade, haze, AO and contact shadows; 0: the look before it (test pictures)"));
	static TAutoConsoleVariable<float> Sat(TEXT("jeeb.Look.Sat"), 1.16f, TEXT("Overall colour saturation at noon (eases to 1.0 at the low sun, which is golden enough)"));
	static TAutoConsoleVariable<float> Contrast(TEXT("jeeb.Look.Contrast"), 0.98f, TEXT("Overall contrast (the old painterly grade had 0.9)"));
	static TAutoConsoleVariable<float> Warm(TEXT("jeeb.Look.Warm"), 0.075f, TEXT("Warmth in the highlights: red up and blue down by this much"));
	static TAutoConsoleVariable<float> ShadowCool(TEXT("jeeb.Look.ShadowCool"), 0.02f, TEXT("How much the shadows are lifted, a little blue (0.012 was the old lift)"));
	static TAutoConsoleVariable<float> Bloom(TEXT("jeeb.Look.Bloom"), 1.15f, TEXT("Bloom intensity (was 0.95 on every pixel)"));
	static TAutoConsoleVariable<float> BloomThreshold(TEXT("jeeb.Look.BloomThreshold"), -1.f, TEXT("Bloom threshold: -1 every pixel blooms a little; above 0 only what is brighter blooms"));
	static TAutoConsoleVariable<float> Aerial(TEXT("jeeb.Look.Aerial"), 18.3529f, TEXT("Sky atmosphere aerial perspective distance scale: far hills fade toward the sky's blue (engine default 1)"));
	static TAutoConsoleVariable<float> HazeBlue(TEXT("jeeb.Look.HazeBlue"), 0.85f, TEXT("0: the old warm grey height fog; 1: a soft blue-grey haze at noon, a warm peach-lilac at the low sun"));
	static TAutoConsoleVariable<float> HazeStart(TEXT("jeeb.Look.HazeStart"), 1238.24f, TEXT("Height fog start distance in cm (was 600): the town around you stays clear"));
	static TAutoConsoleVariable<float> SunGlow(TEXT("jeeb.Look.SunGlow"), 1.6303f, TEXT("The warm glow in the haze toward the sun (directional inscattering), strongest at the low sun"));
	static TAutoConsoleVariable<float> AO(TEXT("jeeb.Look.AO"), 0.6f, TEXT("Ambient occlusion strength (where it is on: r.AmbientOcclusionLevels, set by ApplyGraphics)"));
	static TAutoConsoleVariable<float> AORadius(TEXT("jeeb.Look.AORadius"), 100.f, TEXT("Ambient occlusion radius in cm"));
	static TAutoConsoleVariable<float> ShadowFar(TEXT("jeeb.Shadow.Far"), 0.f, TEXT("How far the sun's one far shadow cascade reaches, metres (0 none). Only what casts far shadows draws into it: the forest's stand-in crowns (jeeb.Forest.FarShade), so far trees and the ground under them stay shaded. Set by the graphics setting (2026-11-16). Live; looks only."));
	static TAutoConsoleVariable<float> SunStep(TEXT("jeeb.Sun.Step"), 0.f, TEXT("The sun's direction moves in steps of this many degrees (0: every frame), so cached sun shadows stay valid between steps. Live; looks only."));
	static TAutoConsoleVariable<float> ShadowDF(TEXT("jeeb.Shadow.DF"), 0.f, TEXT("Distance field sun shadows past the ordinary ones out to this many metres (0 off). Needs mesh distance fields (r.GenerateMeshDistanceFields, off in this project). Live; looks only."));
	static TAutoConsoleVariable<float> Contact(TEXT("jeeb.Look.Contact"), 0.f, TEXT("The sun's screen-space contact shadow length (share of the screen; 0 off: at 0.03 it cost 0.07 ms and barely showed, 2026-11-13)"));
	static TAutoConsoleVariable<float> AerialStart(TEXT("jeeb.Look.AerialStart"), 0.3f, TEXT("Where the aerial perspective haze begins, in km (engine default 0.1)"));
	static TAutoConsoleVariable<float> Exposure(TEXT("jeeb.Look.Exposure"), 0.24f, TEXT("Exposure bias at noon, in stops (0.35 until 2026-11-13, owner: \"the whole game lower by 15%\": about -0.19 stops gives 15% less light on screen, through the tone curve)"));
	static TAutoConsoleVariable<float> LowLift(TEXT("jeeb.Look.LowLift"), 1.4f, TEXT("Extra exposure at the low sun, in stops, so the golden hour glows instead of going murky"));
	static TAutoConsoleVariable<float> Vignette(TEXT("jeeb.Look.Vignette"), 0.3f, TEXT("Darkening toward the picture's corners (engine default 0.4)"));
	// (2026-11-14, the owner approved TownLab's new look: golden haze, stronger aerial perspective, lifted shadows, a warmer, softer
	// grade and gentle light shafts round the sun. Its numbers became the defaults above; the look before: Sat 1.1, Contrast 1, Warm
	// 0.04, ShadowCool 0.012, Bloom 0.8, Aerial 16, HazeBlue 1, HazeStart 2500, SunGlow 1, AerialStart 0.35, Exposure 0.16, LowLift
	// 0.8, Vignette 0.4, Shafts 0)
	// (2026-11-15, owner: the LOOK preset Save1_20261115 became the defaults: haze from 12.4 m (was 22), thin low clouds (density
	// 0.5, was 3; bottom 0.8 km, was 1.6), glossy rippled water (Ripple 1, Rough 0.02, no noon glow), grass tint 1.01 0.83 1.0 in
	// Palette.ini; the rest were within a hair of the old defaults. Its held sun, 0.49, was left out: the clock still moves the sun.)
	static TAutoConsoleVariable<int32> Shafts(TEXT("jeeb.Look.Shafts"), 1, TEXT("Gentle light shafts round the sun (bloom shafts only, no occlusion pass): 1 on, 0 off"));
	// (2026-11-15, owner: "a slider for me to move the sun in the sky"; the LOOK tab's SKY + HAZE group. This machine's view only.)
	static TAutoConsoleVariable<float> SunPhase(TEXT("jeeb.Sun.Phase"), -1.f, TEXT("Holds the sun at a point of its daily loop: 0 noon (41 degrees up), 0.26 sunset glow (5 up), 0.29 sunset, 0.34 10 degrees down, 0.5 midnight (25 down), 0.71 sunrise; negative: the clock moves it"));
	// (2026-11-15, owner: the silver armour "much too flat/grey ... reflective like a mirror": the sun's own shine is off for the whole world, so plate had no highlight at all)
	static TAutoConsoleVariable<float> SkyGround(TEXT("jeeb.Sky.Ground"), 0.f, TEXT("The sky light's colour from below the horizon, a meadow green-brown at this strength (0: the sky atmosphere's own, nearly black, as before 2026-11-15): what polished armour mirrors facing down"));
	static TAutoConsoleVariable<float> ArmourGlint(TEXT("jeeb.Armour.Glint"), 0.1513f, TEXT("The sun's glint on the knights' plate armour (shine only, lighting channel 1): 0 none, 1 as bright as the sun"));

	// The HIGH volumetric clouds (2026-11-13, owner: "clouds like this without taking up too much processing power")
	static TAutoConsoleVariable<float> CloudSamples(TEXT("jeeb.Cloud.Samples"), 0.6f, TEXT("Volumetric cloud ray-march samples (ViewSampleCountScale, engine default 1): lower is cheaper and noisier"));
	static TAutoConsoleVariable<float> CloudShadowSamples(TEXT("jeeb.Cloud.ShadowSamples"), 0.5f, TEXT("Samples toward the sun inside the clouds (ShadowViewSampleCountScale, engine default 1)"));
	static TAutoConsoleVariable<float> CloudTrace(TEXT("jeeb.Cloud.Trace"), 40.f, TEXT("How far the clouds are traced, km (was 60; 2026-11-15 the owner approved the cheaper clouds: Samples 0.6, ShadowSamples 0.5, Trace 40, were 1, 1, 60)"));
	static TAutoConsoleVariable<float> CloudBottom(TEXT("jeeb.Cloud.Bottom"), 0.8f, TEXT("The cloud layer's bottom, km"));
	static TAutoConsoleVariable<float> CloudHeight(TEXT("jeeb.Cloud.Height"), 5.0084f, TEXT("The cloud layer's depth, km: deep layers tower up in heaps"));
	static TAutoConsoleVariable<float> CloudUnder(TEXT("jeeb.Cloud.Under"), 0.2017f, TEXT("How much sky light the undersides keep (SkyLightCloudBottomOcclusion, engine default 0.5; lower is darker, shapelier undersides)"));
	static TAutoConsoleVariable<float> CloudBounce(TEXT("jeeb.Cloud.Bounce"), 0.4622f, TEXT("Light bounced up from the ground into the undersides (GroundAlbedo grey level; warm-tinted above 0.3)"));
	static TAutoConsoleVariable<float> CloudScale(TEXT("jeeb.Cloud.Scale"), 1.2983f, TEXT("Size of the cloud pattern (multiplies the material's Layout_CloudGlobalScale): above 1 bigger heaps"));
	static TAutoConsoleVariable<float> CloudFill(TEXT("jeeb.Cloud.Fill"), 0.9454f, TEXT("Multiplies the material's FillScatterIntensity: light filling the shaded side"));
	static TAutoConsoleVariable<float> CloudWarm(TEXT("jeeb.Cloud.Warm"), 0.5084f, TEXT("Warmth of the sunlight scattered in the clouds (the sun's cloud luminance tint: 0 white, 1 golden)"));
	static TAutoConsoleVariable<float> CloudDensity(TEXT("jeeb.Cloud.Density"), 0.5f, TEXT("Multiplies the cloud material's density: higher reads as solid heaps with crisp edges and darker undersides, lower as mist"));
	static TAutoConsoleVariable<FString> CloudSet(TEXT("jeeb.Cloud.Set"), TEXT(""), TEXT("Test pictures: any of the cloud material's scalars, Name=Value;Name=Value (the names are logged as JEEBCLOUDPARAM at the start)"));
	static TAutoConsoleVariable<float> CloudCoverFix(TEXT("jeeb.Cloud.Cover"), -1.f, TEXT("Test pictures: hold the cloud cover (0 clear, 1 scattered, 1.5 heavy); -1 the weather decides"));
	static TAutoConsoleVariable<float> LowCloudBright(TEXT("jeeb.Cloud.LowBright"), 1.7647f, TEXT("LOW's painted cloud ring: brightness at noon (2.3 until 2026-11-14: the shaded bases burned out to white)"));
	static TAutoConsoleVariable<float> LowCloudHaze(TEXT("jeeb.Cloud.LowHaze"), 0.25f, TEXT("LOW's painted cloud ring: share of the haze's colour mixed into it (it takes no fog itself)"));
	static TAutoConsoleVariable<float> LowCloudDusk(TEXT("jeeb.Cloud.LowDusk"), 0.18f, TEXT("LOW's painted cloud ring at the low sun: share of its noon brightness"));

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

// The night (2026-11-17, owner: "not pitch black when the sun goes down"). Like most stylised games (Zelda, Sea of Thieves,
// Fortnite) the night is a readable, moonlit blue rather than real darkness: a cool moonlight with its own shadows, a raised
// sky light, more exposure and a gentle blue, less colourful grade, all eased in with ADayNight::Night. Live; looks only.
namespace JeebNight
{
	static TAutoConsoleVariable<float> SunLow(TEXT("jeeb.Sun.Low"), -25.f, TEXT("The sun's height at midnight, degrees (negative: below the horizon). Noon stays 41; the loop grows, it is not shifted (11 until 2026-11-17: it never set)"));
	static TAutoConsoleVariable<float> Moon(TEXT("jeeb.Night.Moon"), 0.5f, TEXT("The moonlight's strength at full night (the sun is 6.2 at noon). It also lights the night sky and clouds"));
	static TAutoConsoleVariable<FString> MoonColor(TEXT("jeeb.Night.MoonColor"), TEXT("0.60 0.72 1.00"), TEXT("The moonlight's colour, \"r g b\" (a cool blue)"));
	static TAutoConsoleVariable<int32> MoonShadows(TEXT("jeeb.Night.MoonShadows"), 1, TEXT("1: the moonlight casts shadows (it takes over the sun's, never both at once); 0 none"));
	static TAutoConsoleVariable<float> Ambient(TEXT("jeeb.Night.Ambient"), 2.f, TEXT("The sky light (the soft fill in the shade) times this at full night: lifts the night's floor so nothing goes black"));
	static TAutoConsoleVariable<float> Exposure(TEXT("jeeb.Night.Exposure"), 0.5f, TEXT("Extra exposure at full night, in stops, on top of jeeb.Look.Exposure and LowLift (the eye adapting to the dark)"));
	static TAutoConsoleVariable<float> Tint(TEXT("jeeb.Night.Tint"), 0.5f, TEXT("The night's blue grade: 0 none, 1 strong (blue up, red down, colours a little paler: the eye's night vision)"));
	static TAutoConsoleVariable<float> ShadowLift(TEXT("jeeb.Night.ShadowLift"), 0.012f, TEXT("A blue lift in the darkest shadows at full night (added to jeeb.Look.ShadowCool's)"));
	static TAutoConsoleVariable<float> Twilight(TEXT("jeeb.Night.Twilight"), 0.6f, TEXT("Extra exposure in stops right round sunset and sunrise (most with the sun on the horizon, none 7 degrees either side): the short handover, when neither the sun nor the moon lights the ground much, never sinks into a murky dip"));
	static TAutoConsoleVariable<float> Sky(TEXT("jeeb.Night.Sky"), 0.5f, TEXT("The night sky's brightness (the sky atmosphere lit by the moon) times this at full night (1: as the moonlight lights it, a dim day-blue; lower is a deeper night blue where the stars show)"));

	FLinearColor Rgb(const FString& S, const FLinearColor& Def)
	{
		TArray<FString> P; S.ParseIntoArrayWS(P);
		return P.Num() >= 3 ? FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])) : Def;
	}
}

float ADayNight::Phase = 0.f;
float ADayNight::SunElevation = 54.f;
float ADayNight::Dusk = 0.f;
float ADayNight::Night = 0.f;
FLinearColor ADayNight::TrailLight = FLinearColor::White;
FVector ADayNight::SunDir = FVector::UpVector;
FVector ADayNight::MoonDir = FRotator(30.f, 205.f, 0.f).Vector();   // (15 degrees up until 2026-11-17; higher now it lights the night)
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
			L->ForwardShadingPriority = 1;   // (the one for water, glass and fog, ahead of the armour's glint light below)
			L->MarkRenderStateDirty();
			SunBaseDiffuse = L->DiffuseScale;
			SunBaseSpecular = L->SpecularScale;
			UE_LOG(LogTemp, Log, TEXT("JEEBSHADOW sun: mobility %d, shadow distance %.0f m (times r.Shadow.DistanceScale), %d cascades, distribution %.2f, far cascades %d to %.0f m"),
				int32(L->Mobility.GetValue()), L->DynamicShadowDistanceMovableLight / 100.f, L->DynamicShadowCascades, L->CascadeDistributionExponent, L->FarShadowCascadeCount, L->FarShadowDistance / 100.f);
		}
		break;
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		Sky = *It;
		bSkyRealTime = It->GetLightComponent()->bRealTimeCapture;
		bSkyLowerBlack = It->GetLightComponent()->bLowerHemisphereIsBlack;
		UE_LOG(LogTemp, Log, TEXT("JEEBSKY real-time capture %d, lower hemisphere black %d, source %d"), bSkyRealTime ? 1 : 0, It->GetLightComponent()->bLowerHemisphereIsBlack ? 1 : 0, int32(It->GetLightComponent()->SourceType));
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
			CloudComp = VC;   // (its layer, tracing and lighting: ApplyCloudLook, from the jeeb.Cloud.* variables)
			if (UMaterialInterface* Parent = VC->GetMaterial())
			{
				CloudMat = UMaterialInstanceDynamic::Create(Parent, this);
				VC->SetMaterial(CloudMat);
				BaseCoverage = CloudMat->K2_GetScalarParameterValue(TEXT("Cloud_GlobalCoverage"));
				BaseDensity = CloudMat->K2_GetScalarParameterValue(TEXT("Cloud_GlobalDensity"));
				float V = 0.f;
				if (CloudMat->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Layout_CloudGlobalScale")), V)) CloudBaseScale = V;
				if (CloudMat->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("FillScatterIntensity")), V)) CloudBaseFill = V;
				UE_LOG(LogTemp, Log, TEXT("Clouds: base coverage %.3f, density %.3f, pattern scale %.3f, fill scatter %.3f"), BaseCoverage, BaseDensity, CloudBaseScale, CloudBaseFill);
#if !UE_BUILD_SHIPPING
				TArray<FMaterialParameterInfo> Infos; TArray<FGuid> Ids;
				CloudMat->GetAllScalarParameterInfo(Infos, Ids);
				for (const FMaterialParameterInfo& I : Infos) { float PV = 0.f; CloudMat->GetScalarParameterValue(I, PV); UE_LOG(LogTemp, Log, TEXT("JEEBCLOUDPARAM %s = %g"), *I.Name.ToString(), PV); }
#endif
			}
			ApplyCloudLook();
			WeatherRand.Initialize(FMath::RandRange(1, 1000000));
			break;
		}
	}

	if (World->GetNetMode() != NM_DedicatedServer) BuildLowClouds();

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
	// (2026-11-15) the server's clock, so every player online sees the sun in the same place (each machine kept its own until then)
	const AGameStateBase* GS = GetWorld()->GetGameState();
	Clock = GS ? float(GS->GetServerWorldTimeSeconds()) * ClockSpeed : Clock + DeltaSeconds * ClockSpeed;
	TickClouds(DeltaSeconds);
	const float HeldPhase = JeebLook::SunPhase.GetValueOnGameThread();
	Phase = HeldPhase >= 0.f ? FMath::Fmod(HeldPhase, 1.f) : FixedPhase >= 0.f ? FixedPhase : FMath::Fmod(Clock / (2.f * HalfLoopSeconds), 1.f);

	// The sun circles the whole sky once a loop, high at noon (phase 0) and lowest at midnight (phase 0.5). 2026-11-17 (owner:
	// "the lowest about 25 degrees below the horizon ... the sun loop to get bigger instead of shifting"): noon stays 41 degrees and
	// the loop's swing grows (15 to 33 degrees), so it sets at about phase 0.29 and rises at 0.71: 11.6 minutes of day and 8.4 of
	// night in the 20-minute loop. Until then it was a midnight sun (26 +- 15: 41 to 11 degrees) that never set.
	const float A = 2.f * PI * Phase;
	const float Noon = 41.f, Low = FMath::Clamp(JeebNight::SunLow.GetValueOnGameThread(), -60.f, 30.f);
	SunElevation = 0.5f * (Noon + Low) + 0.5f * (Noon - Low) * FMath::Cos(A);   // 8 + 33 cos: 41 at noon, -25 at midnight
	const float Azimuth = 35.f + 360.f * (Phase - 0.5f);   // at the low point it sits opposite the moon
	SunDir = FRotator(SunElevation, Azimuth, 0.f).Vector();

	const float E = SunElevation;
	const float High = FMath::SmoothStep(14.f, 41.f, E);   // 0 at the golden low sun (and all night), 1 at noon
	Dusk = 1.f - FMath::SmoothStep(13.f, 24.f, E);        // 1 while the sun is near the horizon or below it (lamps, lit windows)
	// The handover (2026-11-17): the sun's light on the ground fades out as it crosses the horizon (gone 1 degree down), and only
	// then does the moonlight fade in (full by 8 degrees down) and take over the shadows: no pop, never two shadow casters.
	// The sun itself stays the sky atmosphere's light until 16 degrees down, so the twilight glow in the sky lasts.
	const float SunGround = FMath::SmoothStep(-1.f, 4.f, E);
	const float SunSky = FMath::SmoothStep(-16.f, -8.f, E);
	const float MoonW = 1.f - FMath::SmoothStep(-8.f, -1.f, E);
	Night = 1.f - FMath::SmoothStep(-6.f, 8.f, E);        // the night's exposure and ambient: a little from the golden hour (so sunset never sinks into a murky dip), full by 6 down
	// The trails are drawn with their own light, so in the low sun they glowed against the darker world (owner, 2026-11-01): they
	// follow the sunlight instead, about 40% as bright and a soft gold at the low point, full white at noon
	TrailLight = FMath::Lerp(FMath::Lerp(FLinearColor(0.42f, 0.34f, 0.27f), FLinearColor(0.30f, 0.34f, 0.44f), Night), FLinearColor::White, High);   // (a moonlit blue-grey at night)

	TickMoon(MoonW);
	if (ADirectionalLight* S = Sun.Get())
	{
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(S->GetLightComponent());
		// (2026-11-16) one far shadow cascade past the ordinary ones, drawn only by what casts far shadows (the forest's stand-in crowns)
		const float FarM = FMath::Max(0.f, JeebLook::ShadowFar.GetValueOnGameThread());
		if (L && FarM != FarNow)
		{
			FarNow = FarM;
			L->FarShadowCascadeCount = FarM > 0.f ? 1 : 0;
			L->FarShadowDistance = FMath::Max(FarM, 1.f) * 100.f;
			L->MarkRenderStateDirty();
			UE_LOG(LogTemp, Log, TEXT("JEEBSHADOW far cascade %s, %.0f m"), FarM > 0.f ? TEXT("on") : TEXT("off"), FarM);
		}
		// (jeeb.Sun.Step, 2026-11-16: the sun's direction moves in small steps instead of every frame, so cached shadows (CSM caching,
		// virtual shadow maps) stay valid between steps; its colour and the sky still change smoothly)
		const float Step = FMath::Max(0.f, JeebLook::SunStep.GetValueOnGameThread());
		const float Es = Step > 0.f ? FMath::GridSnap(E, Step) : E, As = Step > 0.f ? FMath::GridSnap(Azimuth, Step) : Azimuth;
		if (Step <= 0.f || !S->GetActorRotation().Equals(FRotator(-Es, As + 180.f, 0.f), 0.001f)) S->SetActorRotation(FRotator(-Es, As + 180.f, 0.f));
		const float DfM = FMath::Max(0.f, JeebLook::ShadowDF.GetValueOnGameThread());
		if (L && DfM != DfNow)
		{
			DfNow = DfM;
			L->bUseRayTracedDistanceFieldShadows = DfM > 0.f;
			L->DistanceFieldShadowDistance = FMath::Max(DfM, 1.f) * 100.f;
			L->MarkRenderStateDirty();
			UE_LOG(LogTemp, Log, TEXT("JEEBSHADOW distance field shadows %s, %.0f m"), DfM > 0.f ? TEXT("on") : TEXT("off"), DfM);
		}
		const FLinearColor Ember(1.0f, 0.50f, 0.26f), Golden(1.0f, 0.62f, 0.34f), NoonCol(1.0f, 0.94f, 0.84f);
		const FLinearColor LowCol = FMath::Lerp(Ember, Golden, FMath::SmoothStep(11.f, 20.f, E));   // a little redder right down by the horizon
		L->SetIntensity((3.5f + 2.7f * High) * SunSky);   // 6.2 at noon: bright, but not glaring (fades out in the late twilight, 8 to 16 degrees down)
		L->SetLightColor(FMath::Lerp(LowCol, NoonCol, High));
		// (2026-11-17) below the horizon the sun lights only the sky: its light on the ground is faded through the horizon, and its
		// shadows stop once that is out (the moon's start there); in deep night the light is hidden altogether (no light pass)
		// (in steps of 1/20: each change rebuilds the light's render state, so it happens about 20 times a sunset, not every frame)
		const float Sg = FMath::RoundToFloat(SunGround * 20.f) / 20.f;
		L->SetDiffuseScale(SunBaseDiffuse * Sg);
		L->SetSpecularScale(SunBaseSpecular * Sg);
		const int32 SunShadow = Sg > 0.f ? 1 : 0;
		if (SunShadow != SunShadowNow) { SunShadowNow = SunShadow; L->SetCastShadows(SunShadow != 0); }
		const int32 SunShown = SunSky > 0.f ? 1 : 0;
		if (SunShown != SunShownNow) { SunShownNow = SunShown; L->SetVisibility(SunShown != 0); }
		// The glint follows the sun exactly; only its shine reaches the plate (DiffuseScale 0), so the armour's look in the shade is unchanged
		const float G = JeebLook::ArmourGlint.GetValueOnGameThread();
		if (!Glint && G > 0.f)
		{
			Glint = NewObject<UDirectionalLightComponent>(this, TEXT("ArmourGlint"));
			Glint->SetMobility(EComponentMobility::Movable);
			Glint->SetupAttachment(GetRootComponent());
			Glint->SetUsingAbsoluteRotation(true);
			Glint->SetCastShadows(false);
			Glint->SetAtmosphereSunLight(false);
			Glint->bAffectTranslucentLighting = false;
			Glint->SetVolumetricScatteringIntensity(0.f);
			Glint->LightSourceAngle = 0.6f;   // (the real sun's size: a small crisp highlight; the world's sun is 4 degrees for soft shadows)
			Glint->SetDiffuseScale(0.f);
			Glint->SetLightingChannels(false, true, false);
			Glint->RegisterComponent();
		}
		if (Glint)
		{
			// (2026-11-17) at night it follows the moonlight instead (the sun's glint is gone with its light on the ground)
			const bool bMoonGlint = MoonW > 0.f && Moonlight;
			Glint->SetVisibility(G > 0.f);
			Glint->SetWorldRotation(bMoonGlint ? Moonlight->GetComponentRotation() : S->GetActorRotation());
			Glint->SetIntensity(bMoonGlint ? Moonlight->Intensity : (3.5f + 2.7f * High) * SunGround);
			Glint->SetLightColor(bMoonGlint ? Moonlight->GetLightColor() : L->GetLightColor());
			Glint->SetSpecularScale(G);
		}
	}

	if (ASkyLight* SL = Sky.Get())
	{
		USkyLightComponent* C = SL->GetLightComponent();
		const FLinearColor SkyCol = FMath::Lerp(FLinearColor(0.66f, 0.66f, 0.86f), FLinearColor(0.88f, 0.88f, 0.96f), High);
		C->SetLightColor(FMath::Lerp(SkyCol, FLinearColor(0.62f, 0.72f, 1.0f), Night * Night));   // (a moonlit blue at night)
		// generous sky light: shade stays soft and readable, never black; at night times jeeb.Night.Ambient (the night's floor)
		C->SetIntensity((1.7f + 0.5f * High) * FMath::Lerp(1.f, FMath::Max(0.f, JeebNight::Ambient.GetValueOnGameThread()), Night));
		{   // (2026-11-15) what the sky light gives from below the horizon: black left polished plate mirroring black wherever it faced down
			// (the map's sky light takes the sky atmosphere's own planet ground below the horizon, nearly black: jeeb.Sky.Ground > 0 puts a meadow colour there instead)
			const float Gd = FMath::Max(JeebLook::SkyGround.GetValueOnGameThread(), 0.f);
			if (Gd != SkyGroundNow)
			{
				SkyGroundNow = Gd;
				C->bLowerHemisphereIsBlack = Gd > 0.f ? true : bSkyLowerBlack;
				C->SetLowerHemisphereColor(FLinearColor(0.11f, 0.13f, 0.07f) * Gd);
				C->MarkRenderStateDirty();
				C->RecaptureSky();
			}
		}
		if (!bSkyRealTime)
		{
			RecaptureTimer += DeltaSeconds;
			if (RecaptureTimer > 1.f) { RecaptureTimer = 0.f; C->RecaptureSky(); }
		}
	}

	TickLook(High);
	ApplyCloudLook();
	TickLowClouds(High);
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
			S.bOverride_ColorSaturation = S.bOverride_ColorGainHighlights = S.bOverride_ColorGain = false;
			S.bOverride_AmbientOcclusionIntensity = S.bOverride_AmbientOcclusionRadius = false;
			S.AutoExposureBias = 0.35f;
			S.bOverride_VignetteIntensity = false;
		}
		if (ContactNow != 0.f) if (ADirectionalLight* SL = Sun.Get()) if (ULightComponent* L = SL->GetLightComponent()) { L->ContactShadowLength = 0.f; L->MarkRenderStateDirty(); ContactNow = 0.f; }
		SetShafts(0);
		return;
	}

	if (FC)
	{
		// Noon: a light, only slightly cool grey leaning to the sky, so far hills fade toward the sky; the low sun: a warm peach-lilac that sits under the gold
		static const FName HazeLowKey(TEXT("HazeLow")), HazeNoonKey(TEXT("HazeNoon"));
		const FLinearColor NewHaze = FMath::Lerp(JeebPalette::Graded(HazeLowKey), JeebPalette::Graded(HazeNoonKey), High);   // (the world palette's, Palette.ini: 0.60, 0.46, 0.40 and 0.54, 0.58, 0.66)
		const FLinearColor DayHaze = FMath::Lerp(OldHaze, NewHaze, FMath::Clamp(HazeBlue.GetValueOnGameThread(), 0.f, 1.f));
		// (2026-11-17) the haze glows by itself, so at night it is a dim moonlit blue: the same on screen whatever the exposure (it is divided by it)
		const float NightBias = Exposure.GetValueOnGameThread() + LowLift.GetValueOnGameThread() + JeebNight::Exposure.GetValueOnGameThread();
		const FLinearColor NightHaze = FLinearColor(0.16f, 0.21f, 0.34f) * FMath::Exp2(-NightBias);
		FC->SetFogInscatteringColor(FMath::Lerp(DayHaze, NightHaze, Night * Night));   // (Night squared: the golden hour keeps its colours)
		FC->SetStartDistance(HazeStart.GetValueOnGameThread());
		// Looking toward the sun the haze takes on its colour, most at the low sun (the golden hour)
		FLinearColor SunCol = FLinearColor::White;
		if (ADirectionalLight* SL = Sun.Get()) SunCol = SL->GetLightComponent()->GetLightColor();
		FC->SetDirectionalInscatteringColor(SunCol * (SunGlow.GetValueOnGameThread() * FMath::Lerp(0.3f, 0.1f, High) * (1.f - Night * Night)));   // (none at night)
		FC->SetDirectionalInscatteringExponent(6.f);
		FC->SetDirectionalInscatteringStartDistance(4000.f);
	}
	if (USkyAtmosphereComponent* SA = Atmosphere.Get())
	{
		SA->SetAerialPespectiveViewDistanceScale(Aerial.GetValueOnGameThread());
		SA->SetAerialPerspectiveStartDepth(AerialStart.GetValueOnGameThread());
		// (jeeb.Night.Sky: the moonlit night sky brighter or darker; in steps of 0.02, sent only when it changes, 1 = untouched)
		const float SkyF = FMath::RoundToFloat(FMath::Lerp(1.f, FMath::Max(0.f, JeebNight::Sky.GetValueOnGameThread()), Night) * 50.f) / 50.f;
		if (!FMath::IsNearlyEqual(SA->SkyLuminanceFactor.R, SkyF, 0.001f)) SA->SetSkyLuminanceFactor(FLinearColor(SkyF, SkyF, SkyF));
	}

	if (Post)
	{
		FPostProcessSettings& S = Post->Settings;
		// (2026-11-17) the night's grade, eased in with Night: a blue shift and paler colours (jeeb.Night.Tint, the eye's night
		// vision, as in Ghost of Tsushima's Purkinje shift), no warm highlights, and a blue lift in the darkest shadows
		const float Nt = Night * Night * FMath::Clamp(JeebNight::Tint.GetValueOnGameThread(), 0.f, 2.f);
		const float Sa = FMath::Lerp(1.f, Sat.GetValueOnGameThread(), High) * (1.f - 0.3f * Nt);
		const float Co = Contrast.GetValueOnGameThread(), Wa = Warm.GetValueOnGameThread() * (1.f - Night * Night), Sh = ShadowCool.GetValueOnGameThread();
		const float NL = Night * FMath::Max(0.f, JeebNight::ShadowLift.GetValueOnGameThread());
		S.bOverride_ColorSaturation = true;
		S.ColorSaturation = FVector4(1.f, 1.f, 1.f, Sa);
		S.ColorContrast = FVector4(Co, Co, Co, 1.f);
		S.ColorOffsetShadows = FVector4(Sh * 0.9f + NL * 0.6f, Sh + NL * 0.8f, Sh * 1.4f + NL * 1.4f, 0.f);
		S.bOverride_ColorGainHighlights = true;
		S.ColorGainHighlights = FVector4(1.f + Wa, 1.f + 0.3f * Wa, 1.f - Wa, 1.f);
		S.bOverride_ColorGain = true;
		S.ColorGain = FVector4(1.f - 0.28f * Nt, 1.f - 0.08f * Nt, 1.f + 0.18f * Nt, 1.f);
		S.BloomIntensity = Bloom.GetValueOnGameThread();
		const float Th = BloomThreshold.GetValueOnGameThread();
		S.bOverride_BloomThreshold = Th >= 0.f;
		S.BloomThreshold = Th;
		S.bOverride_AmbientOcclusionIntensity = S.bOverride_AmbientOcclusionRadius = true;
		S.AmbientOcclusionIntensity = AO.GetValueOnGameThread();
		S.AmbientOcclusionRadius = AORadius.GetValueOnGameThread();
		S.AutoExposureBias = Exposure.GetValueOnGameThread() + LowLift.GetValueOnGameThread() * (1.f - High) + JeebNight::Exposure.GetValueOnGameThread() * Night
			+ JeebNight::Twilight.GetValueOnGameThread() * (1.f - FMath::SmoothStep(0.f, 7.f, FMath::Abs(SunElevation)));
		S.bOverride_VignetteIntensity = true;
		S.VignetteIntensity = Vignette.GetValueOnGameThread();
	}

	SetShafts(Shafts.GetValueOnGameThread() != 0 ? 1 : 0);
	const float C = Contact.GetValueOnGameThread();
	if (C != ContactNow)
		if (ADirectionalLight* SL = Sun.Get())
			if (ULightComponent* L = SL->GetLightComponent()) { L->ContactShadowLength = C; L->MarkRenderStateDirty(); ContactNow = C; }
}

// The moonlight (2026-11-17): MoonW is 0 while the sun lights the ground and eases to 1 by the time it is 8 degrees down. It
// shines from the moon (MoonDir), a cool blue, is the sky atmosphere's second light (the night sky and clouds glow faintly
// with it, and the sky light gathers that), and takes over the sun's shadows: the sun's stop exactly where the moon's start,
// 1 degree below the horizon, where both lights are dark. Hidden by day, so it costs nothing then.
void ADayNight::TickMoon(float MoonW)
{
	const bool bOn = MoonW > 0.f && !JeebLook::Old() && GetWorld()->GetNetMode() != NM_DedicatedServer;
	if (!Moonlight && bOn)
	{
		Moonlight = NewObject<UDirectionalLightComponent>(this, TEXT("Moonlight"));
		Moonlight->SetMobility(EComponentMobility::Movable);
		Moonlight->SetupAttachment(GetRootComponent());
		Moonlight->SetUsingAbsoluteRotation(true);
		Moonlight->SetAtmosphereSunLight(true);
		Moonlight->SetAtmosphereSunLightIndex(1);
		Moonlight->SetAtmosphereSunDiskColorScale(FLinearColor::Black);   // (no second sun disc: the moon is AMoon's picture)
		Moonlight->LightSourceAngle = 1.5f;   // (a little crisper than the sun's soft 4 degrees)
		Moonlight->ForwardShadingPriority = 2;   // (water, glass and fog take the moon at night; it is hidden by day)
		Moonlight->SetVolumetricScatteringIntensity(0.f);
		if (ADirectionalLight* S = Sun.Get())
			if (const UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(S->GetLightComponent()))
			{   // the same shadow reach and cascades as the sun's (no far cascade: the forest's far shadows are a daytime touch)
				Moonlight->DynamicShadowDistanceMovableLight = L->DynamicShadowDistanceMovableLight;
				Moonlight->DynamicShadowCascades = L->DynamicShadowCascades;
				Moonlight->CascadeDistributionExponent = L->CascadeDistributionExponent;
				Moonlight->CascadeTransitionFraction = L->CascadeTransitionFraction;
				Moonlight->ShadowDistanceFadeoutFraction = L->ShadowDistanceFadeoutFraction;
				Moonlight->ShadowBias = L->ShadowBias;
				Moonlight->ShadowSlopeBias = L->ShadowSlopeBias;
				Moonlight->LightingChannels = L->LightingChannels;
			}
		Moonlight->SetCastShadows(false);
		Moonlight->SetWorldRotation((-MoonDir).Rotation());
		Moonlight->RegisterComponent();
		UE_LOG(LogTemp, Log, TEXT("JEEBNIGHT moonlight made: shadow distance %.0f m, %d cascades"), Moonlight->DynamicShadowDistanceMovableLight / 100.f, Moonlight->DynamicShadowCascades);
	}
	if (!Moonlight) return;
	if (Moonlight->IsVisible() != bOn) Moonlight->SetVisibility(bOn);
	if (!bOn) return;
	Moonlight->SetIntensity(FMath::Max(0.f, JeebNight::Moon.GetValueOnGameThread()) * MoonW);
	Moonlight->SetLightColor(JeebNight::Rgb(JeebNight::MoonColor.GetValueOnGameThread(), FLinearColor(0.60f, 0.72f, 1.f)));
	const int32 Sh = JeebNight::MoonShadows.GetValueOnGameThread() != 0 ? 1 : 0;
	if (Sh != MoonShadowNow) { MoonShadowNow = Sh; Moonlight->SetCastShadows(Sh != 0); }
}

// Gentle light shafts round the sun (TownLab's new look, 2026-11-14): bloom shafts only; the occlusion pass is left off, it costs and darkens
void ADayNight::SetShafts(int32 On)
{
	if (On == ShaftsNow) return;
	ADirectionalLight* SL = Sun.Get();
	UDirectionalLightComponent* L = SL ? Cast<UDirectionalLightComponent>(SL->GetLightComponent()) : nullptr;
	if (!L) return;
	L->bEnableLightShaftBloom = On != 0;
	L->BloomScale = 0.12f;
	L->BloomThreshold = 0.35f;
	L->BloomMaxBrightness = 4.f;
	L->BloomTint = FColor(255, 214, 160);
	L->bEnableLightShaftOcclusion = false;
	L->MarkRenderStateDirty();
	ShaftsNow = On;
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
	if (JeebLook::CloudCoverFix.GetValueOnGameThread() >= 0.f) CloudCover = JeebLook::CloudCoverFix.GetValueOnGameThread();   // (test pictures)

	// Cover moves the amount of sky the clouds fill; density thins them to wisps (and nothing) toward a clear sky
	CloudMat->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), BaseCoverage + 0.5f * (CloudCover - 1.f));
	CloudMat->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), BaseDensity * JeebLook::CloudDensity.GetValueOnGameThread() * FMath::Clamp(0.15f + 0.85f * CloudCover, 0.f, 1.3f));
#if !UE_BUILD_SHIPPING
	// (test pictures: jeeb.Cloud.Set="Name=Value;Name=Value" sets any of the material's scalars each frame)
	const FString Set = JeebLook::CloudSet.GetValueOnGameThread();
	if (!Set.IsEmpty())
	{
		TArray<FString> Parts; Set.ParseIntoArray(Parts, TEXT(";"));
		for (const FString& P : Parts) { FString K, Val; if (P.Split(TEXT("="), &K, &Val)) CloudMat->SetScalarParameterValue(FName(*K.TrimStartAndEnd()), FCString::Atof(*Val)); }
	}
#endif
}

// The HIGH volumetric clouds' layer, sampling and light, from the jeeb.Cloud.* variables (re-sent only when one changes)
void ADayNight::ApplyCloudLook()
{
	using namespace JeebLook;
	UVolumetricCloudComponent* VC = CloudComp.Get();
	if (!VC) return;
	const float Sm = CloudSamples.GetValueOnGameThread(), Sh = CloudShadowSamples.GetValueOnGameThread(), Tr = CloudTrace.GetValueOnGameThread();
	const float Bo = CloudBottom.GetValueOnGameThread(), He = CloudHeight.GetValueOnGameThread(), Un = CloudUnder.GetValueOnGameThread();
	const float Bn = CloudBounce.GetValueOnGameThread(), Sc = CloudScale.GetValueOnGameThread(), Fi = CloudFill.GetValueOnGameThread(), Wa = CloudWarm.GetValueOnGameThread();
	const FString Now = FString::Printf(TEXT("%g %g %g %g %g %g %g %g %g %g"), Sm, Sh, Tr, Bo, He, Un, Bn, Sc, Fi, Wa);
	if (Now == CloudSent) return;
	CloudSent = Now;
	VC->SetLayerBottomAltitude(Bo);
	VC->SetLayerHeight(He);
	VC->SetTracingMaxDistance(Tr);
	VC->SetViewSampleCountScale(Sm);
	VC->SetReflectionViewSampleCountScale(Sm);   // (the sky light's capture of the clouds too)
	VC->SetShadowViewSampleCountScale(Sh);
	VC->SetShadowReflectionViewSampleCountScale(Sh);
	VC->SetSkyLightCloudBottomOcclusion(Un);
	// light bounced up off sunny grass and earth: a warm grey (the engine's own is a plain 0.3 grey)
	const float W = FMath::Clamp((Bn - 0.3f) / 0.3f, 0.f, 1.f);
	VC->SetGroundAlbedo(FLinearColor(Bn * (1.f + 0.10f * W), Bn, Bn * (1.f - 0.20f * W)).ToFColor(true));
	if (CloudMat && CloudBaseScale != 0.f) CloudMat->SetScalarParameterValue(TEXT("Layout_CloudGlobalScale"), CloudBaseScale * Sc);
	if (CloudMat && CloudBaseFill != 0.f) CloudMat->SetScalarParameterValue(TEXT("FillScatterIntensity"), CloudBaseFill * Fi);
	if (ADirectionalLight* S = Sun.Get())
		if (UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(S->GetLightComponent()))
		{
			L->CloudScatteredLuminanceScale = FLinearColor(1.f, 1.f - 0.10f * Wa, 1.f - 0.28f * Wa);   // golden light through the heaps
			L->MarkRenderStateDirty();
		}
	UE_LOG(LogTemp, Log, TEXT("JEEBCLOUD samples %.2f shadow %.2f trace %.0f km layer %.1f + %.1f km under %.2f bounce %.2f scale x%.2f fill x%.2f warm %.2f"), Sm, Sh, Tr, Bo, He, Un, Bn, Sc, Fi, Wa);
}

// LOW's sky (2026-11-13): no volumetric clouds there, so a ring of painted cumulus cards (T_Cumulus's four heaps, M_Cumulus)
// stands far out round the camera, low over the horizon like distant fair-weather heaps. All in one mesh: one draw, a few
// dozen triangles. It moves with the camera (never comes closer), and hides while the volumetric clouds are on.
namespace
{
	constexpr float LowCloudRing = 300000.f;   // 3 km out: behind every hill of the 2.3 x 1.7 km world
}

void ADayNight::BuildLowClouds()
{
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Cumulus.M_Cumulus"));
	if (!Mat) { UE_LOG(LogTemp, Warning, TEXT("M_Cumulus is missing: run Tools/BuildMaterials.ps1. LOW's sky stays clear.")); return; }
	LowClouds = NewObject<UProceduralMeshComponent>(this, TEXT("LowClouds"));
	LowClouds->SetupAttachment(GetRootComponent());
	LowClouds->SetUsingAbsoluteLocation(true);
	LowClouds->SetUsingAbsoluteRotation(true);
	LowClouds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LowClouds->SetCastShadow(false);
	LowClouds->bAffectDistanceFieldLighting = false;
	LowClouds->SetVisibleInRayTracing(false);
	LowClouds->RegisterComponent();

	TArray<FVector> V, N; TArray<int32> T; TArray<FVector2D> UV; TArray<FLinearColor> C; TArray<FProcMeshTangent> Tg;
	FRandomStream R(9127);   // (the same sky on every machine and every run)
	const int32 Count = 26;
	for (int32 i = 0; i < Count; ++i)
	{
		const float Yaw = (i + R.FRandRange(-0.35f, 0.35f)) * 2.f * PI / Count;
		if (R.FRand() < 0.18f) continue;   // a few gaps of open sky
		const float Dist = LowCloudRing * R.FRandRange(0.85f, 1.1f);
		const float Size = R.FRandRange(90000.f, 170000.f);           // 0.9 to 1.7 km across
		const float Base = Dist * FMath::Tan(FMath::DegreesToRadians(R.FRandRange(1.5f, 5.5f)));   // the flat base a few degrees up
		const FVector Out(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f), Side(-Out.Y, Out.X, 0.f);
		const FVector Mid = Out * Dist;
		// the tile's flat base sits at 0.79 of its height: put that on Base
		const float Bottom = Base - Size * (1.f - 0.79f);
		const int32 Tile = R.RandRange(0, 3);
		const float U0 = (Tile % 2) * 0.5f, V0 = (Tile / 2) * 0.5f;
		const bool bFlip = R.FRand() < 0.5f;
		const int32 First = V.Num();
		const FVector P[4] = { Mid - Side * Size * 0.5f + FVector(0, 0, Bottom), Mid + Side * Size * 0.5f + FVector(0, 0, Bottom),
		                       Mid + Side * Size * 0.5f + FVector(0, 0, Bottom + Size), Mid - Side * Size * 0.5f + FVector(0, 0, Bottom + Size) };
		const float Ul = bFlip ? U0 + 0.5f : U0, Ur = bFlip ? U0 : U0 + 0.5f;
		const FVector2D Q[4] = { FVector2D(Ul, V0 + 0.5f), FVector2D(Ur, V0 + 0.5f), FVector2D(Ur, V0), FVector2D(Ul, V0) };
		for (int32 k = 0; k < 4; ++k) { V.Add(P[k]); N.Add(-Out); UV.Add(Q[k]); C.Add(FLinearColor::White); }
		T.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });   // (M_Cumulus is two-sided: seen from the middle either way)
	}
	LowClouds->CreateMeshSection_LinearColor(0, V, T, N, UV, C, Tg, false);
	LowCloudMat = UMaterialInstanceDynamic::Create(Mat, this);
	LowCloudMat->SetScalarParameterValue(TEXT("Opacity"), 1.f);   // (solid in the middle: at 0.95 the sun disc behind showed through as a white ball)
	LowClouds->SetMaterial(0, LowCloudMat);
	LowClouds->SetVisibility(false);
}

void ADayNight::TickLowClouds(float High)
{
	if (!LowClouds || !LowCloudMat) return;
	static IConsoleVariable* VolCloud = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricCloud"));
	const bool bShow = VolCloud && VolCloud->GetInt() == 0;
	if (LowClouds->IsVisible() != bShow) LowClouds->SetVisibility(bShow);
	if (!bShow) return;
	// (purely a look on this machine: the local camera, null-checked)
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->PlayerCameraManager) LowClouds->SetWorldLocation(PC->PlayerCameraManager->GetCameraLocation());
	// lit sides take the sun's colour, shaded sides the sky's blue-grey (warmer and dimmer at the low sun)
	FLinearColor SunCol = FLinearColor::White;
	if (ADirectionalLight* S = Sun.Get()) SunCol = S->GetLightComponent()->GetLightColor();
	// (at the low sun the sky behind them is a deep dusky brown: the heaps go dim, amber on top and dusky mauve below, like HIGH's)
	// (M_Cumulus takes no fog, so a share of the haze's own colour is mixed in here: far heaps sit in the same air as the hills)
	FLinearColor Haze(0.6f, 0.62f, 0.68f);
	if (UExponentialHeightFogComponent* FC = Fog.IsValid() ? Fog->GetComponent() : nullptr) Haze = FC->FogInscatteringLuminance;
	// (second review, 2026-11-14: at the low sun the heaps read as cold lavender stickers on the orange sky. Now their lit
	// side is the sun's colour pulled toward a warm cream-peach, their base a muted warm brown-pink, and more of the sky's
	// own orange haze goes in as the sun sinks; noon is as before)
	const float HazeShare = FMath::Lerp(JeebLook::LowCloudHaze.GetValueOnGameThread() + 0.1f, JeebLook::LowCloudHaze.GetValueOnGameThread(), High);
	const FLinearColor LitLow = FMath::Lerp(SunCol, FLinearColor(1.f, 0.84f, 0.66f), 0.4f);
	const FLinearColor LitHigh = FMath::Lerp(SunCol, FLinearColor(1.f, 0.98f, 0.94f), 0.5f);
	LowCloudMat->SetVectorParameterValue(TEXT("Lit"), FMath::Lerp(FMath::Lerp(LitLow, LitHigh, High), Haze, HazeShare));
	LowCloudMat->SetVectorParameterValue(TEXT("Shade"), FMath::Lerp(FMath::Lerp(FLinearColor(0.52f, 0.36f, 0.30f), FLinearColor(0.58f, 0.64f, 0.76f), High), Haze, HazeShare));
	LowCloudMat->SetScalarParameterValue(TEXT("Bright"), JeebLook::LowCloudBright.GetValueOnGameThread() * FMath::Lerp(JeebLook::LowCloudDusk.GetValueOnGameThread(), 1.f, High) * FMath::Lerp(1.f, 0.25f, Night));   // (dimmer at night: 2026-11-17)
}
