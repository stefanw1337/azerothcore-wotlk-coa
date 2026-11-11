/*
 * mod-skinshifter - the slots of the talent trees: what each slot holds, and the upgrades under it.
 *
 * A slot node (SkinshifterSpells.cpp, the slot trees) opens a slot once learned. The player chooses the stolen ability
 * that goes in it in the talent window (the SkinCollection addon shows the Grimoire's abilities of the slot's
 * creature types when you point at the node). An upgrade node improves whatever is in its slot:
 *   UpgradeDouble    a chance that the ability strikes again (the copy is a free, triggered cast)
 *   UpgradeCrit      more critical strike chance for it
 *   UpgradeBleed     it leaves Lingering Wound: a part of its damage as a bleed
 *   UpgradeCooldown  a shorter cooldown
 *   UpgradeDamage    more damage and healing (AllSlots: for every slot)
 * Skinshifter.Slots.Enforce = 1 makes the slots the loadout: a stolen ability is only usable while it is in an open
 * slot (off by default until the first slots that come without talents exist).
 * Stored in `skinshifter_slot` (guid, slot, spell). Messages (hidden addon whispers, prefix "SKIN"):
 *   client -> server  SLOTS (send the state), SLOT;<slot>;<spell, 0 to empty it>
 *   server -> client  SLOTDEF;<entry>;<slot>;<creature type mask>;<name>, SLOT;<slot>;<spell>;<1: open>, SLOTEND
 *
 * This code is released under the GNU AGPL v3 like AzerothCore.
 */

#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SkinshifterSpells.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Tokenize.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
using namespace SkinshifterSpells;

constexpr uint8 SkinshifterClassId = 33;
bool Enforce = false;

struct SlotState
{
    std::array<uint32, SlotCount + 1> spell{};  // by slot number; [0] unused
};
std::unordered_map<uint32, SlotState> State;

bool IsSkinshifter(Player const* player)
{
    return player && player->getClass() == SkinshifterClassId;
}

uint32 RankOf(Player const* player, TalentDef const& talent)
{
    for (uint32 rank = talent.ranks; rank > 0; --rank)
        if (player->HasSpell(talent.firstSpell + rank - 1))
            return rank;
    return 0;
}

bool SlotOpen(Player const* player, uint8 slot)
{
    TalentDef const* node = SlotNode(slot);
    return node && player->HasSpell(node->firstSpell);
}

// The open slot the spell is in (0: none).
uint8 SlotOfSpell(Player const* player, uint32 spellId)
{
    auto const state = State.find(player->GetGUID().GetCounter());
    if (state == State.end() || !spellId)
        return 0;
    for (uint8 slot = 1; slot <= SlotCount; ++slot)
        if (state->second.spell[slot] == spellId && SlotOpen(player, slot))
            return slot;
    return 0;
}

int32 UpgradeOf(Player const* player, uint8 slot, UpgradeKind kind)
{
    int32 amount = 0;
    for (TalentDef const& talent : Talents())
    {
        if (talent.upgradeOf != slot && talent.upgradeOf != AllSlots)
            continue;
        int32 const perRank = talent.upgrade == kind ? talent.amountPerRank : talent.upgrade2 == kind ?
            talent.amount2 : talent.upgrade3 == kind ? talent.amount3 : 0;  // a set gives more than one kind
        if (perRank)
            if (uint32 const rank = RankOf(player, talent))
                amount += perRank * int32(rank);
    }
    return amount;
}

// The creature types (CreatureTypeBit) the player stole this ability from; 0 when it is not in the Grimoire.
uint32 GrimoireTypes(Player const* player, uint32 learnedSpell)
{
    uint32 types = 0;
    if (QueryResult result = CharacterDatabase.Query("SELECT `spell`, `entry` FROM `skinshifter_grimoire` WHERE `guid` = {}",
        player->GetGUID().GetCounter()))
        do
        {
            Field* fields = result->Fetch();
            uint32 const stolen = fields[0].Get<uint32>();
            uint32 const copy = CopyOf(stolen);
            uint32 const learned = copy && sSpellMgr->GetSpellInfo(copy) ? copy : stolen;
            if (learned != learnedSpell)
                continue;
            if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(fields[1].Get<uint32>()))
                types |= CreatureTypeBit(creature->type) ? CreatureTypeBit(creature->type) : 1u << 31;
        } while (result->NextRow());
    return types;
}

