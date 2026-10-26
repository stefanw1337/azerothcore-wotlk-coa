#include "CellImpl.h"
#include "GridNotifiers.h"
#include "Group.h"
#include "LootMgr.h"
#include "MotionMaster.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "EsteriaRaceIds.h"
#include "SpellScript.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <vector>

namespace
{
    enum DrakonidSpells : uint32
    {
        SPELL_CHARGE_IMPACT = 110401,
        SPELL_CHARGE_STUN   = 110402,
        SPELL_CHARGE_STOMP  = 110403,
    };

    constexpr float ChargeDistance     = 10.0f;
    constexpr float ChargeSpeed        = 30.0f;
    constexpr float ChargeHitRadius    = 3.0f;
    constexpr float KnockAsideSpeedXY  = 8.0f;
    constexpr float KnockAsideSpeedZ   = 4.0f;
    constexpr uint32 StompDelayPadMs   = 100;
    constexpr int32 ChargeDamagePerLevel = 6;
    constexpr float ChargePowerCoefficient = 0.3f;
    constexpr int32 TreasureHoarderPct = 5;

    bool IsDrakonid(Unit const* unit)
    {
        if (!unit || !unit->IsPlayer())
            return false;

        uint32 race = unit->ToPlayer()->getRace(true);
        return race == RACE_DRAKONID_ALLIANCE || race == RACE_DRAKONID_HORDE;
    }

    struct ChargeTarget
    {
        Unit* unit;
        float along;
        float closestX;
        float closestY;
    };
}

class spell_drakonid_draconic_charge : public SpellScript
{
    PrepareSpellScript(spell_drakonid_draconic_charge);

    SpellCastResult CheckCast()
    {
        if (GetCaster()->HasUnitState(UNIT_STATE_ROOT))
            return SPELL_FAILED_ROOTED;

        return SPELL_CAST_OK;
    }

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        float startX = caster->GetPositionX();
        float startY = caster->GetPositionY();
        Position dest = caster->GetFirstCollisionPosition(ChargeDistance, 0.0f);

        StrikePath(caster, startX, startY, dest.GetPositionX(), dest.GetPositionY());

        float travelled = caster->GetExactDist2d(dest.GetPositionX(), dest.GetPositionY());
        caster->GetMotionMaster()->MoveCharge(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), ChargeSpeed);

        Milliseconds delay = Milliseconds(uint32(travelled / ChargeSpeed * 1000.0f) + StompDelayPadMs);
        caster->m_Events.AddEventAtOffset([caster]()
        {
            if (caster->IsAlive())
                caster->CastSpell(caster, SPELL_CHARGE_STOMP, true);
        }, delay);
    }

    void StrikePath(Unit* caster, float startX, float startY, float endX, float endY)
    {
        float pathX = endX - startX;
        float pathY = endY - startY;
        float pathLengthSq = pathX * pathX + pathY * pathY;
        if (pathLengthSq < 0.01f)
            return;

        std::list<Unit*> units;
        Acore::AnyUnfriendlyUnitInObjectRangeCheck check(caster, caster, ChargeDistance + ChargeHitRadius);
        Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(caster, units, check);
        Cell::VisitObjects(caster, searcher, ChargeDistance + ChargeHitRadius);

        std::vector<ChargeTarget> struck;
        for (Unit* unit : units)
        {
            if (!caster->IsValidAttackTarget(unit))
                continue;

            float toX = unit->GetPositionX() - startX;
            float toY = unit->GetPositionY() - startY;
            float along = std::clamp((toX * pathX + toY * pathY) / pathLengthSq, 0.0f, 1.0f);
            float closestX = startX + pathX * along;
            float closestY = startY + pathY * along;
            if (unit->GetExactDist2d(closestX, closestY) <= ChargeHitRadius)
                struck.push_back({ unit, along, closestX, closestY });
        }

        std::sort(struck.begin(), struck.end(), [](ChargeTarget const& a, ChargeTarget const& b) { return a.along < b.along; });

        float pathLength = std::sqrt(pathLengthSq);
        bool stunned = false;
        for (ChargeTarget const& hit : struck)
        {
            caster->CastSpell(hit.unit, SPELL_CHARGE_IMPACT, true);
            if (!stunned)
            {
                caster->CastSpell(hit.unit, SPELL_CHARGE_STUN, true);
                stunned = true;
            }

            Creature const* creature = hit.unit->ToCreature();
            if (creature && (creature->isWorldBoss() || creature->IsDungeonBoss()))
                continue;

            float originX = hit.closestX;
            float originY = hit.closestY;
            if (hit.unit->GetExactDist2d(originX, originY) < 0.5f)
            {
                originX = hit.unit->GetPositionX() + pathY / pathLength;
                originY = hit.unit->GetPositionY() - pathX / pathLength;
            }

            hit.unit->KnockbackFrom(originX, originY, KnockAsideSpeedXY, KnockAsideSpeedZ);
        }
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_drakonid_draconic_charge::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_drakonid_draconic_charge::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_drakonid_charge_damage : public SpellScript
{
    PrepareSpellScript(spell_drakonid_charge_damage);

    void HandleDamage(SpellEffIndex effIndex)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target)
            return;

        float attackPower = std::max(caster->GetTotalAttackPowerValue(BASE_ATTACK), caster->GetTotalAttackPowerValue(RANGED_ATTACK));
        int32 damage = int32(caster->GetLevel() * ChargeDamagePerLevel + attackPower * ChargePowerCoefficient);

        damage = caster->SpellDamageBonusDone(target, GetSpellInfo(), uint32(damage), SPELL_DIRECT_DAMAGE, effIndex);
        damage = target->SpellDamageBonusTaken(caster, GetSpellInfo(), uint32(damage), SPELL_DIRECT_DAMAGE);
        SetHitDamage(damage);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_drakonid_charge_damage::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

class drakonid_treasure_hoarder : public PlayerScript
{
public:
    drakonid_treasure_hoarder() : PlayerScript("drakonid_treasure_hoarder", { PLAYERHOOK_ON_BEFORE_LOOT_MONEY }) { }

    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override
    {
        if (!loot || loot->loot_type != LOOT_CORPSE || !loot->gold)
            return;

        std::vector<Player*> recipients;
        if (Group* group = player->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (player->IsAtLootRewardDistance(member))
                        recipients.push_back(member);
        }

        if (recipients.empty())
            recipients.push_back(player);

        uint32 share = loot->gold / recipients.size();
        for (Player* recipient : recipients)
            if (IsDrakonid(recipient))
                recipient->ModifyMoney(CalculatePct(share, TreasureHoarderPct));
    }
};

void AddSC_drakonid_racials()
{
    RegisterSpellScript(spell_drakonid_draconic_charge);
    RegisterSpellScript(spell_drakonid_charge_damage);
    new drakonid_treasure_hoarder();
}
