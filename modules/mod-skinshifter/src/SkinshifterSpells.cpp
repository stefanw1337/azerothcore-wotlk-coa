/*
 * mod-skinshifter - the Skinshifter's own spells (the starter kit).
 *
 * Each spell is a copy of a real spell from the server's Spell.dbc (its visuals, sounds and timing), changed field by
 * field and written to `spell_dbc` before the server loads its spells (OnAfterConfigLoad). CoA's client spell stream
 * sends `spell_dbc` rows to the client at login once the id is registered, so the client needs no Spell.dbc patch.
 * The behaviour of Steal Form, Shed Skin and Skin Collection is in Skinshifter.cpp (OnPlayerSpellCast).
 *
 * This code is released under the GNU AGPL v3 like AzerothCore.
 */

#include "AscensionClientSpellPatches.h"
#include "Config.h"
#include "DBCfmt.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "SkinshifterSpells.h"
#include "SmartScriptMgr.h"
#include "SharedDefines.h"
#include "DBCStores.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Tokenize.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr uint32 SpellFields = 234;
constexpr uint32 NameField = 136;
constexpr uint32 RankField = 153;
constexpr uint32 DescriptionField = 170;
constexpr uint32 ToolTipField = 187;
constexpr uint32 LocalizedDwords = 17;

namespace SpellField
{
constexpr uint32 Attributes = 4;        // ... 11: AttributesEx7
constexpr uint32 Stances = 12;          // ... 15: StancesNot
constexpr uint32 Targets = 16;
constexpr uint32 TargetCreatureType = 17;
constexpr uint32 RequiresSpellFocus = 18;
constexpr uint32 CasterAuraState = 20;  // ... 27: exclude target aura spell
constexpr uint32 CastingTimeIndex = 28;
constexpr uint32 RecoveryTime = 29;
constexpr uint32 CategoryRecoveryTime = 30;
constexpr uint32 InterruptFlags = 31;
constexpr uint32 AuraInterruptFlags = 32;
constexpr uint32 ChannelInterruptFlags = 33;
constexpr uint32 MaxLevel = 37;
constexpr uint32 BaseLevel = 38;
constexpr uint32 SpellLevel = 39;
constexpr uint32 DurationIndex = 40;
constexpr uint32 PowerType = 41;
constexpr uint32 ManaCost = 42;
constexpr uint32 ManaCostPerLevel = 43;
constexpr uint32 ManaPerSecond = 44;
constexpr uint32 ManaPerSecondPerLevel = 45;
constexpr uint32 RangeIndex = 46;
constexpr uint32 Speed = 47;
constexpr uint32 StackAmount = 49;      // CumulativeAura
constexpr uint32 Totem = 50;            // ... 51
constexpr uint32 Reagent = 52;          // ... 59, then 60 ... 67 ReagentCount
constexpr uint32 EquippedItemClass = 68;
constexpr uint32 EquippedItemSubClassMask = 69;
constexpr uint32 EquippedItemInventoryTypeMask = 70;
constexpr uint32 ProcFlags = 34;
constexpr uint32 ProcChance = 35;
constexpr uint32 ProcCharges = 36;
constexpr uint32 EffectTriggerSpell = 116;
constexpr uint32 Effect = 71;           // 3 per field group from here to 121
constexpr uint32 EffectBasePoints = 80;
constexpr uint32 EffectTargetA = 86;
constexpr uint32 EffectTargetB = 89;
constexpr uint32 EffectRadiusIndex = 92;
constexpr uint32 EffectAura = 95;
constexpr uint32 EffectAmplitude = 98;
constexpr uint32 EffectDieSides = 74;
constexpr uint32 EffectRealPointsPerLevel = 77;
constexpr uint32 EffectMiscValue = 110;
constexpr uint32 EffectsEnd = 122;
constexpr uint32 SpellClassMask = 122;  // ... 130
constexpr uint32 SpellVisual = 131;     // ... 132
constexpr uint32 SpellIcon = 133;
constexpr uint32 ManaCostPercentage = 204;
constexpr uint32 StartRecoveryCategory = 205;
constexpr uint32 StartRecoveryTime = 206;
constexpr uint32 MaxTargetLevel = 207;
constexpr uint32 SpellFamilyName = 208;
constexpr uint32 SpellFamilyFlags = 209; // ... 211
constexpr uint32 DmgClass = 213;
constexpr uint32 SchoolMask = 225;
constexpr uint32 RuneCostID = 226;
}

constexpr uint32 EFFECT_DUMMY = 3;
constexpr uint32 TARGET_CASTER = 1;
constexpr uint32 TARGET_UNIT_TARGET_ANY = 25;
constexpr uint32 TARGET_FLAG_UNIT = 0x2;
constexpr uint32 CAST_INSTANT = 1;
constexpr uint32 CAST_1_5_SEC = 16;
constexpr uint32 RANGE_SELF = 1;
constexpr uint32 RANGE_TEN_YARDS = 7;
constexpr uint32 RANGE_THIRTY_YARDS = 4;
constexpr uint32 TARGET_UNIT_TARGET_ENEMY = 6;
constexpr uint32 EFFECT_SCHOOL_DAMAGE = 2;
constexpr uint32 RANGE_ANYWHERE = 13;
constexpr uint32 EFFECT_APPLY_AURA = 6;
constexpr uint32 AURA_DUMMY = 4;
constexpr uint32 DURATION_INFINITE = 21;
constexpr uint32 EFFECT_PULL_TOWARDS = 124;
constexpr uint32 EFFECT_WEAPON_PERCENT_DAMAGE = 31;
constexpr uint32 RANGE_MELEE = 2;  // "Combat Range": melee
constexpr uint32 AURA_PERIODIC_LEECH = 53;

struct Record
{
    std::array<uint32, SpellFields> values{};
    std::map<uint32, std::string> strings;
};

// Clears effect `index` (0..2) in every effect column group (Effect .. EffectPointsPerComboPoint).
void ClearEffect(Record& r, uint32 index)
{
    for (uint32 field = SpellField::Effect; field < SpellField::EffectsEnd; field += 3)
        r.values[field + index] = 0;
}

// No rune costs: a known spell with one makes CoA's client show the Death Knight rune bar (C_Spell.HasRuneUI), and
// a Skinshifter has no runes to pay with. Runes and runic power become mana.
void NoRunes(Record& r)
{
    r.values[SpellField::RuneCostID] = 0;
    if (r.values[SpellField::PowerType] == POWER_RUNE || r.values[SpellField::PowerType] == POWER_RUNIC_POWER)
    {
        r.values[SpellField::PowerType] = POWER_MANA;
        r.values[SpellField::ManaCost] = 0;
        r.values[SpellField::ManaCostPerLevel] = 0;
        r.values[SpellField::ManaPerSecond] = 0;
        r.values[SpellField::ManaPerSecondPerLevel] = 0;
    }
}

struct SpellDef
{
    uint32 id;
    uint32 source;
    uint32 iconFrom;  // the old-client look: the icon of this existing spell
    uint32 icon;      // our own SpellIcon row (Skinshifter.Spells.OwnIcons = 1)
    uint32 skillLine; // the spellbook tab (0 = none, for the effects)
    char const* name;
    char const* description;
    char const* toolTip;
    void (*shape)(Record&);
};

void Generic(Record& r)
{
    // our spells borrow a base row: never its creature-type limit (Mind Control's "humanoids only" refused every beast)
    r.values[SpellField::TargetCreatureType] = 0;
    r.values[SpellField::MaxLevel] = 0;
    r.values[SpellField::BaseLevel] = 1;
    r.values[SpellField::SpellLevel] = 1;
    r.values[SpellField::MaxTargetLevel] = 0;
    r.values[SpellField::SpellFamilyName] = 0;
    for (uint32 field = SpellField::SpellFamilyFlags; field < SpellField::SpellFamilyFlags + 3; ++field)
        r.values[field] = 0;
    for (uint32 field = SpellField::SpellClassMask; field < SpellField::SpellClassMask + 9; ++field)
        r.values[field] = 0;
}

// A plain spell with one dummy effect: no requirements, no cost, a normal global cooldown.
void DummyUtility(Record& r, uint32 target, uint32 castTime, uint32 range)
{
    Generic(r);
    for (uint32 field = SpellField::Attributes; field <= SpellField::Attributes + 7; ++field)
        r.values[field] = 0;
    for (uint32 field = SpellField::Stances; field <= SpellField::Stances + 3; ++field)
        r.values[field] = 0;
    for (uint32 field = SpellField::CasterAuraState; field <= SpellField::CasterAuraState + 7; ++field)
        r.values[field] = 0;
    for (uint32 field = SpellField::Effect; field < SpellField::EffectsEnd; ++field)
        r.values[field] = 0;
    r.values[SpellField::Targets] = target == TARGET_CASTER ? 0 : TARGET_FLAG_UNIT;
    r.values[SpellField::CastingTimeIndex] = castTime;
    r.values[SpellField::RecoveryTime] = 0;
    r.values[SpellField::CategoryRecoveryTime] = 0;
    r.values[SpellField::InterruptFlags] = castTime == CAST_INSTANT ? 0 : 0xF;
    r.values[SpellField::DurationIndex] = 0;
    r.values[SpellField::PowerType] = POWER_MANA;
    r.values[SpellField::ManaCost] = 0;
    r.values[SpellField::ManaCostPercentage] = 0;
    r.values[SpellField::RangeIndex] = range;
    r.values[SpellField::Effect] = EFFECT_DUMMY;
    r.values[SpellField::EffectTargetA] = target;
    r.values[SpellField::StartRecoveryCategory] = 133;
    r.values[SpellField::StartRecoveryTime] = 1500;
    r.values[SpellField::DmgClass] = 0;
}

void ShapeStealForm(Record& r) { DummyUtility(r, TARGET_UNIT_TARGET_ANY, CAST_1_5_SEC, RANGE_TEN_YARDS); }

// Turnskin, Kindred Skin, Hostile Takeover, Willing Host: instant, 30 yards, at a creature; the module does the rest
// (SkinshifterTakeover.cpp) and starts the cooldown the two share (Skinshifter.Takeover.Cooldown).
void ShapeTakeover(Record& r) { DummyUtility(r, TARGET_UNIT_TARGET_ANY, CAST_INSTANT, RANGE_THIRTY_YARDS); }
void ShapeTakeoverReset(Record& r) { DummyUtility(r, TARGET_CASTER, CAST_INSTANT, RANGE_SELF); }

// The takeover's hold, cast by the module: Mind Control's possession (effect 0 on the creature) and its marker on
// you (effect 1, the buff the possess bar's cancel button cancels), not channelled, until the module ends it. It
// cannot miss, be resisted or break by itself, has no level limit (an amount of 0), and pierces the charm immunity
// of rares and bosses.
void ShapeTakeoverControl(Record& r)
{
    Generic(r);
    for (uint32 field = SpellField::Attributes; field <= SpellField::Attributes + 7; ++field)
        r.values[field] = 0;
    for (uint32 field = SpellField::Stances; field <= SpellField::Stances + 3; ++field)
        r.values[field] = 0;
    for (uint32 field = SpellField::CasterAuraState; field <= SpellField::CasterAuraState + 7; ++field)
        r.values[field] = 0;
    r.values[1] = 0;  // no category
    r.values[2] = 0;  // no dispel type: it cannot be dispelled off the creature
    r.values[3] = 0;  // no mechanic: charm immunity does not stop it
    r.values[SpellField::Attributes] = SPELL_ATTR0_NO_IMMUNITIES;
    r.values[SpellField::Attributes + 1] = SPELL_ATTR1_NO_THREAT;
    r.values[SpellField::Attributes + 2] = SPELL_ATTR2_IGNORE_LINE_OF_SIGHT;
    r.values[SpellField::Attributes + 3] = SPELL_ATTR3_ALWAYS_HIT;
    r.values[SpellField::Targets] = TARGET_FLAG_UNIT;
    r.values[SpellField::CastingTimeIndex] = CAST_INSTANT;
    r.values[SpellField::RecoveryTime] = 0;
    r.values[SpellField::CategoryRecoveryTime] = 0;
    r.values[SpellField::InterruptFlags] = 0;
    r.values[SpellField::AuraInterruptFlags] = 0;
    r.values[SpellField::ChannelInterruptFlags] = 0;
    r.values[SpellField::DurationIndex] = DURATION_INFINITE;
    r.values[SpellField::PowerType] = POWER_MANA;
    r.values[SpellField::ManaCost] = 0;
    r.values[SpellField::ManaCostPercentage] = 0;
    r.values[SpellField::RangeIndex] = RANGE_ANYWHERE;
    r.values[SpellField::StartRecoveryCategory] = 0;
    r.values[SpellField::StartRecoveryTime] = 0;
    r.values[SpellField::DmgClass] = 0;
    for (uint32 index = 0; index < 3; ++index)
        ClearEffect(r, index);
    r.values[SpellField::Effect] = EFFECT_APPLY_AURA;
    r.values[SpellField::EffectAura] = SPELL_AURA_MOD_POSSESS;
    r.values[SpellField::EffectTargetA] = TARGET_UNIT_TARGET_ANY;
    r.values[SpellField::Effect + 1] = EFFECT_APPLY_AURA;
    r.values[SpellField::EffectAura + 1] = AURA_DUMMY;
    r.values[SpellField::EffectTargetA + 1] = TARGET_CASTER;
}
void ShapeShedSkin(Record& r) { DummyUtility(r, TARGET_CASTER, CAST_INSTANT, RANGE_SELF); }
void ShapeSkinCollection(Record& r) { DummyUtility(r, TARGET_CASTER, CAST_INSTANT, RANGE_SELF); }

void ShapeMimicStrike(Record& r)
{
    Generic(r);
    r.values[SpellField::PowerType] = POWER_MANA;
    r.values[SpellField::ManaCost] = 0;
    r.values[SpellField::ManaCostPercentage] = 4;
}

// Stolen Vigor: Renew turned into a drain on an enemy: a Shadow damage over time whose every tick heals you for what it
// took (periodic leech, as Drain Life without the channel).
void ShapeStolenVigor(Record& r)
{
    Generic(r);
    // No explicit target flag: CoA's client does not cast an enemy spell that has one (TARGET_FLAG_UNIT); it lights
    // the button and waits. Its own enemy spells leave the field 0 and take the target from the effect.
    r.values[SpellField::Targets] = 0;
    r.values[SpellField::RangeIndex] = RANGE_THIRTY_YARDS;
    r.values[SpellField::SchoolMask] = SPELL_SCHOOL_MASK_SHADOW;
    r.values[SpellField::DmgClass] = SPELL_DAMAGE_CLASS_MAGIC;
    ClearEffect(r, 1);
    ClearEffect(r, 2);
    r.values[SpellField::Effect] = EFFECT_APPLY_AURA;
    r.values[SpellField::EffectTargetA] = TARGET_UNIT_TARGET_ENEMY;
    r.values[SpellField::EffectTargetB] = 0;
    r.values[SpellField::EffectAura] = AURA_PERIODIC_LEECH;
}

// Visual-only spells for the effects: the source's visuals and missile speed, nothing else. No threat, no procs, no
// global cooldown, hidden from the spellbook, aura bar and combat log.
void FxCommon(Record& r)
{
    r.values[SpellField::Attributes] |= SPELL_ATTR0_DO_NOT_DISPLAY;
    r.values[SpellField::Attributes + 1] |= SPELL_ATTR1_NO_THREAT | SPELL_ATTR1_NO_AURA_ICON;
    r.values[SpellField::Attributes + 3] |= SPELL_ATTR3_SUPPRESS_CASTER_PROCS | SPELL_ATTR3_SUPPRESS_TARGET_PROCS;
    r.values[SpellField::AuraInterruptFlags] = 0;
    r.values[SpellField::ChannelInterruptFlags] = 0;
    r.values[SpellField::StartRecoveryCategory] = 0;
    r.values[SpellField::StartRecoveryTime] = 0;
}

void ShapeFxAura(Record& r)
{
    DummyUtility(r, TARGET_CASTER, CAST_INSTANT, RANGE_SELF);
    r.values[SpellField::Effect] = EFFECT_APPLY_AURA;
    r.values[SpellField::EffectAura] = AURA_DUMMY;
    r.values[SpellField::DurationIndex] = DURATION_INFINITE;
    FxCommon(r);
}

void ShapeFxMissile(Record& r)
{
    DummyUtility(r, TARGET_UNIT_TARGET_ANY, CAST_INSTANT, RANGE_ANYWHERE);
    FxCommon(r);
}

// An identity passive: one aura on yourself, always on, shown as "Passive" in the spellbook.
void Passive(Record& r, uint32 aura, int32 amount, uint32 miscValue)
{
    DummyUtility(r, TARGET_CASTER, CAST_INSTANT, RANGE_SELF);
    r.values[SpellField::Attributes] |= SPELL_ATTR0_PASSIVE;
    r.values[SpellField::Effect] = EFFECT_APPLY_AURA;
    r.values[SpellField::EffectAura] = aura;
    r.values[SpellField::EffectDieSides] = 1;
    r.values[SpellField::EffectBasePoints] = uint32(amount - 1);
    r.values[SpellField::EffectMiscValue] = miscValue;
    r.values[SpellField::DurationIndex] = DURATION_INFINITE;
    r.values[SpellField::StartRecoveryCategory] = 0;
    r.values[SpellField::StartRecoveryTime] = 0;
}

void ShapeFeralSkins(Record& r) { Passive(r, SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 5, SPELL_SCHOOL_MASK_NORMAL); }
void ShapeSilverTongue(Record& r) { Passive(r, SPELL_AURA_MOD_HEALING_DONE_PERCENT, 10, 0); }
void ShapeFelTouched(Record& r)
{
    Passive(r, SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 5, SPELL_SCHOOL_MASK_SHADOW | SPELL_SCHOOL_MASK_FIRE);
}
void ShapeElementalAttunement(Record& r)
{
    Passive(r, SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 5,
        SPELL_SCHOOL_MASK_FIRE | SPELL_SCHOOL_MASK_FROST | SPELL_SCHOOL_MASK_NATURE);
}

// A skin buff: on you while you wear a skin of one creature type, shown on your buff bar, gone when you shed it
// (it cannot be cancelled by hand). The amounts come from the config, so the tooltip always matches.
void SkinBuffEffect(Record& r, uint32 index, uint32 aura, int32 amount, uint32 miscValue)
{
    r.values[SpellField::Effect + index] = EFFECT_APPLY_AURA;
    r.values[SpellField::EffectTargetA + index] = TARGET_CASTER;
    r.values[SpellField::EffectAura + index] = aura;
    r.values[SpellField::EffectDieSides + index] = 1;
    r.values[SpellField::EffectBasePoints + index] = uint32(amount - 1);
    r.values[SpellField::EffectMiscValue + index] = miscValue;
}

// Skin buffs (while worn) and Echoes (20 sec after you change out of the skin), by creature type: their effects.
struct TypeBuffEffect { uint32 aura; int32 amount; uint32 misc; };
constexpr uint32 SchoolsElemental = SPELL_SCHOOL_MASK_FIRE | SPELL_SCHOOL_MASK_FROST | SPELL_SCHOOL_MASK_NATURE;
std::map<uint32, std::vector<TypeBuffEffect>> const TypeBuffs =
{
    { SkinshifterSpells::SkinDragonkin, { { SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 8, SchoolsElemental },
        { SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT, 8, 0 } } },
    { SkinshifterSpells::SkinDemon, { { SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 10,
        SPELL_SCHOOL_MASK_FIRE | SPELL_SCHOOL_MASK_SHADOW } } },
    { SkinshifterSpells::SkinElemental, { { SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 10, SchoolsElemental } } },
    { SkinshifterSpells::SkinGiant, { { SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT, 15, 0 } } },
    { SkinshifterSpells::SkinUndead, { { SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 10, SPELL_SCHOOL_MASK_SHADOW },
        { SPELL_AURA_MOD_RESISTANCE, 50, SPELL_SCHOOL_MASK_SHADOW } } },
    { SkinshifterSpells::SkinHumanoid, { { SPELL_AURA_MOD_HEALING_DONE_PERCENT, 10, 0 },
        { SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, 5, uint32(-1) } } },
    { SkinshifterSpells::SkinMechanical, { { SPELL_AURA_MOD_RESISTANCE_PCT, 25, SPELL_SCHOOL_MASK_NORMAL } } },
    { SkinshifterSpells::EchoBeast, { { SPELL_AURA_MOD_MELEE_HASTE, 10, 0 }, { SPELL_AURA_MOD_INCREASE_SPEED, 10, 0 } } },
    { SkinshifterSpells::EchoDragonkin, { { SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 8, SchoolsElemental } } },
    { SkinshifterSpells::EchoDemon, { { SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 8,
        SPELL_SCHOOL_MASK_FIRE | SPELL_SCHOOL_MASK_SHADOW } } },
    { SkinshifterSpells::EchoElemental, { { SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 8, SchoolsElemental } } },
    { SkinshifterSpells::EchoGiant, { { SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT, 10, 0 } } },
    { SkinshifterSpells::EchoUndead, { { SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, 8, SPELL_SCHOOL_MASK_SHADOW },
        { SPELL_AURA_MOD_RESISTANCE, 50, SPELL_SCHOOL_MASK_SHADOW } } },
    { SkinshifterSpells::EchoHumanoid, { { SPELL_AURA_MOD_HEALING_DONE_PERCENT, 15, 0 } } },
    { SkinshifterSpells::EchoMechanical, { { SPELL_AURA_MOD_RESISTANCE_PCT, 20, SPELL_SCHOOL_MASK_NORMAL } } },
};
constexpr uint32 DURATION_20_SEC = 18;

void ShapeTypeBuff(Record& r)
{
    DummyUtility(r, TARGET_CASTER, CAST_INSTANT, RANGE_SELF);
    bool const echo = r.values[0] >= SkinshifterSpells::EchoBeast;
    if (!echo)
        r.values[SpellField::Attributes] |= SPELL_ATTR0_NO_AURA_CANCEL;
    r.values[SpellField::Attributes + 1] |= SPELL_ATTR1_NO_THREAT;
    r.values[SpellField::Attributes + 3] |= SPELL_ATTR3_ALLOW_AURA_WHILE_DEAD;
    r.values[SpellField::DurationIndex] = echo ? DURATION_20_SEC : DURATION_INFINITE;
    r.values[SpellField::StartRecoveryCategory] = 0;
    r.values[SpellField::StartRecoveryTime] = 0;
    r.values[SpellField::Effect] = 0;
    if (auto const buff = TypeBuffs.find(r.values[0]); buff != TypeBuffs.end())
        for (uint32 index = 0; index < buff->second.size() && index < 3; ++index)
            SkinBuffEffect(r, index, buff->second[index].aura, buff->second[index].amount, buff->second[index].misc);
}

