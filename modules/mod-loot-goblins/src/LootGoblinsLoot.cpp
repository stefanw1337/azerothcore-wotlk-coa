/*
 * mod-loot-goblins: what a dead goblin gives.
 *
 *  - Every participant gets their own piles, rolled for their own level. One pile per item, the money in the first.
 *  - Pinata: the piles fly out of the goblin in arcs and land around it; each pile gets a loot beam in its quality's
 *    colour. Piles and beams are summoned for their owner only (nobody else sees them).
 *  - Group rolls: in a group (in dungeons and raids by default) the headline items go up for Need / Greed / Pass
 *    in our addon's roll window. Roll = 1-100 + ceil(share % / BonusDivisor); Need beats Greed; ties roll again.
 *    The winner may then get the item upgraded: base upgrade % x (roll / 100) x share multiplier.
 *    Players without the addon greed automatically.
 *  - The addon channel (prefix LGOB) also carries the Rift-style timer.
 */

#include "LootGoblins.h"

#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "QueryResult.h"
#include "GameTime.h"
#include "Group.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "WorldPacket.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <unordered_set>
#include <mutex>
#include "WorldSession.h"

namespace LootGoblins
{
    namespace
    {
        uint32 NowMs() { return GameTime::GetGameTimeMS().count(); }

        struct Drop { uint32 item = 0; uint32 count = 1; bool headline = false; };

        // ---- per-player names for the drop slots -------------------------------------------------------
        std::mutex gNameMutex;
        std::map<std::pair<uint32, uint32>, std::pair<std::string, std::string>> gPileNames;   // (account, entry) -> name, sub
        uint32 gNextSlot = 0;

        char const* QualityName(uint32 q)
        {
            static char const* const names[] = { "Poor", "Common", "Uncommon", "Rare", "Epic", "Legendary", "Artifact", "Heirloom" };
            return names[std::min<uint32>(q, 7)];
        }

        bool IsHeldItem(ItemTemplate const* p)
        {
            if (!p)
                return false;
            // fishing poles and "miscellaneous" weapons are not drawn when a creature holds them: nothing to click
            if (!Conf().pilePoleModels && p->Class == ITEM_CLASS_WEAPON && (p->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE || p->SubClass == ITEM_SUBCLASS_WEAPON_MISC))
                return false;
            if (!p->DisplayInfoID)
                return false;
            switch (p->InventoryType)
            {
                case INVTYPE_WEAPON: case INVTYPE_2HWEAPON: case INVTYPE_WEAPONMAINHAND: case INVTYPE_WEAPONOFFHAND:
                case INVTYPE_SHIELD: case INVTYPE_HOLDABLE:
                    return true;
                case INVTYPE_RANGED: case INVTYPE_RANGEDRIGHT: case INVTYPE_THROWN:
                    // the ranged slot is only drawn in ranged stance, so these go in the main-hand slot
                    return Conf().pileRangedModels;
                default:
                    return false;
            }
        }

        uint8 HeldSlot(ItemTemplate const* p)
        {
            switch (p->InventoryType)
            {
                case INVTYPE_SHIELD: case INVTYPE_HOLDABLE: case INVTYPE_WEAPONOFFHAND: return 1;
                default: return 0;
            }
        }

        // D3 item ground model ("flippy") for armor and jewelry: lootgoblin_beams key 21+ -> display
        uint8 FlippyKey(ItemTemplate const* p)
        {
            if (!p)
                return 0;
            switch (p->InventoryType)
            {
                case INVTYPE_HEAD: return 21;
                case INVTYPE_NECK: case INVTYPE_TRINKET: return 22;
                case INVTYPE_SHOULDERS: return 23;
                case INVTYPE_BODY: case INVTYPE_CHEST: case INVTYPE_ROBE: case INVTYPE_TABARD: case INVTYPE_CLOAK: return 24;
                case INVTYPE_WAIST: return 25;
                case INVTYPE_LEGS: return 26;
                case INVTYPE_FEET: return 27;
                case INVTYPE_WRISTS: return 28;
                case INVTYPE_HANDS: return 29;
                case INVTYPE_FINGER: return 30;
                case INVTYPE_SHIELD: return 31;
                default: return 0;
            }
        }

        // Our piles, beams and effects were copied from a creature that wanders about: make them stand still.
        void StandStill(Creature* c)
        {
            c->SetWanderDistance(0.0f);
            c->SetDefaultMovementType(IDLE_MOTION_TYPE);
            c->GetMotionMaster()->Clear();
            c->GetMotionMaster()->MoveIdle();
            c->StopMoving();
        }

        void SendPileName(Player* owner, uint32 entry)
        {
            if (owner)
                SendPileNameTo(owner->GetSession(), entry);
        }

        struct FlyingPile
        {
            ObjectGuid pile, owner;   // pile: the flying look (a visual only); the real pile is made where it lands
            uint32 entry = 0;
            uint32 mapId = 0;
            std::vector<Drop> drops;
            uint32 gold = 0;
            uint32 landMs = 0;
            Position land;
        };

        struct LandedPile
        {
            ObjectGuid pile, beam, owner;
            uint32 endMs = 0;
        };

