/*
 * mod-loot-goblins: the world boss chain (open world only).
 *
 *  - A goblin that half the zone piles onto panics: its portal is far too big, and Greed steps out of it.
 *  - Greed: 10 minutes, then she leaves with her gold. Shockwaves, chain pulls, charges, gold mines, blinks.
 *  - Kill her within 5 minutes and her death tears the portal open again: hard mode. Until Avarice (Diablo 4) is
 *    ported, the hard-mode boss is a bigger, faster, gilded Greed with Avarice's tricks (teleport slam, golden sludge).
 *
 * Damage is a share of the target's own health, so a crowd of mixed levels can fight her; hits on her are
 * normalised to her level like the goblins'. Everyone who fought the goblin starts in her fight.
 */

#include "LootGoblins.h"
#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameTime.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "WorldPacket.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

using namespace LootGoblins;

namespace
{
    uint32 NowMs() { return GameTime::GetGameTimeMS().count(); }
    std::unordered_map<uint32, uint32> gZoneCooldownUntil;   // zone -> NowMs()

    enum BossEvents : uint16
    {
        EV_TICK = 1,
        EV_SHOCKWAVE,
        EV_SHOCKWAVE_HIT,
        EV_CHAIN,
        EV_CHARGE,
        EV_CHARGE_GO,
        EV_MINES,
        EV_MINES_BOOM,
        EV_TELEPORT,
        EV_SLUDGE,
        EV_INTRO_DONE,
    };
    uint32 const POINT_CHARGE = 9871;

    uint32 Pct(Unit const* target, float pct)
    {
        return std::max<uint32>(1, uint32(float(target->GetMaxHealth()) * pct / 100.0f * frand(0.85f, 1.15f)));
    }

    char const* BossName(uint8 tier) { return tier == 2 ? "Gilded Greed" : "Greed"; }
}

class npc_loot_goblin_boss : public ScriptedAI
{
public:
    explicit npc_loot_goblin_boss(Creature* creature) : ScriptedAI(creature) { }

    uint8 tier = 1;
    EventMap events;
    Position home;
    Position chargeTo;
    std::vector<Position> mines;
    std::vector<std::pair<Position, uint32>> sludge;   // pool, ends at
    uint32 lastFightersMs = 0;
    uint32 nextTeleportHpCheck = 50;
    bool begun = false, intro = true, charging = false, done = false;

    GoblinState* State() { return Find(me->GetGUID()); }

