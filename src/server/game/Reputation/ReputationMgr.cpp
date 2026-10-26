/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ReputationMgr.h"
#include "ClassicPlusReputation.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

const int32 ReputationMgr::PointsInRank[MAX_REPUTATION_RANK] = {36000, 3000, 3000, 3000, 6000, 12000, 21000, 1000};
const int32 ReputationMgr::Reputation_Cap    =  42999;
const int32 ReputationMgr::Reputation_Bottom = -42000;

// The 3.3.5 client computes DBC base reputation from (race-1)&31. Standing
// in SMSG_INITIALIZE_FACTIONS is an offset from that client base. Using the
// mapped starting-zone mask here made Thunder Bluff send 0 (Tauren base 4000)
// while the client added Blood Elf base 0 → Neutral, and Silvermoon Friendly.
static uint32 ReputationRaceMask(Player const* player)
{
    uint8 const race = player->getRace(true);
    if (!race)
        return 0;
    return 1u << ((race - 1) & 31);
}

ReputationRank ReputationMgr::ReputationToRank(int32 standing)
{
    int32 limit = Reputation_Cap + 1;
    for (int i = MAX_REPUTATION_RANK - 1; i >= MIN_REPUTATION_RANK; --i)
    {
        limit -= PointsInRank[i];
        if (standing >= limit)
            return ReputationRank(i);
    }
    return MIN_REPUTATION_RANK;
}

int32 ReputationMgr::ReputationRankToStanding(ReputationRank rank)
{
    int32 standing = Reputation_Bottom;
    for (uint8 i = MIN_REPUTATION_RANK; i <= rank; ++i)
    {
        standing += PointsInRank[i];
    }

    return std::max(standing - 1, Reputation_Bottom);
}

bool ReputationMgr::IsAtWar(uint32 faction_id) const
{
    FactionEntry const* factionEntry = sFactionStore.LookupEntry(faction_id);

    if (!factionEntry)
    {
        LOG_ERROR("reputation", "ReputationMgr::IsAtWar: Can't get AtWar flag of {} for unknown faction (faction id) #{}.", _player->GetName(), faction_id);
        return 0;
    }

    return IsAtWar(factionEntry);
}

bool ReputationMgr::IsAtWar(FactionEntry const* factionEntry) const
{
    if (!factionEntry)
        return false;

    if (FactionState const* factionState = GetState(factionEntry))
        return (factionState->Flags & FACTION_FLAG_AT_WAR);
    return false;
}

int32 ReputationMgr::GetReputation(uint32 faction_id) const
{
    FactionEntry const* factionEntry = sFactionStore.LookupEntry(faction_id);

    if (!factionEntry)
    {
        LOG_ERROR("reputation", "ReputationMgr::GetReputation: Can't get reputation of {} for unknown faction (faction id) #{}.", _player->GetName(), faction_id);
        return 0;
    }

    return GetReputation(factionEntry);
}

int32 ReputationMgr::GetBaseReputation(FactionEntry const* factionEntry) const
{
    if (!factionEntry)
        return 0;

    uint32 raceMask = ReputationRaceMask(_player);
    uint32 classMask = _player->getClassMask();
    for (int i = 0; i < 4; i++)
    {
        if ((factionEntry->BaseRepRaceMask[i] & raceMask  ||
                (factionEntry->BaseRepRaceMask[i] == 0  &&
                 factionEntry->BaseRepClassMask[i] != 0)) &&
                (factionEntry->BaseRepClassMask[i] & classMask ||
                 factionEntry->BaseRepClassMask[i] == 0))

            return factionEntry->BaseRepValue[i];
    }

    // in faction.dbc exist factions with (RepListId >=0, listed in character reputation list) with all BaseRepRaceMask[i] == 0
    return 0;
}

int32 ReputationMgr::GetReputation(FactionEntry const* factionEntry) const
{
    // Faction without recorded reputation. Just ignore.
    if (!factionEntry)
        return 0;

    if (FactionState const* state = GetState(factionEntry))
        return GetBaseReputation(factionEntry) + state->Standing;

    return 0;
}

