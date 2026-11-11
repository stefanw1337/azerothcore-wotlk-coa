/*
 * mod-skinshifter - ids of the Skinshifter's own spells. Released under the GNU AGPL v3 like AzerothCore.
 */

#ifndef SKINSHIFTER_SPELLS_H
#define SKINSHIFTER_SPELLS_H

#include "Define.h"
#include <array>
#include <map>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SkinshifterSpells
{
// Steed Shift carries a passenger (the Sandstone Drake's way): our Vehicle and VehicleSeat rows, the seat on the
// mount model's saddle point. The same ids in tools\mkskinclient.py (STEED_VEHICLE, STEED_SEAT).
constexpr uint32 SteedVehicleId = 9910;
constexpr uint32 SteedSeatId = 10010;

enum : uint32
{
    StealForm      = 1990001,
    ShedSkin       = 1990002,
    SkinCollection = 1990003,
    MimicStrike    = 1990004,
    StolenVigor    = 1990005,
    SlipAway       = 1990006,
    Possess        = 1990007,  // Possessor races: Steal Form that also wears the body, with a ghost transition
    Release        = 1990008,  // Possessor races: Shed Skin
    SlipBetweenWorlds = 1990009,  // Possessor races: Slip Away
    BorrowedShadow = 1990010,  // Felstriker starter: a Shadow Bolt
    StolenSpark    = 1990011,  // Elementalist starter: a Fireball
    BorrowedHide   = 1990012,  // while you wear a beast skin: more health and armour (skin buff)
    Lure           = 1990013,  // starter: a thrown blade at range, to pull one creature to you
    SkinStride     = 1990014,  // Steed Shift: a Skinshifter's mount used in a skin; the mount's shape and speed
    WraithRunning  = 1990015,  // the Wraith Running buff: ghost Possessors' travel form, mount speed in a shadow mist
    PhaseRunning   = 1990016,  // the Phase Running buff: energy Possessors' travel form, crackling with energy
    RootWalk       = 1990017,  // Skinshifter travel: dig through the ground as a tiny worm, at mount speed
    RootWalking    = 1990018,  // the Root Walk buff (speed; the module gives it the worm's look)
    WraithRun      = 1990019,  // ghost Possessors cast this for Wraith Running (toggles)
    PhaseRun       = 1990020,  // energy Possessors cast this for Phase Running (toggles)
    PossessedSteed = 1990021,  // a Possessor's mount used in a skin: possess it, its shape and speed
    LingeringWound = 1990022,  // the bleed a slot upgrade leaves (cast by the module with the amount)
    // Level 20, Takeover (SkinshifterTakeover.cpp): go into a creature and play it. Hostile: it stays a monster that
    // players hunt; peaceful: it takes your side. Skinshifter / Possessor names.
    Turnskin        = 1990023,  // Skinshifter, hostile
    KindredSkin     = 1990024,  // Skinshifter, peaceful
    HostileTakeover = 1990025,  // Possessor, hostile
    WillingHost     = 1990026,  // Possessor, peaceful
    TakeoverControl = 1990027,  // cast by the module: the possession of the creature (and its marker on you)
    TakeoverReset   = 1990028,  // on the creature's bar: run home, heal up and come back (out of combat)
    // Talent abilities (the spec trees). Hydra Head (Beastskin): three bolts, each a random one of the three below.
    HydraHead       = 1990029,
    HydraPoison     = 1990030,  // Poison Bolt (Nature)
    HydraArcane     = 1990031,  // Arcane Missile (Arcane)
    HydraSand       = 1990032,  // Sand Speckle (Physical)
    // Cerberus (Felstriker): three bites, one a head.
    Cerberus        = 1990033,
    CerberusBite    = 1990034,  // Physical
    CerberusFire    = 1990035,  // Fire
    CerberusShadow  = 1990036,  // Shadow
    TwinFangsStrike = 1990037,  // Twin Fangs (Beastskin): the extra strike it gives
    SteedReins      = 1990038,  // cast by the module: a passenger steering the Skinshifter steed (a possession)
    // a skin's buff while you wear it (by creature type; a beast's is Borrowed Hide) and the Echo it leaves for
    // 20 sec when you change out of it: 1990160 + type and 1990170 + type
    SkinDragonkin  = 1990162,
    SkinDemon      = 1990163,
    SkinElemental  = 1990164,
    SkinGiant      = 1990165,
    SkinUndead     = 1990166,
    SkinHumanoid   = 1990167,
    SkinMechanical = 1990169,
    EchoBeast      = 1990171,
    EchoDragonkin  = 1990172,
    EchoDemon      = 1990173,
    EchoElemental  = 1990174,
    EchoGiant      = 1990175,
    EchoUndead     = 1990176,
    EchoHumanoid   = 1990177,
    EchoMechanical = 1990179,

    // Level 10 specializations: a signature ability and an identity passive each.
    SavageRend          = 1990201,  // Beastskin
    BorrowedGrace       = 1990202,  // Courtier
    SoulSiphon          = 1990203,  // Felstriker
    StormTheft          = 1990204,  // Elementalist
    FeralSkins          = 1990211,  // Beastskin passive: +5% physical damage
    SilverTongue        = 1990212,  // Courtier passive: +10% healing done
    FelTouched          = 1990213,  // Felstriker passive: +5% Shadow and Fire damage
    ElementalAttunement = 1990214,  // Elementalist passive: +5% Fire, Frost and Nature damage
};

// Visual-only copies of existing spells (no effect, no threat, no procs), used by the Skinshifter effects.
// Aura = shown on you while it lasts; Missile = flies from you to the target.
enum Fx : uint32
{
    FxGhostVisual       = 1990101,  // aura  9617  Ghost Visual
    FxGhostBlueFog      = 1990102,  // aura  35838 Ghost Visual (blue fog)
    FxGhostFogSheer     = 1990103,  // aura  256802 Ghost Visual Blue Fog Transparent
    FxGhostHalfSeen     = 1990104,  // aura  44816 Transparency (50%)
    FxGhostCosmetic     = 1990105,  // aura  136151 Ghost Visual Effect
    FxBlueGlow          = 1990106,  // aura  22576 with the client patch's visual 9900003: pale blue, the body fully seen
    FxEtherealVisual    = 1990111,  // aura  254226 Ethereal Visual Effect
    FxEtherealBeacon    = 1990112,  // aura  32368 Ethereal Becon Visual
    FxPurpleGhost       = 1990113,  // aura  47840 Purple Ghost Aura
    FxOverwhelmEnergy   = 1990114,  // aura  254733 Overwhelming Energy Visual
    FxSpiritPulse       = 1990121,  // aura  84522 Spirit Visual
    FxEtherealRingStart = 1990122,  // aura  40952 Ethereal Ring, Cannon Death Visual, start
    FxSoulTransfer      = 1990131,  // missile 257619 Soul Transfer
    FxDrawnSoul         = 1990132,  // missile 2144741 Drawn Soul
    FxSoulReaped        = 1990133,  // missile 288816 Soul Reaped
    FxEtherealBolt      = 1990141,  // missile 40758 Ethereal Ring: The Bolt
    FxEnergyBall        = 1990142,  // missile 257471 Energy Ball
    FxStrangeEnergy     = 1990143,  // missile 2147502 Strange Energy
    FxSoulFragment      = 1990151,  // missile 2143254 Soul Fragment
};

// Own SpellIcon.dbc rows, added by the client patch (tools\mkskinclient.py, ICONS). Keep the two lists the same.
enum Icon : uint32
{
    IconStealForm      = 39901,
    IconShedSkin       = 39902,
    IconSkinCollection = 39903,
    IconMimicStrike    = 39904,
    IconStolenVigor    = 39905,
    IconSlipAway       = 39906,
    IconClass          = 39907,
    IconSheepsClothing = 39908,
    IconPossess        = 39909,
    IconRelease        = 39910,
    IconPossessorClass = 39911,
    IconNotAProfessor  = 39912,
    IconSlipBetweenWorlds = 39913,
    IconBeastskin      = 39914,
    IconCourtier       = 39915,
    IconFelstriker     = 39916,
    IconElementalist   = 39917,
    IconTabBeast       = 39918,
    IconTabHumanoid    = 39919,
    IconTabUndead      = 39920,
    IconTabDemon       = 39921,
    IconTabElemental   = 39922,
    IconTabDragonkin   = 39923,
    IconTabGiant       = 39924,
    IconTabMechanical  = 39925,
    IconEmptySlot      = 39926,  // an empty slot node in the talent trees (filled, the addon shows the ability)
};

// Spellbook tabs: one class skill line per tree. The client patch (mkskinclient.py, SKILL_LINES) must use the
// same ids.
enum SkillLine : uint32
{
    SkillSkinshifter  = 3330,
    SkillBeastskin    = 3331,
    SkillCourtier     = 3332,
    SkillFelstriker   = 3333,
    SkillElementalist = 3334,
    // Stolen abilities, by the creature type they come from (the Grimoire copies).
    SkillStolenBeast      = 3335,
    SkillStolenDragonkin  = 3336,
    SkillStolenDemon      = 3337,
    SkillStolenElemental  = 3338,
    SkillStolenGiant      = 3339,
    SkillStolenUndead     = 3340,
    SkillStolenHumanoid   = 3341,
    SkillStolenMechanical = 3342,
    SkillStolenOther      = 3343,
};
std::vector<uint32> SkillLines();
// The spellbook tab of abilities stolen from a creature of this type.
uint32 StolenSkillLine(uint32 creatureType);

enum Spec : uint8
{
    SpecNone = 0,
    SpecBeastskin = 1,
    SpecCourtier = 2,
    SpecFelstriker = 3,
    SpecElementalist = 4,
};
constexpr uint8 SpecCount = 4;
constexpr uint8 SpecLevel = 10;

struct SpecInfo
{
    char const* name;
    uint32 signature;
    uint32 passive;
    uint32 skillLine;
    // stock spells the specialization also brings: its weapons and shields (weapon skills, Shield and Block) and
    // Beastskin's Dual Wield; 0 ends the list
    std::array<uint32, 6> extras{};
};
// The specialization's name, its two spells and its spellbook tab (spec 1..SpecCount).
SpecInfo const& SpecOf(uint8 spec);

// ---- Talents (CoA Character Advancement) ---------------------------------------------------------------------
// The class tree (tab 0, ability essence) and one tree per specialization (tabs 1..4, talent essence). Each rank is
// its own passive spell (firstSpell + rank - 1); the amount grows with the rank. Spell modifiers (ADD_FLAT/PCT)
// reach the Skinshifter's own spells through spell family SkinshifterFamily and the bits in `affects`.
constexpr uint8 ClassId = 33;
// Not class 33 + 6 as CoA's classes: CoA's client keeps its spell modifiers in a table for families 0-38 only and
// writes a family-39 modifier past its end. 16 is a family the 3.3.5 data leaves unused.
constexpr uint32 SkinshifterFamily = 16;
enum FamilyBit : uint32
{
    BitStealForm = 0, BitPossess, BitShedSkin, BitRelease, BitMimicStrike, BitLure, BitStolenVigor, BitSlipAway,
    BitSlipBetweenWorlds, BitSkinCollection, BitRootWalk, BitWraithRun, BitPhaseRun, BitSavageRend,
    BitBorrowedGrace, BitSoulSiphon, BitStormTheft,
};
constexpr std::array<uint32, 3> Bits(std::initializer_list<uint32> bits)
{
    std::array<uint32, 3> mask{};
    for (uint32 bit : bits)
        mask[bit / 32] |= uint32(1) << (bit % 32);
    return mask;
}

struct TalentDef
{
    uint32 entry;       // CharacterAdvancement entry
    uint32 firstSpell;  // rank 1; rank n is firstSpell + n - 1
    uint8 ranks;
    uint8 tree;         // 0: class tree, 1..4: Beastskin, Courtier, Felstriker, Elementalist
    uint8 row;
    uint8 column;
    uint32 iconFrom;    // the spell whose icon it shows
    char const* name;
    char const* description;  // the rank spell's text; $s1 is that rank's amount
    uint32 aura;
    int32 misc;
    int32 amountPerRank;
    std::array<uint32, 3> affects;  // spell modifiers: which of the Skinshifter's spells (FamilyBit)
    // The slot trees: a slot node opens a slot for a stolen ability, an upgrade improves what is in a slot.
    uint8 slot = 0;                 // a slot node: the slot it opens (1..SlotCount)
    uint32 slotTypes = 0;           // a slot node: the creature types it takes (CreatureTypeBit), all for an open slot
    uint8 upgradeOf = 0;            // an upgrade: the slot it improves, AllSlots for every slot
    uint8 upgrade = 0;              // an upgrade: UpgradeKind
    uint32 group = 0;               // pick one: the entries of a group share a place; only one can be learned
    uint8 level = 0;                // the level it needs (0: none)
    std::array<uint32, 4> from{};   // the entries it hangs from in the tree (a line from each; one must be learned)
    char const* nodeType = nullptr; // the node's shape: SpendSquare, SpendCircle, SpendHex, SpendDiamond
    // A set: an upgrade node that gives more than one kind at once (amounts per rank).
    uint8 upgrade2 = 0;
    int32 amount2 = 0;
    uint8 upgrade3 = 0;
    int32 amount3 = 0;
    // An ability node: firstSpell is an active spell of the spell list (not a rank made from aura and amount).
    bool active = false;
};
std::vector<TalentDef> const& Talents();

enum UpgradeKind : uint8
{
    UpgradeNone = 0,
    UpgradeDouble,    // chance (%) that the ability strikes again
    UpgradeCrit,      // critical strike chance (%)
    UpgradeBleed,     // leaves a bleed: % of the damage over Lingering Wound's duration
    UpgradeCooldown,  // cooldown shorter by %
    UpgradeDamage,    // damage and healing increased by %
};
constexpr uint8 AllSlots = 255;
constexpr uint8 SlotCount = 18;
constexpr uint32 CreatureTypeBit(uint32 creatureType) { return creatureType ? 1u << (creatureType - 1) : 0; }
constexpr uint32 AnyCreatureType = 0xFFFFFFFF;
// The slot node of a slot (nullptr when there is none).
TalentDef const* SlotNode(uint8 slot);
// The name of one of the Skinshifter's own spells ("" for others).
char const* NameOf(uint32 spellId);
// Talents with an effect of their own in the module (the aura is a marker).
enum TalentSpell : uint32
{
    TalentHardenedHide  = 1990341,  // Borrowed Hide twice as strong
    TalentCleanGetaway  = 1990356,  // Shed Skin / Release also free you from snares
    TalentPerfectMimic  = 1990376,  // Mimic Strike's echo at full strength
    TalentThousandSkins = 1990381,  // every skin gives Borrowed Hide, not only beasts
};
// The specialization rows of Character Advancement: its tree's tab, its ChrSpecs row and its two entries.
struct AdvancementSpec
{
    uint8 spec;              // 1..4
    uint32 chrSpec;          // ChrSpecs id
    uint32 tab;              // CharacterAdvancementTabTypes id
    char const* token;       // BEASTSKIN, ...
    uint32 signatureEntry;   // class tree, row 0: the signature ability
    uint32 identityEntry;    // the spec tree: the identity passive (choosing it chooses the spec)
    char const* primaryStat; // as ChrSpecs writes it: Agility, Spirit, Intellect
    char const* description;
};
std::vector<AdvancementSpec> const& AdvancementSpecs();
constexpr uint32 AdvancementClassType = 47;  // CharacterAdvancementClassTypes row of class 33
// Writes the Skinshifter's rows of the Character Advancement tables (and ChrClasses) into DataDir/dbc_skinshifter/
// (the server reads those instead of DataDir/dbc/ with skinshifter-core.patch) and skinshifter_advancement.txt for
// the client patch, which also carries the Root Walk node looks the client lacks. Call before the server loads the
// CoA talent catalog (OnStartup).
// iconOfSpell: the SpellIcon id of every spell a talent or spec row takes its icon from (TalentDef::iconFrom and the
// signatures). Returns false (and writes nothing) when the server's tables are not as expected.
// talents false: only the Root Walk node looks are written (Skinshifter.Advancement.Enable = 0).
bool InstallAdvancement(std::string const& dataDir, std::map<uint32, uint32> const& iconOfSpell, bool talents);
void RemoveAdvancement(std::string const& dataDir);
// Root Walk: the creature look (and the size it is shown at) of a gathering node, by the node's
// GameObjectDisplayInfo id; display 0 when the node has none. Known after InstallAdvancement.
struct NodeLook
{
    uint32 display = 0;
    float scale = 1.0f;
};
NodeLook NodeLookOf(uint32 objectDisplayId);
// Root Walk's worm and mound of earth as client rows at their config size (0 when they could not be made).
struct RootWalkLooks
{
    uint32 worm = 0;
    uint32 burrow = 0;
};
RootWalkLooks RootWalkLooksOf();
// The rows were written at this start (Character Advancement can be used).
bool AdvancementInstalled();

// SkinshifterSpec.cpp
uint8 CurrentSpec(class Player const* player);
void LoadSpec(class Player* player);
void ForgetSpec(uint32 guid);
void ApplySpec(class Player* player);
void SendSpecState(class Player* player, bool offer);
bool ChooseSpec(class Player* player, uint8 spec);
bool HandleSpecMessage(class Player* player, std::string_view body);
// The slot messages of the talent window (SkinshifterSlots.cpp); true when the message was one.
bool HandleSlotMessage(class Player* player, std::string_view body);
// Soul / Skin Tokens (SkinshifterTokens.cpp): the addon's TOKENS message, and the GM command .skin tokens <count>.
bool HandleTokenMessage(class Player* player, std::string_view body);
// TIP;<spell id>: the addon's tooltip lines of what the spell really does (SkinshifterSpellText.cpp).
bool HandleSpellTextMessage(class Player* player, std::string_view body);
// The spells a creature casts by its script (SmartAI), indexed at startup (Skinshifter.cpp).
std::vector<uint32> CreatureScriptedSpells(uint32 entry);
// The worn skin's effects (a glow, a fog) off while the body is hidden in a takeover, back on after (Skinshifter.cpp).
void RestoreOwnLook(class Player* player);
void RefreshBodyFx(class Player* player, bool hidden);
bool HandleTokensCommand(class ChatHandler* handler, uint32 count);
// Tokens for the takeovers: how many you have, the cost of a creature of this rank (0 for a normal one), and
// spending or earning them (an earned token keeps the creature given, as a kill would).
uint32 TokenCount(class Player* player);
uint32 TokenCostOf(uint32 rank, bool boss);
bool SpendTokens(class Player* player, uint32 count);
void EarnToken(class Player* player, class Creature const* creature);
// Takeover (SkinshifterTakeover.cpp).
bool InTakeover(class Player const* player);
// The creature someone is inside (kills of and by it do not count for tokens).
bool IsTakeoverCreature(class Creature const* creature);
// Called from the Skinshifter's own spell handling: true when the spell was a takeover one.
bool HandleTakeoverSpell(class Player* player, class Unit* target, uint32 spellId);
// Before the cast: false (with the reason told) when the takeover cannot happen.
bool TakeoverCanStart(class Player* player, class Unit* target, uint32 spellId);
// The possess bar's cancel button (CMSG_CANCEL_AURA of TakeoverControl): ends the takeover.
bool HandleTakeoverCancel(class Player* player, uint32 spellId);
// Grimoire copies with the creature type and level they come from (for the takeover kit).
struct CopyInfo { uint32 copy; uint8 type; uint8 level; };
std::vector<CopyInfo> const& AllCopies();
void OnSkinshifterLevel(class Player* player, uint8 oldLevel);
// Character Advancement (Skinshifter.Advancement.Enable): the specialization is chosen there, not in the module's
// window; the module only keeps the extras of a spec (Beastskin's Dual Wield) with its identity passive.
bool AdvancementEnabled();
void SyncSpecExtras(class Player* player, uint32 spellId, bool learned);
// The weapons, shield and off-hand items you can no longer use (after a spec change) go back to your bags (or by
// mail when they are full).
void UnequipUnusable(class Player* player);
// Refunds a talent tree whose learned talents no longer fit it (after a rework); the number refunded.
uint32 RefundBrokenTrees(class Player* player);
// Gives a Skinshifter the five tab skills (each login, so older characters get them too).
void LearnSkillLines(class Player* player);

// Races that play the class as a Possessor (Skinshifter.Class.PossessorRaces).
bool IsPossessorRace(uint8 race);
// Possessor races that are energy rather than ghosts (Skinshifter.Fx.EnergyRaces).
bool IsEnergyRace(uint8 race);

// The starter abilities for this race, and the ones a Possessor gets instead (pairs: Skinshifter, Possessor).
// From Skinshifter.Takeover.Level (20) also the two takeovers.
std::vector<uint32> StarterSpells(uint8 race, uint8 level = 0);
// The level the takeovers come at (Skinshifter.Takeover.Level).
uint8 TakeoverLevel();
std::vector<std::pair<uint32, uint32>> PossessorSwaps();
// Spells that were in the starter kit and are now stolen from low-level creatures instead (Stolen Vigor stayed:
// the one heal a level 1 Skinshifter has).
std::vector<uint32> FormerStarterSpells();
void Install();
void Remove();

// Grimoire copies: every creature ability that can be stolen has a Skinshifter copy (spell ids CopyFirst..CopyLast)
// with a global cooldown, a cooldown of its own, a mana cost, damage that grows with the level, and the spellbook
// tab of its creature type. A Skinshifter learns the copy, not the creature's spell. Made at startup from the world
// database (creature_template_spell, smart_scripts) and the server's Spell.dbc; the ids are kept in
// `skinshifter_copy`, so a copy keeps its id when more are added.
constexpr uint32 CopyFirst = 1991000;
constexpr uint32 CopyLast = 2000999;
uint32 CopyOf(uint32 original);  // 0: no copy
uint32 OriginalOf(uint32 copy);  // 0: not a copy
// The rules for what can be stolen, shared by the copies (from the raw Spell.dbc) and Steal Form (from SpellInfo).
bool NameIsJunk(std::string const& name);
bool IsForbiddenEffect(uint32 effect);
bool IsForbiddenAura(uint32 aura);
// Skinshifter.cpp: every ability a low creature without abilities of its own teaches, with its creature type.
std::vector<std::pair<uint32, uint32>> StarterCreatureSpells();
// Logs which Skinshifter spells the server loaded (call after the spells are loaded).
void Report();
}

#endif
