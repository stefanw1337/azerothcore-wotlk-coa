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

#include "Optional.h"
#include "ReputationMgr.h"
#include "IntegrationTestFixture.h"

namespace
{
constexpr uint8 Ogre = 53;
constexpr uint8 TwoHeadedOgre = 85;
constexpr uint8 Forgotten = 32;
constexpr uint8 ThinTwins = 96;
constexpr uint32 Orgrimmar = 76;
constexpr uint32 Stormwind = 72;
constexpr uint32 Darnassus = 69;

class RacedPlayer : public TestPlayer
{
public:
    using TestPlayer::TestPlayer;

    void SetRealRace(uint8 race) { m_realRace = race; }
};

class CustomRaceTeamTest : public IntegrationTestFixture
{
protected:
    void SetUp() override
    {
        IntegrationTestFixture::SetUp();
        int32 listId = 250;
        for (uint32 faction : { Orgrimmar, Stormwind, Darnassus })
        {
            if (sFactionStore.LookupEntry(faction))
                continue;
            auto* entry = new FactionEntry{};
            entry->ID = faction;
            entry->reputationListID = listId++;
            sFactionStore.SetEntry(faction, entry);
            _addedFactions.push_back(faction);
        }
    }

    void TearDown() override
    {
        IntegrationTestFixture::TearDown();
        for (uint32 faction : _addedFactions)
            sFactionStore.SetEntry(faction, nullptr);
    }

    RacedPlayer* CreatePlayer(uint8 race)
    {
        auto* session = new WorldSession(++_guid, "RacedPlayer", 0, nullptr, SEC_PLAYER,
            EXPANSION_WRATH_OF_THE_LICH_KING, 0, LOCALE_enUS, 0, false, false, 0);
        session->InitRBACDataForTest();
        auto* player = new RacedPlayer(session);
        player->ForceInitValues(_guid);
        session->SetPlayer(player);
        player->SetRealRace(race);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 1, uint8(CLASS_WARRIOR));
        player->GetReputationMgr().LoadFromDB(nullptr);
        return player;
    }

private:
    ObjectGuid::LowType _guid = 0;
    std::vector<uint32> _addedFactions;
};
}

TEST_F(CustomRaceTeamTest, OwnRacesShareTheRaceMaskOfTheRaceTheyCopy)
{
    EXPECT_EQ(CreatePlayer(TwoHeadedOgre)->getRaceMask(), CreatePlayer(Ogre)->getRaceMask());
    EXPECT_EQ(CreatePlayer(ThinTwins)->getRaceMask(), CreatePlayer(Forgotten)->getRaceMask());
}

TEST_F(CustomRaceTeamTest, OwnRacesStartWithTheReputationOfTheRaceTheyCopy)
{
    for (auto [own, copied] : { std::pair{ TwoHeadedOgre, Ogre }, std::pair{ ThinTwins, Forgotten } })
    {
        ReputationMgr const& ownReputation = CreatePlayer(own)->GetReputationMgr();
        ReputationMgr const& copiedReputation = CreatePlayer(copied)->GetReputationMgr();
        for (uint32 faction : { Orgrimmar, Stormwind, Darnassus })
            EXPECT_EQ(ownReputation.GetReputation(faction), copiedReputation.GetReputation(faction))
                << "race " << uint32(own) << ", faction " << faction;
    }

    ReputationMgr const& twoHeadedOgre = CreatePlayer(TwoHeadedOgre)->GetReputationMgr();
    EXPECT_GE(twoHeadedOgre.GetRank(sFactionStore.LookupEntry(Orgrimmar)), REP_FRIENDLY);
    EXPECT_EQ(twoHeadedOgre.GetRank(sFactionStore.LookupEntry(Darnassus)), REP_HATED);
}
