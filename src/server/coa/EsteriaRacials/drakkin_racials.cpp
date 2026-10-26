#include "Group.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "EsteriaRaceIds.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

#include <algorithm>
#include <array>
#include <vector>

namespace
{
    enum DrakkinSpells : uint32
    {
        SPELL_RUBY_BLOOD          = 110301,
        SPELL_AZURE_BLOOD         = 110302,
        SPELL_EMERALD_BLOOD       = 110303,
        SPELL_BRONZE_BLOOD        = 110304,
        SPELL_OBSIDIAN_BLOOD      = 110305,
        SPELL_TWILIGHT_BLOOD      = 110306,
        SPELL_INFINITE_BLOOD      = 110307,
        SPELL_FROSTBROOD_BLOOD    = 110308,
        SPELL_DRACONIC_EXPOSURE   = 110310,
        SPELL_BREATH_FIRE         = 110311,
        SPELL_BREATH_ARCANE       = 110312,
        SPELL_BREATH_NATURE       = 110313,
        SPELL_BREATH_SHADOW       = 110314,
        SPELL_BREATH_FROST        = 110315,
        SPELL_TOGETHER_STRONG     = 110317,
        SPELL_BROODBLOOD_BUFF     = 110319,
    };

    constexpr std::array<uint32, 8> BloodSpells =
    {
        SPELL_RUBY_BLOOD, SPELL_AZURE_BLOOD, SPELL_EMERALD_BLOOD, SPELL_BRONZE_BLOOD,
        SPELL_OBSIDIAN_BLOOD, SPELL_TWILIGHT_BLOOD, SPELL_INFINITE_BLOOD, SPELL_FROSTBROOD_BLOOD
    };

    constexpr float TogetherStrongRange   = 30.0f;
    constexpr uint32 TogetherStrongMax    = 4;
    constexpr int32 BroodbloodHealthPct   = 30;
    constexpr int32 BreathDamagePerLevel  = 8;
    constexpr float BreathPowerCoefficient = 0.4f;

    bool IsDrakkin(Unit const* unit)
    {
        if (!unit || !unit->IsPlayer())
            return false;

        uint32 race = unit->ToPlayer()->getRace(true);
        return race == RACE_DRAKKIN_ALLIANCE || race == RACE_DRAKKIN_HORDE;
    }

    uint32 GetBreathDamageSpell(Unit const* caster)
    {
        if (caster->HasAura(SPELL_AZURE_BLOOD) || caster->HasAura(SPELL_BRONZE_BLOOD) || caster->HasAura(SPELL_INFINITE_BLOOD))
            return SPELL_BREATH_ARCANE;

        if (caster->HasAura(SPELL_EMERALD_BLOOD))
            return SPELL_BREATH_NATURE;

        if (caster->HasAura(SPELL_TWILIGHT_BLOOD))
            return SPELL_BREATH_SHADOW;

        if (caster->HasAura(SPELL_FROSTBROOD_BLOOD))
            return SPELL_BREATH_FROST;

        return SPELL_BREATH_FIRE;
    }
}

class spell_drakkin_chromatic_blood : public AuraScript
{
    PrepareAuraScript(spell_drakkin_chromatic_blood);

    void HandleApply(AuraEffect const*  , AuraEffectHandleModes  )
    {
        for (uint32 bloodSpell : BloodSpells)
            if (bloodSpell != GetId())
                GetTarget()->RemoveAurasDueToSpell(bloodSpell);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_drakkin_chromatic_blood::HandleApply, EFFECT_0, SPELL_AURA_ANY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_drakkin_lesser_dragonbreath : public SpellScript
{
    PrepareSpellScript(spell_drakkin_lesser_dragonbreath);

    void HandleDummy(SpellEffIndex  )
    {
        Unit* target = GetHitUnit();
        if (!target)
            return;

        Unit* caster = GetCaster();
        caster->CastSpell(target, GetBreathDamageSpell(caster), true);
        caster->CastSpell(target, SPELL_DRACONIC_EXPOSURE, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_drakkin_lesser_dragonbreath::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_drakkin_breath_damage : public SpellScript
{
    PrepareSpellScript(spell_drakkin_breath_damage);

    void HandleDamage(SpellEffIndex effIndex)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target)
            return;

        float attackPower = std::max(caster->GetTotalAttackPowerValue(BASE_ATTACK), caster->GetTotalAttackPowerValue(RANGED_ATTACK));
        float spellPower = float(caster->SpellBaseDamageBonusDone(GetSpellInfo()->GetSchoolMask()));
        int32 damage = int32(caster->GetLevel() * BreathDamagePerLevel + std::max(attackPower, spellPower) * BreathPowerCoefficient);

        damage = caster->SpellDamageBonusDone(target, GetSpellInfo(), uint32(damage), SPELL_DIRECT_DAMAGE, effIndex);
        damage = target->SpellDamageBonusTaken(caster, GetSpellInfo(), uint32(damage), SPELL_DIRECT_DAMAGE);
        SetHitDamage(damage);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_drakkin_breath_damage::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

class spell_drakkin_together_strong : public AuraScript
{
    PrepareAuraScript(spell_drakkin_together_strong);

    void HandlePeriodic(AuraEffect const*  )
    {
        Player* owner = GetTarget()->ToPlayer();
        if (!owner || !owner->IsAlive())
            return;

        Group* group = owner->GetGroup();
        if (!group)
            return;

        std::vector<Player*> members;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member->IsAlive() && member->IsInWorld() && member->GetMapId() == owner->GetMapId())
                    members.push_back(member);

        for (Player* member : members)
        {
            if (!owner->IsWithinDist(member, TogetherStrongRange))
                continue;

            uint32 drakkinNearby = 0;
            for (Player* other : members)
                if (IsDrakkin(other) && member->IsWithinDist(other, TogetherStrongRange))
                    ++drakkinNearby;

            int32 stamina = int32(std::min(drakkinNearby, TogetherStrongMax));
            if (stamina)
                owner->CastCustomSpell(member, SPELL_TOGETHER_STRONG, &stamina, nullptr, nullptr, true);
        }
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_drakkin_together_strong::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

class spell_drakkin_ancient_broodblood : public AuraScript
{
    PrepareAuraScript(spell_drakkin_ancient_broodblood);

    bool CheckProc(ProcEventInfo& eventInfo)
    {
        DamageInfo* damageInfo = eventInfo.GetDamageInfo();
        Unit* target = GetTarget();
        return damageInfo && damageInfo->GetDamage() && target->IsAlive() && target->HealthBelowPct(BroodbloodHealthPct);
    }

    void HandleProc(AuraEffect const*  , ProcEventInfo&  )
    {
        PreventDefaultAction();
        GetTarget()->CastSpell(GetTarget(), SPELL_BROODBLOOD_BUFF, true);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_drakkin_ancient_broodblood::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_drakkin_ancient_broodblood::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

void AddSC_drakkin_racials()
{
    RegisterSpellScript(spell_drakkin_chromatic_blood);
    RegisterSpellScript(spell_drakkin_lesser_dragonbreath);
    RegisterSpellScript(spell_drakkin_breath_damage);
    RegisterSpellScript(spell_drakkin_together_strong);
    RegisterSpellScript(spell_drakkin_ancient_broodblood);
}
