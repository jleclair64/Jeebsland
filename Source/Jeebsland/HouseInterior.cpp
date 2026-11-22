#include "HouseInterior.h"
#include "OakHill.h"
#include "Terrain.h"
#include "KnightCharacter.h"
#include "KnightGameState.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Camera/PlayerCameraManager.h"

static TAutoConsoleVariable<float> CVarIntCam(TEXT("jeeb.Interior.Cam"), 0.5f, TEXT("The open house: the camera arm's share of its usual length while he stands inside (1 = as outside)."));
static TAutoConsoleVariable<float> CVarIntExposure(TEXT("jeeb.Interior.Exposure"), -0.9f, TEXT("The open house: exposure bias inside (EV; the land's own is about 0.35). Higher = brighter room."));
static TAutoConsoleVariable<float> CVarIntReach(TEXT("jeeb.Interior.LightReach"), 400.f, TEXT("The open house: its lights are on only while the camera is within this many cm of the room (inside it, or just outside the door)."));
static TAutoConsoleVariable<float> CVarIntLight(TEXT("jeeb.Interior.Light"), 0.5f, TEXT("The open house: every light inside times this."));
// (2026-11-22, owner: "still too bright ... much warmer ... focused on the candle, chandelier, oil lamp and fireplace", "each candle light
// up", "intermittently blink as if they were real candles". Local visuals only.)
static TAutoConsoleVariable<float> CVarIntFlame(TEXT("jeeb.Interior.Flame"), 1.f, TEXT("The open house: brightness of the little flames and their halos at every wick (0 hides them)."));
static TAutoConsoleVariable<float> CVarIntBlink(TEXT("jeeb.Interior.Blink"), 1.f, TEXT("The open house: how strongly the flames now and then gutter and blink (0: only a gentle flicker)."));
static TAutoConsoleVariable<float> CVarIntReachK(TEXT("jeeb.Interior.LightRadius"), 1.f, TEXT("The open house: every light's reach times this (smaller = pools of light round each flame, darker corners)."));
static TAutoConsoleVariable<float> CVarIntLoad(TEXT("jeeb.Interior.LoadReach"), 600.f, TEXT("The open house: its room (furniture, lights) is built on your machine once your knight or camera is within this many cm of its door, or inside."));
static TAutoConsoleVariable<float> CVarIntUnload(TEXT("jeeb.Interior.UnloadReach"), 1000.f, TEXT("The open house: its room is taken down again once you are further than this many cm from its door."));
static TAutoConsoleVariable<float> CVarDoorClose(TEXT("jeeb.Interior.DoorClose"), 2.f, TEXT("The open house's door (server): it shuts by itself this many seconds after the doorway is clear (0: never)."));
static TAutoConsoleVariable<float> CVarDoorSpeed(TEXT("jeeb.Interior.DoorSpeed"), 4.f, TEXT("The open house's door: how fast it swings (higher = snappier)."));

namespace
{
	TArray<TWeakObjectPtr<AHouseInterior>> GRooms;
	void HashIn(uint32& H, float V) { const int32 I = FMath::RoundToInt(V * 10.f); H = (H ^ uint32(I)) * 16777619u; }
	float F(const TArray<FString>& C, int32 i, float Def = 0.f) { return C.IsValidIndex(i) && !C[i].TrimStartAndEnd().IsEmpty() ? FCString::Atof(*C[i]) : Def; }

