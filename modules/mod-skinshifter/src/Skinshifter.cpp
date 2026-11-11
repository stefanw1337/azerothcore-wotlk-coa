/*
 * mod-skinshifter - prototype of the Skinshifter class mechanics.
 *
 * Lift: take a creature's abilities into the Grimoire (learned for good) and its exact model into the
 * Skin Collection. Wear: become any collected skin at will; Shed: return to your own look.
 *
 * This first version is driven by chat commands (.skin ...) so the mechanics can be tried on any character
 * before the class itself exists. It is separate from mod-subclass and every other module.
 *
 * This code is released under the GNU AGPL v3 like AzerothCore.
 */

#include "CharacterCache.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Timer.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SmartScriptMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "SkinshifterSpells.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include "Vehicle.h"
#include "WorldPacket.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <tuple>
#include <string>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
struct SkinshifterConfig
{
    bool enabled = true;
    float liftRange = 10.0f;
    bool allowFriendly = true;
    bool allowBosses = false;
    bool allowElites = true;
    int32 maxLevelAbove = 3;
    bool wearInCombat = false;
    bool stealWears = true;   // Steal Form puts the skin on at once
    bool stealName = true;    // others see the creature's name above you while you wear its skin (chat keeps yours)
    bool stealHeld = true;    // and what it holds (weapons, a torch)
    bool steedPassenger = true;  // Steed Shift carries one party member on its saddle
    float minScale = 0.1f;
    float maxScale = 5.0f;
    float smallestRadius = 0.3f;
    float largestRadius = 2.0f;
    uint32 shortSpellCooldown = 8000;
    uint32 crowdControlCooldown = 20000;
    uint32 starterMaxLevel = 10;
    std::set<uint8> energyRaces;
    uint32 fxDelayMin = 250;
    uint32 swapDelay = 500;
    uint32 rootWalkDisplay = 16892;
    uint32 wraithDisplay = 66620;  // Wraith Glide's look (a floating wraith)
    float wraithLift = 0.5f;       // and how high it hovers
    uint8 wraithAnimTier = 2;      // its animations: 0 ground (runs), 2 hover, 3 fly
    float rootWalkScale = 0.2f;
    uint32 burrowDisplay = 400828;
    float burrowScale = 1.0f;
    uint32 surfaceDelay = 500;
    float nodeRange = 100.0f;
    bool hideFromCreatures = true;
    std::set<uint32> blockedAreas;
};

SkinshifterConfig Cfg;

struct WornSkin
{
    uint32 displayId;
    float scale;
    uint32 entry = 0;  // the creature it came from (its name and what it holds: the disguise), 0: not known
};

std::unordered_map<uint32, std::vector<uint32>> ScriptedSpells;
std::unordered_map<uint32, WornSkin> Worn;
// Players shown in a mount's shape (Steed Shift and the Possessor runs) or as the Root Walk worm instead of their
// skin or their own look; it comes back when that ends.
std::unordered_set<uint32> TemporaryLook;
std::unordered_map<uint32, std::unordered_set<uint32>> Grimoires;
std::unordered_set<uint32> CollectionKnown;  // the addon holds the whole skin list (sent since its last load)
std::unordered_set<uint32> Reapplying;

// ---- Effects -----------------------------------------------------------------------------------------------
// Three styles: Skinshifter (everyone), Ghost (Possessor races) and Energy (Possessor races in Fx.EnergyRaces).
// Each style has numbered options to try in game with `.skin fx <n>`; the choice is saved per character.
//   worn    an aura on you while you wear a skin (it follows whatever model you show)
//   travel  a missile from you to the target on Possess; the body swaps when it lands
//   pulse   shown on you for a moment when you put on or take off a skin
enum class FxStyle : uint8 { Skinshifter, Ghost, Energy };

struct FxOption
{
    char const* name;
    uint32 worn;
    uint32 travel;
    uint32 pulse;
};

using namespace SkinshifterSpells;

std::vector<FxOption> const SkinshifterFx =
{
    { "plain: no effect", 0, 0, 0 },
    { "purple flicker on every change", 0, 0, FxPurpleGhost },
    { "soul fragment on Steal Form, spirit pulse on change", 0, FxSoulFragment, FxSpiritPulse },
};

// The first is the default: the possessed body fully seen, in a blue glow (the ghost inside it); the others show the
// body see-through.
std::vector<FxOption> const GhostFx =
{
    { "blue glow (the body fully seen), soul transfer, spirit pulse", FxBlueGlow, FxSoulTransfer, FxSpiritPulse },
    { "ghost visual, soul transfer, spirit pulse", FxGhostVisual, FxSoulTransfer, FxSpiritPulse },
    { "blue fog, drawn soul, spirit pulse", FxGhostBlueFog, FxDrawnSoul, FxSpiritPulse },
    { "transparent blue fog, soul transfer", FxGhostFogSheer, FxSoulTransfer, FxSpiritPulse },
    { "50% see-through, soul reaped", FxGhostHalfSeen, FxSoulReaped, FxSpiritPulse },
    { "ghost visual effect, drawn soul", FxGhostCosmetic, FxDrawnSoul, FxGhostVisual },
};

std::vector<FxOption> const EnergyFx =
{
    { "ethereal visual, ethereal bolt, ethereal burst", FxEtherealVisual, FxEtherealBolt, FxEtherealRingStart },
    { "ethereal beacon, energy ball, ethereal burst", FxEtherealBeacon, FxEnergyBall, FxEtherealRingStart },
    { "purple ghost, strange energy, ethereal burst", FxPurpleGhost, FxStrangeEnergy, FxEtherealRingStart },
    { "overwhelming energy, energy ball", FxOverwhelmEnergy, FxEnergyBall, FxEtherealVisual },
};

std::unordered_map<uint32, uint8> FxChoice;

FxStyle StyleOf(Player const* player)
{
    if (!IsPossessorRace(player->getRace()))
        return FxStyle::Skinshifter;
    return Cfg.energyRaces.count(player->getRace()) ? FxStyle::Energy : FxStyle::Ghost;
}

char const* StyleName(FxStyle style)
{
    switch (style)
    {
        case FxStyle::Ghost: return "Possessor (ghost)";
        case FxStyle::Energy: return "Possessor (energy)";
        default: return "Skinshifter";
    }
}

std::vector<FxOption> const& OptionsOf(FxStyle style)
{
    switch (style)
    {
        case FxStyle::Ghost: return GhostFx;
        case FxStyle::Energy: return EnergyFx;
        default: return SkinshifterFx;
    }
}

FxOption const& FxOf(Player const* player)
{
    std::vector<FxOption> const& options = OptionsOf(StyleOf(player));
    auto const choice = FxChoice.find(player->GetGUID().GetCounter());
    uint8 const index = choice == FxChoice.end() ? 0 : choice->second;
    return options[index < options.size() ? index : 0];
}

bool HasSpellData(uint32 spellId)
{
    return spellId && sSpellMgr->GetSpellInfo(spellId);
}

// Takes every worn effect of every style off, then puts the chosen one on if a skin is worn.
void RefreshWornFx(Player* player, bool wearing)
{
    for (FxStyle style : { FxStyle::Skinshifter, FxStyle::Ghost, FxStyle::Energy })
        for (FxOption const& option : OptionsOf(style))
            if (option.worn)
                player->RemoveAurasDueToSpell(option.worn);
    uint32 const worn = FxOf(player).worn;
    if (wearing && HasSpellData(worn))
        player->CastSpell(player, worn, true);
}

void Pulse(Player* player)
{
    uint32 const pulse = FxOf(player).pulse;
    if (!HasSpellData(pulse) || player->HasAura(pulse))
        return;
    player->CastSpell(player, pulse, true);
    player->m_Events.AddEventAtOffset([player, pulse]() { player->RemoveAurasDueToSpell(pulse); }, 1500ms);
}

char const* TypeName(uint32 type)
{
    switch (type)
    {
        case CREATURE_TYPE_BEAST:         return "Beast";
        case CREATURE_TYPE_DRAGONKIN:     return "Dragonkin";
        case CREATURE_TYPE_DEMON:         return "Demon";
        case CREATURE_TYPE_ELEMENTAL:     return "Elemental";
        case CREATURE_TYPE_GIANT:         return "Giant";
        case CREATURE_TYPE_UNDEAD:        return "Undead";
        case CREATURE_TYPE_HUMANOID:      return "Humanoid";
        case CREATURE_TYPE_MECHANICAL:    return "Mechanical";
        case CREATURE_TYPE_NOT_SPECIFIED: return "Other";
        default:                          return "Unknown";
    }
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

bool IsLiftable(SpellInfo const* spellInfo)
{
    if (!spellInfo || spellInfo->IsPassive() || spellInfo->HasAttribute(SPELL_ATTR0_DO_NOT_DISPLAY))
        return false;

    std::string const name = spellInfo->SpellName[0] ? spellInfo->SpellName[0] : "";
    if (name.empty() || SkinshifterSpells::NameIsJunk(name))
        return false;

    bool anyUseful = false;
    for (SpellEffectInfo const& effect : spellInfo->GetEffects())
    {
        if (!effect.Effect)
            continue;
        if (SkinshifterSpells::IsForbiddenEffect(effect.Effect) || SkinshifterSpells::IsForbiddenAura(effect.ApplyAuraName))
            return false;
        if (effect.Effect != SPELL_EFFECT_DUMMY && effect.Effect != SPELL_EFFECT_SCRIPT_EFFECT &&
            effect.ApplyAuraName != SPELL_AURA_DUMMY)
            anyUseful = true;
    }
    return anyUseful;
}

// Low creatures without abilities of their own still teach one: a real low-level creature ability of their kind.
// Creatures of the same kind teach different ones (chosen by entry), so it pays to steal from several. The first row
// that matches the creature counts; -1 matches anything.
struct StarterKind
{
    int32 type;
    int32 family;
    int32 unitClass;
    std::vector<uint32> spells;
};

std::vector<StarterKind> const& StarterKinds()
{
    static std::vector<StarterKind> const kinds =
    {
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_WOLF, -1, { 3604, 3149 } },            // Tendon Rip, Furious Howl
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_CAT, -1, { 3604, 12166 } },            // Tendon Rip, Muscle Tear
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_SPIDER, -1, { 6751, 12023 } },         // Weak Poison, Web
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_BEAR, -1, { 3242, 15793 } },           // Ravage, Maul
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_BOAR, -1, { 6268, 3385 } },            // Rushing Charge, Boar Charge
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_CROCOLISK, -1, { 12166 } },            // Muscle Tear
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_CARRION_BIRD, -1, { 5708 } },          // Swoop
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_BIRD_OF_PREY, -1, { 5708 } },          // Swoop
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_CRAB, -1, { 5424 } },                  // Claw Grasp
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_RAPTOR, -1, { 6268, 13443 } },         // Rushing Charge, Rend
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_TALLSTRIDER, -1, { 7272 } },           // Dust Cloud
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_SCORPID, -1, { 6751, 13443 } },        // Weak Poison, Rend
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_BAT, -1, { 8281 } },                   // Sonic Burst
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_RAVAGER, -1, { 13443 } },              // Rend
        { CREATURE_TYPE_BEAST, CREATURE_FAMILY_MOTH, -1, { 36332 } },                 // Rake
        { CREATURE_TYPE_BEAST, -1, -1, { 32906, 13443 } },                            // Chomp, Rend
        { CREATURE_TYPE_HUMANOID, -1, CLASS_MAGE, { 9532, 12544 } },                  // Lightning Bolt, Frost Armor
        { CREATURE_TYPE_HUMANOID, -1, CLASS_PALADIN, { SkinshifterSpells::StolenVigor, 332 } },  // Stolen Vigor, Healing Wave
        { CREATURE_TYPE_HUMANOID, -1, CLASS_ROGUE, { 7159 } },                        // Backstab
        { CREATURE_TYPE_HUMANOID, -1, -1, { 11976, 6016 } },                          // Strike, Pierce Armor
        { CREATURE_TYPE_UNDEAD, -1, -1, { 3234, 3237 } },                             // Disease Touch, Curse of Thule
        { CREATURE_TYPE_DEMON, -1, -1, { SkinshifterSpells::BorrowedShadow, 11976 } },  // Borrowed Shadow, Strike
        { CREATURE_TYPE_ELEMENTAL, -1, -1, { SkinshifterSpells::StolenSpark, 8050 } },  // Stolen Spark, Flame Shock
        { CREATURE_TYPE_MECHANICAL, -1, -1, { 12612 } },                              // Stomp
        { CREATURE_TYPE_GIANT, -1, -1, { 5568 } },                                    // Trample
        { CREATURE_TYPE_DRAGONKIN, -1, -1, { 13443, 11976 } },                        // Rend, Strike
        { -1, -1, -1, { 6751 } },                                                     // Weak Poison (oozes, silithid, ...)
    };
    return kinds;
}

std::vector<uint32> StarterSpellsFor(CreatureTemplate const* tmpl)
{
    auto const matches = [](int32 wanted, uint32 value) { return wanted < 0 || uint32(wanted) == value; };
    for (StarterKind const& kind : StarterKinds())
        if (matches(kind.type, tmpl->type) && matches(kind.family, tmpl->family) && matches(kind.unitClass, tmpl->unit_class))
            return kind.spells;
    return StarterKinds().back().spells;
}

}

std::vector<std::pair<uint32, uint32>> SkinshifterSpells::StarterCreatureSpells()
{
    std::vector<std::pair<uint32, uint32>> spells;
    for (StarterKind const& kind : StarterKinds())
        for (uint32 spell : kind.spells)
            spells.emplace_back(spell, kind.type < 0 ? uint32(CREATURE_TYPE_NOT_SPECIFIED) : uint32(kind.type));
    return spells;
}