ReputationRank ReputationMgr::GetRank(FactionEntry const* factionEntry) const
{
    int32 reputation = GetReputation(factionEntry);
    return ReputationToRank(reputation);
}

ReputationRank ReputationMgr::GetBaseRank(FactionEntry const* factionEntry) const
{
    int32 reputation = GetBaseReputation(factionEntry);
    return ReputationToRank(reputation);
}

void ReputationMgr::ApplyForceReaction(uint32 faction_id, ReputationRank rank, bool apply)
{
    if (apply)
        _forcedReactions[faction_id] = rank;
    else
        _forcedReactions.erase(faction_id);
}

uint32 ReputationMgr::GetDefaultStateFlags(FactionEntry const* factionEntry) const
{
    if (!factionEntry)
        return 0;

    uint32 raceMask = ReputationRaceMask(_player);
    uint32 classMask = _player->getClassMask();
    for (int i = 0; i < 4; i++)
    {
        if ((factionEntry->BaseRepRaceMask[i] & raceMask  ||
                (factionEntry->BaseRepRaceMask[i] == 0  &&
                 factionEntry->BaseRepClassMask[i] != 0)) &&
                (factionEntry->BaseRepClassMask[i] & classMask ||
                 factionEntry->BaseRepClassMask[i] == 0))

            return factionEntry->ReputationFlags[i];
    }
    return 0;
}

void ReputationMgr::SendForceReactions()
{
    WorldPacket data;
    data.Initialize(SMSG_SET_FORCED_REACTIONS, 4 + _forcedReactions.size() * (4 + 4));
    data << uint32(_forcedReactions.size());
    for (ForcedReactions::const_iterator itr = _forcedReactions.begin(); itr != _forcedReactions.end(); ++itr)
    {
        data << uint32(itr->first);                         // faction_id (Faction.dbc)
        data << uint32(itr->second);                        // reputation rank
    }
    _player->SendDirectMessage(&data);
}

void ReputationMgr::SendState(FactionState const* faction)
{
    uint32 count = 1;

    WorldPacket data(SMSG_SET_FACTION_STANDING, 17);
    data << float(0);
    data << uint8(_sendFactionIncreased);
    _sendFactionIncreased = false; // Reset

    std::size_t p_count = data.wpos();
    data << uint32(count);

    data << uint32(faction->ReputationListID);
    data << uint32(faction->Standing);

    for (FactionStateList::iterator itr = _factions.begin(); itr != _factions.end(); ++itr)
    {
        if (itr->second.needSend)
        {
            itr->second.needSend = false;
            if (itr->second.ReputationListID != faction->ReputationListID)
            {
                data << uint32(itr->second.ReputationListID);
                data << uint32(itr->second.Standing);
                ++count;
            }
        }
    }

    data.put<uint32>(p_count, count);
    _player->SendDirectMessage(&data);
}

void ReputationMgr::SendInitialReputations()
{
    // Stock 3.3.5 is 128. Ascension/CoA Faction.dbc uses reputationListID >= 128;
    // those rows never reached the client until this count is raised.
    uint32 count = 128;
    for (FactionStateList::const_iterator itr = _factions.begin(); itr != _factions.end(); ++itr)
        if (itr->first + 1 > static_cast<int32>(count))
            count = static_cast<uint32>(itr->first + 1);
    if (count < 128)
        count = 128;
    if (count > 256)
        count = 256;

    WorldPacket data(SMSG_INITIALIZE_FACTIONS, (4 + count * 5));
    data << uint32(count);

    RepListID a = 0;

    for (FactionStateList::iterator itr = _factions.begin(); itr != _factions.end(); ++itr)
    {
        if (itr->first >= static_cast<int32>(count))
            break;

        for (; a != itr->first; a++)
        {
            data << uint8  (0x00);
            data << uint32 (0x00000000);
        }

        data << uint8  (itr->second.Flags);
        data << uint32 (itr->second.Standing);

        itr->second.needSend = false;

        ++a;
    }

    for (; a != static_cast<RepListID>(count); a++)
    {
        data << uint8  (0x00);
        data << uint32 (0x00000000);
    }

    _player->SendDirectMessage(&data);
}