        struct RollSession
        {
            uint32 id = 0;
            Kind kind = Kind::Gold;
            uint32 mapId = 0;
            Position pos;
            uint32 deadlineMs = 0;
            std::vector<ObjectGuid> members;
            std::unordered_map<ObjectGuid, float> sharePct, multiplier;
            std::vector<Drop> items;
            std::map<std::pair<uint32, ObjectGuid>, uint8> choices;   // (item index, player) -> 1 need, 2 greed, 3 pass
        };

        std::unordered_set<ObjectGuid> gAddonUsers;
        // Piles are made, landed and cleaned up from their owner's own update (the owner's map thread), never
        // from the world thread; the lists are shared between map threads, hence the lock.
        std::mutex gPileMutex;
        std::list<FlyingPile> gFlying;
        std::list<LandedPile> gLanded;
        std::map<uint32, RollSession> gRolls;
        uint32 gNextRollId = 1;
        std::unordered_map<uint8, uint32> gBeamDisplay;   // quality (6 = money) -> display
        bool gVisualsLoaded = false;

        char const* const QUALITY_COLOR[] = { "ff9d9d9d", "ffffffff", "ff1eff00", "ff0070dd", "ffa335ee", "ffff8000", "ffe6cc80", "ffe6cc80" };

        void LoadVisuals()
        {
            gVisualsLoaded = true;
            gBeamDisplay.clear();
            if (QueryResult result = WorldDatabase.Query("SELECT Quality, DisplayID FROM lootgoblin_beams"))
            {
                do
                {
                    gBeamDisplay[(*result)[0].Get<uint8>()] = (*result)[1].Get<uint32>();
                } while (result->NextRow());
            }
        }

        // ---- rolling one player's loot ---------------------------------------------------------------
        uint8 RollQuality(uint8 level, float multiplier, uint8 minimum)
        {
            Config const& c = Conf();
            float green = float(c.qualityGreen);
            float blue = float(c.qualityBlue) * multiplier;
            float epic = float(c.qualityEpic) * multiplier * EpicFactor(level);
            float roll = frand(0.0f, green + blue + epic);
            uint8 quality = roll < epic ? ITEM_QUALITY_EPIC : (roll < epic + blue ? ITEM_QUALITY_RARE : ITEM_QUALITY_UNCOMMON);
            quality = std::max(quality, minimum);
            if (quality == ITEM_QUALITY_UNCOMMON && roll_chance_f(c.upgradeGreenToBlue * multiplier))
                quality = ITEM_QUALITY_RARE;
            if (quality == ITEM_QUALITY_RARE && roll_chance_f(c.upgradeBlueToEpic * multiplier * EpicFactor(level)))
                quality = ITEM_QUALITY_EPIC;
            return quality;
        }

        std::vector<Drop> RollLoot(Kind kind, uint8 level, float multiplier, uint32 extraItems)
        {
            Config const& c = Conf();
            std::vector<Drop> out;
            uint32 total = urand(c.itemsMin, c.itemsMax) + extraItems;
            auto add = [&](uint32 item, uint32 count, bool headline)
            {
                if (!item)
                    return;
                // Last check: gear above the looter's level is shrunk to it; if it can't be, it isn't dropped.
                if (ItemTemplate const* p = sObjectMgr->GetItemTemplate(item))
                    if ((p->Class == ITEM_CLASS_WEAPON || p->Class == ITEM_CLASS_ARMOR) && ItemForLevel(p->RequiredLevel, p->ItemLevel) > uint32(level) + 3)
                    {
                        item = ScaledCopy(BaseOfCopy(item), level);
                        if (!item)
                            return;
                    }
                out.push_back({ item, std::max(1u, count), headline });
            };

            switch (kind)
            {
                // the goblin's promise: its headline item is always there (a pick can come back empty or unusable)
                case Kind::Uncommon:
                    for (uint32 i = 0; i < 6 && out.empty(); ++i)
                        add(PickGear(RollQuality(level, multiplier, ITEM_QUALITY_UNCOMMON), level), 1, true);
                    break;
                case Kind::Rare:
                    for (uint32 i = 0; i < 6 && out.empty(); ++i)
                        add(PickGear(RollQuality(level, multiplier, ITEM_QUALITY_RARE), level), 1, true);
                    break;
                case Kind::Epic:
                    for (uint32 i = 0; i < 6 && out.empty(); ++i)
                        add(PickGear(ITEM_QUALITY_EPIC, level), 1, true);
                    break;
                case Kind::Unknown:
                    for (uint32 i = 0; i < c.unknownItems; ++i)
                        add(PickUnknown(level), 1, true);
                    break;
                case Kind::Worldforged:
                    for (uint32 i = 0; i < c.worldforgedItems; ++i)
                        add(PickWorldforged(level), 1, true);
                    if (out.empty())
                        add(PickGear(ITEM_QUALITY_RARE, level), 1, true);   // no Worldforged items in this database
                    break;
                default:
                    if (IsGoodsKind(kind))
                    {
                        uint8 profession = uint8(kind) - uint8(Kind::Mining);
                        uint32 stacks = urand(3, 5);
                        for (uint32 i = 0; i < stacks; ++i)
                            add(PickGoods(profession, level), urand(2, 6), false);
                    }
                    break;
            }
            for (uint32 attempt = 0; out.size() < total && attempt < total * 4; ++attempt)
            {
                if (roll_chance_i(50))
                    add(PickGear(RollQuality(level, multiplier, ITEM_QUALITY_UNCOMMON), level), 1, false);
                else
                    add(PickGoods(uint8(urand(0, 11)), level), urand(1, 4), false);
            }
            return out;
        }

