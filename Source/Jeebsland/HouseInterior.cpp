#include "HouseInterior.h"
#include "OakHill.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Camera/PlayerCameraManager.h"

static TAutoConsoleVariable<float> CVarIntCam(TEXT("jeeb.Interior.Cam"), 0.5f, TEXT("The open house: the camera arm's share of its usual length while he stands inside (1 = as outside)."));
static TAutoConsoleVariable<float> CVarIntExposure(TEXT("jeeb.Interior.Exposure"), 0.f, TEXT("The open house: exposure bias inside (EV; the land's own is about 0.35). Higher = brighter room."));
static TAutoConsoleVariable<float> CVarIntReach(TEXT("jeeb.Interior.LightReach"), 400.f, TEXT("The open house: its lights are on only while the camera is within this many cm of the room (inside it, or just outside the door)."));
static TAutoConsoleVariable<float> CVarIntLight(TEXT("jeeb.Interior.Light"), 1.f, TEXT("The open house: every light inside times this."));

namespace
{
	TArray<TWeakObjectPtr<AHouseInterior>> GRooms;
	void HashIn(uint32& H, float V) { const int32 I = FMath::RoundToInt(V * 10.f); H = (H ^ uint32(I)) * 16777619u; }
	float F(const TArray<FString>& C, int32 i, float Def = 0.f) { return C.IsValidIndex(i) && !C[i].TrimStartAndEnd().IsEmpty() ? FCString::Atof(*C[i]) : Def; }
}

AHouseInterior::AHouseInterior()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
}

void AHouseInterior::BeginPlay()
{
	Super::BeginPlay();
	GRooms.Add(this);
	Build();
	if (RoomLocal.IsValid)
	{
		// (no grass, flowers or leaves on the room's floor: JeebOakHill::OnBuilding asks this)
		const float R = FMath::DegreesToRadians(GetActorRotation().Yaw);
		JeebOakHill::OpenRooms().Add({ this, FVector2D(GetActorLocation()), FMath::Cos(R), FMath::Sin(R),
			FVector2D(RoomLocal.Min) - FVector2D(30.f), FVector2D(RoomLocal.Max) + FVector2D(30.f) });
	}
}

void AHouseInterior::EndPlay(const EEndPlayReason::Type Why)
{
	GRooms.RemoveAll([this](const TWeakObjectPtr<AHouseInterior>& R) { return !R.IsValid() || R.Get() == this; });
	JeebOakHill::OpenRooms().RemoveAll([this](const JeebOakHill::FOpenRoom& R) { return R.Owner == this; });
	Super::EndPlay(Why);
}

float AHouseInterior::CamScaleAt(const UWorld* World, const FVector& At)
{
	for (const TWeakObjectPtr<AHouseInterior>& R : GRooms)
		if (R.IsValid() && R->GetWorld() == World && R->RoomLocal.IsValid)
		{
			const FVector L = R->GetActorTransform().InverseTransformPosition(At);
			if (R->RoomLocal.ExpandBy(FVector(0.f, 0.f, 100.f)).IsInsideXY(L) && L.Z > R->RoomLocal.Min.Z - 50.f && L.Z < R->RoomLocal.Max.Z + 100.f)
				return FMath::Clamp(CVarIntCam.GetValueOnGameThread(), 0.1f, 1.f);
		}
	return 1.f;
}

