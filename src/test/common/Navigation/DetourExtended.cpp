/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "DetourExtended.h"
#include "MapDefines.h"
#include "gtest/gtest.h"

#include <cmath>

namespace
{
    float constexpr PlayerWalkableNormalUp = 0.64278764f;

    struct SlopedTriangle
    {
        float verts[9];
        // Detour keeps tiles in zeroed raw memory; dtMeshTile has no constructor to call.
        alignas(dtMeshTile) unsigned char tileStorage[sizeof(dtMeshTile)]{};
        dtMeshTile& tile = *reinterpret_cast<dtMeshTile*>(tileStorage);
        dtPoly poly{};

        SlopedTriangle(float degrees, unsigned short terrain)
        {
            float const rise = std::tan(degrees * float(M_PI) / 180.0f) * 10.0f;
            float const corners[9] = { 0.0f, 0.0f, 0.0f, 10.0f, rise, 0.0f, 0.0f, 0.0f, 10.0f };
            for (int i = 0; i < 9; ++i)
                verts[i] = corners[i];

            tile.verts = verts;
            poly.verts[0] = 0;
            poly.verts[1] = 1;
            poly.verts[2] = 2;
            poly.vertCount = 3;
            poly.flags = terrain;
            poly.setArea(static_cast<unsigned char>(terrain));
            poly.setType(DT_POLYTYPE_GROUND);
        }

        bool PassesFor(dtQueryFilterExt const& filter) const { return filter.passFilter(1, &tile, &poly); }
    };
}

TEST(DetourExtended, KeepsEverySlopeTheMeshAllowsByDefault)
{
    dtQueryFilterExt filter;
    SlopedTriangle const cliff(58.0f, NAV_GROUND);

    EXPECT_TRUE(cliff.PassesFor(filter));
}

TEST(DetourExtended, SkipsGroundSteeperThanThePlayerWalkableSlope)
{
    dtQueryFilterExt filter;
    filter.setMinGroundNormalUp(PlayerWalkableNormalUp);
    SlopedTriangle const hill(45.0f, NAV_GROUND);
    SlopedTriangle const mountainside(55.0f, NAV_GROUND);

    EXPECT_TRUE(hill.PassesFor(filter));
    EXPECT_FALSE(mountainside.PassesFor(filter));
}

TEST(DetourExtended, LeavesWaterAndExcludedTerrainToTheFlags)
{
    dtQueryFilterExt filter;
    filter.setMinGroundNormalUp(PlayerWalkableNormalUp);
    SlopedTriangle const waterfall(70.0f, NAV_WATER);
    SlopedTriangle const flat(0.0f, NAV_GROUND);
    filter.setExcludeFlags(NAV_GROUND);

    EXPECT_TRUE(waterfall.PassesFor(filter));
    EXPECT_FALSE(flat.PassesFor(filter));
}
