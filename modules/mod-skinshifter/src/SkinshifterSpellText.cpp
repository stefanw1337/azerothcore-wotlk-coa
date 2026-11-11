/*
 * mod-skinshifter - what a spell really does, for the tooltips of the SkinCollection addon (SkinTooltips.lua).
 *
 * Stolen creature spells often have no numbers in their text, or none at all. The addon asks for any spell its
 * tooltip shows; the answer is read from the spell itself, with this player's level:
 *   client -> server  TIP;<spell id>
 *   server -> client  TIP;<spell id>;<line> (one per line), TIPEND;<spell id>
 *   A line starting with ! is a warning, with ~ a note (the addon colours them).
 * A DUMMY or SCRIPT effect without a script behind it is said to do nothing, which is what happens in game.
 *
 * This code is released under the GNU AGPL v3 like AzerothCore.
 */

#include "Chat.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SkinshifterSpells.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "WorldPacket.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace
{
void SendTipLine(Player* player, std::string const& line)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, "SKIN\t" + line);
    player->SendDirectMessage(&packet);
}

std::string Num(float value)
{
    if (std::fabs(value) >= 10.0f)  // the game rounds damage and healing to whole numbers
        return std::to_string(int32(std::round(value)));
    float const rounded = std::round(value * 10.0f) / 10.0f;
    if (std::fabs(rounded - std::round(rounded)) < 0.05f)
        return std::to_string(int32(std::round(rounded)));
    std::string text = std::to_string(rounded);
    return text.substr(0, text.find('.') + 2);
}

std::string SecondsText(int32 ms)
{
    return Num(float(ms) / 1000.0f) + " sec";
}

std::string School(SpellInfo const* spell)
{
    uint32 const mask = spell->GetSchoolMask();
    static char const* const names[] = { "Physical", "Holy", "Fire", "Nature", "Frost", "Shadow", "Arcane" };
    std::string text;
    for (uint32 bit = 0; bit < 7; ++bit)
        if (mask & (1u << bit))
            text += (text.empty() ? "" : "/") + std::string(names[bit]);
    return text.empty() ? "Physical" : text;
}

std::string SpellName(uint32 id)
{
    SpellInfo const* spell = sSpellMgr->GetSpellInfo(id);
    return spell && spell->SpellName[0] && *spell->SpellName[0] ? spell->SpellName[0] : "spell " + std::to_string(id);
}

// The effect's range for this player (base points, die and level scaling, then the player's modifiers).
std::string Amount(Player* player, SpellInfo const* spell, uint8 index, bool absolute = true)
{
    SpellEffectInfo const& effect = spell->Effects[index];
    float base = float(effect.BasePoints);
    if (effect.RealPointsPerLevel != 0.0f)
    {
        int32 level = int32(player->GetLevel());
        if (spell->MaxLevel > 0 && level > int32(spell->MaxLevel))
            level = int32(spell->MaxLevel);
        level = std::max(level, int32(spell->BaseLevel)) - int32(std::max(spell->BaseLevel, spell->SpellLevel));
        base += float(level) * effect.RealPointsPerLevel;
    }
    int32 const die = effect.DieSides;
    float low = base + float(die == 0 ? 0 : (die >= 1 ? 1 : die));
    float high = base + float(die == 0 ? 0 : (die >= 1 ? die : 1));
    low = player->ApplyEffectModifiers(spell, index, low);
    high = player->ApplyEffectModifiers(spell, index, high);
    if (absolute)
    {
        low = std::fabs(low);
        high = std::fabs(high);
    }
    if (low > high)
        std::swap(low, high);
    return Num(low) == Num(high) ? Num(low) : Num(low) + " to " + Num(high);
}

bool HasScript(uint32 id)
{
    // the Skinshifter's own spells (below the stolen copies) are worked by the module, not by a spell script
    if (id >= SkinshifterSpells::StealForm && id < SkinshifterSpells::CopyFirst)
        return true;
    SpellScriptsBounds const bounds = sObjectMgr->GetSpellScriptsBounds(id);
    return bounds.first != bounds.second || sSpellScripts.find(id) != sSpellScripts.end();
}