void ReputationMgr::SendStates()
{
    for (FactionStateList::iterator itr = _factions.begin(); itr != _factions.end(); ++itr)
        SendState(&(itr->second));
}

void ReputationMgr::SendVisible(FactionState const* faction) const
{
    if (_player->GetSession()->PlayerLoading())
        return;

    // make faction visible in reputation list at client
    WorldPacket data(SMSG_SET_FACTION_VISIBLE, 4);
    data << faction->ReputationListID;
    _player->SendDirectMessage(&data);
}

void ReputationMgr::Initialize()
{
    _factions.clear();
    _visibleFactionCount = 0;
    _honoredFactionCount = 0;
    _reveredFactionCount = 0;
    _exaltedFactionCount = 0;
    _sendFactionIncreased = false;

    for (unsigned int i = 1; i < sFactionStore.GetNumRows(); i++)
    {
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(i);

        if (factionEntry && (factionEntry->reputationListID >= 0))
        {
            FactionState newFaction;
            newFaction.ID = factionEntry->ID;
            newFaction.ReputationListID = factionEntry->reputationListID;
            newFaction.Standing = 0;
            newFaction.Flags = GetDefaultStateFlags(factionEntry);
            newFaction.needSend = true;
            newFaction.needSave = true;
            newFaction.roundedUp = false;

            if (newFaction.Flags & FACTION_FLAG_VISIBLE)
                ++_visibleFactionCount;

            UpdateRankCounters(REP_HOSTILE, GetBaseRank(factionEntry));

            _factions[newFaction.ReputationListID] = newFaction;
        }
    }
}

bool ReputationMgr::SetReputation(FactionEntry const* factionEntry, float standing, bool incremental, bool noSpillOver, Optional<ReputationRank> repMaxCap)
{
    bool res = false;

    if (!noSpillOver)
    {
        ClassicPlusReputation::Spillover const* classicSpillover = nullptr;
        if (sWorld->getBoolConfig(CONFIG_CLASSIC_PLUS_REPUTATION_SPILLOVER) &&
            _player->GetLevel() <= ClassicPlusReputation::MaxLevel)
            classicSpillover = ClassicPlusReputation::Find(factionEntry->ID);

        if (classicSpillover)
        {
            for (ClassicPlusReputation::SpilloverTarget const& target : classicSpillover->targets)
            {
                FactionEntry const* targetEntry = target.faction ? sFactionStore.LookupEntry(target.faction) : nullptr;
                if (targetEntry)
                    SetOneFactionReputation(targetEntry, standing * target.rate, incremental);
            }
        }
        // if spillover definition exists in DB, override DBC
        else if (RepSpilloverTemplate const* repTemplate = sObjectMgr->GetRepSpilloverTemplate(factionEntry->ID))
        {
            for (uint32 i = 0; i < MAX_SPILLOVER_FACTIONS; ++i)
            {
                if (repTemplate->faction[i])
                {
                    if (_player->GetReputationRank(repTemplate->faction[i]) <= ReputationRank(repTemplate->faction_rank[i]))
                    {
                        // bonuses are already given, so just modify standing by rate
                        float spilloverRep = standing * repTemplate->faction_rate[i];
                        SetOneFactionReputation(sFactionStore.LookupEntry(repTemplate->faction[i]), spilloverRep, incremental);
                    }
                }
            }
        }
        else
        {
            float spillOverRepOut = float(standing);
            // check for sub-factions that receive spillover
            SimpleFactionsList const* flist = GetFactionTeamList(factionEntry->ID);
            // if has no sub-factions, check for factions with same parent
            if (!flist && factionEntry->team && factionEntry->spilloverRateOut != 0.0f)
            {
                spillOverRepOut *= factionEntry->spilloverRateOut;
                if (FactionEntry const* parent = sFactionStore.LookupEntry(factionEntry->team))
                {
                    FactionStateList::iterator parentState = _factions.find(parent->reputationListID);
                    // some team factions have own reputation standing, in this case do not spill to other sub-factions
                    if (parentState != _factions.end() && (parentState->second.Flags & FACTION_FLAG_SPECIAL))
                    {
                        SetOneFactionReputation(parent, spillOverRepOut, incremental);
                    }
                    else // spill to "sister" factions
                    {
                        flist = GetFactionTeamList(factionEntry->team);
                    }
                }
            }
            if (flist)
            {
                // Spillover to affiliated factions
                for (SimpleFactionsList::const_iterator itr = flist->begin(); itr != flist->end(); ++itr)
                {
                    if (FactionEntry const* factionEntryCalc = sFactionStore.LookupEntry(*itr))
                    {
                        if (factionEntryCalc == factionEntry || GetRank(factionEntryCalc) > ReputationRank(factionEntryCalc->spilloverMaxRankIn))
                            continue;
                        float spilloverRep = spillOverRepOut * factionEntryCalc->spilloverRateIn;
                        if (spilloverRep != 0 || !incremental)
                            res = SetOneFactionReputation(factionEntryCalc, spilloverRep, incremental);
                    }
                }
            }
        }
    }

    bool spillOverOnly = repMaxCap ? GetRank(factionEntry) > *repMaxCap : false;

    // spillover done, update faction itself
    FactionStateList::iterator faction = _factions.find(factionEntry->reputationListID);
    if (faction != _factions.end())
    {
        // Xinef: if we update spillover only, do not update main reputation (rank exceeds creature reward rate)
        if (!spillOverOnly)
        {
            res = SetOneFactionReputation(factionEntry, standing, incremental, repMaxCap);
        }

        // only this faction gets reported to client, even if it has no own visible standing
        SendState(&faction->second);
    }
    return res;
}