	// The doorway, in the house's frame (cm; the shell's cut, Tools/interior_house_open.py, at the game's 0.96): X -42..96, the
	// lining from the room's inner wall (Y 228) out to the porch face (Y 298), 216 high. The door hangs at the lining's room end,
	// hinged on its east post (+X: he turns west to the hearth, so the open door is never between him and the camera), and swings
	// into the room along the doorway's east side (the room keeps that clear, interior_lib.py). The hinge turns 180 (shut) to 268 (open).
	const FVector DoorHinge(96.f, 228.f, 0.f);
	const float DoorW = 138.f, DoorH = 216.f, DoorOpenYaw = 88.f;
	const FVector DoorMid(27.f, 262.f, 0.f);
	// someone here stops it shutting on them (its swing), or keeps it open (the doorway and a metre either side)
	const FBox SwingZone(FVector(-60.f, 70.f, -60.f), FVector(110.f, 320.f, 260.f));
	const FBox ClearZone(FVector(-100.f, 40.f, -60.f), FVector(150.f, 420.f, 260.f));
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
	BuildDoor();
	if (RoomLocal.IsValid)
	{
		// (no grass, flowers or leaves on the room's floor: JeebOakHill::OnBuilding asks this)
		const float R = FMath::DegreesToRadians(GetActorRotation().Yaw);
		JeebOakHill::OpenRooms().Add({ this, FVector2D(GetActorLocation()), FMath::Cos(R), FMath::Sin(R),
			FVector2D(RoomLocal.Min) - FVector2D(30.f), FVector2D(RoomLocal.Max) + FVector2D(30.f) });
	}
	// (joining late: the server's door may already be open)
	if (GetNetMode() == NM_Client)
		if (const AKnightGameState* GS = GetWorld()->GetGameState<AKnightGameState>())
		{
			bool bOpen = false;
			if (GS->FindDoor(DoorKey(), bOpen)) SetDoorOpen(bOpen);
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

bool AHouseInterior::HasRoom(const TCHAR* PlotId)
{
	return PlotId && *PlotId && FPaths::FileExists(FPaths::ProjectContentDir() / FString::Printf(TEXT("Data/Interior_%s.csv"), PlotId));
}

FVector AHouseInterior::DoorKey() const { return GetActorTransform().TransformPosition(DoorHinge + FVector(0.f, 0.f, FloorZ)); }
FVector AHouseInterior::DoorHandle() const { return GetActorTransform().TransformPosition(DoorMid + FVector(0.f, 0.f, FloorZ + 105.f)); }

AHouseInterior* AHouseInterior::DoorInReach(const AActor* Knight, bool bFacing)
{
	if (!Knight) return nullptr;
	const FVector Me = Knight->GetActorLocation();
	AHouseInterior* Best = nullptr;
	float BestD = 200.f;
	for (const TWeakObjectPtr<AHouseInterior>& R : GRooms)
		if (R.IsValid() && R->GetWorld() == Knight->GetWorld())
		{
			const FVector H = R->DoorHandle();
			const float D = FVector::Dist2D(Me, H);
			if (bFacing && FVector::DotProduct(Knight->GetActorForwardVector().GetSafeNormal2D(), (H - Me).GetSafeNormal2D()) < 0.6f) continue;
			if (D < BestD && FMath::Abs(Me.Z - H.Z) < 150.f) { BestD = D; Best = R.Get(); }
		}
	return Best;
}

bool AHouseInterior::SomeoneIn(const FBox& Zone) const
{
	const FTransform Xf = GetActorTransform();
	const FBox Z(Zone.Min + FVector(0.f, 0.f, FloorZ), Zone.Max + FVector(0.f, 0.f, FloorZ));
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		if (It->IsPlayerControlled() && Z.IsInside(Xf.InverseTransformPosition(It->GetActorLocation()))) return true;   // (players only: a villager on the doorstep never holds it open)
	return false;
}

bool AHouseInterior::ToggleDoor()
{
	if (bDoorOpen && SomeoneIn(SwingZone)) return false;   // (not shut on someone in its swing)
	bDoorOpen = !bDoorOpen;
	DoorOpenFor = DoorClearFor = 0.f;
	UE_LOG(LogTemp, Log, TEXT("JEEBDOOR %s %s (server, by E)"), *PlotId, bDoorOpen ? TEXT("opened") : TEXT("shut"));
	return true;
}

void AHouseInterior::SetDoorOpen(bool bOpen)
{
	if (bOpen != bDoorOpen) UE_LOG(LogTemp, Log, TEXT("JEEBDOOR %s %s (from the server), room %s"), *PlotId, bOpen ? TEXT("opened") : TEXT("shut"), bLoaded ? TEXT("loaded") : TEXT("not loaded"));
	bDoorOpen = bOpen;
}

// The door: the tavern pack's planked door, stretched to the doorway, on a hinge that swings. Its solid box is on every machine
// (the server's decides), the mesh only where someone can see it.
void AHouseInterior::BuildDoor()
{
	if (!RoomLocal.IsValid) return;
	DoorPivot = NewObject<USceneComponent>(this, TEXT("DoorPivot"));
	DoorPivot->SetupAttachment(RootComponent);
	DoorPivot->SetMobility(EComponentMobility::Movable);
	DoorPivot->SetRelativeLocation(DoorHinge + FVector(0.f, 0.f, FloorZ));
	DoorPivot->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	DoorPivot->RegisterComponent();
	if (!IsNetMode(NM_DedicatedServer))
		if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Medieval_Tavern/Meshes/Modular/SM_Door_001")))
		{
			// (its bounds: X -3.7..110.3 along the planks, Y +-12.2 thick, Z 0..215: stretched to the doorway, its back face on the hinge line)
			const FBox B = M->GetBoundingBox();
			const FVector S(DoorW / FMath::Max(1.f, B.GetSize().X), 1.f, DoorH / FMath::Max(1.f, B.GetSize().Z));
			DoorMesh = NewObject<UStaticMeshComponent>(this, TEXT("Door"));
			DoorMesh->SetStaticMesh(M);
			DoorMesh->SetupAttachment(DoorPivot);
			DoorMesh->SetMobility(EComponentMobility::Movable);
			DoorMesh->SetRelativeScale3D(S);
			DoorMesh->SetRelativeLocation(FVector(-B.Min.X * S.X, -B.Max.Y, -B.Min.Z * S.Z + 1.f));
			DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			DoorMesh->SetCastShadow(true);
			DoorMesh->SetCachedMaxDrawDistance(6000.f);
			DoorMesh->LDMaxDrawDistance = 6000.f;
			DoorMesh->RegisterComponent();
		}
	DoorSolid = NewObject<UBoxComponent>(this, TEXT("DoorSolid"));
	DoorSolid->SetupAttachment(DoorPivot);
	DoorSolid->SetMobility(EComponentMobility::Movable);
	DoorSolid->SetBoxExtent(FVector(DoorW * 0.5f, 10.f, DoorH * 0.5f));
	DoorSolid->SetRelativeLocation(FVector(DoorW * 0.5f, -12.f, DoorH * 0.5f + 1.f));
	DoorSolid->SetCollisionProfileName(TEXT("BlockAll"));
	DoorSolid->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	DoorSolid->SetCanEverAffectNavigation(false);
	DoorSolid->RegisterComponent();
}

void AHouseInterior::TickDoor(float Dt)
{
	if (!DoorPivot) return;
	// The server shuts it once the doorway has been clear a moment (so for everyone else it opens and shuts right behind someone)
	if (GetNetMode() != NM_Client && bDoorOpen)
	{
		DoorOpenFor += Dt;
		DoorClearFor = SomeoneIn(ClearZone) ? 0.f : DoorClearFor + Dt;
		const float After = CVarDoorClose.GetValueOnGameThread();
		if (After > 0.f && DoorOpenFor > 1.5f && DoorClearFor > After && !SomeoneIn(SwingZone))
		{
			bDoorOpen = false;
			UE_LOG(LogTemp, Log, TEXT("JEEBDOOR %s shut by itself (server; open %.1f s)"), *PlotId, DoorOpenFor);
			if (AKnightGameState* GS = GetWorld()->GetGameState<AKnightGameState>()) GS->SyncDoor(this);
		}
	}
	const float Target = bDoorOpen ? DoorOpenYaw : 0.f;
	if (!FMath::IsNearlyEqual(DoorAngle, Target, 0.05f))
	{
		DoorAngle = FMath::FInterpTo(DoorAngle, Target, Dt, CVarDoorSpeed.GetValueOnGameThread());
		if (FMath::IsNearlyEqual(DoorAngle, Target, 0.05f)) DoorAngle = Target;
		DoorPivot->SetRelativeRotation(FRotator(0.f, 180.f + DoorAngle, 0.f));
		if (DoorSolid)
		{
			// solid shut (not to the camera, so it never snaps in at the door) and solid again once fully open against the doorway's side
			// (to the camera too: it stops at the open door instead of passing through it); never while it swings
			const bool bShut = DoorAngle < 25.f, bOpenWide = DoorAngle > DoorOpenYaw - 6.f;
			DoorSolid->SetCollisionEnabled(bShut || bOpenWide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
			DoorSolid->SetCollisionResponseToChannel(ECC_Camera, bOpenWide ? ECR_Block : ECR_Ignore);
		}
	}
}

// Content/Data/Interior_<PlotId>.csv (written by Tools/interior_rooms.py, which holds the shell every house shares): Mesh,X,Y,Z,Yaw,Pitch,Roll,SX,SY,SZ,Solid,Shadow,Light,Radius,Mat
//  Mat: a material for every slot instead of its own, or slot=material|slot=material for single slots (slot: its number)
//  Mesh: an asset path, or ROOM (X,Y,Z the room's low corner, SX,SY,SZ its high one), LIGHT (a lamp's light alone), FIRE (the hearth's
//  light: flickers). X,Y,Z: cm in the house's frame (X along its long side, +Y out of the door), Z from the floor. SY/SZ empty = SX.
//  Solid 1: a box from the mesh's bounds blocks him (every machine). Shadow 1: casts shadows. Light: lumens of a light at the spot
//  (on a mesh row: at the mesh's middle top), Radius: its reach in cm.
// Build reads the list and makes the solid boxes (every machine); the meshes and lights wait for LoadRoom (players' machines, near).
void AHouseInterior::Build()
{
	int32 Pieces0 = 0, Solids = 0, Missing = 0;
	BuildFrom(FString::Printf(TEXT("Data/Interior_%s.csv"), *PlotId), Pieces0, Solids, Missing);
	const FTransform Xf = GetActorTransform();
	int32 NLights = 0;
	for (const FRow& R : Rows) if (R.Lm > 0.f) ++NLights;
	UE_LOG(LogTemp, Log, TEXT("JEEBINT house %s at %.0f %.0f %.0f yaw %.0f: %d pieces, %d solid, %d lights, room %s, hash %u (room built when a player comes near)"), *PlotId, Xf.GetLocation().X, Xf.GetLocation().Y, Xf.GetLocation().Z,
		Xf.Rotator().Yaw, Pieces0, Solids, NLights, *RoomLocal.ToString(), Hash);
#if !UE_BUILD_SHIPPING
	// (-RoomEyeLog, test pictures: where a -ShotEyes eye must be for spots in this room: the world X Y of a grid of spots in the house's
	// frame and how far the floor is above the land there, since an eye's height counts from the land)
	if (FParse::Param(FCommandLine::Get(), TEXT("RoomEyeLog")))
		for (float Lx = -400.f; Lx <= 440.f; Lx += 60.f)
			for (float Ly = -280.f; Ly <= 560.f; Ly += 60.f)
			{
				const FVector W = Xf.TransformPosition(FVector(Lx, Ly, FloorZ));
				UE_LOG(LogTemp, Log, TEXT("JEEBINTEYE %s %.0f %.0f %.0f %.0f %.0f"), *PlotId, Lx, Ly, W.X, W.Y, W.Z - JeebTerrain::HeightAt(W.X, W.Y));
			}
#endif
}

void AHouseInterior::BuildFrom(const FString& File, int32& NPieces, int32& Solids, int32& Missing)
{
	const bool bServer = IsNetMode(NM_DedicatedServer);
	TArray<FString> Lines;
	const FString Path = FPaths::ProjectContentDir() / File;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path)) { UE_LOG(LogTemp, Warning, TEXT("JEEBINT no %s"), *Path); return; }
	const FTransform Xf = GetActorTransform();
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
		FRow Row;
		Row.Mesh = Mesh; Row.bShadow = bShadow; Row.Lm = Lm; Row.Rad = Rad; Row.bFire = Mesh == TEXT("FIRE");
		Row.bMesh = Mesh != TEXT("LIGHT") && !Row.bFire;
		Row.LightAt = P;
		if (Row.bMesh)
		{
			// (the mesh is needed now only for its bounds: a solid box, or where its light sits; the drawn copy waits for LoadRoom)
			UStaticMesh* M = nullptr;
			if (bSolid || Lm > 0.f || !bServer)
			{
				UStaticMesh*& Mc = MeshCache.FindOrAdd(Mesh);
				if (!Mc) Mc = LoadObject<UStaticMesh>(nullptr, *Mesh);
				M = Mc;
				if (!M) { if (Missing++ < 10) UE_LOG(LogTemp, Warning, TEXT("JEEBINT missing %s"), *Mesh); continue; }
			}
			Row.Xf = FTransform(Rot, P, S) * Xf;
			if (C.IsValidIndex(14)) Row.Mat = C[14].TrimStartAndEnd();
			if (bSolid && M)
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
			if (M) Row.LightAt = P + Rot.RotateVector(FVector(M->GetBoundingBox().GetCenter().X, M->GetBoundingBox().GetCenter().Y, M->GetBoundingBox().Max.Z) * S);
			++NPieces;
		}
		HashIn(Hash, P.X); HashIn(Hash, P.Y); HashIn(Hash, P.Z); HashIn(Hash, Rot.Yaw);
		if (Row.bMesh || (Lm > 0.f && !bServer)) Rows.Add(Row);
	}
}