void ShapeBorrowedHide(Record& r)
{
    DummyUtility(r, TARGET_CASTER, CAST_INSTANT, RANGE_SELF);
    r.values[SpellField::Attributes] |= SPELL_ATTR0_NO_AURA_CANCEL;
    r.values[SpellField::Attributes + 1] |= SPELL_ATTR1_NO_THREAT;
    r.values[SpellField::Attributes + 3] |= SPELL_ATTR3_ALLOW_AURA_WHILE_DEAD;  // the skin stays on through death
    r.values[SpellField::DurationIndex] = DURATION_INFINITE;
    r.values[SpellField::StartRecoveryCategory] = 0;
    r.values[SpellField::StartRecoveryTime] = 0;
    SkinBuffEffect(r, 0, SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT,
        sConfigMgr->GetOption<int32>("Skinshifter.SkinBuff.BeastHealthPercent", 20), 0);
    SkinBuffEffect(r, 1, SPELL_AURA_MOD_RESISTANCE_PCT,
        sConfigMgr->GetOption<int32>("Skinshifter.SkinBuff.BeastArmorPercent", 50), SPELL_SCHOOL_MASK_NORMAL);
}

// Steed Shift / Wraith Running / Phase Running: a mount used while wearing a skin. Most creature models have no riding pose, so you
// take the mount's shape instead and run at its speed (the module sets the amount and the look) until you attack,
// cast or take damage; then the skin comes back.
void ShapeStride(Record& r)
{
    DummyUtility(r, TARGET_CASTER, CAST_INSTANT, RANGE_SELF);
    r.values[SpellField::Attributes + 1] |= SPELL_ATTR1_NO_THREAT;
    r.values[SpellField::DurationIndex] = DURATION_INFINITE;
    r.values[SpellField::StartRecoveryCategory] = 0;
    r.values[SpellField::StartRecoveryTime] = 0;
    r.values[SpellField::AuraInterruptFlags] = AURA_INTERRUPT_FLAG_CAST | AURA_INTERRUPT_FLAG_MELEE_ATTACK |
        AURA_INTERRUPT_FLAG_SPELL_ATTACK | AURA_INTERRUPT_FLAG_TAKE_DAMAGE | AURA_INTERRUPT_FLAG_CHANGE_MAP;
    SkinBuffEffect(r, 0, SPELL_AURA_MOD_INCREASE_SPEED, 60, 0);
}

// Steed Shift / Possessed Steed: you are the mount for as long as you ride it. Taking damage does not end it (a
// mount does not throw you off for that either); casting or attacking dismounts, as from any mount.
void ShapeSteed(Record& r)
{
    ShapeStride(r);
    r.values[SpellField::AuraInterruptFlags] &= ~uint32(AURA_INTERRUPT_FLAG_TAKE_DAMAGE);
}

// The travel buffs (Root Walking, Wraith Glide, Phase Running) end like the strides but not by casting (the module
// ends them when you cast anything other than their toggle). The module casts them for the toggle, and they carry
// the mount rules: 1.5 sec standing still, out of combat. Outdoors is checked by the module when you start, not by
// the spell: an "outdoors only" spell ends the moment you are under a roof, and Root Walk and Wraith Glide pass
// under and through buildings (Root Walk ended under the Goldshire inn's floor and threw you outside). No fall
// damage while they last (passing through a floor can drop you onto the ground below).
void ShapeRootWalking(Record& r)
{
    ShapeStride(r);
    r.values[SpellField::AuraInterruptFlags] &= ~uint32(AURA_INTERRUPT_FLAG_CAST);
    r.values[SpellField::CastingTimeIndex] = CAST_1_5_SEC;
    r.values[SpellField::InterruptFlags] = 0xF;
    r.values[SpellField::Attributes] |= SPELL_ATTR0_NOT_IN_COMBAT_ONLY_PEACEFUL;
    SkinBuffEffect(r, 1, SPELL_AURA_SAFE_FALL, 1000, 0);  // falls up to 1000 yards do no harm
}

// Wraith Glide: a travel buff that also hovers you a little off the ground (the module sets how high, in the
// wraith's look) so you glide, not run.
void ShapeWraithGlide(Record& r)
{
    ShapeRootWalking(r);
    SkinBuffEffect(r, 2, SPELL_AURA_HOVER, 0, 0);
}

// Root Walk, Wraith Run, Phase Run: the travel form's toggle, instant like a mount's dismount, so it also ends the
// form on the move. Starting it casts the buff, which takes the 1.5 sec.
void ShapeRootWalk(Record& r)
{
    DummyUtility(r, TARGET_CASTER, CAST_INSTANT, RANGE_SELF);
    r.values[SpellField::StartRecoveryCategory] = 0;
    r.values[SpellField::StartRecoveryTime] = 0;
}

// Lure: a thrown blade (the look of an NPC's Throw Dagger) that pulls one creature to you from 30 yards, so a
// fight starts where you choose. Little damage, growing with level; a short cooldown.
// Built on Heroic Throw (57755): a throw that is a melee-class attack, so it needs no ranged weapon. Throw (2764)
// and an NPC's Throw Dagger are ranged-class: CoA's client takes them for a ranged-weapon shot, lights the button
// and waits for a ranged swing that never comes without a ranged weapon (the cast is never sent). Heroic Throw's
// main-hand requirement, its form ties and its rage cost are taken off. The dagger's look (VisualFrom) is Monster
// Slayer's Dagger's (54071, visual 11844), whose missile is a real thrown dagger model: Throw Dagger's and Heroic
// Throw's visuals fly the thrower's own weapon, which with none in hand is the client's checkered box.
void ShapeLure(Record& r)
{
    Generic(r);
    for (uint32 index = 0; index < 3; ++index)
        ClearEffect(r, index);
    r.values[1] = 0;  // no category
    r.values[2] = 0;  // no dispel type
    r.values[3] = 0;  // no mechanic
    for (uint32 field = SpellField::Attributes; field <= SpellField::Attributes + 7; ++field)
        r.values[field] = 0;  // not "main hand weapon", not "not while shapeshifted": a skin is no reason to fail
    r.values[SpellField::Attributes] = SPELL_ATTR0_IS_ABILITY;
    for (uint32 field = SpellField::Stances; field <= SpellField::Stances + 3; ++field)
        r.values[field] = 0;
    for (uint32 field = SpellField::CasterAuraState; field <= SpellField::CasterAuraState + 7; ++field)
        r.values[field] = 0;
    r.values[SpellField::EquippedItemClass] = uint32(-1);  // no weapon needed at all
    r.values[SpellField::EquippedItemSubClassMask] = 0;
    r.values[SpellField::EquippedItemInventoryTypeMask] = 0;
    for (uint32 field = 216; field <= 218; ++field)
        r.values[field] = std::bit_cast<uint32>(1.0f);  // full damage on each effect
    r.values[SpellField::Targets] = 0;  // no explicit target flag on an enemy spell (CoA's client)
    r.values[SpellField::CastingTimeIndex] = CAST_INSTANT;
    r.values[SpellField::InterruptFlags] = 0;
    r.values[SpellField::RecoveryTime] = 6000;
    r.values[SpellField::CategoryRecoveryTime] = 0;
    r.values[SpellField::RangeIndex] = RANGE_THIRTY_YARDS;
    r.values[SpellField::PowerType] = POWER_MANA;
    r.values[SpellField::ManaCost] = 0;
    r.values[SpellField::ManaCostPercentage] = 3;
    r.values[SpellField::StartRecoveryCategory] = 133;
    r.values[SpellField::StartRecoveryTime] = 1500;
    r.values[SpellField::SchoolMask] = SPELL_SCHOOL_MASK_NORMAL;
    r.values[SpellField::DmgClass] = SPELL_DAMAGE_CLASS_MELEE;
    r.values[SpellField::Effect] = EFFECT_SCHOOL_DAMAGE;
    r.values[SpellField::EffectTargetA] = TARGET_UNIT_TARGET_ENEMY;
    r.values[SpellField::EffectBasePoints] = 7;
    r.values[SpellField::EffectDieSides] = 5;
    r.values[SpellField::EffectRealPointsPerLevel] = std::bit_cast<uint32>(1.5f);
    // and it draws the creature to you (as Death Grip: a jump to where you stand; bosses do not move)
    r.values[SpellField::Effect + 1] = EFFECT_PULL_TOWARDS;
    r.values[SpellField::EffectTargetA + 1] = TARGET_UNIT_TARGET_ENEMY;
    r.values[SpellField::EffectMiscValue + 1] = 250;  // 25 yards a second
}

// ---- Talent abilities ------------------------------------------------------------------------------------------
// The active nodes of the spec trees, at a target: built like Lure (on Heroic Throw: a melee-class ability that needs
// no weapon, no form ties, no explicit target flag for CoA's client), effect 0 a dummy the module answers
// (Skinshifter.cpp, HandleOwnSpell): it casts the heads.
void EnemyAbility(Record& r, uint32 range, uint32 cooldown, uint32 manaPercent)
{
    ShapeLure(r);
    for (uint32 index = 0; index < 3; ++index)
        ClearEffect(r, index);
    r.values[SpellField::RecoveryTime] = cooldown;
    r.values[SpellField::RangeIndex] = range;
    r.values[SpellField::ManaCostPercentage] = manaPercent;
    r.values[SpellField::Speed] = 0;
    r.values[SpellField::Effect] = EFFECT_DUMMY;
    r.values[SpellField::EffectTargetA] = TARGET_UNIT_TARGET_ENEMY;
}

// Hydra Head (Beastskin): three bolts, one after another, each a random head's (the module picks).
void ShapeHydraHead(Record& r) { EnemyAbility(r, RANGE_THIRTY_YARDS, 8000, 4); }
// Cerberus (Felstriker): three bites in a row, Physical, Fire and Shadow (the module casts them).
void ShapeCerberus(Record& r) { EnemyAbility(r, RANGE_MELEE, 10000, 4); }

// A head's strike, cast by the module (triggered): no cost, cooldown or global cooldown.
void HeadStrike(Record& r, uint32 schoolMask, uint32 damageClass)
{
    Generic(r);
    for (uint32 index = 0; index < 3; ++index)
        ClearEffect(r, index);
    r.values[1] = 0;
    r.values[2] = 0;
    r.values[3] = 0;
    for (uint32 field = SpellField::Attributes; field <= SpellField::Attributes + 7; ++field)
        r.values[field] = 0;
    r.values[SpellField::Attributes] = SPELL_ATTR0_IS_ABILITY;
    for (uint32 field = SpellField::Stances; field <= SpellField::Stances + 3; ++field)
        r.values[field] = 0;
    for (uint32 field = SpellField::CasterAuraState; field <= SpellField::CasterAuraState + 7; ++field)
        r.values[field] = 0;
    r.values[SpellField::EquippedItemClass] = uint32(-1);
    r.values[SpellField::EquippedItemSubClassMask] = 0;
    r.values[SpellField::EquippedItemInventoryTypeMask] = 0;
    for (uint32 field = 216; field <= 218; ++field)
        r.values[field] = std::bit_cast<uint32>(1.0f);
    r.values[SpellField::Targets] = 0;
    r.values[SpellField::CastingTimeIndex] = CAST_INSTANT;
    r.values[SpellField::InterruptFlags] = 0;
    r.values[SpellField::RecoveryTime] = 0;
    r.values[SpellField::CategoryRecoveryTime] = 0;
    r.values[SpellField::DurationIndex] = 0;
    r.values[SpellField::RangeIndex] = damageClass == SPELL_DAMAGE_CLASS_MELEE ? RANGE_MELEE : RANGE_THIRTY_YARDS;
    r.values[SpellField::PowerType] = POWER_MANA;
    r.values[SpellField::ManaCost] = 0;
    r.values[SpellField::ManaCostPercentage] = 0;
    r.values[SpellField::StartRecoveryCategory] = 0;
    r.values[SpellField::StartRecoveryTime] = 0;
    r.values[SpellField::SchoolMask] = schoolMask;
    r.values[SpellField::DmgClass] = damageClass;
    r.values[SpellField::EffectTargetA] = TARGET_UNIT_TARGET_ENEMY;
}

// A hydra's bolt: about a third of a 1.5 sec nuke at 60 each (110), growing with level.
void HydraBolt(Record& r, uint32 schoolMask)
{
    HeadStrike(r, schoolMask, SPELL_DAMAGE_CLASS_MAGIC);
    r.values[SpellField::Speed] = std::bit_cast<uint32>(30.0f);
    r.values[SpellField::Effect] = EFFECT_SCHOOL_DAMAGE;
    r.values[SpellField::EffectBasePoints] = 3;
    r.values[SpellField::EffectDieSides] = 6;
    r.values[SpellField::EffectRealPointsPerLevel] = std::bit_cast<uint32>(1.75f);
}
void ShapeHydraPoison(Record& r) { HydraBolt(r, SPELL_SCHOOL_MASK_NATURE); }
void ShapeHydraArcane(Record& r) { HydraBolt(r, SPELL_SCHOOL_MASK_ARCANE); }
void ShapeHydraSand(Record& r) { HydraBolt(r, SPELL_SCHOOL_MASK_NORMAL); }

// A Cerberus head's bite: 60% of your weapon damage in its school.
void CerberusHead(Record& r, uint32 schoolMask)
{
    HeadStrike(r, schoolMask, SPELL_DAMAGE_CLASS_MELEE);
    r.values[SpellField::Effect] = EFFECT_WEAPON_PERCENT_DAMAGE;
    r.values[SpellField::EffectBasePoints] = 59;
    r.values[SpellField::EffectDieSides] = 1;
}
void ShapeCerberusBite(Record& r) { CerberusHead(r, SPELL_SCHOOL_MASK_NORMAL); }
void ShapeCerberusFire(Record& r) { CerberusHead(r, SPELL_SCHOOL_MASK_FIRE); }
void ShapeCerberusShadow(Record& r) { CerberusHead(r, SPELL_SCHOOL_MASK_SHADOW); }

// Twin Fangs' extra strike (Beastskin talent, a chance on each auto attack): your weapon's full damage once more.
void ShapeTwinFangsStrike(Record& r)
{
    CerberusHead(r, SPELL_SCHOOL_MASK_NORMAL);
    r.values[SpellField::EffectBasePoints] = 99;
}

// A signature ability: the source spell as it is, on mana, with no class or level ties.
// Lingering Wound: Rend's bleed with no cost or cooldown; the module casts it with the amount per tick (a slot upgrade).
void ShapeLingeringWound(Record& r)
{
    Generic(r);
    r.values[SpellField::ManaCost] = 0;
    r.values[SpellField::ManaCostPercentage] = 0;
    r.values[SpellField::RecoveryTime] = 0;
    r.values[SpellField::CategoryRecoveryTime] = 0;
    r.values[SpellField::StartRecoveryCategory] = 0;
    r.values[SpellField::StartRecoveryTime] = 0;
    r.values[SpellField::Attributes + 1] |= SPELL_ATTR1_NO_THREAT;
}

void ShapeSignature(Record& r)
{
    Generic(r);
    r.values[SpellField::PowerType] = POWER_MANA;
    r.values[SpellField::ManaCost] = 0;
    r.values[SpellField::ManaCostPercentage] = 6;
}

void ShapeStarterCaster(Record& r)
{
    Generic(r);
    r.values[SpellField::PowerType] = POWER_MANA;
    r.values[SpellField::ManaCost] = 0;
    r.values[SpellField::ManaCostPercentage] = 5;
}

void ShapeSlipAway(Record& r)
{
    Generic(r);
    r.values[SpellField::RecoveryTime] = 10000;
}

// The Possessor slips between worlds instead of hiding in this one: Stealth's stealth becomes invisibility (Invisibility's
// 340, which the client shows as the world turned pale and ghostly while you still see all of it); the slowed walk stays.
void ShapeSlipBetweenWorlds(Record& r)
{
    ShapeSlipAway(r);
    for (uint32 index = 0; index < 3; ++index)
        if (r.values[SpellField::EffectAura + index] == SPELL_AURA_MOD_STEALTH)
        {
            r.values[SpellField::EffectAura + index] = SPELL_AURA_MOD_INVISIBILITY;
            r.values[SpellField::EffectMiscValue + index] = 0;  // INVISIBILITY_GENERAL
            r.values[SpellField::EffectBasePoints + index] = 339;
        }
}

#define FX_AURA(id, source, name) { SkinshifterSpells::id, source, source, 0, 0, name, "", "", ShapeFxAura }
#define FX_MISSILE(id, source, name) { SkinshifterSpells::id, source, source, 0, 0, name, "", "", ShapeFxMissile }

