/*
 * Copyright (C) 2016+     AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "DetourExtended.h"
#include "DetourCommon.h"
#include "Geometry.h"
#include "MapDefines.h"
#include <cmath>

bool dtQueryFilterExt::passFilter(const dtPolyRef ref, dtMeshTile const* tile, dtPoly const* poly) const
{
    if (!dtQueryFilter::passFilter(ref, tile, poly))
        return false;

    if (_minGroundNormalUp <= 0.0f || poly->getType() != DT_POLYTYPE_GROUND || poly->getArea() != NAV_GROUND)
        return true;

    // Newell normal of the polygon; Detour's y axis points up.
    float normal[3] = { 0.0f, 0.0f, 0.0f };
    for (unsigned char i = 0; i < poly->vertCount; ++i)
    {
        float const* a = &tile->verts[poly->verts[i] * 3];
        float const* b = &tile->verts[poly->verts[(i + 1) % poly->vertCount] * 3];
        normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
        normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
        normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }

    float const length = dtVlen(normal);
    return length <= 0.0f || std::fabs(normal[1]) >= _minGroundNormalUp * length;
}

float dtQueryFilterExt::getCost(float const* pa, float const* pb,
                const dtPolyRef /*prevRef*/, dtMeshTile const* /*prevTile*/, dtPoly const* /*prevPoly*/,
                const dtPolyRef /*curRef*/, dtMeshTile const* /*curTile*/, dtPoly const* curPoly,
                const dtPolyRef /*nextRef*/, dtMeshTile const* /*nextTile*/, dtPoly const* /*nextPoly*/) const
{
    float startX = pa[2], startY = pa[0], startZ = pa[1];
    float destX = pb[2], destY = pb[0], destZ = pb[1];
    float slopeAngle = getSlopeAngle(startX, startY, startZ, destX, destY, destZ);
    float slopeAngleDegree = (slopeAngle * 180.0f / M_PI);
    float cost = slopeAngleDegree > 0 ? 1.0f + (1.0f * (slopeAngleDegree / 100)) : 1.0f;
    float dist = dtVdist(pa, pb);
    auto totalCost = dist * cost * getAreaCost(curPoly->getArea());
    return totalCost;
}
