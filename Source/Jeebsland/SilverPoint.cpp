#include "SilverPoint.h"

// (Terrain.h asks this for its ground cover; SilverPoint.h includes Terrain.h, so it is defined here. 2026-11-27: the cobbles too, so
// no grass, flowers or snow grow on the plaza and lanes)
bool JeebTerrain::JeebSilverOnBuilding(float X, float Y) { return JeebSilver::OnBuilding(X, Y) || (JeebSilver::Near(X, Y, 1000.f) && JeebSilver::StreetClearance(X, Y) < 40.f); }
