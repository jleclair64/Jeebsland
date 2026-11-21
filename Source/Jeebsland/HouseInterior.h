#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HouseInterior.generated.h"

class UPointLightComponent;
class APostProcessVolume;

// The walk-in thatched house (2026-11-21, owner: House_CLS1 behind the church; Docs/TownLab/InteriorPlan.md). ATownLabTown draws that
// plot with SM_TL_HouseOpen (the doorway cut through the shell and its solid copy, Tools/interior_house_open.py) and spawns one of
// these at the house's own transform (yaw and place, not its scale). Everything in the room comes from Content/Data/InteriorHouse.csv
// (mesh, spot in the house's frame in cm from the floor at the model's middle, yaw, scale, solid, shadow, light): the room shell
// (Stylized Medieval Tavern floor, walls and beams), the furniture and props, and the lights.
//  - Fixed geometry built the same on every machine (JEEBINT hash in the log); the solid pieces collide on the server too (a box from
//    each mesh's bounds), so the server decides where he walks.
//  - The lights (one shadowed fire, a few shadowless candles and lanterns) are on only while the local player is within
//    jeeb.Interior.LightReach of the door; a post-process box inside lifts the exposure (jeeb.Interior.Exposure).
//  - The third-person camera: KnightCharacterBow.cpp shortens the arm while he stands inside (CamScaleAt; jeeb.Interior.Cam).
UCLASS()
class AHouseInterior : public AActor
{
	GENERATED_BODY()

public:
	AHouseInterior();
	// The camera arm's share for someone standing at World: 1 outside every open house, jeeb.Interior.Cam inside one
	static float CamScaleAt(const UWorld* World, const FVector& At);
	// The open house's plot Id (only this one gets SM_TL_HouseOpen and a room)
	static const TCHAR* OpenPlotId() { return TEXT("House_CLS1"); }
	// The house's floor height above its foot, in the house's frame (cm): the props' Z is from here
	float FloorZ = 0.f;
	uint32 Hash = 2166136261u;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Why) override;
	virtual void Tick(float Dt) override;

private:
	void Build();
	void TestTick(float Dt);
	float TestClock = 0.f;
	UPROPERTY() TArray<UPointLightComponent*> Lights;
	TArray<float> LightBase;
	UPROPERTY() APostProcessVolume* Post = nullptr;
	FBox RoomLocal = FBox(ForceInit);   // the room inside the walls, in the house's frame
	float Flicker = 0.f;
	bool bLit = false;
	float LitAlpha = 0.f;
};
