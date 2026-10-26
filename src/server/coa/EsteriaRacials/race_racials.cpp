#include "Cell.h"
#include "Chat.h"
#include "CellImpl.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "ObjectMgr.h"
#include "TemporarySummon.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "LootMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include <mutex>
#include "ScriptedGossip.h"
#include "SharedDefines.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"

#include "race_racials_generated.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace RaceRacials;

namespace
{

    constexpr int32 DamagePerLevel          = 8;
    constexpr float PowerCoefficient        = 0.4f;
    constexpr int32 HealPerLevel            = 6;
    constexpr float HealCoefficient         = 0.3f;
    constexpr int32 BleedPerLevel           = 6;
    constexpr float BleedCoefficient        = 0.5f;
    constexpr uint32 BleedTicks             = 3;
    constexpr uint32 RegenerationTicks      = 6;
    constexpr int32 RegenerationPct         = 50;
    constexpr uint32 TidalMendTicks         = 3;
    constexpr int32 TidalMendPct            = 15;
    constexpr float LightsJudgmentRadius    = 10.0f;
    constexpr uint32 LightsJudgmentDelayMs  = 3000;
    constexpr float LightsReckoningRadius   = 15.0f;
    constexpr float SurgeRadius             = 15.0f;
    constexpr uint8 SurgeHealLevel          = 40;
    constexpr uint8 SurgeStrikeLevel        = 70;
    constexpr float ThornBloomRadius        = 8.0f;
    constexpr uint32 ThornBloomMaxTargets   = 8;
    constexpr float WingBuffetRange         = 15.0f;
    constexpr float KnockSpeedXY            = 14.0f;
    constexpr float KnockSpeedZ             = 6.0f;
    constexpr float BullRushDistance        = 12.0f;
    constexpr float BullRushSpeed           = 12.0f;
    constexpr float BullRushHitRadius       = 3.0f;
    constexpr float PounceSpeed             = 30.0f;
    constexpr float RuggedTenacityPerStamina = 0.05f;
    constexpr uint32 FirebloodMaxRemoved    = 5;
    constexpr int32 FirebloodBasePct        = 2;
    constexpr uint32 CombatAnalysisTickMs   = 5000;
    constexpr uint32 FailsafeCooldownMs     = 150000;
    constexpr uint32 NoseCooldownMs         = 30000;
    constexpr int32 FailsafeHealthPct       = 20;
    constexpr int32 FailsafeHealPct         = 15;
    constexpr int32 NosePct                 = 5;
    constexpr int32 CityOfGoldPct           = 2;
    constexpr int32 DemonbanePct            = 20;
    constexpr int32 PredatorXpPct           = 10;
    constexpr int32 ExplorationXpPct        = 200;
    constexpr float EpicureanMultiplier     = 2.0f;
    constexpr float HeartyMealsMultiplier   = 1.1f;
    constexpr float MakeDoMultiplier        = 1.1f;
    constexpr uint32 SpellPickLock          = 1804;
    constexpr uint32 SkillLockpicking       = 633;

    bool Knows(Unit const* unit, uint32 spellId)
    {
        return unit && unit->IsPlayer() && unit->HasAura(spellId);
    }

    uint32 ScaledAmount(Unit* caster, SpellInfo const* spell, int32 perLevel, float coefficient)
    {
        float attackPower = std::max(caster->GetTotalAttackPowerValue(BASE_ATTACK), caster->GetTotalAttackPowerValue(RANGED_ATTACK));
        float spellPower = float(std::max(caster->SpellBaseDamageBonusDone(spell->GetSchoolMask()),
                                          caster->SpellBaseHealingBonusDone(spell->GetSchoolMask())));
        return uint32(std::max(0.0f, caster->GetLevel() * perLevel + std::max(attackPower, spellPower) * coefficient));
    }

    template <typename Check>
    std::list<Unit*> Collect(WorldObject* center, Check& check, float radius)
    {
        std::list<Unit*> units;
        Acore::UnitListSearcher<Check> searcher(center, units, check);
        Cell::VisitObjects(center, searcher, radius);
        return units;
    }

    std::list<Unit*> HostilesAround(Unit* caster, WorldObject* center, float radius)
    {
        Acore::AnyUnfriendlyUnitInObjectRangeCheck check(center, caster, radius);
        auto units = Collect(center, check, radius);
        units.remove_if([caster](Unit* unit) { return !caster->IsValidAttackTarget(unit); });
        return units;
    }

    std::list<Unit*> FriendsAround(Unit* caster, WorldObject* center, float radius)
    {
        Acore::AnyFriendlyUnitInObjectRangeCheck check(center, caster, radius, true);
        return Collect(center, check, radius);
    }

    bool IsBoss(Unit const* unit)
    {
        Creature const* creature = unit->ToCreature();
        return creature && (creature->isWorldBoss() || creature->IsDungeonBoss());
    }

    void RefreshOutOfCombatSpeed(Unit* unit)
    {
        if (!Knows(unit, SPELL_HUNTERS_PACE))
            return;

        if (unit->IsInCombat())
            unit->RemoveAurasDueToSpell(SPELL_HUNTERS_PACE_SPEED);
        else if (!unit->HasAura(SPELL_HUNTERS_PACE_SPEED))
            unit->CastSpell(unit, SPELL_HUNTERS_PACE_SPEED, true);
    }

    constexpr uint32 SpellCloneMe           = 45204;
    constexpr uint32 GossipTextId           = 1;
    constexpr uint32 LoaChangeCooldownMs    = RACIAL_LOA_COOLDOWN_DAYS * DAY * IN_MILLISECONDS;
    constexpr int32 EmbraceProcChance       = 10;
    constexpr uint32 EmbraceProcCooldownMs  = 3000;
    constexpr int32 AkundaExtraHealPct      = 25;
    constexpr uint32 SpatialRiftCooldownMs  = 3 * MINUTE * IN_MILLISECONDS;
    constexpr float SpatialRiftRange        = 35.0f;
    constexpr uint32 MailboxLifetimeSec     = 90;
    constexpr float SummonDistance          = 2.5f;
    constexpr uint32 ForgeLifetimeSec       = 120;
    constexpr uint32 CampLifetimeSec        = HOUR;
    constexpr uint32 MastercraftIntervalMs  = 4000;
    constexpr uint32 MastercraftLifetimeSec = 10;
    constexpr uint32 DecoyLifetimeMs        = 15000;
    constexpr int32 DecoyHealthPct          = 10;
    constexpr float DecoyTauntRadius        = 30.0f;
    constexpr float DecoyThreat             = 100000.0f;
    constexpr int32 QuietContemplationPct   = 35;
    constexpr uint32 QuietContemplationTicks = 10;
    constexpr int32 WasteNotChance          = 10;
    constexpr int32 InnerPeaceFactor        = 2;
    constexpr uint32 ThornBloomPulses       = 3;
    constexpr uint32 ThornBloomPulseDelayMs = 2000;
    constexpr uint32 SecondMs               = 1000;

