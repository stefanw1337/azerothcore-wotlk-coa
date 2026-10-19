// Every login, an added race (12 and up, CoaCustomRacials.h) ends up with exactly its own racials: the ones it should
// have are learned, racials of other races (Esteria's, or our older CoA set it used to have) are removed, and the
// borrowed vanilla racial lines are stripped. Creation only covers new characters; this covers the existing ones too,
// for every class (AscensionCompat's own sync only runs for the CoA classes).

#include "CoaCustomRacials.h"
#include "DBCStores.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"

#include <unordered_set>
#include <vector>

namespace
{
    void StripVanillaRacialLines(Player* player)
    {
        for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
        {
            SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(i);
            if (!ability || !IsCoaVanillaRacialSkill(ability->SkillLine))
                continue;
            if (player->HasSpell(ability->Spell))
                player->removeSpell(ability->Spell, SPEC_MASK_ALL, false);
        }

        for (uint32 skill : { 101u, 124u, 125u, 126u, 220u, 733u, 753u, 754u, 756u, 760u, 11125u, 11760u })
            if (player->HasSkill(skill))
                player->SetSkill(skill, 0, 0, 0);
    }
}

class CoaRacialSync final : public PlayerScript
{
public:
    CoaRacialSync() : PlayerScript("CoaRacialSync", { PLAYERHOOK_ON_LOGIN }) { }

    void OnPlayerLogin(Player* player) override
    {
        CoaCustomRacial const* own = GetCoaCustomRacial(player->getRace(true));
        if (!own)
            return;

        std::unordered_set<uint32> wanted;
        for (uint32 spell : own->Spells)
            if (spell)
                wanted.insert(spell);

        std::vector<uint32> stale;
        for (auto const& [spellId, spell] : player->GetSpellMap())
            if (spell->State != PLAYERSPELL_REMOVED && IsCoaManagedRacialSpell(spellId) && !wanted.count(spellId))
                stale.push_back(spellId);
        for (uint32 spellId : stale)
            player->removeSpell(spellId, SPEC_MASK_ALL, false);

        StripVanillaRacialLines(player);

        for (uint32 spellId : wanted)
            if (!player->HasSpell(spellId) && sSpellMgr->GetSpellInfo(spellId))
                player->learnSpell(spellId);
    }
};

void AddSC_CoaEsteriaRacials()
{
    void AddSC_race_racials();
    void AddSC_drakkin_racials();
    void AddSC_drakonid_racials();
    void AddSC_darkfallen_racials();
    void AddSC_broken_racials();

    new CoaRacialSync();
    AddSC_race_racials();
    AddSC_drakkin_racials();
    AddSC_drakonid_racials();
    AddSC_darkfallen_racials();
    AddSC_broken_racials();
}
