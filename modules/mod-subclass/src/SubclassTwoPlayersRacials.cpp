/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license.
 */

#include "AllSpellScript.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Group.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SubclassRules.h"
#include "Timer.h"
#include "UnitScript.h"
#include "WorldScript.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>

uint32 TwoPlayersPartnerOf(uint32 guid);
bool TwoPlayersIsLeftHead(uint32 guid);
Player* TwoPlayersSeatedPartner(Player const* head);

namespace
{
constexpr uint32 SharedBloodSpell = 9530119;
constexpr uint32 MutateSpell = 9530130;
constexpr uint32 AgreeAura = 9530131;
constexpr uint32 DisagreeAura = 9530132;
constexpr uint32 RacialTickMs = 1000;

std::mutex& racialMutex = *new std::mutex;
std::unordered_map<uint32, Subclass::Accord>& accords = *new std::unordered_map<uint32, Subclass::Accord>;
std::unordered_set<uint32>& mutated = *new std::unordered_set<uint32>;
std::unordered_map<uint32, uint32>& sinceTick = *new std::unordered_map<uint32, uint32>;

void Tell(Player* player, std::string const& text)
{
    ChatHandler(player->GetSession()).SendSysMessage(text);
}

bool TwoPlayersBody(Player const* player)
{
    return Subclass::IsTwoPlayersBody(player->getRace(true), player->GetByteValue(PLAYER_BYTES_3, 0));
}

Subclass::AccordSettings Settings()
{
    Subclass::AccordSettings settings;
    settings.AgreeMaxStacks = sConfigMgr->GetOption<uint32>("Subclass.TwoPlayers.Agree.MaxStacks", 3);
    settings.DisagreeMaxStacks = sConfigMgr->GetOption<uint32>("Subclass.TwoPlayers.Disagree.MaxStacks", 3);
    settings.HoldMs = sConfigMgr->GetOption<uint32>("Subclass.TwoPlayers.AgreeDisagree.Seconds", 5) * IN_MILLISECONDS;
    return settings;
}

float PerStack(char const* option)
{
    return sConfigMgr->GetOption<float>(option, 1.0f);
}

uint32 PairKey(Player const* head)
{
    uint32 const guid = head->GetGUID().GetCounter();
    return TwoPlayersIsLeftHead(guid) ? guid : TwoPlayersPartnerOf(guid);
}

Subclass::Accord AccordOf(Player const* head)
{
    std::lock_guard<std::mutex> lock(racialMutex);
    auto const found = accords.find(PairKey(head));
    return found == accords.end() ? Subclass::Accord{} : found->second;
}

uint64 OwnTarget(Subclass::Accord const& accord, Player const* head)
{
    return TwoPlayersIsLeftHead(head->GetGUID().GetCounter()) ? accord.MeleeTarget : accord.RangedTarget;
}

void ShowAccord(Player* head, Subclass::Accord const& accord)
{
    uint32 const wanted = accord.Mode == Subclass::AccordMode::Agree ? AgreeAura :
        accord.Mode == Subclass::AccordMode::Disagree ? DisagreeAura : 0;
    for (uint32 aura : { AgreeAura, DisagreeAura })
        if (aura != wanted)
            head->RemoveAurasDueToSpell(aura);
    if (!wanted)
        return;
    Aura* aura = head->GetAura(wanted);
    if (!aura)
        aura = head->AddAura(wanted, head);
    if (aura)
        aura->SetStackAmount(uint8(std::max<uint32>(1, accord.Stacks)));
}

void Bicker(Player* body, Player* rider, Subclass::AccordMode mode)
{
    std::string const line = mode == Subclass::AccordMode::Agree ?
        "|cffd8b46a[The body]|r Both heads glare at the same foe. For once, they agree." :
        "|cffd8b46a[The body]|r The heads can't agree on a target and start bickering.";
    Tell(body, line);
    Tell(rider, line);
}

void CountHit(Player* head, Unit* victim)
{
    Player* partner = TwoPlayersSeatedPartner(head);
    if (!partner)
        return;
    bool const byMelee = TwoPlayersIsLeftHead(head->GetGUID().GetCounter());
    Player* body = byMelee ? head : partner;
    Player* rider = byMelee ? partner : head;
    Subclass::Accord before;
    Subclass::Accord after;
    {
        std::lock_guard<std::mutex> lock(racialMutex);
        Subclass::Accord& accord = accords[body->GetGUID().GetCounter()];
        before = accord;
        accord = Subclass::AccordAfterHit(accord, byMelee, victim->GetGUID().GetRawValue(), getMSTime(), Settings());
        after = accord;
    }
    if (after.Mode == before.Mode && after.Stacks == before.Stacks)
        return;
    ShowAccord(body, after);
    ShowAccord(rider, after);
    if (before.Mode != Subclass::AccordMode::None && after.Mode != before.Mode)
        Bicker(body, rider, after.Mode);
}

void FadeAccord(Player* body, Player* rider)
{
    bool faded = false;
    {
        std::lock_guard<std::mutex> lock(racialMutex);
        auto const found = accords.find(body->GetGUID().GetCounter());
        if (found == accords.end())
            return;
        faded = !rider || Subclass::AccordExpired(found->second, getMSTime(), Settings());
        if (faded)
            accords.erase(found);
    }
    if (!faded)
        return;
    ShowAccord(body, {});
    if (rider)
        ShowAccord(rider, {});
}

bool IsMutated(uint32 guid)
{
    std::lock_guard<std::mutex> lock(racialMutex);
    return mutated.count(guid) != 0;
}

void KeepMutatedLook(Player* body)
{
    if (!IsMutated(body->GetGUID().GetCounter()) || body->GetDisplayId() != body->GetNativeDisplayId() ||
        body->IsMounted() || body->GetShapeshiftForm() != FORM_NONE)
        return;
    body->SetDisplayId(Subclass::MutatedBodyDisplay);
}

void Mutate(Player* body)
{
    uint32 const guid = body->GetGUID().GetCounter();
    bool now = false;
    {
        std::lock_guard<std::mutex> lock(racialMutex);
        now = mutated.insert(guid).second;
        if (!now)
            mutated.erase(guid);
    }
    if (now)
    {
        CharacterDatabase.Execute("REPLACE INTO `mod_subclass_two_players_mutated` (`guid`) VALUES ({})", guid);
        KeepMutatedLook(body);
        Tell(body, "Eyes open all over the body. Mutate Me! again to change back.");
        return;
    }
    CharacterDatabase.Execute("DELETE FROM `mod_subclass_two_players_mutated` WHERE `guid` = {}", guid);
    if (body->GetDisplayId() == Subclass::MutatedBodyDisplay)
        body->RestoreDisplayId();
    Tell(body, "The extra eyes close again.");
}

void LearnRacials(Player* player)
{
    if (!TwoPlayersBody(player))
        return;
    for (uint32 spell : { SharedBloodSpell, MutateSpell })
        if (!player->HasSpell(spell))
            player->learnSpell(spell, false);
}

void ShareBlood(Player* rider, Player const* body)
{
    if (!rider->IsAlive() || !body->IsAlive())
        return;
    uint32 const health = std::max<uint32>(1, uint32(rider->GetMaxHealth() * body->GetHealthPct() / 100.0f));
    if (rider->GetHealth() != health)
        rider->SetHealth(health);
}

void RacialTick(Player* player)
{
    LearnRacials(player);
    uint32 const guid = player->GetGUID().GetCounter();
    if (!TwoPlayersPartnerOf(guid))
    {
        KeepMutatedLook(player);
        return;
    }
    Player* partner = TwoPlayersSeatedPartner(player);
    if (!TwoPlayersIsLeftHead(guid))
    {
        if (partner)
            ShareBlood(player, partner);
        return;
    }
    KeepMutatedLook(player);
    FadeAccord(player, partner);
}

class mod_subclass_two_players_racials_player : public PlayerScript
{
public:
    mod_subclass_two_players_racials_player() : PlayerScript("mod_subclass_two_players_racials_player",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_GIVE_EXP })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        if (QueryResult result = CharacterDatabase.Query(
            "SELECT 1 FROM `mod_subclass_two_players_mutated` WHERE `guid` = {}", guid))
        {
            std::lock_guard<std::mutex> lock(racialMutex);
            mutated.insert(guid);
        }
        LearnRacials(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        std::lock_guard<std::mutex> lock(racialMutex);
        mutated.erase(guid);
        sinceTick.erase(guid);
        accords.erase(guid);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        {
            std::lock_guard<std::mutex> lock(racialMutex);
            uint32& since = sinceTick[player->GetGUID().GetCounter()];
            since += diff;
            if (since < RacialTickMs)
                return;
            since = 0;
        }
        RacialTick(player);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 xpSource) override
    {
        if (xpSource != XPSOURCE_KILL || !TwoPlayersSeatedPartner(player))
            return;
        Group const* group = player->GetGroup();
        if (group && group->GetMembersCount() == 2)
            amount *= 2;
    }
};

