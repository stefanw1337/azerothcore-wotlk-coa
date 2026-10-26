#ifndef ASCENSION_FELSWORN_RIFTS_H
#define ASCENSION_FELSWORN_RIFTS_H

#include <array>
#include <cstdint>

namespace AscensionCompatData
{
struct FelswornRiftGrant
{
    std::uint32_t SpellId;
    std::uint8_t RequiredLevel;
};

inline constexpr std::array<FelswornRiftGrant, 3> FelswornHordeCapitalRifts =
{{
    {535598, 26},
    {535599, 30},
    {535600, 36}
}};
}

#endif
