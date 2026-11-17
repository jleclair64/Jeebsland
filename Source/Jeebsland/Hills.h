#pragma once

#include "CoreMinimal.h"

// The forested hills round Oak Hill (owner, 2026-11-16: "add hills similar to the hills we have in the oak hill town and apply them to the
// full surrounding area where we added trees ... having some hills to cut off the horizon can help make the grass and tree shadow issue
// less noticeable").
//
// Broad, smooth hills over the forest's ring (JeebOakForest::Reach round the plaza), rising away from the town: a few metres high at the
// town's fringe, about 15-37 m over the land out at 300-500 m, with valleys and saddles from two slow noises, fading back to the world's
// own land past the forest's edge. They keep off the town's ground (its box + 30 m), the original square (+ 15 m), the rivers and their
// banks (+ 30 m from the water, full height from 150-220 m) and, in the TownLab map, its lab site; each edge blends smoothly.
// The forest roads (OakForest.cpp RoadLines) are routed on these hills; then each road's corridor is levelled to a profile no steeper
// than RoadGrade (5 degrees, the hills and the land together) with banks that ease back into the hills.
//
// JeebTerrain::HeightAt adds Offset() to the land before the rivers and the sea, so every river keeps its channel and its banks.
// Worked out once per process into a 4 m grid, deterministic (every machine the same: online depends on it); log JEEBHILLS hash.
// jeeb.Hills.Scale (default 1) scales the height; read once when the world is first built, so set it before loading (Config or the
// command line) and give the server and every player the same value.
namespace JeebHills
{
	constexpr float GridHalf = 64000.f;   // cm round the plaza covered by the grid (the hills are 0 from 62 m inside its edge)
	constexpr float GridStep = 400.f;
	constexpr float RoadGrade = 0.0875f;  // tan 5 degrees: the steepest a forest road climbs
	// cm: the hills before the forest roads' corridors are levelled (what the roads are routed on)
	float Raw(float X, float Y);
	// cm: the hills as built (Raw with the forest roads levelled), from the grid
	float Offset(float X, float Y);
	// 0 .. 1: how much of the hills a point takes (the town, the square, the rivers, the rim)
	float KeepOut(float X, float Y);
	// Builds the grid now (BuildWorld calls it first, on the game thread) and logs JEEBHILLS
	void Prepare();
	// Logs the finished ground's heights and slopes over the ring (JEEBHILLS stats); after the world's ground exists
	void LogStats();
	float Scale();
}