    void InitializeAI() override
    {
        tier = me->GetEntry() == Conf().bossEntryHard ? 2 : 1;
        uint32 now = NowMs();
        GoblinState state;
        state.guid = me->GetGUID();
        state.mapId = me->GetMapId();
        state.kind = Kind::Epic;
        state.rank = Rank::RareElite;
        state.awake = true;
        state.spawnedMs = state.firstHitMs = now;
        state.deadlineMs = now + (tier == 2 ? Conf().hardTime : Conf().greedTime) * IN_MILLISECONDS;
        state.boss = tier;
        Active()[me->GetGUID()] = state;

        home = me->GetPosition();
        lastFightersMs = now;
        me->SetFaction(Conf().hostileFaction);
        me->SetReactState(REACT_PASSIVE);   // the intro: she steps out of the portal first
        me->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
        me->SetCorpseDelay(120);
        if (tier == 2)
            me->SetObjectScale(me->GetObjectScale() * 1.35f);
        for (Mechanics m : { MECHANIC_CHARM, MECHANIC_DISORIENTED, MECHANIC_FEAR, MECHANIC_ROOT, MECHANIC_SILENCE,
                             MECHANIC_SLEEP, MECHANIC_SNARE, MECHANIC_STUN, MECHANIC_FREEZE, MECHANIC_KNOCKOUT,
                             MECHANIC_POLYMORPH, MECHANIC_BANISH, MECHANIC_HORROR, MECHANIC_SAPPED, MECHANIC_GRIP,
                             MECHANIC_DAZE, MECHANIC_INTERRUPT })
            me->ApplySpellImmune(0, IMMUNITY_MECHANIC, m, true);
        me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK, true);
        me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK_DEST, true);
        me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_JUMP, true);
        me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_JUMP_DEST, true);
        me->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);
        events.Reset();
        events.ScheduleEvent(EV_INTRO_DONE, 4s);
        events.ScheduleEvent(EV_TICK, 1s);
    }

    // Called by SummonBoss right after the spawn.
    void Begin(uint8 level, GoblinState const* fighters)
    {
        GoblinState* state = State();
        if (!state)
            return;
        me->SetLevel(std::max<uint8>(1, level));
        if (fighters)
            for (auto const& [guid, p] : fighters->participants)
            {
                Participant& q = state->participants[guid];
                q = p;
                q.lastExtendMs = 0;
            }
        Rescale();
        begun = true;
    }

    void Rescale()
    {
        GoblinState* state = State();
        CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(me->GetLevel(), CLASS_WARRIOR);
        if (!state || !stats)
            return;
        float pct = me->GetMaxHealth() > 1 ? me->GetHealthPct() : 100.0f;
        float perPlayer = tier == 2 ? Conf().hardHealthPerPlayer : Conf().greedHealthPerPlayer;
        uint32 fighters = std::max<uint32>(uint32(state->participants.size()), Conf().bossCrowdMin);
        uint32 max = std::max<uint32>(100, uint32(float(stats->GenerateHealth(me->GetCreatureTemplate())) * perPlayer * float(fighters)));
        if (max <= me->GetMaxHealth())
            return;   // she only ever grows
        me->SetCreateHealth(max);
        me->SetMaxHealth(max);
        me->SetHealth(std::max<uint32>(1, uint32(float(max) * pct / 100.0f)));
    }

    // ---- damage --------------------------------------------------------------------------------
    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType /*type*/, SpellSchoolMask /*school*/) override
    {
        Player* player = attacker ? attacker->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        GoblinState* state = State();
        if (!player || !state || intro || done)
        {
            damage = 0;   // guards and mobs can't take her; nobody can during the intro
            return;
        }
        Participant& p = state->participants[player->GetGUID()];
        bool joined = p.level == 0;
        p.level = uint8(player->GetLevel());
        if (joined)
            Rescale();
        float scaled = float(damage) * LevelDamageRatio(uint8(me->GetLevel()), uint8(player->GetLevel()));
        float cap = float(me->GetMaxHealth()) * Conf().bossMaxHitPercent / 100.0f;
        if (damage)
            damage = uint32(std::max(1.0f, std::min(scaled, cap)));
        p.contribution += float(damage);
        p.damage += float(damage);
        lastFightersMs = NowMs();

        if (tier == 1 && me->HealthBelowPctDamaged(int32(nextTeleportHpCheck), damage) && nextTeleportHpCheck > 0)
        {
            nextTeleportHpCheck = nextTeleportHpCheck > 25 ? nextTeleportHpCheck - 25 : 0;
            events.ScheduleEvent(EV_TELEPORT, 500ms);
        }
    }

    // Her swings take a share of the target's health, whatever its level.
    void DamageDealt(Unit* victim, uint32& damage, DamageEffectType type, SpellSchoolMask /*school*/) override
    {
        if (type == DIRECT_DAMAGE && victim && victim->GetCharmerOrOwnerPlayerOrPlayerItself() && damage)
            damage = Pct(victim, Conf().bossMeleePct * (tier == 2 ? 1.3f : 1.0f));
    }

    void Hit(Unit* target, float pct)
    {
        if (!target || !target->IsAlive())
            return;
        uint32 dmg = Pct(target, pct * (tier == 2 ? 1.25f : 1.0f));
        Unit::DealDamage(me, target, dmg, nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
    }

    std::vector<Player*> Fighters(float range)
    {
        std::vector<Player*> out;
        if (GoblinState* state = State())
            for (auto const& [guid, p] : state->participants)
                if (Player* pl = ObjectAccessor::GetPlayer(*me, guid))
                    if (pl->IsAlive() && pl->IsInMap(me) && pl->GetDistance(me) <= range && !pl->IsGameMaster())
                        out.push_back(pl);
        return out;
    }

    std::vector<Player*> PlayersNear(Position const& at, float range)
    {
        std::vector<Player*> out;
        me->GetMap()->DoForAllPlayers([&](Player* pl)
        {
            if (pl->IsAlive() && pl->InSamePhase(me) && !pl->IsGameMaster() && pl->GetExactDist2d(at.GetPositionX(), at.GetPositionY()) <= range)
                out.push_back(pl);
        });
        return out;
    }

    void Warn(std::string const& text) { me->TextEmote(text, nullptr, true); }

    // ---- fighting --------------------------------------------------------------------------------
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        // An open-world boss doesn't reset when its target dies or runs: it turns on whoever is left.
        if (done)
            return;
        auto closeBy = Fighters(60.0f);
        if (!closeBy.empty())
            AttackStart(closeBy.front());
    }

    void PickVictim()
    {
        if (me->GetVictim() && me->GetVictim()->IsAlive())
            return;
        auto closeBy = Fighters(60.0f);
        if (!closeBy.empty())
            AttackStart(*std::min_element(closeBy.begin(), closeBy.end(), [&](Player* a, Player* b) { return a->GetDistance(me) < b->GetDistance(me); }));
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == POINT_MOTION_TYPE && id == POINT_CHARGE && charging)
        {
            charging = false;
            me->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK_UNARMED);
            for (Player* pl : PlayersNear(me->GetPosition(), 7.0f))
            {
                Hit(pl, Conf().bossAbilityPct * 0.9f);
                pl->KnockbackFrom(me->GetPositionX(), me->GetPositionY(), 14.0f, 7.0f);
            }
            Coins(3);
            me->SetReactState(REACT_AGGRESSIVE);
            PickVictim();
        }
    }

    void Coins(uint32 n)
    {
        for (uint32 i = 0; i < n; ++i)
        {
            Position at = me->GetFirstCollisionPosition(frand(1.0f, 6.0f), frand(0.0f, 2.0f * float(M_PI)));
            me->SummonCreature(Conf().coinEntry, at, TEMPSUMMON_TIMED_DESPAWN, 1500);
        }
    }

    void Marker(Position const& at, uint32 ms)
    {
        if (TempSummon* m = me->SummonCreature(Conf().pileEntry, at, TEMPSUMMON_TIMED_DESPAWN, ms))
        {
            m->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
            m->SetReactState(REACT_PASSIVE);
            m->SetFaction(35);
            m->SetWanderDistance(0.0f);
            m->SetDefaultMovementType(IDLE_MOTION_TYPE);
            m->GetMotionMaster()->MoveIdle();
        }
    }

    void Teleport(bool slam)
    {
        me->SummonCreature(Conf().portalEntry, me->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 2500);
        Position to = home;
        auto closeBy = Fighters(45.0f);
        if (!closeBy.empty())
        {
            Player* pick = Acore::Containers::SelectRandomContainerElement(closeBy);
            to = pick->GetFirstCollisionPosition(2.0f, frand(0.0f, 2.0f * float(M_PI)));
        }
        else
            to = me->GetFirstCollisionPosition(frand(15.0f, 25.0f), frand(0.0f, 2.0f * float(M_PI)));
        me->NearTeleportTo(to.GetPositionX(), to.GetPositionY(), to.GetPositionZ(), me->GetOrientation());
        me->SummonCreature(Conf().portalEntry, to, TEMPSUMMON_TIMED_DESPAWN, 2500);
        if (slam)
        {
            Warn(Acore::StringFormat("{} teleports - and slams the ground!", BossName(tier)));
            events.ScheduleEvent(EV_SHOCKWAVE_HIT, 1200ms);
        }
    }

    void ExecuteEvent(uint32 id)
    {
        switch (id)
        {
            case EV_INTRO_DONE:
                intro = false;
                me->RemoveUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
                me->SetReactState(REACT_AGGRESSIVE);
                PickVictim();
                events.ScheduleEvent(EV_SHOCKWAVE, 10s, 14s);
                events.ScheduleEvent(EV_CHAIN, 16s, 22s);
                events.ScheduleEvent(EV_CHARGE, 22s, 28s);
                events.ScheduleEvent(EV_MINES, 28s, 34s);
                if (tier == 2)
                {
                    events.ScheduleEvent(EV_TELEPORT, 18s, 24s);
                    events.ScheduleEvent(EV_SLUDGE, 14s, 18s);
                }
                break;
            case EV_SHOCKWAVE:
                Warn(Acore::StringFormat("{} raises her hammer - get away from her!", BossName(tier)));
                me->HandleEmoteCommand(EMOTE_ONESHOT_SPELL_PRECAST);
                events.ScheduleEvent(EV_SHOCKWAVE_HIT, 2500ms);
                events.ScheduleEvent(EV_SHOCKWAVE, tier == 2 ? 11s : 14s, tier == 2 ? 14s : 18s);
                break;
            case EV_SHOCKWAVE_HIT:
                me->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK2HTIGHT);
                for (Player* pl : PlayersNear(me->GetPosition(), 12.0f))
                {
                    Hit(pl, Conf().bossAbilityPct);
                    pl->KnockbackFrom(me->GetPositionX(), me->GetPositionY(), 12.0f, 6.0f);
                }
                Coins(4);
                break;
            case EV_CHAIN:
            {
                std::vector<Player*> distant;
                for (Player* pl : Fighters(40.0f))
                    if (pl->GetDistance(me) > 12.0f)
                        distant.push_back(pl);
                if (!distant.empty())
                {
                    Player* pl = Acore::Containers::SelectRandomContainerElement(distant);
                    me->HandleEmoteCommand(EMOTE_ONESHOT_SPELL_CAST);
                    Warn(Acore::StringFormat("{} hooks {} with her chain!", BossName(tier), pl->GetName()));
                    pl->JumpTo(me, 8.0f);
                    Hit(pl, Conf().bossAbilityPct * 0.4f);
                }
                events.ScheduleEvent(EV_CHAIN, 18s, 24s);
                break;
            }
            case EV_CHARGE:
            {
                auto list = Fighters(40.0f);
                std::vector<Player*> distant;
                for (Player* pl : list)
                    if (pl->GetDistance(me) > 10.0f)
                        distant.push_back(pl);
                Player* pl = !distant.empty() ? Acore::Containers::SelectRandomContainerElement(distant)
                                          : (!list.empty() ? Acore::Containers::SelectRandomContainerElement(list) : nullptr);
                if (pl)
                {
                    chargeTo = pl->GetPosition();
                    Warn(Acore::StringFormat("{} glares at {} and lowers her head...", BossName(tier), pl->GetName()));
                    me->SetReactState(REACT_PASSIVE);
                    me->AttackStop();
                    me->StopMoving();
                    me->SetFacingToObject(pl);
                    me->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
                    events.ScheduleEvent(EV_CHARGE_GO, 1500ms);
                }
                events.ScheduleEvent(EV_CHARGE, 22s, 30s);
                break;
            }
            case EV_CHARGE_GO:
                charging = true;
                me->GetMotionMaster()->MoveCharge(chargeTo.GetPositionX(), chargeTo.GetPositionY(), chargeTo.GetPositionZ(), 32.0f, POINT_CHARGE);
                break;
            case EV_MINES:
            {
                Warn(Acore::StringFormat("{} flings her gold around - don't stand on it!", BossName(tier)));
                me->HandleEmoteCommand(EMOTE_ONESHOT_SPELL_CAST);
                mines.clear();
                uint32 n = tier == 2 ? 7 : 5;
                auto list = Fighters(40.0f);
                for (uint32 i = 0; i < n; ++i)
                {
                    Position at = (!list.empty() && i < list.size()) ? list[i]->GetPosition()
                                : me->GetFirstCollisionPosition(frand(4.0f, 16.0f), frand(0.0f, 2.0f * float(M_PI)));
                    mines.push_back(at);
                    Marker(at, 3500);
                }
                events.ScheduleEvent(EV_MINES_BOOM, 3s);
                events.ScheduleEvent(EV_MINES, 28s, 34s);
                break;
            }
            case EV_MINES_BOOM:
                for (Position const& at : mines)
                {
                    me->SummonCreature(Conf().coinEntry, at, TEMPSUMMON_TIMED_DESPAWN, 1500);
                    for (Player* pl : PlayersNear(at, 4.0f))
                        Hit(pl, Conf().bossAbilityPct * 0.7f);
                }
                mines.clear();
                break;
            case EV_TELEPORT:
                Teleport(true);
                if (tier == 2)
                    events.ScheduleEvent(EV_TELEPORT, 18s, 24s);
                break;
            case EV_SLUDGE:
            {
                auto list = Fighters(40.0f);
                if (!list.empty())
                {
                    Player* pl = Acore::Containers::SelectRandomContainerElement(list);
                    Warn(Acore::StringFormat("{} spews molten gold at {}!", BossName(tier), pl->GetName()));
                    sludge.emplace_back(pl->GetPosition(), NowMs() + 12000);
                    Marker(pl->GetPosition(), 12000);
                }
                events.ScheduleEvent(EV_SLUDGE, 14s, 18s);
                break;
            }
            case EV_TICK:
                Tick();
                events.ScheduleEvent(EV_TICK, 1s);
                break;
        }
    }

    // ---- once a second: timers, pins, leash, sludge, leaving -------------------------------------
    void Tick()
    {
        GoblinState* state = State();
        if (!state || done)
            return;
        uint32 now = NowMs();

        for (auto itr = sludge.begin(); itr != sludge.end();)
        {
            if (now >= itr->second) { itr = sludge.erase(itr); continue; }
            for (Player* pl : PlayersNear(itr->first, 3.5f))
                Hit(pl, 5.0f);
            ++itr;
        }

        if (me->GetExactDist2d(home.GetPositionX(), home.GetPositionY()) > Conf().bossLeash && !charging)
        {
            Warn(Acore::StringFormat("{} won't leave her gold - she blinks back to it!", BossName(tier)));
            me->NearTeleportTo(home.GetPositionX(), home.GetPositionY(), home.GetPositionZ(), me->GetOrientation());
        }

        if (!Fighters(80.0f).empty())
            lastFightersMs = now;

        // timers for everyone who fought her (addon bar, or the fatigue bar)
        uint32 left = state->deadlineMs > now ? state->deadlineMs - now : 0;
        uint32 max = (tier == 2 ? Conf().hardTime : Conf().greedTime) * IN_MILLISECONDS;
        uint32 hardLeft = 0;
        if (tier == 1)
        {
            uint32 hardEnd = state->firstHitMs + Conf().greedHardModeTime * IN_MILLISECONDS;
            hardLeft = hardEnd > now ? hardEnd - now : 0;
        }
        for (auto const& [guid, p] : state->participants)
            if (Player* pl = ObjectAccessor::GetPlayer(*me, guid))
                if (pl->IsInMap(me) && pl->GetDistance(me) <= 150.0f)
                {
                    if (HasAddon(pl))
                        SendAddon(pl, Acore::StringFormat("TIME\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", me->GetGUID().GetCounter(),
                            BossName(tier), left, max, tier == 1 ? hardLeft : left, tier == 1 && hardLeft ? "hard" : "boss",
                            uint32(me->GetHealthPct()), BossName(tier)));
                    else if (Conf().mirrorTimer)
                    {
                        WorldPacket data(SMSG_START_MIRROR_TIMER, 21);
                        data << uint32(0) << uint32(left) << uint32(max) << int32(-1) << uint8(0) << uint32(0);
                        pl->SendDirectMessage(&data);
                    }
                }
        if (Conf().mapPins && (now / 1000) % 3 == 0)
            me->GetMap()->DoForAllPlayers([&](Player* pl)
            {
                if (HasAddon(pl) && pl->InSamePhase(me))
                    SendAddon(pl, Acore::StringFormat("SPOT\t{}\t{}\t{:.1f}\t{:.1f}\t{}\t{}\t{}\t{}\t{}", me->GetGUID().GetCounter(), me->GetMapId(),
                        me->GetPositionX(), me->GetPositionY(), BossName(tier), "boss", left, uint32(me->GetHealthPct()), ZoneName(me)));
            });

        if (now >= state->deadlineMs)
            Leave(Acore::StringFormat("{} grows bored and leaves through her portal with all her gold. Better luck next time!", BossName(tier)));
        else if (now - lastFightersMs > 45000)
            Leave(Acore::StringFormat("Nobody is left to fight {} - she leaves with her gold.", BossName(tier)));
    }

    void EndTimers(char const* reason)
    {
        GoblinState* state = State();
        if (!state)
            return;
        for (auto const& [guid, p] : state->participants)
            if (Player* pl = ObjectAccessor::GetPlayer(*me, guid))
            {
                if (HasAddon(pl))
                    SendAddon(pl, Acore::StringFormat("END\t{}\t{}", me->GetGUID().GetCounter(), reason));
                WorldPacket data(SMSG_STOP_MIRROR_TIMER, 4);
                data << uint32(0);
                pl->SendDirectMessage(&data);
            }
        me->GetMap()->DoForAllPlayers([&](Player* pl)
        {
            if (HasAddon(pl))
                SendAddon(pl, Acore::StringFormat("GONE\t{}", me->GetGUID().GetCounter()));
        });
    }

    void Leave(std::string const& text)
    {
        done = true;
        EndTimers("escaped");
        Announce(text);
        me->SetReactState(REACT_PASSIVE);
        me->AttackStop();
        me->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
        me->SummonCreature(Conf().portalEntry, me->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 3000);
        Active().erase(me->GetGUID());
        me->DespawnOrUnsummon(2500ms);
    }

    void JustDied(Unit* /*killer*/) override
    {
        done = true;
        GoblinState* state = State();
        if (!state)
            return;
        EndTimers("killed");
        uint32 fight = NowMs() - state->firstHitMs;
        me->SummonCreature(Conf().deathEntry, me->GetPosition(), TEMPSUMMON_TIMED_DESPAWN, 4000);
        Coins(8);
        DropLoot(me, *state);
        if (tier == 1 && fight <= Conf().greedHardModeTime * IN_MILLISECONDS)
        {
            Announce(Acore::StringFormat("Greed fell in {} in {}! Her portal tears wide open... HARD MODE: something far worse steps out!",
                Clock(fight), ZoneName(me)));
            GoblinState fighters = *state;
            Position at = me->GetPosition();
            Active().erase(me->GetGUID());
            SummonBoss(me, at, 2, &fighters);
            return;
        }
        Announce(Acore::StringFormat("{} was slain in {} after {}!{}", BossName(tier), ZoneName(me), Clock(fight),
            tier == 1 ? Acore::StringFormat(" (Kill her within {} to unlock hard mode.)", Clock(Conf().greedHardModeTime * IN_MILLISECONDS)) : std::string()));
        Active().erase(me->GetGUID());
    }

    void UpdateAI(uint32 diff) override
    {
        if (done)
            return;
        events.Update(diff);
        while (uint32 id = events.ExecuteEvent())
            ExecuteEvent(id);
        if (intro || charging || done)
            return;
        PickVictim();
        if (me->GetVictim())
            DoMeleeAttackIfReady();
    }
};