    enum RacialMenu : uint32
    {
        MENU_LOA = 0x52520001,
        MENU_MOLE_MACHINE,
        MENU_CHOSEN_IDENTITY,
    };

    enum RacialSetting : uint32
    {
        SETTING_IDENTITY_RACE,
        SETTING_BAG_TRICK,
        SETTING_SADDLEBAG_GIVEN,
        SETTING_CAMP_MAP,
        SETTING_CAMP_X,
        SETTING_CAMP_Y,
        SETTING_CAMP_Z,
        SETTING_CAMP_EXPIRES,
    };

    enum BagTrick : uint32
    {
        TRICK_PHYSICAL,
        TRICK_FIRE,
        TRICK_FROST,
        TRICK_COUNT,
    };

    char const* const SettingsSource = "race_racials";

    uint32 Setting(Player* player, uint32 index)
    {
        return player->GetPlayerSetting(SettingsSource, index).value;
    }

    void SetSetting(Player* player, uint32 index, uint32 value)
    {
        player->UpdatePlayerSetting(SettingsSource, index, value);
    }

    uint32 FloatToBits(float value)
    {
        uint32 bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    float BitsToFloat(uint32 bits)
    {
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void Say(Player* player, char const* text)
    {
        ChatHandler(player->GetSession()).SendSysMessage(text);
    }

    void OpenMenu(Player* player, uint32 menuId)
    {
        player->PlayerTalkClass->GetGossipMenu().SetMenuId(menuId);
        SendGossipMenuFor(player, GossipTextId, player->GetGUID());
    }

    template <typename Fn>
    void Defer(Unit* owner, Fn&& fn)
    {
        owner->m_Events.AddEventAtOffset(std::forward<Fn>(fn), Milliseconds(1));
    }

    bool RollEmbrace(Player* player, uint32 spellId)
    {
        if (!Knows(player, spellId) || player->HasSpellCooldown(spellId) || !roll_chance_i(EmbraceProcChance))
            return false;

        player->AddSpellCooldown(spellId, 0, EmbraceProcCooldownMs);
        return true;
    }

    void SummonInFront(Unit* caster, uint32 entry, uint32 lifetimeSec)
    {
        Position pos = caster->GetFirstCollisionPosition(SummonDistance, 0.0f);
        caster->SummonGameObject(entry, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(),
                                 caster->GetOrientation(), 0.0f, 0.0f, 0.0f, 0.0f, lifetimeSec);
    }

    void RefreshMastercraft(Player* player)
    {
        for (uint32 entry : { uint32(RACIAL_GO_INVISIBLE_ANVIL), uint32(RACIAL_GO_INVISIBLE_FORGE),
                              uint32(RACIAL_GO_INVISIBLE_COOKING) })
            player->SummonGameObject(entry, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),
                                     player->GetOrientation(), 0.0f, 0.0f, 0.0f, 0.0f, MastercraftLifetimeSec);
    }

    Item* FindGem(Player* player)
    {
        auto isGem = [](Item* item)
        {
            ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
            return proto && proto->Class == ITEM_CLASS_GEM && proto->SubClass != ITEM_SUBCLASS_GEM_META;
        };

        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot); isGem(item))
                return item;

        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = player->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = bag->GetItemByPos(slot); isGem(item))
                        return item;

        return nullptr;
    }

    uint32 IngestBuffForGem(uint32 gemColor)
    {
        switch (gemColor)
        {
            case ITEM_SUBCLASS_GEM_RED:
            case ITEM_SUBCLASS_GEM_ORANGE:
                return SPELL_INGEST_RUBY;
            case ITEM_SUBCLASS_GEM_BLUE:
                return SPELL_INGEST_SAPPHIRE;
            case ITEM_SUBCLASS_GEM_YELLOW:
                return SPELL_INGEST_AMBER;
            case ITEM_SUBCLASS_GEM_GREEN:
                return SPELL_INGEST_EMERALD;
            default:
                return SPELL_INGEST_ONYX;
        }
    }

    uint32 BagTrickDamageSpell(Player* player)
    {
        switch (Setting(player, SETTING_BAG_TRICK))
        {
            case TRICK_FIRE:
                return SPELL_BAG_OF_TRICKS_FIRE;
            case TRICK_FROST:
                return SPELL_BAG_OF_TRICKS_FROST;
            default:
                return SPELL_BAG_OF_TRICKS_DAMAGE;
        }
    }

    uint32 PulseDivisor(uint32 spellId)
    {
        return spellId == SPELL_THORN_BLOOM_DAMAGE || spellId == SPELL_THORN_BLOOM_HEAL ? ThornBloomPulses : 1;
    }

    void TeleportToTele(Player* player, uint32 teleId)
    {
        GameTele const* tele = sObjectMgr->GetGameTele(teleId);
        if (tele)
            player->TeleportTo(tele->mapId, tele->position_x, tele->position_y, tele->position_z, tele->orientation);
    }

    struct RiftTarget
    {
        uint32 mapId;
        Position pos;
    };

    std::unordered_map<ObjectGuid, RiftTarget> Rifts;

    void ChooseLoa(Player* player, uint32 spellId)
    {
        std::vector<uint32> const& loa = LoaEmbraces();
        if (std::find(loa.begin(), loa.end(), spellId) == loa.end() || player->HasSpell(spellId))
            return;

        if (!player->HasSpell(SPELL_EMBRACE_OF_THE_LOA) || player->HasSpellCooldown(SPELL_EMBRACE_OF_THE_LOA))
        {
            Say(player, "You embraced a Loa too recently to change it.");
            return;
        }

        for (uint32 old : loa)
            if (player->HasSpell(old))
                player->removeSpell(old, SPEC_MASK_ALL, false);

        player->learnSpell(spellId);
        player->AddSpellCooldown(SPELL_EMBRACE_OF_THE_LOA, 0, LoaChangeCooldownMs, true);
    }

    void ChooseMoleDestination(Player* player, uint32 teleId)
    {
        for (MenuEntry const& entry : MoleDestinations())
        {
            if (entry.id != teleId || player->GetLevel() < entry.minLevel)
                continue;

            if (!player->HasSpell(SPELL_MOLE_MACHINE) || player->IsInCombat() || player->GetMap()->Instanceable())
                return;

            TeleportToTele(player, teleId);
            return;
        }
    }

    void ApplyVisage(Player* player)
    {
        player->RemoveAurasDueToSpell(SPELL_VISAGE_FORM);
        player->CastSpell(player, SPELL_VISAGE_FORM, true);
    }

    void ChooseIdentity(Player* player, uint32 race)
    {
        for (MenuEntry const& entry : IdentityRaces())
        {
            if (entry.id != race)
                continue;

            SetSetting(player, SETTING_IDENTITY_RACE, race);
            if (player->HasAura(SPELL_VISAGE_FORM))
                ApplyVisage(player);

            return;
        }
    }
}