namespace
{
// The abilities a creature of this template and level can be robbed of (its own and its scripted casts; a low
// creature with none gives a starter ability of its kind).
std::vector<uint32> LiftableSpellsOf(CreatureTemplate const* tmpl, uint32 level)
{
    std::set<uint32> found;
    for (uint32 spellId : tmpl->spells)
        if (spellId)
            found.insert(spellId);

    auto scripted = ScriptedSpells.find(tmpl->Entry);
    if (scripted != ScriptedSpells.end())
        found.insert(scripted->second.begin(), scripted->second.end());

    std::vector<uint32> liftable;
    for (uint32 spellId : found)
        if (IsLiftable(sSpellMgr->GetSpellInfo(spellId)))
            liftable.push_back(spellId);

    if (liftable.empty() && level <= Cfg.starterMaxLevel)
    {
        std::vector<uint32> const starter = StarterSpellsFor(tmpl);
        uint32 const spellId = starter[tmpl->Entry % starter.size()];
        if (sSpellMgr->GetSpellInfo(spellId))
            liftable.push_back(spellId);
    }
    return liftable;
}

std::vector<uint32> LiftableSpellsOf(Creature const* creature)
{
    return LiftableSpellsOf(creature->GetCreatureTemplate(), creature->GetLevel());
}

bool IsCastAction(uint32 actionType)
{
    return actionType == SMART_ACTION_CAST || actionType == SMART_ACTION_SELF_CAST ||
        actionType == SMART_ACTION_CROSS_CAST || actionType == SMART_ACTION_INVOKER_CAST;
}

void IndexScriptedCasts(uint32 entry, int32 scriptKey)
{
    for (SmartScriptHolder const& holder : sSmartScriptMgr->GetScript(scriptKey, SMART_SCRIPT_TYPE_CREATURE))
        if (IsCastAction(holder.GetActionType()) && holder.action.raw.param1)
            ScriptedSpells[entry].push_back(holder.action.raw.param1);
}

void LoadScriptedSpells()
{
    ScriptedSpells.clear();
    for (auto const& [entry, tmpl] : *sObjectMgr->GetCreatureTemplates())
        IndexScriptedCasts(entry, int32(entry));
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        IndexScriptedCasts(data.id, -int32(spawnId));

    uint32 count = 0;
    for (auto& [entry, spells] : ScriptedSpells)
    {
        std::sort(spells.begin(), spells.end());
        spells.erase(std::unique(spells.begin(), spells.end()), spells.end());
        count += spells.size();
    }
    LOG_INFO("module", "Skinshifter: {} scripted creature spells indexed for {} creatures.", count, ScriptedSpells.size());
}

void LoadConfig()
{
    Cfg.enabled = sConfigMgr->GetOption<bool>("Skinshifter.Enable", true);
    Cfg.liftRange = sConfigMgr->GetOption<float>("Skinshifter.Lift.Range", 10.0f);
    Cfg.allowFriendly = sConfigMgr->GetOption<bool>("Skinshifter.Lift.AllowFriendly", true);
    Cfg.allowBosses = sConfigMgr->GetOption<bool>("Skinshifter.Lift.AllowBosses", false);
    Cfg.allowElites = sConfigMgr->GetOption<bool>("Skinshifter.Lift.AllowElites", true);
    Cfg.maxLevelAbove = sConfigMgr->GetOption<int32>("Skinshifter.Lift.MaxLevelAbovePlayer", 3);
    Cfg.wearInCombat = sConfigMgr->GetOption<bool>("Skinshifter.Wear.InCombat", true);
    Cfg.stealWears = sConfigMgr->GetOption<bool>("Skinshifter.StealForm.WearAtOnce", true);
    Cfg.stealName = sConfigMgr->GetOption<bool>("Skinshifter.Disguise.Name", true);
    Cfg.stealHeld = sConfigMgr->GetOption<bool>("Skinshifter.Disguise.HeldItems", true);
    Cfg.steedPassenger = sConfigMgr->GetOption<bool>("Skinshifter.SteedShift.Passenger", true);
    Cfg.minScale = sConfigMgr->GetOption<float>("Skinshifter.Wear.ScaleFloor", 0.1f);
    Cfg.maxScale = sConfigMgr->GetOption<float>("Skinshifter.Wear.ScaleCeiling", 5.0f);
    Cfg.smallestRadius = sConfigMgr->GetOption<float>("Skinshifter.Wear.SmallestSize", 0.3f);
    Cfg.largestRadius = sConfigMgr->GetOption<float>("Skinshifter.Wear.LargestSize", 2.0f);
    Cfg.swapDelay = sConfigMgr->GetOption<uint32>("Skinshifter.Wear.SwapDelay", 500);
    Cfg.rootWalkDisplay = sConfigMgr->GetOption<uint32>("Skinshifter.RootWalk.Display", 16892);
    Cfg.wraithDisplay = sConfigMgr->GetOption<uint32>("Skinshifter.WraithGlide.Display", 66620);
    Cfg.wraithLift = sConfigMgr->GetOption<float>("Skinshifter.WraithGlide.Lift", 0.5f);
    Cfg.wraithAnimTier = uint8(std::min<uint32>(sConfigMgr->GetOption<uint32>("Skinshifter.WraithGlide.AnimTier", 2), 3));
    Cfg.rootWalkScale = sConfigMgr->GetOption<float>("Skinshifter.RootWalk.Scale", 0.05f);
    Cfg.burrowDisplay = sConfigMgr->GetOption<uint32>("Skinshifter.RootWalk.BurrowDisplay", 462175);
    Cfg.burrowScale = sConfigMgr->GetOption<float>("Skinshifter.RootWalk.BurrowScale", 0.1f);
    Cfg.surfaceDelay = sConfigMgr->GetOption<uint32>("Skinshifter.RootWalk.SurfaceDelay", 500);
    Cfg.nodeRange = sConfigMgr->GetOption<float>("Skinshifter.RootWalk.NodeRange", 100.0f);
    Cfg.hideFromCreatures = sConfigMgr->GetOption<bool>("Skinshifter.RootWalk.HideFromCreatures", true);
    Cfg.blockedAreas.clear();
    std::string const blockedAreas = sConfigMgr->GetOption<std::string>("Skinshifter.RootWalk.BlockedAreas", "876");
    for (std::string_view area : Acore::Tokenize(blockedAreas, ',', false))
        if (Optional<uint32> id = Acore::StringTo<uint32>(area))
            Cfg.blockedAreas.insert(*id);
    Cfg.shortSpellCooldown = sConfigMgr->GetOption<uint32>("Skinshifter.Grimoire.ShortSpellCooldown", 8000);
    Cfg.crowdControlCooldown = sConfigMgr->GetOption<uint32>("Skinshifter.Grimoire.CrowdControlCooldown", 20000);
    Cfg.starterMaxLevel = sConfigMgr->GetOption<uint32>("Skinshifter.Lift.StarterMaxLevel", 10);
    std::string const energy = sConfigMgr->GetOption<std::string>("Skinshifter.Fx.EnergyRaces", "109");
    Cfg.energyRaces.clear();
    for (std::string_view token : Acore::Tokenize(energy, ',', false))
        if (Optional<uint32> id = Acore::StringTo<uint32>(token))
            Cfg.energyRaces.insert(uint8(*id));
    Cfg.fxDelayMin = sConfigMgr->GetOption<uint32>("Skinshifter.Fx.MinSwapDelay", 250);
}

void CreateTables()
{
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `skinshifter_skin_size` (`guid` INT UNSIGNED NOT NULL, `display_id` INT UNSIGNED NOT NULL, "
        "`percent` SMALLINT UNSIGNED NOT NULL DEFAULT 100, PRIMARY KEY (`guid`, `display_id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `skinshifter_skin` (`guid` INT UNSIGNED NOT NULL, `display_id` INT UNSIGNED NOT NULL, "
        "`entry` INT UNSIGNED NOT NULL, `scale` FLOAT NOT NULL DEFAULT 1, `learned` INT UNSIGNED NOT NULL, "
        "PRIMARY KEY (`guid`, `display_id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `skinshifter_grimoire` (`guid` INT UNSIGNED NOT NULL, `spell` INT UNSIGNED NOT NULL, "
        "`entry` INT UNSIGNED NOT NULL, `learned` INT UNSIGNED NOT NULL, PRIMARY KEY (`guid`, `spell`)) "
        "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `skinshifter_worn` (`guid` INT UNSIGNED NOT NULL, `display_id` INT UNSIGNED NOT NULL, "
        "`scale` FLOAT NOT NULL DEFAULT 1, PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `skinshifter_fx` (`guid` INT UNSIGNED NOT NULL, `choice` TINYINT UNSIGNED NOT NULL, "
        "PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
}

bool Refuse(ChatHandler* handler, std::string_view message)
{
    handler->SendSysMessage(message);
    handler->SetSentErrorMessage(true);
    return false;
}

bool MayUse(ChatHandler* handler, Player* player)
{
    if (!Cfg.enabled)
    {
        return Refuse(handler, "Skinshifter is disabled on this realm.");
    }
    // the class is the Skinshifter (its races: Skinshifter.Class.Races); a game master may test on any character
    if (player->getClass() != SkinshifterSpells::ClassId && player->GetSession()->GetSecurity() < SEC_GAMEMASTER)
    {
        return Refuse(handler, "Only a Skinshifter can do that.");
    }
    return true;
}

std::string LiftRefusal(Player* player, Creature* creature)
{
    CreatureTemplate const* tmpl = creature->GetCreatureTemplate();
    if (creature->IsPet() || creature->IsGuardian() || creature->IsSummon() || creature->IsTrigger())
        return "That is not a creature you can lift from.";
    if (tmpl->type == CREATURE_TYPE_CRITTER || tmpl->type == CREATURE_TYPE_TOTEM ||
        tmpl->type == CREATURE_TYPE_NON_COMBAT_PET || tmpl->type == CREATURE_TYPE_GAS_CLOUD)
        return "There is nothing worth stealing from that.";
    if (tmpl->RacialLeader)
        return "Faction leaders guard their secrets.";
    if (!Cfg.allowBosses && (tmpl->rank == CREATURE_ELITE_WORLDBOSS || creature->IsDungeonBoss() || creature->isWorldBoss()))
        return "Its power is too great to lift.";
    if (!Cfg.allowElites && (tmpl->rank == CREATURE_ELITE_ELITE || tmpl->rank == CREATURE_ELITE_RAREELITE))
        return "Elites cannot be lifted on this realm.";
    if (!Cfg.allowFriendly && !creature->IsHostileTo(player))
        return "You can only lift from enemies.";
    if (int32(creature->GetLevel()) > int32(player->GetLevel()) + Cfg.maxLevelAbove)
        return "It is too powerful for you to lift from yet.";
    if (!player->IsWithinDistInMap(creature, Cfg.liftRange))
        return "You must be closer.";
    return "";
}

// A worn skin has the creature's own size in the world, as when the game shows it on a mount or a morph: the scale
// its template gives the model (creature_template_model.DisplayScale). The display's own scale
// (CreatureDisplayInfo) is applied by the client to a player wearing it as to the creature, so it is not applied
// here again (doing so made a Duskbat, display scale 0.1, a tenth of its size). Only the extremes are changed: a
// model radius below Wear.SmallestSize yards grows to it (rats, critters), above Wear.LargestSize it shrinks to it
// (dragons, giants, Onyxia).
float SkinScaleFor(uint32 displayId, float naturalScale)
{
    float scale = naturalScale > 0.0f ? naturalScale : 1.0f;
    if (CreatureModelInfo const* info = sObjectMgr->GetCreatureModelInfo(displayId))
        if (info->bounding_radius > 0.0f)
        {
            float const radius = info->bounding_radius * scale;
            scale *= std::clamp(radius, Cfg.smallestRadius, Cfg.largestRadius) / radius;
        }
    return std::clamp(scale, Cfg.minScale, Cfg.maxScale);
}

// The scale the creature's template gives this model (creature_template_model.DisplayScale).
float TemplateScale(CreatureTemplate const* tmpl, uint32 displayId)
{
    if (!tmpl)
        return 1.0f;
    for (CreatureModel const& model : tmpl->Models)
        if (model.CreatureDisplayID == displayId && model.DisplayScale > 0.0f)
            return model.DisplayScale;
    CreatureModel const* first = tmpl->GetFirstValidModel();
    return first && first->DisplayScale > 0.0f ? first->DisplayScale : 1.0f;
}

float SkinScale(Creature const* creature)
{
    return SkinScaleFor(creature->GetNativeDisplayId(), TemplateScale(creature->GetCreatureTemplate(),
        creature->GetNativeDisplayId()));
}

// Your own size for a skin, in percent of the size the game works out (the Skin Collection's Size slider).
constexpr uint32 SizeMin = 50, SizeMax = 200;

uint32 SizePercent(Player const* player, uint32 displayId)
{
    if (QueryResult result = CharacterDatabase.Query("SELECT `percent` FROM `skinshifter_skin_size` WHERE `guid` = {} AND "
        "`display_id` = {}", player->GetGUID().GetCounter(), displayId))
        return std::clamp<uint32>(result->Fetch()[0].Get<uint32>(), SizeMin, SizeMax);
    return 100;
}

float WithSize(Player const* player, uint32 displayId, float scale)
{
    return std::clamp(scale * float(SizePercent(player, displayId)) / 100.0f, Cfg.minScale, Cfg.maxScale);
}

// The size of a collected skin, worked out again when it is worn so a change of the limits applies to old skins;
// with your own size for it on top.
float CollectedSkinScale(Player const* player, uint32 displayId, uint32 entry)
{
    return WithSize(player, displayId, SkinScaleFor(displayId, TemplateScale(sObjectMgr->GetCreatureTemplate(entry), displayId)));
}

// The size the server uses for melee range and for where attackers stand (bounding radius, combat reach) follows
// the worn model, so creatures do not walk into a large skin; the player's own values come back on Shed.
std::unordered_map<uint32, std::pair<float, float>> NativeSize;

void ApplyLook(Player* player, WornSkin const& skin)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if (!NativeSize.count(guid))
        NativeSize[guid] = { player->GetFloatValue(UNIT_FIELD_BOUNDINGRADIUS), player->GetFloatValue(UNIT_FIELD_COMBATREACH) };
    Reapplying.insert(guid);
    player->SetDisplayId(skin.displayId, skin.scale);
    Reapplying.erase(guid);
    if (CreatureModelInfo const* info = sObjectMgr->GetCreatureModelInfo(skin.displayId))
    {
        if (info->bounding_radius > 0.0f)
            player->SetFloatValue(UNIT_FIELD_BOUNDINGRADIUS, info->bounding_radius * skin.scale);
        if (info->combat_reach > 0.0f)
            player->SetFloatValue(UNIT_FIELD_COMBATREACH, info->combat_reach * skin.scale);
    }
}

void RestoreNativeSize(Player* player)
{
    // a creature model sets the gender byte; the character's own model needs its own back (weapons, race effects)
    player->SetByteValue(UNIT_FIELD_BYTES_0, 2, player->GetByteValue(PLAYER_BYTES_3, 0));
    auto const native = NativeSize.find(player->GetGUID().GetCounter());
    if (native == NativeSize.end())
        return;
    player->SetFloatValue(UNIT_FIELD_BOUNDINGRADIUS, native->second.first);
    player->SetFloatValue(UNIT_FIELD_COMBATREACH, native->second.second);
    NativeSize.erase(native);
}

// Skin buffs: wearing a skin gives the buff of its creature type (Beast: Borrowed Hide), and changing out of it
// leaves that type's Echo for 20 sec. The type comes from the creature the skin was stolen from.
std::map<uint32, uint32> const SkinBuffs =
{
    { CREATURE_TYPE_BEAST, SkinshifterSpells::BorrowedHide }, { CREATURE_TYPE_DRAGONKIN, SkinshifterSpells::SkinDragonkin },
    { CREATURE_TYPE_DEMON, SkinshifterSpells::SkinDemon }, { CREATURE_TYPE_ELEMENTAL, SkinshifterSpells::SkinElemental },
    { CREATURE_TYPE_GIANT, SkinshifterSpells::SkinGiant }, { CREATURE_TYPE_UNDEAD, SkinshifterSpells::SkinUndead },
    { CREATURE_TYPE_HUMANOID, SkinshifterSpells::SkinHumanoid },
    { CREATURE_TYPE_MECHANICAL, SkinshifterSpells::SkinMechanical },
};
std::map<uint32, uint32> const SkinEchoes =
{
    { CREATURE_TYPE_BEAST, SkinshifterSpells::EchoBeast }, { CREATURE_TYPE_DRAGONKIN, SkinshifterSpells::EchoDragonkin },
    { CREATURE_TYPE_DEMON, SkinshifterSpells::EchoDemon }, { CREATURE_TYPE_ELEMENTAL, SkinshifterSpells::EchoElemental },
    { CREATURE_TYPE_GIANT, SkinshifterSpells::EchoGiant }, { CREATURE_TYPE_UNDEAD, SkinshifterSpells::EchoUndead },
    { CREATURE_TYPE_HUMANOID, SkinshifterSpells::EchoHumanoid },
    { CREATURE_TYPE_MECHANICAL, SkinshifterSpells::EchoMechanical },
};

CreatureTemplate const* SkinTemplate(Player* player, uint32 displayId)
{
    if (QueryResult result = CharacterDatabase.Query("SELECT `entry` FROM `skinshifter_skin` WHERE `guid` = {} AND "
        "`display_id` = {}", player->GetGUID().GetCounter(), displayId))
        return sObjectMgr->GetCreatureTemplate(result->Fetch()[0].Get<uint32>());
    return nullptr;
}

uint32 SkinCreatureType(Player* player, uint32 displayId)
{
    CreatureTemplate const* tmpl = SkinTemplate(player, displayId);
    return tmpl ? tmpl->type : 0;
}

// Family traits: what the body can do, while you wear it. Winged beasts glide (CoA's Glide: jump while falling).
constexpr uint32 GlideSpell = 131347;
std::unordered_map<uint32, std::vector<uint32>> const FamilyTraits =
{
    { CREATURE_FAMILY_BAT, { GlideSpell } },
    { CREATURE_FAMILY_BIRD_OF_PREY, { GlideSpell } },
    { CREATURE_FAMILY_CARRION_BIRD, { GlideSpell } },
    { CREATURE_FAMILY_WIND_SERPENT, { GlideSpell } },
    { CREATURE_FAMILY_DRAGONHAWK, { GlideSpell } },
    { CREATURE_FAMILY_NETHER_RAY, { GlideSpell } },
};
// Characters in a winged skin: they glide whenever they fall (UpdateGlide).
std::unordered_set<uint32> Gliders;

void RefreshFamilyTraits(Player* player, uint32 displayId)
{
    std::set<uint32> wanted;
    if (displayId)
        if (CreatureTemplate const* tmpl = SkinTemplate(player, displayId))
            if (auto const traits = FamilyTraits.find(tmpl->family); traits != FamilyTraits.end())
                wanted.insert(traits->second.begin(), traits->second.end());
    if (wanted.count(GlideSpell))
        Gliders.insert(player->GetGUID().GetCounter());
    else
        Gliders.erase(player->GetGUID().GetCounter());
    // A trait is the spell itself, learned for as long as you wear the skin (a temporary spell: never saved, and
    // removing only temporary ones leaves a race's own Glide alone). CoA's client glides only with the spell known.
    for (auto const& [family, spells] : FamilyTraits)
        for (uint32 spellId : spells)
            if (!wanted.count(spellId))
                player->removeSpell(spellId, SPEC_MASK_ALL, true);
    for (uint32 spellId : wanted)
    {
        if (!sSpellMgr->GetSpellInfo(spellId) || player->HasActiveSpell(spellId))
            continue;
        player->learnSpellWithoutAnnouncement(spellId, true);
        if (!player->HasAura(spellId))
            player->AddAura(spellId, player);
        LOG_INFO("module", "Skinshifter: {} gets {} ({}) from the skin: {}.", player->GetName(), spellId,
            sSpellMgr->GetSpellInfo(spellId)->SpellName[0], player->HasActiveSpell(spellId) ? "known" :
            player->HasAura(spellId) ? "as an aura only (the spell could not be learned)" : "FAILED");
    }
}

// Glide ends when you land (its aura goes on landing), and a 3.3.5 client sends nothing for a jump in the air, so a
// winged skin opens its wings by itself: once a fall has lasted past a jump's (GlideAfterMs), Glide comes back.
constexpr uint32 GlideAfterMs = 650;

void UpdateGlide(Player* player)
{
    if (!Gliders.count(player->GetGUID().GetCounter()) || !player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING) ||
        player->m_movementInfo.fallTime < GlideAfterMs || player->HasAura(GlideSpell) || player->IsMounted() ||
        player->IsFlying() || player->IsInWater() || !player->IsAlive())
        return;
    player->CastSpell(player, GlideSpell, true);
}

// Talents: Thousand Skins gives Borrowed Hide in every skin, Hardened Hide makes it twice as strong.
void RefreshSkinBuff(Player* player, uint32 displayId)
{
    RefreshFamilyTraits(player, displayId);
    uint32 const type = displayId ? SkinCreatureType(player, displayId) : 0;
    auto const typeBuff = SkinBuffs.find(type);
    // the skin's own type buff; Thousand Skins makes every skin's hide also Borrowed Hide
    uint32 const own = displayId && typeBuff != SkinBuffs.end() ? typeBuff->second : 0;
    uint32 const wanted = displayId && (own == SkinshifterSpells::BorrowedHide ||
        player->HasSpell(SkinshifterSpells::TalentThousandSkins)) ? uint32(SkinshifterSpells::BorrowedHide) : 0;
    for (auto const& [buffType, spellId] : SkinBuffs)
        if (spellId != wanted && spellId != own)
            player->RemoveAurasDueToSpell(spellId);
    if (own && own != SkinshifterSpells::BorrowedHide && !player->HasAura(own))
        player->CastSpell(player, own, true);
    SpellInfo const* info = wanted ? sSpellMgr->GetSpellInfo(wanted) : nullptr;
    if (!info)
        return;
    bool const hardened = wanted == SkinshifterSpells::BorrowedHide &&
        player->HasSpell(SkinshifterSpells::TalentHardenedHide);
    if (Aura const* aura = player->GetAura(wanted))
    {
        AuraEffect const* first = aura->GetEffect(EFFECT_0);
        bool const isHardened = first && first->GetAmount() > info->Effects[EFFECT_0].CalcValue(player);
        if (isHardened == hardened)
            return;
        player->RemoveAurasDueToSpell(wanted);
    }
    if (!hardened)
    {
        player->CastSpell(player, wanted, true);
        return;
    }
    int32 const health = info->Effects[EFFECT_0].CalcValue(player) * 2;
    int32 const armor = info->Effects[EFFECT_1].CalcValue(player) * 2;
    player->CastCustomSpell(player, wanted, &health, &armor, nullptr, true);
}

// A mount used in a skin: most creature models have no riding pose (a bat lying on a horse), so the mount runs
// riderless (your body invisible in the saddle): Steed Shift, or Possessed Steed for a Possessor. It lasts as long as
// the mount (damage and combat do not end it); when you dismount (also by casting or attacking) the skin comes back.
void EndStride(Player* player)
{
    player->RemoveAurasDueToSpell(SkinshifterSpells::SkinStride);
    player->RemoveAurasDueToSpell(SkinshifterSpells::PossessedSteed);
}

