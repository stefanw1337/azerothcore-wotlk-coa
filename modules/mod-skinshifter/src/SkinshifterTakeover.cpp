/*
 * mod-skinshifter - Takeover (level 20): go into a creature and play it.
 *
 * Two abilities with a name for each flavour:
 *   hostile  (Turnskin / Hostile Takeover): the creature keeps its monster faction. Players can attack it without
 *            being flagged and you can attack them (the module drops the creature's "player controlled" flag, so the
 *            game treats it as the monster it is).
 *   peaceful (Kindred Skin / Willing Host): the creature takes your faction and fights mobs with you.
 * Normal creatures are free; rares, elites and bosses cost tokens (Skinshifter.Tokens.Cost.*). Bosses only the
 * hostile way; dungeon bosses not yet (the "DM" idea). Hostile takeovers only in the open world, not in capitals,
 * sanctuaries or starting zones.
 *
 * While inside: your body is hidden (unseen, unselectable, unattackable) where it stood; the creature gets its own
 * abilities and a kit of Grimoire copies of creatures of its type and level (more the higher its rank) on the
 * possess bar, with Reset in the last button. Past the leash (Skinshifter.Takeover.Leash yards from the creature's
 * home) or with Reset out of combat, the creature runs home on its own, heals to full and you get it back. In a
 * hostile takeover each player who fought you in the last seconds adds health and melee damage (up to
 * Skinshifter.Takeover.Scaling.MaxAttackers), and each minute of fighting players (outside your group and guild)
 * earns a token, player kills one more, up to Skinshifter.Takeover.Reward.TokenCap per takeover.
 *
 * The end: the possess bar's cancel button or the time running out (Skinshifter.Takeover.Duration): you step out
 * where the creature stands, it dies and its kill and loot are yours. The creature dying throws you out at that
 * spot. Logging out, teleporting or the body coming to harm ends it with the creature going back to its own faction
 * and AI. The cooldown (Skinshifter.Takeover.Cooldown) starts when it ends. Kills made inside a creature and its own
 * death do not count for tokens.
 *
 * Messages to the SkinCollection addon (the token ring shows the timer):
 *   TAKEOVER;<seconds left, 0: no limit>;<seconds inside>;<seconds fighting players>;<tokens earned>;<1 hostile>;<name>
 *   TAKEOVEREND
 *
 * This code is released under the GNU AGPL v3 like AzerothCore.
 */

#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Group.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SkinshifterSpells.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Tokenize.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
using namespace SkinshifterSpells;

constexpr uint8 SkinshifterClassId = 33;
constexpr uint8 ResetButton = MAX_CREATURE_SPELLS - 1;  // the last button of the possess bar

struct TakeoverConfig
{
    bool enabled = true;
    float leash = 40.0f;
    float homeRing = 10.0f;
    uint32 resetTimeout = 15000;
    uint32 cooldown = 60000;
    uint32 duration = 120000;
    uint32 bossDuration = 60000;
    int32 maxLevelAbove = 3;
    float range = 30.0f;
    std::set<uint32> noHostileZones;
    bool hostileAsMonster = true;
    uint32 healthPerAttacker = 50;   // % more health for each attacker after the first
    uint32 damagePerAttacker = 10;   // % more melee damage for each attacker after the first
    uint32 maxAttackers = 5;
    uint32 attackerWindow = 15000;   // an attacker counts this long after the last hit
    uint32 tokenEvery = 60000;       // ms of fighting players for a token
    uint32 tokenCap = 5;
    bool killOnEnd = true;
    uint32 graceMs = 3000;        // after a takeover: seconds nothing can attack you
    uint32 hostCollapsePct = 25;  // a willing host this worn down dies as you leave; a healthier one goes home
    uint32 minStay = 60000;  // ms before you may step out (and kill the body)
    uint32 ccCooldown = 20000;
    uint32 shortCooldown = 8000;
};
TakeoverConfig Cfg;

enum class End
{
    StepOut,       // the cancel button: the creature dies, its kill and loot are yours
    TimeUp,        // the same, when the time runs out
    CreatureDied,  // killed: you are thrown out where it fell
    Lost,          // the creature is gone (despawned, another map)
    Released,      // logout, teleport, the body harmed: the creature goes back to its own AI
};

struct Takeover
{
    ObjectGuid creature;
    uint32 entry = 0;
    bool hostile = false;
    bool boss = false;
    std::array<uint32, MAX_CREATURE_SPELLS> oldSpells{};
    Position home;
    uint32 started = 0;
    uint32 duration = 0;
    uint32 baseMaxHealth = 0;
    uint32 bodyFlagsAdded = 0;
    bool playerFaction = false;  // hostile: you took the creature's faction
    Position bodyAt;             // where you stood as you went in
    std::map<ObjectGuid, std::pair<uint32, uint32>> summons;  // the creature's summons: entry, first seen
    bool resetting = false;
    uint32 resetStarted = 0;
    uint32 lastUpdate = 0;
    uint32 pvpMs = 0;
    uint32 tokenMs = 0;
    uint32 earned = 0;
    uint32 attackers = 1;
    std::map<ObjectGuid, uint32> hunters;  // player -> the last time they fought you
};

std::unordered_map<uint32, Takeover> Active;                  // player low guid
std::unordered_map<ObjectGuid, ObjectGuid> PlayerOfCreature;  // creature -> player
std::unordered_map<uint32, uint32> ReadyAt;                   // player low guid -> getMSTime() the cooldown ends
std::set<ObjectGuid> Dying;                                   // a creature being killed as you step out of it

bool IsHostileSpell(uint32 spellId)
{
    return spellId == Turnskin || spellId == HostileTakeover;
}

bool IsTakeoverSpell(uint32 spellId)
{
    return spellId == Turnskin || spellId == HostileTakeover || spellId == KindredSkin || spellId == WillingHost;
}

char const* TokenName(Player const* player)
{
    return IsPossessorRace(player->getRace()) ? "Soul Tokens" : "Skin Tokens";
}

bool Refuse(Player* player, std::string const& text)
{
    ChatHandler(player->GetSession()).SendSysMessage(text);
    return false;
}

void SendLine(Player* player, std::string const& line)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, "SKIN\t" + line);
    player->SendDirectMessage(&packet);
}