class spell_race_scaled_damage : public SpellScript
{
    PrepareSpellScript(spell_race_scaled_damage);

    void HandleDamage(SpellEffIndex effIndex)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target)
            return;

        uint32 damage = ScaledAmount(caster, GetSpellInfo(), DamagePerLevel, PowerCoefficient);
        damage /= PulseDivisor(GetSpellInfo()->Id);
        damage = caster->SpellDamageBonusDone(target, GetSpellInfo(), damage, SPELL_DIRECT_DAMAGE, effIndex);
        damage = target->SpellDamageBonusTaken(caster, GetSpellInfo(), damage, SPELL_DIRECT_DAMAGE);
        SetHitDamage(int32(damage));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_scaled_damage::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

class spell_race_scaled_heal : public SpellScript
{
    PrepareSpellScript(spell_race_scaled_heal);

    void HandleHeal(SpellEffIndex effIndex)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target)
            return;

        uint32 heal = ScaledAmount(caster, GetSpellInfo(), HealPerLevel, HealCoefficient);
        heal /= PulseDivisor(GetSpellInfo()->Id);
        heal = caster->SpellHealingBonusDone(target, GetSpellInfo(), heal, HEAL, effIndex);
        heal = target->SpellHealingBonusTaken(caster, GetSpellInfo(), heal, HEAL);
        SetHitHeal(int32(heal));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_scaled_heal::HandleHeal, EFFECT_0, SPELL_EFFECT_HEAL);
    }
};

class spell_race_scaled_bleed : public AuraScript
{
    PrepareAuraScript(spell_race_scaled_bleed);

    void CalculateAmount(AuraEffect const*  , int32& amount, bool&  )
    {
        Unit* caster = GetCaster();
        if (!caster)
            return;

        amount = int32(ScaledAmount(caster, GetSpellInfo(), BleedPerLevel, BleedCoefficient) / BleedTicks);
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_race_scaled_bleed::CalculateAmount, EFFECT_1, SPELL_AURA_PERIODIC_DAMAGE);
    }
};

namespace
{
    int32 PctOfMaxHealth(Unit const* target, int32 percent, uint32 ticks)
    {
        return int32(CalculatePct(target->GetMaxHealth(), percent) / ticks);
    }
}

class spell_race_tidal_mend : public AuraScript
{
    PrepareAuraScript(spell_race_tidal_mend);

    void CalculateAmount(AuraEffect const*  , int32& amount, bool& canBeRecalculated)
    {
        canBeRecalculated = false;
        amount = PctOfMaxHealth(GetUnitOwner(), TidalMendPct, TidalMendTicks);
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_race_tidal_mend::CalculateAmount, EFFECT_0, SPELL_AURA_PERIODIC_HEAL);
    }
};

class spell_race_regeneratin : public AuraScript
{
    PrepareAuraScript(spell_race_regeneratin);

    void CalculateAmount(AuraEffect const*  , int32& amount, bool& canBeRecalculated)
    {
        canBeRecalculated = false;
        amount = PctOfMaxHealth(GetUnitOwner(), RegenerationPct, RegenerationTicks);
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_race_regeneratin::CalculateAmount, EFFECT_0, SPELL_AURA_PERIODIC_HEAL);
    }
};