bool ReputationMgr::SetOneFactionReputation(FactionEntry const* factionEntry, float stand, bool incremental, Optional<ReputationRank> repMaxCap)
{
    FactionStateList::iterator itr = _factions.find(factionEntry->reputationListID);
    if (itr != _factions.end())
    {
        int32 BaseRep = GetBaseReputation(factionEntry);

        if (incremental)
        {
            stand *= sWorld->getRate(RATE_REPUTATION_GAIN);
        }

        int32 standing = 0;
        float stand2;
        if (fabs(modff(stand, &stand2)) < 1.f)
        {
            if (itr->second.roundedUp)
            {
                standing = static_cast<int32>(ceil(stand));
            }
            else
            {
                standing = static_cast<int32>(stand);
            }

            itr->second.roundedUp = !itr->second.roundedUp;
        }

        if (incremental)
        {
            standing += itr->second.Standing + BaseRep;
        }

        if (standing > Reputation_Cap)
            standing = Reputation_Cap;
        else if (standing < Reputation_Bottom)
            standing = Reputation_Bottom;

        ReputationRank old_rank = ReputationToRank(itr->second.Standing + BaseRep);
        ReputationRank new_rank = ReputationToRank(standing);
        if (repMaxCap && new_rank > *repMaxCap)
        {
            standing = ReputationRankToStanding(*repMaxCap);
            new_rank = *repMaxCap;
        }

        if (sScriptMgr->OnPlayerReputationChange(_player, factionEntry->ID, standing, incremental))
        {
            itr->second.Standing = standing - BaseRep;
            itr->second.needSend = true;
            itr->second.needSave = true;

            SetVisible(&itr->second);

            if (new_rank <= REP_HOSTILE)
                SetAtWar(&itr->second, true);

            if (old_rank <= REP_HOSTILE && new_rank >= REP_UNFRIENDLY && factionEntry->CanBeSetAtWar())
                SetAtWar(&itr->second, false);

            if (new_rank > old_rank)
                _sendFactionIncreased = true;

            if (new_rank != old_rank)
            {
                sScriptMgr->OnPlayerReputationRankChange(_player, factionEntry->ID, new_rank, old_rank, _sendFactionIncreased);
            }

            UpdateRankCounters(old_rank, new_rank);

            _player->ReputationChanged(factionEntry);
            _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_KNOWN_FACTIONS, factionEntry->ID);
            _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GAIN_REPUTATION, factionEntry->ID);
            _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GAIN_EXALTED_REPUTATION, factionEntry->ID);
            _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GAIN_REVERED_REPUTATION, factionEntry->ID);
            _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GAIN_HONORED_REPUTATION, factionEntry->ID);

            return true;
        }
    }
    return false;
}

