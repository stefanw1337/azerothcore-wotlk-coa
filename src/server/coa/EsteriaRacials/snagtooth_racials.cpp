#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Unit.h"

enum SnagtoothRacials
{
    SPELL_SNAGGLETOOTH_ROAR = 15000942,
    SOUND_VIRMEN_BATTLEROAR = 9300008,
};

class spell_snagtooth_roar : public SpellScript
{
    PrepareSpellScript(spell_snagtooth_roar);

    void HandleCast()
    {
        if (Unit* caster = GetCaster())
        {
            caster->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
            caster->PlayDistanceSound(SOUND_VIRMEN_BATTLEROAR);
        }
    }

    void Register() override
    {
        OnCast += SpellCastFn(spell_snagtooth_roar::HandleCast);
    }
};

void AddSC_snagtooth_racials()
{
    RegisterSpellScript(spell_snagtooth_roar);
}