// Content/Data/InteriorHouse.csv: Mesh,X,Y,Z,Yaw,Pitch,Roll,SX,SY,SZ,Solid,Shadow,Light,Radius,Mat (Mat: a material for every slot instead of its own)
//  Mesh: an asset path, or ROOM (X,Y,Z the room's low corner, SX,SY,SZ its high one), LIGHT (a lamp's light alone), FIRE (the hearth's
//  light: flickers). X,Y,Z: cm in the house's frame (X along its long side, +Y out of the door), Z from the floor. SY/SZ empty = SX.
//  Solid 1: a box from the mesh's bounds blocks him (every machine). Shadow 1: casts shadows. Light: lumens of a light at the spot
//  (on a mesh row: at the mesh's middle top), Radius: its reach in cm.
void AHouseInterior::Build()
{
	const bool bServer = IsNetMode(NM_DedicatedServer);
	TArray<FString> Lines;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/InteriorHouse.csv");
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path)) { UE_LOG(LogTemp, Warning, TEXT("JEEBINT no %s: an empty house"), *Path); return; }
	const FTransform Xf = GetActorTransform();
	int32 Pieces = 0, Solids = 0, Missing = 0;
	TMap<FString, UStaticMesh*> Cache;
	for (int32 n = 1; n < Lines.Num(); ++n)
	{
		FString Line = Lines[n];
		int32 Hash0; if (Line.FindChar(TEXT('#'), Hash0)) Line.LeftInline(Hash0);
		TArray<FString> C; Line.ParseIntoArray(C, TEXT(","), false);
		if (C.Num() < 4 || C[0].TrimStartAndEnd().IsEmpty()) continue;
		const FString Mesh = C[0].TrimStartAndEnd();
		const FVector P(F(C, 1), F(C, 2), F(C, 3) + FloorZ);
		const FRotator Rot(F(C, 5), F(C, 4), F(C, 6));
		const float SX = F(C, 7, 1.f);
		const FVector S(SX, F(C, 8, SX), F(C, 9, SX));
		const bool bSolid = F(C, 10) > 0.5f, bShadow = F(C, 11) > 0.5f;
		const float Lm = F(C, 12), Rad = F(C, 13, 350.f);
		if (Mesh == TEXT("ROOM")) { RoomLocal = FBox(P, FVector(S.X, S.Y, S.Z + FloorZ)); continue; }
		FVector LightAt = P;
		if (Mesh != TEXT("LIGHT") && Mesh != TEXT("FIRE"))
		{
			UStaticMesh*& M = Cache.FindOrAdd(Mesh);
			if (!M) M = LoadObject<UStaticMesh>(nullptr, *Mesh);
			if (!M) { if (Missing++ < 10) UE_LOG(LogTemp, Warning, TEXT("JEEBINT missing %s"), *Mesh); continue; }
			const FTransform PieceXf = FTransform(Rot, P, S) * Xf;
			if (!bServer)
			{
				UStaticMeshComponent* Sm = NewObject<UStaticMeshComponent>(this);
				Sm->SetStaticMesh(M);
				Sm->SetupAttachment(RootComponent);
				Sm->SetMobility(EComponentMobility::Static);
				Sm->SetWorldTransform(PieceXf);
				Sm->SetCastShadow(bShadow);
				if (C.IsValidIndex(14) && !C[14].TrimStartAndEnd().IsEmpty())   // (Mat: another material on every slot)
					if (UMaterialInterface* Mi = LoadObject<UMaterialInterface>(nullptr, *C[14].TrimStartAndEnd()))
						for (int32 s = 0; s < Sm->GetNumMaterials(); ++s) Sm->SetMaterial(s, Mi);
				Sm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Sm->SetCachedMaxDrawDistance(3000.f);
				Sm->LDMaxDrawDistance = 3000.f;
				Sm->RegisterComponent();
			}
			if (bSolid)
			{
				const FBox B = M->GetBoundingBox();
				UBoxComponent* Bx = NewObject<UBoxComponent>(this);
				Bx->SetupAttachment(RootComponent);
				Bx->SetBoxExtent(B.GetExtent() * S.GetAbs());
				Bx->SetWorldTransform(FTransform(Rot, P + Rot.RotateVector(B.GetCenter() * S)) * Xf);
				Bx->SetWorldScale3D(FVector::OneVector);
				Bx->SetCollisionProfileName(TEXT("BlockAll"));
				Bx->SetMobility(EComponentMobility::Static);
				Bx->SetCanEverAffectNavigation(false);
				Bx->RegisterComponent();
				HashIn(Hash, Bx->GetScaledBoxExtent().X); HashIn(Hash, Bx->GetScaledBoxExtent().Y);
				++Solids;
			}
			LightAt = P + Rot.RotateVector(FVector(M->GetBoundingBox().GetCenter().X, M->GetBoundingBox().GetCenter().Y, M->GetBoundingBox().Max.Z) * S);
			++Pieces;
		}
		HashIn(Hash, P.X); HashIn(Hash, P.Y); HashIn(Hash, P.Z); HashIn(Hash, Rot.Yaw);
		if (Lm > 0.f && !bServer)
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(this);
			L->SetupAttachment(RootComponent);
			L->SetWorldLocation(Xf.TransformPosition(LightAt));
			L->SetIntensityUnits(ELightUnits::Lumens);
			L->SetIntensity(0.f);
			L->SetAttenuationRadius(Rad);
			// (2026-11-21 evening, owner: "reads very orange": still warm, nearer candle-white; were 1 0.55 0.25 and 1 0.72 0.42)
			L->SetLightColor(Mesh == TEXT("FIRE") ? FLinearColor(1.f, 0.74f, 0.52f) : FLinearColor(1.f, 0.9f, 0.78f));
			L->SetCastShadows(bShadow);
			L->SetSourceRadius(Mesh == TEXT("FIRE") ? 25.f : 3.f);
			L->SetVisibility(false);
			L->RegisterComponent();
			Lights.Add(L);
			LightBase.Add(Mesh == TEXT("FIRE") ? -Lm : Lm);   // (negative: flickers)
		}
	}
	// A gentle exposure lift inside, so the room reads cosy rather than black (a box over the room, above the land's own volume)
	if (!bServer && RoomLocal.IsValid)
	{
		FActorSpawnParameters SP; SP.Owner = this;
		Post = GetWorld()->SpawnActor<APostProcessVolume>(Xf.TransformPosition(RoomLocal.GetCenter()), Xf.Rotator(), SP);
		if (Post)
		{
			Post->bUnbound = true;   // (a volume spawned in play has no brush to be inside of: its weight follows the camera instead, in Tick)
			Post->BlendWeight = 0.f;
			Post->Priority = 20.f;
			if (UPrimitiveComponent* Pc = Cast<UPrimitiveComponent>(Post->GetRootComponent())) Pc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Post->Settings.bOverride_AutoExposureBias = true;
			Post->Settings.AutoExposureBias = CVarIntExposure.GetValueOnGameThread();
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBINT house at %.0f %.0f %.0f yaw %.0f: %d pieces, %d solid, %d lights, room %s, hash %u"), Xf.GetLocation().X, Xf.GetLocation().Y, Xf.GetLocation().Z,
		Xf.Rotator().Yaw, Pieces, Solids, Lights.Num(), *RoomLocal.ToString(), Hash);
}

