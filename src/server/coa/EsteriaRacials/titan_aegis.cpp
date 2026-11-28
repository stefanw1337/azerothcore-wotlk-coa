#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "Unit.h"
#include <algorithm>
#include <cstdint>

class spell_titan_aegis : public AuraScript
{
    PrepareAuraScript(spell_titan_aegis);

    void CalcAmount(AuraEffect const*, int32& amount, bool& canBeRecalculated)
    {
        canBeRecalculated = false;
        if (Unit* owner = GetUnitOwner())
            amount = int32(std::min<uint32>(owner->GetMaxHealth(), uint32(INT32_MAX)));
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_titan_aegis::CalcAmount, EFFECT_0, SPELL_AURA_SCHOOL_ABSORB);
    }
};

void AddSC_titan_aegis()
{
    RegisterSpellScript(spell_titan_aegis);
}