void ReputationMgr::SetVisible(FactionTemplateEntry const* factionTemplateEntry)
{
    if (!factionTemplateEntry->faction)
        return;

    if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionTemplateEntry->faction))
        // Never show factions of the opposing team
        if (!(factionEntry->BaseRepRaceMask[1] & ReputationRaceMask(_player) && factionEntry->BaseRepValue[1] == Reputation_Bottom))
            SetVisible(factionEntry);
}

void ReputationMgr::SetVisible(FactionEntry const* factionEntry)
{
    if (factionEntry->reputationListID < 0)
        return;

    FactionStateList::iterator itr = _factions.find(factionEntry->reputationListID);
    if (itr == _factions.end())
        return;

    SetVisible(&itr->second);
}

void ReputationMgr::SetVisible(FactionState* faction)
{
    // always invisible or hidden faction can't be make visible
    // except if faction has FACTION_FLAG_SPECIAL
    if (faction->Flags & (FACTION_FLAG_INVISIBLE_FORCED | FACTION_FLAG_HIDDEN) && !(faction->Flags & FACTION_FLAG_SPECIAL))
        return;

    // already set
    if (faction->Flags & FACTION_FLAG_VISIBLE)
        return;

    faction->Flags |= FACTION_FLAG_VISIBLE;
    faction->needSend = true;
    faction->needSave = true;

    ++_visibleFactionCount;

    SendVisible(faction);
}

void ReputationMgr::SetAtWar(RepListID repListID, bool on)
{
    FactionStateList::iterator itr = _factions.find(repListID);
    if (itr == _factions.end())
        return;

    // always invisible or hidden faction can't change war state
    if (itr->second.Flags & (FACTION_FLAG_INVISIBLE_FORCED | FACTION_FLAG_HIDDEN))
        return;

    SetAtWar(&itr->second, on);
}

void ReputationMgr::SetAtWar(FactionState* faction, bool atWar) const
{
    // not allow declare war to own faction
    if (atWar && (faction->Flags & FACTION_FLAG_PEACE_FORCED))
        return;

    // already set
    if (((faction->Flags & FACTION_FLAG_AT_WAR) != 0) == atWar)
        return;

    if (atWar)
        faction->Flags |= FACTION_FLAG_AT_WAR;
    else
        faction->Flags &= ~FACTION_FLAG_AT_WAR;

    faction->needSend = true;
    faction->needSave = true;
}

void ReputationMgr::SetInactive(RepListID repListID, bool on)
{
    FactionStateList::iterator itr = _factions.find(repListID);
    if (itr == _factions.end())
        return;

    SetInactive(&itr->second, on);
}

void ReputationMgr::SetInactive(FactionState* faction, bool inactive) const
{
    // always invisible or hidden faction can't be inactive
    if (inactive && ((faction->Flags & (FACTION_FLAG_INVISIBLE_FORCED | FACTION_FLAG_HIDDEN)) || !(faction->Flags & FACTION_FLAG_VISIBLE)))
        return;

    // already set
    if (((faction->Flags & FACTION_FLAG_INACTIVE) != 0) == inactive)
        return;

    if (inactive)
        faction->Flags |= FACTION_FLAG_INACTIVE;
    else
        faction->Flags &= ~FACTION_FLAG_INACTIVE;

    faction->needSend = true;
    faction->needSave = true;
}