void LoadConfig()
{
    Cfg.enabled = sConfigMgr->GetOption<bool>("Skinshifter.Takeover.Enable", true);
    Cfg.leash = sConfigMgr->GetOption<float>("Skinshifter.Takeover.Leash", 40.0f);
    Cfg.homeRing = sConfigMgr->GetOption<float>("Skinshifter.Takeover.HomeRing", 10.0f);
    Cfg.cooldown = sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.Cooldown", 60) * IN_MILLISECONDS;
    Cfg.duration = sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.Duration", 120) * IN_MILLISECONDS;
    Cfg.bossDuration = sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.BossDuration", 60) * IN_MILLISECONDS;
    Cfg.maxLevelAbove = sConfigMgr->GetOption<int32>("Skinshifter.Takeover.MaxLevelAbovePlayer", 3);
    Cfg.range = sConfigMgr->GetOption<float>("Skinshifter.Takeover.Range", 30.0f);
    Cfg.hostileAsMonster = sConfigMgr->GetOption<bool>("Skinshifter.Takeover.HostileAsMonster", true);
    Cfg.healthPerAttacker = sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.Scaling.HealthPerAttacker", 50);
    Cfg.damagePerAttacker = sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.Scaling.DamagePerAttacker", 10);
    Cfg.maxAttackers = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.Scaling.MaxAttackers", 5));
    Cfg.attackerWindow = sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.Scaling.AttackerWindow", 15) * IN_MILLISECONDS;
    Cfg.tokenEvery = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.Reward.SecondsPerToken", 60)) *
        IN_MILLISECONDS;
    Cfg.tokenCap = sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.Reward.TokenCap", 5);
    Cfg.killOnEnd = sConfigMgr->GetOption<bool>("Skinshifter.Takeover.KillOnEnd", true);
    Cfg.hostCollapsePct = sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.HostCollapseBelow", 25);
    Cfg.graceMs = sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.GraceSeconds", 3) * IN_MILLISECONDS;
    Cfg.minStay = sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.MinimumStay", 60) * IN_MILLISECONDS;
    Cfg.ccCooldown = sConfigMgr->GetOption<uint32>("Skinshifter.Grimoire.CrowdControlCooldown", 20000);
    Cfg.shortCooldown = sConfigMgr->GetOption<uint32>("Skinshifter.Grimoire.ShortSpellCooldown", 8000);
    Cfg.noHostileZones.clear();
    // Elwynn, Dun Morogh, Durotar, Tirisfal, Teldrassil, Mulgore, Eversong, Azuremyst, Ebon Hold
    std::string const zones = sConfigMgr->GetOption<std::string>("Skinshifter.Takeover.NoHostileZones",
        "12,1,14,85,141,215,3430,3524,4298");
    for (std::string_view token : Acore::Tokenize(zones, ',', false))
        if (Optional<uint32> id = Acore::StringTo<uint32>(token))
            Cfg.noHostileZones.insert(*id);
}

// ---- The kit: Grimoire copies for the creature's bar ---------------------------------------------------------
enum Role : uint8 { RoleDamage, RoleArea, RoleControl, RoleHeal, RoleGuard, RoleBuff, RoleCount };

bool IsDamage(SpellEffectInfo const& effect)
{
    switch (effect.Effect)
    {
        case SPELL_EFFECT_SCHOOL_DAMAGE:
        case SPELL_EFFECT_WEAPON_DAMAGE:
        case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
        case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
        case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
        case SPELL_EFFECT_HEALTH_LEECH:
            return true;
        case SPELL_EFFECT_APPLY_AURA:
            return effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE || effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH;
        default:
            return false;
    }
}

// What a copy is good for on a creature's bar (RoleCount: nothing the kit wants).
Role RoleOf(SpellInfo const* info)
{
    if (!info || info->IsPassive() || (info->IsChanneled() && info->GetMaxDuration() > 10000))
        return RoleCount;
    bool damage = false, area = info->IsAffectingArea(), control = false, heal = false, guard = false, aura = false;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        damage = damage || IsDamage(effect);
        heal = heal || effect.Effect == SPELL_EFFECT_HEAL ||
            (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL);
        if (effect.Effect != SPELL_EFFECT_APPLY_AURA)
            continue;
        aura = true;
        switch (effect.ApplyAuraName)
        {
            case SPELL_AURA_MOD_STUN:
            case SPELL_AURA_MOD_FEAR:
            case SPELL_AURA_MOD_ROOT:
            case SPELL_AURA_MOD_CONFUSE:
                control = true;
                break;
            case SPELL_AURA_SCHOOL_ABSORB:
                guard = true;
                break;
            case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
                guard = guard || effect.BasePoints < 0;
                break;
            default:
                break;
        }
    }
    if (control && !info->IsPositive())
        return RoleControl;
    if (damage && !info->IsPositive())
        return area ? RoleArea : RoleDamage;
    if (heal && info->IsPositive())
        return RoleHeal;
    if (guard && info->IsPositive())
        return RoleGuard;
    if (aura && info->IsPositive() && info->Dispel == DISPEL_MAGIC)
        return RoleBuff;
    return RoleCount;
}

struct KitCandidate
{
    uint32 spell;
    uint8 level;
};
// creature type -> role -> copies
std::map<uint32, std::array<std::vector<KitCandidate>, RoleCount>> KitPools;
bool KitPoolsBuilt = false;

void BuildKitPools()
{
    if (KitPoolsBuilt)
        return;
    KitPoolsBuilt = true;
    uint32 count = 0;
    for (CopyInfo const& copy : AllCopies())
    {
        Role const role = RoleOf(sSpellMgr->GetSpellInfo(copy.copy));
        if (role == RoleCount)
            continue;
        KitPools[copy.type][role].push_back({ copy.copy, copy.level });
        KitPools[0][role].push_back({ copy.copy, copy.level });  // 0: any type, the fallback
        ++count;
    }
    LOG_INFO("module", "Skinshifter: takeover kit: {} Grimoire copies sorted into damage, area, control, heal, guard "
        "and magic buff.", count);
}

