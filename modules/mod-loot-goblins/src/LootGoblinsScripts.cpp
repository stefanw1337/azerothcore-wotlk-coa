/*
 * mod-loot-goblins: the goblin's AI, the GM commands and the world hooks (natural spawns, healing credit,
 * shorter crowd control, config reload).
 */

#include "LootGoblins.h"

#include "Chat.h"
#include "Config.h"
#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "GridNotifiers.h"
#include "Group.h"
#include "ChatCommand.h"
#include "Creature.h"
#include "CreatureScript.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "UnitScript.h"
#include "WorldPacket.h"
#include "WorldScript.h"
#include "Battleground.h"
#include <algorithm>

using namespace LootGoblins;
using namespace Acore::ChatCommands;

namespace
{
    uint32 NowMs() { return GameTime::GetGameTimeMS().count(); }

    enum class Phase : uint8 { Unaware, Running, PortalCast, Escaping, Gone };

    constexpr uint32 POINT_FLEE = 1;
    constexpr uint32 MIRROR_FATIGUE = 0;

    // Hard CC the goblin shrugs off ("Immune"). Softer effects that land are shortened (see the aura hook).
    Mechanics const IMMUNE[] =
    {
        MECHANIC_CHARM, MECHANIC_DISORIENTED, MECHANIC_FEAR, MECHANIC_ROOT, MECHANIC_SLEEP, MECHANIC_STUN,
        MECHANIC_FREEZE, MECHANIC_KNOCKOUT, MECHANIC_POLYMORPH, MECHANIC_BANISH, MECHANIC_HORROR, MECHANIC_SAPPED,
        MECHANIC_SILENCE, MECHANIC_INTERRUPT, MECHANIC_GRIP, MECHANIC_TURN, MECHANIC_SHACKLE, MECHANIC_IMMUNE_SHIELD
    };
}

// ================================================================================================
// The goblin
// ================================================================================================
struct npc_loot_goblin : public ScriptedAI
{
    explicit npc_loot_goblin(Creature* creature) : ScriptedAI(creature) { }

    Phase phase = Phase::Unaware;
    uint32 baseLevel = 1;
    uint32 nextMoveMs = 0;
    uint32 nextTimerPacketMs = 0;
    uint32 nextSpotMs = 0;

    // ---- map pins (addon) ------------------------------------------------------------------------
    void Broadcast(std::string const& msg)
    {
        if (!Conf().mapPins || !me->GetMap())
            return;
        me->GetMap()->DoForAllPlayers([&](Player* player)
        {
            if (HasAddon(player) && player->InSamePhase(me))
                SendAddon(player, msg);
        });
    }

    void SendSpot(bool force = false)
    {
        GoblinState* state = State();
        uint32 now = NowMs();
        if (!state || (!force && now < nextSpotMs))
            return;
        nextSpotMs = now + 2000;
        uint32 end = state->awake ? std::min(state->deadlineMs, state->firstHitMs + Conf().maxLifetime * IN_MILLISECONDS)
                                  : state->spawnedMs + Conf().unawareTime * IN_MILLISECONDS;
        Broadcast(Acore::StringFormat("SPOT\t{}\t{}\t{:.1f}\t{:.1f}\t{}\t{}\t{}\t{}\t{}", me->GetGUID().GetCounter(), me->GetMapId(),
            me->GetPositionX(), me->GetPositionY(), Info(state->kind).subname, state->awake ? "run" : "idle", end > now ? end - now : 0,
            uint32(me->GetHealthPct()), ZoneName(me)));
    }

    void SendGone() { Broadcast(Acore::StringFormat("GONE\t{}", me->GetGUID().GetCounter())); }
    uint32 nextCoinMs = 0;
    uint32 castEndMs = 0;
    uint32 portalRetryMs = 0;
    bool finalCast = false;
    bool callBoss = false;     // the crowd got big: this portal brings Greed
    bool sized = false;
    uint32 lastPresenceMs = 0;
    uint32 allDeadSinceMs = 0;
    char const* mood = "normal";
    ObjectGuid portal;

    ~npc_loot_goblin() override
    {
        StopTimers();
        Active().erase(me->GetGUID());
    }

    GoblinState* State() { return Find(me->GetGUID()); }

    Kind KindOf() const
    {
        int32 index = int32(me->GetEntry()) - int32(Conf().typeEntryBase);
        index = index >= 0 ? index % int32(RANK_STRIDE) : 0;
        return index < KIND_COUNT ? Kind(index) : Kind::Gold;
    }

    Rank RankOf() const
    {
        int32 index = int32(me->GetEntry()) - int32(Conf().typeEntryBase);
        return index >= 0 ? Rank(std::min<int32>(index / int32(RANK_STRIDE), 3)) : Rank::Normal;
    }

