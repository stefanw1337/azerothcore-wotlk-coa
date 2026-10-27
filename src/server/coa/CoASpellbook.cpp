#include "CoASpellbook.h"

namespace
{
    CoASpellbook::Provider SpellbookProvider;
    CoASpellbook::ExtraClasses ExtraClassesProvider = nullptr;
}

void CoASpellbook::SetExtraClasses(ExtraClasses provider)
{
    ExtraClassesProvider = provider;
}

std::vector<std::pair<std::uint8_t, std::uint32_t>> CoASpellbook::ExtraClassesOf(Player const* player)
{
    return ExtraClassesProvider ? ExtraClassesProvider(player) : std::vector<std::pair<std::uint8_t, std::uint32_t>>{};
}

void CoASpellbook::SetProvider(Provider provider)
{
    SpellbookProvider = provider;
}

bool CoASpellbook::Available()
{
    return SpellbookProvider.RowCount && SpellbookProvider.OffersSpell && SpellbookProvider.CoversSpell;
}

double CoASpellbook::RowCount(Player* player)
{
    return SpellbookProvider.RowCount ? double(SpellbookProvider.RowCount(player)) : -1.0;
}

double CoASpellbook::OffersSpell(Player* player, std::uint32_t spellId)
{
    return SpellbookProvider.OffersSpell ? double(SpellbookProvider.OffersSpell(player, spellId)) : -1.0;
}

double CoASpellbook::CoversSpell(Player* player, std::uint32_t spellId)
{
    return SpellbookProvider.CoversSpell ? double(SpellbookProvider.CoversSpell(player, spellId)) : -1.0;
}

std::vector<std::uint32_t> CoASpellbook::UpgradeRanksAbove(Player* player, std::uint8_t level)
{
    return SpellbookProvider.UpgradeRanksAbove ? SpellbookProvider.UpgradeRanksAbove(player, level)
        : std::vector<std::uint32_t>{};
}
