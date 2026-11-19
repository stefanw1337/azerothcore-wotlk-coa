/*
 * mod-loot-goblins: scaled-down item copies.
 *
 * A higher-level item a goblin drops for a lower-level player becomes a copy shrunk to that player's level and a bit
 * stronger than normal gear there (LootGoblins.Scaled.PowerBonus), locked at that level. Originals are never touched.
 * Copies get ids from LootGoblins.Scaled.FirstEntry on, are remembered in the characters database
 * (`lootgoblin_scaled_item`) so items in bags keep working after a restart, and one copy is shared by everyone who
 * gets the same item in the same level bracket.
 *
 * The core is not changed: on CoA the item lookup already asks a resolver for item ids it doesn't know
 * (LocalLevelScaling::ScaledItemTemplateOwner, used by CoA's own scaling-up copies). We put ours in front of it and pass
 * every other id on. On a core without that resolver, scaling is switched off and goblins drop items of the
 * player's own level instead.
 */

#include "LootGoblins.h"

#include "Config.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>

#if __has_include("LocalLevelScaling.h")
#include "LocalLevelScaling.h"
#define LOOTGOBLINS_HAS_ITEM_RESOLVER 1
#else
#define LOOTGOBLINS_HAS_ITEM_RESOLVER 0
#endif

namespace LootGoblins
{
    namespace
    {
        struct Copy
        {
            uint32 base = 0;
            uint8 level = 0;
            std::unique_ptr<ItemTemplate> proto;
        };

        std::shared_mutex gMutex;
        std::map<uint32, Copy> gCopies;                          // copy entry -> copy
        std::map<std::pair<uint32, uint8>, uint32> gByBase;      // (base, level) -> copy entry
        uint32 gNextEntry = 0;
        bool gActive = false;

#if LOOTGOBLINS_HAS_ITEM_RESOLVER
        LocalLevelScaling::ScaledItemTemplateResolver gPrevious = nullptr;

        std::unique_ptr<ItemTemplate> Build(ItemTemplate const& base, uint32 entry, uint8 level);

        // a copy's template is only built the first time it is asked for (most premade copies are never used)
        ItemTemplate const* Built(uint32 entry)
        {
            {
                std::shared_lock lock(gMutex);
                auto itr = gCopies.find(entry);
                if (itr == gCopies.end())
                    return nullptr;
                if (itr->second.proto)
                    return itr->second.proto.get();
            }
            std::unique_lock lock(gMutex);
            auto itr = gCopies.find(entry);
            if (itr == gCopies.end())
                return nullptr;
            if (!itr->second.proto)
            {
                ItemTemplate const* base = sObjectMgr->GetItemTemplate(itr->second.base);
                if (!base)
                    return nullptr;
                itr->second.proto = Build(*base, entry, itr->second.level);
            }
            return itr->second.proto.get();
        }

        ItemTemplate const* Resolve(uint32 entry)
        {
            if (ItemTemplate const* copy = Built(entry))
                return copy;
            return gPrevious ? gPrevious(entry) : nullptr;
        }
#endif

        uint32 TargetItemLevel(uint32 quality, uint8 level)
        {
            static uint32 const offset[] = { 0, 2, 5, 8, 12, 15, 15, 15 };
            float bonus = Conf().scaledPowerBonus;
            return uint32(std::lround(float(level + offset[std::min<uint32>(quality, 7)]) * bonus));
        }

        bool Scalable(ItemTemplate const& p)
        {
            return (p.Class == ITEM_CLASS_WEAPON || p.Class == ITEM_CLASS_ARMOR) && p.InventoryType != INVTYPE_NON_EQUIP
                && p.InventoryType != INVTYPE_BODY && p.InventoryType != INVTYPE_TABARD && p.ItemLevel > 0
                && p.ScalingStatDistribution == 0 && p.StartQuest == 0 && p.Quality >= ITEM_QUALITY_UNCOMMON;
        }

        std::unique_ptr<ItemTemplate> Build(ItemTemplate const& base, uint32 entry, uint8 level)
        {
            auto p = std::make_unique<ItemTemplate>(base);
            uint32 target = TargetItemLevel(base.Quality, level);
            float r = std::min(1.0f, float(target) / float(std::max<uint32>(1, base.ItemLevel)));
            auto scale = [r](float v) { return v * r; };

            p->ItemId = entry;
            p->ItemLevel = std::min(base.ItemLevel, target);
            p->RequiredLevel = level;
            for (auto& stat : p->ItemStat)
                if (stat.ItemStatValue > 0)
                    stat.ItemStatValue = std::max(1, int32(std::lround(scale(float(stat.ItemStatValue)))));
            for (auto& dmg : p->Damage)
            {
                dmg.DamageMin = std::floor(scale(dmg.DamageMin));
                dmg.DamageMax = std::max(dmg.DamageMin + 1.0f, std::ceil(scale(dmg.DamageMax)));
                if (dmg.DamageMin <= 0.0f && dmg.DamageMax <= 1.0f)
                    dmg.DamageMin = dmg.DamageMax = 0.0f;
            }
            p->Armor = uint32(std::lround(scale(float(base.Armor))));
            p->Block = uint32(std::lround(scale(float(base.Block))));
            p->HolyRes = int32(std::lround(scale(float(base.HolyRes))));
            p->FireRes = int32(std::lround(scale(float(base.FireRes))));
            p->NatureRes = int32(std::lround(scale(float(base.NatureRes))));
            p->FrostRes = int32(std::lround(scale(float(base.FrostRes))));
            p->ShadowRes = int32(std::lround(scale(float(base.ShadowRes))));
            p->ArcaneRes = int32(std::lround(scale(float(base.ArcaneRes))));
            p->SellPrice = uint32(float(base.SellPrice) * r * r);
            p->BuyPrice = 0;
            p->ItemSet = 0;                       // a copy is not part of the set
            p->Bonding = BIND_WHEN_PICKED_UP;
            p->RequiredDisenchantSkill = base.RequiredDisenchantSkill ? std::min<uint32>(base.RequiredDisenchantSkill, level * 5) : 0;
            // Spells: 0 = none, 1 = only "on equip" effects, 2 = all of them
            for (auto& spell : p->Spells)
            {
                bool keep = Conf().scaledSpellMode == 2 || (Conf().scaledSpellMode == 1 && spell.SpellTrigger == ITEM_SPELLTRIGGER_ON_EQUIP);
                if (!keep)
                {
                    spell.SpellId = 0;
                    spell.SpellTrigger = 0;
                    spell.SpellCharges = 0;
                    spell.SpellPPMRate = 0.0f;
                    spell.SpellCooldown = -1;
                    spell.SpellCategory = 0;
                    spell.SpellCategoryCooldown = -1;
                }
            }
            if (!Conf().scaledSuffix.empty())
                p->Name1 += Conf().scaledSuffix;
            return p;
        }
    }