    void InitializeAI() override
    {
        GoblinState state;
        state.guid = me->GetGUID();
        state.mapId = me->GetMapId();
        state.kind = KindOf();
        state.rank = RankOf();
        state.spawnedMs = NowMs();
        Active()[me->GetGUID()] = state;

        if (uint32 display = PickModel())
        {
            me->SetDisplayId(display);
            me->SetNativeDisplayId(display);
        }
        me->SetReactState(REACT_PASSIVE);
        me->SetFaction(Conf().neutralFaction);
        me->SetCorpseDelay(Conf().corpseSeconds);
        for (Mechanics m : IMMUNE)
            me->ApplySpellImmune(0, IMMUNITY_MECHANIC, m, true);
        me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK, true);
        me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_ATTACK_ME, true);
        me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_TAUNT, true);
        baseLevel = me->GetLevel();
        me->GetMotionMaster()->MoveRandom(4.0f);   // shuffles around its spot, counting coins
    }

    void Reset() override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { }   // never resets: it runs until it dies or escapes
    void AttackStart(Unit* /*who*/) override { }
    void MoveInLineOfSight(Unit* /*who*/) override { }

    void RescaleHealth(uint32 participants)
    {
        CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(me->GetLevel(), me->GetCreatureTemplate()->unit_class);
        if (!stats)
            return;
        float pct = me->GetMaxHealth() ? me->GetHealthPct() : 100.0f;
        float health = float(stats->GenerateHealth(me->GetCreatureTemplate())) * Conf().healthMultiplier
            * Conf().rankHealth[uint8(RankOf())];
        health *= 1.0f + Conf().healthPerAttacker / 100.0f * float(participants > 1 ? participants - 1 : 0);
        uint32 max = std::max<uint32>(1, uint32(health));
        me->SetCreateHealth(max);
        me->SetMaxHealth(max);
        me->SetHealth(std::max<uint32>(1, uint32(max * pct / 100.0f)));
    }

    void Wake(Player* by)
    {
        GoblinState* state = State();
        if (!state || state->awake)
            return;
        uint32 now = NowMs();
        state->awake = true;
        state->firstHitMs = now;
        state->deadlineMs = now + Conf().startTime * IN_MILLISECONDS;
        phase = Phase::Running;
        me->SetFaction(Conf().hostileFaction);
        me->SetSpeedRate(MOVE_RUN, Conf().runSpeedRate);
        me->SetSpeed(MOVE_RUN, Conf().runSpeedRate, true);
        me->SetWalk(false);
        me->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
        nextMoveMs = 0;
        if (Conf().announceChase)
            AnnounceZone(me, Acore::StringFormat("The Loot Goblin <{}> in {} is running! It escapes in {} unless you keep hitting it - every hit buys time. Come help!",
                Info(state->kind).subname, ZoneName(me), Clock(Conf().startTime * IN_MILLISECONDS)));
        SendSpot(true);
        if (by)
            me->SetFacingToObject(by);
    }

    uint32 Deadline(GoblinState const& state) const
    {
        uint32 hardLimit = state.firstHitMs + Conf().maxLifetime * IN_MILLISECONDS;
        return std::min(state.deadlineMs, hardLimit);
    }

    // ---- damage --------------------------------------------------------------------------------
    void DamageTaken(Unit* attacker, uint32& damage, DamageEffectType /*type*/, SpellSchoolMask /*school*/) override
    {
        Player* player = attacker ? attacker->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (!player)
        {
            if (Conf().playersOnly)
                damage = 0;   // guards and mobs can't steal the kill
            return;
        }
        if (phase == Phase::Escaping || phase == Phase::Gone)
        {
            damage = 0;
            return;
        }

        GoblinState* state = State();
        if (!state)
            return;
        Wake(player);

        Participant& p = state->participants[player->GetGUID()];
        bool joined = p.level == 0;
        p.level = uint8(player->GetLevel());

        // Level: the goblin drops to the lowest participant's level and never rises again.
        if (Conf().followLowestLevel && player->GetLevel() < me->GetLevel())
        {
            me->SetLevel(player->GetLevel());
            RescaleHealth(uint32(state->participants.size()));
        }
        else if (joined)
            RescaleHealth(uint32(state->participants.size()));
        if (joined)
            CheckCrowd(*state);

        // A level 60 helping a level 10 hits like a (slightly better) level 10; no single hit takes more than a slice.
        float scaled = float(damage) * LevelDamageRatio(uint8(me->GetLevel()), uint8(player->GetLevel()));
        float cap = float(me->GetMaxHealth()) * Conf().maxHitPercent / 100.0f;
        if (damage)
            damage = uint32(std::max(1.0f, std::min(scaled, cap)));
        p.contribution += float(damage);
        p.damage += float(damage);

        // Others need time to get here: early on, its health can't fall faster than a straight line from full
        // to nothing over MinFightSeconds (the hits still count for the loot shares).
        if (Conf().minFightSeconds && damage)
        {
            float elapsed = float(NowMs() - state->firstHitMs) / 1000.0f;
            float floorPct = std::max(0.0f, 1.0f - elapsed / float(Conf().minFightSeconds));
            uint32 floorHp = uint32(float(me->GetMaxHealth()) * floorPct);
            if (floorHp)
                damage = me->GetHealth() > floorHp ? std::min<uint32>(damage, me->GetHealth() - floorHp) : 0;
        }

        // Every hit buys time (once a second per player), never past the hard limit.
        uint32 now = NowMs();
        if (!finalCast && now - p.lastExtendMs >= uint32(Conf().extendCooldown * IN_MILLISECONDS))
        {
            p.lastExtendMs = now;
            uint32 before = state->deadlineMs;
            state->deadlineMs = std::min(state->deadlineMs + uint32(Conf().timePerHit * IN_MILLISECONDS),
                                         state->firstHitMs + Conf().maxLifetime * IN_MILLISECONDS);
            if (state->deadlineMs > before && HasAddon(player))
                SendAddon(player, Acore::StringFormat("ADD\t{}\t{}", me->GetGUID().GetCounter(), state->deadlineMs - before));
        }

        // Coins fly when it's hit.
        if (now >= nextCoinMs)
        {
            nextCoinMs = now + 3000;
            me->SummonCreature(Conf().coinEntry, *me, TEMPSUMMON_TIMED_DESPAWN, 1500);
        }

        // A hit breaks the portal attempt (not the final, uninterruptible one).
        if (phase == Phase::PortalCast && !finalCast)
            InterruptPortal();
    }

    // ---- who is in the fight -----------------------------------------------------------------------
    // Being there counts. Group members of anyone fighting it join in when they are close, so a healer or tank who
    // never hits the goblin still takes part; every second alive within PresenceRange adds presence.
    void TrackPresence(GoblinState& state, uint32 step)
    {
        std::vector<Player*> nearby;
        for (auto const& [guid, p] : state.participants)
            if (Player* pl = ObjectAccessor::GetPlayer(*me, guid))
                if (Group* g = pl->GetGroup())
                    for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
                        if (Player* member = ref->GetSource())
                            if (member->GetMap() == me->GetMap() && !state.participants.count(member->GetGUID())
                                && member->IsAlive() && member->GetDistance(me) <= Conf().presenceRange)
                                nearby.push_back(member);
        for (Player* member : nearby)
        {
            Participant& p = state.participants[member->GetGUID()];
            p.level = uint8(member->GetLevel());
        }
        for (auto& [guid, p] : state.participants)
            if (Player* pl = ObjectAccessor::GetPlayer(*me, guid))
                if (pl->IsAlive() && pl->GetDistance(me) <= Conf().presenceRange)
                    p.presenceMs += step;
    }

    // Everyone who fought it is dead: it doesn't wait around.
    bool CheckAllDead(GoblinState const& state, uint32 now)
    {
        bool anyAlive = false;
        for (auto const& [guid, p] : state.participants)
            if (Player* pl = ObjectAccessor::GetPlayer(*me, guid))
                if (pl->IsAlive() && pl->GetDistance(me) <= 100.0f)
                    anyAlive = true;
        if (anyAlive)
        {
            allDeadSinceMs = 0;
            return false;
        }
        if (!allDeadSinceMs)
            allDeadSinceMs = now;
        if (now - allDeadSinceMs >= Conf().allDeadEscape * IN_MILLISECONDS && phase == Phase::Running)
        {
            StartPortal(true);
            return true;
        }
        return false;
    }

    // ---- the goblin's choice: drag them into more mobs, or not ------------------------------------
    // Strong group (healthy, nobody dead, few mobs on them): it runs through packs. Struggling group (low health,
    // someone dead, many mobs already): it avoids packs. Otherwise it doesn't care.
    int32 Mood(GoblinState const& state, std::list<Creature*> const& around)
    {
        if (!Conf().pullEnable)
            return 0;
        float health = 0.0f;
        uint32 alive = 0, dead = 0;
        for (auto const& [guid, p] : state.participants)
            if (Player* pl = ObjectAccessor::GetPlayer(*me, guid))
            {
                if (pl->IsAlive()) { health += pl->GetHealthPct(); ++alive; }
                else ++dead;
            }
        if (!alive)
            return -1;
        health /= float(alive);
        uint32 engaged = 0;
        for (Creature* c : around)
            if (c->IsInCombat() && c->GetVictim() && c->GetVictim()->GetCharmerOrOwnerPlayerOrPlayerItself())
                ++engaged;
        uint32 maxEngaged = Conf().pullMaxEngagedBase + uint32(state.participants.size()) / 2;
        if (health < Conf().pullHealthLow || dead > 0 || engaged >= maxEngaged)
        {
            mood = "careful";
            return -1;
        }
        if (health >= Conf().pullHealthHigh && engaged < maxEngaged)
        {
            mood = "pulling";
            return 1;
        }
        mood = "normal";
        return 0;
    }

    // ---- moving ---------------------------------------------------------------------------------
    Unit* NearestThreat()
    {
        Unit* nearest = nullptr;
        float best = 100.0f;
        if (GoblinState* state = State())
            for (auto const& [guid, p] : state->participants)
                if (Player* pl = ObjectAccessor::GetPlayer(*me, guid))
                    if (pl->IsAlive() && pl->GetDistance(me) < best)
                    {
                        best = pl->GetDistance(me);
                        nearest = pl;
                    }
        return nearest;
    }

    void Flee()
    {
        GoblinState* state = State();
        Unit* threat = NearestThreat();
        float away = threat ? threat->GetAbsoluteAngle(me) : me->GetOrientation();

        // Idle hostile mobs around: what it could drag the group into.
        std::list<Creature*> around;
        {
            std::list<Unit*> units;
            Acore::AnyUnitInObjectRangeCheck check(me, Conf().fleeDistance + 20.0f);
            Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(me, units, check);
            Cell::VisitObjects(me, searcher, Conf().fleeDistance + 20.0f);
            for (Unit* u : units)
                if (Creature* c = u->ToCreature())
                    if (c != me && !c->IsSummon() && !IsGoblin(c) && threat && c->IsHostileTo(threat))
                        around.push_back(c);
        }
        int32 want = state ? Mood(*state, around) : 0;

        Position best;
        float bestScore = -1e9f;
        for (int i = 0; i < 9; ++i)
        {
            float angle = away + (float(i) - 4.0f) * float(M_PI) / 8.0f + frand(-0.15f, 0.15f);   // fan of +-90 degrees
            Position target = me->GetFirstCollisionPosition(Conf().fleeDistance * frand(0.8f, 1.2f), angle - me->GetOrientation());
            float run = me->GetExactDist2d(&target);
            if (run < 4.0f)
                continue;
            uint32 packs = 0;
            for (Creature* c : around)
                if (!c->IsInCombat() && c->GetExactDist2d(&target) < 12.0f)
                    ++packs;
            float score = run * 0.2f - std::fabs(float(i) - 4.0f) * 0.6f + frand(0.0f, 1.5f);   // mostly away, some wiggle
            if (want > 0) score += float(std::min<uint32>(packs, 4)) * 3.0f;
            else if (want < 0) score -= float(packs) * 5.0f;
            if (score > bestScore)
            {
                bestScore = score;
                best = target;
            }
        }
        if (bestScore < -1e8f)   // cornered: anywhere it can go
            best = me->GetFirstCollisionPosition(Conf().fleeDistance, float(M_PI) / 2.0f * (urand(0, 1) ? 1 : -1));
        me->GetMotionMaster()->Clear(false);
        me->GetMotionMaster()->MovePoint(POINT_FLEE, best, FORCED_MOVEMENT_NONE, 0.0f, true, true);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == POINT_MOTION_TYPE && id == POINT_FLEE && phase == Phase::Running)
            nextMoveMs = 0;
    }

    // ---- escape ---------------------------------------------------------------------------------
    void StartPortal(bool final)
    {
        phase = Phase::PortalCast;
        finalCast = final;
        castEndMs = NowMs() + Conf().portalCastTime;
        me->StopMoving();
        me->GetMotionMaster()->Clear(false);
        me->GetMotionMaster()->MoveIdle();
        me->SetEmoteState(EMOTE_STATE_READY_SPELL_OMNI);
        Position spot = me->GetFirstCollisionPosition(2.5f, 0.0f);
        if (TempSummon* p = me->SummonCreature(Conf().portalEntry, spot, TEMPSUMMON_TIMED_DESPAWN, Conf().portalCastTime + 2500))
            portal = p->GetGUID();
        nextTimerPacketMs = 0;
    }

    void InterruptPortal()
    {
        me->SetEmoteState(EMOTE_ONESHOT_NONE);
        if (Creature* p = ObjectAccessor::GetCreature(*me, portal))
            p->DespawnOrUnsummon();
        portal.Clear();
        phase = Phase::Running;
        portalRetryMs = NowMs() + Conf().portalRetryDelay * IN_MILLISECONDS;
        nextTimerPacketMs = 0;
        nextMoveMs = 0;
        me->HandleEmoteCommand(EMOTE_ONESHOT_WOUND_CRITICAL);
    }

    void Escape()
    {
        phase = Phase::Escaping;
        StopTimers("escaped");
        SendGone();
        me->SetEmoteState(EMOTE_ONESHOT_NONE);
        me->HandleEmoteCommand(EMOTE_ONESHOT_SPELL_CAST_OMNI);
        if (Creature* p = ObjectAccessor::GetCreature(*me, portal))
            me->GetMotionMaster()->MovePoint(0, *p, FORCED_MOVEMENT_NONE, 0.0f, false, true);
        if (GoblinState* state = State())
        {
            if (callBoss)
            {
                Creature* p = ObjectAccessor::GetCreature(*me, portal);
                SummonBoss(me, p ? p->GetPosition() : me->GetPosition(), 1, state);
            }
            else if (Conf().announceEscape && !state->participants.empty())
                Announce(Acore::StringFormat("The Loot Goblin <{}> escaped with its loot in {}!", Info(state->kind).subname, ZoneName(me)));
        }
        Active().erase(me->GetGUID());
        me->DespawnOrUnsummon(1500ms);
    }

    // Enough of the zone came for it: it panics and opens a portal - and something big answers.
    void CheckCrowd(GoblinState const& state)
    {
        if (callBoss || phase == Phase::Escaping || phase == Phase::Gone || !BossWanted(me, state))
            return;
        callBoss = true;
        Announce(Acore::StringFormat("The Loot Goblin <{}> in {} is surrounded and calls for help... the portal it opens is far too big!",
            Info(state.kind).subname, ZoneName(me)));
        StartPortal(true);
    }

    void ForceEscape() { if (phase != Phase::Escaping && phase != Phase::Gone) StartPortal(true); }

    // ---- timer bar --------------------------------------------------------------------------------
    char const* PhaseName() const
    {
        switch (phase)
        {
            case Phase::PortalCast: return finalCast ? "escape" : "portal";
            case Phase::Escaping: return "escape";
            default: return "run";
        }
    }

    void SendTimers(GoblinState const& state, uint32 now)
    {
        uint32 left = Deadline(state) > now ? Deadline(state) - now : 0;
        uint32 hardLeft = state.firstHitMs + Conf().maxLifetime * IN_MILLISECONDS > now ? state.firstHitMs + Conf().maxLifetime * IN_MILLISECONDS - now : 0;
        for (auto const& [guid, p] : state.participants)
            if (Player* pl = ObjectAccessor::GetPlayer(*me, guid))
                if (pl->GetDistance(me) <= Conf().timerRange)
                {
                    if (Conf().addonTimer && HasAddon(pl))
                    {
                        SendAddon(pl, Acore::StringFormat("TIME\t{}\t{}\t{}\t{}\t{}\t{}\t{}", me->GetGUID().GetCounter(),
                            Info(state.kind).subname, left, Conf().maxLifetime * IN_MILLISECONDS, hardLeft, PhaseName(),
                            uint32(me->GetHealthPct())));
                        continue;
                    }
                    if (!Conf().mirrorTimer)
                        continue;
                    WorldPacket data(SMSG_START_MIRROR_TIMER, 21);
                    data << uint32(MIRROR_FATIGUE) << uint32(left) << uint32(Conf().maxLifetime * IN_MILLISECONDS)
                         << int32(-1) << uint8(0) << uint32(0);
                    pl->SendDirectMessage(&data);
                }
    }

    void StopTimers(char const* reason = "gone")
    {
        GoblinState* state = State();
        if (!state)
            return;
        for (auto const& [guid, p] : state->participants)
            if (Player* pl = ObjectAccessor::GetPlayer(*me, guid))
            {
                if (HasAddon(pl))
                    SendAddon(pl, Acore::StringFormat("END\t{}\t{}", me->GetGUID().GetCounter(), reason));
                if (!Conf().mirrorTimer)
                    continue;
                WorldPacket data(SMSG_STOP_MIRROR_TIMER, 4);
                data << uint32(MIRROR_FATIGUE);
                pl->SendDirectMessage(&data);
            }
    }

    // ---- update ---------------------------------------------------------------------------------
    void UpdateAI(uint32 /*diff*/) override
    {
        GoblinState* state = State();
        if (!state || phase == Phase::Escaping || phase == Phase::Gone)
            return;
        uint32 now = NowMs();
        if (!sized)
        {
            sized = true;
            RescaleHealth(1);   // the spawner may have set its level after InitializeAI
        }

        SendSpot();
        if (!state->awake)
        {
            if (now - state->spawnedMs >= Conf().unawareTime * IN_MILLISECONDS)
            {
                SendGone();
                phase = Phase::Gone;
                Active().erase(me->GetGUID());
                me->DespawnOrUnsummon();
            }
            return;
        }

        if (now >= nextTimerPacketMs)
        {
            uint32 step = lastPresenceMs ? std::min<uint32>(now - lastPresenceMs, 3000) : 1000;
            lastPresenceMs = now;
            nextTimerPacketMs = now + 1000;
            TrackPresence(*state, step);
            CheckCrowd(*state);
            SendTimers(*state, now);
            if (CheckAllDead(*state, now))
                return;
        }

        uint32 deadline = Deadline(*state);
        uint32 left = deadline > now ? deadline - now : 0;

        if (phase == Phase::PortalCast)
        {
            if (now >= castEndMs)
                Escape();
            return;
        }

        if (left == 0)
        {
            StartPortal(true);                                   // time's up: nothing stops this one
            return;
        }
        if (left <= Conf().portalWindow * IN_MILLISECONDS && now >= portalRetryMs)
        {
            StartPortal(false);                                  // tries to slip away; a hit breaks it
            return;
        }
        if (now >= nextMoveMs || !me->isMoving())
        {
            nextMoveMs = now + urand(1500, 2500);
            Flee();
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        phase = Phase::Gone;
        StopTimers("killed");
        SendGone();
        me->SummonCreature(Conf().deathEntry, *me, TEMPSUMMON_TIMED_DESPAWN, 4000);
        if (GoblinState* state = State())
        {
            DropLoot(me, *state);
            if (Conf().announce)
                Announce(Acore::StringFormat("A Loot Goblin <{}> was slain in {}!", Info(state->kind).subname, ZoneName(me)));
        }
        Active().erase(me->GetGUID());
        // the body carries no loot (the piles do), so it fades soon instead of lying there untargetable
        me->DespawnOrUnsummon(Milliseconds(Conf().corpseSeconds * IN_MILLISECONDS));
    }
};

