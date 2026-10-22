/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Player.h"
#include "PlayerScript.h"

#include <array>
#include <utility>

namespace
{
constexpr uint32 DualWield = 674;
constexpr uint32 SecondDualWield = 370094;
constexpr std::array<std::pair<uint32, uint32>, 1> CopiesOf = { { { SecondDualWield, DualWield } } };

void DropCopies(Player* player)
{
    for (auto const& [copy, original] : CopiesOf)
        if (player->HasSpell(copy) && player->HasSpell(original))
            player->removeSpell(copy, SPEC_MASK_ALL, false);
}

bool InvolvesCopy(uint32 spellId)
{
    for (auto const& [copy, original] : CopiesOf)
        if (spellId == copy || spellId == original)
            return true;
    return false;
}
}

class mod_subclass_spellbook : public PlayerScript
{
public:
    mod_subclass_spellbook() : PlayerScript("mod_subclass_spellbook",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LEARN_SPELL }) { }

    void OnPlayerLogin(Player* player) override
    {
        DropCopies(player);
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        if (InvolvesCopy(spellId))
            DropCopies(player);
    }
};

void AddSC_mod_subclass_spellbook()
{
    new mod_subclass_spellbook();
}