// The roles a creature of this rank gets, in order: two damage abilities for any, crowd control and a heal from a
// rare up, area damage and a damage reduction from an elite up, a magic buff (to purge or steal) for a boss.
std::vector<Role> KitRoles(uint32 rank, bool boss)
{
    std::vector<Role> roles = { RoleDamage, RoleDamage };
    if (rank != CREATURE_ELITE_NORMAL || boss)
    {
        roles.push_back(RoleControl);
        roles.push_back(RoleHeal);
    }
    if (rank == CREATURE_ELITE_ELITE || rank == CREATURE_ELITE_RAREELITE || rank == CREATURE_ELITE_WORLDBOSS || boss)
    {
        roles.insert(roles.begin() + 1, RoleArea);
        roles.push_back(RoleGuard);
    }
    if (rank == CREATURE_ELITE_WORLDBOSS || boss)
        roles.push_back(RoleBuff);
    return roles;
}

// A copy for the role from creatures of the type near the level (the same creature always gets the same kit).
uint32 PickKitSpell(uint32 type, Role role, uint32 level, uint32 entry, std::set<uint32> const& taken)
{
    // damage may come from any creature when its type has none; a heal, a shield or a buff only from its own kind
    // (a beast with Power Word: Shield is not a beast)
    bool const anyType = role == RoleDamage || role == RoleArea || role == RoleControl;
    for (uint32 pool : { type, 0u })
    {
        if (pool == 0 && !anyType)
            break;
        auto const pools = KitPools.find(pool);
        if (pools == KitPools.end())
            continue;
        std::vector<KitCandidate> const& candidates = pools->second[role];
        for (uint32 spread : { 5u, 15u, 100u })
        {
            std::vector<uint32> nearby;
            for (KitCandidate const& candidate : candidates)
                if (candidate.level + spread >= level && candidate.level <= level + spread / 2 + 2 &&
                    !taken.count(candidate.spell))
                    nearby.push_back(candidate.spell);
            if (!nearby.empty())
                return nearby[(entry * 2654435761u + uint32(role) * 40503u) % nearby.size()];
        }
    }
    return 0;
}

// The creature's bar while you are inside: its own abilities (up to 4 before the kit), the kit, the rest of its
// own if there is room, and Reset in the last button.
std::array<uint32, MAX_CREATURE_SPELLS> BarFor(Creature const* creature, bool boss)
{
    BuildKitPools();
    CreatureTemplate const* tmpl = creature->GetCreatureTemplate();
    std::vector<uint32> own;
    // its own abilities: those on its template and those its script casts (King Bangalash calls his tigers by script)
    std::vector<uint32> candidates(std::begin(creature->m_spells), std::end(creature->m_spells));
    for (uint32 spellId : CreatureScriptedSpells(creature->GetEntry()))
        candidates.push_back(spellId);
    for (uint32 spellId : candidates)
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId); info && !info->IsPassive() &&
            std::find(own.begin(), own.end(), spellId) == own.end())
            own.push_back(spellId);
    std::set<uint32> taken(own.begin(), own.end());
    std::vector<uint32> kit;
    for (Role role : KitRoles(tmpl->rank, boss))
        if (uint32 spellId = PickKitSpell(tmpl->type, role, creature->GetLevel(), creature->GetEntry(), taken))
        {
            kit.push_back(spellId);
            taken.insert(spellId);
        }
    std::vector<uint32> bar;
    std::size_t const firstOwn = std::min<std::size_t>(own.size(), 4);
    bar.insert(bar.end(), own.begin(), own.begin() + firstOwn);
    bar.insert(bar.end(), kit.begin(), kit.end());
    bar.insert(bar.end(), own.begin() + firstOwn, own.end());
    std::array<uint32, MAX_CREATURE_SPELLS> spells{};
    for (std::size_t index = 0; index < bar.size() && index < ResetButton; ++index)
        spells[index] = bar[index];
    spells[ResetButton] = TakeoverReset;
    return spells;
}

// ---- Rules ---------------------------------------------------------------------------------------------------
bool IsBoss(Creature const* creature)
{
    return creature->GetCreatureTemplate()->rank == CREATURE_ELITE_WORLDBOSS || creature->isWorldBoss() ||
        creature->IsDungeonBoss();
}

bool InNoHostileArea(Player* player)
{
    for (uint32 areaId : { player->GetZoneId(), player->GetAreaId() })
        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId))
            if (area->flags & (AREA_FLAG_CAPITAL | AREA_FLAG_SANCTUARY))
                return true;
    return Cfg.noHostileZones.count(player->GetZoneId()) || player->IsInSanctuary();
}

bool Ally(Player const* player, Player const* other)
{
    if (other == player)
        return true;
    if (Group const* group = player->GetGroup(); group && group->IsMember(other->GetGUID()))
        return true;
    return player->GetGuildId() && player->GetGuildId() == other->GetGuildId();
}

// Why this takeover cannot happen ("" when it can).
std::string Refusal(Player* player, Creature* creature, uint32 spellId)
{
    bool const hostile = IsHostileSpell(spellId);
    if (!Cfg.enabled)
        return "Takeovers are switched off on this realm.";
    if (Active.count(player->GetGUID().GetCounter()))
        return "You are inside a creature already.";
    auto const ready = ReadyAt.find(player->GetGUID().GetCounter());
    if (ready != ReadyAt.end() && int32(ready->second - getMSTime()) > 0)
        return Acore::StringFormat("You can take over again in {} sec.", (ready->second - getMSTime()) / 1000 + 1);
    if (player->GetLevel() < TakeoverLevel())
        return Acore::StringFormat("You can take over creatures from level {}.", TakeoverLevel());
    if (player->GetCharmGUID() || player->GetCharmerGUID() || player->GetVehicle() || player->IsInFlight())
        return "Not now.";
    if (!creature)
        return "Target a creature to take over.";
    CreatureTemplate const* tmpl = creature->GetCreatureTemplate();
    if (!creature->IsAlive())
        return "It is dead.";
    if (creature->IsPet() || creature->IsGuardian() || creature->IsSummon() || creature->IsTotem() ||
        creature->IsTrigger() || creature->GetOwnerGUID() || creature->GetCharmerGUID() || creature->IsVehicle() ||
        creature->GetVehicleKit() || creature->IsControlledByPlayer())
        return "That is not a creature you can take over.";
    if (tmpl->type == CREATURE_TYPE_CRITTER || tmpl->type == CREATURE_TYPE_TOTEM ||
        tmpl->type == CREATURE_TYPE_NON_COMBAT_PET || tmpl->type == CREATURE_TYPE_GAS_CLOUD)
        return "There is nothing in that worth taking over.";
    if (tmpl->RacialLeader)
        return "Faction leaders will not be taken over.";
    if (!player->IsValidAttackTarget(creature))
        return "You can only take over creatures you could fight.";
    if (int32(creature->GetLevel()) > int32(player->GetLevel()) + Cfg.maxLevelAbove)
        return "It is too strong for you to take over yet.";
    if (!player->IsWithinDistInMap(creature, Cfg.range))
        return "You must be closer.";
    bool const boss = IsBoss(creature);
    if (creature->IsDungeonBoss() || (boss && player->GetMap()->Instanceable()))
        return "Dungeon bosses cannot be taken over yet.";
    if (boss && !hostile)
        return "A boss will never fight on your side: take it over the hostile way.";
    if (hostile && (player->GetMap()->Instanceable() || InNoHostileArea(player)))
        return "Hostile takeovers are for the open world, not capitals, sanctuaries, starting zones or instances.";
    uint32 const cost = TokenCostOf(tmpl->rank, boss);
    if (cost && TokenCount(player) < cost)
        return Acore::StringFormat("Taking over {} costs {} {}; you have {}.", creature->GetName(), cost,
            TokenName(player), TokenCount(player));
    return "";
}