void ReputationMgr::LoadFromDB(PreparedQueryResult result)
{
    // Set initial reputations (so everything is nifty before DB data load)
    Initialize();

    //QueryResult* result = CharacterDatabase.Query("SELECT faction, standing, flags FROM character_reputation WHERE guid = '{}'", GetGUID().GetCounter());

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            FactionEntry const* factionEntry = sFactionStore.LookupEntry(fields[0].Get<uint16>());
            if (factionEntry && (factionEntry->reputationListID >= 0))
            {
                FactionState* faction = &_factions[factionEntry->reputationListID];

                // update standing to current
                faction->Standing = fields[1].Get<int32>();

                // update counters
                int32 BaseRep = GetBaseReputation(factionEntry);
                ReputationRank old_rank = ReputationToRank(BaseRep);
                ReputationRank new_rank = ReputationToRank(BaseRep + faction->Standing);
                UpdateRankCounters(old_rank, new_rank);

                uint32 dbFactionFlags = fields[2].Get<uint16>();

                if (dbFactionFlags & FACTION_FLAG_VISIBLE)
                    SetVisible(faction);                    // have internal checks for forced invisibility

                if (dbFactionFlags & FACTION_FLAG_INACTIVE)
                    SetInactive(faction, true);              // have internal checks for visibility requirement

                if (dbFactionFlags & FACTION_FLAG_AT_WAR)  // DB at war
                {
                    // Xinef: dont set at war for friendly hidden reputations
                    SetAtWar(faction, bool(!(dbFactionFlags & FACTION_FLAG_HIDDEN) || GetRank(factionEntry) < REP_NEUTRAL)); // have internal checks for FACTION_FLAG_PEACE_FORCED
                }
                else                                        // DB not at war
                {
                    // allow remove if visible (and then not FACTION_FLAG_INVISIBLE_FORCED or FACTION_FLAG_HIDDEN)
                    if (faction->Flags & FACTION_FLAG_VISIBLE)
                        SetAtWar(faction, false);            // have internal checks for FACTION_FLAG_PEACE_FORCED
                }

                // set atWar for hostile
                if (GetRank(factionEntry) <= REP_HOSTILE)
                    SetAtWar(faction, true);

                // reset changed flag if values similar to saved in DB
                if (faction->Flags == dbFactionFlags)
                {
                    faction->needSend = false;
                    faction->needSave = false;
                }

                faction->roundedUp = false;
            }
        } while (result->NextRow());
    }

    ApplyStartingZoneReputation();
}