bool IsStride(uint32 spellId)
{
    return spellId == SkinshifterSpells::SkinStride || spellId == SkinshifterSpells::PossessedSteed;
}

// The travel forms (cast to start, cast again to stop): Root Walk, Wraith Running, Phase Running.
bool IsTravel(uint32 buff)
{
    return buff == SkinshifterSpells::RootWalking || buff == SkinshifterSpells::WraithRunning ||
        buff == SkinshifterSpells::PhaseRunning;
}

bool IsTravelCast(uint32 spellId)
{
    return spellId == SkinshifterSpells::RootWalk || spellId == SkinshifterSpells::WraithRun ||
        spellId == SkinshifterSpells::PhaseRun;
}

void EndTravel(Player* player)
{
    for (uint32 buff : { SkinshifterSpells::RootWalking, SkinshifterSpells::WraithRunning, SkinshifterSpells::PhaseRunning })
        player->RemoveAurasDueToSpell(buff);
}

// Steed Shift (or Possessed Steed) as a vehicle with one passenger seat on the mount model's saddle point, as a
// multi-seat mount gives its rider: a party member right-clicks you to ride along, where your invisible body sits.
// The seat goes with the shape (and with a change of map, which takes every vehicle away).
void GiveSteedSeat(Player* player)
{
    if (!Cfg.steedPassenger || player->GetVehicleKit() || player->GetVehicle() ||
        !sVehicleStore.LookupEntry(SkinshifterSpells::SteedVehicleId) ||
        !player->CreateVehicleKit(SkinshifterSpells::SteedVehicleId, 0))
        return;
    player->GetVehicleKit()->Reset();
    WorldPacket data(SMSG_PLAYER_VEHICLE_DATA, player->GetPackGUID().size() + 4);
    data << player->GetPackGUID();
    data << uint32(SkinshifterSpells::SteedVehicleId);
    player->SendMessageToSet(&data, true);
}

// ---- Steed Shift's reins (experimental) ----------------------------------------------------------------------
// The passenger steers by possessing the steed (Reins: Mind Control without its look). The keys that do nothing for
// the one not steering ask for the reins through the addon (SkinReins.lua): the passenger takes them, the steed
// takes them back at once (it is their body).
void SendReinsLine(Player* player, std::string const& line)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, "SKIN\t" + line);
    player->SendDirectMessage(&packet);
}
std::unordered_map<uint32, ObjectGuid> ReinsRider;  // steed low guid -> the passenger holding the reins

bool IsSteed(Player* steed)
{
    return steed && (steed->HasAura(SkinshifterSpells::SkinStride) || steed->HasAura(SkinshifterSpells::PossessedSteed)) &&
        steed->GetVehicleKit() && steed->GetVehicleKit()->GetVehicleInfo()->m_ID == SkinshifterSpells::SteedVehicleId;
}

Player* SteedOf(Player* rider)
{
    Unit* base = rider->GetVehicleBase();
    Player* steed = base ? base->ToPlayer() : nullptr;
    return IsSteed(steed) ? steed : nullptr;
}

void DropReins(Player* steed)
{
    auto const found = ReinsRider.find(steed->GetGUID().GetCounter());
    if (found == ReinsRider.end())
        return;
    ObjectGuid const riderGuid = found->second;
    ReinsRider.erase(found);
    steed->RemoveAurasDueToSpell(SkinshifterSpells::SteedReins);
    SendReinsLine(steed, "REINS;off");
    if (Player* rider = ObjectAccessor::FindConnectedPlayer(riderGuid))
        SendReinsLine(rider, SteedOf(rider) == steed ? "REINS;rider" : "REINS;off");
    LOG_INFO("module", "Skinshifter: {} has the reins again.", steed->GetName());
}

// REINS (a press of a movement key) or REINS? (just boarded: is this a steed's seat?)
void HandleReins(Player* player, bool asking)
{
    if (Player* steed = SteedOf(player))
    {
        auto const held = ReinsRider.find(steed->GetGUID().GetCounter());
        bool const holding = held != ReinsRider.end() && held->second == player->GetGUID();
        if (asking || held != ReinsRider.end() || steed->GetCharmerGUID() || steed->IsInCombat() || player->IsInCombat())
        {
            SendReinsLine(player, holding ? "REINS;off" : "REINS;rider");
            return;
        }
        player->AddAura(SkinshifterSpells::SteedReins, steed);
        if (steed->GetCharmerGUID() != player->GetGUID())
        {
            steed->RemoveAurasDueToSpell(SkinshifterSpells::SteedReins);
            LOG_INFO("module", "Skinshifter: {} could not take {}'s reins (the possession did not hold).",
                player->GetName(), steed->GetName());
            return;
        }
        ReinsRider[steed->GetGUID().GetCounter()] = player->GetGUID();
        SendReinsLine(player, "REINS;off");
        SendReinsLine(steed, "REINS;steed");
        LOG_INFO("module", "Skinshifter: {} takes {}'s reins.", player->GetName(), steed->GetName());
        return;
    }
    if (!asking)
        DropReins(player);
}

// the passenger got off, the possession ended some other way, or a fight began (the steed steers in combat: the
// addon cannot move the keys then, so neither could ask for the reins back)
void CheckReins(Player* steed)
{
    auto const found = ReinsRider.find(steed->GetGUID().GetCounter());
    if (found == ReinsRider.end())
        return;
    Player* rider = ObjectAccessor::FindConnectedPlayer(found->second);
    if (!rider || SteedOf(rider) != steed || !steed->HasAura(SkinshifterSpells::SteedReins) || steed->IsInCombat() ||
        rider->IsInCombat())
        DropReins(steed);
}

void TakeSteedSeat(Player* player)
{
    DropReins(player);
    Vehicle* kit = player->GetVehicleKit();
    if (!kit || kit->GetVehicleInfo()->m_ID != SkinshifterSpells::SteedVehicleId)
        return;
    WorldPacket data(SMSG_PLAYER_VEHICLE_DATA, player->GetPackGUID().size() + 4);
    data << player->GetPackGUID();
    data << uint32(0);
    player->SendMessageToSet(&data, true);
    player->RemoveVehicleKit();  // the passenger gets off
}

constexpr uint32 InvisibleRiderDisplay = 11686;  // "Invisible Stalker": a model that draws nothing

void StrideInsteadOfMount(Player* player)
{
    if (!player->IsMounted() && !player->HasAuraType(SPELL_AURA_MOUNTED))
        return;
    if (player->IsFlying() || player->IsInFlight())
        return;  // on a flying mount in the air: taking the mount away would drop you
    uint32 const stride = StyleOf(player) == FxStyle::Skinshifter ? SkinshifterSpells::SkinStride :
        SkinshifterSpells::PossessedSteed;
    if (!player->GetUInt32Value(UNIT_FIELD_MOUNTDISPLAYID) || !sSpellMgr->GetSpellInfo(stride))
        return;
    // the buff shows the speed and carries the ending rules; the mount itself gives the speed (a mounted speed
    // buff and this one do not add up). Already on (a login, mounted): only the look and the seat again.
    if (!player->HasAura(stride))
    {
        int32 speed = player->GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED);
        if (speed <= 0)
            speed = player->GetSkillValue(SKILL_RIDING) >= 150 ? 100 : 60;
        player->CastCustomSpell(player, stride, &speed, nullptr, nullptr, true);
        if (!player->HasAura(stride))
            return;
    }
    // You stay mounted and your body goes invisible: the mount runs riderless, and a passenger gets the real mount's
    // saddle and riding pose (on your own body shaped as the mount the client never seated one: she stood upright).
    uint32 const guid = player->GetGUID().GetCounter();
    Reapplying.insert(guid);
    player->SetDisplayId(InvisibleRiderDisplay);
    Reapplying.erase(guid);
    TemporaryLook.insert(guid);
    RefreshWornFx(player, false);  // the worn skin's effects would hang in the empty saddle
    GiveSteedSeat(player);
}

// The client blends a change of size (OBJECT_FIELD_SCALE_X) over a moment but swaps the model at once, so a model
// and a size changed together show the new model at the old size first: a huge player after a large skin, a
// player-sized Onyxia. Between two looks of a different size the player is shown as the invisible model for
// Wear.SwapDelay ms while the size blends, covered by the pulse effect; then the new look comes at its own size.
// A later change of look cancels a pending one.
constexpr uint32 InvisibleDisplay = 11686;
std::unordered_map<uint32, uint32> LookSerial;

void SwapLook(Player* player, float fromScale, float toScale, std::function<void()> show)
{
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const serial = ++LookSerial[guid];
    if (!Cfg.swapDelay || std::fabs(fromScale - toScale) < 0.05f)
    {
        show();
        return;
    }
    Reapplying.insert(guid);
    player->SetDisplayId(InvisibleDisplay, toScale);
    Reapplying.erase(guid);
    player->m_Events.AddEventAtOffset([guid, serial, show]()
    {
        if (LookSerial[guid] == serial)
            show();
    }, Milliseconds(Cfg.swapDelay));
}

// The Echo of the skin you change out of (of another type than the one you change into), for 20 sec.
void LeaveSkinEcho(Player* player, uint32 leftDisplay, uint32 newDisplay)
{
    uint32 const left = SkinCreatureType(player, leftDisplay);
    if (!left || (newDisplay && SkinCreatureType(player, newDisplay) == left))
        return;
    if (auto const echo = SkinEchoes.find(left); echo != SkinEchoes.end())
        player->CastSpell(player, echo->second, true);
}

// ---- The disguise: the creature's name and what it holds -------------------------------------------------
// With Skinshifter.Disguise.Name others see the creature's name above you: every name answer about you
// (SMSG_NAME_QUERY_RESPONSE) is rewritten and the players around you get the new name at once. The client keeps one
// name per player (by guid) for everything, chat included, so your chat goes out under a stand-in guid (your
// "chat guid", ChatGuidOf) whose name is your own: chat, whispers and replies keep your real name. You see your own
// name as always (nothing is rewritten for you). Your hands show what the creature holds, its equipment from creature_equip_template
// (Skinshifter.Disguise.HeldItems); your own weapons come back when you shed.
std::unordered_map<uint32, std::string> StolenNames;  // low guid -> the name others see

struct HeldItems
{
    std::array<uint32, 3> shown{};  // main hand, off hand, ranged as the creature holds them
    std::array<uint32, 3> own{};    // yours, for when you shed
};
std::unordered_map<uint32, HeldItems> StolenHeld;

uint16 VisibleItemField(uint8 hand)
{
    return uint16(PLAYER_VISIBLE_ITEM_1_ENTRYID + (EQUIPMENT_SLOT_MAINHAND + hand) * 2);
}

template <typename Scripts>
void ApplyNameQueryRace(Scripts* scripts, Player* player, uint8& race)
{
    if constexpr (requires { scripts->OnPlayerGetNameQueryRace(player, race); })
        scripts->OnPlayerGetNameQueryRace(player, race);
}

WorldPacket NameAnswer(Player* player, std::string const& name)
{
    uint8 race = player->getRace();
    ApplyNameQueryRace(sScriptMgr, player, race);
    WorldPacket data(SMSG_NAME_QUERY_RESPONSE, 8 + 1 + name.size() + 1 + 1 + 3 + 1);
    data << player->GetPackGUID();
    data << uint8(0);         // the name is known
    data << name;
    data << std::string();    // realm
    data << race << player->getGender() << player->getClass();
    data << uint8(0);         // no declined names
    return data;
}

// A client keeps the name it showed when the unit came into view (a later name answer only fills its cache), so
// the player is shown to the viewer afresh. Not to a passenger riding the player.
void ShowAfresh(Player* player, Player* viewer)
{
    if (viewer == player || !viewer->HaveAtClient(player) || viewer->GetVehicleBase() == player)
        return;
    player->DestroyForPlayer(viewer);
    viewer->GetObjectVisibilityContainer().UnlinkWorldObjectVisibility(player);
    viewer->UpdateVisibilityOf(player);
}

void SendNameAround(Player* player, std::string const& name)
{
    WorldPacket data = NameAnswer(player, name);
    player->SendMessageToSet(&data, false);  // not to yourself: you keep seeing your own name
    std::vector<Player*> viewers;
    for (auto const& [guid, viewer] : player->GetObjectVisibilityContainer().GetVisiblePlayersMap())
        viewers.push_back(viewer);
    for (Player* viewer : viewers)
        ShowAfresh(player, viewer);
}

// The stand-in guid a disguised player's chat comes from (its name is the player's own), and back.
constexpr uint32 ChatGuidBit = 0x40000000;

ObjectGuid ChatGuidOf(ObjectGuid guid)
{
    return ObjectGuid::Create<HighGuid::Player>(guid.GetCounter() | ChatGuidBit);
}

bool IsChatGuid(ObjectGuid guid)
{
    return guid.IsPlayer() && (guid.GetCounter() & ChatGuidBit);
}

ObjectGuid OwnerOfChatGuid(ObjectGuid guid)
{
    return ObjectGuid::Create<HighGuid::Player>(guid.GetCounter() & ~ChatGuidBit);
}

// The name others see in a name answer about this player ("" when not disguised).
std::string const* StolenNameOf(ObjectGuid guid)
{
    if (!guid.IsPlayer())
        return nullptr;
    auto const found = StolenNames.find(guid.GetCounter());
    return found == StolenNames.end() ? nullptr : &found->second;
}

void KeepHeldItems(Player* player)
{
    auto held = StolenHeld.find(player->GetGUID().GetCounter());
    if (held == StolenHeld.end())
        return;
    for (uint8 hand = 0; hand < 3; ++hand)
    {
        uint32 const now = player->GetUInt32Value(VisibleItemField(hand));
        if (now == held->second.shown[hand])
            continue;
        held->second.own[hand] = now;  // you changed what you hold (equip, transmog): kept for later
        player->SetUInt32Value(VisibleItemField(hand), held->second.shown[hand]);
    }
}

void PutOnDisguise(Player* player, uint32 entry)
{
    uint32 const guid = player->GetGUID().GetCounter();
    CreatureTemplate const* tmpl = entry ? sObjectMgr->GetCreatureTemplate(entry) : nullptr;
    if (Cfg.stealName)
    {
        if (tmpl && !tmpl->Name.empty())
        {
            StolenNames[guid] = tmpl->Name;
            SendNameAround(player, tmpl->Name);
        }
        else if (StolenNames.erase(guid))
            SendNameAround(player, player->GetName());
    }
    if (!Cfg.stealHeld)
        return;
    HeldItems held;
    auto const before = StolenHeld.find(guid);
    for (uint8 hand = 0; hand < 3; ++hand)
        held.own[hand] = before != StolenHeld.end() ? before->second.own[hand] : player->GetUInt32Value(VisibleItemField(hand));
    int8 equipment = 1;
    if (EquipmentInfo const* info = tmpl ? sObjectMgr->GetEquipmentInfo(entry, equipment) : nullptr)
        for (uint8 hand = 0; hand < 3; ++hand)
            held.shown[hand] = info->ItemEntry[hand];
    StolenHeld[guid] = held;
    for (uint8 hand = 0; hand < 3; ++hand)
        player->SetUInt32Value(VisibleItemField(hand), held.shown[hand]);
}

void TakeOffDisguise(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if (StolenNames.erase(guid))
        SendNameAround(player, player->GetName());
    auto const held = StolenHeld.find(guid);
    if (held == StolenHeld.end())
        return;
    for (uint8 hand = 0; hand < 3; ++hand)
        player->SetUInt32Value(VisibleItemField(hand), held->second.own[hand]);
    StolenHeld.erase(held);
}

void Wear(Player* player, WornSkin const& skin)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if (auto const before = Worn.find(guid); before != Worn.end() && before->second.displayId != skin.displayId)
        LeaveSkinEcho(player, before->second.displayId, skin.displayId);
    EndStride(player);
    EndTravel(player);
    float const fromScale = player->GetObjectScale();
    {
        CreatureDisplayInfoEntry const* display = sCreatureDisplayInfoStore.LookupEntry(skin.displayId);
        CreatureModelInfo const* model = sObjectMgr->GetCreatureModelInfo(skin.displayId);
        LOG_INFO("module", "Skinshifter: {} wears display {} at scale {:.3f} (display scale {:.3f}, model radius {:.3f}, "
            "combat reach {:.3f}).", player->GetName(), skin.displayId, skin.scale, display ? display->scale : 0.0f,
            model ? model->bounding_radius : 0.0f, model ? model->combat_reach : 0.0f);
    }
    Worn[guid] = skin;
    Pulse(player);
    SwapLook(player, fromScale, skin.scale, [player, skin]()
    {
        ApplyLook(player, skin);
        PutOnDisguise(player, skin.entry);
        RefreshWornFx(player, true);
        RefreshSkinBuff(player, skin.displayId);
        StrideInsteadOfMount(player);
    });
    CharacterDatabase.Execute("REPLACE INTO `skinshifter_worn` (`guid`, `display_id`, `scale`) VALUES ({}, {}, {})",
        guid, skin.displayId, skin.scale);
}

void Shed(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    float const fromScale = player->GetObjectScale();
    if (auto const before = Worn.find(guid); before != Worn.end())
        LeaveSkinEcho(player, before->second.displayId, 0);
    Worn.erase(guid);
    TakeOffDisguise(player);
    Pulse(player);
    RefreshWornFx(player, false);
    RefreshSkinBuff(player, 0);
    EndStride(player);
    EndTravel(player);
    SwapLook(player, fromScale, player->GetNativeObjectScale(), [player]()
    {
        player->RestoreDisplayId();  // sets the size to 1, so the native size comes after it
        player->SetObjectScale(player->GetNativeObjectScale());
        RestoreNativeSize(player);
    });
    CharacterDatabase.Execute("DELETE FROM `skinshifter_worn` WHERE `guid` = {}", guid);
}

// The skin (or your own look) comes back after a mount's shape or the Root Walk worm.
void RestoreAfterTemporaryLook(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if (!TemporaryLook.erase(guid))
        return;
    float const fromScale = player->GetObjectScale();
    auto const worn = Worn.find(guid);
    if (worn != Worn.end())
    {
        WornSkin const skin = worn->second;
        SwapLook(player, fromScale, skin.scale, [player, skin]() { ApplyLook(player, skin); });
        return;
    }
    SwapLook(player, fromScale, player->GetNativeObjectScale(), [player]()
    {
        player->RestoreDisplayId();
        player->SetObjectScale(player->GetNativeObjectScale());
        RestoreNativeSize(player);
    });
}