void SendTimer(Player* player, Takeover const& t, Creature const* creature)
{
    uint32 const inside = getMSTimeDiff(t.started, getMSTime()) / 1000;
    uint32 const left = t.duration ? (t.duration > inside * 1000 ? (t.duration - inside * 1000) / 1000 : 0) : 0;
    std::string name = creature ? creature->GetName() : "Takeover";
    std::replace(name.begin(), name.end(), ';', ',');
    SendLine(player, Acore::StringFormat("TAKEOVER;{};{};{};{};{};{}", left, inside, t.pvpMs / 1000, t.earned,
        t.hostile ? 1 : 0, name));
}

constexpr uint32 InvisibleBodyDisplay = 11686;  // "Invisible Stalker": a model that draws nothing

// Your client colours every creature by how it stands to your own faction, and only when that creature changes: after
// your faction changes, those around you are sent again, or they keep the old colour until you target them.
void RefreshReactions(Player* player)
{
    std::list<Unit*> units;
    Acore::AnyUnitInObjectRangeCheck check(player, player->GetVisibilityRange());
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(player, units, check);
    Cell::VisitObjects(player, searcher, player->GetVisibilityRange());
    for (Unit* unit : units)
        if (unit != player)
            unit->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);
}

void HideBody(Player* player, Takeover& t)
{
    uint32 const wanted = UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE;
    t.bodyFlagsAdded = wanted & ~player->GetUnitFlags();
    player->SetUnitFlag(UnitFlags(t.bodyFlagsAdded));
    player->SetVisible(false);  // gone for everyone else
    // and for you: your spirit is in the creature, no shell stays behind (an invisible model, as world triggers use)
    player->SetDisplayId(InvisibleBodyDisplay);
    RefreshBodyFx(player, true);  // the worn skin's glow would still hang where the body is
}

void ShowBody(Player* player, Takeover const& t)
{
    player->RemoveUnitFlag(UnitFlags(t.bodyFlagsAdded));
    player->SetVisible(true);
    if (player->GetDisplayId() == InvisibleBodyDisplay)
        RestoreOwnLook(player);
    RefreshBodyFx(player, false);
}

void SetScaling(Creature* creature, Takeover& t, uint32 attackers)
{
    attackers = std::clamp<uint32>(attackers, 1, Cfg.maxAttackers);
    if (attackers == t.attackers)
        return;
    t.attackers = attackers;
    float const healthPct = creature->GetHealthPct();
    uint32 const extra = attackers - 1;
    creature->SetMaxHealth(uint32(t.baseMaxHealth * (1.0f + extra * Cfg.healthPerAttacker / 100.0f)));
    creature->SetHealth(std::max<uint32>(1, uint32(creature->GetMaxHealth() * healthPct / 100.0f)));
    creature->SetStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, 1.0f + extra * Cfg.damagePerAttacker / 100.0f);
    creature->UpdateDamagePhysical(BASE_ATTACK);
}

void StartCooldown(Player* player)
{
    ReadyAt[player->GetGUID().GetCounter()] = getMSTime() + Cfg.cooldown;
    if (!Cfg.cooldown)
        return;
    for (uint32 spellId : { Turnskin, KindredSkin, HostileTakeover, WillingHost })
    {
        if (!player->HasSpell(spellId))
            continue;
        player->AddSpellCooldown(spellId, 0, Cfg.cooldown);
        WorldPacket data;
        player->BuildCooldownPacket(data, SPELL_COOLDOWN_FLAG_NONE, spellId, Cfg.cooldown);
        player->SendDirectMessage(&data);
    }
}

char const* EndText(End how, Player const* player)
{
    bool const possessor = IsPossessorRace(player->getRace());
    switch (how)
    {
        case End::StepOut: return possessor ? "You leave the body; it falls dead." : "You step out of the skin; it falls dead.";
        case End::TimeUp: return possessor ? "Your hold runs out: you leave the body and it falls dead." :
            "Your hold runs out: you step out and the skin falls dead.";
        case End::CreatureDied: return possessor ? "The body dies around you and casts you out." :
            "The creature dies around you and you tumble out.";
        case End::Lost: return "You lose your hold on the creature.";
        case End::Released: return "You let the creature go.";
    }
    return "";
}

// The fallen body's loot is yours (stepped out, time up, or killed while you were inside), unless a player who hunted
// it tapped it: the core left a steered creature without loot.
void LootFor(Player* player, Creature* creature)
{
    if (!creature || creature->IsAlive() || creature->IsLootRewardDisabled())
        return;
    if (creature->hasLootRecipient() && creature->GetLootRecipientGUID() != player->GetGUID() && !creature->loot.isLooted())
        return;
    creature->SetLootRecipient(player);
    creature->LowerPlayerDamageReq(creature->GetMaxHealth(), true, player->GetLevel());
    Loot& loot = creature->loot;
    bool const filled = loot.isLooted();
    if (filled)
    {
        loot.clear();
        CreatureTemplate const* info = creature->GetCreatureTemplate();
        if (info->lootid)
            loot.FillLoot(info->lootid, LootTemplates_Creature, player, false, false, creature->GetLootMode(), creature);
        if (creature->GetLootMode())
            loot.generateMoneyLoot(info->mingold, info->maxgold);
    }
    if (!loot.isLooted())
        creature->SetDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
    creature->ForceValuesUpdateAtIndex(UNIT_DYNAMIC_FLAGS);
    LOG_INFO("module", "Skinshifter: {}'s loot from {} ({}): {} items, {} copper{}.", player->GetName(),
        creature->GetName(), creature->GetEntry(), loot.items.size(), loot.gold, filled ? " (filled here)" : "");
}

