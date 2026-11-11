/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "Player.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "UnitScript.h"
#include <algorithm>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

Player* TwoPlayersSeatedPartner(Player const* head);

namespace
{
constexpr uint32 AgreeAura = 9530131;
constexpr uint32 DisagreeAura = 9530132;

std::mutex& mirrorMutex = *new std::mutex;
std::set<std::pair<uint32, uint32>>& mirrors = *new std::set<std::pair<uint32, uint32>>;
thread_local bool mirroring = false;

std::pair<uint32, uint32> KeyOf(Player const* holder, uint32 spellId)
{
    return { holder->GetGUID().GetCounter(), spellId };
}

bool IsMirror(Player const* holder, uint32 spellId)
{
    std::lock_guard<std::mutex> lock(mirrorMutex);
    return mirrors.count(KeyOf(holder, spellId)) != 0;
}

void MarkMirror(Player const* holder, uint32 spellId, bool mirror)
{
    std::lock_guard<std::mutex> lock(mirrorMutex);
    if (mirror)
        mirrors.insert(KeyOf(holder, spellId));
    else
        mirrors.erase(KeyOf(holder, spellId));
}

bool Shared(SpellInfo const* info)
{
    if (!info || info->IsPassive() || !info->IsPositive() || info->Id == AgreeAura || info->Id == DisagreeAura ||
        (info->AuraInterruptFlags & AURA_INTERRUPT_FLAG_NOT_SEATED))
        return false;
    for (SpellEffectInfo const& effect : info->Effects)
    {
        switch (effect.ApplyAuraName)
        {
            case SPELL_AURA_MOUNTED:
            case SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED:
            case SPELL_AURA_MOD_SHAPESHIFT:
            case SPELL_AURA_CONTROL_VEHICLE:
            case SPELL_AURA_TRANSFORM:
            case SPELL_AURA_FLY:
            case SPELL_AURA_BIND_SIGHT:
            case SPELL_AURA_FAR_SIGHT:
            case SPELL_AURA_MOD_POSSESS:
            case SPELL_AURA_MOD_CHARM:
                return false;
            default:
                break;
        }
    }
    return true;
}

void CopyOnto(Player* other, Aura const* original)
{
    uint32 const spellId = original->GetId();
    Aura* copy = other->GetAura(spellId);
    if (copy && !IsMirror(other, spellId))
        return;
    mirroring = true;
    if (!copy)
    {
        copy = other->AddAura(spellId, other);
        if (copy)
            MarkMirror(other, spellId, true);
    }
    if (copy)
    {
        int32 const duration = original->GetDuration();
        copy->SetMaxDuration(duration < 0 ? -1 : std::max(original->GetMaxDuration(), duration));
        copy->SetDuration(duration);
        if (copy->GetStackAmount() != original->GetStackAmount())
            copy->SetStackAmount(original->GetStackAmount());
        if (copy->GetCharges() != original->GetCharges())
            copy->SetCharges(original->GetCharges());
        copy->SetNeedClientUpdateForTargets();
    }
    mirroring = false;
}
}

void TwoPlayersShareBuffs(Player* first, Player* second)
{
    for (auto const& [from, to] : { std::pair<Player*, Player*>(first, second), { second, first } })
    {
        std::vector<Aura*> owned;
        for (auto const& [spellId, aura] : from->GetOwnedAuras())
            if (Shared(aura->GetSpellInfo()) && !IsMirror(from, spellId))
                owned.push_back(aura);
        for (Aura* aura : owned)
            CopyOnto(to, aura);
    }
}

void TwoPlayersDropSharedBuffs(Player* holder)
{
    std::vector<uint32> spells;
    {
        std::lock_guard<std::mutex> lock(mirrorMutex);
        uint32 const guid = holder->GetGUID().GetCounter();
        for (auto itr = mirrors.lower_bound({ guid, 0 }); itr != mirrors.end() && itr->first == guid;)
        {
            spells.push_back(itr->second);
            itr = mirrors.erase(itr);
        }
    }
    mirroring = true;
    for (uint32 spellId : spells)
        holder->RemoveAurasDueToSpell(spellId);
    mirroring = false;
}

class mod_subclass_two_players_auras : public UnitScript
{
public:
    mod_subclass_two_players_auras() : UnitScript("mod_subclass_two_players_auras", true,
        { UNITHOOK_ON_AURA_APPLY, UNITHOOK_ON_AURA_REMOVE }) { }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        Player* head = unit ? unit->ToPlayer() : nullptr;
        if (mirroring || !head || aura->GetOwner() != unit || !Shared(aura->GetSpellInfo()) ||
            IsMirror(head, aura->GetId()))
            return;
        if (Player* other = TwoPlayersSeatedPartner(head))
            CopyOnto(other, aura);
    }

    void OnAuraRemove(Unit* unit, AuraApplication* application, AuraRemoveMode mode) override
    {
        Player* head = unit ? unit->ToPlayer() : nullptr;
        Aura const* aura = application ? application->GetBase() : nullptr;
        if (mirroring || !head || !aura || aura->GetOwner() != unit || !Shared(aura->GetSpellInfo()))
            return;
        uint32 const spellId = aura->GetId();
        Player* other = TwoPlayersSeatedPartner(head);
        if (IsMirror(head, spellId))
        {
            MarkMirror(head, spellId, false);
            if (mode == AURA_REMOVE_BY_EXPIRE || !other || IsMirror(other, spellId))
                return;
            mirroring = true;
            other->RemoveAurasDueToSpell(spellId);
            mirroring = false;
            return;
        }
        if (!other || !IsMirror(other, spellId))
            return;
        MarkMirror(other, spellId, false);
        mirroring = true;
        other->RemoveAurasDueToSpell(spellId);
        mirroring = false;
    }
};

void AddSC_mod_subclass_two_players_auras()
{
    new mod_subclass_two_players_auras();
}
