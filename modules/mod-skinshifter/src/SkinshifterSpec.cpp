/*
 * mod-skinshifter - the level 10 specialization (Beastskin, Courtier, Felstriker, Elementalist).
 *
 * Two ways, by Skinshifter.Advancement.Enable:
 *   1 (default): CoA's Character Advancement. The class tree holds the four signatures, each spec tree its identity
 *     passive and talents (SkinshifterAdvancement.cpp writes the rows); CoA teaches them. The current spec is the
 *     identity passive the character knows; the module only keeps a spec's extras with it (Beastskin: Dual Wield).
 *     A spec chosen in the module's own window before is taken back once, so it is chosen in CoA's window.
 *   0: the module's own choice, stored in `skinshifter_spec`; a specialization is its signature ability and its
 *     identity passive. The window is in the SkinCollection addon.
 * The messages are hidden addon whispers to oneself with the prefix "SKIN":
 *   client -> server  SPECS (what is my state), SPEC;<n> (choose n)
 *   server -> client  SPEC;<current>;<level needed>;<change cost in copper>;<1: Character Advancement>,
 *                     SPECOFFER (open the window; only without Character Advancement)
 *
 * This code is released under the GNU AGPL v3 like AzerothCore.
 */

#include "AscensionSpecialization.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "Player.h"
#include "SkinshifterSpells.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace
{
constexpr uint8 SkinshifterClassId = 33;
std::unordered_map<uint32, uint8> Specs;

void SendLine(Player* player, std::string const& line)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, "SKIN\t" + line);
    player->SendDirectMessage(&packet);
}

uint32 ChangeCost()
{
    return sConfigMgr->GetOption<uint32>("Skinshifter.Spec.ChangeCost", 0);
}

void RemoveActionButtonsFor(Player* player, uint32 spellId)
{
    for (uint8 slot = 0; slot < MAX_ACTION_BUTTONS; ++slot)
        if (ActionButton const* action = player->GetActionButton(slot))
            if (action->GetType() == ACTION_BUTTON_SPELL && action->GetAction() == spellId)
                player->removeActionButton(slot);
}

void PlaceOnBar(Player* player, uint32 spellId)
{
    for (uint8 slot = 0; slot < 12; ++slot)
    {
        ActionButton const* action = player->GetActionButton(slot);
        if (action && action->GetType() == ACTION_BUTTON_SPELL && action->GetAction() == spellId)
            return;
    }
    for (uint8 slot = 0; slot < 12; ++slot)
        if (!player->GetActionButton(slot))
        {
            player->addActionButton(slot, spellId, ACTION_BUTTON_SPELL);
            return;
        }
}
}

