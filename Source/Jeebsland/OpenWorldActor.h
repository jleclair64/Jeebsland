#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpenWorldActor.generated.h"

class UInstancedStaticMeshComponent;
class UPointLightComponent;

// The open world's two visible pieces that are not ground, roads or trees (OpenWorld.h has those):
//  - a placeholder house at each town still to be built (JeebOpen::Sites: Silver Point, Anchorage, Birch Grove, Palmer Square), the
//    TownLab house with its solid shape, on every machine (the server decides where players walk); log JEEBOPEN houses hash;
//  - the snowfall (owner, 2026-11-21: "replace the falling leave animation with a falling snow animation that's sometimes clear and
//    sometimes constantly snowing (not intermittent like the leaves). I want there to be more snow the higher up you go"): flakes in a
//    box that follows the camera, players' machines only. Whether it snows comes from the shared clock (JeebOpen::Snowing), so every
//    player sees the same weather; how many flakes fall grows with the camera's height. Each flake carries its previous-frame
//    transform, so TSR has motion vectors and nothing smears.
UCLASS()
class AOpenWorld : public AActor
{
	GENERATED_BODY()

public:
	AOpenWorld();
	virtual void Tick(float DeltaSeconds) override;

	static float SnowNow;   // 0 clear .. 1 snowing: the weather this frame (for anything else that wants to follow it)
	static float StormNow;   // 0 .. 1: the storm at this machine's camera (the day's weather x the mountain-top height gate)
	static float SilverMistNow;   // (2026-11-28) 0 .. 1: Silver Point's summit fog round this machine's camera (KnightGameState thickens the height fog with it)

protected:
	virtual void BeginPlay() override;

private:
	void BuildHouses();
	// Floating candles over and round Silver Point (owner, 2026-11-22: "floating candles that light up (similar to Harry Potter)"): a few
	// dozen slim cream candles hovering 2.5-6 m up, each with a flickering flame and halo, bobbing and drifting a little. The layout comes
	// from a fixed seed (every machine the same); the bobbing and flicker are local looks only. Three instanced meshes, a handful of
	// shadowless real lights on the candles nearest the camera, the lot lit by the night (dim flames by day).
	void BuildCandles();
	void BuildSilverGround();   // Silver Point's cobbled plaza and lanes (2026-11-27)
	UPROPERTY() class UProceduralMeshComponent* SilverGround = nullptr;
	UPROPERTY() class UMaterialInstanceDynamic* SilverGroundMid = nullptr;
	// (2026-11-27) the summit's own fog, coming and going on JeebOpen::SilverWeather's schedule (its snowfall uses the flakes below)
	UPROPERTY() class ULocalFogVolumeComponent* SilverFog = nullptr;
	float SilverFogNow = -1.f, SilverSnowNow = 0.f;
	// (round 2, 2026-11-28: the local fog volume never showed) the fog as banks of mist: soft cloud cards (M_Cumulus) hugging the upper
	// slopes and drifting over the top, turned to face the camera every frame, one mesh
	UPROPERTY() class UProceduralMeshComponent* SilverMist = nullptr;
	UPROPERTY() class UMaterialInstanceDynamic* SilverMistMid = nullptr;
	struct FMistCard { float Phi, R, Z, Size, Speed, Phase; int32 Tile; bool bFlip; };
	TArray<FMistCard> MistCards;
	float MistShown = -1.f;
	void TickSilverMist(float Fog, const FVector& Cam);
	void TickSilverWeather(float Dt, double Clock, const FVector& Cam);
	void TickRoadWalk(float Dt);   // -SilverRoadWalk test (not in Shipping)
	UPROPERTY() TArray<class UMaterialInstanceDynamic*> GlowMids;   // the village buildings' windows (2026-11-27)
	UPROPERTY() TArray<class UMaterialInterface*> GlowMats;
	TArray<float> GlowBase;
	float LastDusk = -1.f;
	void TickCandles(float Dt, const FVector& Cam);
	struct FCandle { FVector Home; float Bob, BobRate, BobPhase, Drift, DriftRate, DriftPhase, Height, Phase, Lean; };
	TArray<FCandle> Candle;
	FVector2D CandleSite = FVector2D::ZeroVector;
	UPROPERTY() UInstancedStaticMeshComponent* CandleBody = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* CandleFlame = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* CandleHalo = nullptr;
	UPROPERTY() TArray<UPointLightComponent*> CandleLights;
	TArray<FTransform> CandleNow[3], CandlePrev[3];
	bool bCandleSeen = false;
	UPROPERTY() UInstancedStaticMeshComponent* Flakes = nullptr;
	struct FFlake { FVector Pos; float Fall, Phase, Size, Spin; bool bUnderRoof; };
	TArray<FFlake> Flake;
	TArray<FTransform> NewT, PrevT;
	TArray<uint8> Seen;       // (each flake drawn last frame: its previous transform is real)
	FRandomStream Rand;
	float Time = 0.f;
	int32 Shown = 0;
	float CoverWant = 0.f, CoverNow = 0.f, CoverAge = 99.f;   // how much snow lies round the camera: it only snows where snow lies (2026-11-22)
};
