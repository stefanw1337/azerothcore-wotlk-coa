/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "SubclassRules.h"

#include <array>

namespace
{
constexpr uint32 EbonHoldMap = 609;
constexpr std::array<uint8, 10> StockRaces = { RACE_HUMAN, RACE_ORC, RACE_DWARF, RACE_NIGHTELF, RACE_UNDEAD_PLAYER,
    RACE_TAUREN, RACE_GNOME, RACE_TROLL, RACE_BLOODELF, RACE_DRAENEI };

PlayerInfo const* HomeStart(uint8 race)
{
    for (uint8 classId = CLASS_WARRIOR; classId < MAX_CLASSES; ++classId)
        if (PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, classId); info && info->mapId != EbonHoldMap)
            return info;
    return nullptr;
}

uint32 StartZoneRaceMask(uint8 race)
{
    PlayerInfo const* start = HomeStart(race);
    if (!start)
        return 0;
    for (uint8 stock : StockRaces)
        if (PlayerInfo const* home = HomeStart(stock);
            home && home->mapId == start->mapId && home->areaId == start->areaId)
            return 1u << (stock - 1);
    return 0;
}

uint32 QuestRacesOf(uint8 race)
{
    static std::array<uint32, 2> const shared = { StartZoneRaceMask(Subclass::ThinHumanRace),
        StartZoneRaceMask(Subclass::TwoHeadedOgreRace) };
    if (race == Subclass::ThinHumanRace)
        return shared[0];
    if (race == Subclass::TwoHeadedOgreRace)
        return shared[1];
    return 0;
}
}

class mod_subclass_quest_races : public PlayerScript
{
public:
    mod_subclass_quest_races() : PlayerScript("mod_subclass_quest_races", { PLAYERHOOK_ON_GET_QUEST_RACE_MASK }) { }

    bool OnPlayerGetQuestRaceMask(Player const* player, uint32& mask) override
    {
        uint32 const startZoneRaces = QuestRacesOf(player->getRace(true));
        if (!startZoneRaces)
            return false;
        mask = player->getRaceMask() | startZoneRaces;
        return true;
    }
};

void AddSC_mod_subclass_quests()
{
    new mod_subclass_quest_races();
}