void EndTakeover(Player* player, End how)
{
    auto const found = Active.find(player->GetGUID().GetCounter());
    if (found == Active.end())
        return;
    Takeover t = found->second;
    // gone from the lists first: stepping out teleports the body, and a teleport ends a takeover
    Active.erase(found);
    PlayerOfCreature.erase(t.creature);
    Creature* creature = ObjectAccessor::GetCreature(*player, t.creature);
    if (creature && creature->GetMapId() != player->GetMapId())
        creature = nullptr;

    if (creature)
    {
        // what it summoned while you were inside (Bangalash's panthers) goes with you: left behind they fought on,
        // and a second call doubled them
        std::list<Unit*> around;
        Acore::AnyUnitInObjectRangeCheck check(creature, 150.0f);
        Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(creature, around, check);
        Cell::VisitObjects(creature, searcher, 150.0f);
        for (Unit* unit : around)
            if (TempSummon* summon = unit->ToTempSummon(); summon && summon->GetSummonerGUID() == t.creature)
                summon->UnSummon();
        if (t.resetting)
            creature->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
        if (t.attackers != 1 && creature->IsAlive())
            SetScaling(creature, t, 1);
        creature->RemoveAurasDueToSpell(TakeoverControl, player->GetGUID());
        for (uint8 index = 0; index < MAX_CREATURE_SPELLS; ++index)
            creature->m_spells[index] = t.oldSpells[index];
    }
    player->RemoveAurasDueToSpell(TakeoverControl);
    if (player->GetCharmGUID() == t.creature)
        player->StopCastingCharm();
    ShowBody(player, t);
    if (t.playerFaction)
    {
        player->RestoreFaction();  // your race's faction again
        RefreshReactions(player);
    }

    // you come out (not when the takeover ends for a logout or a teleport of its own): from a hostile takeover where
    // the creature falls; from a willing host your spirit flies back to where you stood, away from the creature
    if (creature && how != End::Released)
    {
        if (!t.hostile && player->GetExactDist(&t.bodyAt) < 250.0f)
        {
            if (player->GetExactDist(&t.bodyAt) > 1.0f)
                player->NearTeleportTo(t.bodyAt.GetPositionX(), t.bodyAt.GetPositionY(), t.bodyAt.GetPositionZ(),
                    t.bodyAt.GetOrientation());
        }
        else if (player->GetDistance(creature) > 1.0f)
            player->NearTeleportTo(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(),
                creature->GetOrientation());
        // a moment to get your bearings: nothing can attack you for a few seconds
        if (Cfg.graceMs && !player->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
        {
            player->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            ObjectGuid const guid = player->GetGUID();
            player->m_Events.AddEventAtOffset([player, guid]()
            {
                if (player->GetGUID() == guid && !Active.count(guid.GetCounter()))
                    player->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            }, Milliseconds(Cfg.graceMs));
        }
    }

    // stepping out: the creature dies, with its kill (quests, achievements) and its loot for you; it was still a
    // takeover creature at this moment, so its death does not count for tokens
    // a willing host was a tool, not a kill: it wanders home dazed (no grudge, healed on the way), unless the fighting
    // wore it down so far that it collapses as you leave
    bool const goesHome = creature && creature->IsAlive() && !t.hostile &&
        creature->GetHealthPct() > float(Cfg.hostCollapsePct);
    if (goesHome)
    {
        creature->AttackStop();
        creature->CombatStop(true);
        creature->GetThreatMgr().ClearAllThreat();
        if (creature->IsAIEnabled && creature->AI())
            creature->AI()->EnterEvadeMode();
        else
            creature->GetMotionMaster()->MoveTargetedHome();
        ChatHandler(player->GetSession()).PSendSysMessage("{} shakes off your spirit and wanders home, dazed.",
            creature->GetName());
    }
    else if (creature && creature->IsAlive() && Cfg.killOnEnd && (how == End::StepOut || how == End::TimeUp))
    {
        creature->SetLootRecipient(player);
        creature->LowerPlayerDamageReq(creature->GetMaxHealth(), true, player->GetLevel());
        Dying.insert(t.creature);
        Unit::Kill(player, creature);
        Dying.erase(t.creature);
    }
    if (creature && how != End::Released)
        LootFor(player, creature);

    StartCooldown(player);
    SendLine(player, "TAKEOVEREND");
    ChatHandler handler(player->GetSession());
    handler.SendSysMessage(EndText(how, player));
    if (t.earned)
        handler.PSendSysMessage("The hunt earned you {} {}.", t.earned, TokenName(player));
    LOG_INFO("module", "Skinshifter: {}'s takeover of {} ends ({}); {} sec inside, {} sec against players, {} tokens.",
        player->GetName(), t.entry, uint32(how), getMSTimeDiff(t.started, getMSTime()) / 1000, t.pvpMs / 1000, t.earned);
}

bool StartTakeover(Player* player, Creature* creature, uint32 spellId)
{
    std::string const refusal = Refusal(player, creature, spellId);
    if (!refusal.empty())
        return Refuse(player, refusal);

    bool const hostile = IsHostileSpell(spellId);
    bool const boss = IsBoss(creature);
    CreatureTemplate const* tmpl = creature->GetCreatureTemplate();
    uint32 const cost = TokenCostOf(tmpl->rank, boss);

    // out of the saddle and the travel forms
    player->RemoveAurasByType(SPELL_AURA_MOUNTED);
    for (uint32 travel : { WraithRunning, PhaseRunning, RootWalking, SkinStride, PossessedSteed })
        player->RemoveAurasDueToSpell(travel);

    Takeover t;
    t.creature = creature->GetGUID();
    t.entry = creature->GetEntry();
    t.hostile = hostile;
    t.boss = boss;
    t.home = creature->GetHomePosition();
    t.duration = boss ? Cfg.bossDuration : Cfg.duration;
    for (uint8 index = 0; index < MAX_CREATURE_SPELLS; ++index)
        t.oldSpells[index] = creature->m_spells[index];

    // the bar is the creature's spells when the hold starts
    std::array<uint32, MAX_CREATURE_SPELLS> const bar = BarFor(creature, boss);
    for (uint8 index = 0; index < MAX_CREATURE_SPELLS; ++index)
        creature->m_spells[index] = bar[index];

    uint32 const faction = creature->GetFaction();
    // a charm left over from an earlier hold (its creature gone) blocks every new one
    if (player->GetCharmGUID())
    {
        if (Unit* old = player->GetCharm())
            old->RemoveCharmAuras();
        if (player->GetCharmGUID() && !player->GetCharm())
            player->SetGuidValue(UNIT_FIELD_CHARM, ObjectGuid::Empty);
    }
    SpellCastResult const result = player->CastSpell(creature, TakeoverControl, true);
    if (creature->GetCharmerGUID() != player->GetGUID())
    {
        for (uint8 index = 0; index < MAX_CREATURE_SPELLS; ++index)
            creature->m_spells[index] = t.oldSpells[index];
        player->RemoveAurasDueToSpell(TakeoverControl);
        std::string why;
        switch (result)
        {
            case SPELL_FAILED_ALREADY_HAVE_CHARM: why = "you already control another creature"; break;
            case SPELL_FAILED_ALREADY_HAVE_SUMMON: why = "dismiss your pet first"; break;
            case SPELL_FAILED_CHARMED: why = creature->GetCharmerGUID() ? "something else controls it" : "you are controlled yourself"; break;
            case SPELL_FAILED_CANT_BE_CHARMED: why = "it cannot be controlled while mounted or as someone's pet"; break;
            case SPELL_FAILED_TARGET_IS_PLAYER_CONTROLLED: why = "it belongs to a player"; break;
            case SPELL_FAILED_BAD_IMPLICIT_TARGETS: why = "it is a vehicle"; break;
            case SPELL_FAILED_HIGHLEVEL: why = "it is too high level"; break;
            case SPELL_FAILED_OUT_OF_RANGE: why = "it is out of range"; break;
            case SPELL_FAILED_LINE_OF_SIGHT: why = "you cannot see it"; break;
            case SPELL_FAILED_NOT_READY: why = "the hold is not ready"; break;
            default: why = "the hold did not take (cast result " + std::to_string(uint32(result)) + ")"; break;
        }
        LOG_INFO("module", "Skinshifter: {}'s takeover of {} ({}) failed: {} (cast result {}).", player->GetName(),
            creature->GetName(), creature->GetEntry(), why, uint32(result));
        return Refuse(player, "The creature resists your hold: " + why + ".");
    }
    if (cost)
        SpendTokens(player, cost);

    // hostile: it stays the monster it is (its own faction, not "player controlled": anyone may fight it)
    if (hostile)
    {
        creature->SetFaction(faction);
        if (Cfg.hostileAsMonster)
            creature->RemoveUnitFlag(UNIT_FLAG_PLAYER_CONTROLLED);
        // you are a monster now: its kin are your kin (green), its enemies and the players' sides are yours
        player->SetFaction(faction);
        t.playerFaction = true;
    }
    t.baseMaxHealth = creature->GetMaxHealth();
    t.started = getMSTime();
    t.lastUpdate = t.started;
    t.bodyAt = player->GetPosition();
    HideBody(player, t);
    Active[player->GetGUID().GetCounter()] = t;
    if (t.playerFaction)
        RefreshReactions(player);
    PlayerOfCreature[t.creature] = player->GetGUID();

    ChatHandler handler(player->GetSession());
    std::string kit;
    for (uint8 index = 0; index < ResetButton; ++index)
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(bar[index]))
            kit += (kit.empty() ? "" : ", ") + std::string(info->SpellName[0]);
    handler.PSendSysMessage("You are inside {} ({}{}). Its bar: {}. Cancel on the possess bar to step out{}.",
        creature->GetName(), hostile ? "hostile: players can hunt you" : "on your side",
        cost ? Acore::StringFormat(", {} {}", cost, TokenName(player)) : "", kit.empty() ? "its attacks" : kit,
        t.duration ? Acore::StringFormat("; your hold lasts {} sec", t.duration / 1000) : "");
    SendTimer(player, t, creature);
    LOG_INFO("module", "Skinshifter: {} takes over {} ({}, rank {}, {}), cost {}; bar: {}.", player->GetName(),
        creature->GetName(), creature->GetEntry(), tmpl->rank, hostile ? "hostile" : "peaceful", cost, kit);
    return true;
}