void ReputationMgr::ApplyStartingZoneReputation()
{
    uint8 const race = _player->getRace(true);
    if (race >= 1 && race <= 11)
        return;

    enum Template : uint8 { T_HUMAN = 1, T_ORC = 2, T_DWARF = 3, T_NE = 4, T_UD = 5, T_TAUREN = 6 };
    uint8 tmpl = T_HUMAN;
    switch (race)
    {
        case 12: case 13: case 15: case 16: case 18: case 20: case 22: case 27: case 30:
        case 32: case 47: case 50: case 65: case 71: case 79: case 80: case 82: case 103:
            tmpl = T_HUMAN; break;
        case 19: case 21: case 23: case 24: case 25: case 26: case 28: case 29: case 31:
        case 49: case 51: case 53: case 55: case 56: case 57: case 58: case 69: case 70:
        case 74: case 83: case 86: case 87: case 89: case 98: case 104:
            tmpl = T_ORC; break;
        case 48: case 67: case 68:
            tmpl = T_DWARF; break;
        case 14: case 54: case 59: case 61: case 62: case 75: case 85: case 97: case 99:
        case 100: case 107: case 109: case 110:
            tmpl = T_NE; break;
        case 60: case 63: case 81: case 84: case 90: case 101:
            tmpl = T_UD; break;
        case 17: case 66: case 102: case 106: case 112:
            tmpl = T_TAUREN; break;
        default:
        {
            ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race);
            if (raceEntry)
            {
                switch (raceEntry->FactionID)
                {
                    case 1: tmpl = T_HUMAN; break;
                    case 2: tmpl = T_ORC; break;
                    case 3: tmpl = T_DWARF; break;
                    case 4: tmpl = T_NE; break;
                    case 5: tmpl = T_UD; break;
                    case 6: tmpl = T_TAUREN; break;
                    default: break;
                }
            }
            break;
        }
    }

    // Absolute standing (what the book shows). Independent of getRaceMask / DBC base.
    struct Pair { uint32 faction; int32 value; };
    Pair const* rows = nullptr;
    static Pair const kHuman[] = {
        {469, 3300}, {72, 3100}, {47, 3100}, {54, 3100}, {69, 3100}, {930, 3100},
        {67, -42000}, {76, -42000}, {81, -42000}, {68, -42000}, {911, -42000}, {530, -42000}, {0, 0}
    };
    static Pair const kOrc[] = {
        {67, 3500}, {76, 4000}, {81, 3100}, {530, 3100}, {68, 500}, {911, 400},
        {469, -42000}, {72, -42000}, {47, -42000}, {54, -42000}, {69, -42000}, {0, 0}
    };
    static Pair const kDwarf[] = {
        {469, 3300}, {47, 4000}, {54, 3100}, {72, 3100}, {69, 3100}, {930, 3100},
        {67, -42000}, {76, -42000}, {81, -42000}, {68, -42000}, {911, -42000}, {0, 0}
    };
    static Pair const kNE[] = {
        {469, 3300}, {69, 4000}, {72, 3100}, {47, 3100}, {54, 3100}, {930, 3100},
        {67, -42000}, {76, -42000}, {81, -42000}, {68, -42000}, {911, -42000}, {530, -42000}, {0, 0}
    };
    static Pair const kUD[] = {
        {67, 3500}, {68, 4000}, {911, 3100}, {76, 500}, {81, 500}, {530, 500},
        {469, -42000}, {72, -42000}, {47, -42000}, {69, -42000}, {0, 0}
    };
    static Pair const kTauren[] = {
        {67, 3500}, {81, 4000}, {76, 3100}, {530, 3100}, {68, 500}, {911, 0},
        {469, -42000}, {72, -42000}, {47, -42000}, {69, -42000}, {54, -42000}, {0, 0}
    };
    switch (tmpl)
    {
        case T_ORC: rows = kOrc; break;
        case T_DWARF: rows = kDwarf; break;
        case T_NE: rows = kNE; break;
        case T_UD: rows = kUD; break;
        case T_TAUREN: rows = kTauren; break;
        default: rows = kHuman; break;
    }

    for (Pair const* p = rows; p->faction; ++p)
    {
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(p->faction);
        if (!factionEntry || factionEntry->reputationListID < 0)
            continue;
        FactionStateList::iterator itr = _factions.find(factionEntry->reputationListID);
        if (itr == _factions.end())
            continue;
        int32 const base = GetBaseReputation(factionEntry);
        itr->second.Standing = p->value - base;
        itr->second.Flags = (itr->second.Flags | FACTION_FLAG_VISIBLE) & ~FACTION_FLAG_INVISIBLE_FORCED;
        itr->second.needSend = true;
        itr->second.needSave = true;
        SetVisible(&itr->second);
        if (GetRank(factionEntry) <= REP_HOSTILE)
            SetAtWar(&itr->second, true);
        else if (GetRank(factionEntry) >= REP_FRIENDLY)
            SetAtWar(&itr->second, false);
    }
}

void ReputationMgr::SaveToDB(CharacterDatabaseTransaction trans)
{
    for (FactionStateList::iterator itr = _factions.begin(); itr != _factions.end(); ++itr)
    {
        if (itr->second.needSave)
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_REPUTATION_BY_FACTION);
            stmt->SetData(0, _player->GetGUID().GetCounter());
            stmt->SetData(1, uint16(itr->second.ID));
            trans->Append(stmt);

            stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_REPUTATION_BY_FACTION);
            stmt->SetData(0, _player->GetGUID().GetCounter());
            stmt->SetData(1, uint16(itr->second.ID));
            stmt->SetData(2, itr->second.Standing);
            stmt->SetData(3, uint16(itr->second.Flags));
            trans->Append(stmt);

            itr->second.needSave = false;
        }
    }
}

void ReputationMgr::UpdateRankCounters(ReputationRank old_rank, ReputationRank new_rank)
{
    if (old_rank >= REP_EXALTED)
        --_exaltedFactionCount;
    if (old_rank >= REP_REVERED)
        --_reveredFactionCount;
    if (old_rank >= REP_HONORED)
        --_honoredFactionCount;

    if (new_rank >= REP_EXALTED)
        ++_exaltedFactionCount;
    if (new_rank >= REP_REVERED)
        ++_reveredFactionCount;
    if (new_rank >= REP_HONORED)
        ++_honoredFactionCount;
}
