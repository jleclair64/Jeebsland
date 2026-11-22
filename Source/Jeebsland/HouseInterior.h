#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HouseInterior.generated.h"

class UPointLightComponent;
class APostProcessVolume;
class UStaticMesh;
class UStaticMeshComponent;
class UBoxComponent;
class APawn;

// The walk-in thatched house (2026-11-21, owner: House_CLS1 behind the church; Docs/TownLab/InteriorPlan.md). ATownLabTown draws that
// plot with SM_TL_HouseOpen (the doorway cut through the shell and its solid copy, Tools/interior_house_open.py) and spawns one of
// these at the house's own transform (yaw and place, not its scale). Everything in the room comes from that house's own list,
// Content/Data/Interior_<PlotId>.csv (mesh, spot in the house's frame in cm from the floor at the model's middle, yaw, scale, solid,
// shadow, light): the room shell (Stylized Medieval Tavern floor, walls and beams, the hearth), the furniture and props, and the
// lights. Tools/interior_rooms.py writes all ten Oak Hill lists (the shell they share and each house's furnishing; 2026-11-21).
//  - Fixed geometry built the same on every machine (JEEBINT hash in the log); the solid pieces collide on the server too (a box from
//    each mesh's bounds), so the server decides where he walks.
//  - The lights (one shadowed fire, a few shadowless candles and lanterns) are on only while the local player is within
//    jeeb.Interior.LightReach of the door; a post-process box inside lifts the exposure (jeeb.Interior.Exposure).
//  - The door (2026-11-21, agent doors): a real door in the doorway (the tavern pack's SM_Door_001), shut by default, opened and
//    shut with E. The server decides (EInvOp::HouseDoor, range checked), the state goes to everyone through AKnightGameState's Doors
//    list, and every machine swings its own copy. The server shuts it by itself jeeb.Interior.DoorClose seconds after the doorway
//    is clear, and it is solid while shut.
//  - On demand: the room's meshes, lights and exposure box exist only on a player's machine while that player's own knight (or
//    camera) is within jeeb.Interior.LoadReach of the door, built on the way in so nothing pops when the door opens, and removed
//    again past jeeb.Interior.UnloadReach. A dedicated server never builds them; every machine keeps the solid boxes and the door.
//  - The third-person camera: KnightCharacterBow.cpp shortens the arm while he stands inside (CamScaleAt; jeeb.Interior.Cam).
UCLASS()
class AHouseInterior : public AActor
{
	GENERATED_BODY()

public:
	AHouseInterior();
	// The camera arm's share for someone standing at World: 1 outside every open house, jeeb.Interior.Cam inside one
	static float CamScaleAt(const UWorld* World, const FVector& At);
	// Whether this plot's thatched house is a walk-in one: it has a room list, Content/Data/Interior_<PlotId>.csv (2026-11-21: every
	// thatched house in Oak Hill)
	static bool HasRoom(const TCHAR* PlotId);
	// The open-house door within E reach of this knight (the doorway's middle within 2 m, either side), or null
	static AHouseInterior* DoorInReach(const AActor* Knight, bool bFacing = false);   // bFacing: only if he faces it
	// Server: open or shut it (false, refused: shutting it on someone standing in its swing)
	bool ToggleDoor();
	// From the Doors list (players' machines)
	void SetDoorOpen(bool bOpen);
	bool IsDoorOpen() const { return bDoorOpen; }
	// The door's hinge in the world (its key in the Doors list) and the doorway's middle at handle height (the E prompt)
	FVector DoorKey() const;
	FVector DoorHandle() const;
	bool IsRoomLoaded() const { return bLoaded; }
	// Set by ATownLabTown before FinishSpawning: which house this is
	FString PlotId;
	// The house's floor height above its foot, in the house's frame (cm): the props' Z is from here
	float FloorZ = 0.f;
	uint32 Hash = 2166136261u;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Why) override;
	virtual void Tick(float Dt) override;

private:
	void Build();
	void BuildFrom(const FString& File, int32& Pieces, int32& Solids, int32& Missing);
	void TestTick(float Dt);
	void NetTestTick(float Dt);
	void LoadRoom();
	void UnloadRoom();
	void BuildDoor();
	void TickDoor(float Dt);
	bool SomeoneIn(const FBox& LocalZone) const;
	struct FRow { FString Mesh, Mat; FTransform Xf; bool bShadow = false, bFire = false, bMesh = true; float Lm = 0.f, Rad = 350.f; FVector LightAt = FVector::ZeroVector; };
	TArray<FRow> Rows;
	UPROPERTY() TArray<UStaticMeshComponent*> Pieces;
	UPROPERTY() TMap<FString, UStaticMesh*> MeshCache;   // (kept after the first visit, so coming back needs no loading)
	UPROPERTY() USceneComponent* DoorPivot = nullptr;
	UPROPERTY() UStaticMeshComponent* DoorMesh = nullptr;
	UPROPERTY() UBoxComponent* DoorSolid = nullptr;
	UPROPERTY() UStaticMeshComponent* Backdrop = nullptr;
	bool bDoorOpen = false;
	float DoorAngle = 0.f;
	float DoorOpenFor = 0.f, DoorClearFor = 0.f;
	bool bLoaded = false;
	float NetClock = 0.f, NetAskAt = -1.f;
	bool bNetMoved = false, bNetAsked = false;
	TWeakObjectPtr<APawn> NetKnight;
	float TestClock = 0.f;
	UPROPERTY() TArray<UPointLightComponent*> Lights;
	TArray<float> LightBase;
	UPROPERTY() APostProcessVolume* Post = nullptr;
	FBox RoomLocal = FBox(ForceInit);   // the room inside the walls, in the house's frame
	float Flicker = 0.f;
	bool bLit = false;
	float LitAlpha = 0.f;
};
