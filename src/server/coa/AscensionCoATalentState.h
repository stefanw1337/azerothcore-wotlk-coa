#ifndef ASCENSION_COA_TALENT_STATE_H
#define ASCENSION_COA_TALENT_STATE_H

#include "AscensionCoATalentData.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace AscensionCoATalentState
{
using HasSpell = std::function<bool(std::uint32_t)>;

struct KnownEntry
{
    std::uint32_t EntryId;
    std::uint32_t Rank;
    std::uint32_t LearnedSpellRank = 0;
    bool Locked = false;
    std::uint32_t LearnOrder = 0;
};

struct SpecializationSlot
{
    std::uint32_t ClassId = 0;
    std::uint32_t SpecId = 0;
    std::vector<KnownEntry> Entries;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> Actions;
};

std::vector<std::uint32_t> SpecializationSlotRecord(SpecializationSlot const& slot);

bool ParseSpecializationSlot(std::vector<std::uint32_t> const& record, SpecializationSlot& slot);

std::uint32_t KnownRank(AscensionCompatData::CoATalentEntry const& entry, HasSpell const& hasSpell);

std::vector<KnownEntry> KnownEntries(std::uint8_t classId, HasSpell const& hasSpell);

struct TalentTree
{
    std::uint8_t ClassId = 0;
    std::uint16_t SpecId = 0;
};

struct SpecializationSwap
{
    std::uint16_t ReplacedSpecId = 0;
    TalentTree Borrowed;
};

struct TalentGrant
{
    std::vector<TalentTree> ExtraTrees;
    std::vector<SpecializationSwap> Swaps;
};

struct TalentAccess
{
    std::uint8_t ClassId = 0;
    std::uint32_t ActiveSpecId = 0;
    std::vector<TalentTree> ExtraTrees;
    std::vector<SpecializationSwap> Swaps;
};

bool IsExtraTree(TalentAccess const& access, AscensionCompatData::CoATalentEntry const& entry);

bool IsBorrowed(TalentAccess const& access, AscensionCompatData::CoATalentEntry const& entry);

bool IsReplaced(TalentAccess const& access, std::uint32_t specId);

bool IsSelectableSpecialization(TalentAccess const& access, std::uint32_t specId);

bool OwnsEntry(TalentAccess const& access, AscensionCompatData::CoATalentEntry const& entry);

bool IsTreeOpen(TalentAccess const& access, AscensionCompatData::CoATalentEntry const& entry);

std::vector<KnownEntry> KnownEntries(TalentAccess const& access, HasSpell const& hasSpell);

std::vector<KnownEntry> WithoutExtraTrees(TalentAccess const& access, std::vector<KnownEntry> const& known);

bool CanGrantAutomatic(AscensionCompatData::CoATalentEntry const& entry, std::uint8_t classId, std::uint32_t level,
    std::uint32_t specId, HasSpell const& hasSpell);

bool CanGrantAutomatic(AscensionCompatData::CoATalentEntry const& entry, TalentAccess const& access,
    std::uint32_t level, HasSpell const& hasSpell);

std::vector<KnownEntry> SlotKnownEntries(SpecializationSlot const& slot, TalentAccess const& access,
    std::uint32_t level, HasSpell const& carried);

std::vector<KnownEntry> SlotKnownEntries(SpecializationSlot const& slot, std::uint32_t level,
    HasSpell const& carried);

std::vector<std::uint8_t> InspectSpecsPayload(std::vector<std::vector<KnownEntry>> const& specs);

struct SpentPoints
{
    std::uint32_t AE = 0;
    std::uint32_t TE = 0;
};

SpentPoints Spent(std::vector<KnownEntry> const& known);

std::vector<std::uint8_t> KnownEntriesPayload(std::vector<KnownEntry> const& known);

bool ParseKnownEntriesUpload(std::uint8_t const* data, std::size_t size, std::vector<KnownEntry>& known);

struct UploadedSpecialization
{
    std::uint32_t SpecId = 0;
    bool Mixed = false;
    bool ChoosesTalents = false;
};

UploadedSpecialization SpecializationOf(std::vector<KnownEntry> const& upload);

std::vector<KnownEntry> SpecializationSwitch(std::uint8_t classId, HasSpell const& hasSpell, std::uint32_t specId,
    std::vector<KnownEntry> const* current = nullptr);
constexpr std::uint32_t TALENT_PURGE_ITEM = 919291;
constexpr std::uint32_t MARK_OF_ASCENSION_ITEM = 375250;

enum class ResetCreditType : std::uint8_t
{
    AbilityReset = 1,
    TalentReset = 2,
    AbilityUnlearn = 3,
    TalentUnlearn = 4,
};

using ResetCredits = std::array<std::uint32_t, 4>;

struct UnlearnPrice
{
    std::uint32_t Money = 0;
    std::uint32_t Marks = 0;
};

UnlearnPrice UnlearnPriceAt(std::uint32_t level, ResetCredits const& credits, bool freeUnlearn = false);

bool IsUnpricedRemoval(AscensionCompatData::CoATalentEntry const& entry);

std::vector<std::uint32_t> SpellsAboveRank(AscensionCompatData::CoATalentEntry const& entry, std::uint32_t rank);

struct PurgePrice
{
    std::uint32_t Money = 0;
    std::uint32_t Marks = 0;
    std::uint32_t Item = 0;
    std::uint32_t ItemCount = 0;
};

PurgePrice TalentPurgePriceAt(std::uint32_t level, ResetCredits const& credits);

struct RemovalPayment
{
    bool Affordable = true;
    std::uint32_t Money = 0;
    std::uint32_t Marks = 0;
};

RemovalPayment PayForRemovals(std::vector<UnlearnPrice> const& prices, std::uint32_t marksHeld,
    std::uint32_t moneyHeld);

}

#endif