// The room's furniture, walls, lights and exposure lift, on this machine only (its own knight is near)
void AHouseInterior::LoadRoom()
{
	if (bLoaded || IsNetMode(NM_DedicatedServer)) return;
	const double T0 = FPlatformTime::Seconds();
	const FTransform Xf = GetActorTransform();
	for (const FRow& R : Rows)
	{
		if (R.bMesh)
		{
			UStaticMesh** Mp = MeshCache.Find(R.Mesh);
			UStaticMesh* M = Mp ? *Mp : nullptr;
			if (!M) { M = LoadObject<UStaticMesh>(nullptr, *R.Mesh); if (!M) continue; MeshCache.Add(R.Mesh, M); }
			UStaticMeshComponent* Sm = NewObject<UStaticMeshComponent>(this);
			Sm->SetStaticMesh(M);
			Sm->SetupAttachment(RootComponent);
			Sm->SetMobility(EComponentMobility::Static);
			Sm->SetWorldTransform(R.Xf);
			Sm->SetCastShadow(R.bShadow);
			if (!R.Mat.IsEmpty())   // (Mat: another material on every slot, or slot=material|slot=material)
			{
				TArray<FString> Ms; R.Mat.ParseIntoArray(Ms, TEXT("|"));
				for (const FString& One : Ms)
				{
					FString Slot, MatPath;
					const bool bSlot = One.Split(TEXT("="), &Slot, &MatPath);
					if (UMaterialInterface* Mi = LoadObject<UMaterialInterface>(nullptr, bSlot ? *MatPath : *One))
						for (int32 s = 0; s < Sm->GetNumMaterials(); ++s) if (!bSlot || FCString::Atoi(*Slot) == s) Sm->SetMaterial(s, Mi);
				}
			}
			Sm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Sm->SetCachedMaxDrawDistance(3000.f);
			Sm->LDMaxDrawDistance = 3000.f;
			Sm->RegisterComponent();
			Pieces.Add(Sm);
		}
		if (R.Lm > 0.f)
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(this);
			L->SetupAttachment(RootComponent);
			L->SetWorldLocation(Xf.TransformPosition(R.LightAt));
			L->SetIntensityUnits(ELightUnits::Lumens);
			L->SetIntensity(0.f);
			L->SetAttenuationRadius(R.Rad * FMath::Clamp(CVarIntReachK.GetValueOnGameThread(), 0.2f, 3.f));
			// (2026-11-22, owner: "much warmer": candle and firelight colours, deep amber; were 1 0.62 0.36 and 1 0.78 0.56)
			L->SetLightColor(R.bFire ? FLinearColor(1.f, 0.42f, 0.13f) : FLinearColor(1.f, 0.52f, 0.2f));
			// (few shadowed lights for cost: the hearth and the chandelier; the small candles and lamps are shadowless)
			const bool bChandelier = R.Mesh.Contains(TEXT("SM_Lamp_001"));
			L->SetCastShadows(R.bShadow || R.bFire || bChandelier);
			L->SetSourceRadius(R.bFire ? 25.f : 2.f);
			L->SetVisibility(false);
			L->RegisterComponent();
			Lights.Add(L);
			LightBase.Add(R.Lm);
			LightFlicker.Add(1.f);
			AddFlames(R, Lights.Num() - 1);
		}
	}
	// A gentle exposure lift inside, so the room reads cosy rather than black (a box over the room, above the land's own volume)
	if (RoomLocal.IsValid)
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
	bLoaded = true;
	UE_LOG(LogTemp, Log, TEXT("JEEBINT room %s loaded: %d pieces, %d lights in %.1f ms"), *PlotId, Pieces.Num(), Lights.Num(), (FPlatformTime::Seconds() - T0) * 1000.0);
}

