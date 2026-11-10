/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "MotionMaster.h"
#include "Player.h"
#include "WorldSession.h"
#include <string>

#if __has_include("Playerbots.h") && __has_include("PlayerbotMgr.h") && __has_include("PlayerbotAIConfig.h")
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#define SUBCLASS_WITH_SQUID_PLAYERBOTS 1
#endif

bool TwoPlayersHeadIsBot(Player const* head)
{
    return head && head->GetSession() && head->GetSession()->IsBot();
}

#ifdef SUBCLASS_WITH_SQUID_PLAYERBOTS

namespace
{
std::string QuestingStrategy()
{
    return sPlayerbotAIConfig.enableNewRpgStrategy ? "new rpg" : "rpg";
}

void LinkForOneLogin(uint32 headAccount, uint32 masterAccount, bool link)
{
    if (link)
        PlayerbotsDatabase.DirectExecute("INSERT IGNORE INTO playerbots_account_links (account_id, linked_account_id) "
            "VALUES ({}, {})", headAccount, masterAccount);
    else
        PlayerbotsDatabase.DirectExecute("DELETE FROM playerbots_account_links WHERE account_id = {} AND "
            "linked_account_id = {}", headAccount, masterAccount);
}
}

bool TwoPlayersBotsAvailable()
{
    return true;
}

bool TwoPlayersCallBot(Player* master, uint32 headGuid)
{
    PlayerbotMgr* manager = GET_PLAYERBOT_MGR(master);
    if (!manager)
        return false;
    ObjectGuid const head = ObjectGuid::Create<HighGuid::Player>(headGuid);
    uint32 const headAccount = sCharacterCache->GetCharacterAccountIdByGuid(head);
    uint32 const masterAccount = master->GetSession()->GetAccountId();
    if (!headAccount)
        return false;
    bool const alreadyLinked = headAccount == masterAccount || manager->IsAccountLinked(headAccount, masterAccount);
    if (!alreadyLinked)
        LinkForOneLogin(headAccount, masterAccount, true);
    manager->AddPlayerBot(head, masterAccount);
    if (!alreadyLinked)
        LinkForOneLogin(headAccount, masterAccount, false);
    return true;
}

void TwoPlayersSteerBotBody(Player* body, bool roam)
{
    PlayerbotAI* brain = GET_PLAYERBOT_AI(body);
    if (!brain)
        return;
    std::string const questing = QuestingStrategy();
    bool const roaming = brain->HasStrategy("grind", BOT_STATE_NON_COMBAT) &&
        brain->HasStrategy(questing, BOT_STATE_NON_COMBAT);
    bool const following = brain->HasStrategy("follow", BOT_STATE_NON_COMBAT);
    bool const staying = brain->HasStrategy("stay", BOT_STATE_NON_COMBAT);
    if (roam)
    {
        if (!roaming || following || staying)
            brain->ChangeStrategy("-follow,-stay,+grind,+" + questing, BOT_STATE_NON_COMBAT);
        return;
    }
    if (staying && !roaming && !following)
        return;
    brain->ChangeStrategy("-follow,-grind,-" + questing + ",+stay", BOT_STATE_NON_COMBAT);
    body->StopMoving();
    body->GetMotionMaster()->Clear();
}

void TwoPlayersKeepBotRiderStill(Player* rider)
{
    PlayerbotAI* brain = GET_PLAYERBOT_AI(rider);
    if (!brain)
        return;
    std::string const questing = QuestingStrategy();
    if (brain->HasStrategy("grind", BOT_STATE_NON_COMBAT) || brain->HasStrategy(questing, BOT_STATE_NON_COMBAT) ||
        brain->HasStrategy("food", BOT_STATE_NON_COMBAT))
        brain->ChangeStrategy("-grind,-food,-" + questing, BOT_STATE_NON_COMBAT);
    if (brain->rpgInfo.GetStatus() == RPG_IDLE)
        return;
    brain->rpgInfo.ChangeToIdle();
    if (rider->getStandState() != UNIT_STAND_STATE_STAND)
        rider->SetStandState(UNIT_STAND_STATE_STAND);
}

#else

bool TwoPlayersBotsAvailable()
{
    return false;
}

bool TwoPlayersCallBot(Player* /*master*/, uint32 /*headGuid*/)
{
    return false;
}

void TwoPlayersSteerBotBody(Player* /*body*/, bool /*roam*/)
{
}

void TwoPlayersKeepBotRiderStill(Player* /*rider*/)
{
}

#endif