void StartReset(Player* player, Takeover& t, Creature* creature, char const* why)
{
    t.resetting = true;
    t.resetStarted = getMSTime();
    player->SetClientControl(creature, false, true);  // the packet only: your view stays with the creature
    creature->InterruptNonMeleeSpells(false);
    creature->AttackStop();
    creature->CombatStop(true);
    creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
    creature->GetMotionMaster()->Clear(false);
    creature->GetMotionMaster()->MovePoint(0, t.home);
    ChatHandler(player->GetSession()).SendSysMessage(why);
}

void FinishReset(Player* player, Takeover& t, Creature* creature)
{
    t.resetting = false;
    creature->GetMotionMaster()->Clear(false);
    creature->GetMotionMaster()->MoveIdle();
    creature->StopMoving();
    if (creature->GetDistance(t.home) > Cfg.homeRing)
        creature->NearTeleportTo(t.home.GetPositionX(), t.home.GetPositionY(), t.home.GetPositionZ(), t.home.GetOrientation());
    SetScaling(creature, t, 1);
    t.hunters.clear();
    creature->SetFullHealth();
    creature->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
    player->SetClientControl(creature, true, true);
    ChatHandler(player->GetSession()).SendSysMessage("You are home and whole again; the creature is yours.");
}

void UpdateTakeover(Player* player)
{
    auto found = Active.find(player->GetGUID().GetCounter());
    if (found == Active.end())
        return;
    Takeover& t = found->second;
    uint32 const now = getMSTime();
    uint32 const diff = getMSTimeDiff(t.lastUpdate, now);
    if (diff < 500)
        return;
    t.lastUpdate = now;

    Creature* creature = ObjectAccessor::GetCreature(*player, t.creature);
    if (!creature || creature->GetMapId() != player->GetMapId() || !creature->IsInWorld())
        return EndTakeover(player, End::Lost);
    if (!creature->IsAlive())
        return EndTakeover(player, End::CreatureDied);
    if (creature->GetCharmerGUID() != player->GetGUID())
        return EndTakeover(player, End::Lost);
    if (t.duration && getMSTimeDiff(t.started, now) >= t.duration)
        return EndTakeover(player, End::TimeUp);

    if (t.resetting)
    {
        if (creature->GetDistance(t.home) <= Cfg.homeRing || getMSTimeDiff(t.resetStarted, now) >= Cfg.resetTimeout)
            FinishReset(player, t, creature);
    }
    else if (creature->GetDistance(t.home) > Cfg.leash)
        StartReset(player, t, creature, "Too far from home: the creature turns back on its own.");

    // a new call replaces the old: calling Bangalash's panthers again sends the first ones away instead of doubling them
    {
        std::list<Unit*> around;
        Acore::AnyUnitInObjectRangeCheck check(creature, 150.0f);
        Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(creature, around, check);
        Cell::VisitObjects(creature, searcher, 150.0f);
        std::map<uint32, uint32> newest;
        std::vector<TempSummon*> mine;
        for (Unit* unit : around)
            if (TempSummon* summon = unit->ToTempSummon(); summon && summon->GetSummonerGUID() == t.creature &&
                summon->IsAlive())
            {
                auto const seen = t.summons.try_emplace(summon->GetGUID(), summon->GetEntry(), now).first->second;
                newest[seen.first] = std::max(newest[seen.first], seen.second);
                mine.push_back(summon);
            }
        for (TempSummon* summon : mine)
        {
            auto const& seen = t.summons[summon->GetGUID()];
            if (seen.second + 1500 < newest[seen.first])
            {
                t.summons.erase(summon->GetGUID());
                summon->UnSummon();
            }
        }
    }

    // the players fighting you (outside your group and guild), for the scaling and the tokens
    for (auto hunter = t.hunters.begin(); hunter != t.hunters.end();)
    {
        Player* other = ObjectAccessor::FindPlayer(hunter->first);
        if (!other || getMSTimeDiff(hunter->second, now) > Cfg.attackerWindow || !other->IsInMap(creature))
            hunter = t.hunters.erase(hunter);
        else
            ++hunter;
    }
    if (t.hostile && !t.resetting)
    {
        SetScaling(creature, t, uint32(t.hunters.size()));
        if (!t.hunters.empty() && creature->IsInCombat())
        {
            t.pvpMs += diff;
            t.tokenMs += diff;
            if (t.tokenMs >= Cfg.tokenEvery && t.earned < Cfg.tokenCap)
            {
                t.tokenMs -= Cfg.tokenEvery;
                ++t.earned;
                EarnToken(player, creature);
            }
        }
    }
    SendTimer(player, t, creature);
}