std::array<SpellDef, 22 + 6 + 9 + 8 + 18 + 16> const Spells =
{{
    { SkinshifterSpells::StealForm, 921, 921, SkinshifterSpells::IconStealForm, SkinshifterSpells::SkillSkinshifter, "Steal Form",
        "Study your target for 1.5 sec, then steal its abilities and its form. Works on beasts, monsters and people "
        "alike, friend or foe. Faction leaders and bosses resist.",
        "Steal the target's abilities and form.", ShapeStealForm },
    { SkinshifterSpells::ShedSkin, 921, 1856, SkinshifterSpells::IconShedSkin, SkinshifterSpells::SkillSkinshifter, "Shed Skin",
        "Shed your stolen skin and return to your own form.",
        "Return to your own form.", ShapeShedSkin },
    { SkinshifterSpells::SkinCollection, 921, 1784, SkinshifterSpells::IconSkinCollection, SkinshifterSpells::SkillSkinshifter, "Skin Collection",
        "Open your Skin Collection: see every skin you have stolen and choose one to wear.",
        "", ShapeSkinCollection },
    { SkinshifterSpells::MimicStrike, 1752, 1752, SkinshifterSpells::IconMimicStrike, SkinshifterSpells::SkillSkinshifter, "Mimic Strike",
        "A quick strike causing $s1 additional damage. It also echoes the last offensive ability you stole, at "
        "half strength and at most every 6 sec.",
        "", ShapeMimicStrike },
    { SkinshifterSpells::StolenVigor, 139, 139, SkinshifterSpells::IconStolenVigor, SkinshifterSpells::SkillSkinshifter, "Stolen Vigor",
        "Steal the target's vigor: $o1 Shadow damage over $d, and every bit of it heals you.",
        "Losing $s1 health every $t1 sec to a thief.", ShapeStolenVigor },
    { SkinshifterSpells::SlipAway, 1784, 1784, SkinshifterSpells::IconSlipAway, SkinshifterSpells::SkillSkinshifter, "Slip Away",
        "Slip out of sight. Moving slowly, you are hard to spot until you attack or take damage.",
        "Stealthed. Movement slowed.", ShapeSlipAway },
    { SkinshifterSpells::Possess, 921, 921, SkinshifterSpells::IconPossess, SkinshifterSpells::SkillSkinshifter, "Possess",
        "Slip your spirit into the target for 1.5 sec, then wear its body: you steal its abilities and its form. Works "
        "on beasts, monsters and people alike, friend or foe. Faction leaders and bosses resist.",
        "Possess the target, stealing its abilities and form.", ShapeStealForm },
    { SkinshifterSpells::SlipBetweenWorlds, 1784, 58984, SkinshifterSpells::IconSlipBetweenWorlds, SkinshifterSpells::SkillSkinshifter, "Slip Between Worlds",
        "Step half out of this world: the world turns pale and ghostly, and those who cannot see the invisible cannot "
        "see you, until you attack or take damage. You move slowly while between worlds.",
        "Between worlds: invisible. Movement slowed.", ShapeSlipBetweenWorlds },
    { SkinshifterSpells::Release, 921, 1856, SkinshifterSpells::IconRelease, SkinshifterSpells::SkillSkinshifter, "Release",
        "Release the body you are wearing and return to your own form.",
        "Return to your own form.", ShapeShedSkin },
    { SkinshifterSpells::Turnskin, 921, 1513, 0, SkinshifterSpells::SkillSkinshifter, "Turnskin",
        "PvPvE ability. "
        "Tear open the target and slide inside: you play the creature, with its health, its speed and its abilities "
        "and more from your Grimoire. It stays a monster: players can hunt you, and you them. Rares, elites and "
        "bosses cost Skin Tokens. When you step out, the creature dies and its kill and loot are yours.",
        "", ShapeTakeover },
    { SkinshifterSpells::KindredSkin, 921, 1515, 0, SkinshifterSpells::SkillSkinshifter, "Kindred Skin",
        "Slide inside the target and make it kin: you play the creature on your own side, with its health, its "
        "speed and its abilities and more from your Grimoire. Rares and elites cost Skin Tokens; bosses will not be "
        "made kin. When you step out, it wanders home, unless the fighting wore it below a quarter of its health: "
        "then it dies, and its kill and loot are yours.",
        "", ShapeTakeover },
    { SkinshifterSpells::HostileTakeover, 921, 605, 0, SkinshifterSpells::SkillSkinshifter, "Hostile Takeover",
        "PvPvE ability. "
        "Pour your spirit into the target: you play the creature, with its health, its speed and its abilities and "
        "more from your Grimoire. It stays a monster: players can hunt you, and you them. Rares, elites and bosses "
        "cost Soul Tokens. When you leave, the creature dies and its kill and loot are yours.",
        "", ShapeTakeover },
    { SkinshifterSpells::WillingHost, 921, 1098, 0, SkinshifterSpells::SkillSkinshifter, "Willing Host",
        "Pour your spirit into the target and make it a willing host: you play the creature on your own side, with "
        "its health, its speed and its abilities and more from your Grimoire. Rares and elites cost Soul Tokens; "
        "bosses are never willing. When you leave, it wanders home, unless the fighting wore it below a quarter of "
        "its health: then it dies, and its kill and loot are yours.",
        "", ShapeTakeover },
    { SkinshifterSpells::SteedReins, 605, 605, 0, 0, "Reins",
        "Your passenger holds the reins.", "Your passenger steers. Move to take the reins back.", ShapeTakeoverControl },
    { SkinshifterSpells::TakeoverControl, 605, 605, 0, 0, "Takeover",
        "You are inside the creature.", "Inside a creature. Cancel to step out.", ShapeTakeoverControl },
    { SkinshifterSpells::TakeoverReset, 921, 8690, 0, 0, "Reset",
        "Out of combat: the creature runs back to where it lives, heals up, and you steer it again.", "",
        ShapeTakeoverReset },
    { SkinshifterSpells::HydraHead, 57755, 102137, 0, SkinshifterSpells::SkillBeastskin, "Hydra Head",
        "Three heads of a stolen hydra spit at the target, one after another. Each bolt is a random Poison Bolt "
        "(Nature), Arcane Missile (Arcane) or Sand Speckle (Physical), dealing $1990030s1 damage.", "", ShapeHydraHead },
    { SkinshifterSpells::HydraPoison, 21067, 21067, 0, 0, "Poison Bolt", "A hydra head's bolt of poison.", "",
        ShapeHydraPoison },
    { SkinshifterSpells::HydraArcane, 7268, 7268, 0, 0, "Arcane Missile", "A hydra head's missile of arcane power.",
        "", ShapeHydraArcane },
    { SkinshifterSpells::HydraSand, 21067, 255645, 0, 0, "Sand Speckle", "A hydra head's spray of scouring sand.", "",
        ShapeHydraSand },
    { SkinshifterSpells::Cerberus, 57755, 54049, 0, SkinshifterSpells::SkillFelstriker, "Cerberus",
        "The three heads of the hound of the underworld bite the target in turn: $1990034m1% weapon damage as "
        "Physical, then as Fire, then as Shadow.", "", ShapeCerberus },
    { SkinshifterSpells::CerberusBite, 17253, 17253, 0, 0, "Cerberus: Bite", "A head of Cerberus bites.", "",
        ShapeCerberusBite },
    { SkinshifterSpells::CerberusFire, 256492, 256492, 0, 0, "Cerberus: Fire Bite", "A head of Cerberus burns.", "",
        ShapeCerberusFire },
    { SkinshifterSpells::CerberusShadow, 54049, 54049, 0, 0, "Cerberus: Shadow Bite", "A head of Cerberus withers.",
        "", ShapeCerberusShadow },
    { SkinshifterSpells::TwinFangsStrike, 16459, 16459, 0, 0, "Twin Fangs", "Your weapon strikes again.", "",
        ShapeTwinFangsStrike },
    { SkinshifterSpells::BorrowedShadow, 686, 686, 0, SkinshifterSpells::SkillFelstriker, "Borrowed Shadow",
        "Hurl a borrowed bolt of demon shadow, causing $s1 Shadow damage.", "", ShapeStarterCaster },
    { SkinshifterSpells::StolenSpark, 133, 133, 0, SkinshifterSpells::SkillElementalist, "Stolen Spark",
        "Throw a borrowed spark of elemental fire, causing $s1 Fire damage.", "", ShapeStarterCaster },
    { SkinshifterSpells::SkinStride, 58875, 58875, 0, 0, "Steed Shift",
        "You shift into the shape of your mount and run as it does.", "Shifted into your mount. Movement speed "
        "increased by $s1%.", ShapeSteed },
    { SkinshifterSpells::PossessedSteed, 48265, 48265, 0, 0, "Possessed Steed",
        "You slip into your mount instead of onto it and run in its body.",
        "Possessing your mount. Movement speed increased by $s1%.", ShapeSteed },
    { SkinshifterSpells::WraithRunning, 48265, 48265, 0, 0, "Wraith Glide",
        "You glide along as a wraith, through walls and trees, trailing shadow.",
        "A wraith, gliding through obstacles. Movement speed increased by $s1%.", ShapeWraithGlide },
    { SkinshifterSpells::PhaseRunning, 254226, 254226, 0, 0, "Phase Running",
        "You race along half out of this world, crackling with energy.", "Movement speed increased by $s1%.",
        ShapeRootWalking },
    { SkinshifterSpells::WraithRun, 48265, 48265, 0, SkinshifterSpells::SkillSkinshifter, "Wraith Glide",
        "Become a wraith and glide at the speed of a mount, through walls, trees and fences; only the ground stops "
        "you. Attacking, casting or taking damage ends it. Cannot be used in combat; start it outdoors.", "",
        ShapeRootWalk },
    { SkinshifterSpells::PhaseRun, 254226, 254226, 0, SkinshifterSpells::SkillSkinshifter, "Phase Running",
        "Race along half out of this world at the speed of a mount. Attacking, casting or taking damage ends it. "
        "Cannot be used in combat.", "", ShapeRootWalk },
    { SkinshifterSpells::RootWalk, 26381, 26381, 0, SkinshifterSpells::SkillSkinshifter, "Root Walk",
        "Dig into the ground and travel under it at the speed of a mount, under buildings, trees and fences. Standing "
        "still you come up as a herb or ore vein that grows nearby. Creatures leave you alone. Attacking, casting or "
        "taking damage brings you back up. Cannot be used in combat or indoors.", "", ShapeRootWalk },
    { SkinshifterSpells::RootWalking, 26381, 26381, 0, 0, "Root Walking",
        "You dig into the ground.", "Underground. Movement speed increased by $s1%.", ShapeRootWalking },
    { SkinshifterSpells::Lure, 57755, 41152, 0, SkinshifterSpells::SkillSkinshifter, "Lure",
        "Throw a pilfered blade at the target, causing $s1 Physical damage and drawing it to you.", "",
        ShapeLure },
    { SkinshifterSpells::BorrowedHide, 16929, 16929, 0, 0, "Borrowed Hide",
        "While you wear a beast's skin its hide is yours: health increased by $s1% and armor by $s2%.",
        "Health increased by $s1%. Armor increased by $s2%.", ShapeBorrowedHide },
    { SkinshifterSpells::SkinDragonkin, 16929, 16929, 0, 0, "Dragon Hide",
        "While you wear a dragon's skin: Fire, Frost and Nature damage increased by $s1% and health by $s2%.",
        "Fire, Frost and Nature damage increased by $s1%. Health increased by $s2%.", ShapeTypeBuff },
    { SkinshifterSpells::SkinDemon, 16929, 16929, 0, 0, "Demon Hide",
        "While you wear a demon's skin: Fire and Shadow damage increased by $s1%.",
        "Fire and Shadow damage increased by $s1%.", ShapeTypeBuff },
    { SkinshifterSpells::SkinElemental, 16929, 16929, 0, 0, "Elemental Shell",
        "While you wear an elemental's form: Fire, Frost and Nature damage increased by $s1%.",
        "Fire, Frost and Nature damage increased by $s1%.", ShapeTypeBuff },
    { SkinshifterSpells::SkinGiant, 16929, 16929, 0, 0, "Giant Hide",
        "While you wear a giant's skin: health increased by $s1%.", "Health increased by $s1%.", ShapeTypeBuff },
    { SkinshifterSpells::SkinUndead, 16929, 16929, 0, 0, "Grave Skin",
        "While you wear one of the dead: Shadow damage increased by $s1% and Shadow resistance by $s2.",
        "Shadow damage increased by $s1%. Shadow resistance increased by $s2.", ShapeTypeBuff },
    { SkinshifterSpells::SkinHumanoid, 16929, 16929, 0, 0, "Borrowed Face",
        "While you wear a person's face: healing done increased by $s1% and all stats by $s2%.",
        "Healing done increased by $s1%. All stats increased by $s2%.", ShapeTypeBuff },
    { SkinshifterSpells::SkinMechanical, 16929, 16929, 0, 0, "Iron Shell",
        "While you wear a machine's shell: armor increased by $s1%.", "Armor increased by $s1%.", ShapeTypeBuff },
    { SkinshifterSpells::EchoBeast, 16929, 16929, 0, 0, "Beast Echo",
        "The beast you left lingers: attack speed increased by $s1% and movement speed by $s2% for $d.",
        "Attack speed increased by $s1%. Movement speed increased by $s2%.", ShapeTypeBuff },
    { SkinshifterSpells::EchoDragonkin, 16929, 16929, 0, 0, "Dragon Echo",
        "The dragon you left lingers: Fire, Frost and Nature damage increased by $s1% for $d.",
        "Fire, Frost and Nature damage increased by $s1%.", ShapeTypeBuff },
    { SkinshifterSpells::EchoDemon, 16929, 16929, 0, 0, "Demon Echo",
        "The demon you left lingers: Fire and Shadow damage increased by $s1% for $d.",
        "Fire and Shadow damage increased by $s1%.", ShapeTypeBuff },
    { SkinshifterSpells::EchoElemental, 16929, 16929, 0, 0, "Elemental Echo",
        "The elemental you left lingers: Fire, Frost and Nature damage increased by $s1% for $d.",
        "Fire, Frost and Nature damage increased by $s1%.", ShapeTypeBuff },
    { SkinshifterSpells::EchoGiant, 16929, 16929, 0, 0, "Giant Echo",
        "The giant you left lingers: health increased by $s1% for $d.", "Health increased by $s1%.", ShapeTypeBuff },
    { SkinshifterSpells::EchoUndead, 16929, 16929, 0, 0, "Undead Echo",
        "The dead you left linger: Shadow damage increased by $s1% and Shadow resistance by $s2 for $d.",
        "Shadow damage increased by $s1%. Shadow resistance increased by $s2.", ShapeTypeBuff },
    { SkinshifterSpells::EchoHumanoid, 16929, 16929, 0, 0, "Humanoid Echo",
        "The face you left lingers: healing done increased by $s1% for $d.", "Healing done increased by $s1%.",
        ShapeTypeBuff },
    { SkinshifterSpells::EchoMechanical, 16929, 16929, 0, 0, "Mechanical Echo",
        "The shell you left lingers: armor increased by $s1% for $d.", "Armor increased by $s1%.", ShapeTypeBuff },
    { SkinshifterSpells::LingeringWound, 13443, 13443, 0, 0, "Lingering Wound",
        "Bleeding for $o1 over $d.", "Bleeding for $o1 over $d.", ShapeLingeringWound },
    { SkinshifterSpells::SavageRend, 13443, 13443, 0, SkinshifterSpells::SkillBeastskin, "Savage Rend",
        "Tear into the target like the beasts you wear, causing it to bleed for $o1 over $d.", "Bleeding.",
        ShapeSignature },
    { SkinshifterSpells::BorrowedGrace, 2061, 2061, 0, SkinshifterSpells::SkillCourtier, "Borrowed Grace",
        "A quick heal in the manner of the healers you have studied, healing the target for $s1.", "",
        ShapeSignature },
    { SkinshifterSpells::SoulSiphon, 689, 689, 0, SkinshifterSpells::SkillFelstriker, "Soul Siphon",
        "Draw the life out of the target in the way of demons and the dead, $s1 every $t1 sec for $d, healing you.",
        "Life siphoned.", ShapeSignature },
    { SkinshifterSpells::StormTheft, 403, 403, 0, SkinshifterSpells::SkillElementalist, "Storm Theft",
        "Hurl a bolt of stolen lightning at the target, causing $s1 Nature damage.", "", ShapeSignature },
    { SkinshifterSpells::FeralSkins, 20598, 9634, 0, SkinshifterSpells::SkillBeastskin, "Feral Skins",
        "Your physical damage is increased by $s1%.", "", ShapeFeralSkins },
    { SkinshifterSpells::SilverTongue, 20598, 2060, 0, SkinshifterSpells::SkillCourtier, "Silver Tongue",
        "Your healing is increased by $s1%.", "", ShapeSilverTongue },
    { SkinshifterSpells::FelTouched, 20598, 1949, 0, SkinshifterSpells::SkillFelstriker, "Fel-Touched",
        "Your Shadow and Fire damage is increased by $s1%.", "", ShapeFelTouched },
    { SkinshifterSpells::ElementalAttunement, 20598, 30706, 0, SkinshifterSpells::SkillElementalist,
        "Elemental Attunement", "Your Fire, Frost and Nature damage is increased by $s1%.", "",
        ShapeElementalAttunement },
    FX_AURA(FxGhostVisual, 9617, "Skinshifter FX: Ghost Visual"),
    FX_AURA(FxGhostBlueFog, 35838, "Skinshifter FX: Ghost Blue Fog"),
    FX_AURA(FxGhostFogSheer, 256802, "Skinshifter FX: Ghost Fog Transparent"),
    FX_AURA(FxGhostHalfSeen, 44816, "Skinshifter FX: Transparency 50%"),
    FX_AURA(FxGhostCosmetic, 136151, "Skinshifter FX: Ghost Visual Effect"),
    FX_AURA(FxBlueGlow, 22576, "Skinshifter FX: Blue Glow"),  // its look: Skinshifter.Fx.BlueGlowVisual
    FX_AURA(FxEtherealVisual, 254226, "Skinshifter FX: Ethereal Visual"),
    FX_AURA(FxEtherealBeacon, 32368, "Skinshifter FX: Ethereal Beacon"),
    FX_AURA(FxPurpleGhost, 47840, "Skinshifter FX: Purple Ghost"),
    FX_AURA(FxOverwhelmEnergy, 254733, "Skinshifter FX: Overwhelming Energy"),
    FX_AURA(FxSpiritPulse, 84522, "Skinshifter FX: Spirit Pulse"),
    FX_AURA(FxEtherealRingStart, 40952, "Skinshifter FX: Ethereal Burst"),
    FX_MISSILE(FxSoulTransfer, 257619, "Skinshifter FX: Soul Transfer"),
    FX_MISSILE(FxDrawnSoul, 2144741, "Skinshifter FX: Drawn Soul"),
    FX_MISSILE(FxSoulReaped, 288816, "Skinshifter FX: Soul Reaped"),
    FX_MISSILE(FxEtherealBolt, 40758, "Skinshifter FX: Ethereal Bolt"),
    FX_MISSILE(FxEnergyBall, 257471, "Skinshifter FX: Energy Ball"),
    FX_MISSILE(FxStrangeEnergy, 2147502, "Skinshifter FX: Strange Energy"),
}};

#undef FX_AURA
#undef FX_MISSILE

// Spells that take their look from another spell: Slip Between Worlds keeps Stealth's effect but shows CoA's
// "Cosmetic - Transparency (90%)", so a Possessor is nearly gone even to themselves.
// Stolen Vigor wears Corruption's shadow on the target, not Renew's holy light.
std::map<uint32, uint32> const VisualFrom = { { SkinshifterSpells::SlipBetweenWorlds, 2180249 },
    { SkinshifterSpells::StolenVigor, 172 }, { SkinshifterSpells::Lure, 54071 },
    { SkinshifterSpells::HydraSand, 255645 } };

constexpr uint32 WraithWalkIcon = 14326;
constexpr uint32 DigVisual = 9900004;  // tools/mkskinclient.py DIG_VISUAL

// The Skinshifter's own spells are in spell family 16 (SkinshifterFamily), each with a bit of its own, so talents can
// change them with spell modifiers (the client works out the tooltips with the same bits).
std::map<uint32, uint32> const FamilyBitOf =
{
    { SkinshifterSpells::StealForm, SkinshifterSpells::BitStealForm },
    { SkinshifterSpells::Possess, SkinshifterSpells::BitPossess },
    { SkinshifterSpells::ShedSkin, SkinshifterSpells::BitShedSkin },
    { SkinshifterSpells::Release, SkinshifterSpells::BitRelease },
    { SkinshifterSpells::MimicStrike, SkinshifterSpells::BitMimicStrike },
    { SkinshifterSpells::Lure, SkinshifterSpells::BitLure },
    { SkinshifterSpells::StolenVigor, SkinshifterSpells::BitStolenVigor },
    { SkinshifterSpells::SlipAway, SkinshifterSpells::BitSlipAway },
    { SkinshifterSpells::SlipBetweenWorlds, SkinshifterSpells::BitSlipBetweenWorlds },
    { SkinshifterSpells::SkinCollection, SkinshifterSpells::BitSkinCollection },
    { SkinshifterSpells::RootWalk, SkinshifterSpells::BitRootWalk },
    { SkinshifterSpells::WraithRun, SkinshifterSpells::BitWraithRun },
    { SkinshifterSpells::PhaseRun, SkinshifterSpells::BitPhaseRun },
    { SkinshifterSpells::SavageRend, SkinshifterSpells::BitSavageRend },
    { SkinshifterSpells::BorrowedGrace, SkinshifterSpells::BitBorrowedGrace },
    { SkinshifterSpells::SoulSiphon, SkinshifterSpells::BitSoulSiphon },
    { SkinshifterSpells::StormTheft, SkinshifterSpells::BitStormTheft },
};

void SetFamily(Record& r, std::array<uint32, 3> const& flags)
{
    r.values[SpellField::SpellFamilyName] = SkinshifterSpells::SkinshifterFamily;
    for (uint32 word = 0; word < 3; ++word)
        r.values[SpellField::SpellFamilyFlags + word] = flags[word];
}

// A talent rank: a passive aura on yourself; a spell modifier reaches the spells in `affects` (family 16).
constexpr uint32 TalentTemplate = 20598;  // a racial passive, as the identity passives
void ShapeTalent(Record& r, SkinshifterSpells::TalentDef const& talent, uint32 rank)
{
    Passive(r, talent.aura, talent.amountPerRank * int32(rank), uint32(talent.misc));
    r.values[SpellField::EquippedItemClass] = uint32(-1);  // all weapons (Predatory Strikes' crit), no item needed
    r.values[SpellField::EquippedItemSubClassMask] = 0;
    r.values[SpellField::EquippedItemInventoryTypeMask] = 0;
    r.values[SpellField::Attributes + 1] |= SPELL_ATTR1_NO_THREAT;
    // a talent's rank lives in the talent tree, not in the spellbook's General tab (nor on the buff bar)
    r.values[SpellField::Attributes] |= SPELL_ATTR0_DO_NOT_DISPLAY;
    SetFamily(r, {});
    for (uint32 word = 0; word < 3; ++word)
        r.values[SpellField::SpellClassMask + word] = talent.affects[word];
    // a set's other kinds: dummies whose amounts the tooltip shows as $s2 and $s3 (the module reads the talent)
    for (auto const& [index, kind, amount] : { std::tuple<uint32, uint8, int32>{ 1, talent.upgrade2, talent.amount2 },
             std::tuple<uint32, uint8, int32>{ 2, talent.upgrade3, talent.amount3 } })
    {
        if (!kind)
            continue;
        r.values[SpellField::Effect + index] = EFFECT_APPLY_AURA;
        r.values[SpellField::EffectAura + index] = SPELL_AURA_DUMMY;
        r.values[SpellField::EffectTargetA + index] = TARGET_CASTER;
        r.values[SpellField::EffectDieSides + index] = 1;
        r.values[SpellField::EffectBasePoints + index] = uint32(amount * int32(rank) - 1);
    }
    // a chance on each auto attack to cast `misc` (Twin Fangs): the chance grows with the rank
    if (talent.aura == SPELL_AURA_PROC_TRIGGER_SPELL)
    {
        r.values[SpellField::ProcFlags] = PROC_FLAG_DONE_MELEE_AUTO_ATTACK;
        r.values[SpellField::ProcChance] = uint32(talent.amountPerRank * int32(rank));
        r.values[SpellField::ProcCharges] = 0;
        r.values[SpellField::EffectBasePoints] = 0;
        r.values[SpellField::EffectDieSides] = 0;
        r.values[SpellField::EffectMiscValue] = 0;
        r.values[SpellField::EffectTriggerSpell] = uint32(talent.misc);
    }
}

std::map<uint32, Record> ReadSpellRecords(std::string const& path, std::set<uint32> const& ids)
{
    std::map<uint32, Record> found;
    std::ifstream file(path, std::ios::binary);
    uint32 header[5] = {};
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header)) || std::memcmp(header, "WDBC", 4) != 0 ||
        header[2] != SpellFields || header[3] != SpellFields * 4)
        return found;
    uint32 const count = header[1];
    std::streamoff const stringBlock = 20 + std::streamoff(count) * header[3];
    std::vector<uint32> record(SpellFields);
    for (uint32 row = 0; row < count && found.size() < ids.size(); ++row)
    {
        file.seekg(20 + std::streamoff(row) * header[3]);
        if (!file.read(reinterpret_cast<char*>(record.data()), header[3]))
            break;
        if (!ids.count(record[0]))
            continue;
        Record& out = found[record[0]];
        std::copy(record.begin(), record.end(), out.values.begin());
        for (uint32 field : { NameField, RankField, DescriptionField, ToolTipField })
        {
            std::string text;
            file.clear();
            file.seekg(stringBlock + record[field]);
            std::getline(file, text, '\0');
            out.strings[field] = text;
        }
    }
    return found;
}

std::vector<std::string> SpellDbcColumnTypes()
{
    std::vector<std::string> types;
    if (QueryResult result = WorldDatabase.Query("SELECT COLUMN_TYPE FROM information_schema.COLUMNS WHERE "
        "TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'spell_dbc' ORDER BY ORDINAL_POSITION"))
    {
        do
            types.push_back(result->Fetch()[0].Get<std::string>());
        while (result->NextRow());
    }
    return types;
}

std::string Escaped(std::string text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '\'' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

bool IsTextType(std::string const& type)
{
    return type.find("char") != std::string::npos || type.find("text") != std::string::npos;
}

// One line per spell: the 234 raw fields (string fields as 0), then name, rank, description, tooltip and the
// spellbook tab (skill line, 0 = none).
std::string ClientRow(Record const& r, uint32 skillLine)
{
    std::string line;
    for (uint32 field = 0; field < SpellFields; ++field)
    {
        bool const text = field >= NameField && field < ToolTipField + LocalizedDwords &&
            (field - NameField) % LocalizedDwords != LocalizedDwords - 1;  // the locale strings, not their masks
        line += Acore::StringFormat("{}{}", field ? "," : "", text ? 0u : r.values[field]);
    }
    for (uint32 field : { NameField, RankField, DescriptionField, ToolTipField })
    {
        auto const found = r.strings.find(field);
        // escaped, so a row is always one line with five tab-separated parts (CoA's texts hold line breaks)
        std::string value;
        for (char c : found == r.strings.end() ? std::string() : found->second)
        {
            switch (c)
            {
                case '\\': value += "\\\\"; break;
                case '\n': value += "\\n"; break;
                case '\r': value += "\\r"; break;
                case '\t': value += "\\t"; break;
                default: value += c; break;
            }
        }
        line += "\t" + value;
    }
    return line + Acore::StringFormat("\t{}\n", skillLine);
}

std::string RowValues(Record const& r, std::vector<std::string> const& types)
{
    std::string values;
    for (uint32 field = 0; field < SpellFields; ++field)
    {
        std::string const& type = types[field];
        if (field)
            values += ", ";
        if (IsTextType(type))
        {
            auto const text = r.strings.find(field);
            values += "'" + Escaped(text == r.strings.end() ? "" : text->second) + "'";
        }
        else if (SpellEntryfmt[field] == 'f' || type.find("float") != std::string::npos ||
            type.find("double") != std::string::npos)
            values += Acore::StringFormat("{}", std::bit_cast<float>(r.values[field]));
        else if (type.find("unsigned") != std::string::npos)
            values += Acore::StringFormat("{}", r.values[field]);
        else
            values += Acore::StringFormat("{}", int32(r.values[field]));
    }
    return values;
}
}