void AHouseInterior::UnloadRoom()
{
	if (!bLoaded) return;
	for (UStaticMeshComponent* Sm : Pieces) if (Sm) Sm->DestroyComponent();
	for (UPointLightComponent* L : Lights) if (L) L->DestroyComponent();
	Pieces.Empty(); Lights.Empty(); LightBase.Empty(); LightFlicker.Empty(); Flames.Empty();
	if (FlameCones) { FlameCones->DestroyComponent(); FlameCones = nullptr; }
	if (FlameHalos) { FlameHalos->DestroyComponent(); FlameHalos = nullptr; }
	if (Post) { Post->Destroy(); Post = nullptr; }
	bLoaded = bLit = false;
	LitAlpha = 0.f;
	UE_LOG(LogTemp, Log, TEXT("JEEBINT room %s unloaded"), *PlotId);
}

void AHouseInterior::Tick(float Dt)
{
	Super::Tick(Dt);
	TestTick(Dt);
	NetTestTick(Dt);
	TickDoor(Dt);
	if (IsNetMode(NM_DedicatedServer) || !RoomLocal.IsValid) return;
	// (local visuals only: this machine's own knight and camera; other players' knights never build a room here)
	const APlayerController* Pc = GetWorld()->GetFirstPlayerController();
	const APawn* Me = Pc ? Pc->GetPawn() : nullptr;
	FVector Eye = Me ? Me->GetActorLocation() : FVector(1.0e9f);
	if (Pc && Pc->PlayerCameraManager) Eye = Pc->PlayerCameraManager->GetCameraLocation();
	const FTransform Xf = GetActorTransform();
	{
		const FVector Door = Xf.TransformPosition(DoorMid);
		float D = FVector::Dist(Eye, Door);
		if (Me) D = FMath::Min(D, FVector::Dist(Me->GetActorLocation(), Door));
		// (inside: his knight, or the camera anywhere in the house up to the roof, e.g. pushed into a wall from the back yard: never an
		// empty shell seen from within, whatever the distance to the door)
		const FVector EyeIn = Xf.InverseTransformPosition(Eye);
		const bool bInside = (Me && RoomLocal.ExpandBy(50.f).IsInside(Xf.InverseTransformPosition(Me->GetActorLocation())))
			|| RoomLocal.ExpandBy(FVector(150.f, 150.f, 400.f)).IsInside(EyeIn);
		if (!bLoaded && (D < CVarIntLoad.GetValueOnGameThread() || bInside)) LoadRoom();
		else if (bLoaded && D > FMath::Max(CVarIntUnload.GetValueOnGameThread(), CVarIntLoad.GetValueOnGameThread() + 200.f) && !bInside) UnloadRoom();
	}
	if (!bLoaded || Lights.Num() == 0) return;
	const float Reach = CVarIntReach.GetValueOnGameThread();
	const FVector EyeL = Xf.InverseTransformPosition(Eye);
	const bool bNear = FVector::Dist(RoomLocal.GetClosestPointTo(EyeL), EyeL) < Reach;
	LitAlpha = FMath::FInterpConstantTo(LitAlpha, bNear ? 1.f : 0.f, Dt, 2.f);
	const bool bOn = LitAlpha > 0.f;
	if (Post)
	{
		// (the exposure lift: full with the camera inside the room, none 1.5 m outside its walls)
		const float Out = FVector::Dist(RoomLocal.GetClosestPointTo(EyeL), EyeL);
		Post->BlendWeight = FMath::FInterpTo(Post->BlendWeight, 1.f - FMath::Clamp(Out / 150.f, 0.f, 1.f), Dt, 4.f);
		Post->Settings.AutoExposureBias = CVarIntExposure.GetValueOnGameThread();
	}
	if (!bOn && !bLit) return;
	bLit = bOn;
	Flicker += Dt;
	TickFlames(bOn, LitAlpha);
	const float K = LitAlpha * CVarIntLight.GetValueOnGameThread();
	for (int32 i = 0; i < Lights.Num(); ++i)
	{
		UPointLightComponent* L = Lights[i];
		if (!L) continue;
		if (L->IsVisible() != bOn) L->SetVisibility(bOn);
		L->SetIntensity(LightBase[i] * K * LightFlicker[i]);
	}
}

