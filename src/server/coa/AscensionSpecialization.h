#ifndef ASCENSION_SPECIALIZATION_H
#define ASCENSION_SPECIALIZATION_H

#include "AscensionCoATalentState.h"
#include "Define.h"

#include <functional>
#include <string>
#include <vector>

class Player;

uint32 GetAscensionActiveSpecialization(Player const* player);

bool SwitchAscensionSpecialization(Player* player, uint32 specializationId);

using AscensionSpecializationSwitchGuard =
    std::function<std::string(Player* player, uint32 activeSpecializationId, uint32 requestedSpecializationId)>;

void AddAscensionSpecializationSwitchGuard(AscensionSpecializationSwitchGuard guard);

std::string AscensionSpecializationSwitchRefusal(Player* player, uint32 activeSpecializationId,
    uint32 requestedSpecializationId);

using AscensionTalentTreeProvider =
    std::function<AscensionCoATalentState::TalentGrant(Player const* player)>;

void AddAscensionTalentTreeProvider(AscensionTalentTreeProvider provider);

using AscensionTalentBudgetProvider =
    std::function<void(Player const* player, uint32& classPoints, uint32& specializationPoints)>;

void AddAscensionTalentBudgetProvider(AscensionTalentBudgetProvider provider);

AscensionCoATalentState::TalentAccess GetAscensionTalentAccess(Player const* player);

bool ApplyAscensionTalentBuild(Player* player, std::vector<AscensionCoATalentState::KnownEntry> const& build,
    std::string& refusal);

bool GetAscensionTalentPointsLeft(Player const* player, uint32& classPoints, uint32& specializationPoints);

void ClearAscensionSpecializationSlots(Player* player);

uint32 ForgetAscensionClassTalents(Player* player);

uint32 GetAscensionTalentRank(Player const* player, uint32 entryId);

bool RestoreAscensionSpecializationSignature(Player* player);

std::vector<AscensionCoATalentState::KnownEntry> GetAscensionKnownTalentEntries(Player const* player);

bool SetAscensionTalentRank(Player* player, uint32 entryId, uint32 rank);

uint32 SynchronizeAscensionTalentReplacements(Player* player);

bool IsAscensionCustomClassId(uint8 classId);

struct AscensionClassAbility
{
    uint32 SpellId;
    uint32 FirstSpellId;
    uint16 SpecId;
    uint8 RequiredLevel;
};

std::vector<AscensionClassAbility> GetAscensionClassAbilities(uint8 classId);

#endif
