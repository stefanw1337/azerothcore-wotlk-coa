/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Player.h"
#include "PlayerScript.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <vector>

namespace
{
constexpr uint32 DualWield = 674;

bool OnlyGrantsDualWield(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info || !info->IsPassive())
        return false;
    bool dualWield = false;
    for (SpellEffectInfo const& effect : info->GetEffects())
    {
        if (!effect.IsEffect())
            continue;
        if (effect.Effect != SPELL_EFFECT_DUAL_WIELD)
            return false;
        dualWield = true;
    }
    return dualWield;
}

void DropCopies(Player* player)
{
    std::vector<uint32> known;
    for (auto const& [spellId, spell] : player->GetSpellMap())
        if (spell->State != PLAYERSPELL_REMOVED && OnlyGrantsDualWield(spellId))
            known.push_back(spellId);
    if (known.size() < 2)
        return;
    uint32 const kept = std::find(known.begin(), known.end(), DualWield) != known.end() ? DualWield :
        *std::min_element(known.begin(), known.end());
    for (uint32 spellId : known)
        if (spellId != kept)
            player->removeSpell(spellId, SPEC_MASK_ALL, false);
}

bool InvolvesCopy(uint32 spellId)
{
    return OnlyGrantsDualWield(spellId);
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