namespace
{
	// Content/Data/InteriorFlames.csv: Mesh,X,Y,Z,Size: each fixture mesh's wicks in the mesh's own frame (cm, before the row's
	// scale) and the flame's height in cm (times the row's height scale). Read once.
	const TMap<FString, TArray<FVector4f>>& FlameTable()
	{
		static TMap<FString, TArray<FVector4f>> T;
		static bool bRead = false;
		if (bRead) return T;
		bRead = true;
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectContentDir() / TEXT("Data/InteriorFlames.csv")))) { UE_LOG(LogTemp, Warning, TEXT("JEEBINT no InteriorFlames.csv")); return T; }
		for (int32 n = 1; n < Lines.Num(); ++n)
		{
			FString Line = Lines[n];
			int32 H; if (Line.FindChar(TEXT('#'), H)) Line.LeftInline(H);
			TArray<FString> C; Line.ParseIntoArray(C, TEXT(","), false);
			if (C.Num() < 4 || C[0].TrimStartAndEnd().IsEmpty()) continue;
			T.FindOrAdd(C[0].TrimStartAndEnd()).Add(FVector4f(F(C, 1), F(C, 2), F(C, 3), F(C, 4, 3.f)));
		}
		return T;
	}

	// One flame's brightness now (about 1): a soft, quick waver, and now and then (every few seconds, each flame on its own) a
	// short gutter where it dips and flutters, as a real candle does in a draught. Time-based, on this machine only.
	float FlameNow(float T, float Ph, float Blink)
	{
		const float Waver = 0.06f * FMath::Sin(T * 8.3f + Ph) + 0.04f * FMath::Sin(T * 13.7f + 1.7f * Ph) + 0.05f * FMath::PerlinNoise1D(T * 5.f + Ph * 9.f);
		const float Gust = FMath::Clamp((FMath::PerlinNoise1D(T * 0.55f + Ph * 13.f) - 0.22f) * 4.f, 0.f, 1.f);
		const float Flutter = 0.55f + 0.45f * FMath::Sin(T * 27.f + Ph * 3.f);
		return FMath::Max(0.15f, 0.95f + Waver - 0.55f * Blink * Gust * Flutter);
	}
}

