// Broken racials ported from Esteria's Worgoblin module (Salvager repair discount, Echo of the Naaru heal amount).
// Keyed on knowing the racial rather than on Esteria's Broken race id. Fel-Scarred's mana-drain reduction was a core
// SpellEffects change on Esteria and is not ported; its Shadow resistance comes from the spell itself.

#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

namespace
{
    enum BrokenRacials : uint32
    {
        SPELL_BROKEN_SALVAGER   = 111981,   // Esteria 110001 (moved: Ascension uses 110001)
        SPELL_BROKEN_ECHO_NAARU = 111984,   // Esteria 110004
    };
}

class broken_racials_player : public PlayerScript
{
public:
    broken_racials_player() : PlayerScript("broken_racials_player", { PLAYERHOOK_ON_BEFORE_DURABILITY_REPAIR }) { }

    void OnPlayerBeforeDurabilityRepair(Player* player, ObjectGuid /*npc*/, ObjectGuid /*item*/, float& discountMod,
        uint8 /*guildBank*/) override
    {
        if (player->HasSpell(SPELL_BROKEN_SALVAGER))
            discountMod *= 0.9f;
    }
};

class spell_broken_echo_of_the_naaru : public AuraScript
{
    PrepareAuraScript(spell_broken_echo_of_the_naaru);

    void HandleApply(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        if (Player* player = GetTarget()->ToPlayer())
            const_cast<AuraEffect*>(aurEff)->SetAmount(int32(CalculatePct(player->GetMaxHealth(), 3)));
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_broken_echo_of_the_naaru::HandleApply, EFFECT_0, SPELL_AURA_PERIODIC_HEAL,
            AURA_EFFECT_HANDLE_REAL);
    }
};

void AddSC_broken_racials()
{
    new broken_racials_player();
    RegisterSpellScript(spell_broken_echo_of_the_naaru);
}