        uint32 RollGold(Kind kind, uint8 level, float multiplier)
        {
            float base = float(Conf().goldPerLevel) * std::max<uint8>(1, level) * frand(1.0f, 3.0f);
            if (kind == Kind::Gold)
                base *= Conf().goldGoblinMultiplier;
            else if (kind == Kind::WarSpoils)
                base *= 4.0f;
            return uint32(base * std::max(0.5f, multiplier));
        }

        uint8 PileQuality(std::vector<Drop> const& drops, uint32 gold)
        {
            uint8 q = 0;
            for (Drop const& d : drops)
                if (ItemTemplate const* p = sObjectMgr->GetItemTemplate(d.item))
                    q = std::max<uint8>(q, uint8(p->Quality));
            if (drops.empty() && gold)
                return 6;
            return std::max<uint8>(q, 1);
        }

        // ---- piles -------------------------------------------------------------------------------------
        void Land(Creature* pile, FlyingPile const& f, Player* owner)
        {
            StandStill(pile);
            pile->RemoveUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
            // no "<Name's Minion>" line under the item's name (the pile stays visible to its owner only)
            pile->SetGuidValue(UNIT_FIELD_SUMMONEDBY, ObjectGuid::Empty);
            pile->SetGuidValue(UNIT_FIELD_CREATEDBY, ObjectGuid::Empty);
            ItemTemplate const* held = f.drops.size() == 1 ? sObjectMgr->GetItemTemplate(BaseOfCopy(f.drops.front().item)) : nullptr;
            auto carrier = gBeamDisplay.find(8);
            if (Conf().pileItemModels && IsHeldItem(held) && carrier != gBeamDisplay.end())
            {   // weapons and shields: the real item lies on the ground (held by an invisible carrier)
                pile->SetDisplayId(carrier->second);
                pile->SetNativeDisplayId(carrier->second);
                for (uint8 slot = 0; slot < 3; ++slot)
                    pile->SetVirtualItem(slot, 0);
                pile->SetVirtualItem(HeldSlot(held), held->ItemId);
                pile->SetSheath(SHEATH_STATE_MELEE);
            }
            else if (auto flippy = gBeamDisplay.find(FlippyKey(held)); Conf().pileItemModels && flippy != gBeamDisplay.end())
            {   // armor and jewelry: the D3 ground model for its kind
                pile->SetDisplayId(flippy->second);
                pile->SetNativeDisplayId(flippy->second);
            }
            else
            {   // a real, clickable heap of gold: dead creatures can only be looted by clicking their model

                auto heap = gBeamDisplay.find(7);
                if (heap != gBeamDisplay.end())
                {
                    pile->SetDisplayId(heap->second);
                    pile->SetNativeDisplayId(heap->second);
                }
            }
            // the pile is a corpse: keep it for the whole loot lifetime (the realm's corpse decay is far shorter)
            pile->SetCorpseDelay(Conf().lootLifetime + 5);
            pile->setDeathState(DeathState::JustDied);
            pile->SetCorpseRemoveTime(Conf().lootLifetime + 5);
            pile->loot.clear();
            pile->loot.lootOwnerGUID = f.owner;
            pile->loot.loot_type = LOOT_CORPSE;
            for (Drop const& d : f.drops)
                pile->loot.AddItem(LootStoreItem(d.item, 0, 100.0f, false, LOOT_MODE_DEFAULT, 0, int32(d.count), uint8(std::min<uint32>(d.count, 255))));
            pile->loot.gold = f.gold;
            // WoW only lets you loot a corpse that players did enough damage to; the pile was never hit, so
            // count it as fully damaged by its owner - without this the client never offers to loot it.
            pile->LowerPlayerDamageReq(pile->GetMaxHealth() + 1, true, uint8(owner->GetLevel()));
            // the owner's group (if any) is the recipient, else the server refuses grouped players
            pile->SetLootRecipient(owner, owner->GetGroup() != nullptr);
            pile->loot.roundRobinPlayer = owner->GetGUID();
            pile->SetDynamicFlag(UNIT_DYNFLAG_LOOTABLE);

            LandedPile landed;
            landed.pile = pile->GetGUID();
            landed.owner = f.owner;
            landed.endMs = NowMs() + Conf().lootLifetime * IN_MILLISECONDS;
            uint8 quality = PileQuality(f.drops, f.gold);
            if (Conf().beams && quality >= Conf().beamMinQuality)
            {
                auto itr = gBeamDisplay.find(quality);
                if (itr != gBeamDisplay.end())
                    if (TempSummon* beam = owner->SummonCreature(Conf().beamEntry, f.land, TEMPSUMMON_TIMED_DESPAWN,
                            Conf().lootLifetime * IN_MILLISECONDS, 0, nullptr, Conf().personalVisibility))
                    {
                        beam->SetDisplayId(itr->second);
                        beam->SetNativeDisplayId(itr->second);
                        beam->SetGuidValue(UNIT_FIELD_SUMMONEDBY, ObjectGuid::Empty);
                        beam->SetGuidValue(UNIT_FIELD_CREATEDBY, ObjectGuid::Empty);
                        beam->SetReactState(REACT_PASSIVE);
                        StandStill(beam);
                        // a creature summoned by a player is treated as theirs and trails after them: nail it down
                        beam->SetUnitFlag(UNIT_FLAG_DISABLE_MOVE);
                        beam->SetControlled(true, UNIT_STATE_ROOT);
                        beam->SetSpeed(MOVE_WALK, 0.0f);
                        beam->SetSpeed(MOVE_RUN, 0.0f);
                        landed.beam = beam->GetGUID();
                    }
            }
            std::lock_guard lock(gPileMutex);
            gLanded.push_back(landed);
        }