void AHouseInterior::Tick(float Dt)
{
	Super::Tick(Dt);
	TestTick(Dt);
	if (Lights.Num() == 0) return;
	const APlayerController* Pc = GetWorld()->GetFirstPlayerController();   // (local visuals only)
	const APawn* Me = Pc ? Pc->GetPawn() : nullptr;
	FVector Eye = Me ? Me->GetActorLocation() : FVector(1.0e9f);
	if (Pc && Pc->PlayerCameraManager) Eye = Pc->PlayerCameraManager->GetCameraLocation();
	const float Reach = CVarIntReach.GetValueOnGameThread();
	const FVector EyeL = GetActorTransform().InverseTransformPosition(Eye);
	const bool bNear = RoomLocal.IsValid && FVector::Dist(RoomLocal.GetClosestPointTo(EyeL), EyeL) < Reach;
	LitAlpha = FMath::FInterpConstantTo(LitAlpha, bNear ? 1.f : 0.f, Dt, 2.f);
	const bool bOn = LitAlpha > 0.f;
	if (Post)
	{
		// (the exposure lift: full with the camera inside the room, none 1.5 m outside its walls)
		const FVector L = GetActorTransform().InverseTransformPosition(Eye);
		const FVector C = RoomLocal.GetClosestPointTo(L);
		const float Out = FVector::Dist(C, L);
		Post->BlendWeight = FMath::FInterpTo(Post->BlendWeight, 1.f - FMath::Clamp(Out / 150.f, 0.f, 1.f), Dt, 4.f);
		Post->Settings.AutoExposureBias = CVarIntExposure.GetValueOnGameThread();
	}
	if (!bOn && !bLit) return;
	bLit = bOn;
	Flicker += Dt;
	const float K = LitAlpha * CVarIntLight.GetValueOnGameThread();
	for (int32 i = 0; i < Lights.Num(); ++i)
	{
		UPointLightComponent* L = Lights[i];
		if (!L) continue;
		const float B = LightBase[i];
		const float Fl = B < 0.f ? 0.85f + 0.1f * FMath::Sin(Flicker * 11.f) + 0.05f * FMath::Sin(Flicker * 23.7f + 1.3f) : 1.f;
		if (L->IsVisible() != bOn) L->SetVisibility(bOn);
		L->SetIntensity(FMath::Abs(B) * K * Fl);
	}
}