class spell_race_lights_judgment : public SpellScript
{
    PrepareSpellScript(spell_race_lights_judgment);

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target)
            return;

        ObjectGuid targetGuid = target->GetGUID();
        caster->m_Events.AddEventAtOffset([caster, targetGuid]()
        {
            Unit* center = ObjectAccessor::GetUnit(*caster, targetGuid);
            if (!caster->IsAlive() || !center)
                return;

            for (Unit* unit : HostilesAround(caster, center, LightsJudgmentRadius))
                caster->CastSpell(unit, SPELL_LIGHTS_JUDGMENT_DAMAGE, true);
        }, Milliseconds(LightsJudgmentDelayMs));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_lights_judgment::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_fireblood : public SpellScript
{
    PrepareSpellScript(spell_race_fireblood);

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        std::vector<uint32> removable;
        for (auto const& [spellId, application] : caster->GetAppliedAuras())
        {
            SpellInfo const* info = application->GetBase()->GetSpellInfo();
            if (application->IsPositive())
                continue;

            bool bleed = info->GetAllEffectsMechanicMask() & (1ULL << MECHANIC_BLEED);
            bool dispellable = info->Dispel == DISPEL_MAGIC || info->Dispel == DISPEL_CURSE
                || info->Dispel == DISPEL_DISEASE || info->Dispel == DISPEL_POISON;
            if (bleed || dispellable)
                removable.push_back(spellId);
        }

        std::sort(removable.begin(), removable.end());
        removable.erase(std::unique(removable.begin(), removable.end()), removable.end());
        for (uint32 spellId : removable)
            caster->RemoveAurasDueToSpell(spellId);

        int32 pct = FirebloodBasePct + int32(std::min<uint32>(removable.size(), FirebloodMaxRemoved));
        caster->CastCustomSpell(caster, SPELL_FIREBLOOD_BUFF, &pct, nullptr, nullptr, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_fireblood::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_bull_rush : public SpellScript
{
    PrepareSpellScript(spell_race_bull_rush);

    SpellCastResult CheckCast()
    {
        return GetCaster()->HasUnitState(UNIT_STATE_ROOT) ? SPELL_FAILED_ROOTED : SPELL_CAST_OK;
    }

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        float startX = caster->GetPositionX();
        float startY = caster->GetPositionY();
        Position dest = caster->GetFirstCollisionPosition(BullRushDistance, 0.0f);
        float pathX = dest.GetPositionX() - startX;
        float pathY = dest.GetPositionY() - startY;
        float pathLengthSq = pathX * pathX + pathY * pathY;

        if (pathLengthSq > 0.01f)
        {
            for (Unit* unit : HostilesAround(caster, caster, BullRushDistance + BullRushHitRadius))
            {
                float toX = unit->GetPositionX() - startX;
                float toY = unit->GetPositionY() - startY;
                float along = std::clamp((toX * pathX + toY * pathY) / pathLengthSq, 0.0f, 1.0f);
                if (unit->GetExactDist2d(startX + pathX * along, startY + pathY * along) <= BullRushHitRadius)
                    caster->CastSpell(unit, SPELL_BULL_RUSH_KNOCKDOWN, true);
            }
        }

        caster->GetMotionMaster()->MoveCharge(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), BullRushSpeed);
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_race_bull_rush::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_race_bull_rush::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_pounce : public SpellScript
{
    PrepareSpellScript(spell_race_pounce);

    SpellCastResult CheckCast()
    {
        return GetCaster()->HasUnitState(UNIT_STATE_ROOT) ? SPELL_FAILED_ROOTED : SPELL_CAST_OK;
    }

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target)
            return;

        float gap = std::max(caster->GetCombatReach() + target->GetCombatReach(), 1.0f);
        float angle = target->GetAngle(caster);
        float x = target->GetPositionX() + std::cos(angle) * gap;
        float y = target->GetPositionY() + std::sin(angle) * gap;
        float travelMs = caster->GetExactDist2d(x, y) / PounceSpeed * 1000.0f;
        caster->GetMotionMaster()->MoveCharge(x, y, target->GetPositionZ(), PounceSpeed);

        ObjectGuid targetGuid = target->GetGUID();
        caster->m_Events.AddEventAtOffset([caster, targetGuid]()
        {
            Unit* victim = ObjectAccessor::GetUnit(*caster, targetGuid);
            if (caster->IsAlive() && victim && victim->IsAlive())
                caster->CastSpell(victim, SPELL_POUNCE_DAZE, true);
        }, Milliseconds(uint32(travelMs) + 100));
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_race_pounce::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_race_pounce::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_bag_of_tricks : public SpellScript
{
    PrepareSpellScript(spell_race_bag_of_tricks);

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target)
            return;

        if (caster->IsValidAttackTarget(target))
        {
            caster->CastSpell(target, caster->IsPlayer() ? BagTrickDamageSpell(caster->ToPlayer()) : uint32(SPELL_BAG_OF_TRICKS_DAMAGE), true);
            caster->CastSpell(target, SPELL_BAG_OF_TRICKS_SLOW, true);
        }
        else
        {
            caster->CastSpell(target, SPELL_BAG_OF_TRICKS_HEAL, true);
            caster->CastSpell(target, SPELL_BAG_OF_TRICKS_SPEED, true);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_bag_of_tricks::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_wing_buffet : public SpellScript
{
    PrepareSpellScript(spell_race_wing_buffet);

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target || IsBoss(target))
            return;

        target->KnockbackFrom(caster->GetPositionX(), caster->GetPositionY(), KnockSpeedXY, KnockSpeedZ);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_wing_buffet::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_azerite_surge : public SpellScript
{
    PrepareSpellScript(spell_race_azerite_surge);

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        std::list<Unit*> hostiles = HostilesAround(caster, caster, SurgeRadius);
        for (Unit* unit : hostiles)
            caster->CastSpell(unit, SPELL_AZERITE_SURGE_DAMAGE, true);

        if (caster->GetLevel() >= SurgeHealLevel)
            for (Unit* unit : FriendsAround(caster, caster, SurgeRadius))
                caster->CastSpell(unit, SPELL_AZERITE_SURGE_HEAL, true);

        if (caster->GetLevel() >= SurgeStrikeLevel && !hostiles.empty())
        {
            Unit* healthiest = *std::max_element(hostiles.begin(), hostiles.end(),
                [](Unit const* a, Unit const* b) { return a->GetHealth() < b->GetHealth(); });
            caster->CastSpell(healthiest, SPELL_AZERITE_SURGE_STRIKE, true);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_azerite_surge::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_thorn_bloom : public SpellScript
{
    PrepareSpellScript(spell_race_thorn_bloom);

    static void Pulse(Unit* caster, Unit* center)
    {
        uint32 struck = 0;
        for (Unit* unit : HostilesAround(caster, center, ThornBloomRadius))
        {
            if (struck++ >= ThornBloomMaxTargets)
                break;

            caster->CastSpell(unit, SPELL_THORN_BLOOM_DAMAGE, true);
        }

        uint32 healed = 0;
        for (Unit* unit : FriendsAround(caster, center, ThornBloomRadius))
        {
            if (healed++ >= ThornBloomMaxTargets)
                break;

            caster->CastSpell(unit, SPELL_THORN_BLOOM_HEAL, true);
        }
    }

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        Unit* center = GetHitUnit();
        if (!center)
            return;

        Pulse(caster, center);

        ObjectGuid centerGuid = center->GetGUID();
        for (uint32 pulse = 1; pulse < ThornBloomPulses; ++pulse)
            caster->m_Events.AddEventAtOffset([caster, centerGuid]()
            {
                Unit* target = ObjectAccessor::GetUnit(*caster, centerGuid);
                if (caster->IsAlive() && target)
                    Pulse(caster, target);
            }, Milliseconds(ThornBloomPulseDelayMs * pulse));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_thorn_bloom::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_cantrips : public SpellScript
{
    PrepareSpellScript(spell_race_cantrips);

    void HandleDummy(SpellEffIndex  )
    {
        SummonInFront(GetCaster(), RACIAL_GO_MAILBOX, MailboxLifetimeSec);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_cantrips::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_spatial_rift : public SpellScript
{
    PrepareSpellScript(spell_race_spatial_rift);

    void HandleDummy(SpellEffIndex  )
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        if (player->HasAura(SPELL_SPATIAL_RIFT_OPEN))
        {
            auto it = Rifts.find(player->GetGUID());
            if (it != Rifts.end() && it->second.mapId == player->GetMapId())
                player->NearTeleportTo(it->second.pos.GetPositionX(), it->second.pos.GetPositionY(), it->second.pos.GetPositionZ(),
                                       player->GetOrientation());

            player->AddSpellCooldown(SPELL_SPATIAL_RIFT, 0, SpatialRiftCooldownMs, true);
            player->RemoveAurasDueToSpell(SPELL_SPATIAL_RIFT_OPEN);
            return;
        }

        Rifts[player->GetGUID()] = { player->GetMapId(), player->GetFirstCollisionPosition(SpatialRiftRange, 0.0f) };
        player->CastSpell(player, SPELL_SPATIAL_RIFT_OPEN, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_spatial_rift::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_spatial_rift_open : public AuraScript
{
    PrepareAuraScript(spell_race_spatial_rift_open);

    void HandleRemove(AuraEffect const*  , AuraEffectHandleModes  )
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player)
            return;

        Rifts.erase(player->GetGUID());
        if (GetTargetApplication()->GetRemoveMode() == AURA_REMOVE_BY_EXPIRE)
            player->AddSpellCooldown(SPELL_SPATIAL_RIFT, 0, SpatialRiftCooldownMs, true);
    }

    void Register() override
    {
        OnEffectRemove += AuraEffectRemoveFn(spell_race_spatial_rift_open::HandleRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_race_forge_of_light : public SpellScript
{
    PrepareSpellScript(spell_race_forge_of_light);

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        SummonInFront(caster, RACIAL_GO_ANVIL, ForgeLifetimeSec);
        caster->CastSpell(caster, SPELL_FORGE_OF_LIGHT_SKILL, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_forge_of_light::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_embrace_of_the_loa : public SpellScript
{
    PrepareSpellScript(spell_race_embrace_of_the_loa);

    void HandleDummy(SpellEffIndex  )
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        ClearGossipMenuFor(player);
        for (uint32 spellId : LoaEmbraces())
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, info->SpellName[0], MENU_LOA, spellId);

        OpenMenu(player, MENU_LOA);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_embrace_of_the_loa::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_bwonsamdi_shadow : public SpellScript
{
    PrepareSpellScript(spell_race_bwonsamdi_shadow);

    void HandleDamage(SpellEffIndex effIndex)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!target)
            return;

        uint32 damage = ScaledAmount(caster, GetSpellInfo(), DamagePerLevel, PowerCoefficient) / 2;
        damage = caster->SpellDamageBonusDone(target, GetSpellInfo(), damage, SPELL_DIRECT_DAMAGE, effIndex);
        damage = target->SpellDamageBonusTaken(caster, GetSpellInfo(), damage, SPELL_DIRECT_DAMAGE);
        SetHitDamage(int32(damage));

        HealInfo heal(caster, caster, damage, GetSpellInfo(), GetSpellInfo()->GetSchoolMask());
        caster->HealBySpell(heal);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_bwonsamdi_shadow::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

class spell_race_kimbul_bleed : public AuraScript
{
    PrepareAuraScript(spell_race_kimbul_bleed);

    void CalculateAmount(AuraEffect const*  , int32& amount, bool&  )
    {
        if (Unit* caster = GetCaster())
            amount = int32(ScaledAmount(caster, GetSpellInfo(), BleedPerLevel, BleedCoefficient) / BleedTicks);
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_race_kimbul_bleed::CalculateAmount, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE);
    }
};

class spell_race_mole_machine : public SpellScript
{
    PrepareSpellScript(spell_race_mole_machine);

    SpellCastResult CheckCast()
    {
        Unit* caster = GetCaster();
        if (caster->IsInCombat())
            return SPELL_FAILED_AFFECTING_COMBAT;

        return caster->GetMap()->Instanceable() ? SPELL_FAILED_NOT_HERE : SPELL_CAST_OK;
    }

    void HandleDummy(SpellEffIndex  )
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        ClearGossipMenuFor(player);
        for (MenuEntry const& entry : MoleDestinations())
            if (player->GetLevel() >= entry.minLevel && sObjectMgr->GetGameTele(entry.id))
                AddGossipItemFor(player, GOSSIP_ICON_TAXI, entry.label, MENU_MOLE_MACHINE, entry.id);

        OpenMenu(player, MENU_MOLE_MACHINE);
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_race_mole_machine::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_race_mole_machine::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_holo : public SpellScript
{
    PrepareSpellScript(spell_race_holo);

    void HandleDummy(SpellEffIndex  )
    {
        Unit* caster = GetCaster();
        std::list<Unit*> hostiles = HostilesAround(caster, caster, DecoyTauntRadius);
        for (float angle : { float(M_PI_2), float(-M_PI_2) })
        {
            Position pos = caster->GetFirstCollisionPosition(SummonDistance, angle);
            TempSummon* decoy = caster->SummonCreature(RACIAL_CREATURE_HOLO_DECOY, pos, TEMPSUMMON_TIMED_DESPAWN, DecoyLifetimeMs);
            if (!decoy)
                continue;

            decoy->SetFaction(caster->GetFaction());
            decoy->SetLevel(caster->GetLevel());
            decoy->SetMaxHealth(std::max<uint32>(1, CalculatePct(caster->GetMaxHealth(), DecoyHealthPct)));
            decoy->SetHealth(decoy->GetMaxHealth());
            caster->CastSpell(decoy, SpellCloneMe, true);

            for (Unit* hostile : hostiles)
                if (hostile->IsEngagedBy(caster))
                    hostile->AddThreat(decoy, DecoyThreat);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_holo::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_make_camp : public SpellScript
{
    PrepareSpellScript(spell_race_make_camp);

    SpellCastResult CheckCast()
    {
        Unit* caster = GetCaster();
        if (caster->GetMap()->Instanceable())
            return SPELL_FAILED_NOT_HERE;

        return caster->IsOutdoors() ? SPELL_CAST_OK : SPELL_FAILED_ONLY_OUTDOORS;
    }

    void HandleDummy(SpellEffIndex  )
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        SetSetting(player, SETTING_CAMP_MAP, player->GetMapId());
        SetSetting(player, SETTING_CAMP_X, FloatToBits(player->GetPositionX()));
        SetSetting(player, SETTING_CAMP_Y, FloatToBits(player->GetPositionY()));
        SetSetting(player, SETTING_CAMP_Z, FloatToBits(player->GetPositionZ()));
        SetSetting(player, SETTING_CAMP_EXPIRES, uint32(GameTime::GetGameTime().count()) + CampLifetimeSec);
        SummonInFront(player, RACIAL_GO_CAMPFIRE, CampLifetimeSec);
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_race_make_camp::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_race_make_camp::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_return_to_camp : public SpellScript
{
    PrepareSpellScript(spell_race_return_to_camp);

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return SPELL_FAILED_BAD_TARGETS;

        if (player->IsInCombat())
            return SPELL_FAILED_AFFECTING_COMBAT;

        if (Setting(player, SETTING_CAMP_EXPIRES) <= uint32(GameTime::GetGameTime().count()))
        {
            Say(player, "You have no camp. Use Make Camp first.");
            return SPELL_FAILED_NOT_HERE;
        }

        return SPELL_CAST_OK;
    }

    void HandleDummy(SpellEffIndex  )
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        player->TeleportTo(Setting(player, SETTING_CAMP_MAP), BitsToFloat(Setting(player, SETTING_CAMP_X)),
                           BitsToFloat(Setting(player, SETTING_CAMP_Y)), BitsToFloat(Setting(player, SETTING_CAMP_Z)),
                           player->GetOrientation());
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_race_return_to_camp::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_race_return_to_camp::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_rummage_your_bag : public SpellScript
{
    PrepareSpellScript(spell_race_rummage_your_bag);

    void HandleDummy(SpellEffIndex  )
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        uint32 trick = (Setting(player, SETTING_BAG_TRICK) + urand(1, TRICK_COUNT - 1)) % TRICK_COUNT;
        SetSetting(player, SETTING_BAG_TRICK, trick);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_rummage_your_bag::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_visage : public SpellScript
{
    PrepareSpellScript(spell_race_visage);

    void HandleDummy(SpellEffIndex  )
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        if (player->HasAura(SPELL_VISAGE_FORM))
            player->RemoveAurasDueToSpell(SPELL_VISAGE_FORM);
        else
            ApplyVisage(player);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_visage::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_visage_form : public AuraScript
{
    PrepareAuraScript(spell_race_visage_form);

    void HandleApply(AuraEffect const*  , AuraEffectHandleModes  )
    {
        Player* player = GetTarget()->ToPlayer();
        if (!player)
            return;

        uint32 race = Setting(player, SETTING_IDENTITY_RACE);
        PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race ? race : uint32(RACE_HUMAN), player->getClass());
        if (info)
            player->SetDisplayId(player->getGender() == GENDER_MALE ? info->displayId_m : info->displayId_f);
    }

    void HandleRemove(AuraEffect const*  , AuraEffectHandleModes  )
    {
        GetTarget()->DeMorph();
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_race_visage_form::HandleApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        OnEffectRemove += AuraEffectRemoveFn(spell_race_visage_form::HandleRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

class spell_race_chosen_identity : public SpellScript
{
    PrepareSpellScript(spell_race_chosen_identity);

    void HandleDummy(SpellEffIndex  )
    {
        Player* player = GetCaster()->ToPlayer();
        if (!player)
            return;

        ClearGossipMenuFor(player);
        for (MenuEntry const& entry : IdentityRaces())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, entry.label, MENU_CHOSEN_IDENTITY, entry.id);

        OpenMenu(player, MENU_CHOSEN_IDENTITY);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_race_chosen_identity::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_ingest_minerals : public SpellScript
{
    PrepareSpellScript(spell_race_ingest_minerals);

    SpellCastResult CheckCast()
    {
        Player* player = GetCaster()->ToPlayer();
        return player && FindGem(player) ? SPELL_CAST_OK : SPELL_FAILED_REAGENTS;
    }

    void HandleDummy(SpellEffIndex  )
    {
        Player* player = GetCaster()->ToPlayer();
        Item* gem = player ? FindGem(player) : nullptr;
        if (!gem)
            return;

        uint32 buff = IngestBuffForGem(gem->GetTemplate()->SubClass);
        uint32 count = 1;
        player->DestroyItemCount(gem, count, true);

        for (uint32 old : { uint32(SPELL_INGEST_AMBER), uint32(SPELL_INGEST_EMERALD), uint32(SPELL_INGEST_ONYX),
                            uint32(SPELL_INGEST_RUBY), uint32(SPELL_INGEST_SAPPHIRE) })
            player->RemoveAurasDueToSpell(old);

        player->CastSpell(player, buff, true);
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_race_ingest_minerals::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_race_ingest_minerals::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_race_quiet_contemplation : public AuraScript
{
    PrepareAuraScript(spell_race_quiet_contemplation);

    void CalculateHealth(AuraEffect const*  , int32& amount, bool& canBeRecalculated)
    {
        canBeRecalculated = false;
        amount = PctOfMaxHealth(GetUnitOwner(), QuietContemplationPct, QuietContemplationTicks);
    }

    void CalculateMana(AuraEffect const*  , int32& amount, bool& canBeRecalculated)
    {
        canBeRecalculated = false;
        amount = int32(CalculatePct(GetUnitOwner()->GetMaxPower(POWER_MANA), QuietContemplationPct) / QuietContemplationTicks);
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_race_quiet_contemplation::CalculateHealth, EFFECT_0, SPELL_AURA_PERIODIC_HEAL);
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_race_quiet_contemplation::CalculateMana, EFFECT_1, SPELL_AURA_PERIODIC_ENERGIZE);
    }
};

class spell_race_rootwalking : public SpellScript
{
    PrepareSpellScript(spell_race_rootwalking);

    SpellCastResult CheckCast()
    {
        Unit* caster = GetCaster();
        if (caster->IsInCombat())
            return SPELL_FAILED_AFFECTING_COMBAT;

        return sObjectMgr->GetGameTele(RACIAL_TELE_WORLD_TREE) ? SPELL_CAST_OK : SPELL_FAILED_NOT_HERE;
    }

    void HandleDummy(SpellEffIndex  )
    {
        if (Player* player = GetCaster()->ToPlayer())
            TeleportToTele(player, RACIAL_TELE_WORLD_TREE);
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_race_rootwalking::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_race_rootwalking::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class race_racials_player : public PlayerScript
{
public:
    race_racials_player() : PlayerScript("race_racials_player", {
        PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_PLAYER_JUST_DIED, PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_BEFORE_LOOT_MONEY, PLAYERHOOK_ON_BEFORE_UPDATE, PLAYERHOOK_ON_GOSSIP_SELECT, PLAYERHOOK_ON_LOOT_ITEM }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (Knows(player, SPELL_SKELETON_PINKIE))
        {
            if (!player->HasSpell(SpellPickLock))
                player->learnSpell(SpellPickLock);

            uint16 value = uint16(std::min<uint32>(player->GetLevel() * 5u, 375u));
            if (player->GetSkillValue(SkillLockpicking) < value)
                player->SetSkill(SkillLockpicking, 0, value, 375);
        }

        if (Knows(player, SPELL_ALPACA_SADDLEBAGS) && !Setting(player, SETTING_SADDLEBAG_GIVEN) &&
            player->AddItem(RACIAL_ITEM_ALPACA_BAG, 1))
            SetSetting(player, SETTING_SADDLEBAG_GIVEN, 1);

        RefreshOutOfCombatSpeed(player);
    }

    void OnPlayerGossipSelect(Player* player, uint32 menuId, uint32  , uint32 action) override
    {
        switch (menuId)
        {
            case MENU_LOA:
                ChooseLoa(player, action);
                break;
            case MENU_MOLE_MACHINE:
                ChooseMoleDestination(player, action);
                break;
            case MENU_CHOSEN_IDENTITY:
                ChooseIdentity(player, action);
                break;
            default:
                return;
        }

        CloseGossipMenuFor(player);
    }

    void OnPlayerLootItem(Player* player, Item* item, uint32  , ObjectGuid  ) override
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (proto && proto->Class == ITEM_CLASS_TRADE_GOODS && proto->SubClass == ITEM_SUBCLASS_MEAT &&
            Knows(player, SPELL_WASTE_NOT_WANT_NOT) && roll_chance_i(WasteNotChance))
            player->AddItem(proto->ItemId, 1);
    }

    void OnPlayerJustDied(Player* player) override
    {
        if (!Knows(player, SPELL_LIGHTS_RECKONING))
            return;

        for (Unit* unit : HostilesAround(player, player, LightsReckoningRadius))
            player->CastSpell(unit, SPELL_LIGHTS_RECKONING_DAMAGE, true);

        for (Unit* unit : FriendsAround(player, player, LightsReckoningRadius))
            if (unit != player)
                player->CastSpell(unit, SPELL_LIGHTS_RECKONING_HEAL, true);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* victim, uint8 xpSource) override
    {
        if (xpSource == XPSOURCE_EXPLORE && Knows(player, SPELL_WIDE_EYED_WONDER))
            AddPct(amount, ExplorationXpPct);

        Creature* creature = victim ? victim->ToCreature() : nullptr;
        if (!creature)
            return;

        uint32 type = creature->GetCreatureTemplate()->type;
        if (type == CREATURE_TYPE_DEMON && Knows(player, SPELL_DEMONBANE))
            AddPct(amount, DemonbanePct);

        if ((type == CREATURE_TYPE_BEAST || type == CREATURE_TYPE_ELEMENTAL) && Knows(player, SPELL_SUBTERRANEAN_PREDATOR))
            AddPct(amount, PredatorXpPct);
    }

    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override
    {
        if (!loot || loot->loot_type != LOOT_CORPSE || !loot->gold || !Knows(player, SPELL_CITY_OF_GOLD))
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
            if (Knows(recipient, SPELL_CITY_OF_GOLD))
                recipient->ModifyMoney(CalculatePct(share, CityOfGoldPct));
    }

    void OnPlayerBeforeUpdate(Player* player, uint32 diff) override
    {

        std::lock_guard<std::recursive_mutex> guard(_tickLock);
        OneSecondTick(player, diff);
        CombatAnalysis(player, diff);
    }

private:

    void OneSecondTick(Player* player, uint32 diff)
    {
        uint32& elapsed = _secondMs[player->GetGUID()];
        elapsed += diff;
        if (elapsed < SecondMs)
            return;

        elapsed -= SecondMs;

        if (Knows(player, SPELL_DUNGEON_DELVER))
        {
            bool indoors = !player->IsOutdoors();
            if (indoors && !player->HasAura(SPELL_DUNGEON_DELVER_SPEED))
                player->CastSpell(player, SPELL_DUNGEON_DELVER_SPEED, true);
            else if (!indoors)
                player->RemoveAurasDueToSpell(SPELL_DUNGEON_DELVER_SPEED);
        }

        if (Knows(player, SPELL_INNER_PEACE))
        {
            auto [it, fresh] = _rest.try_emplace(player->GetGUID(), player->GetRestBonus());
            float rest = player->GetRestBonus();
            if (!fresh && rest > it->second)
                player->SetRestBonus(rest + (rest - it->second) * (InnerPeaceFactor - 1));

            it->second = player->GetRestBonus();
        }

        if (Knows(player, SPELL_MASTERCRAFT) && player->IsAlive())
        {
            uint32& tool = _toolMs[player->GetGUID()];
            tool += SecondMs;
            if (tool >= MastercraftIntervalMs)
            {
                tool = 0;
                RefreshMastercraft(player);
            }
        }
    }

    void CombatAnalysis(Player* player, uint32 diff)
    {
        if (!player->IsInCombat() || !Knows(player, SPELL_COMBAT_ANALYSIS))
        {
            _combatMs.erase(player->GetGUID());
            return;
        }

        uint32& elapsed = _combatMs[player->GetGUID()];
        elapsed += diff;
        if (elapsed < CombatAnalysisTickMs)
            return;

        elapsed -= CombatAnalysisTickMs;
        player->CastSpell(player, SPELL_COMBAT_ANALYSIS_STACK, true);
    }

    std::recursive_mutex _tickLock;
    std::unordered_map<ObjectGuid, uint32> _combatMs;
    std::unordered_map<ObjectGuid, uint32> _secondMs;
    std::unordered_map<ObjectGuid, uint32> _toolMs;
    std::unordered_map<ObjectGuid, float> _rest;
};

class race_racials_unit : public UnitScript
{
public:
    race_racials_unit() : UnitScript("race_racials_unit", true, {
        UNITHOOK_ON_DAMAGE, UNITHOOK_ON_HEAL, UNITHOOK_ON_AURA_APPLY, UNITHOOK_ON_UNIT_ENTER_COMBAT, UNITHOOK_ON_UNIT_EXIT_COMBAT }) { }

    void OnUnitEnterCombat(Unit* unit, Unit*  ) override { RefreshOutOfCombatSpeed(unit); }
    void OnUnitExitCombat(Unit* unit) override { RefreshOutOfCombatSpeed(unit); }

    void OnHeal(Unit* healer, Unit* receiver, uint32& gain) override
    {
        if (!gain || !healer || !receiver || !healer->IsPlayer())
            return;

        Player* player = healer->ToPlayer();
        if (!RollEmbrace(player, SPELL_EMBRACE_AKUNDA))
            return;

        ObjectGuid receiverGuid = receiver->GetGUID();
        uint32 extra = CalculatePct(gain, AkundaExtraHealPct);
        Defer(player, [player, receiverGuid, extra]()
        {
            Unit* target = ObjectAccessor::GetUnit(*player, receiverGuid);
            SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_EMBRACE_AKUNDA);
            if (!target || !target->IsAlive() || !player->IsAlive())
                return;

            HealInfo heal(player, target, extra, info, SPELL_SCHOOL_MASK_NATURE);
            player->HealBySpell(heal);
        });
    }

    void OnOffensiveEmbrace(Player* player, Unit* victim)
    {
        ObjectGuid victimGuid = victim->GetGUID();
        if (RollEmbrace(player, SPELL_EMBRACE_BWONSAMDI))
            Defer(player, [player, victimGuid]()
            {
                if (Unit* target = ObjectAccessor::GetUnit(*player, victimGuid))
                    player->CastSpell(target, SPELL_BWONSAMDI_SHADOW, true);
            });

        if (RollEmbrace(player, SPELL_EMBRACE_KIMBUL))
            Defer(player, [player, victimGuid]()
            {
                if (Unit* target = ObjectAccessor::GetUnit(*player, victimGuid))
                    player->CastSpell(target, SPELL_KIMBUL_BLEED, true);
            });

        if (RollEmbrace(player, SPELL_EMBRACE_PAKU))
            Defer(player, [player]() { player->CastSpell(player, SPELL_PAKU_BUFF, true); });
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!damage)
            return;

        if (attacker && attacker != victim && attacker->IsPlayer() && victim->IsAlive())
            OnOffensiveEmbrace(attacker->ToPlayer(), victim);

        if (!victim->IsPlayer())
            return;

        Player* player = victim->ToPlayer();

        if (RollEmbrace(player, SPELL_EMBRACE_KRAGWA))
            Defer(player, [player]() { player->CastSpell(player, SPELL_KRAGWA_BUFF, true); });

        if (attacker && attacker != victim && Knows(player, SPELL_NOSE_FOR_TROUBLE) && !player->HasSpellCooldown(SPELL_NOSE_FOR_TROUBLE))
        {
            damage -= std::min(damage, CalculatePct(player->GetMaxHealth(), NosePct));
            player->AddSpellCooldown(SPELL_NOSE_FOR_TROUBLE, 0, NoseCooldownMs);
        }

        if (Knows(player, SPELL_RUGGED_TENACITY))
            damage -= std::min(damage, uint32(player->GetStat(STAT_STAMINA) * RuggedTenacityPerStamina));

        if (Knows(player, SPELL_EMERGENCY_FAILSAFE) && !player->HasSpellCooldown(SPELL_EMERGENCY_FAILSAFE) &&
            damage < player->GetHealth() && player->GetHealth() - damage < CalculatePct(player->GetMaxHealth(), FailsafeHealthPct))
        {
            player->AddSpellCooldown(SPELL_EMERGENCY_FAILSAFE, 0, FailsafeCooldownMs);
            player->m_Events.AddEventAtOffset([player]()
            {
                if (!player->IsAlive())
                    return;

                SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_EMERGENCY_FAILSAFE);
                HealInfo heal(player, player, CalculatePct(player->GetMaxHealth(), FailsafeHealPct), info, SPELL_SCHOOL_MASK_NORMAL);
                player->HealBySpell(heal);
            }, Milliseconds(1));
        }
    }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        SpellInfo const* info = aura->GetSpellInfo();
        if (!info || !unit->IsPlayer())
            return;

        float multiplier = 1.0f;
        if (std::strcmp(info->SpellName[0] ? info->SpellName[0] : "", "Well Fed") == 0)
        {
            if (Knows(unit, SPELL_EPICUREAN))
                multiplier = EpicureanMultiplier;
            else if (Knows(unit, SPELL_HEARTY_MEALS))
                multiplier = HeartyMealsMultiplier;
        }
        else if (info->HasAura(SPELL_AURA_PERIODIC_HEAL) || info->HasAura(SPELL_AURA_PERIODIC_ENERGIZE))
        {
            Unit* caster = aura->GetCaster();
            bool eating = info->AuraInterruptFlags & AURA_INTERRUPT_FLAG_NOT_SEATED;
            bool bandage = info->SpellName[0] && std::strstr(info->SpellName[0], "Bandage");
            if ((eating || bandage) && Knows(caster, SPELL_MAKE_DO))
                multiplier = MakeDoMultiplier;
        }

        if (multiplier == 1.0f)
            return;

        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (AuraEffect* effect = aura->GetEffect(i))
                if (effect->GetBaseAmount())
                    effect->ChangeAmount(int32(effect->GetBaseAmount() * multiplier));
    }
};

void AddSC_race_racials()
{
    RegisterSpellScript(spell_race_scaled_damage);
    RegisterSpellScript(spell_race_scaled_heal);
    RegisterSpellScript(spell_race_scaled_bleed);
    RegisterSpellScript(spell_race_tidal_mend);
    RegisterSpellScript(spell_race_regeneratin);
    RegisterSpellScript(spell_race_lights_judgment);
    RegisterSpellScript(spell_race_fireblood);
    RegisterSpellScript(spell_race_bull_rush);
    RegisterSpellScript(spell_race_pounce);
    RegisterSpellScript(spell_race_bag_of_tricks);
    RegisterSpellScript(spell_race_wing_buffet);
    RegisterSpellScript(spell_race_azerite_surge);
    RegisterSpellScript(spell_race_thorn_bloom);
    RegisterSpellScript(spell_race_cantrips);
    RegisterSpellScript(spell_race_spatial_rift);
    RegisterSpellScript(spell_race_spatial_rift_open);
    RegisterSpellScript(spell_race_forge_of_light);
    RegisterSpellScript(spell_race_embrace_of_the_loa);
    RegisterSpellScript(spell_race_bwonsamdi_shadow);
    RegisterSpellScript(spell_race_kimbul_bleed);
    RegisterSpellScript(spell_race_mole_machine);
    RegisterSpellScript(spell_race_holo);
    RegisterSpellScript(spell_race_make_camp);
    RegisterSpellScript(spell_race_return_to_camp);
    RegisterSpellScript(spell_race_rummage_your_bag);
    RegisterSpellScript(spell_race_visage);
    RegisterSpellScript(spell_race_visage_form);
    RegisterSpellScript(spell_race_chosen_identity);
    RegisterSpellScript(spell_race_ingest_minerals);
    RegisterSpellScript(spell_race_quiet_contemplation);
    RegisterSpellScript(spell_race_rootwalking);
    new race_racials_player();
    new race_racials_unit();
}