        void SpawnPile(Player* owner, Position const& from, std::vector<Drop> drops, uint32 gold, float lift = 0.0f, float distance = 0.0f)
        {
            if (!owner || (drops.empty() && !gold))
                return;
            if (!gVisualsLoaded)
                LoadVisuals();
            Position start = from;
            float angle = float(rand_norm()) * 2.0f * float(M_PI);
            float dist = distance > 0.0f ? distance : Conf().pinata ? frand(3.0f, 7.0f) : frand(1.5f, 4.0f);
            if (distance > 0.0f)
                angle = from.GetOrientation();
            Position land(from);
            if (Map* map = owner->GetMap())
            {
                // Land where the goblin could walk to: not through walls, off cliffs, outside caves or buildings (WMOs),
                // or out over the void. Several directions are tried; the goblin's own spot is the fallback.
                uint32 const phase = owner->GetPhaseMask();
                float const fz = from.GetPositionZ();
                bool found = false;
                for (uint32 attempt = 0; attempt < 8 && !found; ++attempt)
                {
                    float a = attempt == 0 ? angle : float(rand_norm()) * 2.0f * float(M_PI);
                    float d = attempt < 4 ? dist : dist * 0.5f;
                    float x = from.GetPositionX() + d * std::cos(a), y = from.GetPositionY() + d * std::sin(a), z = fz;
                    if (!map->CanReachPositionAndGetValidCoords(owner, from.GetPositionX(), from.GetPositionY(), fz, x, y, z, true, true))
                        continue;
                    float ground = map->GetHeight(phase, x, y, z + 2.0f, true, 10.0f);   // +2: stays under a cave or house ceiling
                    if (ground <= INVALID_HEIGHT || std::fabs(ground - fz) > 4.0f)
                        continue;
                    if (!map->isInLineOfSight(from.GetPositionX(), from.GetPositionY(), fz + 1.5f, x, y, ground + 0.5f, phase,
                            LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
                        continue;
                    land.Relocate(x, y, ground);
                    found = true;
                }
                if (!found)
                {
                    float ground = map->GetHeight(phase, from.GetPositionX(), from.GetPositionY(), fz + 2.0f, true, 10.0f);
                    if (ground > INVALID_HEIGHT)
                        land.m_positionZ = ground;
                }
            }
            dist = from.GetExactDist2d(land);
            land.m_positionZ += lift;
            uint32 entry = Conf().pileEntry;
            if (Conf().pileNames && Conf().pileSlots && sObjectMgr->GetCreatureTemplate(Conf().pileSlotFirst))
            {
                uint32 slot = Conf().pileSlotFirst + (gNextSlot++ % Conf().pileSlots);
                if (sObjectMgr->GetCreatureTemplate(slot))
                {
                    entry = slot;
                    std::string name, sub;
                    ItemTemplate const* p = drops.empty() ? nullptr : sObjectMgr->GetItemTemplate(drops.front().item);
                    if (p)
                    {
                        name = p->Name1;
                        if (Conf().pileNameColors)   // experiment: does the client colour names over units? (quality colours)
                        {
                            static char const* const hex[] = { "9d9d9d", "ffffff", "1eff00", "0070dd", "a335ee", "ff8000", "e6cc80", "e6cc80" };
                            name = Acore::StringFormat("|cff{}{}|r", hex[std::min<uint32>(p->Quality, 7)], name);
                        }
                        if (drops.front().count > 1)
                            name += Acore::StringFormat(" x{}", drops.front().count);
                        sub = QualityName(p->Quality);
                        if (gold)
                            sub += " + " + MoneyText(gold);
                    }
                    else
                        name = MoneyText(gold);
                    {
                        std::lock_guard lock(gNameMutex);
                        gPileNames[{ owner->GetSession()->GetAccountId(), entry }] = { name, sub };
                    }
                    SendPileName(owner, entry);
                }
            }
            FlyingPile f;
            f.owner = owner->GetGUID();
            f.entry = entry;
            f.drops = std::move(drops);
            f.gold = gold;
            f.land = land;
            f.mapId = owner->GetMapId();
            f.landMs = NowMs();
            if (Conf().pinata)
            {
                // the arc is only a look: a gold heap flies out, and the real (lootable) pile appears where it lands
                float speedXY = frand(6.0f, 9.0f);
                if (TempSummon* flyer = owner->SummonCreature(Conf().coinEntry, start, TEMPSUMMON_TIMED_DESPAWN,
                        uint32(dist / speedXY * 1000.0f) + 400, 0, nullptr, Conf().personalVisibility))
                {
                    auto heap = gBeamDisplay.find(7);
                    if (heap != gBeamDisplay.end())
                    {
                        flyer->SetDisplayId(heap->second);
                        flyer->SetNativeDisplayId(heap->second);
                    }
                    flyer->SetUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
                    flyer->SetGuidValue(UNIT_FIELD_SUMMONEDBY, ObjectGuid::Empty);
                    flyer->GetMotionMaster()->MoveJump(land, speedXY, frand(9.0f, 13.0f));
                    f.pile = flyer->GetGUID();
                }
                f.landMs += uint32(dist / speedXY * 1000.0f) + 250;
            }
            std::lock_guard lock(gPileMutex);
            gFlying.push_back(std::move(f));
        }

        // ---- group rolls ------------------------------------------------------------------------------
        float Multiplier(float sharePct, uint32 count)
        {
            if (count <= 1)
                return 1.0f;
            float r = sharePct / 100.0f * float(count);
            return std::clamp(1.0f + Conf().contributionBonus * (r - 1.0f), Conf().contributionMin, Conf().contributionMax);
        }

        void TellGroup(RollSession const& s, std::string const& text)
        {
            for (ObjectGuid const& g : s.members)
                if (Player* p = ObjectAccessor::FindPlayer(g))
                    ChatHandler(p->GetSession()).SendSysMessage(text);
        }

        void Resolve(RollSession& s)
        {
            Config const& c = Conf();
            for (uint32 index = 0; index < s.items.size(); ++index)
            {
                Drop const& item = s.items[index];
                std::vector<ObjectGuid> need, greed;
                for (ObjectGuid const& g : s.members)
                {
                    auto itr = s.choices.find({ index, g });
                    uint8 choice = itr == s.choices.end() ? 2 : itr->second;   // no answer = greed
                    if (choice == 1) need.push_back(g);
                    else if (choice == 2) greed.push_back(g);
                }
                std::vector<ObjectGuid>& pool = need.empty() ? greed : need;
                char const* how = need.empty() ? "Greed" : "Need";
                if (pool.empty())
                {
                    TellGroup(s, Acore::StringFormat("|cffffd000[Loot Goblin]|r Everyone passed on {}.", ItemLink(item.item)));
                    continue;
                }

                ObjectGuid winner;
                uint32 best = 0, bestRoll = 0, bestBonus = 0;
                std::string results;
                for (int round = 0; round < 5 && !winner; ++round)
                {
                    std::vector<ObjectGuid> tied;
                    best = 0;
                    results.clear();
                    for (ObjectGuid const& g : pool)
                    {
                        uint32 bonus = uint32(std::ceil(s.sharePct[g] / c.rollBonusDivisor));
                        uint32 roll = urand(1, 100);
                        uint32 total = roll + bonus;
                        Player* p = ObjectAccessor::FindPlayer(g);
                        results += Acore::StringFormat("{}{} {} (+{})", results.empty() ? "" : ", ", p ? p->GetName() : "?", roll, bonus);
                        if (total > best) { best = total; tied = { g }; bestRoll = roll; bestBonus = bonus; }
                        else if (total == best) tied.push_back(g);
                    }
                    if (tied.size() == 1)
                        winner = tied.front();
                    else
                        pool = tied;   // ties roll again
                }
                if (!winner)
                    winner = pool.front();

                Player* w = ObjectAccessor::FindPlayer(winner);
                if (!w)
                    continue;
                uint8 level = uint8(w->GetLevel());
                uint32 entry = item.item;
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                // Shrink (or re-shrink) to the winner's level when it was rolled for someone else.
                if (proto && ItemForLevel(proto->RequiredLevel, proto->ItemLevel) > uint32(level) + 3)
                    if (uint32 copy = ScaledCopy(BaseOfCopy(entry), level))
                        entry = copy;
                // Upgrade: base % x (total roll / 100) x share multiplier.
                bool upgraded = false;
                if (proto && proto->Quality >= ITEM_QUALITY_UNCOMMON && proto->Quality < ITEM_QUALITY_EPIC)
                {
                    float base = proto->Quality == ITEM_QUALITY_UNCOMMON ? float(c.upgradeGreenToBlue) : float(c.upgradeBlueToEpic) * EpicFactor(level);
                    float chance = base * (float(best) / 100.0f) * s.multiplier[winner];
                    if (roll_chance_f(chance))
                        if (uint32 better = PickGear(uint8(proto->Quality + 1), level))
                        {
                            entry = better;
                            upgraded = true;
                        }
                }
                TellGroup(s, Acore::StringFormat("|cffffd000[Loot Goblin]|r {} wins {} - {} {} (+{}) = {}{}. Rolls: {}",
                    w->GetName(), ItemLink(entry), how, bestRoll, bestBonus, best, upgraded ? ", |cffffff00upgraded!|r" : "", results));
                for (ObjectGuid const& g : s.members)
                    if (Player* p = ObjectAccessor::FindPlayer(g))
                        if (HasAddon(p))
                            SendAddon(p, Acore::StringFormat("RESULT\t{}\t{}\t{}\t{}\t{}\t{}", s.id, index, w->GetName(), entry, best, upgraded ? 1 : 0));
                if (w->GetMapId() == s.mapId)
                    SpawnPile(w, s.pos, { Drop{ entry, item.count, false } }, 0);
            }
        }
    }

    // ---- public ------------------------------------------------------------------------------------
    std::string ItemLink(uint32 entry)
    {
        ItemTemplate const* p = sObjectMgr->GetItemTemplate(entry);
        if (!p)
            return Acore::StringFormat("item {}", entry);
        return Acore::StringFormat("|c{}|Hitem:{}:0:0:0:0:0:0:0:0|h[{}]|h|r", QUALITY_COLOR[std::min<uint32>(p->Quality, 7)], entry, p->Name1);
    }

    void ResetLootVisuals() { gVisualsLoaded = false; }

    thread_local bool tSendingPileName = false;
    bool SendingPileName() { return tSendingPileName; }

    // Tell this client what a drop slot is called right now (it caches names by creature entry).
    void SendPileNameTo(WorldSession* session, uint32 entry)
    {
        CreatureTemplate const* ci = sObjectMgr->GetCreatureTemplate(entry);
        std::string name, sub;
        if (!session || !ci || !PileNameFor(session, entry, name, sub))
            return;
        WorldPacket data(SMSG_CREATURE_QUERY_RESPONSE, 100);
        data << uint32(entry) << name << uint8(0) << uint8(0) << uint8(0) << sub << ci->IconName;
        data << uint32(ci->type_flags | (Conf().forcedNameplates ? FORCED_NAMEPLATE_FLAG : 0)) << uint32(ci->type) << uint32(ci->family) << uint32(ci->rank);
        data << uint32(ci->KillCredit[0]) << uint32(ci->KillCredit[1]);
        for (uint8 i = 0; i < 4; ++i)
            data << uint32(ci->GetModelByIdx(i) ? ci->GetModelByIdx(i)->CreatureDisplayID : 0);
        data << float(ci->ModHealth) << float(ci->ModMana) << uint8(ci->RacialLeader);
        for (std::size_t i = 0; i < MAX_CREATURE_QUEST_ITEMS; ++i)
            data << uint32(0);
        data << uint32(ci->movementId);
        tSendingPileName = true;
        session->SendPacket(&data);
        tSendingPileName = false;
    }

    // Goblins and bosses: the core's own answer, with the forced-nameplate flag added.
    bool SendGoblinQueryWithPlate(WorldSession* session, WorldPacket const& packet)
    {
        if (!session || !Conf().forcedNameplates || packet.size() < 8)
            return false;
        CreatureTemplate const* ci = sObjectMgr->GetCreatureTemplate(packet.read<uint32>(0));
        if (!ci)
            return false;
        std::string const& script = sObjectMgr->GetScriptName(ci->ScriptID);
        if (script != "npc_loot_goblin" && script != "npc_loot_goblin_boss")
            return false;
        // entry, name, 3 empty names, subname, icon name - then the type flags
        std::size_t pos = 4;
        for (int strings = 0; strings < 6; ++strings)
        {
            while (pos < packet.size() && packet.read<uint8>(pos) != 0)
                ++pos;
            ++pos;
        }
        if (pos + 4 > packet.size())
            return false;
        WorldPacket copy(packet);
        copy.put<uint32>(pos, packet.read<uint32>(pos) | FORCED_NAMEPLATE_FLAG);
        tSendingPileName = true;
        session->SendPacket(&copy);
        tSendingPileName = false;
        return true;
    }

    std::string MoneyText(uint32 copper)
    {
        uint32 g = copper / 10000, sv = (copper / 100) % 100, c = copper % 100;
        std::string out;
        auto part = [&](uint32 v, char const* unit) { if (v) out += (out.empty() ? "" : " ") + Acore::StringFormat("{} {}", v, unit); };
        part(g, "Gold");
        part(sv, "Silver");
        part(c, "Copper");
        return out.empty() ? "0 Copper" : out;
    }

    bool PileNameFor(WorldSession* session, uint32 entry, std::string& name, std::string& sub)
    {
        if (!session)
            return false;
        std::lock_guard lock(gNameMutex);
        auto itr = gPileNames.find({ session->GetAccountId(), entry });
        if (itr == gPileNames.end())
            return false;
        name = itr->second.first;
        sub = itr->second.second;
        return true;
    }

    void AddonHello(Player* player) { if (player) gAddonUsers.insert(player->GetGUID()); }
    bool HasAddon(Player const* player) { return player && gAddonUsers.count(player->GetGUID()); }
    void AddonForget(ObjectGuid const& guid) { gAddonUsers.erase(guid); }

    void SendAddon(Player* player, std::string const& message)
    {
        if (!player || !player->GetSession())
            return;
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, std::string(ADDON_PREFIX) + "\t" + message);
        player->SendDirectMessage(&data);
    }

