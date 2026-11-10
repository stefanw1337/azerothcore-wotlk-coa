/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "SubclassQuestRoute.h"
#include <gtest/gtest.h>

using namespace Subclass;

TEST(QuestRoute, NoQuestsMakeNoRoute)
{
    EXPECT_TRUE(PlanQuestRoute(0.0f, 0.0f, {}).empty());
}

TEST(QuestRoute, TheNearestQuestComesFirst)
{
    std::vector<uint32_t> const route = PlanQuestRoute(0.0f, 0.0f, { { 1, 300.0f, 0.0f }, { 2, 50.0f, 0.0f } });
    ASSERT_EQ(route.size(), 2u);
    EXPECT_EQ(route.front(), 2u);
}

TEST(QuestRoute, EveryQuestOfOneAreaComesBeforeTheNextArea)
{
    std::vector<RouteStop> const stops = { { 1, 40.0f, 10.0f }, { 2, 900.0f, 20.0f }, { 3, 60.0f, -30.0f },
        { 4, 880.0f, -10.0f }, { 5, 20.0f, 50.0f }, { 6, 920.0f, 40.0f } };
    std::vector<uint32_t> const route = PlanQuestRoute(0.0f, 0.0f, stops);
    ASSERT_EQ(route.size(), 6u);
    for (std::size_t i = 0; i < 3; ++i)
        EXPECT_TRUE(route[i] == 1u || route[i] == 3u || route[i] == 5u) << "stop " << i << " is quest " << route[i];
}

TEST(QuestRoute, TheRouteIsNoLongerThanVisitingTheNearestEachTime)
{
    std::vector<RouteStop> const stops = { { 1, 100.0f, 0.0f }, { 2, 100.0f, 100.0f }, { 3, 0.0f, 100.0f },
        { 4, 210.0f, 50.0f }, { 5, -90.0f, 40.0f }, { 6, 50.0f, 220.0f }, { 7, 160.0f, 180.0f } };
    std::vector<RouteStop> const greedy = NearestFirst(0.0f, 0.0f, stops);
    std::vector<RouteStop> const planned = Untangled(0.0f, 0.0f, greedy);
    EXPECT_LE(RouteLength(0.0f, 0.0f, planned), RouteLength(0.0f, 0.0f, greedy) + 0.001f);
    EXPECT_EQ(planned.size(), stops.size());
}

TEST(QuestRoute, ACrossedPathIsUntangled)
{
    std::vector<RouteStop> const crossed = { { 1, 100.0f, 100.0f }, { 2, 100.0f, 0.0f }, { 3, 200.0f, 100.0f },
        { 4, 200.0f, 0.0f } };
    std::vector<RouteStop> const planned = Untangled(0.0f, 0.0f, crossed);
    EXPECT_LT(RouteLength(0.0f, 0.0f, planned), RouteLength(0.0f, 0.0f, crossed));
}
