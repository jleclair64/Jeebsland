#pragma once

#include "CoreMinimal.h"

// The open world outside Oak Hill and its hills (owner, 2026-11-21: "I want the mountains into the north to be twice as tall, and I want
// them to have snow ... add roads and trees to the rest of the map ... For the towns, just place a house where we will eventually build
// the towns as a placeholder ... review the topography of the map outside oak hill and redo it. I want it to look something like the
// skyrim and hogwarts and breath of the wild topography"). Docs/OpenWorldPlan.md has the research and the plan.
//
//  - The land: one grid over the whole world on the 8 m ground mesh's own points, added to the land like JeebHills (Terrain.h HeightAt).
//    North of Oak Hill's hills the old 48 m range is replaced by one about 100 m tall with rounded summits, saddles, spurs, foothills and
//    a broad trough where the river runs; elsewhere outside the hills, rolling country. Slopes are cut to 35 degrees except a few rock
//    bands (climbable), convex places are eased so nothing ends in a point. Zero in Oak Hill, the hills round it and Steele Creek, the
//    original square and the level town sites.
//  - The roads: from the forest roads' ends on to Silver Point and Anchorage, Steele Creek to Birch Grove, Birch Grove to Palmer Square
//    and north to the river; a least-climb search over the finished ground, levelled to 5 degrees with banks. JeebTerrain::PathDefs makes
//    them ordinary forest roads.
//  - Where woods grow (Woods), where snow lies (Snow), when it snows (Snowing), the placeholder town sites (Sites).
// Everything is worked out once from fixed numbers, the same on the server and every player's machine: log JEEBOPEN (hashes).
namespace JeebOpen
{
	constexpr float PeakTop = 10000.f;     // cm: about the height of the tallest summit (the height rules for trees and snow are shares of it)
	constexpr float SnowFrom = 4500.f, SnowTo = 7500.f;   // cm: where snow starts to lie in patches, and where it is solid (M_Ground: Tools/world_material.py)
	constexpr float Treeline = 0.64f;      // of PeakTop: no trees above (ragged by noise)

	// cm added to the land at a point (from the grid, laid flat across the world mesh's triangles so it adds nothing the 8 m ground can't draw)
	float Offset(float X, float Y);
	// Builds the grid and the roads now (BuildWorld calls it on the game thread, after JeebHills::Prepare) and logs JEEBOPEN
	void Prepare();
	// Logs the finished ground's heights and slopes and each road's length and steepest climb; after the world's ground exists
	void LogStats();
	// The new roads' centre lines (a point every 60 cm). Never calls JeebTerrain::PathDefs.
	const TArray<TArray<FVector2D>>& RoadLines();

	// 0 .. 1: how wooded the land is at a point before height, water and roads are counted (0.03 open plain with the odd lone tree, about
	// 0.5 intermittent groves, 1 deep forest)
	float Woods(float X, float Y);
	// 0 .. 1: how much snow lies on ground at height Z whose normal's Z is Up (as M_Ground paints it, without its fine noise)
	float Snow(float X, float Y, float Z, float Up);
	// the same before it is clamped (under 0 none, over about 0.8 solid): AWorldTerrain gives it to M_Ground in the ground's second UV, which adds its fine noise
	float SnowRaw(float X, float Y, float Z, float Up);
	// 0 .. 1: the snow on the world's ground at a spot (Snow at the ground's own height and slope)
	float SnowOnGround(float X, float Y);
	// cm a character's drawn body sinks into the snow when its feet stand at Feet on the open ground (0 off the snow, in the air, on
	// rock or roofs); a pure function of the position, so every machine draws the same (2026-11-22, owner: feet sink into the snow)
	constexpr float SinkDepth = 13.f;
	float SnowSink(const FVector& Feet);
	// 0 .. 1: the weather of a moment of the shared clock (AGameStateBase::GetServerWorldTimeSeconds): 1 a storm day, 0 blue skies; one per game day
	float Snowing(double ServerSeconds);   // (2026-11-26: one weather per game day, see OpenWorld.cpp)
	float StormAlt(float Z);   // 0 .. 1 by the camera's height: the storm is on the mountain tops only

	// The towns still to be built: a placeholder house each, and the name shown on walking in
	struct FSite { const TCHAR* Name; FVector2D Pos; float Radius; FVector2D House; float HouseYaw; };
	const TArray<FSite>& Sites();
}