// A fight between the creature you are in and a player who is not with you.
void NoteHunter(Unit* creatureUnit, Unit* other)
{
    if (!creatureUnit || !other)
        return;
    Creature* creature = creatureUnit->ToCreature();
    if (!creature)
        return;
    auto const owner = PlayerOfCreature.find(creature->GetGUID());
    if (owner == PlayerOfCreature.end())
        return;
    Player* hunter = other->GetCharmerOrOwnerPlayerOrPlayerItself();
    Player* player = ObjectAccessor::FindPlayer(owner->second);
    if (!hunter || !player || Ally(player, hunter))
        return;
    auto found = Active.find(player->GetGUID().GetCounter());
    if (found != Active.end())
        found->second.hunters[hunter->GetGUID()] = getMSTime();
}

Takeover* TakeoverOfCreature(Unit const* unit, Player** player = nullptr)
{
    if (!unit || !unit->IsCreature())
        return nullptr;
    auto const owner = PlayerOfCreature.find(unit->GetGUID());
    if (owner == PlayerOfCreature.end())
        return nullptr;
    Player* found = ObjectAccessor::FindPlayer(owner->second);
    if (!found)
        return nullptr;
    auto const active = Active.find(found->GetGUID().GetCounter());
    if (active == Active.end())
        return nullptr;
    if (player)
        *player = found;
    return &active->second;
}

class SkinshifterTakeoverPlayerScript : public PlayerScript
{
public:
    SkinshifterTakeoverPlayerScript() : PlayerScript("SkinshifterTakeoverPlayerScript", { PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_BEFORE_TELEPORT, PLAYERHOOK_ON_PLAYER_JUST_DIED, PLAYERHOOK_ON_PVP_KILL }) { }

    void OnPlayerUpdate(Player* player, uint32 /*diff*/) override
    {
        if (!Active.empty())
            UpdateTakeover(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        EndTakeover(player, End::Released);
        ReadyAt.erase(player->GetGUID().GetCounter());
    }

    bool OnPlayerBeforeTeleport(Player* player, uint32 /*mapid*/, float /*x*/, float /*y*/, float /*z*/,
        float /*orientation*/, uint32 /*options*/, Unit* /*target*/) override
    {
        if (Active.count(player->GetGUID().GetCounter()))
            EndTakeover(player, End::Released);
        return true;
    }

    void OnPlayerJustDied(Player* player) override
    {
        EndTakeover(player, End::Released);
    }

    // a player killed by the creature you are in: one more token (within the cap per takeover)
    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        auto found = killer ? Active.find(killer->GetGUID().GetCounter()) : Active.end();
        if (found == Active.end() || !found->second.hostile || Ally(killer, killed))
            return;
        Takeover& t = found->second;
        if (t.earned >= Cfg.tokenCap)
            return;
        ++t.earned;
        EarnToken(killer, ObjectAccessor::GetCreature(*killer, t.creature));
        ChatHandler(killer->GetSession()).PSendSysMessage("You kill {}: one more {}.", killed->GetName(),
            IsPossessorRace(killer->getRace()) ? "Soul Token" : "Skin Token");
    }
};

class SkinshifterTakeoverUnitScript : public UnitScript
{
public:
    SkinshifterTakeoverUnitScript() : UnitScript("SkinshifterTakeoverUnitScript", true, { UNITHOOK_ON_DAMAGE }) { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& /*damage*/) override
    {
        if (PlayerOfCreature.empty() || !attacker || !victim)
            return;
        NoteHunter(victim, attacker);
        NoteHunter(attacker, victim);
    }
};