namespace SkinshifterSpells
{
bool IsPossessorRace(uint8 race)
{
    std::string const list = sConfigMgr->GetOption<std::string>("Skinshifter.Class.PossessorRaces", "90,109");
    for (std::string_view token : Acore::Tokenize(list, ',', false))
        if (Optional<uint32> id = Acore::StringTo<uint32>(token); id && *id == race)
            return true;
    return false;
}

// Possessor races that are energy rather than ghosts (Skinshifter.Fx.EnergyRaces): their effects and their travel.
bool IsEnergyRace(uint8 race)
{
    std::string const list = sConfigMgr->GetOption<std::string>("Skinshifter.Fx.EnergyRaces", "109");
    for (std::string_view token : Acore::Tokenize(list, ',', false))
        if (Optional<uint32> id = Acore::StringTo<uint32>(token); id && *id == race)
            return true;
    return false;
}

std::vector<uint32> FormerStarterSpells()
{
    return { BorrowedShadow, StolenSpark };
}

std::vector<std::pair<uint32, uint32>> PossessorSwaps()
{
    return { { StealForm, Possess }, { ShedSkin, Release }, { SlipAway, SlipBetweenWorlds },
        { Turnskin, HostileTakeover }, { KindredSkin, WillingHost } };
}

uint8 TakeoverLevel()
{
    return uint8(std::clamp<uint32>(sConfigMgr->GetOption<uint32>("Skinshifter.Takeover.Level", 20), 1, 255));
}

std::vector<uint32> StarterSpells(uint8 race, uint8 level)
{
    std::vector<uint32> ids = { StealForm, ShedSkin, SkinCollection, SlipAway, MimicStrike, Lure, StolenVigor };
    if (level >= TakeoverLevel() && sConfigMgr->GetOption<bool>("Skinshifter.Takeover.Enable", true))
    {
        ids.push_back(Turnskin);
        ids.push_back(KindredSkin);
    }
    // the travel form: a Skinshifter digs as a worm, a Possessor runs as a wraith or as energy
    ids.push_back(!IsPossessorRace(race) ? RootWalk : IsEnergyRace(race) ? PhaseRun : WraithRun);
    if (IsPossessorRace(race))
        for (auto const& [skinshifter, possessor] : PossessorSwaps())
            std::replace(ids.begin(), ids.end(), skinshifter, possessor);
    return ids;
}

bool NameIsJunk(std::string const& name)
{
    static std::vector<std::string> const junk =
    {
        "dnd", "credit", "visual", "test", "dummy", "trigger", "bunny", "quest", "kill ", "despawn", "teleport",
        "transform", "cosmetic", "ride ", "vehicle", "summon ", "script", "zzold", "[ph]", "unused", "deprecated",
        "force target", "sleeping", "letter"
    };
    std::string lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return std::tolower(c); });
    return std::any_of(junk.begin(), junk.end(), [&](std::string const& word) { return lowered.find(word) != std::string::npos; });
}

bool IsForbiddenEffect(uint32 effect)
{
    static std::set<uint32> const forbidden =
    {
        SPELL_EFFECT_INSTAKILL, SPELL_EFFECT_TELEPORT_UNITS, SPELL_EFFECT_SUMMON, SPELL_EFFECT_KILL_CREDIT,
        SPELL_EFFECT_KILL_CREDIT2, SPELL_EFFECT_QUEST_COMPLETE, SPELL_EFFECT_SEND_EVENT, SPELL_EFFECT_CREATE_ITEM,
        SPELL_EFFECT_OPEN_LOCK, SPELL_EFFECT_SUMMON_OBJECT_WILD, SPELL_EFFECT_TRANS_DOOR, SPELL_EFFECT_FORCE_CAST,
        SPELL_EFFECT_FORCE_CAST_WITH_VALUE, SPELL_EFFECT_TELEPORT_GRAVEYARD, SPELL_EFFECT_SUMMON_PLAYER,
        SPELL_EFFECT_RESURRECT_NEW, SPELL_EFFECT_SELF_RESURRECT, SPELL_EFFECT_LEARN_SPELL
    };
    return forbidden.count(effect) != 0;
}

bool IsForbiddenAura(uint32 aura)
{
    static std::set<uint32> const forbidden =
    {
        SPELL_AURA_CONTROL_VEHICLE, SPELL_AURA_TRANSFORM, SPELL_AURA_MOD_SHAPESHIFT, SPELL_AURA_MOD_POSSESS,
        SPELL_AURA_PHASE, SPELL_AURA_MOD_INVISIBILITY, SPELL_AURA_FLY, SPELL_AURA_MOD_SCALE,
        SPELL_AURA_CLONE_CASTER, SPELL_AURA_MOD_FEAR, SPELL_AURA_FEIGN_DEATH
    };
    return forbidden.count(aura) != 0;
}

// The spellbook tab of a stolen ability: one "Skins" tab for them all (the spellbook's search finds one among many;
// the spec tabs hold the spec's own abilities). The per-type lines stay defined but hold nothing.
uint32 StolenSkillLine(uint32 /*creatureType*/)
{
    return SkillStolenOther;
}

namespace
{
std::unordered_map<uint32, uint32> CopyByOriginal;
std::unordered_map<uint32, uint32> OriginalByCopy;
std::vector<CopyInfo> CopyInfos;  // every copy with its creature type and level (the takeover kit)

// Which creatures use a spell: how many of each type, and the lowest level among them.
struct CopyCandidate
{
    std::map<uint32, uint32> types;
    uint32 level = 0;

    void Add(uint32 type, uint32 creatureLevel)
    {
        ++types[type];
        if (creatureLevel && (!level || creatureLevel < level))
            level = creatureLevel;
    }

    uint32 MainType() const
    {
        auto const best = std::max_element(types.begin(), types.end(),
            [](auto const& a, auto const& b) { return a.second < b.second; });
        return best == types.end() ? CREATURE_TYPE_NOT_SPECIFIED : best->first;
    }
};

std::map<uint32, CopyCandidate> CollectCopyCandidates()
{
    std::map<uint32, CopyCandidate> candidates;
    auto const add = [&](QueryResult result)
    {
        if (!result)
            return;
        do
        {
            Field* fields = result->Fetch();
            if (uint32 const spell = fields[0].Get<uint32>())
                candidates[spell].Add(fields[1].Get<uint32>(), fields[2].Get<uint32>());
        } while (result->NextRow());
    };
    add(WorldDatabase.Query("SELECT s.`Spell`, t.`type`, t.`minlevel` FROM `creature_template_spell` s "
        "JOIN `creature_template` t ON t.`entry` = s.`CreatureID`"));
    std::string const casts = Acore::StringFormat("({}, {}, {}, {})", uint32(SMART_ACTION_CAST),
        uint32(SMART_ACTION_SELF_CAST), uint32(SMART_ACTION_CROSS_CAST), uint32(SMART_ACTION_INVOKER_CAST));
    add(WorldDatabase.Query("SELECT ss.`action_param1`, t.`type`, t.`minlevel` FROM `smart_scripts` ss "
        "JOIN `creature_template` t ON t.`entry` = ss.`entryorguid` WHERE ss.`source_type` = 0 AND ss.`entryorguid` > 0 "
        "AND ss.`action_type` IN {}", casts));
    add(WorldDatabase.Query("SELECT ss.`action_param1`, t.`type`, t.`minlevel` FROM `smart_scripts` ss "
        "JOIN `creature` c ON c.`guid` = -ss.`entryorguid` JOIN `creature_template` t ON t.`entry` = c.`id` "
        "WHERE ss.`source_type` = 0 AND ss.`entryorguid` < 0 AND ss.`action_type` IN {}", casts));
    for (auto const& [spell, type] : StarterCreatureSpells())
        if (spell < StealForm || spell > CopyLast)
            candidates[spell].Add(type, 1);
    return candidates;
}

// Can a player use this creature spell at all (the same rules as Steal Form, on the raw Spell.dbc record)?
bool IsSelfDisabling(uint32 aura)
{
    return aura == SPELL_AURA_MOD_STUN || aura == SPELL_AURA_MOD_ROOT || aura == SPELL_AURA_MOD_PACIFY ||
        aura == SPELL_AURA_MOD_SILENCE || aura == SPELL_AURA_MOD_PACIFY_SILENCE || aura == SPELL_AURA_MOD_CONFUSE ||
        aura == SPELL_AURA_MOD_FEAR;
}

// ---- icons -------------------------------------------------------------------------------------------------------
// A creature spell's SpellIcon can be 0, a row the client lacks or a texture it lacks: the spellbook then shows an
// empty square that cannot be dragged. Such a copy gets an icon of its school. Textures the client lacks are listed
// by tools\mkskinclient.py in Data\skinshifter_bad_icons.txt (one SpellIcon id a line).
std::map<uint32, std::string> ReadSpellIcons(std::string const& path)
{
    std::map<uint32, std::string> icons;
    std::ifstream file(path, std::ios::binary);
    uint32 header[5] = {};
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header)) || std::memcmp(header, "WDBC", 4) != 0 ||
        header[2] != 2)
        return icons;
    std::vector<uint32> rows(std::size_t(header[1]) * 2);
    if (!file.read(reinterpret_cast<char*>(rows.data()), std::streamsize(rows.size() * 4)))
        return icons;
    std::string strings(header[4], '\0');
    file.read(strings.data(), header[4]);
    for (std::size_t row = 0; row < header[1]; ++row)
        if (rows[row * 2 + 1] < strings.size())
        {
            std::string texture = strings.c_str() + rows[row * 2 + 1];
            std::transform(texture.begin(), texture.end(), texture.begin(), ::tolower);
            icons[rows[row * 2]] = texture;
        }
    return icons;
}

struct IconFixer
{
    std::map<uint32, std::string> textures;
    std::set<uint32> bad;
    std::map<uint32, uint32> bySchool;  // school bit -> icon id
    uint32 heal = 0;
    uint32 fallback = 0;

    uint32 Find(std::string const& name) const
    {
        std::string const wanted = "interface\\icons\\" + name;
        for (auto const& [id, texture] : textures)
            if (texture == wanted && !bad.count(id))
                return id;
        return 0;
    }

    void Load(std::string const& dataDir)
    {
        textures = ReadSpellIcons(dataDir + "dbc/SpellIcon.dbc");
        std::ifstream list(dataDir + "skinshifter_bad_icons.txt");
        for (uint32 id; list >> id;)
            bad.insert(id);
        char const* const schools[] = { "ability_meleedamage", "spell_holy_holybolt", "spell_fire_flamebolt",
            "spell_nature_lightning", "spell_frost_frostbolt02", "spell_shadow_shadowbolt", "spell_arcane_blast" };
        for (uint32 bit = 0; bit < 7; ++bit)
            bySchool[bit] = Find(schools[bit]);
        heal = Find("spell_holy_heal");
        fallback = Find("inv_misc_questionmark");
    }

    bool Missing(uint32 icon) const
    {
        auto const texture = textures.find(icon);
        return !icon || bad.count(icon) || texture == textures.end() || texture->second.empty() ||
            texture->second.ends_with("\\temp");
    }

    void Fix(Record& r) const
    {
        if (textures.empty() || !Missing(r.values[SpellField::SpellIcon]))
            return;
        uint32 icon = 0;
        for (uint32 index = 0; index < 3 && !icon; ++index)
        {
            uint32 const effect = r.values[SpellField::Effect + index];
            uint32 const aura = r.values[SpellField::EffectAura + index];
            if (effect == SPELL_EFFECT_HEAL || aura == SPELL_AURA_PERIODIC_HEAL)
                icon = heal;
        }
        for (uint32 bit = 0; bit < 7 && !icon; ++bit)
            if (r.values[SpellField::SchoolMask] & (1u << bit))
                icon = bySchool.at(bit);
        r.values[SpellField::SpellIcon] = icon ? icon : fallback ? fallback : 1;
    }
};

// Hurts or kills the caster (Suicide, Self Destruct, a creature's self-damage or self-DoT).
bool IsSelfHarming(uint32 effect, uint32 aura, int32 basePoints)
{
    return IsSelfDisabling(aura) || effect == SPELL_EFFECT_INSTAKILL || effect == SPELL_EFFECT_SCHOOL_DAMAGE ||
        effect == SPELL_EFFECT_ENVIRONMENTAL_DAMAGE || effect == SPELL_EFFECT_HEALTH_LEECH ||
        effect == SPELL_EFFECT_POWER_BURN || effect == SPELL_EFFECT_POWER_DRAIN ||
        aura == SPELL_AURA_PERIODIC_DAMAGE || aura == SPELL_AURA_PERIODIC_DAMAGE_PERCENT ||
        aura == SPELL_AURA_PERIODIC_LEECH || aura == SPELL_AURA_MOD_DECREASE_SPEED ||
        aura == SPELL_AURA_MOD_SPEED_SLOW_ALL || (aura == SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN && basePoints >= 0);
}

bool IsCopyable(Record const& r)
{
    if (r.values[SpellField::Attributes] & (SPELL_ATTR0_PASSIVE | SPELL_ATTR0_DO_NOT_DISPLAY))
        return false;
    auto const name = r.strings.find(NameField);
    if (name == r.strings.end() || name->second.empty() || NameIsJunk(name->second))
        return false;
    bool anyUseful = false;
    bool anyHelps = false;
    for (uint32 index = 0; index < 3; ++index)
    {
        uint32 const effect = r.values[SpellField::Effect + index];
        uint32 const aura = r.values[SpellField::EffectAura + index];
        if (!effect)
            continue;
        if (IsForbiddenEffect(effect) || IsForbiddenAura(aura))
            return false;
        if (effect != SPELL_EFFECT_DUMMY && effect != SPELL_EFFECT_SCRIPT_EFFECT && aura != SPELL_AURA_DUMMY)
            anyUseful = true;
        // on yourself only: a creature's own act (Stun Self, Suicide)
        bool const selfOnly = r.values[SpellField::EffectTargetA + index] == TARGET_UNIT_CASTER &&
            !r.values[SpellField::EffectTargetB + index];
        if (selfOnly && effect == SPELL_EFFECT_INSTAKILL)
            return false;
        if (!selfOnly || !IsSelfHarming(effect, aura, int32(r.values[SpellField::EffectBasePoints + index])))
            anyHelps = true;
    }
    // nothing but harm to yourself; a stun with an immunity (Ice Block) is kept
    return anyUseful && anyHelps;
}

bool IsCrowdControlRecord(Record const& r)
{
    static std::set<uint32> const controls =
    {
        SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_SILENCE, SPELL_AURA_MOD_PACIFY,
        SPELL_AURA_MOD_CONFUSE, SPELL_AURA_MOD_CHARM, SPELL_AURA_MOD_PACIFY_SILENCE
    };
    for (uint32 index = 0; index < 3; ++index)
        if (controls.count(r.values[SpellField::EffectAura + index]))
            return true;
    return false;
}

// Damage and healing amounts (direct and over time) grow with the caster's level.
std::map<uint32, int32> ReadCastTimes(std::string const& path)
{
    std::map<uint32, int32> castTimes;
    std::ifstream file(path, std::ios::binary);
    uint32 header[5] = {};
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header)) || std::memcmp(header, "WDBC", 4) != 0 || header[2] < 2)
        return castTimes;
    std::vector<uint32> row(header[2]);
    for (uint32 index = 0; index < header[1]; ++index)
        if (file.read(reinterpret_cast<char*>(row.data()), header[3]))
            castTimes[row[0]] = int32(row[1]);
    return castTimes;
}

// ---- player-like numbers ------------------------------------------------------------------------------------------
// A creature's numbers are made for that creature (a level 2 mob's Strike can be a raid boss's), so a stolen ability,
// and the Skinshifter's own damage and healing, get numbers of their own: what an ability of that kind does at level
// 60, growing evenly from level 1 (1/60 of it) through the level part (RealPointsPerLevel). By kind:
//   direct damage 220 per second of cast (an instant counts as 1.5 sec), direct healing 260 per second,
//   a weapon strike's bonus 70, damage over time 35 a second of its duration (up to 24 sec) in all, healing over
//   time 42, an absorb 600, a damage shield 30 a hit; on several targets 60% of it.
constexpr float LevelCap = 60.0f;

bool IsAreaTarget(uint32 target)
{
    switch (target)
    {
        case 15: case 16: case 22: case 24: case 28: case 30: case 31: case 33: case 37: case 52: case 53: case 54:
            return true;
        default:
            return false;
    }
}

// What effect `index` does at level 60, or 0 when it is not a kind that is set this way.
float LevelCapAmount(Record const& r, uint32 index, int32 castMs, int32 durationMs)
{
    float const cast = std::max(1.5f, float(castMs) / 1000.0f);
    float const duration = std::clamp(float(durationMs) / 1000.0f, 1.0f, 24.0f);
    uint32 const amplitude = r.values[SpellField::EffectAmplitude + index];
    float const ticks = amplitude ? std::max(1.0f, float(durationMs) / float(amplitude)) : 1.0f;
    float amount = 0.0f;
    switch (r.values[SpellField::Effect + index])
    {
        case SPELL_EFFECT_SCHOOL_DAMAGE:
        case SPELL_EFFECT_HEALTH_LEECH:          amount = 220.0f * cast; break;
        case SPELL_EFFECT_HEAL:                  amount = 260.0f * cast; break;
        case SPELL_EFFECT_WEAPON_DAMAGE:
        case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
        case SPELL_EFFECT_NORMALIZED_WEAPON_DMG: amount = 70.0f; break;
        case SPELL_EFFECT_APPLY_AURA:
        case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
        case SPELL_EFFECT_APPLY_AREA_AURA_RAID:
        case SPELL_EFFECT_APPLY_AREA_AURA_FRIEND:
        case SPELL_EFFECT_APPLY_AREA_AURA_ENEMY:
            switch (r.values[SpellField::EffectAura + index])
            {
                case SPELL_AURA_PERIODIC_DAMAGE:
                case SPELL_AURA_PERIODIC_LEECH: amount = 35.0f * duration / ticks; break;
                case SPELL_AURA_PERIODIC_HEAL:  amount = 42.0f * duration / ticks; break;
                case SPELL_AURA_SCHOOL_ABSORB:  amount = 600.0f; break;
                case SPELL_AURA_DAMAGE_SHIELD:  amount = 30.0f; break;
                default: break;
            }
            break;
        default:
            break;
    }
    if (amount > 0.0f && IsAreaTarget(r.values[SpellField::EffectTargetA + index]))
        amount *= 0.6f;
    return amount;
}

// Sets effect `index` to grow evenly to `atCap` at level 60: value = (level / 60) * atCap. With SpellLevel 1 the
// server's amount is EffectBasePoints + (level - 1) * RealPointsPerLevel + a roll of 1..DieSides.
void ScaleEffect(Record& r, uint32 index, float atCap)
{
    float const perLevel = atCap / LevelCap;
    r.values[SpellField::EffectBasePoints + index] = uint32(int32(std::lround(perLevel)) - 1);
    r.values[SpellField::EffectDieSides + index] = 1;
    r.values[SpellField::EffectRealPointsPerLevel + index] = std::bit_cast<uint32>(perLevel);
}

// Gives the damage and healing effects their player-like numbers (LevelCapAmount); `onlyUnscaled` leaves an effect that
// already grows with level as it is (a Skinshifter spell made to grow its own way).
void PlayerLikeNumbers(Record& r, int32 castMs, int32 durationMs, bool onlyUnscaled)
{
    r.values[SpellField::MaxLevel] = 0;
    r.values[SpellField::BaseLevel] = 1;
    r.values[SpellField::SpellLevel] = 1;
    for (uint32 index = 0; index < 3; ++index)
    {
        if (onlyUnscaled && std::bit_cast<float>(r.values[SpellField::EffectRealPointsPerLevel + index]) != 0.0f)
            continue;
        if (float const atCap = LevelCapAmount(r, index, castMs, durationMs); atCap > 0.0f)
            ScaleEffect(r, index, atCap);
    }
}

// A creature's percentage on an enemy (armor, resistances, damage or healing taken or done, stats) becomes what it
// is at level 60, from 1/60 of it at level 1: Pierce Armor's 50% is 4% for a level 5 thief. Speeds and hastes keep
// their numbers (a slow is a slow at any level).
bool IsScaledPercentAura(uint32 aura)
{
    switch (aura)
    {
        case SPELL_AURA_MOD_RESISTANCE_PCT:
        case SPELL_AURA_MOD_DAMAGE_PERCENT_DONE:
        case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
        case SPELL_AURA_MOD_HEALING_PCT:
        case SPELL_AURA_MOD_HEALING_DONE_PERCENT:
        case SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE:
        case SPELL_AURA_MOD_ATTACK_POWER_PCT:
        case SPELL_AURA_MOD_RANGED_ATTACK_POWER_PCT:
            return true;
        default:
            return false;
    }
}

// A flat stat on an enemy (Curse of Thule's -2 Spirit) grows with level like the creature's own: its amount at the
// creature's level, and as much again for every such span of levels.
bool IsFlatStatAura(uint32 aura)
{
    return aura == SPELL_AURA_MOD_STAT || aura == SPELL_AURA_MOD_RESISTANCE || aura == SPELL_AURA_MOD_ATTACK_POWER ||
        aura == SPELL_AURA_MOD_RANGED_ATTACK_POWER || aura == SPELL_AURA_MOD_DAMAGE_DONE;
}

