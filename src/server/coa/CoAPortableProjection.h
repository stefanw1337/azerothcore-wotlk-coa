#ifndef COA_PORTABLE_PROJECTION_H
#define COA_PORTABLE_PROJECTION_H

#include "Define.h"
#include <string>
#include <utility>
#include <vector>

namespace CoAPortableProjection
{
    inline constexpr uint32 PolicyVersion = 1;
    inline constexpr uint32 Protocol = 1;
    inline constexpr uint32 PinWordCount = 10;

    struct ItemIn
    {
        std::string Id;
        bool HasContainer = false;
        std::string Container;
        uint32 Slot = 0;
        uint32 Entry = 0;
    };

    struct ActionIn
    {
        uint32 Spec = 0;
        uint32 Button = 0;
        uint32 Action = 0;
        uint32 Type = 0;
    };

    struct SettingIn
    {
        std::string Source;
        std::vector<uint32> Values;
    };

    struct Input
    {
        uint32 Class = 0;
        uint32 Level = 0;
        std::vector<ItemIn> Items;
        std::vector<uint32> Spells;
        std::vector<ActionIn> Actions;
        std::vector<SettingIn> Settings;
    };

    struct SettingHold
    {
        std::string Source;
        std::string Kind;
        std::vector<uint32> Entries;
        std::vector<uint32> Buttons;
    };

    struct Result
    {
        bool Ok = false;
        std::string Error;
        uint32 CanonicalLevel = 0;
        uint32 ProjectedLevel = 0;
        std::vector<std::string> HeldItems;
        std::vector<uint32> HeldSpells;
        std::vector<std::pair<uint32, uint32>> HeldActions;
        std::vector<SettingHold> Settings;
        std::vector<std::string> BlockedSettings;

        bool HoldsNothing() const;
    };

    uint32 MaxLevel();
    std::string const& Signature();
    std::vector<uint32> PinWords();
    bool PinMatches(std::vector<uint32> const& words);
    std::string ProgressionJson();
    Result Project(Input const& input, uint32 level);
    std::string ManifestJson(Result const& result);
}

#endif
