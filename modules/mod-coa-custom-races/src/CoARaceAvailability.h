#ifndef COA_RACE_AVAILABILITY_H
#define COA_RACE_AVAILABILITY_H

#include <charconv>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace CoARaceAvailability
{
    inline bool IsStockRace(std::uint32_t race)
    {
        return race >= 1 && race <= 11 && race != 9;
    }

    inline std::string_view Trim(std::string_view value)
    {
        auto const first = value.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
            return {};
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }

    inline std::optional<std::set<std::uint8_t>> Parse(std::string_view value)
    {
        std::set<std::uint8_t> ids;
        value = Trim(value);
        if (value.empty())
            return ids;
        while (true)
        {
            auto const comma = value.find(',');
            auto const token = Trim(value.substr(0, comma));
            std::uint32_t race = 0;
            if (token.empty())
                return std::nullopt;
            auto const parsed = std::from_chars(token.data(), token.data() + token.size(), race);
            if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
                race == 0 || race > 255 || IsStockRace(race) || !ids.insert(static_cast<std::uint8_t>(race)).second)
                return std::nullopt;
            if (comma == std::string_view::npos)
                return ids;
            value.remove_prefix(comma + 1);
        }
    }

    template<class KnownRace>
    bool Valid(std::set<std::uint8_t> const& ids, KnownRace knownRace)
    {
        for (auto race : ids)
            if (!knownRace(race))
                return false;
        return true;
    }

    inline std::optional<std::string> NormalizeCatalogHash(std::string_view value)
    {
        value = Trim(value);
        if (value.empty())
            return std::string{};
        if (value.size() != 64)
            return std::nullopt;
        std::string normalized;
        for (char c : value)
        {
            if (c >= 'A' && c <= 'F')
                c = static_cast<char>(c + ('a' - 'A'));
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return std::nullopt;
            normalized.push_back(c);
        }
        return normalized;
    }

    inline bool CanCreate(bool enabled, std::set<std::uint8_t> const& disabled, std::uint8_t race)
    {
        return IsStockRace(race) || (enabled && disabled.find(race) == disabled.end());
    }
}

#endif