// The creature's own casts while you steer it: Reset, nothing while it runs home, and a cooldown on abilities that
// have none (a creature's AI times them; a player would not).
class SkinshifterTakeoverSpellScript : public AllSpellScript
{
public:
    SkinshifterTakeoverSpellScript() : AllSpellScript("SkinshifterTakeoverSpellScript", { ALLSPELLHOOK_CAN_PREPARE,
        ALLSPELLHOOK_ON_CAST }) { }

    bool CanPrepare(Spell* spell, SpellCastTargets const* /*targets*/, AuraEffect const* triggeredByAura) override
    {
        if (PlayerOfCreature.empty() || triggeredByAura || spell->IsTriggered())
            return true;
        Player* player = nullptr;
        Takeover* t = TakeoverOfCreature(spell->GetCaster(), &player);
        if (!t)
            return true;
        Creature* creature = spell->GetCaster()->ToCreature();
        if (t->resetting)
            return false;
        if (spell->GetSpellInfo()->Id != TakeoverReset)
            return true;
        if (creature->IsInCombat())
            Refuse(player, "Reset works out of combat.");
        else
            StartReset(player, *t, creature, "The creature runs home to heal.");
        return false;
    }

    void OnSpellCast(Spell* spell, Unit* caster, SpellInfo const* info, bool /*skipCheck*/) override
    {
        if (PlayerOfCreature.empty() || spell->IsTriggered() || info->Id == TakeoverReset)
            return;
        Player* player = nullptr;
        if (!TakeoverOfCreature(caster, &player) || info->RecoveryTime || info->CategoryRecoveryTime)
            return;
        bool control = false;
        for (SpellEffectInfo const& effect : info->Effects)
            control = control || (effect.Effect == SPELL_EFFECT_APPLY_AURA &&
                (effect.ApplyAuraName == SPELL_AURA_MOD_STUN || effect.ApplyAuraName == SPELL_AURA_MOD_FEAR ||
                 effect.ApplyAuraName == SPELL_AURA_MOD_ROOT || effect.ApplyAuraName == SPELL_AURA_MOD_CONFUSE ||
                 effect.ApplyAuraName == SPELL_AURA_MOD_SILENCE || effect.ApplyAuraName == SPELL_AURA_MOD_PACIFY_SILENCE));
        uint32 const cooldown = control ? Cfg.ccCooldown : info->CalcCastTime() <= 1500 ? Cfg.shortCooldown : 0;
        if (!cooldown)
            return;
        Creature* creature = caster->ToCreature();
        creature->AddSpellCooldown(info->Id, 0, cooldown);
        // sent after the cast's own cooldown packet, which would otherwise replace it on the bar with a short one
        ObjectGuid const playerGuid = player->GetGUID(), creatureGuid = creature->GetGUID();
        uint32 const spellId = info->Id, sentAt = getMSTime();
        player->m_Events.AddEventAtOffset([player, playerGuid, creatureGuid, spellId, cooldown, sentAt]()
        {
            Creature* steered = player->GetGUID() == playerGuid ? ObjectAccessor::GetCreature(*player, creatureGuid) : nullptr;
            uint32 const passed = getMSTimeDiff(sentAt, getMSTime());
            if (!steered || passed >= cooldown)
                return;
            WorldPacket data;
            steered->BuildCooldownPacket(data, SPELL_COOLDOWN_FLAG_NONE, spellId, cooldown - passed);
            player->SendDirectMessage(&data);
        }, 200ms);
    }
};

class SkinshifterTakeoverWorldScript : public WorldScript
{
public:
    SkinshifterTakeoverWorldScript() : WorldScript("SkinshifterTakeoverWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }

    void OnStartup() override
    {
        LOG_INFO("module", "Skinshifter: takeovers {} from level {}: leash {} yd, {} sec ({} for bosses), cooldown {} "
            "sec.", Cfg.enabled ? "on" : "off", TakeoverLevel(), Cfg.leash, Cfg.duration / 1000, Cfg.bossDuration / 1000,
            Cfg.cooldown / 1000);
    }
};
}

namespace SkinshifterSpells
{
bool InTakeover(Player const* player)
{
    return player && Active.count(player->GetGUID().GetCounter());
}

bool IsTakeoverCreature(Creature const* creature)
{
    return creature && (PlayerOfCreature.count(creature->GetGUID()) || Dying.count(creature->GetGUID()));
}

bool TakeoverCanStart(Player* player, Unit* target, uint32 spellId)
{
    if (!IsTakeoverSpell(spellId))
        return true;
    std::string const refusal = Refusal(player, target ? target->ToCreature() : nullptr, spellId);
    return refusal.empty() || Refuse(player, refusal);
}

bool HandleTakeoverSpell(Player* player, Unit* target, uint32 spellId)
{
    if (!IsTakeoverSpell(spellId))
        return false;
    StartTakeover(player, target ? target->ToCreature() : nullptr, spellId);
    return true;
}

bool HandleTakeoverCancel(Player* player, uint32 spellId)
{
    auto const active = Active.find(player->GetGUID().GetCounter());
    if (spellId != TakeoverControl || active == Active.end())
        return false;
    // no instant kill: stepping out (which kills the body, its loot yours) waits until you have been the creature a
    // while (never longer than the takeover lasts); the request is refused and the takeover goes on
    uint32 const inside = getMSTimeDiff(active->second.started, getMSTime());
    uint32 const minimum = std::min(Cfg.minStay, active->second.duration);
    if (inside < minimum)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("You cannot leave the creature yet: {} more seconds.",
            (minimum - inside + 999) / 1000);
        return true;
    }
    ObjectGuid const guid = player->GetGUID();
    // a moment later, in the player's own update (not inside the packet handling)
    player->m_Events.AddEventAtOffset([player, guid]()
    {
        if (player->GetGUID() == guid)
            EndTakeover(player, End::StepOut);
    }, 1ms);
    return true;
}
}

void AddSC_skinshifter_takeover()
{
    new SkinshifterTakeoverPlayerScript();
    new SkinshifterTakeoverUnitScript();
    new SkinshifterTakeoverSpellScript();
    new SkinshifterTakeoverWorldScript();
}