void AHouseInterior::AddFlames(const FRow& R, int32 LightIndex)
{
	const TArray<FVector4f>* Wicks = R.bMesh ? FlameTable().Find(R.Mesh) : nullptr;
	if (!Wicks || Wicks->Num() == 0) return;
	if (!FlameCones)
	{
		UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
		UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* ColorMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
		UMaterialInterface* HaloMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Flame.M_Flame"));
		auto Make = [this](UStaticMesh* M) {
			UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
			C->SetupAttachment(RootComponent);
			C->SetMobility(EComponentMobility::Movable);
			C->SetStaticMesh(M);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCastShadow(false);
			C->bAffectDynamicIndirectLighting = false;
			C->bAffectDistanceFieldLighting = false;
			C->SetCullDistances(0, 3000);
			C->RegisterComponent();
			C->SetWorldTransform(FTransform::Identity);
			return C;
		};
		FlameCones = Make(Cone);
		FlameHalos = Make(Ball);
		if (ColorMat) if (UMaterialInstanceDynamic* M = FlameCones->CreateDynamicMaterialInstance(0, ColorMat)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(9.f, 3.6f, 0.7f));
		if (HaloMat) if (UMaterialInstanceDynamic* M = FlameHalos->CreateDynamicMaterialInstance(0, HaloMat)) { M->SetScalarParameterValue(TEXT("Intensity"), 0.35f); M->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.f, 0.45f, 0.12f)); }
	}
	const FVector S = R.Xf.GetScale3D();
	FVector Sum = FVector::ZeroVector;
	for (const FVector4f& W : *Wicks)
	{
		FFlame Fl;
		Fl.At = R.Xf.TransformPosition(FVector(W.X, W.Y, W.Z));
		Fl.Size = W.W * FMath::Abs(S.Z);
		// (a phase from where it is, so it is the same every visit; each flame its own)
		Fl.Phase = FMath::Frac(FMath::Sin(Fl.At.X * 0.0137f + Fl.At.Y * 0.0291f + Fl.At.Z * 0.0517f) * 43758.5453f) * 6.2832f;
		Fl.Light = LightIndex;
		Flames.Add(Fl);
		Sum += Fl.At;
		FlameCones->AddInstance(FTransform(FQuat::Identity, Fl.At, FVector::ZeroVector), true);
		FlameHalos->AddInstance(FTransform(FQuat::Identity, Fl.At, FVector::ZeroVector), true);
	}
	// the fixture's real light sits among its flames (a little above, so the candles' own cups don't swallow it)
	if (Lights.IsValidIndex(LightIndex) && Lights[LightIndex]) Lights[LightIndex]->SetWorldLocation(Sum / Wicks->Num() + FVector(0.f, 0.f, 3.f));
}

void AHouseInterior::TickFlames(bool bOn, float Alpha)
{
	for (float& V : LightFlicker) V = 0.f;
	TArray<int32, TInlineAllocator<16>> Count; Count.SetNumZeroed(LightFlicker.Num());
	const float Blink = FMath::Max(0.f, CVarIntBlink.GetValueOnGameThread());
	const float Show = bOn ? Alpha * FMath::Max(0.f, CVarIntFlame.GetValueOnGameThread()) : 0.f;
	for (int32 i = 0; i < Flames.Num(); ++i)
	{
		const FFlame& Fl = Flames[i];
		const float B = FlameNow(Flicker, Fl.Phase, Blink);
		if (LightFlicker.IsValidIndex(Fl.Light)) { LightFlicker[Fl.Light] += B; ++Count[Fl.Light]; }
		// (Engine cone: 100 cm tall, its tip up, its base at the middle's -50; sphere: 100 cm across)
		const float H = Fl.Size * (0.75f + 0.25f * B) * Show, Wd = Fl.Size * 0.38f * Show;
		if (FlameCones) FlameCones->UpdateInstanceTransform(i, FTransform(FQuat::Identity, Fl.At + FVector(0.f, 0.f, H * 0.5f), FVector(Wd, Wd, H) * 0.01f), true, false, true);
		if (FlameHalos) FlameHalos->UpdateInstanceTransform(i, FTransform(FQuat::Identity, Fl.At + FVector(0.f, 0.f, Fl.Size * 0.5f), FVector(Fl.Size * 3.2f * (0.7f + 0.3f * B) * Show * 0.01f)), true, false, true);
	}
	if (FlameCones) FlameCones->MarkRenderStateDirty();
	if (FlameHalos) FlameHalos->MarkRenderStateDirty();
	// a light with no flames (the hearth) flickers by itself, a little deeper; one with flames follows their average
	for (int32 l = 0; l < LightFlicker.Num(); ++l)
		LightFlicker[l] = Count[l] > 0 ? LightFlicker[l] / Count[l] : 0.88f + 0.08f * FMath::Sin(Flicker * 9.f + l) + 0.07f * FMath::PerlinNoise1D(Flicker * 4.f + l * 5.f);
}

