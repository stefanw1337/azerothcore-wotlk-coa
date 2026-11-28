#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "Unit.h"
#include <algorithm>
#include <vector>

// Gilgoblin (race 105) racials
enum GilgoblinRacials
{
    SPELL_CURRENT_RIDER      = 15001051,   // passive: periodic check -> swim speed buff
    SPELL_BRINE_MENDING      = 15001054,   // passive: 1% max health per second in water
    SPELL_BRINEBORN_CASCADE  = 15001055,   // active: bouncing heal / damage
    SPELL_CURRENT_RIDER_BUFF = 15001056,   // +50% swim speed
    SPELL_CASCADE_HEAL       = 15001057,
    SPELL_CASCADE_DAMAGE     = 15001058,
};

constexpr float CASCADE_JUMP_RANGE = 12.0f;
constexpr uint8 CASCADE_TARGETS = 5;
constexpr int32 CASCADE_PCT = 15;

// Current Rider: +50% swim speed in water, only out of combat
class spell_gilgoblin_current_rider : public AuraScript
{
    PrepareAuraScript(spell_gilgoblin_current_rider);

    void OnTick(AuraEffect const* /*aurEff*/)
    {
        Unit* owner = GetUnitOwner();
        if (!owner)
            return;
        bool want = owner->IsInWater() && !owner->IsInCombat();
        bool has = owner->HasAura(SPELL_CURRENT_RIDER_BUFF);
        if (want && !has)
            owner->CastSpell(owner, SPELL_CURRENT_RIDER_BUFF, true);
        else if (!want && has)
            owner->RemoveAurasDueToSpell(SPELL_CURRENT_RIDER_BUFF);
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* owner = GetUnitOwner())
            owner->RemoveAurasDueToSpell(SPELL_CURRENT_RIDER_BUFF);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_gilgoblin_current_rider::OnTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
        AfterEffectRemove += AuraEffectRemoveFn(spell_gilgoblin_current_rider::OnRemove, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// Brine Mending: heal 1% of maximum health every second while in water
class spell_gilgoblin_brine_mending : public AuraScript
{
    PrepareAuraScript(spell_gilgoblin_brine_mending);

    void OnTick(AuraEffect const* /*aurEff*/)
    {
        Unit* owner = GetUnitOwner();
        if (!owner || !owner->IsAlive() || !owner->IsInWater() || owner->IsFullHealth())
            return;
        owner->ModifyHealth(int32(owner->CountPctFromMaxHealth(1)));
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_gilgoblin_brine_mending::OnTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
    }
};

// Brineborn Cascade: hits the target (self, ally or enemy), then jumps to the nearest unhit ally or enemy, 5 targets.
// Allies are healed for 15% of their maximum health, enemies take 15% of the caster's maximum health as Frost damage.
class spell_gilgoblin_brineborn_cascade : public SpellScript
{
    PrepareSpellScript(spell_gilgoblin_brineborn_cascade);

    static bool IsEnemy(Unit* caster, Unit* u) { return caster->IsValidAttackTarget(u); }

    static bool IsJumpTarget(Unit* caster, Unit* from, Unit* u, std::vector<Unit*> const& hit)
    {
        if (!u->IsAlive() || std::find(hit.begin(), hit.end(), u) != hit.end() || !from->IsWithinLOSInMap(u))
            return false;
        if (Creature* c = u->ToCreature())
            if (c->IsCritter() || c->IsTotem())
                return false;
        if (caster->IsValidAttackTarget(u))
            return u->IsHostileTo(caster) || u->IsInCombatWith(caster);   // no neutral bystanders
        return caster->IsValidAssistTarget(u) && u->IsFriendlyTo(caster);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* cur = GetHitUnit();
        if (!caster || !cur)
            return;
        int32 damage = int32(caster->CountPctFromMaxHealth(CASCADE_PCT));
        std::vector<Unit*> hit;
        while (cur && hit.size() < CASCADE_TARGETS)
        {
            hit.push_back(cur);
            if (IsEnemy(caster, cur))
                caster->CastCustomSpell(cur, SPELL_CASCADE_DAMAGE, &damage, nullptr, nullptr, true);
            else
            {
                int32 heal = int32(cur->CountPctFromMaxHealth(CASCADE_PCT));
                caster->CastCustomSpell(cur, SPELL_CASCADE_HEAL, &heal, nullptr, nullptr, true);
            }
            std::list<Unit*> nearby;
            Acore::AnyUnitInObjectRangeCheck check(cur, CASCADE_JUMP_RANGE);
            Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(cur, nearby, check);
            Cell::VisitObjects(cur, searcher, CASCADE_JUMP_RANGE);
            Unit* next = nullptr;
            float best = CASCADE_JUMP_RANGE * 2;
            for (Unit* u : nearby)
            {
                if (!IsJumpTarget(caster, cur, u, hit))
                    continue;
                float d = cur->GetDistance(u);
                if (d < best) { best = d; next = u; }
            }
            cur = next;
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_gilgoblin_brineborn_cascade::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

void AddSC_gilgoblin_racials()
{
    RegisterSpellScript(spell_gilgoblin_current_rider);
    RegisterSpellScript(spell_gilgoblin_brine_mending);
    RegisterSpellScript(spell_gilgoblin_brineborn_cascade);
}
