#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Unit.h"

// Snagtooth (race 94) racials. Springheel (double jump) lives in the client
// (CoA64 Movement.cpp); dodge / speed / Nature passives are plain auras.
enum SnagtoothRacials
{
    SPELL_SNAGGLETOOTH_ROAR = 15000942,   // AoE fear, up to 5 enemies in 8 yd for 4 sec
    SOUND_VIRMEN_BATTLEROAR = 9300008,
};

// Snaggletooth Roar: the virmen rears up and roars (EmoteRoar, anim 74) as the fear goes out
class spell_snagtooth_roar : public SpellScript
{
    PrepareSpellScript(spell_snagtooth_roar);

    void HandleCast()
    {
        if (Unit* caster = GetCaster())
        {
            caster->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
            caster->PlayDistanceSound(SOUND_VIRMEN_BATTLEROAR);   // client SoundEntries (snagtooth/voice.py)
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