void SendLine(Player* player, std::string const& line)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, "SKIN\t" + line);
    player->SendDirectMessage(&packet);
}

void SendSlots(Player* player)
{
    auto const& state = State[player->GetGUID().GetCounter()];
    for (TalentDef const& talent : Talents())
        if (talent.slot)
            SendLine(player, Acore::StringFormat("SLOTDEF;{};{};{};{}", talent.entry, uint32(talent.slot),
                talent.slotTypes, talent.name));
    for (uint8 slot = 1; slot <= SlotCount; ++slot)
        SendLine(player, Acore::StringFormat("SLOT;{};{};{}", uint32(slot), state.spell[slot],
            SlotOpen(player, slot) ? 1 : 0));
    SendLine(player, "SLOTEND");
}

void Load(Player* player)
{
    SlotState state;
    if (QueryResult result = CharacterDatabase.Query("SELECT `slot`, `spell` FROM `skinshifter_slot` WHERE `guid` = {}",
        player->GetGUID().GetCounter()))
        do
        {
            Field* fields = result->Fetch();
            uint8 const slot = fields[0].Get<uint8>();
            if (slot >= 1 && slot <= SlotCount)
                state.spell[slot] = fields[1].Get<uint32>();
        } while (result->NextRow());
    State[player->GetGUID().GetCounter()] = state;
}

void Assign(Player* player, uint8 slot, uint32 spellId)
{
    ChatHandler handler(player->GetSession());
    TalentDef const* node = SlotNode(slot);
    if (!node)
        return;
    if (player->IsInCombat())
    {
        handler.SendSysMessage("You cannot change your slots in combat.");
        return;
    }
    uint32 const guid = player->GetGUID().GetCounter();
    SlotState& state = State[guid];
    if (spellId)
    {
        uint32 const types = GrimoireTypes(player, spellId);
        if (!types)
        {
            handler.SendSysMessage("That ability is not in your Grimoire.");
            return;
        }
        if (!(types & node->slotTypes))
        {
            handler.PSendSysMessage("{} does not take that kind of ability.", node->name);
            return;
        }
        for (uint8 other = 1; other <= SlotCount; ++other)  // an ability is in one slot at a time
            if (other != slot && state.spell[other] == spellId)
            {
                state.spell[other] = 0;
                CharacterDatabase.Execute("DELETE FROM `skinshifter_slot` WHERE `guid` = {} AND `slot` = {}", guid,
                    uint32(other));
            }
    }
    state.spell[slot] = spellId;
    if (spellId)
        CharacterDatabase.Execute("REPLACE INTO `skinshifter_slot` (`guid`, `slot`, `spell`) VALUES ({}, {}, {})", guid,
            uint32(slot), spellId);
    else
        CharacterDatabase.Execute("DELETE FROM `skinshifter_slot` WHERE `guid` = {} AND `slot` = {}", guid, uint32(slot));
    SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr;
    LOG_INFO("module", "Skinshifter: {} puts {} ({}) in slot {} ({}{}).", player->GetName(),
        info ? info->SpellName[0] : "nothing", spellId, uint32(slot), node->name,
        SlotOpen(player, slot) ? "" : ", not learned yet");
    SendSlots(player);
}

// UpgradeDouble: the ability again, a moment later, at the same target (free and triggered, so it cannot repeat).
void StrikeAgain(Player* player, Spell* spell)
{
    ObjectGuid const target = spell->m_targets.GetUnitTargetGUID();
    uint32 const spellId = spell->GetSpellInfo()->Id;
    ObjectGuid const caster = player->GetGUID();
    player->m_Events.AddEventAtOffset([player, caster, target, spellId]()
    {
        if (player->GetGUID() != caster || !player->IsInWorld() || !player->IsAlive())
            return;
        Unit* victim = target ? ObjectAccessor::GetUnit(*player, target) : player;
        if (!victim || !victim->IsAlive())
            return;
        SpellCastResult const result = player->CastSpell(victim, spellId, true);
        LOG_INFO("module", "Skinshifter: {}'s Double Take strikes again with {}: {}.", player->GetName(), spellId,
            result == SPELL_CAST_OK ? "done" : "failed, cast result " + std::to_string(uint32(result)));
    }, 400ms);
}