// ---- Root Walk: underground, and up as what grows nearby -------------------------------------------------------
// Moving, you dig along underground (a burrowing mound of earth) and pass under buildings, trees and fences: the
// client patch skinshifter-burrow-collision.patch lets only the ground stop Root Walking. Standing still for
// RootWalk.SurfaceDelay ms where the sky is open you come up as the nearest herb or ore vein within
// RootWalk.NodeRange yards (every spawn point of a gathering node in the world database counts, also the ones a
// pool has not spawned right now), at the node's own size; in a city, a rested area or a sanctuary, or with no node
// near, you come up as the worm. Creatures ignore you all the while (RootWalk.HideFromCreatures). Under something
// you cannot come up or end it; if it ends there anyway (damage), you come up at the last open spot. Instances and
// RootWalk.BlockedAreas are closed to it: digging into one puts you back where you came from.
// All these looks are client rows at the look's own size (SkinshifterAdvancement.cpp), so your size stays 1.0 and a
// change of look is instant.
struct NodeSpot
{
    float x, y, z;
    SkinshifterSpells::NodeLook look;
};
std::unordered_map<uint32, std::vector<NodeSpot>> NodeSpots;  // by map

struct Burrow
{
    bool surfaced = false;
    uint32 still = 0;
    uint32 sinceCheck = 0;
    bool covered = false;
    bool haveOpenSpot = false;
    uint32 map = 0;
    Position openSpot;  // the last place where the sky was open and digging was allowed
    bool warned = false;
};
std::unordered_map<uint32, Burrow> Burrowing;
std::unordered_set<uint32> Hidden;  // the ones creatures ignore

void LoadNodeSpots()
{
    NodeSpots.clear();
    uint32 count = 0;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllGOData())
    {
        GameObjectTemplate const* object = sObjectMgr->GetGameObjectTemplate(data.id);
        if (!object)
            continue;
        SkinshifterSpells::NodeLook const look = SkinshifterSpells::NodeLookOf(object->displayId);
        if (!look.display)
            continue;
        NodeSpots[data.mapid].push_back({ data.posX, data.posY, data.posZ, look });
        ++count;
    }
    LOG_INFO("module", "Skinshifter: Root Walk knows {} herb and ore spawn points on {} maps.", count, NodeSpots.size());
}

bool InSafePlace(Player* player)
{
    if (player->HasPlayerFlag(PLAYER_FLAGS_RESTING) || player->IsInSanctuary())
        return true;
    for (uint32 area : { player->GetAreaId(), player->GetZoneId() })
        if (AreaTableEntry const* entry = sAreaTableStore.LookupEntry(area))
            if (entry->flags & (AREA_FLAG_CAPITAL | AREA_FLAG_SANCTUARY))
                return true;
    return false;
}

bool ClosedToDigging(Player* player)
{
    if (player->GetMap()->Instanceable())
        return true;
    return Cfg.blockedAreas.count(player->GetAreaId()) || Cfg.blockedAreas.count(player->GetZoneId());
}

// Something above you (a building, a tree, a fence, an object): the server's collision data straight up from you.
bool Covered(Player* player)
{
    float const x = player->GetPositionX(), y = player->GetPositionY(), z = player->GetPositionZ();
    return !player->GetMap()->isInLineOfSight(x, y, z + 0.5f, x, y, z + 12.0f, player->GetPhaseMask(),
        LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing);
}

WornSkin WormLook()
{
    SkinshifterSpells::RootWalkLooks const looks = SkinshifterSpells::RootWalkLooksOf();
    return looks.worm ? WornSkin{ looks.worm, 1.0f } : WornSkin{ Cfg.rootWalkDisplay, Cfg.rootWalkScale };
}

WornSkin BurrowLook()
{
    SkinshifterSpells::RootWalkLooks const looks = SkinshifterSpells::RootWalkLooksOf();
    return looks.burrow ? WornSkin{ looks.burrow, 1.0f } : WornSkin{ Cfg.burrowDisplay, Cfg.burrowScale };
}

WornSkin SurfaceLook(Player* player)
{
    if (InSafePlace(player))
        return WormLook();
    auto const spots = NodeSpots.find(player->GetMapId());
    if (spots == NodeSpots.end())
        return WormLook();
    float best = Cfg.nodeRange * Cfg.nodeRange;
    NodeSpot const* nearest = nullptr;
    for (NodeSpot const& spot : spots->second)
    {
        float const dx = spot.x - player->GetPositionX(), dy = spot.y - player->GetPositionY(),
            dz = spot.z - player->GetPositionZ();
        float const distance = dx * dx + dy * dy + dz * dz;
        if (distance < best)
        {
            best = distance;
            nearest = &spot;
        }
    }
    return nearest ? WornSkin{ nearest->look.display, nearest->look.scale } : WormLook();
}

void ShowBurrowLook(Player* player, WornSkin const& look)
{
    SwapLook(player, player->GetObjectScale(), look.scale, [player, look]() { ApplyLook(player, look); });
}

void StartBurrow(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    Burrow state;
    state.haveOpenSpot = !Covered(player);
    state.map = player->GetMapId();
    state.openSpot = player->GetPosition();
    Burrowing[guid] = state;
    if (Cfg.hideFromCreatures)
        Hidden.insert(guid);
    ShowBurrowLook(player, BurrowLook());
}

void EndBurrow(uint32 guid)
{
    Burrowing.erase(guid);
    Hidden.erase(guid);
}

// Root Walking ended (cast again, damage, a map change): under something, you come up at the last open spot.
void ComeUp(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    auto const burrow = Burrowing.find(guid);
    if (burrow == Burrowing.end())
        return;
    Burrow const state = burrow->second;
    EndBurrow(guid);
    if (!state.haveOpenSpot || !player->IsInWorld() || !player->IsAlive() || player->GetMapId() != state.map ||
        !Covered(player))
        return;
    Position const spot = state.openSpot;
    uint32 const map = state.map;
    player->m_Events.AddEventAtOffset([player, spot, map]()
    {
        if (player->IsInWorld() && player->IsAlive() && player->GetMapId() == map)
            player->NearTeleportTo(spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), player->GetOrientation());
    }, 1ms);
}

void UpdateBurrow(Player* player, uint32 diff)
{
    uint32 const guid = player->GetGUID().GetCounter();
    auto burrow = Burrowing.find(guid);
    if (burrow == Burrowing.end())
        return;
    if (!player->HasAura(SkinshifterSpells::RootWalking))
    {
        EndBurrow(guid);
        return;
    }
    Burrow& state = burrow->second;
    state.sinceCheck += diff;
    if (state.sinceCheck >= 250)
    {
        state.sinceCheck = 0;
        state.covered = Covered(player);
        if (ClosedToDigging(player))
        {
            if (state.haveOpenSpot)
                player->NearTeleportTo(state.openSpot.GetPositionX(), state.openSpot.GetPositionY(),
                    state.openSpot.GetPositionZ(), player->GetOrientation());
            if (!state.warned)
                ChatHandler(player->GetSession()).SendSysMessage("You cannot dig any further here.");
            state.warned = true;
            return;
        }
        state.warned = false;
        if (!state.covered)
        {
            state.haveOpenSpot = true;
            state.openSpot = player->GetPosition();
        }
    }
    if (player->isMoving())
    {
        state.still = 0;
        if (state.surfaced)
        {
            state.surfaced = false;
            ShowBurrowLook(player, BurrowLook());
        }
        return;
    }
    if (state.surfaced || state.covered)
        return;
    state.still += diff;
    if (state.still < Cfg.surfaceDelay)
        return;
    state.surfaced = true;
    ShowBurrowLook(player, SurfaceLook(player));
}

// The travel forms, at mount speed (+60%, +100% from riding skill 150), out of combat, cast again to stop; attacking,
// casting another spell or taking damage also ends them; outdoors only, like a mount. Root Walk goes underground (see
// above); Wraith Running and Phase Running keep your look and add their effect.
// The toggle is instant, so it also ends the form on the move, like a mount; starting it casts the buff itself, which
// takes 1.5 sec (out of combat, outdoors, standing still). TravelStarted follows when the buff is on.
void ToggleTravel(ChatHandler* handler, Player* player, uint32 buff)
{
    if (player->HasAura(buff))
    {
        if (buff == SkinshifterSpells::RootWalking)
            if (auto const burrow = Burrowing.find(player->GetGUID().GetCounter());
                burrow != Burrowing.end() && Covered(player))
            {
                Refuse(handler, "Something is above you: you can only come up where the sky is open.");
                return;
            }
        player->RemoveAurasDueToSpell(buff);
        return;
    }
    if (player->IsInCombat())
    {
        Refuse(handler, "You cannot do that in combat.");
        return;
    }
    if (buff == SkinshifterSpells::RootWalking && ClosedToDigging(player))
    {
        Refuse(handler, "You cannot dig here.");
        return;
    }
    if (!player->IsOutdoors())  // checked here, not by the spell, so the form lasts under roofs (see ShapeRootWalking)
    {
        Refuse(handler, "You can only start that outdoors.");
        return;
    }
    if (player->isMoving())
    {
        Refuse(handler, "You have to stand still for that.");
        return;
    }
    if (player->IsMounted())
    {
        player->RemoveAurasByType(SPELL_AURA_MOUNTED);
        player->Dismount();
    }
    EndStride(player);
    EndTravel(player);
    int32 const speed = player->GetSkillValue(SKILL_RIDING) >= 150 ? 100 : 60;
    ObjectGuid const guid = player->GetGUID();
    // a moment later: the toggle's own cast is still running now, and a new cast would interrupt it
    player->m_Events.AddEventAtOffset([player, guid, buff, speed]()
    {
        if (player->GetGUID() != guid || !player->IsInWorld())
            return;
        int32 bp = speed;
        player->CastCustomSpell(player, buff, &bp, nullptr, nullptr, false);
    }, 1ms);
}

void TravelStarted(Player* player, uint32 buff)
{
    RefreshWornFx(player, false);  // a travel form has its own look: the worn skin's effects wait until it ends
    if (buff == SkinshifterSpells::WraithRunning && Cfg.wraithDisplay &&
        sCreatureDisplayInfoStore.LookupEntry(Cfg.wraithDisplay))
    {
        // Wraith Glide: a floating wraith, hovering a little (the buff's hover; this is how high), its own
        // gliding movement instead of your run; the client patch lets it pass through obstacles
        TemporaryLook.insert(player->GetGUID().GetCounter());
        WornSkin const look{ Cfg.wraithDisplay, SkinScaleFor(Cfg.wraithDisplay, 1.0f) };
        SwapLook(player, player->GetObjectScale(), look.scale, [player, look]() { ApplyLook(player, look); });
        player->SetFloatValue(UNIT_FIELD_HOVERHEIGHT, Cfg.wraithLift);
        // the hover or fly animations instead of the run (which swings the arms)
        player->SetByteValue(UNIT_FIELD_BYTES_1, UNIT_BYTES_1_OFFSET_ANIM_TIER, Cfg.wraithAnimTier);
        return;
    }
    if (buff != SkinshifterSpells::RootWalking)
        return;
    TemporaryLook.insert(player->GetGUID().GetCounter());
    StartBurrow(player);
}

// The ability a Skinshifter learns for a creature spell: its Grimoire copy when there is one.
uint32 LearnedIdOf(uint32 creatureSpell)
{
    uint32 const copy = SkinshifterSpells::CopyOf(creatureSpell);
    return copy && sSpellMgr->GetSpellInfo(copy) ? copy : creatureSpell;
}

// Loads the Grimoire. Abilities stolen before the Grimoire copies existed (the creature's own spell) become their
// copy: in the spellbook, on the action bars and in the Grimoire.
void MigrateToCopies(Player* player, std::unordered_set<uint32>& grimoire)
{
    uint32 const guid = player->GetGUID().GetCounter();
    QueryResult result = CharacterDatabase.Query("SELECT `spell`, `entry`, `learned` FROM `skinshifter_grimoire` "
        "WHERE `guid` = {}", guid);
    if (!result)
        return;
    uint32 moved = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 const original = fields[0].Get<uint32>();
        uint32 const copy = LearnedIdOf(original);
        grimoire.insert(copy);
        if (copy == original)
            continue;
        if (player->HasSpell(original))
            player->removeSpell(original, SPEC_MASK_ALL, false);
        if (!player->HasSpell(copy))
            player->learnSpell(copy);
        for (uint8 slot = 0; slot < MAX_ACTION_BUTTONS; ++slot)
            if (ActionButton const* action = player->GetActionButton(slot))
                if (action->GetType() == ACTION_BUTTON_SPELL && action->GetAction() == original)
                    player->addActionButton(slot, copy, ACTION_BUTTON_SPELL);
        CharacterDatabase.Execute("DELETE FROM `skinshifter_grimoire` WHERE `guid` = {} AND `spell` = {}", guid, original);
        CharacterDatabase.Execute("REPLACE INTO `skinshifter_grimoire` (`guid`, `spell`, `entry`, `learned`) VALUES "
            "({}, {}, {}, {})", guid, copy, fields[1].Get<uint32>(), fields[2].Get<uint32>());
        CollectionKnown.erase(guid);  // the addon's list is out of date
        ++moved;
    } while (result->NextRow());
    if (moved)
    {
        player->SendActionButtons(1);
        LOG_INFO("module", "Skinshifter: {}'s {} stolen abilities became their Grimoire copies.", player->GetName(), moved);
    }
}

// The same ability stolen from several creatures (Aimed Shot from every archer): the spellbook keeps one of a name,
// the strongest, and the others stay in the Grimoire unlearned. Strongest: the biggest effect (damage, healing, an
// aura's amount), then the shortest cooldown, then the newest spell id.
std::tuple<int32, int32, uint32> StrengthOf(SpellInfo const* info)
{
    int32 biggest = 0;
    for (SpellEffectInfo const& effect : info->GetEffects())
        if (effect.IsEffect())
            biggest = std::max(biggest, std::abs(effect.CalcValue()));
    int32 const cooldown = int32(std::max(info->RecoveryTime, info->CategoryRecoveryTime));
    return { biggest, -cooldown, info->Id };
}

std::string NameOf(SpellInfo const* info)
{
    return info->SpellName[0] ? std::string(info->SpellName[0]) : std::string();
}

// A known Grimoire ability of the same name at least as strong (so `info` is not worth a place in the book).
bool WeakerThanKnown(Player* player, SpellInfo const* info)
{
    auto const grimoire = Grimoires.find(player->GetGUID().GetCounter());
    if (!info || grimoire == Grimoires.end())
        return false;
    std::string const name = NameOf(info);
    for (uint32 spellId : grimoire->second)
        if (spellId != info->Id && player->HasSpell(spellId))
            if (SpellInfo const* other = sSpellMgr->GetSpellInfo(spellId))
                if (NameOf(other) == name && StrengthOf(other) >= StrengthOf(info))
                    return true;
    return false;
}

// Spells to unlearn, a few at a time from the player's update (hundreds at once froze the client at login).
std::unordered_map<uint32, std::deque<uint32>> UnlearnQueue;

// Unlearns the weaker of every name the book holds twice; a bar button of one goes to the one kept.
uint32 KeepStrongestByName(Player* player)
{
    auto const grimoire = Grimoires.find(player->GetGUID().GetCounter());
    if (grimoire == Grimoires.end())
        return 0;
    std::unordered_map<std::string, SpellInfo const*> best;
    for (uint32 spellId : grimoire->second)
        if (player->HasSpell(spellId))
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
            {
                SpellInfo const*& kept = best[NameOf(info)];
                if (!kept || StrengthOf(info) > StrengthOf(kept))
                    kept = info;
            }
    uint32 forgotten = 0;
    bool buttons = false;
    for (uint32 spellId : grimoire->second)
    {
        if (!player->HasSpell(spellId))
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        SpellInfo const* kept = info ? best[NameOf(info)] : nullptr;
        if (!kept || kept == info)
            continue;
        for (uint8 slot = 0; slot < MAX_ACTION_BUTTONS; ++slot)
            if (ActionButton const* action = player->GetActionButton(slot))
                if (action->GetType() == ACTION_BUTTON_SPELL && action->GetAction() == spellId)
                {
                    player->addActionButton(slot, kept->Id, ACTION_BUTTON_SPELL);
                    buttons = true;
                }
        auto& queue = UnlearnQueue[player->GetGUID().GetCounter()];
        if (std::find(queue.begin(), queue.end(), spellId) == queue.end())
            queue.push_back(spellId);  // a few at a time (DrainQueues)
        ++forgotten;
    }
    if (buttons)
        player->SendActionButtons(1);
    if (forgotten)
        LOG_INFO("module", "Skinshifter: {} kept the strongest of each ability name; {} weaker copies left the book.",
            player->GetName(), forgotten);
    return forgotten;
}

void LoadPlayer(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    // a stride or Root Walk saved at logout: its look is gone, so its speed goes too; Steed Shift on a mount you
    // logged out on stays (set up again below; ending it would dismount you)
    TemporaryLook.erase(guid);
    if (!player->IsMounted() && !player->HasAuraType(SPELL_AURA_MOUNTED))
        EndStride(player);
    EndTravel(player);
    auto& grimoire = Grimoires[guid];
    grimoire.clear();
    MigrateToCopies(player, grimoire);
    FxChoice.erase(guid);
    if (QueryResult result = CharacterDatabase.Query("SELECT `choice` FROM `skinshifter_fx` WHERE `guid` = {}", guid))
        FxChoice[guid] = result->Fetch()[0].Get<uint8>();
    bool wearing = false;
    if (QueryResult result = CharacterDatabase.Query("SELECT w.`display_id`, w.`scale`, s.`entry` FROM `skinshifter_worn` w "
        "LEFT JOIN `skinshifter_skin` s ON s.`guid` = w.`guid` AND s.`display_id` = w.`display_id` WHERE w.`guid` = {}", guid))
    {
        Field* fields = result->Fetch();
        uint32 const displayId = fields[0].Get<uint32>();
        // worked out again, so a change of the size rules also applies to the skin worn at logout
        float const scale = fields[2].IsNull() ? fields[1].Get<float>() : CollectedSkinScale(player, displayId, fields[2].Get<uint32>());
        WornSkin skin{ displayId, scale, fields[2].IsNull() ? 0 : fields[2].Get<uint32>() };
        Worn[guid] = skin;
        ApplyLook(player, skin);
        PutOnDisguise(player, skin.entry);
        wearing = true;
    }
    RefreshWornFx(player, wearing);
    RefreshSkinBuff(player, wearing ? Worn[guid].displayId : 0);
    // logged in on a mount: its aura came back before the worn skin was known, so Steed Shift was skipped
    if (wearing || IsPossessorRace(player->getRace()))
    {
        ObjectGuid const playerGuid = player->GetGUID();
        player->m_Events.AddEventAtOffset([player, playerGuid]()
        {
            if (player->GetGUID() == playerGuid && player->IsInWorld())
                StrideInsteadOfMount(player);
        }, 500ms);
    }
}