namespace SkinshifterSpells
{
bool AdvancementEnabled()
{
    return AdvancementInstalled() && sConfigMgr->GetOption<bool>("Skinshifter.Advancement.Enable", true);
}

uint8 CurrentSpec(Player const* player)
{
    if (AdvancementEnabled())
    {
        for (uint8 spec = 1; spec <= SpecCount; ++spec)
            if (player->HasSpell(SpecOf(spec).passive))
                return spec;
        return SpecNone;
    }
    auto const spec = Specs.find(player->GetGUID().GetCounter());
    return spec == Specs.end() ? SpecNone : spec->second;
}

// A tree rework moves and removes nodes: a build learned on the old tree can hold a talent whose parents are no
// longer learned, or more ranks than its node has now, and the client then refuses every change to it ("Invalid build
// path"). Such a tree is refunded whole, so its points can be spent again; the others stay as they are.
uint32 RefundBrokenTrees(Player* player)
{
    if (!AdvancementEnabled())
        return 0;
    std::unordered_map<uint32, uint32> rankOf;
    for (TalentDef const& talent : Talents())
        rankOf[talent.entry] = GetAscensionTalentRank(player, talent.entry);
    // The client checks a build by learning it again, one talent after another, in any order that works: a talent
    // needs one of the talents it hangs from, the points its row's gate asks for in the tree (rows 4 and 8 of the
    // class tree at 8 and 16, rows 4 and 7 of a spec tree at 5 and 10), at most its ranks, and no other talent of its
    // pick-one group. A tree that cannot be learned that way is refunded.
    std::array<bool, SpecCount + 1> broken{};
    for (uint8 tree = 0; tree <= SpecCount; ++tree)
    {
        std::vector<TalentDef const*> left;
        for (TalentDef const& talent : Talents())
            if (talent.tree == tree && rankOf[talent.entry])
            {
                if (rankOf[talent.entry] > talent.ranks)
                    broken[tree] = true;
                left.push_back(&talent);
            }
        std::unordered_set<uint32> placed;
        std::unordered_set<uint32> groups;
        uint32 spent = 0;
        for (bool progress = true; progress && !left.empty() && !broken[tree];)
        {
            progress = false;
            for (auto it = left.begin(); it != left.end();)
            {
                TalentDef const& talent = **it;
                bool hangs = false;
                bool held = false;
                for (uint32 parent : talent.from)
                    if (parent)
                    {
                        hangs = true;
                        held = held || placed.count(parent) || !rankOf.count(parent);  // a signature: not ours
                    }
                uint32 const gate = tree == 0 ? (talent.row >= 8 ? 16 : talent.row >= 4 ? 8 : 0) :
                    (talent.row >= 7 ? 10 : talent.row >= 4 ? 5 : 0);
                if ((!hangs || held) && spent >= gate && !(talent.group && groups.count(talent.group)))
                {
                    placed.insert(talent.entry);
                    if (talent.group)
                        groups.insert(talent.group);
                    spent += rankOf[talent.entry];
                    it = left.erase(it);
                    progress = true;
                }
                else
                    ++it;
            }
        }
        if (!left.empty())
            broken[tree] = true;
    }
    uint32 refunded = 0;
    for (TalentDef const& talent : Talents())
        if (broken[talent.tree] && rankOf[talent.entry] && SetAscensionTalentRank(player, talent.entry, 0))
            ++refunded;
    if (refunded)
    {
        LOG_INFO("module", "Skinshifter: {}'s talents did not fit the changed trees; {} refunded.", player->GetName(),
            refunded);
        ChatHandler(player->GetSession()).PSendSysMessage("Skinshifter: your talent trees have changed, so the talents "
            "in the changed trees were refunded ({}). Spend the points again.", refunded);
    }
    return refunded;
}

// A spec's extra spells (weapons, shield, Dual Wield) follow it: with Character Advancement its identity passive,
// without it the chosen spec. Two specs can share one (Shield), so what is wanted is worked out over all specs first.
void SyncAllSpecExtras(Player* player)
{
    std::set<uint32> wanted, all;
    uint8 const chosen = AdvancementEnabled() ? SpecNone :
        (player->GetLevel() >= SpecLevel ? CurrentSpec(player) : SpecNone);
    for (uint8 spec = 1; spec <= SpecCount; ++spec)
    {
        SpecInfo const& info = SpecOf(spec);
        bool const active = AdvancementEnabled() ? player->HasSpell(info.passive) : spec == chosen;
        for (uint32 spellId : info.extras)
            if (spellId && sSpellMgr->GetSpellInfo(spellId))
            {
                all.insert(spellId);
                if (active)
                    wanted.insert(spellId);
            }
    }
    bool removed = false;
    for (uint32 spellId : all)
    {
        if (wanted.count(spellId) && !player->HasSpell(spellId))
            player->learnSpell(spellId);
        else if (!wanted.count(spellId) && player->HasSpell(spellId))
        {
            player->removeSpell(spellId, SPEC_MASK_ALL, false);
            removed = true;
        }
    }
    if (removed)
        UnequipUnusable(player);
}

void SyncSpecExtras(Player* player, uint32 spellId, bool /*learned*/)
{
    if (!AdvancementEnabled())
        return;
    for (uint8 spec = 1; spec <= SpecCount; ++spec)
        if (SpecOf(spec).passive == spellId)
        {
            SyncAllSpecExtras(player);
            return;
        }
}

void UnequipUnusable(Player* player)
{
    for (uint8 slot : { EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND, EQUIPMENT_SLOT_RANGED })
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item || player->CanUseItem(item, false) == EQUIP_ERR_OK)
            continue;
        ItemPosCountVec dest;
        if (player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) == EQUIP_ERR_OK)
        {
            player->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
            player->StoreItem(dest, item, true);
        }
        else
        {
            player->MoveItemFromInventory(INVENTORY_SLOT_BAG_0, slot, true);
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            item->DeleteFromInventoryDB(trans);
            item->SaveToDB(trans);
            MailDraft("Your weapon", "Your specialization cannot use this any more, and your bags were full.")
                .AddItem(item).SendMailTo(trans, player, MailSender(player, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
            CharacterDatabase.CommitTransaction(trans);
        }
        ChatHandler(player->GetSession()).PSendSysMessage("Your specialization cannot use {}; it goes back to your bags.",
            item->GetTemplate()->Name1);
    }
    player->AutoUnequipOffhandIfNeed();
}

namespace
{
// A spec chosen in the module's window before Character Advancement: its spells go (CoA teaches them again when the
// spec is chosen there) and the row with it.
void TakeBackModuleSpec(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    auto const old = Specs.find(guid);
    if (old == Specs.end())
        return;
    for (uint8 spec = 1; spec <= SpecCount; ++spec)
    {
        SpecInfo const& info = SpecOf(spec);
        for (uint32 spellId : { info.signature, info.passive })
            if (spellId && player->HasSpell(spellId))
            {
                player->removeSpell(spellId, SPEC_MASK_ALL, false);
                RemoveActionButtonsFor(player, spellId);
            }
    }
    Specs.erase(old);
    CharacterDatabase.Execute("DELETE FROM `skinshifter_spec` WHERE `guid` = {}", guid);
    player->SendActionButtons(1);
    ChatHandler(player->GetSession()).SendSysMessage("Skinshifter specializations are now chosen in Character "
        "Advancement (N). Choose yours there again.");
    LOG_INFO("module", "Skinshifter: {}'s specialization from the module's window was taken back for Character "
        "Advancement.", player->GetName());
}
}