    bool ScalingAvailable() { return gActive; }

    void InitScaledItems()
    {
#if LOOTGOBLINS_HAS_ITEM_RESOLVER
        if (!Conf().scaledEnable)
        {
            LOG_INFO("module", "Loot Goblins: scaled-down items are off (LootGoblins.Scaled.Enable)");
            return;
        }
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `lootgoblin_scaled_item` (`entry` int unsigned NOT NULL, `base_entry` int unsigned NOT NULL, "
            "`level` tinyint unsigned NOT NULL, PRIMARY KEY (`entry`), UNIQUE KEY `base_level` (`base_entry`, `level`)) "
            "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-loot-goblins: scaled-down item copies (never delete rows: items in bags use them)'");

        std::unique_lock lock(gMutex);
        if (!gActive)
        {
            gPrevious = LocalLevelScaling::ScaledItemTemplateOwner.load(std::memory_order_relaxed);
            LocalLevelScaling::ScaledItemTemplateOwner.store(&Resolve, std::memory_order_relaxed);
            gActive = true;
        }
        gNextEntry = Conf().scaledFirstEntry;
        uint32 loaded = 0;
        if (QueryResult result = CharacterDatabase.Query("SELECT entry, base_entry, level FROM lootgoblin_scaled_item"))
        {
            do
            {
                uint32 entry = (*result)[0].Get<uint32>();
                uint32 base = (*result)[1].Get<uint32>();
                uint8 level = (*result)[2].Get<uint8>();
                gNextEntry = std::max(gNextEntry, entry + 1);
                if (gCopies.count(entry) || !sObjectMgr->GetItemTemplate(base))
                    continue;
                gCopies[entry] = Copy{ base, level, nullptr };   // built when first needed
                gByBase[{ base, level }] = entry;
                ++loaded;
            } while (result->NextRow());
        }
        gNextEntry = std::max(gNextEntry, Conf().scaledFirstEntry);
        LOG_INFO("module", "Loot Goblins: {} scaled-down item copies known ({}){}", loaded,
            Conf().scaledRuntimeCopies ? "new ones are made as needed" : "only these: the installer makes them with their client icons",
            loaded ? "" : " - run the Loot Goblins installer to make them");
#else
        LOG_INFO("module", "Loot Goblins: this core has no item resolver; scaled-down items are off");
#endif
    }

    uint32 ScaledCopy(uint32 baseEntry, uint8 level)
    {
        if (!gActive)
            return 0;
        ItemTemplate const* base = sObjectMgr->GetItemTemplate(baseEntry);
        if (!base || !Scalable(*base))
            return 0;
        uint32 bracket = std::max<uint32>(1, Conf().scaledBracket);
        uint8 locked = uint8(std::max<uint32>(1, (level / bracket) * bracket));   // level 23, bracket 5 -> 20
        if (base->RequiredLevel <= locked && base->ItemLevel <= TargetItemLevel(base->Quality, locked))
            return baseEntry;                                                     // already fits: no copy needed

        {
            std::shared_lock lock(gMutex);
            auto itr = gByBase.find({ baseEntry, locked });
            if (itr != gByBase.end())
                return itr->second;
        }
        if (!Conf().scaledRuntimeCopies)
            return 0;   // a copy made now would have no icon in the client (it is not in the patch's Item table)
        std::unique_lock lock(gMutex);
        auto itr = gByBase.find({ baseEntry, locked });
        if (itr != gByBase.end())
            return itr->second;
        if (gNextEntry > Conf().scaledFirstEntry + 999999)
            return 0;
        uint32 entry = gNextEntry++;
        CharacterDatabase.DirectExecute("INSERT INTO lootgoblin_scaled_item (entry, base_entry, level) VALUES ({}, {}, {})",
            entry, baseEntry, uint32(locked));
        gCopies[entry] = Copy{ baseEntry, locked, nullptr };
        gByBase[{ baseEntry, locked }] = entry;
        return entry;
    }

    uint32 BaseOfCopy(uint32 entry)
    {
        std::shared_lock lock(gMutex);
        auto itr = gCopies.find(entry);
        return itr == gCopies.end() ? entry : itr->second.base;
    }
}