std::string AuraLine(Player* player, SpellInfo const* spell, uint8 index)
{
    SpellEffectInfo const& effect = spell->Effects[index];
    int32 const value = effect.CalcValue(player);
    std::string const amount = Amount(player, spell, index);
    std::string const every = effect.Amplitude ? " every " + SecondsText(int32(effect.Amplitude)) : "";
    std::string const more = value < 0 ? "less" : "more";
    std::string const sign = value < 0 ? "-" : "+";
    switch (effect.ApplyAuraName)
    {
        case SPELL_AURA_PERIODIC_DAMAGE: return amount + " " + School(spell) + " damage" + every;
        case SPELL_AURA_PERIODIC_DAMAGE_PERCENT: return amount + "% of health as " + School(spell) + " damage" + every;
        case SPELL_AURA_PERIODIC_LEECH: return "Drains " + amount + " health" + every;
        case SPELL_AURA_PERIODIC_HEAL: return "Heals " + amount + every;
        case SPELL_AURA_OBS_MOD_HEALTH: return "Heals " + amount + "% of health" + every;
        case SPELL_AURA_PERIODIC_ENERGIZE: return "Restores " + amount + " power" + every;
        case SPELL_AURA_PERIODIC_TRIGGER_SPELL: return "Casts " + SpellName(effect.TriggerSpell) + every;
        case SPELL_AURA_PROC_TRIGGER_SPELL:
            return "When it procs (" + std::to_string(spell->ProcChance) + "%): casts " + SpellName(effect.TriggerSpell);
        case SPELL_AURA_DAMAGE_SHIELD: return "Attackers take " + amount + " " + School(spell) + " damage";
        case SPELL_AURA_SCHOOL_ABSORB: return "Absorbs " + amount + " damage";
        case SPELL_AURA_MOD_STUN: return "Stunned";
        case SPELL_AURA_MOD_ROOT: return "Rooted";
        case SPELL_AURA_MOD_FEAR: return "Feared";
        case SPELL_AURA_MOD_CONFUSE: return "Confused";
        case SPELL_AURA_MOD_SILENCE: return "Silenced";
        case SPELL_AURA_MOD_PACIFY: return "Cannot attack";
        case SPELL_AURA_MOD_DISARM: return "Disarmed";
        case SPELL_AURA_MOD_TAUNT: return "Taunted: attacks the caster";
        case SPELL_AURA_MOD_STEALTH: return "Stealthed";
        case SPELL_AURA_MOD_INVISIBILITY: return "Invisible";
        case SPELL_AURA_TRANSFORM: return "Changes shape";
        case SPELL_AURA_SCHOOL_IMMUNITY:
        case SPELL_AURA_DAMAGE_IMMUNITY: return "Immune to damage";
        case SPELL_AURA_MECHANIC_IMMUNITY:
        case SPELL_AURA_MECHANIC_IMMUNITY_MASK: return "Immune to some crowd control";
        case SPELL_AURA_EFFECT_IMMUNITY:
        case SPELL_AURA_STATE_IMMUNITY: return "Immune to some effects";
        case SPELL_AURA_MOD_PACIFY_SILENCE: return "Cannot attack or cast";
        case SPELL_AURA_REFLECT_SPELLS: return "Reflects " + amount + "% of spells";
        case SPELL_AURA_MOD_INCREASE_SPEED:
        case SPELL_AURA_MOD_SPEED_ALWAYS:
        case SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED: return "Moves " + amount + "% faster";
        case SPELL_AURA_MOD_DECREASE_SPEED:
        case SPELL_AURA_MOD_SPEED_SLOW_ALL: return "Moves " + amount + "% slower";
        case SPELL_AURA_MOD_MELEE_HASTE:
        case SPELL_AURA_MOD_ATTACKSPEED: return "Attacks " + amount + "% " + (value < 0 ? "slower" : "faster");
        case SPELL_AURA_MOD_CASTING_SPEED_NOT_STACK: return "Casts " + amount + "% " + (value < 0 ? "slower" : "faster");
        case SPELL_AURA_MOD_DAMAGE_PERCENT_DONE: return "Deals " + amount + "% " + more + " damage";
        case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN: return "Takes " + amount + "% " + more + " damage";
        case SPELL_AURA_MOD_DAMAGE_DONE: return "Deals " + amount + " " + more + " damage";
        case SPELL_AURA_MOD_DAMAGE_TAKEN: return "Takes " + amount + " " + more + " damage";
        case SPELL_AURA_MOD_HEALING_DONE_PERCENT: return "Heals " + amount + "% " + more;
        case SPELL_AURA_MOD_RESISTANCE: return (value < 0 ? "-" : "+") + amount + " armor/resistance";
        case SPELL_AURA_MOD_STAT: return (value < 0 ? "-" : "+") + amount + " stats";
        case SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE: return (value < 0 ? "-" : "+") + amount + "% stats";
        case SPELL_AURA_MOD_ATTACK_POWER: return (value < 0 ? "-" : "+") + amount + " attack power";
        case SPELL_AURA_MOD_INCREASE_HEALTH: return (value < 0 ? "-" : "+") + amount + " health";
        case SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT: return (value < 0 ? "-" : "+") + amount + "% health";
        case SPELL_AURA_MOD_HIT_CHANCE: return (value < 0 ? "-" : "+") + amount + "% chance to hit";
        case SPELL_AURA_MOD_DODGE_PERCENT: return (value < 0 ? "-" : "+") + amount + "% dodge";
        case SPELL_AURA_MOD_PARRY_PERCENT: return (value < 0 ? "-" : "+") + amount + "% parry";
        case SPELL_AURA_MOD_THREAT: return amount + "% " + more + " threat";
        case SPELL_AURA_MOD_SCALE: return std::string("Size ") + (value < 0 ? "-" : "+") + amount + "%";
        case SPELL_AURA_MOD_HEALING_PCT: return "Healing received " + sign + amount + "%";
        case SPELL_AURA_MOD_HEALING:
        case SPELL_AURA_MOD_HEALING_DONE: return sign + amount + " healing power";
        case SPELL_AURA_ASCENSION_MOD_SPELL_POWER_FLAT: return sign + amount + " spell power";
        case SPELL_AURA_ASCENSION_MOD_ATTACK_POWER_FLAT: return sign + amount + " attack power";
        case SPELL_AURA_MOD_RANGED_ATTACK_POWER: return sign + amount + " ranged attack power";
        case SPELL_AURA_MOD_ATTACK_POWER_PCT: return sign + amount + "% attack power";
        case SPELL_AURA_MOD_PERCENT_STAT: return sign + amount + "% stats";
        case SPELL_AURA_MOD_RESISTANCE_PCT:
        case SPELL_AURA_MOD_BASE_RESISTANCE_PCT: return sign + amount + "% armor/resistance";
        case SPELL_AURA_MOD_REGEN: return "Restores " + amount + " health every 5 sec";
        case SPELL_AURA_MOD_POWER_REGEN: return "Restores " + amount + " mana every 5 sec";
        case SPELL_AURA_MOD_HEALTH_REGEN_PERCENT: return sign + amount + "% health regeneration";
        case SPELL_AURA_MOD_POWER_REGEN_PERCENT: return sign + amount + "% mana regeneration";
        case SPELL_AURA_MOD_CRIT_PCT:
        case SPELL_AURA_MOD_WEAPON_CRIT_PERCENT: return sign + amount + "% critical strike chance";
        case SPELL_AURA_MOD_SPELL_CRIT_CHANCE:
        case SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL: return sign + amount + "% spell critical strike chance";
        case SPELL_AURA_MOD_BLOCK_PERCENT: return sign + amount + "% block";
        case SPELL_AURA_MOD_RATING: return sign + amount + " rating";
        case SPELL_AURA_MOD_INCREASE_SWIM_SPEED: return "Swims " + amount + "% faster";
        case SPELL_AURA_MELEE_SLOW: return "Attacks and casts " + amount + "% " + (value < 0 ? "slower" : "faster");
        case SPELL_AURA_MOD_ATTACKER_MELEE_HIT_CHANCE: return "Melee attacks against it hit " + sign + amount + "%";
        case SPELL_AURA_MOD_ATTACKER_SPELL_HIT_CHANCE: return "Spells against it hit " + sign + amount + "%";
        case SPELL_AURA_SPLIT_DAMAGE_PCT: return amount + "% of damage taken is shared";
        case SPELL_AURA_MOD_POWER_COST_SCHOOL_PCT: return "Spells cost " + amount + "% " + (value < 0 ? "less" : "more");
        case SPELL_AURA_WATER_WALK: return "Walks on water";
        case SPELL_AURA_FEATHER_FALL: return "Falls slowly";
        case SPELL_AURA_HOVER: return "Hovers";
        case SPELL_AURA_FLY: return "Can fly";
        case SPELL_AURA_MOD_POSSESS:
        case SPELL_AURA_MOD_CHARM: return "Takes control of the target";
        case SPELL_AURA_DUMMY:
            return HasScript(spell->Id) ? "Special effect (scripted)" : "!Marker only: does nothing by itself";
        default: return "~Other effect (aura type " + std::to_string(effect.ApplyAuraName) + ", value " + amount + ")";
    }
}