// ================================================================================================
// Commands: .lootgoblin / .lg
// ================================================================================================
class loot_goblins_commandscript : public CommandScript
{
public:
    loot_goblins_commandscript() : CommandScript("loot_goblins_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable sub =
        {
            { "spawn",   HandleSpawn,   SEC_GAMEMASTER, Console::No },
            { "pack",    HandlePack,    SEC_GAMEMASTER, Console::No },
            { "list",    HandleList,    SEC_GAMEMASTER, Console::Yes },
            { "despawn", HandleDespawn, SEC_GAMEMASTER, Console::No },
            { "escape",  HandleEscape,  SEC_GAMEMASTER, Console::No },
            { "boss",    HandleBoss,    SEC_GAMEMASTER, Console::No },
            { "drop",    HandleDrop,    SEC_GAMEMASTER, Console::No },
            { "pools",   HandlePools,   SEC_GAMEMASTER, Console::Yes },
            { "info",    HandleInfo,    SEC_GAMEMASTER, Console::No },
            { "reload",  HandleReload,  SEC_ADMINISTRATOR, Console::Yes },
        };
        static ChatCommandTable root =
        {
            { "lootgoblin", sub },
            { "lg", sub },
        };
        return root;
    }

    static bool HandleSpawn(ChatHandler* handler, Optional<uint32> count, Optional<std::string> type, Optional<std::string> rankText)
    {
        Rank rank;
        Rank const* forcedRank = nullptr;
        if (rankText)
        {
            if (!ParseRank(*rankText, rank))
            {
                handler->SendSysMessage("Ranks: normal, elite, rare, rareelite");
                return false;
            }
            forcedRank = &rank;
        }
        Player* gm = handler->GetSession()->GetPlayer();
        uint32 n = std::clamp<uint32>(count.value_or(1), 1, Conf().commandMaxSpawn);
        Kind kind;
        Kind const* forced = nullptr;
        if (type && *type != "random")
        {
            if (!ParseKind(*type, kind))
            {
                std::string keys;
                for (uint8 i = 0; i < KIND_COUNT; ++i)
                    keys += std::string(i ? ", " : "") + Info(Kind(i)).key;
                handler->PSendSysMessage("Unknown type '{}'. Types: {}, random", *type, keys);
                return false;
            }
            forced = &kind;
        }
        uint32 made = SpawnPack(gm, n, forced, Conf().commandAnnounce, n > 1, forcedRank);
        handler->PSendSysMessage("Spawned {} Loot Goblin(s).", made);
        return made > 0;
    }