    void HandleAddonMessage(Player* player, std::string const& message)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= message.size())
        {
            size_t tab = message.find('\t', start);
            parts.push_back(message.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos)
                break;
            start = tab + 1;
        }
        if (parts.size() < 2 || parts[0] != ADDON_PREFIX)
            return;
        if (parts[1] == "HELLO")
        {
            AddonHello(player);
            WorldPacket stop(SMSG_STOP_MIRROR_TIMER, 4);   // the addon draws the timer from now on
            stop << uint32(0);   // MIRROR_FATIGUE
            player->SendDirectMessage(&stop);
            // which creature entries are drops, so the addon can dress their nameplates as item labels
            SendAddon(player, Acore::StringFormat("PILES\t{}\t{}", Conf().pileSlotFirst, Conf().pileSlots));
            return;
        }
        if (parts[1] == "CHOICE" && parts.size() >= 5)
        {
            uint32 id = uint32(std::strtoul(parts[2].c_str(), nullptr, 10));
            uint32 index = uint32(std::strtoul(parts[3].c_str(), nullptr, 10));
            auto itr = gRolls.find(id);
            if (itr == gRolls.end() || index >= itr->second.items.size())
                return;
            RollSession& s = itr->second;
            if (std::find(s.members.begin(), s.members.end(), player->GetGUID()) == s.members.end())
                return;
            uint8 choice = parts[4] == "need" ? 1 : (parts[4] == "greed" ? 2 : 3);
            s.choices[{ index, player->GetGUID() }] = choice;
        }
    }

    std::unordered_map<ObjectGuid, float> Shares(GoblinState const& state, uint32 nowMs)
    {
        Config const& c = Conf();
        std::unordered_map<ObjectGuid, float> out;
        float total = 0.0f;
        for (auto const& [guid, p] : state.participants)
            total += p.contribution;
        uint32 fight = state.firstHitMs && nowMs > state.firstHitMs ? nowMs - state.firstHitMs : 0;
        uint32 n = 0;
        for (auto const& [guid, p] : state.participants)
            if (p.contribution > 0.0f || p.presenceMs > 0)
                ++n;
        for (auto const& [guid, p] : state.participants)
        {
            float share = total > 0.0f ? p.contribution * 100.0f / total : 0.0f;
            // Being there counts: stayed for most of the fight -> at least a fair share, whatever the role.
            if (n && fight && float(p.presenceMs) * 100.0f / float(fight) >= c.presenceFloor)
                share = std::max(share, 100.0f / float(n));
            out[guid] = share;
        }
        return out;
    }

    bool DropTestItem(Player* owner, uint32 item, uint32 count, float lift)
    {
        ItemTemplate const* p = sObjectMgr->GetItemTemplate(item);
        if (!owner || !p)
            return false;
        SpawnPile(owner, *owner, { Drop{ item, std::max<uint32>(count, 1), false } }, 0, lift, 4.0f);
        return true;
    }

    void DropLoot(Creature* goblin, GoblinState const& state)
    {
        Config const& c = Conf();
        std::unordered_map<ObjectGuid, float> shares = Shares(state, NowMs());

        // Who counts, and their share.
        std::vector<std::pair<Player*, float>> people;
        for (auto const& [guid, share] : shares)
            if (Player* player = ObjectAccessor::GetPlayer(*goblin, guid))
                if (state.participants.size() == 1 || share >= c.minContribution)
                    people.emplace_back(player, share);
        uint32 count = uint32(people.size());

        // Group rolls: participants of one group (in dungeons and raids by default) roll for the headline items.
        Map* map = goblin->GetMap();
        bool rollsHere = c.groupRollMode == 2 || (c.groupRollMode == 1 && map && map->IsDungeon());
        std::map<Group*, RollSession> sessions;

        for (auto const& [player, sharePct] : people)
        {
            float mult = Multiplier(sharePct, count) * c.rankLoot[uint8(state.rank)] * (state.boss ? c.bossLootMultiplier : 1.0f);
            uint8 level = uint8(player->GetLevel());
            uint32 extra = c.rankExtraItems[uint8(state.rank)] + (state.boss == 1 ? c.greedExtraItems : state.boss == 2 ? c.hardExtraItems : 0);
            std::vector<Drop> drops = RollLoot(state.kind, level, mult, extra);
            uint32 gold = RollGold(state.kind, level, mult);

            Group* group = rollsHere ? player->GetGroup() : nullptr;
            if (group)
            {
                uint32 inGroup = 0;
                for (auto const& [other, otherShare] : people)
                    if (other->GetGroup() == group)
                        ++inGroup;
                if (inGroup < 2)
                    group = nullptr;
            }
            if (group)
            {
                RollSession& s = sessions[group];
                float share = sharePct;   // already floored for everyone who stayed in the fight
                s.members.push_back(player->GetGUID());
                s.sharePct[player->GetGUID()] = share;
                s.multiplier[player->GetGUID()] = mult;
                std::vector<Drop> keep;
                for (Drop const& d : drops)
                    (d.headline ? s.items : keep).push_back(d);
                drops.swap(keep);
            }

            {   // server log: what each looter got (to check a goblin's promise)
                std::string list;
                for (Drop const& d : drops)
                    if (ItemTemplate const* p = sObjectMgr->GetItemTemplate(d.item))
                        list += Acore::StringFormat(" [{} q{} x{}]", p->Name1, p->Quality, d.count);
                LOG_INFO("module", "LootGoblins: {} <{}> -> {} (L{}): {}{} + {} copper", goblin->GetName(), Info(state.kind).subname,
                    player->GetName(), level, list.empty() ? " nothing" : list, group ? " (headline to group roll)" : "", gold);
            }

            // Personal piles: one per item, and the money as its own gold heap (as in Diablo III)
            for (Drop const& d : drops)
                SpawnPile(player, *goblin, { d }, 0);
            if (gold)
                SpawnPile(player, *goblin, {}, gold);

            for (Drop const& d : drops)
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(d.item))
                    if (proto->Quality >= ITEM_QUALITY_EPIC && c.announce)
                        Announce(Acore::StringFormat("{} looted {} from a Loot Goblin!", player->GetName(), ItemLink(d.item)));
        }

        for (auto& [group, s] : sessions)
        {
            if (s.items.empty())
                continue;
            s.id = gNextRollId++;
            s.kind = state.kind;
            s.mapId = goblin->GetMapId();
            s.pos = goblin->GetPosition();
            s.deadlineMs = NowMs() + c.rollTimeout * IN_MILLISECONDS;
            for (uint32 i = 0; i < s.items.size(); ++i)
            {
                for (ObjectGuid const& g : s.members)
                    if (Player* p = ObjectAccessor::FindPlayer(g))
                    {
                        if (HasAddon(p))
                            SendAddon(p, Acore::StringFormat("ROLL\t{}\t{}\t{}\t{}\t{}", s.id, i, s.items[i].item, s.items[i].count, c.rollTimeout));
                        else
                            s.choices[{ i, g }] = 2;   // no roll window: greed
                    }
                TellGroup(s, Acore::StringFormat("|cffffd000[Loot Goblin]|r Rolling for {} ({} s).", ItemLink(s.items[i].item), c.rollTimeout));
            }
            gRolls[s.id] = std::move(s);
        }
    }

    // From the owner's own update (map thread): land their flying piles, clean up their looted / old ones.
    void UpdateLootFor(Player* owner)
    {
        if (!owner || !owner->IsInWorld())
            return;
        uint32 now = NowMs();
        ObjectGuid me = owner->GetGUID();
        std::vector<FlyingPile> landing;
        std::vector<LandedPile> finished;
        {
            std::lock_guard lock(gPileMutex);
            if (gFlying.empty() && gLanded.empty())
                return;
            for (auto itr = gFlying.begin(); itr != gFlying.end();)
            {
                if (itr->owner != me || now < itr->landMs) { ++itr; continue; }
                landing.push_back(std::move(*itr));
                itr = gFlying.erase(itr);
            }
            for (auto itr = gLanded.begin(); itr != gLanded.end();)
            {
                if (itr->owner != me) { ++itr; continue; }
                Creature* pile = owner->GetMap()->GetCreature(itr->pile);
                bool done = !pile || now >= itr->endMs || (pile->loot.isLooted() && !pile->loot.gold);
                if (!done) { ++itr; continue; }
                finished.push_back(*itr);
                itr = gLanded.erase(itr);
            }
        }
        for (FlyingPile const& f : landing)
        {
            if (Creature* flyer = owner->GetMap()->GetCreature(f.pile))
                flyer->DespawnOrUnsummon();
            if (owner->GetMapId() != f.mapId || owner->GetExactDist2d(f.land.GetPositionX(), f.land.GetPositionY()) > 150.0f)
                continue;   // left the area before it landed
            if (TempSummon* pile = owner->SummonCreature(f.entry, f.land, TEMPSUMMON_TIMED_DESPAWN,
                    (Conf().lootLifetime + 10) * IN_MILLISECONDS, 0, nullptr, Conf().personalVisibility))
                Land(pile, f, owner);
        }
        for (LandedPile const& l : finished)
        {
            if (Creature* beam = owner->GetMap()->GetCreature(l.beam))
                beam->DespawnOrUnsummon();
            if (Creature* pile = owner->GetMap()->GetCreature(l.pile))
                if (pile->loot.isLooted())
                    pile->DespawnOrUnsummon(1s);
        }
    }

    void UpdateLoot(uint32 /*diff*/)
    {
        uint32 now = NowMs();
        {   // owners who left: their piles go with them
            std::lock_guard lock(gPileMutex);
            gFlying.remove_if([](FlyingPile const& f) { return !ObjectAccessor::FindPlayer(f.owner); });
            gLanded.remove_if([](LandedPile const& l) { return !ObjectAccessor::FindPlayer(l.owner); });
        }

        for (auto itr = gRolls.begin(); itr != gRolls.end();)
        {
            RollSession& s = itr->second;
            bool everyone = s.choices.size() >= s.items.size() * s.members.size();
            if (!everyone && now < s.deadlineMs) { ++itr; continue; }
            Resolve(s);
            itr = gRolls.erase(itr);
        }
    }
}