std::string SpellLink(SpellInfo const* spellInfo)
{
    return Acore::StringFormat("|cff71d5ff|Hspell:{}|h[{}]|h|r", spellInfo->Id, spellInfo->SpellName[0]);
}
}

// ---- Mimic Strike's echo ------------------------------------------------------------------------------------
// Mimic Strike also fires a weaker copy (half strength, free, on every Mimic Strike) of the last offensive ability you
// stole, so the basic attack changes with what you steal.
constexpr float EchoStrength = 0.5f;  // the Perfect Mimic talent: full strength
std::unordered_map<uint32, uint32> EchoSpell;

// Damage of any kind: spell damage, drains, damage over time and weapon damage (strikes, and creature abilities
// like Sonic Burst, which is weapon percent damage).
bool IsDamageEffect(SpellEffectInfo const& effect)
{
    switch (effect.Effect)
    {
        case SPELL_EFFECT_SCHOOL_DAMAGE:
        case SPELL_EFFECT_HEALTH_LEECH:
        case SPELL_EFFECT_WEAPON_DAMAGE:
        case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
        case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
        case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
            return true;
        case SPELL_EFFECT_APPLY_AURA:
            return effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE || effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH;
        default:
            return false;
    }
}

// Silences, stuns, fears, roots, sleeps and other loss of control.
bool IsCrowdControl(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.Effect != SPELL_EFFECT_APPLY_AURA)
            continue;
        switch (effect.ApplyAuraName)
        {
            case SPELL_AURA_MOD_SILENCE:
            case SPELL_AURA_MOD_PACIFY_SILENCE:
            case SPELL_AURA_MOD_STUN:
            case SPELL_AURA_MOD_FEAR:
            case SPELL_AURA_MOD_ROOT:
            case SPELL_AURA_MOD_CONFUSE:
            case SPELL_AURA_MOD_PACIFY:
            case SPELL_AURA_MOD_CHARM:
            case SPELL_AURA_MOD_POSSESS:
                return true;
            default:
                break;
        }
    }
    return false;
}

bool IsOffensive(SpellInfo const* info)
{
    if (!info || info->IsPositive() || info->IsChanneled() || info->IsPassive())
        return false;
    for (SpellEffectInfo const& effect : info->Effects)
        if (IsDamageEffect(effect))
            return true;
    return false;
}

void LoadEcho(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    EchoSpell.erase(guid);
    if (QueryResult result = CharacterDatabase.Query("SELECT `spell` FROM `skinshifter_grimoire` WHERE `guid` = {} "
        "ORDER BY `learned` DESC", guid))
    {
        do
        {
            uint32 const spellId = LearnedIdOf(result->Fetch()[0].Get<uint32>());  // also before the migration is saved
            if (IsOffensive(sSpellMgr->GetSpellInfo(spellId)))
            {
                EchoSpell[guid] = spellId;
                return;
            }
        } while (result->NextRow());
    }
}

void MimicEcho(Player* player, Unit* target)
{
    uint32 const guid = player->GetGUID().GetCounter();
    auto const echo = EchoSpell.find(guid);
    if (echo == EchoSpell.end() || !target || !target->IsAlive())
        return;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(echo->second);
    if (!info)
        return;
    std::array<int32, MAX_SPELL_EFFECTS> points{};
    std::array<int32 const*, MAX_SPELL_EFFECTS> custom{};
    for (uint8 index = 0; index < MAX_SPELL_EFFECTS; ++index)
    {
        SpellEffectInfo const& effect = info->Effects[index];
        if (!IsDamageEffect(effect))
            continue;
        // A custom amount still gets the level part on top (a Grimoire copy grows with the level), so it is
        // taken off here: the echo is EchoStrength of the whole amount.
        int32 levelPart = 0;
        if (effect.RealPointsPerLevel != 0.0f)
        {
            int32 level = int32(player->GetLevel());
            if (info->MaxLevel && level > int32(info->MaxLevel))
                level = int32(info->MaxLevel);
            level = std::max(level, int32(info->BaseLevel)) - int32(std::max(info->BaseLevel, info->SpellLevel));
            levelPart = int32(level * effect.RealPointsPerLevel);
        }
        float const strength = player->HasSpell(SkinshifterSpells::TalentPerfectMimic) ? 1.0f : EchoStrength;
        points[index] = std::max(1, int32(effect.CalcValue(player) * strength) - levelPart);
        custom[index] = &points[index];
    }
    bool const hadCooldown = player->HasSpellCooldown(info->Id);
    SpellCastResult const result = player->CastCustomSpell(target, info->Id, custom[0], custom[1], custom[2], true);
    // The echo is not your cast: the ability stays ready. The server puts no cooldown on a triggered cast, but the
    // client starts its own when it sees the spell go off without the "triggered" mark (a spell with a cast time,
    // which the echo casts at once), so it is told the ability is ready; a cooldown the echo did start goes.
    if (!hadCooldown)
    {
        if (player->HasSpellCooldown(info->Id))
            player->RemoveSpellCooldown(info->Id, true);  // tells the client too
        else
            player->SendClearCooldown(info->Id, player);
    }
    if (result != SPELL_CAST_OK)
        LOG_INFO("module", "Skinshifter: {}'s Mimic Strike echo of spell {} failed (cast result {}).", player->GetName(),
            info->Id, uint32(result));
}

// A newly stolen ability goes into the first empty button of the main action bar (if there is one).
void PlaceStolenOnBar(Player* player, uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info || info->IsPassive())
        return;
    for (uint8 slot = 0; slot < 12; ++slot)
        if (!player->GetActionButton(slot))
        {
            player->addActionButton(slot, spellId, ACTION_BUTTON_SPELL);
            player->SendActionButtons(1);
            return;
        }
}

bool StealFrom(ChatHandler* handler, Player* player, Creature* creature);

// Skin Collection: wear the skin after the one worn now (or the first), in the order they were stolen.
void WearNextSkin(ChatHandler* handler, Player* player)
{
    if (!Cfg.wearInCombat && player->IsInCombat())
    {
        Refuse(handler, "You cannot change skin in combat.");
        return;
    }
    QueryResult result = CharacterDatabase.Query("SELECT `display_id`, `scale`, `entry` FROM `skinshifter_skin` WHERE "
        "`guid` = {} ORDER BY `learned`, `display_id`", player->GetGUID().GetCounter());
    if (!result)
    {
        Refuse(handler, "Your Skin Collection is empty. Use Steal Form on a creature first.");
        return;
    }
    auto const worn = Worn.find(player->GetGUID().GetCounter());
    uint32 const current = worn == Worn.end() ? 0 : worn->second.displayId;
    std::vector<std::tuple<uint32, float, uint32>> skins;
    do
    {
        Field* fields = result->Fetch();
        skins.emplace_back(fields[0].Get<uint32>(), fields[1].Get<float>(), fields[2].Get<uint32>());
    } while (result->NextRow());
    std::size_t next = 0;
    for (std::size_t index = 0; index < skins.size(); ++index)
        if (std::get<0>(skins[index]) == current)
            next = (index + 1) % skins.size();
    auto const& [displayId, scale, entry] = skins[next];
    Wear(player, WornSkin{ displayId, CollectedSkinScale(player, displayId, entry), entry });
    CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
    handler->PSendSysMessage("You wear the skin of {} ({} of {}).", tmpl ? tmpl->Name : "a creature", next + 1, skins.size());
}

// Possess: steal like Steal Form, then fly into the body and wear it when the travel effect lands.
void PossessBody(ChatHandler* handler, Player* player, Creature* creature)
{
    if (!StealFrom(handler, player, creature))
        return;
    if (!Cfg.wearInCombat && player->IsInCombat())
    {
        handler->SendSysMessage(IsPossessorRace(player->getRace()) ?
            "You cannot take over a body in combat; its form is in your collection." :
            "You cannot change skin in combat; its form is in your collection.");
        return;
    }
    WornSkin const skin{ creature->GetNativeDisplayId(), WithSize(player, creature->GetNativeDisplayId(), SkinScale(creature)),
        creature->GetEntry() };
    uint32 delay = Cfg.fxDelayMin;
    uint32 const travel = FxOf(player).travel;
    if (SpellInfo const* travelInfo = HasSpellData(travel) ? sSpellMgr->GetSpellInfo(travel) : nullptr)
    {
        player->CastSpell(creature, travel, true);
        if (travelInfo->Speed > 0.0f)
            delay = std::max(delay, uint32(player->GetDistance(creature) / travelInfo->Speed * 1000.0f) + 100);
    }
    player->m_Events.AddEventAtOffset([player, skin]() { Wear(player, skin); }, Milliseconds(delay));
}

// ---- Skin Collection window (the SkinCollection addon) ------------------------------------------------------
// Hidden addon whispers to oneself, prefix "SKIN". Client -> server: HELLO, LIST, WEAR;<display id>, SHED.
// Server -> client: BEGIN, S;<display>;<entry>;<level>;<type>;<name>;<family>;<radius>;<size %>;<1: character model>,
// A;<entry>;<spell>, END;<worn>, OPEN, WORN;<worn>. Client -> server also SIZE;<display>;<percent>.
std::unordered_set<uint32> CollectionAddon;  // characters whose client has the window

void SendCollectionLineNow(Player* player, std::string const& line)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, "SKIN\t" + line);
    player->SendDirectMessage(&packet);
}

// A big collection (after .skinshifter unlock: thousands of skins and abilities) and many new spells at once are too
// much for the client in one moment; both go out a little at a time, from the player's update, in order.
constexpr std::size_t CollectionLinesPerUpdate = 400;  // 4,000 lines a second
constexpr std::size_t SpellsLearnedPerUpdate = 5;    // 50 a second
std::unordered_map<uint32, std::deque<std::string>> CollectionQueue;
std::unordered_map<uint32, std::deque<uint32>> LearnQueue;

void SendCollectionLine(Player* player, std::string const& line)
{
    auto queue = CollectionQueue.find(player->GetGUID().GetCounter());
    if (queue == CollectionQueue.end() || queue->second.empty())
    {
        if (line != "BEGIN")
        {
            SendCollectionLineNow(player, line);
            return;
        }
        queue = CollectionQueue.try_emplace(player->GetGUID().GetCounter()).first;
    }
    queue->second.push_back(line);
}

std::unordered_map<uint32, uint32> DrainWait;  // ms since the last batch

void DrainQueues(Player* player, uint32 diff)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if ((DrainWait[guid] += diff) < 100)  // a batch every 100 ms
        return;
    DrainWait[guid] = 0;
    if (auto queue = CollectionQueue.find(guid); queue != CollectionQueue.end())
    {
        for (std::size_t sent = 0; sent < CollectionLinesPerUpdate && !queue->second.empty(); ++sent)
        {
            SendCollectionLineNow(player, queue->second.front());
            queue->second.pop_front();
        }
        if (queue->second.empty())
            CollectionQueue.erase(queue);
    }
    if (auto queue = UnlearnQueue.find(guid); queue != UnlearnQueue.end())
    {
        for (std::size_t removed = 0; removed < SpellsLearnedPerUpdate && !queue->second.empty(); ++removed)
        {
            if (player->HasSpell(queue->second.front()))
                player->removeSpell(queue->second.front(), SPEC_MASK_ALL, false);
            queue->second.pop_front();
        }
        if (queue->second.empty())
            UnlearnQueue.erase(queue);
    }
    if (auto queue = LearnQueue.find(guid); queue != LearnQueue.end())
    {
        for (std::size_t learned = 0; learned < SpellsLearnedPerUpdate && !queue->second.empty(); ++learned)
        {
            if (!player->HasSpell(queue->second.front()))
                player->learnSpell(queue->second.front());
            queue->second.pop_front();
        }
        if (queue->second.empty())
        {
            LearnQueue.erase(queue);
            KeepStrongestByName(player);
            ChatHandler(player->GetSession()).PSendSysMessage("Skinshifter: the unlocked abilities are learned.");
        }
    }
}

std::string Clean(std::string text)
{
    std::replace(text.begin(), text.end(), ';', ',');
    return text;
}

std::unordered_set<uint32> const& WorldSkins();  // the skins of the creatures in the world you play (defined below)

void SendCollection(Player* player, bool open)
{
    uint32 const guid = player->GetGUID().GetCounter();
    if (open)
        SendCollectionLineNow(player, "OPEN");  // the window at once; the list fills it as it comes
    CollectionQueue.erase(guid);  // a list still on its way is replaced, not sent twice
    CollectionKnown.insert(guid);
    SendCollectionLine(player, "BEGIN");
    if (QueryResult result = CharacterDatabase.Query("SELECT s.`display_id`, s.`entry`, IFNULL(z.`percent`, 100) FROM "
        "`skinshifter_skin` s LEFT JOIN `skinshifter_skin_size` z ON z.`guid` = s.`guid` AND z.`display_id` = s.`display_id` "
        "WHERE s.`guid` = {} ORDER BY s.`learned`, s.`display_id`", guid))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const entry = fields[1].Get<uint32>();
            CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
            uint32 const displayId = fields[0].Get<uint32>();
            CreatureModelInfo const* model = sObjectMgr->GetCreatureModelInfo(displayId);
            // the model's radius lets the preview step back far enough for a dragon and close enough for a rat
            // a character-model NPC (a display with extra info: race, face, worn items) shows white in the window's
            // model by its display; by its creature entry the client builds it as in the world
            CreatureDisplayInfoEntry const* display = sCreatureDisplayInfoStore.LookupEntry(displayId);
            SendCollectionLine(player, Acore::StringFormat("S;{};{};{};{};{};{};{:.2f};{};{};{}", displayId, entry,
                uint32(tmpl ? tmpl->maxlevel : 0), uint32(tmpl ? tmpl->type : 0), Clean(tmpl ? tmpl->Name : "Unknown creature"),
                uint32(tmpl ? tmpl->family : 0), model ? model->bounding_radius : 0.0f, fields[2].Get<uint64>(),
                display && display->ExtendedDisplayInfoID ? 1 : 0, WorldSkins().count(displayId) ? 1 : 0));
        } while (result->NextRow());
    }
    if (QueryResult result = CharacterDatabase.Query("SELECT `entry`, `spell` FROM `skinshifter_grimoire` WHERE `guid` = {}", guid))
    {
        do
        {
            Field* fields = result->Fetch();
            SendCollectionLine(player, Acore::StringFormat("A;{};{}", fields[0].Get<uint32>(),
                LearnedIdOf(fields[1].Get<uint32>())));
        } while (result->NextRow());
    }
    auto const worn = Worn.find(guid);
    SendCollectionLine(player, Acore::StringFormat("MAX;{}", WorldSkins().size()));
    SendCollectionLine(player, Acore::StringFormat("END;{}", worn == Worn.end() ? 0 : worn->second.displayId));
}

void LearnSkinAbilities(Player* player, uint32 entry);

// After wearing or shedding from the window only the worn skin changes: WORN;<display> (0: your own form), not the
// whole collection again (thousands of lines after .skinshifter unlock).
void SendWorn(Player* player)
{
    auto const worn = Worn.find(player->GetGUID().GetCounter());
    SendCollectionLine(player, Acore::StringFormat("WORN;{}", worn == Worn.end() ? 0 : worn->second.displayId));
}

void WearFromCollection(Player* player, uint32 displayId)
{
    ChatHandler handler(player->GetSession());
    if (!Cfg.wearInCombat && player->IsInCombat())
    {
        Refuse(&handler, "You cannot change skin in combat.");
        return;
    }
    if (player->IsMounted() || player->GetShapeshiftForm() != FORM_NONE)
    {
        Refuse(&handler, "Dismount and leave your form first.");
        return;
    }
    QueryResult result = CharacterDatabase.Query("SELECT `entry` FROM `skinshifter_skin` WHERE `guid` = {} AND "
        "`display_id` = {}", player->GetGUID().GetCounter(), displayId);
    if (!result)
        return;
    uint32 const entry = result->Fetch()[0].Get<uint32>();
    Wear(player, WornSkin{ displayId, CollectedSkinScale(player, displayId, entry), entry });
    LearnSkinAbilities(player, entry);
}

// SIZE;<display>;<percent>: your own size for a skin (the Skin Collection's slider); the skin you wear changes at once.
void SetSkinSize(Player* player, std::string_view args)
{
    std::vector<std::string_view> const parts = Acore::Tokenize(args, ';', false);
    if (parts.size() != 2)
        return;
    Optional<uint32> const displayId = Acore::StringTo<uint32>(parts[0]);
    Optional<uint32> const percent = Acore::StringTo<uint32>(parts[1]);
    uint32 const guid = player->GetGUID().GetCounter();
    if (!displayId || !percent)
        return;
    QueryResult skin = CharacterDatabase.Query("SELECT `entry` FROM `skinshifter_skin` WHERE `guid` = {} AND `display_id` = {}",
        guid, *displayId);
    if (!skin)
        return;
    uint32 const size = std::clamp<uint32>(*percent, SizeMin, SizeMax);
    CharacterDatabase.DirectExecute("REPLACE INTO `skinshifter_skin_size` (`guid`, `display_id`, `percent`) VALUES ({}, {}, {})",
        guid, *displayId, size);
    auto worn = Worn.find(guid);
    if (worn == Worn.end() || worn->second.displayId != *displayId || TemporaryLook.count(guid))
        return;
    worn->second.scale = CollectedSkinScale(player, *displayId, skin->Fetch()[0].Get<uint32>());
    ApplyLook(player, worn->second);
    CharacterDatabase.Execute("REPLACE INTO `skinshifter_worn` (`guid`, `display_id`, `scale`) VALUES ({}, {}, {})",
        guid, *displayId, worn->second.scale);
}