class mod_subclass_two_players_racials_unit : public UnitScript
{
public:
    mod_subclass_two_players_racials_unit() : UnitScript("mod_subclass_two_players_racials_unit", true,
        { UNITHOOK_ON_DAMAGE, UNITHOOK_ON_BEFORE_ROLL_MELEE_OUTCOME_AGAINST })
    {
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        Player* head = attacker ? attacker->ToPlayer() : nullptr;
        if (!head || !victim || victim == attacker || !damage || !TwoPlayersPartnerOf(head->GetGUID().GetCounter()))
            return;
        Subclass::Accord const accord = AccordOf(head);
        if (accord.Mode == Subclass::AccordMode::Agree && victim->GetGUID().GetRawValue() == accord.MeleeTarget)
            damage += uint32(damage * accord.Stacks * PerStack("Subclass.TwoPlayers.Agree.DamagePerStack") / 100.0f);
        CountHit(head, victim);
    }

    void OnBeforeRollMeleeOutcomeAgainst(Unit const* attacker, Unit const* victim, WeaponAttackType /*attType*/,
        int32& /*attackerMaxSkillValueForLevel*/, int32& /*victimMaxSkillValueForLevel*/,
        int32& /*attackerWeaponSkill*/, int32& /*victimDefenseSkill*/, int32& critChance, int32& missChance,
        int32& /*dodgeChance*/, int32& /*parryChance*/, int32& /*blockChance*/) override
    {
        Player const* head = attacker ? attacker->ToPlayer() : nullptr;
        if (!head || !victim || !TwoPlayersPartnerOf(head->GetGUID().GetCounter()))
            return;
        Subclass::Accord const accord = AccordOf(head);
        uint64 const target = victim->GetGUID().GetRawValue();
        if (accord.Mode == Subclass::AccordMode::Agree && target == accord.MeleeTarget)
            missChance = std::max(0, missChance -
                int32(accord.Stacks * PerStack("Subclass.TwoPlayers.Agree.HitPerStack") * 100.0f));
        else if (accord.Mode == Subclass::AccordMode::Disagree && target == OwnTarget(accord, head))
            critChance += int32(accord.Stacks * PerStack("Subclass.TwoPlayers.Disagree.CritPerStack") * 100.0f);
    }
};