void Describe(Player* player, SpellInfo const* spell, std::vector<std::string>& lines, std::string const& indent,
    int depth)
{
    for (uint8 index = 0; index < MAX_SPELL_EFFECTS; ++index)
    {
        SpellEffectInfo const& effect = spell->Effects[index];
        if (!effect.Effect)
            continue;
        std::string const amount = Amount(player, spell, index);
        std::string const who = effect.IsAreaAuraEffect() || effect.HasRadius() ?
            (spell->IsPositiveEffect(index) ? " (allies within " : " (enemies within ") +
            Num(effect.CalcRadius(player)) + " yd)" : "";
        std::string line;
        uint32 trigger = 0;
        switch (effect.Effect)
        {
            case SPELL_EFFECT_SCHOOL_DAMAGE:
            case SPELL_EFFECT_ENVIRONMENTAL_DAMAGE: line = amount + " " + School(spell) + " damage" + who; break;
            case SPELL_EFFECT_HEALTH_LEECH: line = "Drains " + amount + " health" + who; break;
            case SPELL_EFFECT_HEAL: line = "Heals " + amount + who; break;
            case SPELL_EFFECT_HEAL_PCT: line = "Heals " + amount + "% of health" + who; break;
            case SPELL_EFFECT_HEAL_MAX_HEALTH: line = "Heals to full health" + who; break;
            case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE: line = amount + "% weapon damage" + who; break;
            case SPELL_EFFECT_WEAPON_DAMAGE:
            case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
            case SPELL_EFFECT_NORMALIZED_WEAPON_DMG: line = "Weapon damage + " + amount + who; break;
            case SPELL_EFFECT_POWER_BURN: line = "Burns " + amount + " power" + who; break;
            case SPELL_EFFECT_POWER_DRAIN: line = "Drains " + amount + " power" + who; break;
            case SPELL_EFFECT_ENERGIZE: line = "Restores " + amount + " power"; break;
            case SPELL_EFFECT_INSTAKILL: line = "Kills the target"; break;
            case SPELL_EFFECT_KNOCK_BACK: line = "Knocks back" + who; break;
            case SPELL_EFFECT_PULL_TOWARDS: line = "Pulls in" + who; break;
            case SPELL_EFFECT_CHARGE: line = "Charges the target"; break;
            case SPELL_EFFECT_LEAP:
            case SPELL_EFFECT_JUMP:
            case SPELL_EFFECT_JUMP_DEST:
            case SPELL_EFFECT_TELEPORT_UNITS: line = "Moves you"; break;
            case SPELL_EFFECT_INTERRUPT_CAST: line = "Interrupts spellcasting" + who; break;
            case SPELL_EFFECT_DISPEL: line = "Dispels " + amount + " effect(s)" + who; break;
            case SPELL_EFFECT_ATTACK_ME: line = "Taunts" + who; break;
            case SPELL_EFFECT_THREAT: line = amount + " threat" + who; break;
            case SPELL_EFFECT_ADD_COMBO_POINTS: line = "+" + amount + " combo point(s)"; break;
            case SPELL_EFFECT_SUMMON:
            {
                CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(uint32(effect.MiscValue));
                line = "Summons " + (amount != "0" && amount != "1" ? amount + " x " : std::string()) +
                    (creature ? creature->Name : "creature " + std::to_string(effect.MiscValue)) + ", on your side";
                break;
            }
            case SPELL_EFFECT_TRIGGER_SPELL:
            case SPELL_EFFECT_TRIGGER_MISSILE:
            case SPELL_EFFECT_FORCE_CAST:
                line = "Casts " + SpellName(effect.TriggerSpell);
                trigger = effect.TriggerSpell;
                break;
            case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                line = "Ground area " + Num(effect.CalcRadius(player)) + " yd: " + AuraLine(player, spell, index);
                if (effect.ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL)
                    trigger = effect.TriggerSpell;
                break;
            case SPELL_EFFECT_APPLY_AURA:
            case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
            case SPELL_EFFECT_APPLY_AREA_AURA_RAID:
            case SPELL_EFFECT_APPLY_AREA_AURA_FRIEND:
            case SPELL_EFFECT_APPLY_AREA_AURA_ENEMY:
            case SPELL_EFFECT_APPLY_AREA_AURA_PET:
            case SPELL_EFFECT_APPLY_AREA_AURA_OWNER:
                line = AuraLine(player, spell, index) + who;
                if (effect.ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL ||
                    effect.ApplyAuraName == SPELL_AURA_PROC_TRIGGER_SPELL)
                    trigger = effect.TriggerSpell;
                break;
            case SPELL_EFFECT_DUMMY:
            case SPELL_EFFECT_SCRIPT_EFFECT:
                line = HasScript(spell->Id) ? "Special effect (scripted)" :
                    "!No script: this part does nothing when you use it";
                break;
            default: line = "Effect " + std::to_string(effect.Effect); break;
        }
        lines.push_back(indent + line);
        if (trigger && depth < 1 && trigger != spell->Id)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(trigger))
                Describe(player, triggered, lines, indent + "   ", depth + 1);
    }
}
}