// Returns true when the message was for the Skin Collection (and is not sent on as a whisper).
bool HandleCollectionMessage(Player* player, std::string const& message)
{
    std::string_view const prefix = "SKIN\t";
    if (message.compare(0, prefix.size(), prefix) != 0)
        return false;
    std::string_view const body = std::string_view(message).substr(prefix.size());
    uint32 const guid = player->GetGUID().GetCounter();
    if (SkinshifterSpells::HandleSpecMessage(player, body))
        return true;
    if (SkinshifterSpells::HandleSlotMessage(player, body))
        return true;
    if (SkinshifterSpells::HandleTokenMessage(player, body))
        return true;
    if (SkinshifterSpells::HandleSpellTextMessage(player, body))
        return true;
    if (body == "REINS" || body == "REINS?")
    {
        HandleReins(player, body == "REINS?");
        return true;
    }
    if (body == "HELLO")
    {
        CollectionAddon.insert(guid);
        CollectionKnown.erase(guid);  // a new load of the addon holds nothing yet
    }
    else if (body == "LIST")
    {
        if (!CollectionKnown.count(guid))  // the window asks every time it opens; unchanged, nothing is sent
            SendCollection(player, false);
    }
    else if (body == "SHED")
    {
        Shed(player);
        SendWorn(player);
    }
    else if (body.substr(0, 5) == "SIZE;")
        SetSkinSize(player, body.substr(5));
    else if (body.substr(0, 5) == "WEAR;")
    {
        if (Optional<uint32> displayId = Acore::StringTo<uint32>(body.substr(5)))
            WearFromCollection(player, *displayId);
        SendWorn(player);
    }
    return true;
}

// The creature Steal Form or Possess is aimed at. Town NPCs, quest givers and the like cannot be helped by players
// (UNIT_FLAG_IMMUNE_TO_PC), so the client casts these on yourself instead; then the creature you have selected counts.
Creature* AimedCreature(Player* player, Unit* target)
{
    if (target && target != player)
        return target->ToCreature();
    return ObjectAccessor::GetCreature(*player, player->GetTarget());
}

Creature* AimedCreature(Player* player, Spell* spell)
{
    return AimedCreature(player, spell->m_targets.GetUnitTarget());
}

// Hydra Head and Cerberus (talent abilities): three heads strike a moment apart, Hydra's each a random bolt,
// Cerberus's Physical, Fire and Shadow in turn.
void CastHeads(Player* player, Unit* target, uint32 spellId)
{
    if (!target)
        return;
    ObjectGuid const targetGuid = target->GetGUID();
    std::array<uint32, 3> const hydra = { SkinshifterSpells::HydraPoison, SkinshifterSpells::HydraArcane,
        SkinshifterSpells::HydraSand };
    std::array<uint32, 3> const cerberus = { SkinshifterSpells::CerberusBite, SkinshifterSpells::CerberusFire,
        SkinshifterSpells::CerberusShadow };
    for (uint32 head = 0; head < 3; ++head)
    {
        uint32 const strike = spellId == SkinshifterSpells::HydraHead ? hydra[urand(0, 2)] : cerberus[head];
        player->m_Events.AddEventAtOffset([player, targetGuid, strike]()
        {
            Unit* victim = ObjectAccessor::GetUnit(*player, targetGuid);
            if (player->IsInWorld() && player->IsAlive() && victim && victim->IsAlive() &&
                player->IsWithinDistInMap(victim, 45.0f))
                player->CastSpell(victim, strike, true);
        }, Milliseconds(head * 300));
    }
}

bool HandleOwnSpell(Player* player, Spell* spell, uint32 spellId)
{
    ChatHandler handler(player->GetSession());
    switch (spellId)
    {
        case SkinshifterSpells::StealForm:
            if (Creature* creature = AimedCreature(player, spell))
            {
                if (Cfg.stealWears)  // the skin goes on at once (Skinshifter.StealForm.WearAtOnce)
                    PossessBody(&handler, player, creature);
                else if (StealFrom(&handler, player, creature) && HasSpellData(FxOf(player).travel))
                    player->CastSpell(creature, FxOf(player).travel, true);
            }
            else
                Refuse(&handler, "Steal Form only works on creatures and NPCs.");
            return true;
        case SkinshifterSpells::Possess:
            if (Creature* creature = AimedCreature(player, spell))
                PossessBody(&handler, player, creature);
            else
                Refuse(&handler, "Possess only works on creatures and NPCs.");
            return true;
        case SkinshifterSpells::ShedSkin:
        case SkinshifterSpells::Release:
            Shed(player);
            if (player->HasSpell(SkinshifterSpells::TalentCleanGetaway))  // talent: the old skin takes the snares
                player->RemoveMovementImpairingAuras(true);
            handler.SendSysMessage(spellId == SkinshifterSpells::ShedSkin ? "You shed your skin." :
                "You release the body.");
            return true;
        case SkinshifterSpells::RootWalk:
            ToggleTravel(&handler, player, SkinshifterSpells::RootWalking);
            return true;
        case SkinshifterSpells::WraithRun:
            ToggleTravel(&handler, player, SkinshifterSpells::WraithRunning);
            return true;
        case SkinshifterSpells::PhaseRun:
            ToggleTravel(&handler, player, SkinshifterSpells::PhaseRunning);
            return true;
        case SkinshifterSpells::SkinCollection:
            if (CollectionKnown.count(player->GetGUID().GetCounter()))
                SendCollectionLineNow(player, "OPEN");  // the addon has the list: thousands of lines are not sent again
            else if (CollectionAddon.count(player->GetGUID().GetCounter()))
                SendCollection(player, true);
            else
                WearNextSkin(&handler, player);  // a client without the SkinCollection addon
            return true;
        case SkinshifterSpells::HydraHead:
        case SkinshifterSpells::Cerberus:
            CastHeads(player, spell->m_targets.GetUnitTarget(), spellId);
            return true;
        case SkinshifterSpells::Turnskin:
        case SkinshifterSpells::KindredSkin:
        case SkinshifterSpells::HostileTakeover:
        case SkinshifterSpells::WillingHost:  // SkinshifterTakeover.cpp
            return SkinshifterSpells::HandleTakeoverSpell(player, AimedCreature(player, spell), spellId);
        default:
            return false;
    }
}

bool StealFrom(ChatHandler* handler, Player* player, Creature* creature)
{
    std::string const refusal = LiftRefusal(player, creature);
    if (!refusal.empty())
        return Refuse(handler, refusal);

    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const now = uint32(GameTime::GetGameTime().count());
    uint32 const displayId = creature->GetNativeDisplayId();
    float const scale = SkinScale(creature);

    bool const newSkin = !CharacterDatabase.Query(
        "SELECT 1 FROM `skinshifter_skin` WHERE `guid` = {} AND `display_id` = {}", guid, displayId);
    if (newSkin)
    {
        CharacterDatabase.Execute("INSERT INTO `skinshifter_skin` (`guid`, `display_id`, `entry`, `scale`, `learned`) "
            "VALUES ({}, {}, {}, {}, {})", guid, displayId, creature->GetEntry(), scale, now);
        CollectionKnown.erase(guid);  // the addon's list is out of date
    }

    auto& grimoire = Grimoires[guid];
    uint32 learned = 0;
    uint32 known = 0;
    bool echoSet = false;
    for (uint32 creatureSpell : LiftableSpellsOf(creature))
    {
        uint32 const spellId = LearnedIdOf(creatureSpell);
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!echoSet && IsOffensive(spellInfo))
        {
            EchoSpell[guid] = spellId;
            echoSet = true;
        }
        if (player->HasSpell(spellId))
        {
            ++known;
            continue;
        }
        bool const weaker = WeakerThanKnown(player, spellInfo);
        // a stronger one of an ability you know replaces it: its bar buttons move over (KeepStrongestByName)
        bool upgrade = false;
        if (!weaker)
            for (uint32 other : grimoire)
                if (other != spellId && player->HasSpell(other))
                    if (SpellInfo const* known = sSpellMgr->GetSpellInfo(other))
                        if (NameOf(known) == NameOf(spellInfo))
                        {
                            upgrade = true;
                            break;
                        }
        grimoire.insert(spellId);
        if (!weaker)
        {
            player->learnSpell(spellId);
            if (!upgrade)
                PlaceStolenOnBar(player, spellId);
            KeepStrongestByName(player);
        }
        CharacterDatabase.Execute("REPLACE INTO `skinshifter_grimoire` (`guid`, `spell`, `entry`, `learned`) "
            "VALUES ({}, {}, {}, {})", guid, spellId, creature->GetEntry(), now);
        CollectionKnown.erase(guid);  // the addon's list is out of date
        handler->PSendSysMessage(weaker ? "Stole {} from {} (into your Grimoire: you know a stronger one)." :
            upgrade ? "Upgraded {}: a stronger one from {} replaces yours (your bar buttons now use it)." :
            "Stole {} from {}.", SpellLink(spellInfo), creature->GetName());
        ++learned;
    }

    handler->PSendSysMessage("{} ({} {}, model {}): {} new {}, {} already known. Skin {}.",
        creature->GetName(), TypeName(creature->GetCreatureTemplate()->type), creature->GetLevel(), displayId,
        learned, learned == 1 ? "ability" : "abilities", known, newSkin ? "added to your collection" : "already collected");
    return true;
}

// ---- GM: .skinshifter unlock --------------------------------------------------------------------------------
// Every creature that stands somewhere in the world (a spawn in `creature`), as if stolen from each: its skin goes
// into the Skin Collection and its abilities into the Grimoire. The client's spellbook holds about 2,000 entries,
// so at most Skinshifter.Unlock.LearnLimit of the abilities are learned at once (the highest creature levels
// first); the rest are learned when you wear their creature's skin.
struct UnlockTally
{
    uint32 creatures = 0;
    uint32 skins = 0;
    uint32 abilities = 0;
};

struct UnlockPlan
{
    std::map<uint32, std::pair<uint32, float>> skins;           // display -> creature entry, scale
    std::map<uint32, std::pair<uint32, uint32>> abilities;      // learned spell -> creature entry, creature level
    std::map<uint32, UnlockTally> byType;
    UnlockTally total;
    uint32 withoutCopy = 0;  // creature spells with no Grimoire copy (learned as they are, not in the spellbook tabs)
};

bool IsUnlockable(CreatureTemplate const* tmpl)
{
    if (!tmpl || tmpl->RacialLeader || (tmpl->flags_extra & CREATURE_FLAG_EXTRA_TRIGGER))
        return false;
    if (tmpl->type == CREATURE_TYPE_CRITTER || tmpl->type == CREATURE_TYPE_TOTEM ||
        tmpl->type == CREATURE_TYPE_NON_COMBAT_PET || tmpl->type == CREATURE_TYPE_GAS_CLOUD)
        return false;
    std::string name = tmpl->Name;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    for (char const* junk : { "trigger", "bunny", "[dnd]", "dnd ", "invisible", "[ph]", "(test)", "zzold", "stalker" })
        if (name.find(junk) != std::string::npos)
            return false;
    return true;
}

UnlockPlan PlanUnlock()
{
    std::set<uint32> spawned;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        spawned.insert(data.id);
    UnlockPlan plan;
    for (uint32 entry : spawned)
    {
        CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
        if (!IsUnlockable(tmpl))
            continue;
        CreatureModel const* model = tmpl->GetFirstValidModel();
        if (!model || !model->CreatureDisplayID)
            continue;
        UnlockTally& tally = plan.byType[tmpl->type];
        ++tally.creatures;
        ++plan.total.creatures;
        uint32 const display = model->CreatureDisplayID;
        if (!plan.skins.count(display))
        {
            plan.skins[display] = { entry, SkinScaleFor(display, model->DisplayScale) };
            ++tally.skins;
            ++plan.total.skins;
        }
        for (uint32 creatureSpell : LiftableSpellsOf(tmpl, tmpl->maxlevel))
        {
            uint32 const spellId = LearnedIdOf(creatureSpell);
            if (plan.abilities.count(spellId) || !sSpellMgr->GetSpellInfo(spellId))
                continue;
            plan.abilities[spellId] = { entry, tmpl->maxlevel };
            ++tally.abilities;
            ++plan.total.abilities;
            if (spellId == creatureSpell)
                ++plan.withoutCopy;
        }
    }
    return plan;
}

// The skins (displays) of the creatures spawned on the maps of this game's world: the original continents and
// instances. Outland and Northrend creatures are in the database but cannot be reached; their skins are extra.
std::unordered_set<uint32> const& WorldSkins()
{
    static std::unordered_set<uint32> skins;
    static bool built = false;
    if (built)
        return skins;
    built = true;
    std::set<uint32> spawned;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        if (MapEntry const* map = sMapStore.LookupEntry(data.mapid); map && map->Expansion() == 0)
            spawned.insert(data.id);
    for (uint32 entry : spawned)
    {
        CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
        if (!IsUnlockable(tmpl))
            continue;
        if (CreatureModel const* model = tmpl->GetFirstValidModel(); model && model->CreatureDisplayID)
            skins.insert(model->CreatureDisplayID);
    }
    LOG_INFO("module", "Skinshifter: {} skins to collect in this world.", skins.size());
    return skins;
}

void ReportUnlock(ChatHandler* handler, UnlockPlan const& plan)
{
    handler->PSendSysMessage("Creatures in the world that can be stolen from: {}; their skins (models): {}; their "
        "abilities: {} ({} with a Grimoire copy, {} creature spells without one).", plan.total.creatures,
        plan.total.skins, plan.total.abilities, plan.total.abilities - plan.withoutCopy, plan.withoutCopy);
    for (auto const& [type, tally] : plan.byType)
        handler->PSendSysMessage("  {}: {} creatures, {} skins, {} abilities", TypeName(type), tally.creatures,
            tally.skins, tally.abilities);
}

uint32 UnlockLearnLimit()
{
    return sConfigMgr->GetOption<uint32>("Skinshifter.Unlock.LearnLimit", 300);
}

// The skin's own abilities that are in your Grimoire but not learned yet (after .skinshifter unlock): learned as you
// put the skin on.
void LearnSkinAbilities(Player* player, uint32 entry)
{
    CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
    auto const grimoire = Grimoires.find(player->GetGUID().GetCounter());
    if (!tmpl || grimoire == Grimoires.end())
        return;
    ChatHandler handler(player->GetSession());
    for (uint32 creatureSpell : LiftableSpellsOf(tmpl, tmpl->maxlevel))
    {
        uint32 const spellId = LearnedIdOf(creatureSpell);
        if (grimoire->second.count(spellId) && !player->HasSpell(spellId) && sSpellMgr->GetSpellInfo(spellId) &&
            !WeakerThanKnown(player, sSpellMgr->GetSpellInfo(spellId)))
        {
            player->learnSpell(spellId);
            handler.PSendSysMessage("From your Grimoire: {}.", SpellLink(sSpellMgr->GetSpellInfo(spellId)));
        }
    }
    KeepStrongestByName(player);
}

constexpr std::size_t UnlockBatch = 500;  // rows per INSERT

bool UnlockAll(ChatHandler* handler, Player* player)
{
    UnlockPlan const plan = PlanUnlock();
    uint32 const guid = player->GetGUID().GetCounter();
    uint32 const now = uint32(GameTime::GetGameTime().count());

    std::unordered_set<uint32> ownedSkins;  // for the totals in the report
    if (QueryResult owned = CharacterDatabase.Query("SELECT `display_id` FROM `skinshifter_skin` WHERE `guid` = {}", guid))
        do ownedSkins.insert(owned->Fetch()[0].Get<uint32>()); while (owned->NextRow());
    for (auto const& [display, skin] : plan.skins)
        ownedSkins.insert(display);
    std::vector<std::string> skinRows;
    for (auto const& [display, skin] : plan.skins)
        skinRows.push_back(Acore::StringFormat("({}, {}, {}, {}, {})", guid, display, skin.first, skin.second, now));
    std::vector<std::string> grimoireRows;
    for (auto const& [spellId, from] : plan.abilities)
        grimoireRows.push_back(Acore::StringFormat("({}, {}, {}, {})", guid, spellId, from.first, now));
    auto const insert = [](std::string const& head, std::vector<std::string> const& rows)  // 500 rows a statement
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        for (std::size_t first = 0; first < rows.size(); first += UnlockBatch)
        {
            std::string values;
            for (std::size_t index = first; index < std::min(rows.size(), first + UnlockBatch); ++index)
                values += (values.empty() ? "" : ", ") + rows[index];
            trans->Append(head + values);
        }
        CharacterDatabase.CommitTransaction(trans);
    };
    insert("INSERT IGNORE INTO `skinshifter_skin` (`guid`, `display_id`, `entry`, `scale`, `learned`) VALUES ", skinRows);
    insert("INSERT IGNORE INTO `skinshifter_grimoire` (`guid`, `spell`, `entry`, `learned`) VALUES ", grimoireRows);

    // Learned now: the abilities of the highest-level creatures first, up to the limit.
    auto& grimoire = Grimoires[guid];
    std::vector<std::pair<uint32, uint32>> order;  // level, spell
    for (auto const& [spellId, from] : plan.abilities)
    {
        grimoire.insert(spellId);
        if (!player->HasSpell(spellId))
            order.emplace_back(from.second, spellId);
    }
    std::sort(order.begin(), order.end(), std::greater<>());
    // one of a name: the strongest (the book keeps no copies)
    std::unordered_map<std::string, uint32> strongest;
    for (auto const& [level, spellId] : order)
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
        {
            uint32& kept = strongest[NameOf(info)];
            if (!kept || StrengthOf(info) > StrengthOf(sSpellMgr->GetSpellInfo(kept)))
                kept = spellId;
        }
    std::erase_if(order, [&](auto const& item)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(item.second);
        return !info || strongest[NameOf(info)] != item.second || WeakerThanKnown(player, info);
    });
    uint32 const limit = UnlockLearnLimit();
    uint32 learned = 0;
    // Learned on the server only, all at once, and saved: told one by one, the client redraws its spellbook for
    // every spell and freezes over thousands. The next login sends the whole book in one list.
    for (auto const& [level, spellId] : order)
    {
        if (learned >= limit)
            break;
        if (!player->HasSpell(spellId) && player->addSpell(spellId, SPEC_MASK_ALL, true))
            ++learned;
    }
    if (learned)
    {
        KeepStrongestByName(player);  // a weaker copy already in the book goes, a few at a time
        player->SaveToDB(false, false);
    }

    ReportUnlock(handler, plan);
    handler->PSendSysMessage("Unlocked: every skin is in your Skin Collection and every ability in your Grimoire. "
        "Learned now: {} (Skinshifter.Unlock.LearnLimit {}; the strongest of each name) -- log out and in again to "
        "see them in your spellbook. {} more are learned when you wear their creature's skin. "
        ".skin forget all takes it all back.", learned, limit, order.size() > learned ? order.size() - learned : 0);
    uint32 inBook = 0;
    for (auto const& [spellId, state] : player->GetSpellMap())
        if (state->State != PLAYERSPELL_REMOVED && SkinshifterSpells::OriginalOf(spellId))
            ++inBook;
    handler->PSendSysMessage("You now have {} skins, {} abilities in your Grimoire and {} stolen abilities in your "
        "spellbook.", ownedSkins.size(), grimoire.size(), inBook);
    LOG_INFO("module", "Skinshifter: {} unlocked {} skins and {} abilities ({} learned now). Totals: {} skins, {} in "
        "the Grimoire, {} stolen abilities in the spellbook.", player->GetName(), plan.total.skins, plan.total.abilities,
        learned, ownedSkins.size(), grimoire.size(), inBook);
    if (CollectionAddon.count(guid))
        SendCollection(player, false);
    return true;
}