// Test helpers (not in a shipped game): jeeb.Test.GoHouse 1 (console, single player) or -StartAtOpenHouse puts him 2.5 m outside
// the open house's door, facing it. -OpenHouseWalk then walks him in from 7 s for 4.5 s, through the door to the hearth's rug,
// logging where he is each half second (JEEBINT walk); -OpenHouseFilm also films his own camera every 4th frame meanwhile
// (Saved/Shots/<ShotName>_walk_NNN.png).
static TAutoConsoleVariable<int32> CVarGoHouse(TEXT("jeeb.Test.GoHouse"), 0, TEXT("Test builds, single player: 1 puts you just outside the open house's door (House_CLS1)."));
void AHouseInterior::TestTick(float Dt)
{
#if !UE_BUILD_SHIPPING
	if (GetNetMode() != NM_Standalone) return;
	APlayerController* Pc = GetWorld()->GetFirstPlayerController();
	APawn* Me = Pc ? Pc->GetPawn() : nullptr;
	if (!Me) return;
	TestClock += Dt;
	const FTransform Xf = GetActorTransform();
	const FVector Outside = Xf.TransformPosition(FVector(27.f, 520.f, 100.f));
	const FRotator In(0.f, Xf.Rotator().Yaw - 90.f, 0.f);   // (toward -Y in the house: in through the door)
	static const bool bStart = FParse::Param(FCommandLine::Get(), TEXT("StartAtOpenHouse")) || FParse::Param(FCommandLine::Get(), TEXT("OpenHouseWalk"));
	static bool bStarted = false;
	if (CVarGoHouse.GetValueOnGameThread() != 0 || (bStart && !bStarted && TestClock > 5.f))
	{
		CVarGoHouse->Set(0, ECVF_SetByConsole);
		bStarted = true;
		Me->SetActorLocation(Outside, false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorRotation(In);
		Pc->SetControlRotation(FRotator(-12.f, In.Yaw, 0.f));
		UE_LOG(LogTemp, Log, TEXT("JEEBINT test: put outside the door at %s facing %.0f"), *Outside.ToCompactString(), In.Yaw);
	}
	static const bool bWalk = FParse::Param(FCommandLine::Get(), TEXT("OpenHouseWalk"));
	static const bool bFilm = FParse::Param(FCommandLine::Get(), TEXT("OpenHouseFilm"));
	if (!bWalk || !bStarted) return;
	const float T = TestClock - 7.f;
	if (T > 6.f) { Pc->ConsoleCommand(TEXT("quit")); return; }
	if (T > 0.f && T < 4.5f)
	{
		// in at the door, then bear left toward the hearth for the last part
		const FVector Dir = Xf.TransformVectorNoScale(T < 3.f ? FVector(0.f, -1.f, 0.f) : FVector(-0.8f, -0.6f, 0.f));
		Me->AddMovementInput(Dir, T < 3.f ? 0.5f : 0.4f);
		Pc->SetControlRotation(FMath::RInterpTo(Pc->GetControlRotation(), FRotator(-12.f, Dir.Rotation().Yaw, 0.f), Dt, 2.f));
		static int32 Half = -1;
		if (FMath::FloorToInt(T * 2.f) > Half)
		{
			Half = FMath::FloorToInt(T * 2.f);
			const FVector L = Xf.InverseTransformPosition(Me->GetActorLocation());
			FVector Cam = Pc->PlayerCameraManager ? Xf.InverseTransformPosition(Pc->PlayerCameraManager->GetCameraLocation()) : FVector::ZeroVector;
			UE_LOG(LogTemp, Log, TEXT("JEEBINT walk %.1f s: knight at house (%.0f, %.0f, %.0f), speed %.0f, camera at (%.0f, %.0f, %.0f) %s, arm share %.2f"), T, L.X, L.Y, L.Z,
				Me->GetVelocity().Size2D(), Cam.X, Cam.Y, Cam.Z, RoomLocal.IsInsideOrOn(Cam) ? TEXT("inside") : TEXT("outside"), CamScaleAt(GetWorld(), Me->GetActorLocation()));
		}
		static int32 FilmN = 0;
		if (bFilm && GFrameCounter % 4 == 0)
		{
			FString Name = TEXT("house"); FParse::Value(FCommandLine::Get(), TEXT("ShotName="), Name);
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("%s_walk_%03d.png"), *Name, FilmN++), false, false);
		}
	}
#endif
}