    static bool HandlePack(ChatHandler* handler, Optional<std::string> size)
    {
        bool large = size && *size == "large";
        uint32 n = large ? urand(4, 7) : urand(3, 4);
        uint32 made = SpawnPack(handler->GetSession()->GetPlayer(), n, nullptr, true, true);
        handler->PSendSysMessage("Spawned a {} pack of {}.", large ? "large" : "small", made);
        return true;
    }

    static bool HandleList(ChatHandler* handler)
    {
        uint32 now = NowMs();
        handler->PSendSysMessage("{} active Loot Goblin(s). {}", Active().size(), PoolSummary());
        for (auto const& [guid, s] : Active())
        {
            uint32 left = 0;
            if (s.awake)
            {
                uint32 deadline = std::min(s.deadlineMs, s.firstHitMs + Conf().maxLifetime * IN_MILLISECONDS);
                left = deadline > now ? (deadline - now) / 1000 : 0;
            }
            handler->PSendSysMessage("  {} map {} {}<{}> {} {}s left, {} participants", guid.ToString(), s.mapId,
                RankName(s.rank), Info(s.kind).subname, s.awake ? "running" : "unaware", left, s.participants.size());
        }
        return true;
    }

    static Creature* TargetGoblin(ChatHandler* handler)
    {
        Creature* c = handler->getSelectedCreature();
        if (!c || !IsGoblin(c))
        {
            handler->SendSysMessage("Target a Loot Goblin first.");
            return nullptr;
        }
        return c;
    }