class SkinshifterCommandScript : public CommandScript
{
public:
    SkinshifterCommandScript() : CommandScript("SkinshifterCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable unlockTable =
        {
            { "allskins", HandleUnlockAll,   SEC_GAMEMASTER, Console::No },
            { "count",    HandleUnlockCount, SEC_GAMEMASTER, Console::No },
        };
        static ChatCommandTable skinTable =
        {
            { "steal",    HandleLift,     SEC_PLAYER, Console::No },
            { "lift",     HandleLift,     SEC_PLAYER, Console::No },
            { "list",     HandleList,     SEC_PLAYER, Console::No },
            { "wear",     HandleWear,     SEC_PLAYER, Console::No },
            { "shed",     HandleShed,     SEC_PLAYER, Console::No },
            { "grimoire", HandleGrimoire, SEC_PLAYER, Console::No },
            { "forget",   HandleForget,   SEC_PLAYER, Console::No },
            { "fx",       HandleFx,       SEC_PLAYER, Console::No },
            { "tokens",   HandleTokens,   SEC_GAMEMASTER, Console::No },
            { "soultokens", HandleTokens, SEC_GAMEMASTER, Console::No },
            { "soultoken",  HandleTokens, SEC_GAMEMASTER, Console::No },
            { "skintokens", HandleTokens, SEC_GAMEMASTER, Console::No },
            { "unlock",   unlockTable },
            { "leave",    HandleLeave,    SEC_PLAYER, Console::No },
            { "",         HandleHelp,     SEC_PLAYER, Console::No },
        };
        static ChatCommandTable commandTable =
        {
            { "skin", skinTable },
            { "skinshifter", skinTable },
        };
        return commandTable;
    }

    // GM: set your token count, to try the counter (new tokens keep your target creature, if any).
    static bool HandleTokens(ChatHandler* handler, Optional<uint32> count)  // no number: full
    {
        return SkinshifterSpells::HandleTokensCommand(handler, count.value_or(1000000));
    }

    // GM: every skin and every ability there is (.skinshifter unlock allskins), or only how many (count).
    static bool HandleUnlockAll(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!MayUse(handler, player))
            return false;
        return UnlockAll(handler, player);
    }

    // Step out of the creature you have taken over (as the possess bar's cancel button).
    static bool HandleLeave(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player || !SkinshifterSpells::HandleTakeoverCancel(player, SkinshifterSpells::TakeoverControl))
            return Refuse(handler, "You are not inside a creature.");
        return true;
    }

    static bool HandleUnlockCount(ChatHandler* handler)
    {
        ReportUnlock(handler, PlanUnlock());
        return true;
    }

    static bool HandleHelp(ChatHandler* handler)
    {
        handler->SendSysMessage("Skinshifter: .skin steal (target) | list | wear [number, or target] | shed | grimoire | forget <all|spellId> | leave (a takeover)");
        return true;
    }

    static bool HandleLift(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!MayUse(handler, player))
            return false;
        Creature* creature = handler->getSelectedCreature();
        if (!creature)
            return Refuse(handler, "Target a creature to steal from.");
        return StealFrom(handler, player, creature);
    }

    static bool HandleList(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!MayUse(handler, player))
            return false;
        QueryResult result = CharacterDatabase.Query("SELECT `display_id`, `entry` FROM `skinshifter_skin` WHERE `guid` = {} "
            "ORDER BY `learned`, `display_id`", player->GetGUID().GetCounter());
        if (!result)
        {
            handler->SendSysMessage("Your Skin Collection is empty. Target a creature and use .skin steal");
            return true;
        }
        uint32 index = 0;
        handler->SendSysMessage("Skin Collection:");
        do
        {
            Field* fields = result->Fetch();
            CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(fields[1].Get<uint32>());
            handler->PSendSysMessage("  {}. {} ({}, model {})", ++index, tmpl ? tmpl->Name : "?",
                tmpl ? TypeName(tmpl->type) : "?", fields[0].Get<uint32>());
        } while (result->NextRow());
        return true;
    }

    static bool HandleWear(ChatHandler* handler, Optional<uint32> number)
    {
        Player* player = handler->GetPlayer();
        if (!MayUse(handler, player))
            return false;
        if (!Cfg.wearInCombat && player->IsInCombat())
            return Refuse(handler, "You cannot change skin in combat.");
        if (player->IsMounted() || player->GetShapeshiftForm() != FORM_NONE)
            return Refuse(handler, "Dismount and leave your form first.");

        uint32 const guid = player->GetGUID().GetCounter();
        QueryResult result;
        if (number && *number > 0)
            result = CharacterDatabase.Query("SELECT `display_id`, `scale`, `entry` FROM `skinshifter_skin` WHERE `guid` = {} "
                "ORDER BY `learned`, `display_id` LIMIT 1 OFFSET {}", guid, *number - 1);
        else if (Creature* creature = handler->getSelectedCreature())
        {
            result = CharacterDatabase.Query("SELECT `display_id`, `scale`, `entry` FROM `skinshifter_skin` WHERE `guid` = {} "
                "AND `display_id` = {}", guid, creature->GetNativeDisplayId());
            if (!result)
                return Refuse(handler, "You have not lifted that skin yet. Use .skin steal on it first.");
        }
        else
        {
            handler->SendSysMessage("Use .skin wear <number> from your collection, or target a creature you have lifted.");
            return HandleList(handler);
        }

        if (!result)
            return Refuse(handler, "You have no skin with that number. See .skin list");
        Field* fields = result->Fetch();
        Wear(player, WornSkin{ fields[0].Get<uint32>(), CollectedSkinScale(player, fields[0].Get<uint32>(), fields[2].Get<uint32>()),
            fields[2].Get<uint32>() });
        CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(fields[2].Get<uint32>());
        handler->PSendSysMessage("You wear the skin of {}.", tmpl ? tmpl->Name : "a creature");
        return true;
    }

    static bool HandleShed(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        Shed(player);
        handler->SendSysMessage("You shed your skin.");
        return true;
    }

    // .skin fx: list the effect options for your style; .skin fx <n>: pick one (saved).
    static bool HandleFx(ChatHandler* handler, Optional<uint32> number)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!MayUse(handler, player))
            return false;
        FxStyle const style = StyleOf(player);
        std::vector<FxOption> const& options = OptionsOf(style);
        uint32 const guid = player->GetGUID().GetCounter();
        if (number)
        {
            if (*number < 1 || *number > options.size())
                return Refuse(handler, Acore::StringFormat("Pick a number from 1 to {}.", options.size()));
            FxChoice[guid] = uint8(*number - 1);
            CharacterDatabase.Execute("REPLACE INTO `skinshifter_fx` (`guid`, `choice`) VALUES ({}, {})", guid, *number - 1);
            RefreshWornFx(player, Worn.count(guid) > 0);
            Pulse(player);
        }
        FxOption const& current = FxOf(player);
        handler->PSendSysMessage("Effects for {} (.skin fx <n> to pick):", StyleName(style));
        for (std::size_t index = 0; index < options.size(); ++index)
            handler->PSendSysMessage("  {}{}. {}", &options[index] == &current ? "> " : "  ", index + 1, options[index].name);
        return true;
    }

    static bool HandleGrimoire(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!MayUse(handler, player))
            return false;
        QueryResult result = CharacterDatabase.Query("SELECT `spell`, `entry` FROM `skinshifter_grimoire` WHERE `guid` = {} "
            "ORDER BY `learned`, `spell`", player->GetGUID().GetCounter());
        if (!result)
        {
            handler->SendSysMessage("Your Grimoire is empty.");
            return true;
        }
        handler->SendSysMessage("Grimoire:");
        do
        {
            Field* fields = result->Fetch();
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(LearnedIdOf(fields[0].Get<uint32>()));
            CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(fields[1].Get<uint32>());
            if (spellInfo)
                handler->PSendSysMessage("  {} - from {}", SpellLink(spellInfo), tmpl ? tmpl->Name : "?");
        } while (result->NextRow());
        return true;
    }

    static bool HandleForget(ChatHandler* handler, Optional<std::string> whatArg)
    {
        Player* player = handler->GetPlayer();
        uint32 const guid = player->GetGUID().GetCounter();
        std::string const what = whatArg.value_or("");
        auto& grimoire = Grimoires[guid];
        std::vector<uint32> toForget;
        if (Lower(what) == "all")
            toForget.assign(grimoire.begin(), grimoire.end());
        else if (Optional<uint32> spellId = Acore::StringTo<uint32>(what); spellId && grimoire.count(*spellId))
            toForget.push_back(*spellId);
        else
        {
            return Refuse(handler, "Use .skin forget all, or .skin forget <spellId> from your Grimoire.");
        }
        for (uint32 spellId : toForget)
        {
            player->removeSpell(spellId, SPEC_MASK_ALL, false);
            grimoire.erase(spellId);
            CharacterDatabase.Execute("DELETE FROM `skinshifter_grimoire` WHERE `guid` = {} AND `spell` = {}", guid, spellId);
            CollectionKnown.erase(guid);  // the addon's list is out of date
        }
        if (Lower(what) == "all")
        {
            CharacterDatabase.Execute("DELETE FROM `skinshifter_skin` WHERE `guid` = {}", guid);
            CollectionKnown.erase(guid);  // the addon's list is out of date
            CharacterDatabase.Execute("DELETE FROM `skinshifter_skin_size` WHERE `guid` = {}", guid);
            Shed(player);
        }
        handler->PSendSysMessage("Forgot {} {}.", toForget.size(), toForget.size() == 1 ? "ability" : "abilities");
        return true;
    }
};

// A creature's summon spell often has the summoned creature attack whoever summoned it (made for a creature that calls
// adds against a player). Ours fight for the caller: the old fight is dropped, and every second it is checked again,
// while the summon lives, that it attacks the caller's enemy and never the caller or the caller's friends.
void KeepSummonOnSide(TempSummon* summon, ObjectGuid owner, ObjectGuid self)
{
    if (summon->GetGUID() != self || !summon->IsInWorld() || !summon->IsAlive())
        return;
    Player* caller = ObjectAccessor::GetPlayer(*summon, owner);
    if (!caller)
        return;
    Unit* current = summon->GetVictim();
    // a fight of its own choosing is wrong too: only what you fight, or what is fighting you or it
    bool const ours = current && (current == caller->GetVictim() || current->GetVictim() == caller ||
        current->GetVictim() == summon || current->IsInCombatWith(caller));
    bool const wrong = current && (current == caller || !summon->IsValidAttackTarget(current) ||
        caller->IsFriendlyTo(current) || !ours);
    if (wrong || !current)
    {
        if (wrong)
        {
            summon->AttackStop();
            summon->CombatStop(true);
            summon->GetThreatMgr().ClearAllThreat();
        }
        if (Unit* victim = caller->GetVictim(); victim && summon->IsValidAttackTarget(victim) && summon->AI())
            summon->AI()->AttackStart(victim);
        else if (summon->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            summon->GetMotionMaster()->MoveFollow(caller, PET_FOLLOW_DIST, frand(0.0f, 2.0f * float(M_PI)));
    }
    summon->m_Events.AddEventAtOffset([summon, owner, self]() { KeepSummonOnSide(summon, owner, self); }, 1s);
}

class SkinshifterPlayerScript : public PlayerScript
{
public:
    SkinshifterPlayerScript() : PlayerScript("SkinshifterPlayerScript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_DELETE, PLAYERHOOK_ON_SPELL_CAST,
          PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_LEARN_SPELL, PLAYERHOOK_ON_FORGOT_SPELL,
          PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_BEFORE_TEMP_SUMMON_INIT_STATS }) { }

    // What a Skinshifter summons is on the Skinshifter's side. A stolen summon (Zombie Horde, Raise Dead) is the
    // creature's own spell, made for the creature: its summons take a monster faction (fixed in the summon's
    // properties) and turn on the one who called them. They take your faction instead, fight what you fight and
    // follow you. Set a moment later: the summon's own setup (InitStats) gives it its faction after this hook.
    void OnPlayerBeforeTempSummonInitStats(Player* player, TempSummon* summon, uint32& /*duration*/) override
    {
        if (!Cfg.enabled || player->getClass() != SkinshifterSpells::ClassId || summon->IsPet())
            return;
        ObjectGuid const owner = player->GetGUID();
        ObjectGuid const self = summon->GetGUID();
        summon->m_Events.AddEventAtOffset([summon, owner, self]()
        {
            if (summon->GetGUID() != self || !summon->IsInWorld() || !summon->IsAlive())
                return;
            Player* caller = ObjectAccessor::GetPlayer(*summon, owner);
            if (!caller)
                return;
            summon->SetFaction(caller->GetFaction());
            summon->SetCreatorGUID(caller->GetGUID());
            if (summon->IsPet())  // a real pet has its own bar and stances
                return;
            // its kills are the caller's: experience, loot, quest credit and tokens (a guardian has its owner already)
            if (!summon->IsGuardian())
                summon->SetOwnerGUID(caller->GetGUID());
            // it fights what you fight and what attacks you or it, and never starts a fight of its own (that pulled
            // you into fights; guardians too, whose own AI attacks the nearest enemy)
            summon->SetReactState(REACT_DEFENSIVE);
            KeepSummonOnSide(summon, owner, self);
        }, 1ms);
    }

    // A change of map takes every vehicle away; Steed Shift's seat comes back with the shape.
    void OnPlayerMapChanged(Player* player) override
    {
        if (player->HasAura(SkinshifterSpells::SkinStride) || player->HasAura(SkinshifterSpells::PossessedSteed))
            GiveSteedSeat(player);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (!ReinsRider.empty())
            CheckReins(player);
        if (!Burrowing.empty())
            UpdateBurrow(player, diff);
        if (!Gliders.empty())
            UpdateGlide(player);
        if (!StolenHeld.empty())
            KeepHeldItems(player);
        if (!CollectionQueue.empty() || !LearnQueue.empty() || !UnlearnQueue.empty())
            DrainQueues(player, diff);
    }

    // Character Advancement teaches and takes the talents: the ones that change the skin buff apply at once, and
    // Beastskin's identity passive brings Dual Wield (tooth and claw) with it.
    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        TalentChanged(player, spellId, true);
    }

    void OnPlayerForgotSpell(Player* player, uint32 spellId) override
    {
        TalentChanged(player, spellId, false);
    }

    static void TalentChanged(Player* player, uint32 spellId, bool learned)
    {
        if (player->getClass() != SkinshifterSpells::ClassId)
            return;
        if (spellId == SkinshifterSpells::TalentHardenedHide || spellId == SkinshifterSpells::TalentThousandSkins)
        {
            auto const worn = Worn.find(player->GetGUID().GetCounter());
            RefreshSkinBuff(player, worn != Worn.end() ? worn->second.displayId : 0);
        }
        SkinshifterSpells::SyncSpecExtras(player, spellId, learned);
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& message, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || language != LANG_ADDON || receiver != player)
            return true;
        return !HandleCollectionMessage(player, message);
    }

    void OnPlayerLogin(Player* player) override
    {
        if (Cfg.enabled)
        {
            LoadPlayer(player);
            LoadEcho(player);
            KeepStrongestByName(player);
        }
    }

    void OnPlayerLogout(Player* player) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        Worn.erase(guid);
        StolenNames.erase(guid);
        StolenHeld.erase(guid);
        Grimoires.erase(guid);
        CollectionAddon.erase(guid);
        CollectionQueue.erase(guid);
        CollectionKnown.erase(guid);
        LearnQueue.erase(guid);
        UnlearnQueue.erase(guid);
        DrainWait.erase(guid);
        EchoSpell.erase(guid);
        Gliders.erase(guid);
        EndBurrow(guid);
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        uint32 const low = guid.GetCounter();
        CharacterDatabase.Execute("DELETE FROM `skinshifter_skin` WHERE `guid` = {}", low);
        CharacterDatabase.Execute("DELETE FROM `skinshifter_skin_size` WHERE `guid` = {}", low);
        CharacterDatabase.Execute("DELETE FROM `skinshifter_grimoire` WHERE `guid` = {}", low);
        CharacterDatabase.Execute("DELETE FROM `skinshifter_worn` WHERE `guid` = {}", low);
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        SpellInfo const* spellInfo = spell->GetSpellInfo();
        if (spellInfo->Id == SkinshifterSpells::MimicStrike)
        {
            ObjectGuid const target = spell->m_targets.GetUnitTargetGUID();
            player->m_Events.AddEventAtOffset([player, target]()
            {
                MimicEcho(player, ObjectAccessor::GetUnit(*player, target));
            }, 300ms);
            return;
        }
        if (!IsTravelCast(spellInfo->Id) && !IsTravel(spellInfo->Id) && !spell->IsTriggered())
            EndTravel(player);
        if (spellInfo->Id >= SkinshifterSpells::StealForm && spellInfo->Id < SkinshifterSpells::FxGhostVisual)
            LOG_INFO("module", "Skinshifter: {} cast {} ({}).", player->GetName(), spellInfo->SpellName[0], spellInfo->Id);
        if (HandleOwnSpell(player, spell, spellInfo->Id))
            return;
        if (spell->IsTriggered())  // e.g. Mimic Strike's echo: no cooldown on the spell you cast yourself
            return;
        auto grimoire = Grimoires.find(player->GetGUID().GetCounter());
        if (grimoire == Grimoires.end() || !grimoire->second.count(spellInfo->Id))
            return;
        if (spellInfo->RecoveryTime || spellInfo->CategoryRecoveryTime)
            return;
        // Creature abilities have no cooldown of their own (the creature's AI times them). Crowd control gets a
        // long one, other quick abilities a short one.
        uint32 const cooldown = IsCrowdControl(spellInfo) ? Cfg.crowdControlCooldown :
            spellInfo->CalcCastTime() <= 1500 ? Cfg.shortSpellCooldown : 0;
        if (!cooldown)
            return;
        player->AddSpellCooldown(spellInfo->Id, 0, cooldown);
        WorldPacket data;
        player->BuildCooldownPacket(data, SPELL_COOLDOWN_FLAG_NONE, spellInfo->Id, cooldown);
        player->SendDirectMessage(&data);
    }
};