// The debuffs that stack (Skinshifter.Grimoire.DebuffStacks): damage over time and flat stats; never a percentage
// (six Pierce Armors would take all armor and more).
bool IsStackingAura(uint32 aura)
{
    return aura == SPELL_AURA_PERIODIC_DAMAGE || aura == SPELL_AURA_PERIODIC_LEECH ||
        aura == SPELL_AURA_PERIODIC_DAMAGE_PERCENT || IsFlatStatAura(aura);
}

// A creature spell's text often says what it does without its numbers ("Inflicts Shadow damage to an enemy."): the
// copy adds them for each damage or healing effect its text does not already name ($s1, $o2, ...).
std::string SchoolName(uint32 mask)
{
    static std::array<char const*, 7> const names = { "Physical", "Holy", "Fire", "Nature", "Frost", "Shadow", "Arcane" };
    std::string out;
    for (uint32 school = 0; school < names.size(); ++school)
        if (mask & (1u << school))
            out += (out.empty() ? "" : "/") + std::string(names[school]);
    return out.empty() ? "Physical" : out;
}

std::string WithNumbers(std::string text, Record const& r)
{
    std::string const school = SchoolName(r.values[SpellField::SchoolMask]);
    std::string added;
    for (uint32 index = 0; index < 3; ++index)
    {
        uint32 const effect = r.values[SpellField::Effect + index];
        uint32 const aura = r.values[SpellField::EffectAura + index];
        uint32 const n = index + 1;
        auto const named = [&](char token) { return text.find(Acore::StringFormat("${}{}", token, n)) != std::string::npos; };
        if (named('s') || named('o') || named('m'))
            continue;
        std::string line;
        if (effect == SPELL_EFFECT_SCHOOL_DAMAGE)
            line = Acore::StringFormat("Deals $s{} {} damage.", n, school);
        else if (effect == SPELL_EFFECT_HEAL)
            line = Acore::StringFormat("Heals for $s{}.", n);
        else if (effect == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE)
            line = Acore::StringFormat("Deals $s{}% weapon damage.", n);
        else if (effect == SPELL_EFFECT_WEAPON_DAMAGE || effect == SPELL_EFFECT_NORMALIZED_WEAPON_DMG ||
                 effect == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL)
            line = Acore::StringFormat("Deals weapon damage plus $s{}.", n);
        else if (effect == SPELL_EFFECT_APPLY_AURA && aura == SPELL_AURA_PERIODIC_DAMAGE)
            line = Acore::StringFormat("Deals $o{} {} damage over $d.", n, school);
        else if (effect == SPELL_EFFECT_APPLY_AURA && aura == SPELL_AURA_PERIODIC_HEAL)
            line = Acore::StringFormat("Heals for $o{} over $d.", n);
        if (!line.empty())
            added += (added.empty() ? "" : " ") + line;
    }
    if (added.empty())
        return text;
    return text.empty() ? added : text + (text.back() == '.' ? " " : ". ") + added;
}

// Some creature tooltips word a share of weapon damage as a flat bonus: Sonic Burst's "normal damage plus $s1" is
// 50% of your weapon's damage, not weapon damage + 50. The copy says what it does.
std::string WeaponPercentWorded(std::string text, uint32 index)
{
    std::string const token = Acore::StringFormat("$s{}", index + 1);
    for (std::string const phrase : { "normal damage plus ", "weapon damage plus ", "normal damage + " })
    {
        std::string const wrong = phrase + token;
        for (std::size_t at = text.find(wrong); at != std::string::npos; at = text.find(wrong, at))
            text.replace(at, wrong.size(), token + "% weapon damage");
    }
    for (std::size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + token.size()))
    {
        std::size_t const after = at + token.size();
        bool const longerToken = after < text.size() && std::isdigit(static_cast<unsigned char>(text[after]));
        if (!longerToken && (after >= text.size() || text[after] != '%'))
            text.insert(after, "%");
    }
    return text;
}

// "Reduces the Spirit of an enemy by $s1" with a negative amount reads "by -2": the text names the reduction, so the
// copy shows the amount without its sign: "$/-1;s1", the client's division form (as Blizzard's "$/10;s2"); the
// "${$s1*-1}" formula shows as a bare "$" in the client. Amounts in other formulas are left alone.
std::string PositiveWorded(std::string text, uint32 index)
{
    std::string const token = Acore::StringFormat("$s{}", index + 1);
    std::string const positive = Acore::StringFormat("$/-1;s{}", index + 1);
    for (std::size_t at = text.find(token); at != std::string::npos; at = text.find(token, at))
    {
        std::size_t const after = at + token.size();
        bool const longerToken = after < text.size() && std::isdigit(static_cast<unsigned char>(text[after]));
        bool const inFormula = at > 0 && (text[at - 1] == '{' || text[at - 1] == '*' || text[at - 1] == '/' ||
            text[at - 1] == '-' || text[at - 1] == '+');
        if (longerToken || inFormula)
        {
            at = after;
            continue;
        }
        text.replace(at, token.size(), positive);
        at += positive.size();
    }
    return text;
}

// The creature's spell as a Skinshifter ability: no reagents, totems, foci or aura states a player cannot have;
// mana (more for longer casts); a global cooldown; a cooldown for quick abilities (long for crowd control); and
// damage and healing that equal the creature's at the level of the lowest creature that uses it, less below and
// more above.
// ---- limits on stolen spells ------------------------------------------------------------------------------------
// A creature's spell may last forever (it is scripted off again) or reach across the map. A copy keeps an endless
// duration only when it is a buff on you or your allies; anything else gets a cap (Skinshifter.Grimoire.Max*).
std::map<uint32, float> RangeMax;   // SpellRange id -> its longest range (yd)
std::map<uint32, float> RadiusOf;   // SpellRadius id -> radius (yd)

std::map<uint32, float> ReadDbcFloat(std::string const& path, std::initializer_list<uint32> fields)
{
    std::map<uint32, float> out;
    std::ifstream file(path, std::ios::binary);
    uint32 header[5] = {};
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header)) || std::memcmp(header, "WDBC", 4) != 0)
        return out;
    std::vector<uint32> row(header[2]);
    for (uint32 index = 0; index < header[1]; ++index)
        if (file.read(reinterpret_cast<char*>(row.data()), header[3]))
        {
            float value = 0.0f;
            for (uint32 field : fields)
                if (field < row.size())
                    value = std::max(value, std::bit_cast<float>(row[field]));
            out[row[0]] = value;
        }
    return out;
}

void LoadSpellLimits(std::string const& dataDir)
{
    RangeMax = ReadDbcFloat(dataDir + "dbc/SpellRange.dbc", { 3, 4 });   // max range hostile, friendly
    RadiusOf = ReadDbcFloat(dataDir + "dbc/SpellRadius.dbc", { 1, 3 });  // radius, max radius
}

// The spell's range: the shortest standard range that reaches this far (SpellRange.dbc).
void ReachOf(Record& r, float yards)
{
    if (RangeMax.empty())
        LoadSpellLimits(sConfigMgr->GetOption<std::string>("DataDir", "./") + "/");
    uint32 best = 0;
    float bestRange = 0.0f;
    for (auto const& [id, range] : RangeMax)
        if (range >= yards && range <= 100.0f && (!best || range < bestRange))
        {
            best = id;
            bestRange = range;
        }
    if (best)
        r.values[SpellField::RangeIndex] = best;
}

// The row of a table (id -> value) holding exactly this value; 0 when none.
template <typename T>
uint32 RowWith(std::map<uint32, T> const& table, T value)
{
    for (auto const& [id, has] : table)
        if (has == value)
            return id;
    return 0;
}

bool IsFriendlyTarget(uint32 target)
{
    switch (target)
    {
        case TARGET_UNIT_CASTER: case TARGET_UNIT_PET: case TARGET_UNIT_CASTER_AREA_PARTY:
        case TARGET_UNIT_TARGET_ALLY: case TARGET_UNIT_MASTER: case TARGET_UNIT_SRC_AREA_ALLY:
        case TARGET_UNIT_DEST_AREA_ALLY: case TARGET_UNIT_SRC_AREA_PARTY: case TARGET_UNIT_DEST_AREA_PARTY:
        case TARGET_UNIT_TARGET_PARTY: case TARGET_UNIT_LASTTARGET_AREA_PARTY: case TARGET_UNIT_TARGET_CHAINHEAL_ALLY:
        case TARGET_UNIT_CASTER_AREA_RAID: case TARGET_UNIT_TARGET_RAID: case TARGET_UNIT_TARGET_AREA_RAID_CLASS:
            return true;
        default:
            return false;
    }
}

bool IsHarmfulAura(uint32 aura)
{
    switch (aura)
    {
        case SPELL_AURA_PERIODIC_DAMAGE: case SPELL_AURA_PERIODIC_DAMAGE_PERCENT: case SPELL_AURA_PERIODIC_LEECH:
        case SPELL_AURA_PERIODIC_MANA_LEECH: case SPELL_AURA_POWER_BURN: case SPELL_AURA_MOD_DECREASE_SPEED:
        case SPELL_AURA_MOD_SPEED_SLOW_ALL: case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN: case SPELL_AURA_MOD_FEAR:
        case SPELL_AURA_MOD_STUN: case SPELL_AURA_MOD_ROOT: case SPELL_AURA_MOD_SILENCE: case SPELL_AURA_MOD_PACIFY:
        case SPELL_AURA_MOD_PACIFY_SILENCE: case SPELL_AURA_MOD_CONFUSE: case SPELL_AURA_MOD_CHARM:
        case SPELL_AURA_MOD_POSSESS: case SPELL_AURA_MOD_DISARM: case SPELL_AURA_MOD_TAUNT:
            return true;
        default:
            return false;
    }
}

bool IsDamageEffect(uint32 effect)
{
    return effect == SPELL_EFFECT_SCHOOL_DAMAGE || effect == SPELL_EFFECT_WEAPON_DAMAGE ||
        effect == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL || effect == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE ||
        effect == SPELL_EFFECT_NORMALIZED_WEAPON_DMG || effect == SPELL_EFFECT_HEALTH_LEECH ||
        effect == SPELL_EFFECT_POWER_BURN || effect == SPELL_EFFECT_POWER_DRAIN;
}

// A buff: every aura lands on you or your allies and none of them harms.
bool IsBuffRecord(Record const& r)
{
    bool anyAura = false;
    for (uint32 index = 0; index < 3; ++index)
    {
        uint32 const effect = r.values[SpellField::Effect + index];
        if (!effect)
            continue;
        uint32 const aura = r.values[SpellField::EffectAura + index];
        bool const friendlyArea = effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY ||
            effect == SPELL_EFFECT_APPLY_AREA_AURA_RAID || effect == SPELL_EFFECT_APPLY_AREA_AURA_FRIEND ||
            effect == SPELL_EFFECT_APPLY_AREA_AURA_PET || effect == SPELL_EFFECT_APPLY_AREA_AURA_OWNER;
        if (effect == SPELL_EFFECT_APPLY_AURA || friendlyArea)
        {
            anyAura = true;
            if (IsHarmfulAura(aura) || (!friendlyArea && !IsFriendlyTarget(r.values[SpellField::EffectTargetA + index])))
                return false;
        }
        else if (effect == SPELL_EFFECT_PERSISTENT_AREA_AURA || effect == SPELL_EFFECT_APPLY_AREA_AURA_ENEMY ||
            effect == SPELL_EFFECT_SUMMON || IsDamageEffect(effect))
            return false;
    }
    return anyAura;
}

void LimitCopy(Record& r, std::map<uint32, int32> const& durations)
{
    // duration
    auto const duration = durations.find(r.values[SpellField::DurationIndex]);
    int32 const ms = duration == durations.end() ? 0 : duration->second;
    if (ms != 0 && !IsBuffRecord(r))
    {
        bool summon = false, ground = false;
        for (uint32 index = 0; index < 3; ++index)
        {
            summon |= r.values[SpellField::Effect + index] == SPELL_EFFECT_SUMMON;
            ground |= r.values[SpellField::Effect + index] == SPELL_EFFECT_PERSISTENT_AREA_AURA;
        }
        bool const channeled = r.values[SpellField::Attributes + 1] &
            (SPELL_ATTR1_IS_CHANNELED | SPELL_ATTR1_IS_SELF_CHANNELED);
        int32 const cap = int32(sConfigMgr->GetOption<uint32>(
            channeled ? "Skinshifter.Grimoire.MaxChannelDuration" :
            summon ? "Skinshifter.Grimoire.MaxSummonDuration" :
            ground ? "Skinshifter.Grimoire.MaxAreaDuration" :
            IsCrowdControlRecord(r) ? "Skinshifter.Grimoire.MaxCrowdControlDuration" :
            "Skinshifter.Grimoire.MaxDebuffDuration",
            channeled ? 10000 : summon ? 60000 : ground ? 20000 : IsCrowdControlRecord(r) ? 8000 : 30000));
        if (ms < 0 || ms > cap)
        {
            // the longest standard duration that fits under the cap
            uint32 best = 0;
            int32 bestMs = 0;
            for (auto const& [id, length] : durations)
                if (length > bestMs && length <= cap)
                {
                    best = id;
                    bestMs = length;
                }
            if (best)
                r.values[SpellField::DurationIndex] = best;
        }
    }
    // range and radius
    float const maxRange = float(sConfigMgr->GetOption<uint32>("Skinshifter.Grimoire.MaxRange", 40));
    auto const range = RangeMax.find(r.values[SpellField::RangeIndex]);
    if (range != RangeMax.end() && range->second > maxRange)
        if (uint32 const fitting = RowWith(RangeMax, maxRange))
            r.values[SpellField::RangeIndex] = fitting;
    float const maxRadius = float(sConfigMgr->GetOption<uint32>("Skinshifter.Grimoire.MaxRadius", 30));
    for (uint32 index = 0; index < 3; ++index)
    {
        auto const radius = RadiusOf.find(r.values[SpellField::EffectRadiusIndex + index]);
        if (radius != RadiusOf.end() && radius->second > maxRadius)
            if (uint32 const fitting = RowWith(RadiusOf, maxRadius))
                r.values[SpellField::EffectRadiusIndex + index] = fitting;
    }
}

void ShapeCopy(Record& r, uint32 copyId, uint32 referenceLevel, std::map<uint32, int32> const& castTimes,
    std::map<uint32, int32> const& durations)
{
    r.values[0] = copyId;
    for (uint32 field = SpellField::Stances; field <= SpellField::Stances + 3; ++field)
        r.values[field] = 0;
    for (uint32 field = SpellField::CasterAuraState; field <= SpellField::CasterAuraState + 7; ++field)
        r.values[field] = 0;
    r.values[SpellField::RequiresSpellFocus] = 0;
    for (uint32 field = SpellField::Totem; field < SpellField::EquippedItemClass; ++field)
        r.values[field] = 0;  // totems, reagents and their counts
    constexpr uint32 RangedWeapons = (1 << ITEM_SUBCLASS_WEAPON_BOW) | (1 << ITEM_SUBCLASS_WEAPON_GUN) |
        (1 << ITEM_SUBCLASS_WEAPON_CROSSBOW) | (1 << ITEM_SUBCLASS_WEAPON_THROWN) | (1 << ITEM_SUBCLASS_WEAPON_WAND);
    bool const rangedOnly = int32(r.values[SpellField::EquippedItemClass]) == ITEM_CLASS_WEAPON &&
        r.values[SpellField::EquippedItemSubClassMask] && !(r.values[SpellField::EquippedItemSubClassMask] & ~RangedWeapons);
    if (rangedOnly || int32(r.values[SpellField::EquippedItemClass]) == ITEM_CLASS_ARMOR)
    {
        // a creature's Shoot needs no bow, its Shield Bash no shield (the Skinshifter cannot use one)
        r.values[SpellField::EquippedItemClass] = uint32(-1);
        r.values[SpellField::EquippedItemSubClassMask] = 0;
        r.values[SpellField::EquippedItemInventoryTypeMask] = 0;
    }

    auto const castTime = castTimes.find(r.values[SpellField::CastingTimeIndex]);
    int32 const castMs = castTime == castTimes.end() ? 0 : castTime->second;
    bool const channeled = r.values[SpellField::Attributes + 1] & (SPELL_ATTR1_IS_CHANNELED | SPELL_ATTR1_IS_SELF_CHANNELED);
    r.values[SpellField::PowerType] = POWER_MANA;
    r.values[SpellField::ManaCost] = 0;
    r.values[SpellField::ManaCostPerLevel] = 0;
    r.values[SpellField::ManaPerSecond] = 0;
    r.values[SpellField::ManaPerSecondPerLevel] = 0;
    r.values[SpellField::ManaCostPercentage] = channeled ? 5 : castMs <= 0 ? 3 : castMs <= 1500 ? 4 : 6;
    NoRunes(r);

    // A damage over time or flat stat debuff it leaves on an enemy stacks (Skinshifter.Grimoire.DebuffStacks, 6):
    // each use adds a stack, and a Double Take strike adds another, so a use with a second strike is two.
    uint32 const stacks = sConfigMgr->GetOption<uint32>("Skinshifter.Grimoire.DebuffStacks", 6);
    for (uint32 index = 0; index < 3 && stacks > 1; ++index)
    {
        uint32 const target = r.values[SpellField::EffectTargetA + index];
        bool const onEnemy = target == ::TARGET_UNIT_TARGET_ENEMY || target == 15 || target == 16 || target == 24;
        if (r.values[SpellField::Effect + index] == EFFECT_APPLY_AURA && onEnemy &&
            IsStackingAura(r.values[SpellField::EffectAura + index]) && r.values[SpellField::StackAmount] < stacks)
        {
            r.values[SpellField::StackAmount] = stacks;
            break;
        }
    }

    r.values[SpellField::StartRecoveryCategory] = 133;
    r.values[SpellField::StartRecoveryTime] = 1500;
    if (!r.values[SpellField::RecoveryTime] && !r.values[SpellField::CategoryRecoveryTime])
    {
        uint32 const shortCooldown = sConfigMgr->GetOption<uint32>("Skinshifter.Grimoire.ShortSpellCooldown", 8000);
        uint32 const controlCooldown = sConfigMgr->GetOption<uint32>("Skinshifter.Grimoire.CrowdControlCooldown", 20000);
        r.values[SpellField::RecoveryTime] = IsCrowdControlRecord(r) ? controlCooldown :
            (channeled || castMs <= 1500) ? shortCooldown : 0;
    }

    LimitCopy(r, durations);
    // numbers: player-like damage and healing; percentages and flat stats on an enemy grow with level
    auto const duration = durations.find(r.values[SpellField::DurationIndex]);
    int32 const durationMs = duration == durations.end() ? 0 : duration->second;
    PlayerLikeNumbers(r, channeled ? durationMs : castMs, durationMs, false);
    r.values[SpellField::MaxTargetLevel] = 0;
    float const level = float(std::max<uint32>(referenceLevel, 1));
    for (uint32 index = 0; index < 3; ++index)
    {
        uint32 const effect = r.values[SpellField::Effect + index];
        uint32 const aura = r.values[SpellField::EffectAura + index];
        if (effect != EFFECT_APPLY_AURA || std::bit_cast<float>(r.values[SpellField::EffectRealPointsPerLevel + index]) != 0.0f)
            continue;
        int32 const amount = int32(r.values[SpellField::EffectBasePoints + index]) +
            std::max(1, int32(r.values[SpellField::EffectDieSides + index]));
        if (!amount)
            continue;
        if (IsScaledPercentAura(aura))
            ScaleEffect(r, index, float(amount));
        else if (IsFlatStatAura(aura))
            ScaleEffect(r, index, float(amount) * LevelCap / level);
    }

    auto const description = r.strings.find(DescriptionField);
    if (description == r.strings.end() || description->second.empty())
        r.strings[DescriptionField] = "An ability stolen from a creature.";
    for (uint32 index = 0; index < 3; ++index)
    {
        if (r.values[SpellField::Effect + index] == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE)
            for (uint32 field : { DescriptionField, ToolTipField })
                r.strings[field] = WeaponPercentWorded(r.strings[field], index);
        int32 const amount = int32(r.values[SpellField::EffectBasePoints + index]) +
            int32(r.values[SpellField::EffectDieSides + index]);
        if (r.values[SpellField::Effect + index] && amount < 0)
            for (uint32 field : { DescriptionField, ToolTipField })
                r.strings[field] = PositiveWorded(r.strings[field], index);
    }
    r.strings[DescriptionField] = WithNumbers(r.strings[DescriptionField], r);
    // the original's rank text, unless it is a real "Rank N": creature spells carry Ascension's own tags there
    // ("ASC D0 60": dungeon difficulty and level), which the spellbook printed under the name
    std::string& rank = r.strings[RankField];
    if (!(rank.size() > 5 && rank.compare(0, 5, "Rank ") == 0 &&
            std::all_of(rank.begin() + 5, rank.end(), [](unsigned char c) { return std::isdigit(c); })))
        rank.clear();
}

// The tables the server keeps about a spell by its id (its C++ script, its procs, its spell power bonus): the copy
// gets the original's rows.
void CopySideTables()
{
    struct Table { char const* name; char const* idColumn; };
    for (Table const& table : { Table{ "spell_script_names", "spell_id" }, Table{ "spell_proc", "SpellId" },
        Table{ "spell_bonus_data", "entry" } })
    {
        std::string columns;
        std::string selected;
        if (QueryResult result = WorldDatabase.Query("SELECT COLUMN_NAME FROM information_schema.COLUMNS WHERE "
            "TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '{}' ORDER BY ORDINAL_POSITION", table.name))
        {
            do
            {
                std::string const column = result->Fetch()[0].Get<std::string>();
                columns += (columns.empty() ? "`" : ", `") + column + "`";
                selected += selected.empty() ? "" : ", ";
                selected += column == table.idColumn ? std::string("c.`copy`") : "t.`" + column + "`";
            } while (result->NextRow());
        }
        if (selected.find("c.`copy`") == std::string::npos)
            continue;  // no such table, or not keyed the way this expects: a SQL error here would stop the server
        WorldDatabase.DirectExecute("DELETE FROM `{}` WHERE `{}` BETWEEN {} AND {}", table.name, table.idColumn,
            CopyFirst, CopyLast);
        WorldDatabase.DirectExecute("INSERT IGNORE INTO `{}` ({}) SELECT {} FROM `{}` t JOIN `skinshifter_copy` c "
            "ON c.`original` = t.`{}` AND c.`active` = 1", table.name, columns, selected, table.name, table.idColumn);
    }
}
}