    static bool HandleDespawn(ChatHandler* handler, Optional<std::string> what)
    {
        Player* gm = handler->GetSession()->GetPlayer();
        bool all = what && *what == "all";
        std::vector<ObjectGuid> remove;
        for (auto const& [guid, s] : Active())
            if (Creature* c = ObjectAccessor::GetCreature(*gm, guid))
                if (all || c->GetDistance(gm) < 100.0f)
                    remove.push_back(guid);
        for (ObjectGuid const& guid : remove)
            if (Creature* c = ObjectAccessor::GetCreature(*gm, guid))
            {
                Active().erase(guid);
                c->DespawnOrUnsummon();
            }
        handler->PSendSysMessage("Removed {} Loot Goblin(s) (no loot).", remove.size());
        return true;
    }

    static bool HandleEscape(ChatHandler* handler)
    {
        Creature* c = TargetGoblin(handler);
        if (!c)
            return false;
        if (auto* ai = dynamic_cast<npc_loot_goblin*>(c->AI()))
            ai->ForceEscape();
        return true;
    }

    // .lg boss [greed|hard] - test the world boss right here, with you as its only fighter
    static bool HandleBoss(ChatHandler* handler, Optional<std::string> which)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        uint8 tier = which && (*which == "hard" || *which == "2") ? 2 : 1;
        GoblinState fighters;
        fighters.participants[player->GetGUID()].level = uint8(player->GetLevel());
        Position at = player->GetFirstCollisionPosition(10.0f, 0.0f);
        if (!SummonBoss(player, at, tier, &fighters))
        {
            handler->SendSysMessage("Couldn't summon the boss (is LootGoblins.Boss.Enable on, and are its creatures in the database?)");
            return false;
        }
        return true;
    }

    // .lg drop <itemid> [count] [lift]: the item lands 4 yards in front of you as a goblin drop
    static bool HandleDrop(ChatHandler* handler, uint32 item, Optional<uint32> count, Optional<float> lift)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        if (!DropTestItem(player, item, count.value_or(1), lift.value_or(0.0f)))
        {
            handler->PSendSysMessage("No item {} in item_template.", item);
            return false;
        }
        ItemTemplate const* p = sObjectMgr->GetItemTemplate(item);
        handler->PSendSysMessage("Dropped {} (class {} subclass {}, inventory type {}, display {}).", p->Name1, p->Class, p->SubClass, p->InventoryType, p->DisplayInfoID);
        return true;
    }

    // .lg pools [level]: how much gear of each quality can drop at a level (default: yours)
    static bool HandlePools(ChatHandler* handler, Optional<uint32> level)
    {
        uint8 l = uint8(std::clamp<uint32>(level.value_or(handler->GetPlayer() ? handler->GetPlayer()->GetLevel() : 80), 1, 80));
        std::string report = PoolReport(l);
        std::size_t start = 0;
        while (start < report.size())
        {
            std::size_t bar = report.find(" | ", start);
            handler->PSendSysMessage("{}", report.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
            if (bar == std::string::npos)
                break;
            start = bar + 3;
        }
        return true;
    }

    static bool HandleInfo(ChatHandler* handler)
    {
        Creature* c = TargetGoblin(handler);
        GoblinState* s = c ? Find(c->GetGUID()) : nullptr;
        if (!s)
            return false;
        uint32 now = NowMs();
        uint32 deadline = std::min(s->deadlineMs, s->firstHitMs + Conf().maxLifetime * IN_MILLISECONDS);
        handler->PSendSysMessage("{}<{}> level {}, {}/{} health, {}, {}s left", RankName(s->rank), Info(s->kind).subname, c->GetLevel(),
            c->GetHealth(), c->GetMaxHealth(), s->awake ? "running" : "unaware", s->awake && deadline > now ? (deadline - now) / 1000 : 0);
        auto shares = Shares(*s, now);
        uint32 fight = s->awake && now > s->firstHitMs ? now - s->firstHitMs : 0;
        if (auto* ai = dynamic_cast<npc_loot_goblin*>(c->AI()))
            handler->PSendSysMessage("  mood: {}", ai->mood);
        for (auto const& [guid, p] : s->participants)
        {
            Player* pl = ObjectAccessor::FindPlayer(guid);
            handler->PSendSysMessage("  {} (L{}): share {:.1f}% (+{} on rolls) | dmg {:.0f} heal {:.0f} tank {:.0f} adds {:.0f} | there {:.0f}%",
                pl ? pl->GetName() : guid.ToString(), p.level, shares[guid], uint32(std::ceil(shares[guid] / Conf().rollBonusDivisor)),
                p.damage, p.healing, p.tanking, p.adds, fight ? float(p.presenceMs) * 100.0f / float(fight) : 0.0f);
        }
        return true;
    }

    static bool HandleReload(ChatHandler* handler)
    {
        sConfigMgr->Reload();   // read the .conf files again (LoadConfig alone only re-reads what's in memory)
        LoadConfig();
        BuildPools();
        handler->PSendSysMessage("Loot Goblins reloaded. {}", PoolSummary());
        return true;
    }
};