class mod_subclass_two_players_racials_spells : public AllSpellScript
{
public:
    mod_subclass_two_players_racials_spells() : AllSpellScript("mod_subclass_two_players_racials_spells",
        { ALLSPELLHOOK_ON_CAST, ALLSPELLHOOK_ON_CRIT_CHANCE })
    {
    }

    void OnSpellCast(Spell* /*spell*/, Unit* caster, SpellInfo const* spellInfo, bool /*skipCheck*/) override
    {
        Player* player = caster ? caster->ToPlayer() : nullptr;
        if (!player || spellInfo->Id != MutateSpell)
            return;
        uint32 const guid = player->GetGUID().GetCounter();
        Player* body = player;
        if (TwoPlayersPartnerOf(guid) && !TwoPlayersIsLeftHead(guid))
            body = TwoPlayersSeatedPartner(player);
        if (!body)
        {
            Tell(player, "Mutate Me! changes the body you share, and you are not on it right now.");
            return;
        }
        Mutate(body);
    }

    void OnSpellCritChance(Spell* spell, Unit* target, float& chance) override
    {
        Player const* head = spell->GetCaster() ? spell->GetCaster()->ToPlayer() : nullptr;
        if (!head || !target || !TwoPlayersPartnerOf(head->GetGUID().GetCounter()))
            return;
        Subclass::Accord const accord = AccordOf(head);
        if (accord.Mode == Subclass::AccordMode::Disagree && target->GetGUID().GetRawValue() == OwnTarget(accord, head))
            chance += accord.Stacks * PerStack("Subclass.TwoPlayers.Disagree.CritPerStack");
    }
};

class mod_subclass_two_players_racials_world : public WorldScript
{
public:
    mod_subclass_two_players_racials_world() : WorldScript("mod_subclass_two_players_racials_world",
        { WORLDHOOK_ON_STARTUP })
    {
    }

    void OnStartup() override
    {
        CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `mod_subclass_two_players_mutated` ("
            "`guid` INT UNSIGNED NOT NULL, PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 "
            "COLLATE=utf8mb4_unicode_ci");
    }
};
}

void AddSC_mod_subclass_two_players_racials()
{
    new mod_subclass_two_players_racials_player();
    new mod_subclass_two_players_racials_unit();
    new mod_subclass_two_players_racials_spells();
    new mod_subclass_two_players_racials_world();
}