// UpgradeCooldown: after the cast has set its cooldown (the module's own one for creature abilities too).
void ShortenCooldown(Player* player, uint32 spellId, int32 percent)
{
    ObjectGuid const caster = player->GetGUID();
    player->m_Events.AddEventAtOffset([player, caster, spellId, percent]()
    {
        if (player->GetGUID() != caster || !player->HasSpellCooldown(spellId))
            return;
        uint32 const left = player->GetSpellCooldownDelay(spellId);
        if (int32 const cut = int32(left * uint32(std::min(percent, 100)) / 100))
            player->ModifySpellCooldown(spellId, -cut);
    }, 1ms);
}

void LeaveBleed(Player* player, Unit* victim, int32 damage, int32 percent)
{
    SpellInfo const* wound = sSpellMgr->GetSpellInfo(LingeringWound);
    if (!wound || damage <= 0)
        return;
    int32 ticks = 1;
    if (uint32 const period = wound->Effects[EFFECT_0].Amplitude)
        ticks = std::max(1, wound->GetMaxDuration() / int32(period));
    int32 const perTick = std::max(1, damage * percent / 100 / ticks);
    ObjectGuid const caster = player->GetGUID();
    ObjectGuid const target = victim->GetGUID();
    player->m_Events.AddEventAtOffset([player, caster, target, perTick]()
    {
        if (player->GetGUID() != caster || !player->IsInWorld())
            return;
        if (Unit* victim = ObjectAccessor::GetUnit(*player, target); victim && victim->IsAlive())
        {
            int32 bp = perTick;
            player->CastCustomSpell(victim, LingeringWound, &bp, nullptr, nullptr, true);
        }
    }, 1ms);
}

Player* SlotOwner(Unit* unit, SpellInfo const* spellInfo, uint8& slot)
{
    slot = 0;
    Player* player = unit ? unit->ToPlayer() : nullptr;
    if (!IsSkinshifter(player) || !spellInfo)
        return nullptr;
    slot = SlotOfSpell(player, spellInfo->Id);
    return slot ? player : nullptr;
}

class SkinshifterSlotPlayerScript : public PlayerScript
{
public:
    SkinshifterSlotPlayerScript() : PlayerScript("SkinshifterSlotPlayerScript", { PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_DELETE, PLAYERHOOK_ON_SPELL_CAST, PLAYERHOOK_ON_LEARN_SPELL,
        PLAYERHOOK_ON_FORGOT_SPELL }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (IsSkinshifter(player))
            Load(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        State.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        CharacterDatabase.Execute("DELETE FROM `skinshifter_slot` WHERE `guid` = {}", guid.GetCounter());
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!IsSkinshifter(player) || spell->IsTriggered())
            return;
        uint32 const spellId = spell->GetSpellInfo()->Id;
        uint8 const slot = SlotOfSpell(player, spellId);
        if (!slot)
            return;
        if (int32 const chance = UpgradeOf(player, slot, UpgradeDouble); chance > 0 && roll_chance_i(chance))
            StrikeAgain(player, spell);
        if (int32 const percent = UpgradeOf(player, slot, UpgradeCooldown); percent > 0)
            ShortenCooldown(player, spellId, percent);
    }

    // A slot node learned or unlearned: the window shows the slot open or closed.
    void OnPlayerLearnSpell(Player* player, uint32 spellId) override { SlotNodeChanged(player, spellId); }
    void OnPlayerForgotSpell(Player* player, uint32 spellId) override { SlotNodeChanged(player, spellId); }

private:
    static void SlotNodeChanged(Player* player, uint32 spellId)
    {
        if (!IsSkinshifter(player) || !player->IsInWorld())
            return;
        for (TalentDef const& talent : Talents())
            if (talent.slot && talent.firstSpell == spellId)
            {
                SendSlots(player);
                return;
            }
    }
};

class SkinshifterSlotSpellScript : public AllSpellScript
{
public:
    SkinshifterSlotSpellScript() : AllSpellScript("SkinshifterSlotSpellScript", { ALLSPELLHOOK_ON_CRIT_CHANCE,
        ALLSPELLHOOK_CAN_PREPARE }) { }