void LoadSpec(Player* player)
{
    uint32 const guid = player->GetGUID().GetCounter();
    Specs.erase(guid);
    if (QueryResult result = CharacterDatabase.Query("SELECT `spec` FROM `skinshifter_spec` WHERE `guid` = {}", guid))
    {
        uint8 const spec = result->Fetch()[0].Get<uint8>();
        if (spec >= 1 && spec <= SpecCount)
            Specs[guid] = spec;
    }
}

void ForgetSpec(uint32 guid)
{
    Specs.erase(guid);
}

// The spells of the stored specialization are known (and on the bar the first time), every other
// specialization's are not. Below level 10 no specialization applies. Losing Dual Wield puts the off-hand weapon
// back in the bags (the core does that when the spell goes).
void ApplySpec(Player* player)
{
    if (AdvancementEnabled())
    {
        TakeBackModuleSpec(player);
        SyncAllSpecExtras(player);
        return;
    }
    uint8 const current = player->GetLevel() >= SpecLevel ? CurrentSpec(player) : SpecNone;
    bool changed = false;
    for (uint8 spec = 1; spec <= SpecCount; ++spec)
    {
        SpecInfo const& info = SpecOf(spec);
        for (uint32 spellId : { info.signature, info.passive })
        {
            if (!spellId || !sSpellMgr->GetSpellInfo(spellId))
                continue;
            if (spec == current && !player->HasSpell(spellId))
            {
                player->learnSpell(spellId);
                if (spellId == info.signature)
                    PlaceOnBar(player, spellId);
                changed = true;
            }
            else if (spec != current && player->HasSpell(spellId))
            {
                player->removeSpell(spellId, SPEC_MASK_ALL, false);
                RemoveActionButtonsFor(player, spellId);
                changed = true;
            }
        }
    }
    if (changed)
        player->SendActionButtons(1);
    SyncAllSpecExtras(player);
}

void SendSpecState(Player* player, bool offer)
{
    bool const advancement = AdvancementEnabled();
    SendLine(player, Acore::StringFormat("SPEC;{};{};{};{}", uint32(CurrentSpec(player)), uint32(SpecLevel),
        ChangeCost(), advancement ? 1 : 0));
    if (offer && !advancement)
        SendLine(player, "SPECOFFER");
}

bool ChooseSpec(Player* player, uint8 spec)
{
    ChatHandler handler(player->GetSession());
    if (player->getClass() != SkinshifterClassId || spec < 1 || spec > SpecCount)
        return false;
    if (AdvancementEnabled())
    {
        handler.SendSysMessage("Choose your specialization in Character Advancement (N).");
        return false;
    }
    if (player->GetLevel() < SpecLevel)
    {
        handler.PSendSysMessage("You can choose a specialization at level {}.", uint32(SpecLevel));
        return false;
    }
    uint8 const current = CurrentSpec(player);
    if (current == spec)
        return true;
    if (current != SpecNone)
    {
        if (player->IsInCombat())
        {
            handler.SendSysMessage("You cannot change your specialization in combat.");
            return false;
        }
        uint32 const cost = ChangeCost();
        if (cost && !player->HasEnoughMoney(cost))
        {
            handler.SendSysMessage("You cannot afford to change your specialization.");
            return false;
        }
        if (cost)
            player->ModifyMoney(-int32(cost));
    }
    uint32 const guid = player->GetGUID().GetCounter();
    Specs[guid] = spec;
    CharacterDatabase.Execute("REPLACE INTO `skinshifter_spec` (`guid`, `spec`) VALUES ({}, {})", guid, uint32(spec));
    ApplySpec(player);
    handler.PSendSysMessage("You are now a {}.", SpecOf(spec).name);
    LOG_INFO("module", "Skinshifter: {} chose the {} specialization.", player->GetName(), SpecOf(spec).name);
    return true;
}

bool HandleSpecMessage(Player* player, std::string_view body)
{
    if (body == "SPECS")
    {
        SendSpecState(player, false);
        return true;
    }
    if (body.substr(0, 5) == "SPEC;")
    {
        if (Optional<uint32> spec = Acore::StringTo<uint32>(body.substr(5)))
            ChooseSpec(player, uint8(*spec));
        SendSpecState(player, false);
        return true;
    }
    return false;
}

void OnSkinshifterLevel(Player* player, uint8 oldLevel)
{
    ApplySpec(player);
    if (oldLevel < SpecLevel && player->GetLevel() >= SpecLevel && CurrentSpec(player) == SpecNone)
        SendSpecState(player, true);
}
}
