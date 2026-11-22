#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// The stone bridge over the river between the old village site and Oak Hill (2026-11-18). This header is the part the land and the roads need
// (Terrain.h reads it): where the bridge stands, what it does to the ground round its ends and piers, and the roads that lead to it.
// The bridge itself (its model and its collision) is ABridge in Bridge.h/.cpp. Everything here is a fixed function of fixed numbers, so
// the server and every client agree; the numbers live in Bridge.cpp (so changing one doesn't rebuild the world).
namespace JeebBridge
{
	// Off only for comparison pictures: -NoBridge with -JeebShot (never in a shipped game)
	inline bool Enabled()
	{
#if UE_BUILD_SHIPPING
		return true;
#else
		static const bool bOn = !(FParse::Param(FCommandLine::Get(), TEXT("NoBridge")) && FParse::Param(FCommandLine::Get(), TEXT("JeebShot")));
		return bOn;
#endif
	}

	// A generous box round everything the bridge changes in the ground (cm); outside it nothing here costs more than this test
	constexpr float BoxX0 = -5000.f, BoxX1 = -2100.f, BoxY0 = 9000.f, BoxY1 = 15000.f;
	inline bool InBox(float X, float Y) { return X > BoxX0 && X < BoxX1 && Y > BoxY0 && Y < BoxY1; }

	// The ground after the bridge's embankments and pier footings (Bridge.cpp); HeightAt ends with it
	float ApplyGround(float X, float Y, float H);

	// The roads that lead to the bridge, as points before smoothing (Bridge.cpp). Kind 3: the the old village site approach (a drawn ribbon of
	// packed earth); Kind 4: the road over the span itself (keeps grass and trees away and is on the map, but draws no ribbon)
	struct FRoad { TArray<FVector2D> Pts; uint8 Kind = 3; float Width = 300.f; };
	const TArray<FRoad>& Roads();
}