// Test helpers (not in a shipped game): jeeb.Test.GoHouse N (console, single player; 1 CLS1 behind the church, 2 CLS2, 3 CLN2, 4 NL1,
// 5 FL1, 6 SE1, 7 SE2, 8 SE3, 9 SE4, 10 SW2) or -StartAtOpenHouse (with -OpenHouse=<plot id>, else House_CLS1) puts him 2.5 m outside
// that house's door, facing it. -OpenHouseWalk then opens the door (E's request, at 6.5 s) and walks him in from 7 s for 4.5 s, through
// the door to the hearth's rug, logging where he is each half second (JEEBINT walk); -OpenHouseFilm also films his own camera every
// 4th frame meanwhile (Saved/Shots/<ShotName>_walk_NNN.png). -OpenHouseDoorFilm instead stands him outside and films the door
// opening at 6.5 s and shutting by itself (every 2nd frame from 6 s to 12 s, <ShotName>_door_NNN.png), the camera swaying a little.
static TAutoConsoleVariable<int32> CVarGoHouse(TEXT("jeeb.Test.GoHouse"), 0, TEXT("Test builds, single player: N puts you just outside a walk-in house's door (1 House_CLS1 behind the church, 2 CLS2, 3 CLN2, 4 NL1, 5 FL1, 6 SE1, 7 SE2, 8 SE3, 9 SE4, 10 SW2)."));
void AHouseInterior::TestTick(float Dt)
{
#if !UE_BUILD_SHIPPING
	if (GetNetMode() != NM_Standalone) return;
	APlayerController* Pc = GetWorld()->GetFirstPlayerController();
	APawn* Me = Pc ? Pc->GetPawn() : nullptr;
	if (!Me) return;
	TestClock += Dt;
	// (each room looks only at its own number, or its own name for the command line's start)
	static const TCHAR* Order[] = { TEXT("House_CLS1"), TEXT("House_CLS2"), TEXT("House_CLN2"), TEXT("House_NL1"), TEXT("House_FL1"), TEXT("House_SE1"), TEXT("House_SE2"), TEXT("House_SE3"), TEXT("House_SE4"), TEXT("House_SW2") };
	const int32 Go = CVarGoHouse.GetValueOnGameThread();
	const bool bGoMe = Go >= 1 && Go <= UE_ARRAY_COUNT(Order) && PlotId == Order[Go - 1];
	static const FString StartId = []() { FString V = TEXT("House_CLS1"); FParse::Value(FCommandLine::Get(), TEXT("OpenHouse="), V); return V; }();
	if (!bGoMe && PlotId != StartId) return;
	const FTransform Xf = GetActorTransform();
	const FVector Outside = Xf.TransformPosition(FVector(27.f, 520.f, 100.f));
	const FRotator In(0.f, Xf.Rotator().Yaw - 90.f, 0.f);   // (toward -Y in the house: in through the door)
	static const bool bDoorFilm = FParse::Param(FCommandLine::Get(), TEXT("OpenHouseDoorFilm"));
	static const bool bStart = FParse::Param(FCommandLine::Get(), TEXT("StartAtOpenHouse")) || FParse::Param(FCommandLine::Get(), TEXT("OpenHouseWalk")) || bDoorFilm;
	static bool bStarted = false;
	if (bGoMe || (bStart && !bStarted && PlotId == StartId && TestClock > 5.f))
	{
		CVarGoHouse->Set(0, ECVF_SetByConsole);
		bStarted = true;
		Me->SetActorLocation(bDoorFilm ? Xf.TransformPosition(FVector(260.f, 470.f, 100.f)) : Outside, false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorRotation(In);
		Pc->SetControlRotation(FRotator(-12.f, In.Yaw + (bDoorFilm ? -28.f : 0.f), 0.f));
		UE_LOG(LogTemp, Log, TEXT("JEEBINT test: put outside the door at %s facing %.0f"), *Outside.ToCompactString(), In.Yaw);
	}
	static const bool bWalk = FParse::Param(FCommandLine::Get(), TEXT("OpenHouseWalk"));
	static const bool bFilm = FParse::Param(FCommandLine::Get(), TEXT("OpenHouseFilm"));
	if ((!bWalk && !bDoorFilm) || !bStarted || PlotId != StartId) return;
	const float T = TestClock - 7.f;
	static bool bAsked = false;
	if (T > -0.5f && !bAsked)
	{
		bAsked = true;
		if (AKnightCharacter* K = Cast<AKnightCharacter>(Me)) K->Inv(EInvOp::HouseDoor, 0, 0, 0, nullptr, DoorKey());
		UE_LOG(LogTemp, Log, TEXT("JEEBDOOR test: asked to open %s; open %d, room %s"), *PlotId, bDoorOpen ? 1 : 0, bLoaded ? TEXT("loaded") : TEXT("not loaded"));
	}
	if (bDoorFilm)
	{
		if (T > 5.5f) { Pc->ConsoleCommand(TEXT("quit")); return; }
		if (T > -1.f)
		{
			Pc->SetControlRotation(FRotator(-10.f, In.Yaw - 28.f + 6.f * FMath::Sin(T * 2.f), 0.f));   // (a moving camera, for the smear check)
			static int32 FilmN = 0;
			if (GFrameCounter % 2 == 0)
			{
				FString Name = TEXT("house"); FParse::Value(FCommandLine::Get(), TEXT("ShotName="), Name);
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("%s_door_%03d.png"), *Name, FilmN++), false, false);
			}
			static int32 Q = -1;
			if (FMath::FloorToInt(T * 4.f) > Q) { Q = FMath::FloorToInt(T * 4.f); UE_LOG(LogTemp, Log, TEXT("JEEBDOOR film %.2f s: angle %.1f open %d"), T, DoorAngle, bDoorOpen ? 1 : 0); }
		}
		return;
	}
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
			UE_LOG(LogTemp, Log, TEXT("JEEBINT walk %.1f s: knight at house (%.0f, %.0f, %.0f), speed %.0f, camera at (%.0f, %.0f, %.0f) %s, arm share %.2f, door %.0f deg"), T, L.X, L.Y, L.Z,
				Me->GetVelocity().Size2D(), Cam.X, Cam.Y, Cam.Z, RoomLocal.IsInsideOrOn(Cam) ? TEXT("inside") : TEXT("outside"), CamScaleAt(GetWorld(), Me->GetActorLocation()), DoorAngle);
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

