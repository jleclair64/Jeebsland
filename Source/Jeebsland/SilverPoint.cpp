#include "SilverPoint.h"

// (Terrain.h asks this for its ground cover; SilverPoint.h includes Terrain.h, so it is defined here)
bool JeebTerrain::JeebSilverOnBuilding(float X, float Y) { return JeebSilver::OnBuilding(X, Y); }
