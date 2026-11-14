/*
 * Copyright (C) 2016+     AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#ifndef _ACORE_DETOUR_EXTENDED_H
#define _ACORE_DETOUR_EXTENDED_H

#include "DetourNavMeshQuery.h"

class dtQueryFilterExt: public dtQueryFilter
{
public:
    // Ground polygons whose up normal is below this are skipped; 0 keeps every slope the mesh allows.
    void setMinGroundNormalUp(float minNormalUp) { _minGroundNormalUp = minNormalUp; }

    bool passFilter(const dtPolyRef ref, dtMeshTile const* tile, dtPoly const* poly) const override;

    float getCost(float const* pa, float const* pb,
        const dtPolyRef prevRef, dtMeshTile const* prevTile, dtPoly const* prevPoly,
        const dtPolyRef curRef, dtMeshTile const* curTile, dtPoly const* curPoly,
        const dtPolyRef nextRef, dtMeshTile const* nextTile, dtPoly const* nextPoly) const override;

private:
    float _minGroundNormalUp = 0.0f;
};

#endif // _ACORE_DETOUR_EXTENDED_H