// ================================================================================================
// Hooks
// ================================================================================================
namespace
{
    Area Classify(Player* player)
    {
        Map* map = player->GetMap();
        if (map->IsBattleArena())
        {
            Battleground* bg = player->GetBattleground();
            return (bg && bg->isRated()) ? Area::ArenaRated : Area::ArenaSkirmish;
        }
        if (map->IsBattleground())
            return Area::Battleground;
        if (map->IsRaid())
            return Area::Raid;
        if (map->IsDungeon())
            return Area::Dungeon;
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(player->GetAreaId());
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(player->GetZoneId());
        uint32 flags = (area ? area->flags : 0) | (zone ? zone->flags : 0);
        if ((flags & AREA_FLAG_ARENA) || player->IsFFAPvP())
            return Area::FFA;
        if (flags & (AREA_FLAG_SANCTUARY | AREA_FLAG_CAPITAL))
            return Area::SafeZone;
        if (!player->IsOutdoors())
            return Area::Cave;
        return Area::OpenWorld;
    }

    std::unordered_map<uint32, uint32> lastSpawn;   // map id -> game time (s)
}

class loot_goblins_player : public PlayerScript
{
public:
    loot_goblins_player() : PlayerScript("loot_goblins_player") { }

    void OnPlayerUpdate(Player* player, uint32 /*diff*/) override { UpdateLootFor(player); }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (!Conf().enable || !killer || !killed || IsGoblin(killed) || killed->IsSummon())
            return;
        if (killed->GetEntry() >= Conf().typeEntryBase && killed->GetEntry() <= Conf().beamEntry)
            return;
        float chance = Conf().spawnChance[size_t(Classify(killer))];
        if (chance <= 0.0f || !roll_chance_f(chance))
            return;
        uint32 now = uint32(GameTime::GetGameTime().count());
        uint32& last = lastSpawn[killer->GetMapId()];
        if (last && now - last < Conf().spawnCooldown)
            return;
        last = now;