// Online door test (test builds): -HouseDoorNet on the server and every client (-OpenHouse=<plot id>, else House_CLS1). The server
// puts the first knight that joins 2.5 m outside that door 8 s after he arrives; that player's machine, started with -HouseDoorNetA,
// asks to open the door (the same request E sends) 2 s later and walks him in for 3.5 s. Every machine logs the door (JEEBDOOR) and
// whether it built the room (JEEBINT room ... loaded); a client quits itself after 70 s.
void AHouseInterior::NetTestTick(float Dt)
{
#if !UE_BUILD_SHIPPING
	static const bool bNet = FParse::Param(FCommandLine::Get(), TEXT("HouseDoorNet"));
	if (!bNet || GetNetMode() == NM_Standalone) return;
	static const FString StartId = []() { FString V = TEXT("House_CLS1"); FParse::Value(FCommandLine::Get(), TEXT("OpenHouse="), V); return V; }();
	if (PlotId != StartId) return;
	NetClock += Dt;
	const FTransform Xf = GetActorTransform();
	const FVector Outside = Xf.TransformPosition(FVector(27.f, 520.f, 100.f));
	if (GetNetMode() != NM_Client)
	{
		if (!NetKnight.IsValid())
		{
			for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It) if (It->GetController()) { NetKnight = *It; NetClock = 0.f; UE_LOG(LogTemp, Log, TEXT("JEEBDOOR net: first knight %s"), *It->GetName()); break; }
			return;
		}
		if (!bNetMoved && NetClock > 8.f)
		{
			bNetMoved = true;
			NetKnight->SetActorLocation(Outside, false, nullptr, ETeleportType::TeleportPhysics);
			NetKnight->SetActorRotation(FRotator(0.f, Xf.Rotator().Yaw - 90.f, 0.f));
			if (AController* C = NetKnight->GetController()) C->SetControlRotation(FRotator(-12.f, Xf.Rotator().Yaw - 90.f, 0.f));
			UE_LOG(LogTemp, Log, TEXT("JEEBDOOR net: moved %s outside %s's door"), *NetKnight->GetName(), *PlotId);
		}
		return;
	}
	APlayerController* Pc = GetWorld()->GetFirstPlayerController();
	APawn* Me = Pc ? Pc->GetPawn() : nullptr;
	static int32 LastSec = -1;
	if (FMath::FloorToInt(NetClock / 5.f) > LastSec)
	{
		LastSec = FMath::FloorToInt(NetClock / 5.f);
		UE_LOG(LogTemp, Log, TEXT("JEEBDOOR net %.0f s: door %s, room %s, my knight %.0f m from it"), NetClock, bDoorOpen ? TEXT("open") : TEXT("shut"), bLoaded ? TEXT("loaded") : TEXT("not loaded"),
			Me ? FVector::Dist(Me->GetActorLocation(), DoorHandle()) / 100.f : -1.f);
	}
	if (NetClock > 70.f) { if (Pc) Pc->ConsoleCommand(TEXT("quit")); return; }
	static const bool bA = FParse::Param(FCommandLine::Get(), TEXT("HouseDoorNetA"));
	if (!bA || !Me) return;
	if (NetAskAt < 0.f && FVector::Dist2D(Me->GetActorLocation(), Outside) < 120.f) { NetAskAt = NetClock + 2.f; UE_LOG(LogTemp, Log, TEXT("JEEBDOOR net A: outside the door")); }
	if (NetAskAt > 0.f && !bNetAsked && NetClock > NetAskAt)
	{
		bNetAsked = true;
		if (AKnightCharacter* K = Cast<AKnightCharacter>(Me)) K->Inv(EInvOp::HouseDoor, 0, 0, 0, nullptr, DoorKey());
		UE_LOG(LogTemp, Log, TEXT("JEEBDOOR net A: asked the server to open it"));
	}
	if (bNetAsked && NetClock > NetAskAt + 0.6f && NetClock < NetAskAt + 4.1f)
		Me->AddMovementInput(Xf.TransformVectorNoScale(FVector(0.f, -1.f, 0.f)), 0.5f);
#endif
}