// Our own abilities, when the client asks to cast one: what the server thinks of it (a cast that fails here never
// reaches OnPlayerSpellCast, and some failures show no error in the client).
// ---- stolen shapes ----------------------------------------------------------------------------------------------
// A stolen ability that changes your shape (Abyssal Meteor Fall casts Abyssal Transformation) keeps the creature's
// endless, often hidden aura: casting it again ends the shape (a toggle), and so does Shed Skin / Release.
bool ChangesShape(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.ApplyAuraName == SPELL_AURA_TRANSFORM || effect.ApplyAuraName == SPELL_AURA_MOD_SHAPESHIFT)
            return true;
    return false;
}

// True when the spell, or a spell it casts (up to three steps on), changes the shape.
bool LeadsToShape(SpellInfo const* info, int depth = 0)
{
    if (ChangesShape(info))
        return true;
    if (depth >= 3)
        return false;
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.TriggerSpell && effect.TriggerSpell != info->Id)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (LeadsToShape(triggered, depth + 1))
                    return true;
    return false;
}

// Ends every shape a stolen ability gave (not the Skinshifter's own); true when there was one.
bool EndStolenShapes(Player* player)
{
    std::vector<uint32> ended;
    for (AuraEffect const* effect : player->GetAuraEffectsByType(SPELL_AURA_TRANSFORM))
        ended.push_back(effect->GetId());
    for (AuraEffect const* effect : player->GetAuraEffectsByType(SPELL_AURA_MOD_SHAPESHIFT))
        ended.push_back(effect->GetId());
    std::erase_if(ended, [](uint32 id) { return id >= SkinshifterSpells::StealForm && id < SkinshifterSpells::FxGhostVisual; });
    for (uint32 id : ended)
        player->RemoveAurasDueToSpell(id);
    return !ended.empty();
}

class SkinshifterCastCheckScript : public AllSpellScript
{
public:
    SkinshifterCastCheckScript() : AllSpellScript("SkinshifterCastCheckScript", { ALLSPELLHOOK_CAN_PREPARE,
        ALLSPELLHOOK_ON_SPELL_CHECK_CAST }) { }

    // A cast of ours the server turns down, with the reason (SpellCastResult), for the world log.
    void OnSpellCheckCast(Spell* spell, bool /*strict*/, SpellCastResult& result) override
    {
        SpellInfo const* info = spell->GetSpellInfo();
        Player* player = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
        if (!player || result == SPELL_CAST_OK || spell->IsTriggered() || info->Id < SkinshifterSpells::StealForm ||
            info->Id >= SkinshifterSpells::FxGhostVisual)
            return;
        LOG_INFO("module", "Skinshifter: the server turns down {}'s {} ({}): cast result {}.", player->GetName(),
            info->SpellName[0], info->Id, uint32(result));
    }

    bool CanPrepare(Spell* spell, SpellCastTargets const* targets, AuraEffect const* triggeredByAura) override
    {
        SpellInfo const* info = spell->GetSpellInfo();
        Player* player = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
        if (player && !triggeredByAura && !spell->IsTriggered() && SkinshifterSpells::OriginalOf(info->Id))
        {
            // a stolen shape: cast again to end it
            if (LeadsToShape(info) && EndStolenShapes(player))
            {
                spell->SendCastResult(SPELL_FAILED_DONT_REPORT);
                return false;
            }
            return true;
        }
        if (!player || triggeredByAura || spell->IsTriggered() || info->Id < SkinshifterSpells::StealForm ||
            info->Id >= SkinshifterSpells::FxGhostVisual)
            return true;
        Unit* target = targets ? targets->GetUnitTarget() : nullptr;
        LOG_INFO("module", "Skinshifter: {} asks to cast {} ({}) on {}.", player->GetName(), info->SpellName[0], info->Id,
            target ? target->GetName() : "no target");
        // Refused before the cast starts (no cast bar, cooldown or mana): Steal Form and Possess need a creature,
        // Shed Skin and Release a worn skin or body.
        ChatHandler handler(player->GetSession());
        switch (info->Id)
        {
            case SkinshifterSpells::StealForm:
            case SkinshifterSpells::Possess:
                if (AimedCreature(player, target))
                    return true;
                Refuse(&handler, info->Id == SkinshifterSpells::Possess ? "Target a creature or NPC to possess." :
                    "Target a creature or NPC to steal its form.");
                spell->SendCastResult(SPELL_FAILED_BAD_TARGETS);
                return false;
            case SkinshifterSpells::ShedSkin:
            case SkinshifterSpells::Release:
                if (EndStolenShapes(player))  // a stolen shape goes first; the skin under it stays
                {
                    spell->SendCastResult(SPELL_FAILED_DONT_REPORT);
                    return false;
                }
                if (Worn.count(player->GetGUID().GetCounter()))
                    return true;
                Refuse(&handler, info->Id == SkinshifterSpells::Release ? "You are not possessing a body." :
                    "You are not wearing a skin.");
                spell->SendCastResult(SPELL_FAILED_DONT_REPORT);
                return false;
            case SkinshifterSpells::Turnskin:
            case SkinshifterSpells::KindredSkin:
            case SkinshifterSpells::HostileTakeover:
            case SkinshifterSpells::WillingHost:
                if (SkinshifterSpells::TakeoverCanStart(player, AimedCreature(player, target), info->Id))
                    return true;
                spell->SendCastResult(SPELL_FAILED_DONT_REPORT);
                return false;
            default:
                return true;
        }
    }
};

class SkinshifterUnitScript : public UnitScript
{
public:
    SkinshifterUnitScript() : UnitScript("SkinshifterUnitScript", true, { UNITHOOK_ON_DISPLAYID_CHANGE,
        UNITHOOK_ON_AURA_APPLY, UNITHOOK_ON_AURA_REMOVE, UNITHOOK_IF_NORMAL_REACTION }) { }

    // Root Walk, under the ground or standing still as a herb or vein: creatures (not pets or other player-controlled
    // ones) leave you alone.
    bool IfNormalReaction(Unit const* unit, Unit const* target, ReputationRank& reaction) override
    {
        if (Hidden.empty() || !unit || !target)
            return true;
        auto hidden = [](Unit const* player, Unit const* creature)
        {
            return player->IsPlayer() && Hidden.count(player->GetGUID().GetCounter()) && creature->IsCreature() &&
                !creature->IsControlledByPlayer();
        };
        if (hidden(unit, target) || hidden(target, unit))
        {
            reaction = REP_NEUTRAL;
            return false;
        }
        return true;
    }

    // A mount's shape or the Root Walk worm ends with its buff: the skin (or your own look) comes back.
    void OnAuraRemove(Unit* unit, AuraApplication* aurApp, AuraRemoveMode /*mode*/) override
    {
        Player* player = unit->ToPlayer();
        uint32 const spellId = aurApp->GetBase()->GetId();
        if (player && spellId == SkinshifterSpells::RootWalking)
            ComeUp(player);
        if (player && spellId == SkinshifterSpells::WraithRunning)
        {
            player->SetFloatValue(UNIT_FIELD_HOVERHEIGHT, 1.0f);  // the players' default
            player->SetByteValue(UNIT_FIELD_BYTES_1, UNIT_BYTES_1_OFFSET_ANIM_TIER, UNIT_BYTE1_FLAG_GROUND);
        }
        if (player && IsStride(spellId))
            TakeSteedSeat(player);
        if (player && (IsStride(spellId) || IsTravel(spellId)))
            RestoreAfterTemporaryLook(player);
        // Steed Shift and its mount end together (a moment later, not inside this removal)
        bool const strideEnded = IsStride(spellId);
        bool const mountEnded = aurApp->GetBase()->GetSpellInfo()->HasAura(SPELL_AURA_MOUNTED);
        if (player && (strideEnded || mountEnded))
        {
            ObjectGuid const playerGuid = player->GetGUID();
            player->m_Events.AddEventAtOffset([player, playerGuid, strideEnded]()
            {
                if (player->GetGUID() != playerGuid)
                    return;
                if (strideEnded)
                    player->RemoveAurasByType(SPELL_AURA_MOUNTED);
                else
                    EndStride(player);
            }, 1ms);
        }
        if (player && (IsTravel(spellId) || IsStride(spellId)))  // the worn skin's effects come back, a moment later
            player->m_Events.AddEventAtOffset([player]()
            {
                RefreshWornFx(player, Worn.count(player->GetGUID().GetCounter()) != 0);
            }, 1ms);
    }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        Player* player = unit->ToPlayer();
        if (player && IsTravel(aura->GetId()))
        {
            uint32 const buff = aura->GetId();
            ObjectGuid const guid = player->GetGUID();
            player->m_Events.AddEventAtOffset([player, guid, buff]()
            {
                if (player->GetGUID() == guid && player->HasAura(buff))
                    TravelStarted(player, buff);
            }, 1ms);
            return;
        }
        // A mount in a skin becomes its shape (most skins cannot sit on one); a Possessor always possesses the mount,
        // in a body or not.
        if (!player || !aura->GetSpellInfo()->HasAura(SPELL_AURA_MOUNTED) ||
            !(Worn.count(player->GetGUID().GetCounter()) || IsPossessorRace(player->getRace())))
            return;
        // not inside the aura's own application: a moment later
        player->m_Events.AddEventAtOffset([player]() { StrideInsteadOfMount(player); }, 1ms);
    }

    void OnDisplayIdChange(Unit* unit, uint32 displayId) override
    {
        Player* player = unit->ToPlayer();
        if (!player)
            return;
        uint32 const guid = player->GetGUID().GetCounter();
        auto worn = Worn.find(guid);
        if (worn == Worn.end() || Reapplying.count(guid) || displayId == worn->second.displayId)
            return;
        if (displayId != player->GetNativeDisplayId() || !player->GetAuraEffectsByType(SPELL_AURA_TRANSFORM).empty())
            return;
        ApplyLook(player, worn->second);
    }
};

class SkinshifterWorldScript : public WorldScript
{
public:
    SkinshifterWorldScript() : WorldScript("SkinshifterWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }

    void OnStartup() override
    {
        CreateTables();
        LoadScriptedSpells();
        LoadNodeSpots();
        SkinshifterSpells::Report();
    }
};

// Every name answer about a disguised player carries the creature's name (the game builds them in several places:
// a name query, logging in, the dungeon finder), so the answer is changed on its way out.
class SkinshifterDisguiseScript : public ServerScript
{
public:
    SkinshifterDisguiseScript() : ServerScript("SkinshifterDisguiseScript", { SERVERHOOK_CAN_PACKET_SEND }) { }

    bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override
    {
        uint16 const opcode = packet.GetOpcode();
        if (!session || (opcode != SMSG_NAME_QUERY_RESPONSE && opcode != SMSG_MESSAGECHAT &&
            opcode != SMSG_GM_MESSAGECHAT && opcode != SMSG_GROUP_LIST))
            return true;
        try
        {
            if (opcode == SMSG_GROUP_LIST)
                return GroupListOut(session, packet);
            return opcode == SMSG_NAME_QUERY_RESPONSE ? NameAnswerOut(session, packet) : ChatOut(session, packet);
        }
        catch (ByteBufferException const&)
        {
            return true;
        }
    }

private:
    static bool ToSelf(WorldSession* session, ObjectGuid guid)
    {
        return session->GetPlayer() && session->GetPlayer()->GetGUID() == guid;
    }

    // The group list names every member; a disguised one (to the others) by the creature's name, as everywhere:
    // the client keeps one name per player, and the real one here would win over the disguise. Menus (remove from
    // group, vote kick) go by guid; only a typed name needs the real one, which chat shows.
    static bool GroupListOut(WorldSession* session, WorldPacket const& packet)
    {
        if (StolenNames.empty())
            return true;
        WorldPacket read(packet);
        read.rpos(0);
        uint8 const groupType = read.read<uint8>();
        read.rpos(read.rpos() + 3);  // subgroup, flags, roles
        if (groupType & GROUPTYPE_LFG)
            read.rpos(read.rpos() + 5);  // dungeon state, dungeon
        read.rpos(read.rpos() + 8 + 4);  // group guid, counter
        uint32 const count = read.read<uint32>();
        size_t const membersAt = read.rpos();
        WorldPacket out(SMSG_GROUP_LIST, packet.size() + 32);
        out.append(packet.contents(), membersAt);
        bool changed = false;
        for (uint32 index = 0; index < count; ++index)
        {
            std::string name;
            read >> name;
            ObjectGuid const guid(read.read<uint64>());
            std::string const* stolen = StolenNameOf(guid);
            bool const swap = stolen && !ToSelf(session, guid) && name != *stolen;  // not our own rewritten list again
            changed = changed || swap;
            out << (swap ? *stolen : name);
            out << guid.GetRawValue();
            out.append(packet.contents() + read.rpos(), 4);  // online state, subgroup, flags, roles
            read.rpos(read.rpos() + 4);
        }
        if (!changed)
            return true;
        out.append(packet.contents() + read.rpos(), packet.size() - read.rpos());
        session->SendPacket(&out);
        return false;
    }

    // A name answer about a disguised player (to someone else): the creature's name. About a chat guid: its
    // owner's real name.
    static bool NameAnswerOut(WorldSession* session, WorldPacket const& packet)
    {
        WorldPacket read(packet);
        read.rpos(0);
        ObjectGuid guid;
        uint8 unknown = 1;
        std::string name;
        read >> guid.ReadAsPacked() >> unknown;
        if (!unknown)
            read >> name;

        if (IsChatGuid(guid))
        {
            if (!unknown)
                return true;  // our own answer
            ObjectGuid const owner = OwnerOfChatGuid(guid);
            CharacterCacheEntry const* cached = sCharacterCache->GetCharacterCacheByGuid(owner);
            if (!cached)
                return true;
            uint8 race = cached->Race;
            if (Player* player = ObjectAccessor::FindConnectedPlayer(owner))
                ApplyNameQueryRace(sScriptMgr, player, race);
            WorldPacket data(SMSG_NAME_QUERY_RESPONSE, 8 + 1 + cached->Name.size() + 1 + 1 + 3 + 1);
            data << guid.WriteAsPacked();
            data << uint8(0) << cached->Name << std::string() << race << cached->Sex << cached->Class << uint8(0);
            session->SendPacket(&data);
            return false;
        }

        std::string const* stolen = StolenNameOf(guid);
        if (!stolen || unknown || name == *stolen || ToSelf(session, guid))
            return true;
        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        if (!player)
            return true;
        WorldPacket data = NameAnswer(player, *stolen);
        session->SendPacket(&data);
        return false;
    }

    // Chat from a disguised player (to someone else) comes from its chat guid, so it shows the real name.
    static bool ChatOut(WorldSession* session, WorldPacket const& packet)
    {
        if (StolenNames.empty() || packet.size() < 13)
            return true;
        uint8 const type = packet.read<uint8>(0);
        switch (type)
        {
            case CHAT_MSG_SYSTEM:
            case CHAT_MSG_MONSTER_SAY:
            case CHAT_MSG_MONSTER_PARTY:
            case CHAT_MSG_MONSTER_YELL:
            case CHAT_MSG_MONSTER_WHISPER:
            case CHAT_MSG_MONSTER_EMOTE:
            case CHAT_MSG_RAID_BOSS_EMOTE:
            case CHAT_MSG_RAID_BOSS_WHISPER:
            case CHAT_MSG_BATTLENET:
            case CHAT_MSG_WHISPER_FOREIGN:
            case CHAT_MSG_BG_SYSTEM_NEUTRAL:
            case CHAT_MSG_BG_SYSTEM_ALLIANCE:
            case CHAT_MSG_BG_SYSTEM_HORDE:
                return true;
            default:
                break;
        }
        ObjectGuid const sender(packet.read<uint64>(5));
        if (!StolenNameOf(sender) || ToSelf(session, sender))
            return true;
        ObjectGuid const chatGuid = ChatGuidOf(sender);
        WorldPacket data(packet);
        data.put<uint64>(5, chatGuid.GetRawValue());
        // the receiver guid after it (a whisper names the sender twice)
        std::size_t receiverPos = 13;
        if (packet.GetOpcode() == SMSG_GM_MESSAGECHAT && data.size() >= receiverPos + 4)
            receiverPos += 4 + data.read<uint32>(receiverPos);  // the sender's name
        if (type == CHAT_MSG_CHANNEL)
            while (receiverPos < data.size() && data.read<uint8>(receiverPos) != 0)
                ++receiverPos;
        if (type == CHAT_MSG_CHANNEL)
            ++receiverPos;
        if (receiverPos + 8 <= data.size() && data.read<uint64>(receiverPos) == sender.GetRawValue())
            data.put<uint64>(receiverPos, chatGuid.GetRawValue());
        session->SendPacket(&data);
        return false;
    }
};

namespace SkinshifterSpells
{
// Your look again at once (after a takeover): the worn skin, else your own model, size and gender.
void RestoreOwnLook(Player* player)
{
    auto const worn = Worn.find(player->GetGUID().GetCounter());
    if (worn != Worn.end())
        return ApplyLook(player, worn->second);
    player->RestoreDisplayId();
    player->SetObjectScale(player->GetNativeObjectScale());
    RestoreNativeSize(player);
}

void RefreshBodyFx(Player* player, bool hidden)
{
    RefreshWornFx(player, !hidden && Worn.count(player->GetGUID().GetCounter()) > 0);
}

std::vector<uint32> CreatureScriptedSpells(uint32 entry)
{
    auto const found = ScriptedSpells.find(entry);
    return found == ScriptedSpells.end() ? std::vector<uint32>{} : found->second;
}
}

void AddSC_skinshifter()
{
    new SkinshifterDisguiseScript();
    new SkinshifterCommandScript();
    new SkinshifterPlayerScript();
    new SkinshifterUnitScript();
    new SkinshifterCastCheckScript();
    new SkinshifterWorldScript();
}