        uint32 count = 1;
        bool pack = false;
        if (roll_chance_f(Conf().packLargeChance))
            count = urand(4, 7), pack = true;
        else if (roll_chance_f(Conf().packSmallChance))
            count = urand(3, 4), pack = true;
        SpawnPack(killed, count, nullptr, true, pack);
    }

    // Our addon talks to us through addon whispers starting with "LGOB\t".
    void OnPlayerBeforeSendChatMessage(Player* player, uint32& /*type*/, uint32& lang, std::string& msg) override
    {
        if (player && lang == uint32(LANG_ADDON) && msg.rfind(ADDON_PREFIX, 0) == 0)
            HandleAddonMessage(player, msg);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (player)
            AddonForget(player->GetGUID());
    }
};

class loot_goblins_unit : public UnitScript
{
public:
    loot_goblins_unit() : UnitScript("loot_goblins_unit") { }

    // Credit for the fights a goblin causes: healing anyone in it, damage taken from the mobs it dragged in (tanking)
    // and damage done to them, for every awake goblin nearby that the player is part of.
    template <typename Fn>
    static void ForGoblinsNear(Player* player, Fn fn)
    {
        for (auto& [guid, state] : Active())
        {
            if (!state.awake || state.mapId != player->GetMapId())
                continue;
            Creature* goblin = player->GetMap()->GetCreature(guid);
            if (!goblin || goblin->GetDistance(player) > Conf().creditRange)
                continue;
            fn(state, goblin);
        }
    }

