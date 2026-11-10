/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#ifndef MOD_SUBCLASS_QUEST_ROUTE_H
#define MOD_SUBCLASS_QUEST_ROUTE_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Subclass
{
struct RouteStop
{
    uint32_t Quest = 0;
    float X = 0.0f;
    float Y = 0.0f;
};

inline float RouteDistance(float ax, float ay, float bx, float by)
{
    return std::hypot(ax - bx, ay - by);
}

inline float RouteLength(float fromX, float fromY, std::vector<RouteStop> const& stops)
{
    float length = 0.0f;
    float x = fromX;
    float y = fromY;
    for (RouteStop const& stop : stops)
    {
        length += RouteDistance(x, y, stop.X, stop.Y);
        x = stop.X;
        y = stop.Y;
    }
    return length;
}

inline std::vector<RouteStop> NearestFirst(float fromX, float fromY, std::vector<RouteStop> stops)
{
    std::vector<RouteStop> route;
    route.reserve(stops.size());
    float x = fromX;
    float y = fromY;
    while (!stops.empty())
    {
        std::size_t nearest = 0;
        for (std::size_t i = 1; i < stops.size(); ++i)
            if (RouteDistance(x, y, stops[i].X, stops[i].Y) < RouteDistance(x, y, stops[nearest].X, stops[nearest].Y))
                nearest = i;
        route.push_back(stops[nearest]);
        x = stops[nearest].X;
        y = stops[nearest].Y;
        stops.erase(stops.begin() + nearest);
    }
    return route;
}

inline std::vector<RouteStop> Untangled(float fromX, float fromY, std::vector<RouteStop> route)
{
    constexpr int MaxPasses = 50;
    constexpr float Gain = 0.01f;
    std::size_t const count = route.size();
    for (int pass = 0; pass < MaxPasses; ++pass)
    {
        bool improved = false;
        for (std::size_t first = 0; first + 1 < count; ++first)
        {
            float const beforeX = first ? route[first - 1].X : fromX;
            float const beforeY = first ? route[first - 1].Y : fromY;
            for (std::size_t last = first + 1; last < count; ++last)
            {
                float removed = RouteDistance(beforeX, beforeY, route[first].X, route[first].Y);
                float added = RouteDistance(beforeX, beforeY, route[last].X, route[last].Y);
                if (last + 1 < count)
                {
                    removed += RouteDistance(route[last].X, route[last].Y, route[last + 1].X, route[last + 1].Y);
                    added += RouteDistance(route[first].X, route[first].Y, route[last + 1].X, route[last + 1].Y);
                }
                if (added + Gain < removed)
                {
                    std::reverse(route.begin() + first, route.begin() + last + 1);
                    improved = true;
                }
            }
        }
        if (!improved)
            break;
    }
    return route;
}

inline std::vector<uint32_t> PlanQuestRoute(float fromX, float fromY, std::vector<RouteStop> stops)
{
    std::vector<uint32_t> quests;
    for (RouteStop const& stop : Untangled(fromX, fromY, NearestFirst(fromX, fromY, std::move(stops))))
        quests.push_back(stop.Quest);
    return quests;
}
}

#endif