namespace LootGoblins
{
    bool BossWanted(Creature const* goblin, GoblinState const& state)
    {
        Config const& c = Conf();
        Map* map = goblin ? goblin->GetMap() : nullptr;
        if (!c.bossEnable || !map || map->Instanceable() || goblin->GetLevel() < c.bossMinLevel)
            return false;
        uint32 zone = goblin->GetZoneId();
        auto cd = gZoneCooldownUntil.find(zone);
        if (cd != gZoneCooldownUntil.end() && NowMs() < cd->second)
            return false;
        for (auto const& [guid, s] : Active())
            if (s.boss && s.mapId == goblin->GetMapId())
                if (Creature* other = map->GetCreature(guid))
                    if (other->GetZoneId() == zone)
                        return false;
        uint32 inZone = 0;
        map->DoForAllPlayers([&](Player* pl) { if (pl->GetZoneId() == zone && !pl->IsGameMaster()) ++inZone; });
        uint32 need = std::max<uint32>(c.bossCrowdMin, uint32(std::ceil(float(inZone) * c.bossCrowdPercent / 100.0f)));
        return state.participants.size() >= need;
    }

    Creature* SummonBoss(WorldObject* from, Position const& at, uint8 tier, GoblinState const* fighters)
    {
        Config const& c = Conf();
        Map* map = from ? from->GetMap() : nullptr;
        if (!c.bossEnable || !map)
            return nullptr;
        uint32 entry = tier == 2 ? c.bossEntryHard : c.bossEntryGreed;
        if (!sObjectMgr->GetCreatureTemplate(entry))
            return nullptr;
        TempSummon* boss = map->SummonCreature(entry, at);
        if (!boss)
            return nullptr;
        boss->SetPhaseMask(from->GetPhaseMask(), true);
        uint8 level = 0;
        if (fighters)
            for (auto const& [guid, p] : fighters->participants)
                if (p.level)
                    level = level ? std::min(level, p.level) : p.level;
        if (!level)
            level = from->ToUnit() ? uint8(from->ToUnit()->GetLevel()) : 10;
        if (auto* ai = dynamic_cast<npc_loot_goblin_boss*>(boss->AI()))
            ai->Begin(level, fighters);
        gZoneCooldownUntil[boss->GetZoneId()] = NowMs() + c.bossZoneCooldown * IN_MILLISECONDS;
        if (tier == 1)
            Announce(Acore::StringFormat("GREED has stepped out of a goblin portal in {}! You have {} to bring her down - kill her within {} and something worse comes. Everyone, to arms!",
                ZoneName(boss), Clock(c.greedTime * IN_MILLISECONDS), Clock(c.greedHardModeTime * IN_MILLISECONDS)));
        else
            Announce(Acore::StringFormat("GILDED GREED rises in {}! {} on the clock - the best loot of all waits for those who stand.",
                ZoneName(boss), Clock(c.hardTime * IN_MILLISECONDS)));
        return boss;
    }

    void AddLootGoblinsBossScripts()
    {
        RegisterCreatureAI(npc_loot_goblin_boss);
    }
}
