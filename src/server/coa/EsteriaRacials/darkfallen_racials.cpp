#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "EsteriaRaceIds.h"
#include "SpellAuras.h"
#include "SpellInfo.h"

namespace
{
    enum DarkfallenSpells : uint32
    {
        SPELL_CHILDREN_STEALTH_SPEED = 110043,
        SPELL_CHILDREN_GHOST_SPEED   = 110044,
    };

    bool IsDarkfallen(Unit const* unit)
    {
        if (!unit || !unit->IsPlayer())
            return false;

        uint32 race = unit->ToPlayer()->getRace(true);
        return race == RACE_DARKFALLEN_ALLIANCE || race == RACE_DARKFALLEN_HORDE;
    }

    bool IsStealthAura(Aura const* aura)
    {
        return aura && aura->GetSpellInfo() && aura->GetSpellInfo()->HasAura(SPELL_AURA_MOD_STEALTH);
    }
}

class darkfallen_children_of_the_night : public UnitScript
{
public:
    darkfallen_children_of_the_night() : UnitScript("darkfallen_children_of_the_night", true,
        { UNITHOOK_ON_AURA_APPLY, UNITHOOK_ON_AURA_REMOVE }) { }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        if (!IsStealthAura(aura) || !IsDarkfallen(unit))
            return;

        unit->CastSpell(unit, SPELL_CHILDREN_STEALTH_SPEED, true);
    }

    void OnAuraRemove(Unit* unit, AuraApplication* aurApp, AuraRemoveMode  ) override
    {
        if (!aurApp || !IsStealthAura(aurApp->GetBase()) || !IsDarkfallen(unit))
            return;

        if (!unit->HasAuraType(SPELL_AURA_MOD_STEALTH))
            unit->RemoveAurasDueToSpell(SPELL_CHILDREN_STEALTH_SPEED);
    }
};

class darkfallen_ghost_speed : public PlayerScript
{
public:
    darkfallen_ghost_speed() : PlayerScript("darkfallen_ghost_speed") { }

    void OnPlayerReleasedGhost(Player* player) override
    {
        if (IsDarkfallen(player))
            player->CastSpell(player, SPELL_CHILDREN_GHOST_SPEED, true);
    }

    void OnPlayerResurrect(Player* player, float  , bool&  ) override
    {
        if (IsDarkfallen(player))
            player->RemoveAurasDueToSpell(SPELL_CHILDREN_GHOST_SPEED);
    }
};

void AddSC_darkfallen_racials()
{
    new darkfallen_children_of_the_night();
    new darkfallen_ghost_speed();
}