namespace SkinshifterSpells
{
bool HandleSpellTextMessage(Player* player, std::string_view body)
{
    if (body.substr(0, 4) != "TIP;")
        return false;
    Optional<uint32> id = Acore::StringTo<uint32>(body.substr(4));
    if (!id)
        return true;
    std::string const key = std::to_string(*id);
    if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(*id))
    {
        std::vector<std::string> lines;
        Describe(player, spell, lines, "", 0);
        int32 const duration = spell->GetMaxDuration();
        uint32 auras = 0;
        for (uint8 index = 0; index < MAX_SPELL_EFFECTS; ++index)
            auras += spell->Effects[index].IsAura() || spell->Effects[index].Effect == SPELL_EFFECT_PERSISTENT_AREA_AURA;
        if (duration > 0)
            lines.push_back("Lasts " + SecondsText(duration) + (auras > 1 ? ", all of these at the same time" : ""));
        else if (duration < 0 && auras)
            lines.push_back("Lasts until cancelled");
        if (uint32 const original = OriginalOf(*id))
            lines.push_back("~Copy of creature spell " + std::to_string(original));
        for (std::string line : lines)
        {
            std::replace(line.begin(), line.end(), ';', ',');
            if (line.size() > 200)
                line.resize(200);
            SendTipLine(player, "TIP;" + key + ";" + line);
        }
    }
    SendTipLine(player, "TIPEND;" + key);
    return true;
}
}
