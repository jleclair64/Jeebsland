#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TownLabDirector.generated.h"

class ACameraActor;

// TownLab's switches and measuring tour (2026-11-14, the test map only; Docs/TownLab/HowToSee.md):
//   jeeb.TownLab.NewLook 1/0      the new light and atmosphere (golden haze, aerial perspective, light shafts, lifted tinted
//                                  shadows, a painterly grade) or the game's own; lighting is global, so both towns take it
//   jeeb.TownLab.CheapShadows -1/0/1, jeeb.TownLab.CheapClouds -1/0/1   (-1: follow NewLook) the cheaper sun shadows and clouds
//   jeeb.TownLab.ShowOld / ShowNew 1/0   hide a town's buildings and ground (its trees and grass stay; for a clean frame-rate
//                                  number use -JeebShot -NoOakHill or -NoLabNew, which leave the whole town out)
// -LabTour[=stats]: stands at each station in turn (both towns: plaza, street, town edge, from the hill; noon and low sun),
// logs a JEEBLAB STAT line of medians (frame, game, render, RHI and GPU ms, draws, triangles) and, unless =stats, saves a picture.
UCLASS()
class ATownLab : public AActor
{
	GENERATED_BODY()

public:
	ATownLab();
	virtual void Tick(float DeltaSeconds) override;
	// Where players appear in TownLab: between the two towns, north of them, looking south so both are in view
	static FVector SpawnPoint(float& OutYaw);

protected:
	virtual void BeginPlay() override;

private:
	int32 LookNow = -1, ShadowsNow = -1, CloudsNow = -1, ShowOldNow = -1, ShowNewNow = -1;
	TMap<FString, FString> Saved;   // each console variable's value before TownLab first changed it
	void SetVar(const TCHAR* Name, const FString& Value);
	void RestoreVar(const TCHAR* Name);
	void ApplyLook(bool bNew);
	void ApplyShadows(bool bCheap);
	void ApplyClouds(bool bCheap);
	void ShowTown(bool bOld, bool bShow);
	// (retired 2026-11-14: M_Ground carries the lab copy's look itself)
	UPROPERTY() class UMaterialInstanceDynamic* WorldMid = nullptr;
	float WorldSweep = 0.f, WorldFillNow = -1.f;
	int32 WorldSwapped = 0;
	void TickWorldGround(float Dt);

	// the tour
	struct FStation { FString Name; int32 Town; FVector Eye; float Yaw, Pitch, Phase; int32 Look, Cheap; bool bShot; };
	TArray<FStation> Stations;
	int32 StationAt = -1;
	float StationClock = 0.f, TourClock = 0.f;
	bool bTour = false, bTourShots = true;
	TArray<float> SFrame, SGame, SRender, SRhi, SGpu, SDraws, SPrims;
	UPROPERTY() ACameraActor* Eye = nullptr;
	void BuildTour();
	void StartStation(int32 i);
	void TickTour(float Dt);
};