    void OnSpellCritChance(Spell* spell, Unit* /*target*/, float& chance) override
    {
        uint8 slot = 0;
        if (Player* player = SlotOwner(spell->GetCaster(), spell->GetSpellInfo(), slot))
            chance += float(UpgradeOf(player, slot, UpgradeCrit));
    }

    // Skinshifter.Slots.Enforce: a stolen ability works only from an open slot.
    bool CanPrepare(Spell* spell, SpellCastTargets const* /*targets*/, AuraEffect const* triggeredByAura) override
    {
        if (!Enforce || triggeredByAura || spell->IsTriggered())
            return true;
        Player* player = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
        if (!IsSkinshifter(player))
            return true;
        uint32 const spellId = spell->GetSpellInfo()->Id;
        if (SlotOfSpell(player, spellId) || !GrimoireTypes(player, spellId))
            return true;
        ChatHandler(player->GetSession()).SendSysMessage("Put that ability in a slot first (point at a slot node "
            "in the talent window).");
        return false;
    }
};

class SkinshifterSlotUnitScript : public UnitScript
{
public:
    SkinshifterSlotUnitScript() : UnitScript("SkinshifterSlotUnitScript", true, { UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
        UNITHOOK_MODIFY_HEAL_RECEIVED }) { }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        uint8 slot = 0;
        Player* player = SlotOwner(attacker, spellInfo, slot);
        if (!player || damage <= 0)
            return;
        if (int32 const percent = UpgradeOf(player, slot, UpgradeDamage))
            damage += damage * percent / 100;
        if (int32 const percent = UpgradeOf(player, slot, UpgradeBleed); percent > 0 && target)
            LeaveBleed(player, target, damage, percent);
    }

    // The heal's caster is one of the two (the core passes them in an unusual order): whichever has it in a slot.
    void ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellInfo const* spellInfo) override
    {
        uint8 slot = 0;
        Player* player = SlotOwner(healer, spellInfo, slot);
        if (!player)
            player = SlotOwner(target, spellInfo, slot);
        if (!player)
            return;
        if (int32 const percent = UpgradeOf(player, slot, UpgradeDamage))
            heal += heal * uint32(percent) / 100;
    }
};

class SkinshifterSlotWorldScript : public WorldScript
{
public:
    SkinshifterSlotWorldScript() : WorldScript("SkinshifterSlotWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        Enforce = sConfigMgr->GetOption<bool>("Skinshifter.Slots.Enforce", false);
    }

    void OnStartup() override
    {
        CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `skinshifter_slot` (`guid` INT UNSIGNED NOT NULL, "
            "`slot` TINYINT UNSIGNED NOT NULL, `spell` INT UNSIGNED NOT NULL, PRIMARY KEY (`guid`, `slot`)) "
            "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
        uint32 slots = 0;
        for (TalentDef const& talent : Talents())
            slots += talent.slot ? 1 : 0;
        LOG_INFO("module", "Skinshifter: {} slots in the talent trees{}.", slots,
            Enforce ? "; stolen abilities work only from a slot" : "");
    }
};
}

namespace SkinshifterSpells
{
bool HandleSlotMessage(Player* player, std::string_view body)
{
    if (!IsSkinshifter(player))
        return false;
    if (body == "SLOTS")
    {
        if (!State.count(player->GetGUID().GetCounter()))
            Load(player);
        SendSlots(player);
        return true;
    }
    if (body.substr(0, 5) != "SLOT;")
        return false;
    std::vector<std::string_view> const fields = Acore::Tokenize(body.substr(5), ';', true);
    if (fields.size() != 2)
        return true;
    Optional<uint32> const slot = Acore::StringTo<uint32>(fields[0]);
    Optional<uint32> const spellId = Acore::StringTo<uint32>(fields[1]);
    if (slot && spellId && *slot >= 1 && *slot <= SlotCount)
        Assign(player, uint8(*slot), *spellId);
    return true;
}
}

void AddSC_skinshifter_slots()
{
    new SkinshifterSlotPlayerScript();
    new SkinshifterSlotSpellScript();
    new SkinshifterSlotUnitScript();
    new SkinshifterSlotWorldScript();
}