    void OnHeal(Unit* healer, Unit* receiver, uint32& gain) override
    {
        if (!Conf().contributionCountsHealing || !healer || !receiver || !gain || Active().empty())
            return;
        Player* h = healer->GetCharmerOrOwnerPlayerOrPlayerItself();
        Player* r = receiver->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!h || !r)
            return;
        // only what was actually missing counts (no credit for overhealing)
        uint32 effective = std::min<uint32>(gain, receiver->GetMaxHealth() - std::min(receiver->GetHealth(), receiver->GetMaxHealth()));
        if (!effective)
            return;
        ForGoblinsNear(h, [&](GoblinState& state, Creature* goblin)
        {
            if (!state.participants.count(r->GetGUID()))
                return;
            Participant& p = state.participants[h->GetGUID()];
            p.level = p.level ? std::min<uint8>(p.level, uint8(h->GetLevel())) : uint8(h->GetLevel());
            float v = float(effective) * LevelDamageRatio(uint8(goblin->GetLevel()), uint8(h->GetLevel()));
            p.healing += v;
            p.contribution += v * Conf().healWeight;
        });
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!attacker || !victim || !damage || Active().empty() || IsGoblin(victim) || (IsGoblin(attacker) && !IsBoss(attacker)))
            return;
        if (Player* tank = victim->GetCharmerOrOwnerPlayerOrPlayerItself())
        {
            if (attacker->GetTypeId() != TYPEID_UNIT || attacker->GetCharmerOrOwnerPlayerOrPlayerItself())
                return;
            ForGoblinsNear(tank, [&](GoblinState& state, Creature* goblin)
            {
                auto itr = state.participants.find(tank->GetGUID());
                if (itr == state.participants.end())
                    return;
                float v = float(damage) * LevelDamageRatio(uint8(goblin->GetLevel()), uint8(tank->GetLevel()));
                itr->second.tanking += v;
                itr->second.contribution += v * Conf().tankWeight;
            });
            return;
        }
        Player* hitter = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!hitter || victim->GetTypeId() != TYPEID_UNIT)
            return;
        ForGoblinsNear(hitter, [&](GoblinState& state, Creature* goblin)
        {
            auto itr = state.participants.find(hitter->GetGUID());
            if (itr == state.participants.end())
                return;
            float v = float(damage) * LevelDamageRatio(uint8(goblin->GetLevel()), uint8(hitter->GetLevel()));
            itr->second.adds += v;
            itr->second.contribution += v * Conf().addWeight;
        });
    }

    // Soft crowd control that does land on a goblin lasts only part as long.
    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        if (!aura || !IsGoblin(unit) || aura->IsPassive() || aura->GetSpellInfo()->IsPositive())
            return;
        int32 duration = aura->GetDuration();
        if (duration <= 0)
            return;
        int32 shorter = std::max<int32>(1, int32(float(duration) * Conf().ccDurationFactor));
        aura->SetMaxDuration(shorter);
        aura->SetDuration(shorter);
    }
};

// The client asks the server what a creature entry is called; for our drop slots the answer depends on who
// asks (each player's slot holds their own item), so we answer those ourselves.
class loot_goblins_server : public ServerScript
{
public:
    loot_goblins_server() : ServerScript("loot_goblins_server", { SERVERHOOK_CAN_PACKET_SEND }) { }

    bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override
    {
        if (!session || packet.GetOpcode() != SMSG_CREATURE_QUERY_RESPONSE || SendingPileName() || packet.size() < 4)
            return true;
        uint32 entry = packet.read<uint32>(0);
        Config const& c = Conf();
        if (entry < c.pileSlotFirst || entry >= c.pileSlotFirst + c.pileSlots)
            return !SendGoblinQueryWithPlate(session, packet);
        std::string name, sub;
        if (!PileNameFor(session, entry, name, sub))
            return true;
        SendPileNameTo(session, entry);
        return false;
    }
};

class loot_goblins_world : public WorldScript
{
public:
    loot_goblins_world() : WorldScript("loot_goblins_world") { }

    void OnAfterConfigLoad(bool /*reload*/) override { LoadConfig(); }
    void OnStartup() override
    {
        InitScaledItems();
        BuildPools();
    }
    void OnUpdate(uint32 diff) override { UpdateLoot(diff); }
};

void AddLootGoblinsScripts()
{
    RegisterCreatureAI(npc_loot_goblin);
    AddLootGoblinsBossScripts();
    new loot_goblins_commandscript();
    new loot_goblins_player();
    new loot_goblins_unit();
    new loot_goblins_world();
    new loot_goblins_server();
}