// Makes the copies (spell_dbc rows, written only when they changed) and their rows for the client patch.
void InstallCopies(std::vector<std::string> const& types, std::string const& dataDir, std::string& clientRows)
{
    WorldDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `skinshifter_copy` (`original` INT UNSIGNED NOT NULL, "
        "`copy` INT UNSIGNED NOT NULL, `type` TINYINT UNSIGNED NOT NULL DEFAULT 0, `level` TINYINT UNSIGNED NOT NULL "
        "DEFAULT 0, `active` TINYINT UNSIGNED NOT NULL DEFAULT 0, PRIMARY KEY (`original`), UNIQUE KEY (`copy`)) "
        "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    WorldDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `skinshifter_copy_state` (`id` TINYINT UNSIGNED NOT NULL, "
        "`fingerprint` BIGINT UNSIGNED NOT NULL, `count` INT UNSIGNED NOT NULL, PRIMARY KEY (`id`)) "
        "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

    std::map<uint32, CopyCandidate> const candidates = CollectCopyCandidates();
    std::set<uint32> ids;
    for (auto const& [spell, candidate] : candidates)
        ids.insert(spell);
    std::map<uint32, Record> const records = ReadSpellRecords(dataDir + "dbc/Spell.dbc", ids);
    std::map<uint32, int32> const castTimes = ReadCastTimes(dataDir + "dbc/SpellCastTimes.dbc");
    std::map<uint32, int32> const durations = ReadCastTimes(dataDir + "dbc/SpellDuration.dbc");  // id, ms: same shape
    IconFixer icons;
    icons.Load(dataDir);
    LoadSpellLimits(dataDir);
    uint32 iconsFixed = 0;

    std::map<uint32, uint32> known;  // original -> copy, kept between startups
    uint32 next = CopyFirst;
    if (QueryResult result = WorldDatabase.Query("SELECT `original`, `copy` FROM `skinshifter_copy`"))
    {
        do
        {
            Field* fields = result->Fetch();
            known[fields[0].Get<uint32>()] = fields[1].Get<uint32>();
            next = std::max(next, fields[1].Get<uint32>() + 1);
        } while (result->NextRow());
    }

    CopyByOriginal.clear();
    CopyInfos.clear();
    OriginalByCopy.clear();
    std::vector<std::string> rows;
    std::string mapping;
    uint32 skipped = 0;
    uint32 full = 0;
    std::size_t fingerprint = 0;
    for (auto const& [original, candidate] : candidates)
    {
        auto const record = records.find(original);
        if (record == records.end() || !IsCopyable(record->second))
        {
            ++skipped;
            continue;
        }
        uint32 copy = known.count(original) ? known[original] : 0;
        if (!copy)
        {
            if (next > CopyLast)
            {
                ++full;
                continue;
            }
            copy = next++;
        }
        uint32 const type = candidate.MainType();
        Record shaped = record->second;
        ShapeCopy(shaped, copy, candidate.level, castTimes, durations);
        uint32 const iconBefore = shaped.values[SpellField::SpellIcon];
        icons.Fix(shaped);
        iconsFixed += iconBefore != shaped.values[SpellField::SpellIcon];
        std::string const values = RowValues(shaped, types);
        fingerprint ^= std::hash<std::string>{}(values) + 0x9e3779b97f4a7c15ull + (fingerprint << 6) + (fingerprint >> 2);
        rows.push_back(values);
        clientRows += ClientRow(shaped, StolenSkillLine(type));
        mapping += Acore::StringFormat("{}({}, {}, {}, {}, 1)", mapping.empty() ? "" : ", ", original, copy, type,
            std::min<uint32>(candidate.level, 255));
        CopyByOriginal[original] = copy;
        CopyInfos.push_back({ copy, uint8(type), uint8(std::min<uint32>(candidate.level, 255)) });
        OriginalByCopy[copy] = original;
    }

    WorldDatabase.DirectExecute("UPDATE `skinshifter_copy` SET `active` = 0");
    if (!mapping.empty())
        WorldDatabase.DirectExecute("REPLACE INTO `skinshifter_copy` (`original`, `copy`, `type`, `level`, `active`) "
            "VALUES {}", mapping);

    uint32 stored = 0;
    if (QueryResult result = WorldDatabase.Query("SELECT COUNT(*) FROM `spell_dbc` WHERE `ID` BETWEEN {} AND {}",
        CopyFirst, CopyLast))
        stored = uint32(result->Fetch()[0].Get<uint64>());
    bool unchanged = false;
    if (QueryResult result = WorldDatabase.Query("SELECT `fingerprint`, `count` FROM `skinshifter_copy_state` WHERE `id` = 1"))
        unchanged = result->Fetch()[0].Get<uint64>() == uint64(fingerprint) && result->Fetch()[1].Get<uint32>() == rows.size() &&
            stored == rows.size();
    if (!unchanged)
    {
        WorldDatabase.DirectExecute("DELETE FROM `spell_dbc` WHERE `ID` BETWEEN {} AND {}", CopyFirst, CopyLast);
        constexpr std::size_t Batch = 100;
        for (std::size_t first = 0; first < rows.size(); first += Batch)
        {
            std::string values;
            for (std::size_t index = first; index < std::min(rows.size(), first + Batch); ++index)
                values += (values.empty() ? "(" : ", (") + rows[index] + ")";
            WorldDatabase.DirectExecute("INSERT INTO `spell_dbc` VALUES {}", values);
        }
        WorldDatabase.DirectExecute("REPLACE INTO `skinshifter_copy_state` (`id`, `fingerprint`, `count`) VALUES (1, {}, {})",
            uint64(fingerprint), uint32(rows.size()));
    }
    CopySideTables();
    LOG_INFO("module", "Skinshifter: {} stolen abilities had no icon the client can show; they got one of their "
        "school.", iconsFixed);
    LOG_INFO("module", "Skinshifter: {} Grimoire copies of creature abilities {} ({} creature spells cannot be "
        "stolen{}).", rows.size(), unchanged ? "unchanged" : "written", skipped,
        full ? Acore::StringFormat(", {} more found no free spell id", full) : "");
}

std::vector<CopyInfo> const& AllCopies()
{
    return CopyInfos;
}

uint32 CopyOf(uint32 original)
{
    auto const copy = CopyByOriginal.find(original);
    return copy == CopyByOriginal.end() ? 0 : copy->second;
}

uint32 OriginalOf(uint32 copy)
{
    auto const original = OriginalByCopy.find(copy);
    return original == OriginalByCopy.end() ? 0 : original->second;
}

void InstallSkillLines();

void Install()
{
    std::vector<std::string> const types = SpellDbcColumnTypes();
    if (types.size() != SpellFields)
    {
        LOG_ERROR("module", "Skinshifter: `spell_dbc` has {} columns, not {}; the Skinshifter spells are not installed.",
            types.size(), SpellFields);
        return;
    }

    std::string dataDir = sConfigMgr->GetOption<std::string>("DataDir", "./");
    if (!dataDir.empty() && dataDir.back() != '/' && dataDir.back() != '\\')
        dataDir += '/';
    LoadSpellLimits(dataDir);  // the ranges, for the reach of Steal Form and the takeovers
    std::set<uint32> sources;
    for (SpellDef const& def : Spells)
    {
        sources.insert(def.source);
        sources.insert(def.iconFrom);
    }
    for (auto const& [spell, visualSource] : VisualFrom)
        sources.insert(visualSource);
    sources.insert(TalentTemplate);
    for (TalentDef const& talent : Talents())
        sources.insert(talent.iconFrom);
    std::map<uint32, Record> const records = ReadSpellRecords(dataDir + "dbc/Spell.dbc", sources);
    std::map<uint32, int32> const castTimes = ReadCastTimes(dataDir + "dbc/SpellCastTimes.dbc");
    std::map<uint32, int32> const durations = ReadCastTimes(dataDir + "dbc/SpellDuration.dbc");

    // Our own icons need the Skinshifter client patch; without it the client shows a question mark.
    bool const ownIcons = sConfigMgr->GetOption<bool>("Skinshifter.Spells.OwnIcons", true);
    uint32 installed = 0;
    std::string clientRows;
    for (SpellDef const& def : Spells)
    {
        auto const source = records.find(def.source);
        auto const icon = records.find(def.iconFrom);
        if (source == records.end() || icon == records.end())
        {
            LOG_ERROR("module", "Skinshifter: source spell {} for {} is not in {}dbc/Spell.dbc.", def.source, def.name, dataDir);
            continue;
        }
        Record record = source->second;
        record.values[0] = def.id;
        def.shape(record);
        NoRunes(record);
        // the reach the realm's config gives them (the client checks the spell's range before the server sees it)
        if (def.id == SkinshifterSpells::StealForm)
            ReachOf(record, sConfigMgr->GetOption<float>("Skinshifter.Lift.Range", 10.0f));
        else if (def.id == SkinshifterSpells::Possess || def.id == SkinshifterSpells::HostileTakeover)
            ReachOf(record, sConfigMgr->GetOption<float>("Skinshifter.Takeover.Range", 30.0f));
        {
            // the Skinshifter's own damage and healing grow to player-like numbers at 60 (a spell made to grow its
            // own way, as Lure, keeps it)
            auto const cast = castTimes.find(record.values[SpellField::CastingTimeIndex]);
            auto const duration = durations.find(record.values[SpellField::DurationIndex]);
            PlayerLikeNumbers(record, cast == castTimes.end() ? 0 : cast->second,
                duration == durations.end() ? 0 : duration->second, true);
        }
        if (auto const bit = FamilyBitOf.find(def.id); bit != FamilyBitOf.end())
            SetFamily(record, Bits({ bit->second }));
        record.values[SpellField::SpellIcon] = ownIcons && def.icon ? def.icon : icon->second.values[SpellField::SpellIcon];
        // Wraith Running wears Wraith Walk's own icon (interface\icons\inv_helm_plate_raiddeathknight_p_01, CoA's
        // SpellIcon 14326), not Unholy Presence's.
        if (def.id == WraithRun || def.id == WraithRunning)
            record.values[SpellField::SpellIcon] = WraithWalkIcon;
        // Wraith Running's look: a client spell visual chosen in the config. 9900001 is the client patch's own (a
        // see-through pale blue body with Wraith Walk's blue flames at the feet and its sound, tools/mkskinclient.py);
        // 20236 is the eredar shadow form, 25243 the Shadowlands Shadowform smoke, 1376 the Emerald Nightmare's.
        if (def.id == WraithRunning)
            if (uint32 const look = sConfigMgr->GetOption<uint32>("Skinshifter.WraithRunning.Visual", 9900001))
                record.values[SpellField::SpellVisual] = look;
        // The possessed body's look: the client patch's 9900003, Ghost Visual's pale blue tint without its
        // transparency. "Glowy (Blue)" itself (22576) turns a body near black under a cloud of sparkles.
        if (def.id == FxBlueGlow)
            if (uint32 const look = sConfigMgr->GetOption<uint32>("Skinshifter.Fx.BlueGlowVisual", 9900003))
                for (uint32 field = SpellField::SpellVisual; field < SpellField::SpellVisual + 2; ++field)
                    record.values[field] = field == SpellField::SpellVisual ? look : 0;
        // Casting Wraith Running shows nothing of its own: Unholy Presence's look (the skull over the head) is not
        // Wraith Walk's; the buff brings the whole look.
        // Root Walking takes no state look from Burrow (26381): see-through is wrong on a herb or a vein.
        // The takeover's hold shows nothing: Mind Control's swirl on the creature is not a Skinshifter's possession.
        // Root Walk itself shows nothing either: Burrow's cast kit is the "struck hard" flinch.
        // Steed Shift and Possessed Steed show nothing either: Spirit Walk's and Unholy Presence's looks hung over
        // the empty saddle.
        if (def.id == WraithRun || def.id == RootWalk || def.id == RootWalking || def.id == TakeoverControl ||
            def.id == SkinStride || def.id == PossessedSteed || def.id == SteedReins)
            for (uint32 field = SpellField::SpellVisual; field < SpellField::SpellVisual + 2; ++field)
                record.values[field] = 0;
        // Root Walking's 1.5 sec dig throws up earth: the client patch's 9900004 (Burrow's precast only).
        if (def.id == RootWalking)
            record.values[SpellField::SpellVisual] = DigVisual;
        if (auto const visual = VisualFrom.find(def.id); visual != VisualFrom.end())
            if (auto const look = records.find(visual->second); look != records.end())
                for (uint32 field = SpellField::SpellVisual; field < SpellField::SpellVisual + 2; ++field)
                    record.values[field] = look->second.values[field];
        record.strings = { { NameField, def.name }, { RankField, "" }, { DescriptionField, def.description },
            { ToolTipField, def.toolTip } };
        std::string const values = RowValues(record, types);
        WorldDatabase.DirectExecute("REPLACE INTO `spell_dbc` VALUES ({})", values);
        if (!WorldDatabase.Query("SELECT 1 FROM `spell_dbc` WHERE `ID` = {}", def.id))
        {
            LOG_ERROR("module", "Skinshifter: {} ({}) could not be written to `spell_dbc` (see the SQL error above). "
                "Values: {}", def.name, def.id, values);
            continue;
        }
        Ascension::ClientSpellPatches::Instance().Register(def.id);
        clientRows += ClientRow(record, def.skillLine);
        ++installed;
    }
    std::map<uint32, uint32> iconOfSpell;
    for (SpellDef const& def : Spells)
        if (auto const icon = records.find(def.iconFrom); icon != records.end())
            iconOfSpell[def.id] = icon->second.values[SpellField::SpellIcon];
    uint32 talentRanks = 0;
    if (auto const source = records.find(TalentTemplate); source != records.end())
    {
        for (TalentDef const& talent : Talents())
        {
            auto const icon = records.find(talent.iconFrom);
            if (icon == records.end())
            {
                LOG_ERROR("module", "Skinshifter: icon spell {} for the talent {} is not in Spell.dbc.", talent.iconFrom,
                    talent.name);
                continue;
            }
            iconOfSpell[talent.iconFrom] = icon->second.values[SpellField::SpellIcon];
            if (talent.active)
                continue;  // an ability node teaches a spell of the list above
            for (uint32 rank = 1; rank <= talent.ranks; ++rank)
            {
                Record record = source->second;
                uint32 const id = talent.firstSpell + rank - 1;
                record.values[0] = id;
                ShapeTalent(record, talent, rank);
                NoRunes(record);
                record.values[SpellField::SpellIcon] = icon->second.values[SpellField::SpellIcon];
                if (talent.slot)  // the node shows its own icon: an empty slot until the addon shows what is in it
                    record.values[SpellField::SpellIcon] = IconEmptySlot;
                record.strings = { { NameField, talent.name }, { RankField, Acore::StringFormat("Rank {}", rank) },
                    { DescriptionField, talent.description }, { ToolTipField, "" } };
                WorldDatabase.DirectExecute("REPLACE INTO `spell_dbc` VALUES ({})", RowValues(record, types));
                Ascension::ClientSpellPatches::Instance().Register(id);
                clientRows += ClientRow(record, 0);
                ++talentRanks;
            }
        }
    }
    else
        LOG_ERROR("module", "Skinshifter: talent template spell {} is not in Spell.dbc; no talents.", TalentTemplate);
    LOG_INFO("module", "Skinshifter: {} talent ranks installed.", talentRanks);
    InstallAdvancement(dataDir, iconOfSpell, sConfigMgr->GetOption<bool>("Skinshifter.Advancement.Enable", true));
    InstallCopies(types, dataDir, clientRows);
    // The same rows for the client patch: tools\mkskinclient.py puts them into the client's Spell.dbc, so the client
    // knows the spells from the start (CoA's spell stream only takes effect from the next world load of a client).
    std::string const clientFile = dataDir + "skinshifter_client_spells.txt";
    if (std::ofstream out(clientFile, std::ios::binary | std::ios::trunc); out)
        out << clientRows;
    else
        LOG_ERROR("module", "Skinshifter: could not write {} for the client patch.", clientFile);
    LOG_INFO("module", "Skinshifter: {} of {} Skinshifter spells (starter kit and effects) installed and registered for the client spell stream.",
        installed, Spells.size());
    InstallSkillLines();
}

// ---- Talents ---------------------------------------------------------------------------------------------
// The class tree is reached with ability essence and gated at 8 and 16 points spent in it (rows 4 and 8); a spec
// tree with talent essence, gated at 5 and 10 (rows 4 and 7). Row 0 of the class tree holds the four signatures.
// ---- the slot trees ----------------------------------------------------------------------------------------------
// The trees hold the loadout: slot nodes (squares, the shape of an active ability) open a slot for a stolen ability, chosen in the talent window by
// pointing at the node; upgrades under a slot improve whatever is in it (the module does that, it knows the slots);
// lines show what hangs from what (one of the nodes a line comes from must be learned); a choice node holds two
// entries of which only one can be learned. Talents of the old trees keep their entries and spells but move to the
// places below.
TalentDef Node(uint32 entry, uint32 firstSpell, uint8 ranks, uint8 tree, uint8 row, uint8 column, uint32 iconFrom,
    char const* name, char const* description, int32 amountPerRank, uint8 slot, uint32 slotTypes, uint8 upgradeOf,
    uint8 upgrade, uint32 group, uint8 level, std::array<uint32, 4> from, char const* nodeType)
{
    TalentDef talent{ entry, firstSpell, ranks, tree, row, column, iconFrom, name, description, SPELL_AURA_DUMMY, 0,
        amountPerRank, std::array<uint32, 3>{} };
    talent.slot = slot;
    talent.slotTypes = slotTypes;
    talent.upgradeOf = upgradeOf;
    talent.upgrade = upgrade;
    talent.group = group;
    talent.level = level;
    talent.from = from;
    talent.nodeType = nodeType;
    return talent;
}

// A talent of the new kind outside the slots: a passive aura (rank n gives n times the amount).
TalentDef Trait(uint32 entry, uint32 firstSpell, uint8 ranks, uint8 tree, uint8 row, uint8 column, uint32 iconFrom,
    char const* name, char const* description, uint32 aura, int32 misc, int32 amountPerRank, std::array<uint32, 4> from)
{
    TalentDef talent{ entry, firstSpell, ranks, tree, row, column, iconFrom, name, description, aura, misc,
        amountPerRank, std::array<uint32, 3>{} };
    talent.from = from;
    return talent;
}

// An ability node: it teaches an active spell of the spell list (a square, like the slots).
TalentDef Ability(uint32 entry, uint32 spellId, uint8 tree, uint8 row, uint8 column, uint32 iconFrom, char const* name,
    char const* description, std::array<uint32, 4> from)
{
    TalentDef talent = Trait(entry, spellId, 1, tree, row, column, iconFrom, name, description, SPELL_AURA_DUMMY, 0, 0,
        from);
    talent.active = true;
    talent.nodeType = "SpendSquare";
    return talent;
}

// A set: one upgrade node with two or three kinds at once.
TalentDef Set(TalentDef talent, uint8 upgrade2, int32 amount2, uint8 upgrade3 = UpgradeNone, int32 amount3 = 0)
{
    talent.upgrade2 = upgrade2;
    talent.amount2 = amount2;
    talent.upgrade3 = upgrade3;
    talent.amount3 = amount3;
    return talent;
}

// The slots and their upgrades. Each slot grows its own way (a single upgrade, a pick of two out of two choices, a
// set of three), not the whole menu everywhere:
//   class tree  Thief's Slot: Double Take (one kind); Second Thief's Slot: Light Fingers (a set of two)
//   spec trees  Open Slot: Honed Theft (one kind); Slot I: its spec's own kind (bleed, or damage and healing);
//               Slot II: two choices (strike twice or crit, and cooldown or damage), stronger; Slot III: a set of
//               three, stronger still; Thief's Mastery (every slot) or the spec's capstone at the bottom.
// New talents around them: Beastskin's dual wielding (Ambidexterity, Twin Fangs) and Hydra Head, Courtier's and
// Elementalist's shields, Felstriker's spellblade (spell power from attack power and back) and Cerberus.
std::vector<TalentDef> NewTalents()
{
    uint32 const beast = CreatureTypeBit(CREATURE_TYPE_BEAST);
    uint32 const court = CreatureTypeBit(CREATURE_TYPE_HUMANOID);
    uint32 const fel = CreatureTypeBit(CREATURE_TYPE_DEMON) | CreatureTypeBit(CREATURE_TYPE_UNDEAD);
    uint32 const element = CreatureTypeBit(CREATURE_TYPE_ELEMENTAL) | CreatureTypeBit(CREATURE_TYPE_DRAGONKIN) |
        CreatureTypeBit(CREATURE_TYPE_GIANT);
    char const* const open = "Opens a slot for any stolen ability. Point at it here to choose what goes in it.";
    return {
        // ---- class tree
        Node(139150, 1990546, 1, 0, 6, 2, 921, "Thief's Slot", open, 0, 1, AnyCreatureType, 0, UpgradeNone, 0, 20, { 139113, 139114 }, "SpendSquare"),
        Node(139151, 1990551, 1, 0, 6, 6, 921, "Second Thief's Slot", open, 0, 2, AnyCreatureType, 0, UpgradeNone, 0, 40, { 139114, 139115 }, "SpendSquare"),
        Node(139152, 1990556, 2, 0, 7, 2, 13877, "Double Take", "The ability in your Thief's Slot has a $s1% chance to strike twice; a debuff it leaves gains a second stack.", 10, 0, 0, 1, UpgradeDouble, 0, 0, { 139150 }, nullptr),
        Set(Node(139155, 1990571, 2, 0, 7, 6, 13750, "Light Fingers", "The ability in your Second Thief's Slot has a $s1% shorter cooldown and $s2% more critical strike chance.", 10, 0, 0, 2, UpgradeCooldown, 0, 0, { 139151 }, nullptr), UpgradeCrit, 5),

        // ---- Beastskin
        Node(139156, 1990576, 1, 1, 0, 2, 921, "Open Slot", open, 0, 3, AnyCreatureType, 0, UpgradeNone, 0, 10, {  }, "SpendSquare"),
        Node(139157, 1990581, 1, 1, 0, 6, 24604, "Beast Slot I", "Opens a slot for a stolen beast ability. Point at it here to choose what goes in it.", 0, 4, beast, 0, UpgradeNone, 0, 10, {  }, "SpendSquare"),
        Node(139159, 1990591, 2, 1, 1, 2, 14278, "Honed Theft", "Increases the critical strike chance of the ability in your Open Slot by $s1%.", 5, 0, 0, 3, UpgradeCrit, 0, 0, { 139156 }, nullptr),
        Trait(139220, 1990896, 3, 1, 1, 4, 13852, "Ambidexterity", "Increases the damage of your off-hand weapon by $s1%.", SPELL_AURA_MOD_OFFHAND_DAMAGE_PCT, 0, 10, {  }),
        Node(139161, 1990601, 2, 1, 1, 6, 772, "Rending", "The ability in your Beast Slot I leaves a bleed: $s1% of its damage over the next seconds.", 10, 0, 0, 4, UpgradeBleed, 0, 0, { 139157 }, nullptr),
        Trait(139221, 1990901, 3, 1, 3, 5, 12281, "Twin Fangs", "Your auto attacks have a $h% chance to strike again at once.", SPELL_AURA_PROC_TRIGGER_SPELL, int32(TwinFangsStrike), 3, { 139120, 139119 }),
        Node(139164, 1990616, 1, 1, 4, 4, 24604, "Beast Slot II", "Opens a second slot for a stolen beast ability. Point at it here to choose what goes in it.", 0, 5, beast, 0, UpgradeNone, 0, 30, { 139121, 139221 }, "SpendSquare"),
        Node(139165, 1990621, 1, 1, 5, 3, 13877, "Beast Echo", "The ability in your Beast Slot II has a $s1% chance to strike twice; a debuff it leaves gains a second stack.", 25, 0, 0, 5, UpgradeDouble, 33002, 0, { 139164 }, nullptr),
        Node(139166, 1990626, 1, 1, 5, 3, 14278, "Keen Beast", "Increases the critical strike chance of the ability in your Beast Slot II by $s1%.", 15, 0, 0, 5, UpgradeCrit, 33002, 0, { 139164 }, nullptr),
        Node(139163, 1990611, 1, 1, 5, 5, 13750, "Quick Beast", "Reduces the cooldown of the ability in your Beast Slot II by $s1%.", 30, 0, 0, 5, UpgradeCooldown, 33014, 0, { 139164 }, nullptr),
        Node(139222, 1990906, 1, 1, 5, 5, 20217, "Beast Fury", "Increases the damage and healing of the ability in your Beast Slot II by $s1%.", 20, 0, 0, 5, UpgradeDamage, 33014, 0, { 139164 }, nullptr),
        Ability(139223, HydraHead, 1, 6, 4, 102137, "Hydra Head", "Teaches Hydra Head: three heads of a stolen hydra spit at the target, each a random Poison Bolt, Arcane Missile or Sand Speckle.", { 139165, 139166, 139163, 139222 }),
        Node(139167, 1990631, 2, 1, 6, 6, 13750, "Nimble Paws", "Increases your chance to dodge by $s1%.", 2, 0, 0, 0, UpgradeNone, 33003, 0, { 139163, 139222, 139123 }, "SpendCircle"),
        Node(139168, 1990636, 1, 1, 7, 4, 24604, "Beast Slot III", "Opens a third slot for a stolen beast ability. Point at it here to choose what goes in it.", 0, 6, beast, 0, UpgradeNone, 0, 50, { 139223, 139167, 139124 }, "SpendSquare"),
        Set(Node(139169, 1990641, 1, 1, 8, 4, 13877, "Primal Trinity", "The ability in your Beast Slot III has a $s1% chance to strike twice, $s2% more critical strike chance and $s3% more damage and healing.", 20, 0, 0, 6, UpgradeDouble, 0, 0, { 139168 }, nullptr), UpgradeCrit, 10, UpgradeDamage, 20),
        Node(139171, 1990651, 1, 1, 9, 4, 31224, "Thief's Mastery", "Increases the damage and healing of every ability in your slots by $s1%.", 10, 0, 0, AllSlots, UpgradeDamage, 33004, 0, { 139169 }, nullptr),

        // ---- Courtier
        Node(139172, 1990656, 1, 2, 0, 2, 921, "Open Slot", open, 0, 7, AnyCreatureType, 0, UpgradeNone, 0, 10, {  }, "SpendSquare"),
        Node(139173, 1990661, 1, 2, 0, 6, 2061, "Court Slot I", "Opens a slot for a stolen humanoid ability. Point at it here to choose what goes in it.", 0, 8, court, 0, UpgradeNone, 0, 10, {  }, "SpendSquare"),
        Node(139175, 1990671, 2, 2, 1, 2, 14278, "Honed Theft", "Increases the critical strike chance of the ability in your Open Slot by $s1%.", 5, 0, 0, 7, UpgradeCrit, 0, 0, { 139172 }, nullptr),
        Trait(139224, 1990911, 3, 2, 1, 4, 107, "Courtly Guard", "Increases your chance to block attacks with a shield by $s1%.", SPELL_AURA_MOD_BLOCK_PERCENT, 0, 2, {  }),
        Node(139177, 1990681, 2, 2, 1, 6, 2061, "Gentle Hands", "Increases the damage and healing of the ability in your Court Slot I by $s1%.", 8, 0, 0, 8, UpgradeDamage, 0, 0, { 139173 }, nullptr),
        Trait(139225, 1990916, 2, 2, 3, 5, 20128, "Gilded Bulwark", "Increases the amount of damage your shield blocks by $s1%.", SPELL_AURA_MOD_SHIELD_BLOCKVALUE_PCT, 0, 15, { 139128, 139127 }),
        Node(139180, 1990696, 1, 2, 4, 4, 2061, "Court Slot II", "Opens a second slot for a stolen humanoid ability. Point at it here to choose what goes in it.", 0, 9, court, 0, UpgradeNone, 0, 30, { 139129, 139225 }, "SpendSquare"),
        Node(139181, 1990701, 1, 2, 5, 3, 13877, "Court Echo", "The ability in your Court Slot II has a $s1% chance to strike twice; a debuff it leaves gains a second stack.", 25, 0, 0, 9, UpgradeDouble, 33005, 0, { 139180 }, nullptr),
        Node(139182, 1990706, 1, 2, 5, 3, 14278, "Keen Court", "Increases the critical strike chance of the ability in your Court Slot II by $s1%.", 15, 0, 0, 9, UpgradeCrit, 33005, 0, { 139180 }, nullptr),
        Node(139179, 1990691, 1, 2, 5, 5, 13750, "Quick Court", "Reduces the cooldown of the ability in your Court Slot II by $s1%.", 30, 0, 0, 9, UpgradeCooldown, 33015, 0, { 139180 }, nullptr),
        Node(139226, 1990921, 1, 2, 5, 5, 2061, "Court Grace", "Increases the damage and healing of the ability in your Court Slot II by $s1%.", 20, 0, 0, 9, UpgradeDamage, 33015, 0, { 139180 }, nullptr),
        Node(139183, 1990711, 2, 2, 6, 4, 16929, "Poise", "Increases your Stamina by $s1%.", 3, 0, 0, 0, UpgradeNone, 33006, 0, { 139181, 139182, 139179, 139226 }, "SpendCircle"),
        Node(139184, 1990716, 1, 2, 7, 4, 2061, "Court Slot III", "Opens a third slot for a stolen humanoid ability. Point at it here to choose what goes in it.", 0, 10, court, 0, UpgradeNone, 0, 50, { 139183, 139132 }, "SpendSquare"),
        Set(Node(139185, 1990721, 1, 2, 8, 4, 13877, "Grand Masquerade", "The ability in your Court Slot III has a $s1% chance to strike twice, $s2% more critical strike chance and $s3% more damage and healing.", 20, 0, 0, 10, UpgradeDouble, 0, 0, { 139184 }, nullptr), UpgradeCrit, 10, UpgradeDamage, 20),
        Node(139187, 1990731, 1, 2, 9, 4, 31224, "Thief's Mastery", "Increases the damage and healing of every ability in your slots by $s1%.", 10, 0, 0, AllSlots, UpgradeDamage, 33007, 0, { 139185 }, nullptr),

        // ---- Felstriker: caster and blade in one (a spellblade)
        Node(139188, 1990736, 1, 3, 0, 2, 921, "Open Slot", open, 0, 11, AnyCreatureType, 0, UpgradeNone, 0, 10, {  }, "SpendSquare"),
        Node(139189, 1990741, 1, 3, 0, 6, 686, "Fel Slot I", "Opens a slot for a stolen fel ability. Point at it here to choose what goes in it.", 0, 12, fel, 0, UpgradeNone, 0, 10, {  }, "SpendSquare"),
        Node(139191, 1990751, 2, 3, 1, 2, 14278, "Honed Theft", "Increases the critical strike chance of the ability in your Open Slot by $s1%.", 5, 0, 0, 11, UpgradeCrit, 0, 0, { 139188 }, nullptr),
        Trait(139227, 1990926, 3, 3, 1, 4, 18708, "Fel Conduit", "Increases your spell damage by $s1% of your attack power.", SPELL_AURA_MOD_SPELL_DAMAGE_OF_ATTACK_POWER, int32(SPELL_SCHOOL_MASK_ALL), 10, {  }),
        Node(139193, 1990761, 2, 3, 1, 6, 686, "Malice", "Increases the damage and healing of the ability in your Fel Slot I by $s1%.", 8, 0, 0, 12, UpgradeDamage, 0, 0, { 139189 }, nullptr),
        Trait(139228, 1990931, 3, 3, 3, 5, 18731, "Blade of the Mind", "Increases your attack power by $s1% of your Intellect.", SPELL_AURA_MOD_ATTACK_POWER_OF_STAT_PERCENT, int32(STAT_INTELLECT), 10, { 139136, 139135 }),
        Node(139196, 1990776, 1, 3, 4, 4, 686, "Fel Slot II", "Opens a second slot for a stolen fel ability. Point at it here to choose what goes in it.", 0, 13, fel, 0, UpgradeNone, 0, 30, { 139137, 139228 }, "SpendSquare"),
        Node(139197, 1990781, 1, 3, 5, 3, 13877, "Fel Echo", "The ability in your Fel Slot II has a $s1% chance to strike twice; a debuff it leaves gains a second stack.", 25, 0, 0, 13, UpgradeDouble, 33008, 0, { 139196 }, nullptr),
        Node(139198, 1990786, 1, 3, 5, 3, 14278, "Keen Fel", "Increases the critical strike chance of the ability in your Fel Slot II by $s1%.", 15, 0, 0, 13, UpgradeCrit, 33008, 0, { 139196 }, nullptr),
        Node(139195, 1990771, 1, 3, 5, 5, 13750, "Quick Fel", "Reduces the cooldown of the ability in your Fel Slot II by $s1%.", 30, 0, 0, 13, UpgradeCooldown, 33016, 0, { 139196 }, nullptr),
        Node(139229, 1990936, 1, 3, 5, 5, 686, "Fel Fury", "Increases the damage and healing of the ability in your Fel Slot II by $s1%.", 20, 0, 0, 13, UpgradeDamage, 33016, 0, { 139196 }, nullptr),
        Ability(139230, Cerberus, 3, 6, 4, 54049, "Cerberus", "Teaches Cerberus: the three heads of the hound of the underworld bite the target, Physical, then Fire, then Shadow.", { 139197, 139198, 139195, 139229 }),
        Node(139199, 1990791, 2, 3, 6, 6, 18094, "Unholy Vigor", "Increases your Stamina by $s1%.", 3, 0, 0, 0, UpgradeNone, 33009, 0, { 139195, 139229, 139139 }, "SpendCircle"),
        Node(139200, 1990796, 1, 3, 7, 4, 686, "Fel Slot III", "Opens a third slot for a stolen fel ability. Point at it here to choose what goes in it.", 0, 14, fel, 0, UpgradeNone, 0, 50, { 139230, 139199, 139140 }, "SpendSquare"),
        Set(Node(139201, 1990801, 1, 3, 8, 4, 13877, "Unholy Trinity", "The ability in your Fel Slot III has a $s1% chance to strike twice, $s2% more critical strike chance and $s3% more damage and healing.", 20, 0, 0, 14, UpgradeDouble, 0, 0, { 139200 }, nullptr), UpgradeCrit, 10, UpgradeDamage, 20),
        Node(139203, 1990811, 1, 3, 9, 4, 31224, "Thief's Mastery", "Increases the damage and healing of every ability in your slots by $s1%.", 10, 0, 0, AllSlots, UpgradeDamage, 33010, 0, { 139201 }, nullptr),

        // ---- Elementalist
        Node(139204, 1990816, 1, 4, 0, 2, 921, "Open Slot", open, 0, 15, AnyCreatureType, 0, UpgradeNone, 0, 10, {  }, "SpendSquare"),
        Node(139205, 1990821, 1, 4, 0, 6, 403, "Element Slot I", "Opens a slot for a stolen element ability. Point at it here to choose what goes in it.", 0, 16, element, 0, UpgradeNone, 0, 10, {  }, "SpendSquare"),
        Node(139207, 1990831, 2, 4, 1, 2, 14278, "Honed Theft", "Increases the critical strike chance of the ability in your Open Slot by $s1%.", 5, 0, 0, 15, UpgradeCrit, 0, 0, { 139204 }, nullptr),
        Trait(139231, 1990941, 3, 4, 1, 4, 8071, "Earthen Guard", "Increases your chance to block attacks with a shield by $s1%.", SPELL_AURA_MOD_BLOCK_PERCENT, 0, 2, {  }),
        Node(139209, 1990841, 2, 4, 1, 6, 403, "Surge", "Increases the damage and healing of the ability in your Element Slot I by $s1%.", 8, 0, 0, 16, UpgradeDamage, 0, 0, { 139205 }, nullptr),
        Trait(139232, 1990946, 2, 4, 3, 5, 30706, "Elemental Ward", "Reduces the magic damage you take by ${$m1/-1}%.", SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, int32(SPELL_SCHOOL_MASK_MAGIC), -2, { 139143, 139144 }),
        Node(139212, 1990856, 1, 4, 4, 4, 403, "Element Slot II", "Opens a second slot for a stolen element ability. Point at it here to choose what goes in it.", 0, 17, element, 0, UpgradeNone, 0, 30, { 139146, 139232 }, "SpendSquare"),
        Node(139213, 1990861, 1, 4, 5, 3, 13877, "Element Echo", "The ability in your Element Slot II has a $s1% chance to strike twice; a debuff it leaves gains a second stack.", 25, 0, 0, 17, UpgradeDouble, 33011, 0, { 139212 }, nullptr),
        Node(139214, 1990866, 1, 4, 5, 3, 14278, "Keen Element", "Increases the critical strike chance of the ability in your Element Slot II by $s1%.", 15, 0, 0, 17, UpgradeCrit, 33011, 0, { 139212 }, nullptr),
        Node(139211, 1990851, 1, 4, 5, 5, 13750, "Quick Element", "Reduces the cooldown of the ability in your Element Slot II by $s1%.", 30, 0, 0, 17, UpgradeCooldown, 33017, 0, { 139212 }, nullptr),
        Node(139233, 1990951, 1, 4, 5, 5, 403, "Element Fury", "Increases the damage and healing of the ability in your Element Slot II by $s1%.", 20, 0, 0, 17, UpgradeDamage, 33017, 0, { 139212 }, nullptr),
        Node(139215, 1990871, 2, 4, 6, 4, 8071, "Earthen Bulwark", "Increases your Stamina by $s1%.", 3, 0, 0, 0, UpgradeNone, 33012, 0, { 139213, 139214, 139211, 139233 }, "SpendCircle"),
        Node(139216, 1990876, 1, 4, 7, 4, 403, "Element Slot III", "Opens a third slot for a stolen element ability. Point at it here to choose what goes in it.", 0, 18, element, 0, UpgradeNone, 0, 50, { 139215, 139148 }, "SpendSquare"),
        Set(Node(139217, 1990881, 1, 4, 8, 4, 13877, "Primal Convergence", "The ability in your Element Slot III has a $s1% chance to strike twice, $s2% more critical strike chance and $s3% more damage and healing.", 20, 0, 0, 18, UpgradeDouble, 0, 0, { 139216 }, nullptr), UpgradeCrit, 10, UpgradeDamage, 20),
        Node(139219, 1990891, 1, 4, 9, 4, 31224, "Thief's Mastery", "Increases the damage and healing of every ability in your slots by $s1%.", 10, 0, 0, AllSlots, UpgradeDamage, 33013, 0, { 139217 }, nullptr),
    };
}

struct Placement { uint32 entry; uint8 row, column; std::array<uint32, 4> from; uint32 group; };
std::vector<Placement> const& OldPlacements()
{
    static std::vector<Placement> const placements = {
        { 139101, 1, 1, {  }, 0 },
        { 139102, 1, 4, {  }, 0 },
        { 139103, 1, 7, {  }, 0 },
        { 139104, 2, 1, { 139101 }, 0 },
        { 139105, 2, 4, { 139102 }, 0 },
        { 139106, 2, 7, { 139103 }, 0 },
        { 139107, 3, 1, { 139104 }, 0 },
        { 139108, 3, 4, { 139105 }, 0 },
        { 139109, 3, 7, { 139106 }, 0 },
        { 139110, 4, 1, { 139107 }, 0 },
        { 139111, 4, 4, { 139108 }, 0 },
        { 139112, 4, 7, { 139109 }, 0 },
        { 139113, 5, 1, { 139110 }, 0 },
        { 139114, 5, 4, { 139111 }, 0 },
        { 139115, 5, 7, { 139112 }, 0 },
        { 139116, 8, 4, { 139152, 139155 }, 33001 },
        { 139117, 8, 4, { 139152, 139155 }, 33001 },
        // Beastskin
        { 139118, 2, 2, { 139159 }, 0 },          // Bleeding Edge
        { 139120, 2, 4, { 139220 }, 0 },          // Feral Swiftness
        { 139119, 2, 6, { 139161 }, 0 },          // Thick Hide
        { 139121, 3, 3, { 139118, 139120 }, 0 },  // Predatory Strikes
        { 139122, 5, 1, { 139121 }, 0 },          // Pack Instinct
        { 139123, 5, 7, { 139221 }, 0 },          // Rending Echo
        { 139124, 6, 6, { 139163, 139222, 139123 }, 33003 },  // Savage Endurance (or Nimble Paws)
        { 139125, 9, 4, { 139169 }, 33004 },      // Apex Predator (or Thief's Mastery)
        // Courtier
        { 139126, 2, 2, { 139175 }, 0 },          // Bedside Manner
        { 139128, 2, 4, { 139224 }, 0 },          // Quick Study
        { 139127, 2, 6, { 139177 }, 0 },          // Court Etiquette
        { 139129, 3, 3, { 139126, 139128 }, 0 },  // Graceful
        { 139130, 5, 1, { 139129 }, 0 },          // Patron's Favor
        { 139131, 5, 7, { 139225 }, 0 },          // Renewing Vigor
        { 139132, 6, 4, { 139181, 139182, 139179, 139226 }, 33006 },  // Meditation (or Poise)
        { 139133, 9, 4, { 139185 }, 33007 },      // Masquerade (or Thief's Mastery)
        // Felstriker
        { 139134, 2, 2, { 139191 }, 0 },          // Borrowed Hellfire
        { 139136, 2, 4, { 139227 }, 0 },          // Dark Pact
        { 139135, 2, 6, { 139193 }, 0 },          // Soul Toll
        { 139137, 3, 3, { 139134, 139136 }, 0 },  // Fel Intellect
        { 139138, 5, 1, { 139137 }, 0 },          // Grave Chill
        { 139139, 5, 7, { 139228 }, 0 },          // Lingering Torment
        { 139140, 6, 6, { 139195, 139229, 139139 }, 33009 },  // Demonic Resilience (or Unholy Vigor)
        { 139141, 9, 4, { 139201 }, 33010 },      // Damned Chorus (or Thief's Mastery)
        // Elementalist
        { 139142, 2, 2, { 139207 }, 0 },          // Convergence
        { 139143, 2, 4, { 139231 }, 0 },          // Storm Caller
        { 139144, 2, 6, { 139209 }, 0 },          // Static Charge
        { 139146, 3, 3, { 139142, 139143 }, 0 },  // Molten Core
        { 139145, 5, 1, { 139146 }, 0 },          // Elemental Focus
        { 139147, 5, 7, { 139232 }, 0 },          // Stoneskin
        { 139148, 6, 4, { 139213, 139214, 139211, 139233 }, 33012 },  // Tidecaller (or Earthen Bulwark)
        { 139149, 9, 4, { 139217 }, 33013 },      // Avatar of the Elements (or Thief's Mastery)
    };
    return placements;
}

// The row-6 alternatives are real passives, not markers: (entry, aura, misc).
std::vector<std::tuple<uint32, uint32, int32>> const& RealPassives()
{
    static std::vector<std::tuple<uint32, uint32, int32>> const passives = {
        { 139167, SPELL_AURA_MOD_DODGE_PERCENT, int32(0) },
        { 139183, SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, int32(STAT_STAMINA) },
        { 139199, SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, int32(STAT_STAMINA) },
        { 139215, SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, int32(STAT_STAMINA) },
    };
    return passives;
}

std::vector<TalentDef> BuildTalents()
{
    std::vector<TalentDef> talents =
    {
        { 139101, 1990301, 2, 0, 1, 1, 921, "Quick Hands", "Reduces the cast time of Steal Form and Possess by ${$m1/-1000}.1 sec.", SPELL_AURA_ADD_FLAT_MODIFIER, int32(SPELLMOD_CASTING_TIME), -500, Bits({ BitStealForm, BitPossess }) },
        { 139102, 1990306, 3, 0, 1, 4, 16929, "Thick Skin", "Increases your armor by $s1%.", SPELL_AURA_MOD_BASE_RESISTANCE_PCT, int32(SPELL_SCHOOL_MASK_NORMAL), 4, std::array<uint32, 3>{} },
        { 139103, 1990311, 2, 0, 1, 7, 13750, "Nimble Fingers", "Increases your chance to dodge by $s1%.", SPELL_AURA_MOD_DODGE_PERCENT, int32(0), 2, std::array<uint32, 3>{} },
        { 139104, 1990316, 3, 0, 2, 1, 1752, "Mimicry", "Increases the damage of Mimic Strike by $s1%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_DAMAGE), 10, Bits({ BitMimicStrike }) },
        { 139105, 1990321, 3, 0, 2, 4, 20594, "Stolen Stamina", "Increases your Stamina by $s1%.", SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, int32(STAT_STAMINA), 3, std::array<uint32, 3>{} },
        { 139106, 1990326, 2, 0, 2, 7, 41152, "Long Reach", "Increases the range of Lure by $s1 yards.", SPELL_AURA_ADD_FLAT_MODIFIER, int32(SPELLMOD_RANGE), 5, Bits({ BitLure }) },
        { 139107, 1990331, 1, 0, 3, 1, 1856, "Skin Memory", "Reduces the global cooldown of Shed Skin, Release and Skin Collection by ${$m1/-1000}.1 sec.", SPELL_AURA_ADD_FLAT_MODIFIER, int32(SPELLMOD_GLOBAL_COOLDOWN), -500, Bits({ BitShedSkin, BitRelease, BitSkinCollection }) },
        { 139108, 1990336, 2, 0, 3, 4, 2983, "Fleet Feet", "Increases your movement speed by $s1%.", SPELL_AURA_MOD_SPEED_ALWAYS, int32(0), 4, std::array<uint32, 3>{} },
        { 139109, 1990341, 1, 0, 3, 7, 12298, "Hardened Hide", "Borrowed Hide gives twice as much health and armor.", SPELL_AURA_DUMMY, int32(0), 1, std::array<uint32, 3>{} },
        { 139110, 1990346, 3, 0, 4, 1, 20217, "Resonance", "Increases all damage you deal by $s1%.", SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, int32(SPELL_SCHOOL_MASK_ALL), 1, std::array<uint32, 3>{} },
        { 139111, 1990351, 2, 0, 4, 4, 139, "Vigorous Theft", "Increases the damage and healing of Stolen Vigor by $s1%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_DOT), 15, Bits({ BitStolenVigor }) },
        { 139112, 1990356, 1, 0, 4, 7, 1784, "Clean Getaway", "Shed Skin and Release also free you from all movement impairing effects.", SPELL_AURA_DUMMY, int32(0), 1, std::array<uint32, 3>{} },
        { 139113, 1990361, 3, 0, 5, 1, 14278, "Opportunist", "Increases your chance to get a critical strike with all attacks and spells by $s1%.", SPELL_AURA_MOD_CRIT_PCT, int32(0), 1, std::array<uint32, 3>{} },
        { 139114, 1990366, 2, 0, 5, 4, 8122, "Mana Thief", "Increases your maximum mana by $s1%.", SPELL_AURA_MOD_INCREASE_ENERGY_PERCENT, int32(POWER_MANA), 5, std::array<uint32, 3>{} },
        { 139115, 1990371, 2, 0, 5, 7, 5938, "Sticky Fingers", "Reduces the cooldown of Lure by ${$m1/-1}%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_COOLDOWN), -15, Bits({ BitLure }) },
        { 139116, 1990376, 1, 0, 8, 3, 1856, "Perfect Mimic", "Mimic Strike's echo strikes at full strength instead of half.", SPELL_AURA_DUMMY, int32(0), 1, std::array<uint32, 3>{} },
        { 139117, 1990381, 1, 0, 8, 5, 921, "Thousand Skins", "Every skin you wear gives you Borrowed Hide, not only beasts.", SPELL_AURA_DUMMY, int32(0), 1, std::array<uint32, 3>{} },
        { 139118, 1990386, 3, 1, 0, 2, 13443, "Bleeding Edge", "Increases the bleed damage of Savage Rend by $s1%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_DOT), 10, Bits({ BitSavageRend }) },
        { 139119, 1990391, 2, 1, 0, 5, 16931, "Thick Hide", "Increases your armor by $s1%.", SPELL_AURA_MOD_BASE_RESISTANCE_PCT, int32(SPELL_SCHOOL_MASK_NORMAL), 5, std::array<uint32, 3>{} },
        { 139120, 1990396, 2, 1, 1, 3, 17002, "Feral Swiftness", "Increases your attack speed by $s1%.", SPELL_AURA_MOD_MELEE_HASTE, int32(0), 4, std::array<uint32, 3>{} },
        { 139121, 1990401, 3, 1, 2, 1, 16972, "Predatory Strikes", "Increases your chance to get a critical strike with melee attacks by $s1%.", SPELL_AURA_MOD_WEAPON_CRIT_PERCENT, int32(-1), 2, std::array<uint32, 3>{} },
        { 139122, 1990406, 3, 1, 2, 5, 24604, "Pack Instinct", "Increases your Agility by $s1%.", SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, int32(STAT_AGILITY), 3, std::array<uint32, 3>{} },
        { 139123, 1990411, 2, 1, 4, 2, 5221, "Rending Echo", "Increases the damage of Mimic Strike by $s1%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_DAMAGE), 15, Bits({ BitMimicStrike }) },
        { 139124, 1990416, 2, 1, 4, 5, 16858, "Savage Endurance", "Increases your Stamina by $s1%.", SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, int32(STAT_STAMINA), 4, std::array<uint32, 3>{} },
        { 139125, 1990421, 1, 1, 7, 3, 768, "Apex Predator", "Increases your physical damage by $s1%.", SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, int32(SPELL_SCHOOL_MASK_NORMAL), 5, std::array<uint32, 3>{} },
        { 139126, 1990426, 3, 2, 0, 2, 2061, "Bedside Manner", "Increases the healing of Borrowed Grace by $s1%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_DAMAGE), 8, Bits({ BitBorrowedGrace }) },
        { 139127, 1990431, 2, 2, 0, 5, 14751, "Court Etiquette", "Reduces the mana cost of Borrowed Grace by ${$m1/-1}%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_COST), -10, Bits({ BitBorrowedGrace }) },
        { 139128, 1990436, 2, 2, 1, 3, 14748, "Quick Study", "Reduces the cast time of Borrowed Grace by ${$m1/-1000}.2 sec.", SPELL_AURA_ADD_FLAT_MODIFIER, int32(SPELLMOD_CASTING_TIME), -250, Bits({ BitBorrowedGrace }) },
        { 139129, 1990441, 3, 2, 2, 1, 14752, "Graceful", "Increases your Spirit by $s1%.", SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, int32(STAT_SPIRIT), 3, std::array<uint32, 3>{} },
        { 139130, 1990446, 3, 2, 2, 5, 20711, "Patron's Favor", "Increases your healing by $s1%.", SPELL_AURA_MOD_HEALING_DONE_PERCENT, int32(0), 2, std::array<uint32, 3>{} },
        { 139131, 1990451, 2, 2, 4, 2, 774, "Renewing Vigor", "Increases the damage and healing of Stolen Vigor by $s1%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_DOT), 20, Bits({ BitStolenVigor }) },
        { 139132, 1990456, 2, 2, 4, 5, 14521, "Meditation", "Allows $s1% of your mana regeneration to continue while casting.", SPELL_AURA_MOD_MANA_REGEN_INTERRUPT, int32(0), 10, std::array<uint32, 3>{} },
        { 139133, 1990461, 1, 2, 7, 3, 1856, "Masquerade", "Increases your healing by $s1%.", SPELL_AURA_MOD_HEALING_DONE_PERCENT, int32(0), 10, std::array<uint32, 3>{} },
        { 139134, 1990466, 3, 3, 0, 2, 1949, "Borrowed Hellfire", "Increases your Shadow and Fire damage by $s1%.", SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, int32(SPELL_SCHOOL_MASK_SHADOW | SPELL_SCHOOL_MASK_FIRE), 2, std::array<uint32, 3>{} },
        { 139135, 1990471, 2, 3, 0, 5, 689, "Soul Toll", "Increases the damage of Soul Siphon by $s1%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_DOT), 10, Bits({ BitSoulSiphon }) },
        { 139136, 1990476, 2, 3, 1, 3, 18220, "Dark Pact", "Reduces the mana cost of Soul Siphon by ${$m1/-1}%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_COST), -10, Bits({ BitSoulSiphon }) },
        { 139137, 1990481, 3, 3, 2, 1, 18731, "Fel Intellect", "Increases your Intellect by $s1%.", SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, int32(STAT_INTELLECT), 3, std::array<uint32, 3>{} },
        { 139138, 1990486, 3, 3, 2, 5, 17794, "Grave Chill", "Increases your chance to get a critical strike with spells by $s1%.", SPELL_AURA_MOD_SPELL_CRIT_CHANCE, int32(0), 1, std::array<uint32, 3>{} },
        { 139139, 1990491, 2, 3, 4, 2, 18094, "Lingering Torment", "Increases the duration of Soul Siphon by ${$m1/1000} sec.", SPELL_AURA_ADD_FLAT_MODIFIER, int32(SPELLMOD_DURATION), 1000, Bits({ BitSoulSiphon }) },
        { 139140, 1990496, 2, 3, 4, 5, 18769, "Demonic Resilience", "Reduces all damage you take by ${$m1/-1}%.", SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, int32(SPELL_SCHOOL_MASK_ALL), -2, std::array<uint32, 3>{} },
        { 139141, 1990501, 1, 3, 7, 3, 30108, "Damned Chorus", "Increases your Shadow and Fire damage by $s1%.", SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, int32(SPELL_SCHOOL_MASK_SHADOW | SPELL_SCHOOL_MASK_FIRE), 5, std::array<uint32, 3>{} },
        { 139142, 1990506, 3, 4, 0, 2, 16166, "Convergence", "Increases your Fire, Frost and Nature damage by $s1%.", SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, int32(SPELL_SCHOOL_MASK_FIRE | SPELL_SCHOOL_MASK_FROST | SPELL_SCHOOL_MASK_NATURE), 2, std::array<uint32, 3>{} },
        { 139143, 1990511, 2, 4, 0, 5, 421, "Storm Caller", "Reduces the cast time of Storm Theft by ${$m1/-1000}.2 sec.", SPELL_AURA_ADD_FLAT_MODIFIER, int32(SPELLMOD_CASTING_TIME), -250, Bits({ BitStormTheft }) },
        { 139144, 1990516, 3, 4, 1, 3, 324, "Static Charge", "Increases the damage of Storm Theft by $s1%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_DAMAGE), 5, Bits({ BitStormTheft }) },
        { 139145, 1990521, 2, 4, 2, 1, 16164, "Elemental Focus", "Reduces the mana cost of Storm Theft by ${$m1/-1}%.", SPELL_AURA_ADD_PCT_MODIFIER, int32(SPELLMOD_COST), -10, Bits({ BitStormTheft }) },
        { 139146, 1990526, 3, 4, 2, 5, 11129, "Molten Core", "Increases your chance to get a critical strike with spells by $s1%.", SPELL_AURA_MOD_SPELL_CRIT_CHANCE, int32(0), 1, std::array<uint32, 3>{} },
        { 139147, 1990531, 2, 4, 4, 2, 8071, "Stoneskin", "Increases your armor by $s1%.", SPELL_AURA_MOD_BASE_RESISTANCE_PCT, int32(SPELL_SCHOOL_MASK_NORMAL), 5, std::array<uint32, 3>{} },
        { 139148, 1990536, 2, 4, 4, 5, 5394, "Tidecaller", "Increases your Intellect by $s1%.", SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE, int32(STAT_INTELLECT), 2, std::array<uint32, 3>{} },
        { 139149, 1990541, 1, 4, 7, 3, 30706, "Avatar of the Elements", "Increases your Fire, Frost and Nature damage by $s1%.", SPELL_AURA_MOD_DAMAGE_PERCENT_DONE, int32(SPELL_SCHOOL_MASK_FIRE | SPELL_SCHOOL_MASK_FROST | SPELL_SCHOOL_MASK_NATURE), 5, std::array<uint32, 3>{} },
    };
    for (Placement const& place : OldPlacements())
        for (TalentDef& talent : talents)
            if (talent.entry == place.entry)
            {
                talent.row = place.row;
                talent.column = place.column;
                talent.from = place.from;
                talent.group = place.group;
            }
    for (TalentDef const& talent : NewTalents())
        talents.push_back(talent);
    for (auto const& [entry, aura, misc] : RealPassives())
        for (TalentDef& talent : talents)
            if (talent.entry == entry)
            {
                talent.aura = aura;
                talent.misc = misc;
            }
    // A pick-one choice takes one point: Character Advancement refuses any rank of an entry whose group the build
    // already holds, its own first rank too. Such a node gives its full amount at that one rank.
    for (TalentDef& talent : talents)
        if (talent.group && talent.ranks > 1)
        {
            talent.amountPerRank *= talent.ranks;
            talent.amount2 *= talent.ranks;
            talent.amount3 *= talent.ranks;
            talent.ranks = 1;
        }
    return talents;
}

std::vector<TalentDef> const& Talents()
{
    static std::vector<TalentDef> const talents = BuildTalents();
    return talents;
}

TalentDef const* SlotNode(uint8 slot)
{
    for (TalentDef const& talent : Talents())
        if (talent.slot == slot)
            return &talent;
    return nullptr;
}

std::vector<AdvancementSpec> const& AdvancementSpecs()
{
    static std::vector<AdvancementSpec> const specs =
    {
        { SpecBeastskin, 102, 95, "BEASTSKIN", 139001, 139011, "Agility",
            "Fight with tooth and claw in the skins of beasts. Bleeds, speed and pack instinct." },
        { SpecCourtier, 103, 96, "COURTIER", 139002, 139012, "Spirit",
            "Heal with the arts of the healers you study, and walk among people in their faces." },
        { SpecFelstriker, 104, 97, "FELSTRIKER", 139003, 139013, "Intellect",
            "Wield the fire and shadow of demons and the cold of the dead. Drains and curses." },
        { SpecElementalist, 105, 98, "ELEMENTALIST", 139004, 139014, "Intellect",
            "Wear fire, water, earth and air, and the breath of dragons. Every element you hold makes you stronger." },
    };
    return specs;
}

SpecInfo const& SpecOf(uint8 spec)
{
    static std::array<SpecInfo, SpecCount + 1> const specs = { {
        // every Skinshifter: cloth, leather, daggers, one-handed swords, fist weapons, staves (SkinshifterClass.cpp)
        { "", 0, 0, 0, {} },
        // Dual Wield, for tooth and claw
        { "Beastskin", SavageRend, FeralSkins, SkillBeastskin, { 674 } },
        // a healer's shield and maces (any spec can hold an off-hand item: those need no skill)
        { "Courtier", BorrowedGrace, SilverTongue, SkillCourtier, { 9116, 107, 198 } },
        // the fel blade: two-handed swords and axes, one-handed axes
        { "Felstriker", SoulSiphon, FelTouched, SkillFelstriker, { 202, 197, 196 } },
        // the shaman's way: shield and maces, one- and two-handed
        { "Elementalist", StormTheft, ElementalAttunement, SkillElementalist, { 9116, 107, 198, 199 } },
    } };
    return specs[spec <= SpecCount ? spec : 0];
}

std::vector<uint32> SkillLines()
{
    return { SkillSkinshifter, SkillBeastskin, SkillCourtier, SkillFelstriker, SkillElementalist, SkillStolenBeast,
        SkillStolenHumanoid, SkillStolenUndead, SkillStolenDemon, SkillStolenElemental, SkillStolenDragonkin,
        SkillStolenGiant, SkillStolenMechanical, SkillStolenOther };
}

// The client shows a spell only under a tab (skill line) the character has; without the skill the Skinshifter's
// own spells are known but invisible. So the skills are made sure of every login, directly if need be.
void LearnSkillLines(Player* player)
{
    for (uint32 skill : SkillLines())
    {
        if (player->HasSkill(skill))
            continue;
        if (!sSkillLineStore.LookupEntry(skill))
        {
            LOG_ERROR("module", "Skinshifter: skill line {} is not loaded (skillline_dbc); its spellbook tab is missing.", skill);
            continue;
        }
        bool const raceClassInfo = GetSkillRaceClassInfo(skill, player->getRace(), player->getClass()) != nullptr;
        if (raceClassInfo)
            player->LearnDefaultSkill(skill, 0);
        if (!player->HasSkill(skill))
        {
            uint16 const value = player->GetMaxSkillValueForLevel();
            player->SetSkill(skill, 0, value, value);
        }
        LOG_INFO("module", "Skinshifter: {} skill {}: {}{}", player->GetName(), skill,
            player->HasSkill(skill) ? "learned" : "learning it FAILED",
            raceClassInfo ? "" : " (no skillraceclassinfo_dbc row was loaded; set directly)");
    }
}

void InstallSkillLines()
{
    struct Line { uint32 id; char const* name; uint32 icon; };
    std::array<Line, 14> const lines = { {
        { SkillSkinshifter, "Skinshifter", IconClass },
        { SkillBeastskin, "Beastskin", IconBeastskin },
        { SkillCourtier, "Courtier", IconCourtier },
        { SkillFelstriker, "Felstriker", IconFelstriker },
        { SkillElementalist, "Elementalist", IconElementalist },
        { SkillStolenBeast, "Beast", IconTabBeast },
        { SkillStolenHumanoid, "Humanoid", IconTabHumanoid },
        { SkillStolenUndead, "Undead", IconTabUndead },
        { SkillStolenDemon, "Demon", IconTabDemon },
        { SkillStolenElemental, "Elemental", IconTabElemental },
        { SkillStolenDragonkin, "Dragonkin", IconTabDragonkin },
        { SkillStolenGiant, "Giant", IconTabGiant },
        { SkillStolenMechanical, "Mechanical", IconTabMechanical },
        { SkillStolenOther, "Skins", IconSkinCollection },
    } };
    // Flags 0x410 and no race or class mask, as CoA's own class skill lines; only Skinshifters are given them.
    for (Line const& line : lines)
    {
        WorldDatabase.DirectExecute("REPLACE INTO `skillline_dbc` (`ID`, `CategoryID`, `SkillCostsID`, "
            "`DisplayName_Lang_enUS`, `DisplayName_Lang_Mask`, `SpellIconID`, `CanLink`) VALUES ({}, 7, 0, '{}', 16712190, {}, 0)",
            line.id, line.name, line.icon);
        WorldDatabase.DirectExecute("REPLACE INTO `skillraceclassinfo_dbc` (`ID`, `SkillID`, `RaceMask`, `ClassMask`, "
            "`Flags`, `MinLevel`, `SkillTierID`, `SkillCostIndex`) VALUES ({}, {}, 0, 0, 1040, 0, 0, 0)", line.id, line.id);
    }
    WorldDatabase.DirectExecute("DELETE FROM `skilllineability_dbc` WHERE `ID` BETWEEN {} AND {}", uint32(StealForm), uint32(StealForm) + 999);
    // The spellbook tabs: each spell in its tree's skill line (the client patch has the same rows).
    constexpr bool SpellbookTabs = true;
    for (SpellDef const& def : Spells)
        if (SpellbookTabs && def.skillLine)
            WorldDatabase.DirectExecute("INSERT INTO `skilllineability_dbc` (`ID`, `SkillLine`, `Spell`, `RaceMask`, "
                "`ClassMask`, `ExcludeRace`, `ExcludeClass`, `MinSkillLineRank`, `SupercededBySpell`, `AcquireMethod`, "
                "`TrivialSkillLineRankHigh`, `TrivialSkillLineRankLow`, `CharacterPoints_1`, `CharacterPoints_2`) "
                "VALUES ({}, {}, {}, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0)", uint32(def.id), uint32(def.skillLine), uint32(def.id));
}

void Report()
{
    std::string missing;
    for (SpellDef const& def : Spells)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(def.id);
        if (!info)
            missing += Acore::StringFormat(" {} ({})", def.name, def.id);
        else if (def.id < FxGhostVisual)
            LOG_INFO("module", "Skinshifter: spell {} loaded as \"{}\".", def.id, info->SpellName[0]);
        // Stealing and possessing are not attacks, and the effects are only looks: used on a friendly NPC that is
        // flagged for PvP (a town's guards and vendors) they must not flag you as well.
        if (info && (def.id == StealForm || def.id == Possess || def.id >= FxGhostVisual))
            const_cast<SpellInfo*>(info)->AttributesCu |= SPELL_ATTR0_CU_NO_PVP_FLAG;
    }
    uint32 talentRanks = 0;
    for (TalentDef const& talent : Talents())
        for (uint32 rank = 0; rank < talent.ranks; ++rank)
        {
            if (sSpellMgr->GetSpellInfo(talent.firstSpell + rank))
                ++talentRanks;
            else
                missing += Acore::StringFormat(" {} rank {} ({})", talent.name, rank + 1, talent.firstSpell + rank);
        }
    LOG_INFO("module", "Skinshifter: {} talent ranks loaded; Character Advancement {}.", talentRanks,
        AdvancementEnabled() ? "in use" : "not in use (the module's own specialization window)");
    if (!missing.empty())
        LOG_ERROR("module", "Skinshifter: in `spell_dbc` but not loaded by the server:{}", missing);
    for (uint32 skill : SkillLines())
        LOG_INFO("module", "Skinshifter: spellbook tab {}: skill line {}, race/class info {}.", skill,
            sSkillLineStore.LookupEntry(skill) ? "loaded" : "MISSING",
            GetSkillRaceClassInfo(skill, RACE_HUMAN, CLASS_WARRIOR) ? "loaded" : "MISSING");
}

void Remove()
{
    for (uint32 skill : SkillLines())
    {
        WorldDatabase.DirectExecute("DELETE FROM `skillline_dbc` WHERE `ID` = {}", skill);
        WorldDatabase.DirectExecute("DELETE FROM `skillraceclassinfo_dbc` WHERE `ID` = {}", skill);
    }
    WorldDatabase.DirectExecute("DELETE FROM `skilllineability_dbc` WHERE `ID` BETWEEN {} AND {}", uint32(StealForm), uint32(StealForm) + 999);
    for (SpellDef const& def : Spells)
        WorldDatabase.DirectExecute("DELETE FROM `spell_dbc` WHERE `ID` = {}", def.id);
    WorldDatabase.DirectExecute("DELETE FROM `spell_dbc` WHERE `ID` BETWEEN {} AND {}", CopyFirst, CopyLast);
    for (TalentDef const& talent : Talents())
        WorldDatabase.DirectExecute("DELETE FROM `spell_dbc` WHERE `ID` BETWEEN {} AND {}", talent.firstSpell,
            talent.firstSpell + talent.ranks - 1);
    std::string dataDir = sConfigMgr->GetOption<std::string>("DataDir", "./");
    if (!dataDir.empty() && dataDir.back() != '/' && dataDir.back() != '\\')
        dataDir += '/';
    RemoveAdvancement(dataDir);
}

char const* NameOf(uint32 spellId)
{
    for (SpellDef const& def : Spells)
        if (def.id == spellId)
            return def.name;
    return "";
}
}
