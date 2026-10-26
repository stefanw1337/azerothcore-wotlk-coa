#include "CoAPortableImport.h"
#include "AccountMgr.h"
#include "Base64.h"
#include "CharacterCache.h"
#include "CoAPortableJson.h"
#include "CoAPortableProjection.h"
#include "CoAPortableSession.h"
#include "Config.h"
#include "GameTime.h"
#include "CryptoHash.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Tokenize.h"
#include "Util.h"
#include "World.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

class CoAPortableImportAccess
{
public:
    static uint32 HighestPet() { return sObjectMgr->_hiPetNumber; }
    static void SetHighestPet(uint32 value) { sObjectMgr->_hiPetNumber = value; }
};

namespace CoAPortableImport
{
    using CoAPortableJson::Reader;
    using CoAPortableJson::Value;

    namespace
    {
        constexpr std::size_t MaxJobBytes = 24 * 1024 * 1024;
        constexpr uint64 MaxMoney = 0x7FFFFFFEull;
        constexpr std::size_t MaxName = 25;
        constexpr std::size_t MaxRaw = 8 * 1024;
        constexpr uint32 NameColumnChars = 25;

        struct Row
        {
            std::string Table;
            std::string Column;
        };

        std::vector<Row> const CharacterKeyed = {
            { "ascension_manastorm_bonus", "guid" }, { "ascension_manastorm_cache", "guid" }, { "ascension_manastorm_clear", "guid" },
            { "ascension_manastorm_loadout", "guid" }, { "ascension_manastorm_xp", "guid" }, { "character_account_data", "guid" },
            { "character_achievement", "guid" }, { "character_achievement_offline_updates", "guid" },
            { "character_achievement_progress", "guid" }, { "character_action", "guid" }, { "character_appearance", "guid" },
            { "character_appearance_outfit", "guid" }, { "character_appearance_settings", "guid" }, { "character_arena_stats", "guid" },
            { "character_ascension_state", "guid" }, { "character_aura", "guid" }, { "character_banned", "guid" },
            { "character_battleground_random", "guid" }, { "character_brew_of_the_month", "guid" }, { "character_coa_lfg_settings", "guid" },
            { "character_declinedname", "guid" }, { "character_entry_point", "guid" }, { "character_equipmentsets", "guid" },
            { "character_gifts", "guid" }, { "character_glyphs", "guid" }, { "character_homebind", "guid" }, { "character_instance", "guid" },
            { "character_inventory", "guid" }, { "character_pet", "owner" }, { "character_pet_declinedname", "owner" },
            { "character_queststatus", "guid" }, { "character_queststatus_daily", "guid" }, { "character_queststatus_monthly", "guid" },
            { "character_queststatus_rewarded", "guid" }, { "character_queststatus_seasonal", "guid" }, { "character_queststatus_weekly", "guid" },
            { "character_reputation", "guid" }, { "character_settings", "guid" }, { "character_skills", "guid" }, { "character_spell", "guid" },
            { "character_spell_cooldown", "guid" }, { "character_stats", "guid" }, { "character_talent", "guid" },
            { "character_worldforged_loot", "guid" }, { "coa_challenge_completion", "guid" }, { "coa_challenge_failure", "guid" },
            { "coa_character_challenge", "guid" }, { "coa_character_condition", "guid" }, { "coa_character_fatigue", "guid" },
            { "coa_character_gamemode", "guid" }, { "coa_character_gamemode_lives", "guid" }, { "coa_character_looted_item", "guid" },
            { "coa_character_objective", "guid" }, { "coa_character_survival", "guid" }, { "coa_custom_trial", "guid" },
            { "coa_custom_trial_active", "guid" }, { "coa_custom_trial_completion", "guid" }, { "coa_custom_trial_entry", "guid" },
            { "coa_custom_trial_vote", "guid" }, { "corpse", "guid" }, { "item_instance", "owner_guid" }, { "mod_craftsmans_codex", "guid" },
            { "coa_portable_session", "guid" },
        };

        std::vector<Row> const ItemKeyed = {
            { "item_instance", "guid" }, { "character_inventory", "item" }, { "character_gifts", "item_guid" }, { "mail_items", "item_guid" },
            { "auctionhouse", "itemguid" }, { "guild_bank_item", "item_guid" }, { "item_refund_instance", "item_guid" },
            { "item_soulbound_trade_data", "itemGuid" }, { "item_loot_storage", "containerGUID" }, { "ascension_manastorm_cache", "item" },
            { "highrisk_chest_item", "item_guid" }, { "mod_ascension_bank_item", "item_guid" }, { "coa_character_looted_item", "itemGuid" },
        };

        std::vector<Row> const PetKeyed = {
            { "character_pet", "id" }, { "pet_spell", "guid" }, { "pet_aura", "guid" }, { "pet_spell_cooldown", "guid" },
            { "character_pet_declinedname", "id" },
        };

        struct Enchant
        {
            uint8 Slot = 0;
            uint32 Id = 0;
            uint32 Duration = 0;
            uint32 Charges = 0;
        };

        struct ItemRow
        {
            std::string Id;
            bool HasContainer = false;
            std::string Container;
            uint32 Slot = 0;
            uint32 Entry = 0;
            uint32 Count = 0;
            int32 Duration = 0;
            std::vector<int32> Charges;
            uint32 Flags = 0;
            std::vector<Enchant> Enchants;
            int32 RandomProperty = 0;
            uint32 Durability = 0;
            uint32 PlayedTime = 0;
            bool HasText = false;
            std::string Text;
            bool HasGift = false;
            uint32 GiftEntry = 0;
            uint32 GiftFlags = 0;
        };

        struct PetRow
        {
            uint32 Entry = 0;
            uint32 Model = 0;
            uint32 CreatedBy = 0;
            uint32 Type = 0;
            uint32 Level = 0;
            uint32 Exp = 0;
            uint32 React = 0;
            std::string Name;
            bool Renamed = false;
            uint32 Slot = 0;
            uint32 Health = 0;
            uint32 Mana = 0;
            uint32 Happiness = 0;
            std::string ActionBar;
            std::vector<std::pair<uint32, uint32>> Spells;
            bool HasDeclined = false;
            std::string Declined[5];
        };

        struct SkillRow
        {
            uint32 Skill = 0;
            uint32 Value = 0;
            uint32 Max = 0;
        };

        struct GlyphRow
        {
            uint32 Group = 0;
            uint32 Glyph[6] = { };
        };

        struct QuestRow
        {
            uint32 Quest = 0;
            uint32 Status = 0;
            bool Explored = false;
            uint32 Timer = 0;
            uint32 Mob[4] = { };
            uint32 Item[6] = { };
            uint32 Players = 0;
        };

        struct ReputationRow
        {
            uint32 Faction = 0;
            int32 Standing = 0;
            uint32 Flags = 0;
        };

        struct ActionRow
        {
            uint32 Spec = 0;
            uint32 Button = 0;
            uint32 Action = 0;
            uint32 Type = 0;
        };

        struct SettingRow
        {
            std::string Source;
            std::string Data;
        };

        struct Model
        {
            std::string CharacterId;
            std::string Name;
            uint32 Race = 0;
            uint32 Class = 0;
            uint32 Gender = 0;
            uint32 Skin = 0, Face = 0, HairStyle = 0, HairColor = 0, FacialStyle = 0;
            uint32 CosmeticFlags = 0;
            uint32 Level = 0;
            uint32 Xp = 0;
            uint32 Money = 0;
            uint32 ArenaPoints = 0, TotalHonor = 0, TodayHonor = 0, YesterdayHonor = 0, TotalKills = 0, TodayKills = 0, YesterdayKills = 0;
            uint64 KnownCurrencies = 0;
            uint32 ChosenTitle = 0;
            std::string KnownTitles;
            std::string ExploredZones;
            std::string TaxiMask;
            uint32 BankSlots = 0;
            uint32 StableSlots = 0;
            uint32 WatchedFaction = 0;
            uint32 ActionBars = 0;
            std::vector<std::pair<uint32, uint32>> Spells;
            std::vector<std::pair<uint32, uint32>> Talents;
            std::vector<SkillRow> Skills;
            std::vector<GlyphRow> Glyphs;
            uint32 TalentGroups = 1;
            uint32 ActiveTalentGroup = 0;
            int32 ExtraBonusTalents = 0;
            uint32 ResetTalentsCost = 0;
            std::vector<ItemRow> Items;
            std::vector<QuestRow> Quests;
            std::vector<uint32> Rewarded;
            std::vector<ReputationRow> Reputation;
            std::vector<ActionRow> Actions;
            std::vector<PetRow> Pets;
            std::vector<SettingRow> Settings;
            std::vector<std::pair<uint32, uint32>> Appearances;
            std::vector<std::pair<std::string, std::string>> Outfits;
            bool CanSeeItem = true;
            bool CanSeeSpell = true;
            bool HasMacros = false;
            uint32 MacrosTime = 0;
            std::string Macros;
        };

        struct Header
        {
            std::string JobId;
            uint32 Nonce[4] = { };
            uint32 Account = 0;
            uint32 Revision = 0;
            uint32 MaxCharacters = 10;
            std::string SnapshotSha256;
            bool HasSession = false;
            std::string SessionId;
            std::string CharacterId;
            uint32 Generation = 1;
            bool HasProjection = false;
            bool ProjectionActive = false;
            uint32 ProjectionLevel = 0;
            uint32 ProjectionPolicy = 0;
            std::string ProjectionSignature;
        };

        std::string JsonEscape(std::string_view text)
        {
            std::string out;
            for (unsigned char c : text)
            {
                switch (c)
                {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20)
                            out += Acore::StringFormat("\\u{:04x}", uint32(c));
                        else
                            out.push_back(char(c));
                }
            }
            return out;
        }

        std::string Refused(std::string const& code, std::string const& detail)
        {
            return Acore::StringFormat("{{\"status\":\"refused\",\"problems\":[{{\"code\":\"{}\",\"detail\":\"{}\"}}]}}", code, JsonEscape(detail));
        }

        bool ContentNumber(std::string const& text, std::string_view kind, uint32& out)
        {
            std::size_t const last = text.rfind(':');
            if (last == std::string::npos || last == 0)
                return false;
            std::size_t const middle = text.rfind(':', last - 1);
            if (middle == std::string::npos)
                return false;
            if (text.substr(0, middle) != "coa" || text.substr(middle + 1, last - middle - 1) != kind)
                return false;
            std::string const number = text.substr(last + 1);
            if (number.empty() || number.size() > 10 || !std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; }))
                return false;
            uint64 const value = std::stoull(number);
            if (value > 0xFFFFFFFFull)
                return false;
            out = uint32(value);
            return true;
        }

        std::string Numbers(std::vector<uint32> const& values)
        {
            std::string out;
            for (uint32 v : values)
                out += Acore::StringFormat("{} ", v);
            return out;
        }

        bool CarriedSetting(std::string_view source)
        {
            auto digits = [](std::string_view s, std::size_t& pos)
            {
                std::size_t start = pos;
                while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
                    ++pos;
                return pos > start && pos - start <= 9;
            };
            static std::vector<std::string> const literal = { "core.ascension_active_spec", "core.ascension_starter", "core.ascension_starter_live",
                "core.ascension_reset_credits", "core.ascension_slot.active" };
            for (std::string const& l : literal)
                if (source == l)
                    return true;
            auto after = [&](std::string_view prefix, std::size_t& pos) { if (source.substr(0, prefix.size()) != prefix) return false; pos = prefix.size(); return true; };
            std::size_t pos = 0;
            if (after("core.ascension_build.", pos) || after("core.ascension_bar.", pos))
                return digits(source, pos) && pos == source.size();
            if (after("core.ascension_slot.", pos))
            {
                if (!digits(source, pos))
                    return false;
                if (pos == source.size())
                    return true;
                std::string_view const tail1 = ".build.";
                std::string_view const tail2 = ".bar.";
                if (source.substr(pos, tail1.size()) == tail1)
                    pos += tail1.size();
                else if (source.substr(pos, tail2.size()) == tail2)
                    pos += tail2.size();
                else
                    return false;
                return digits(source, pos) && pos == source.size();
            }
            return false;
        }

        template <typename T>
        bool PairList(Reader& r, Value const* list, std::vector<std::pair<uint32, uint32>>& out, std::size_t maximum, std::string_view what)
        {
            if (!list)
                return false;
            for (Value const& item : list->Items)
            {
                if (item.Kind != CoAPortableJson::Type::Array || item.Items.size() != 2 || item.Items[0].Kind != CoAPortableJson::Type::Integer ||
                    item.Items[1].Kind != CoAPortableJson::Type::Integer || item.Items[0].Negative || item.Items[1].Negative ||
                    item.Items[0].Magnitude > 0xFFFFFFFFull || item.Items[1].Magnitude > 0xFFull)
                {
                    r.Fail(Acore::StringFormat("has a bad entry in \"{}\"", what));
                    return false;
                }
                out.emplace_back(uint32(item.Items[0].Magnitude), uint32(item.Items[1].Magnitude));
            }
            return out.size() <= maximum;
        }

        bool DecodeWardrobe(Value const& wardrobe, Model& m, std::string& error);

        bool DecodeModel(Value const& root, Model& m, std::string& error)
        {
            Reader top(root, "snapshot");
            if (top.U64("format_version", 0xFFFFFFFFull) != CharacterFormat)
                top.Fail(Acore::StringFormat("is not character format {}", CharacterFormat));
            m.CharacterId = top.Str("character_id", 36);
            if (top.Str("ruleset", 16) != "coa" || !top.Valid())
            {
                error = top.Valid() ? "only CoA characters can be imported here" : top.Error();
                return false;
            }
            if (top.Str("content_namespace", 16) != "coa")
            {
                error = "the content namespace is not coa";
                return false;
            }
            if (Value const* identity = top.Object("identity"))
            {
                Reader r(*identity, "identity");
                m.Name = r.Str("name", MaxName * 4);
                std::string const race = r.Str("race", 48);
                std::string const klass = r.Str("class", 48);
                if (!ContentNumber(race, "race", m.Race) || !ContentNumber(klass, "class", m.Class))
                    r.Fail("has a race or class that is not a coa id");
                m.Gender = uint32(r.U64("gender", 2));
                if (Value const* appearance = r.Object("appearance"))
                {
                    Reader a(*appearance, "appearance");
                    m.Skin = uint32(a.U64("skin", 255));
                    m.Face = uint32(a.U64("face", 255));
                    m.HairStyle = uint32(a.U64("hair_style", 255));
                    m.HairColor = uint32(a.U64("hair_color", 255));
                    m.FacialStyle = uint32(a.U64("facial_style", 255));
                    a.Finish();
                    if (!a.Valid())
                        r.Fail(a.Error());
                }
                m.CosmeticFlags = uint32(r.U64("cosmetic_flags", 0x0C00));
                r.Finish();
                if (!r.Valid())
                {
                    error = r.Error();
                    return false;
                }
            }
            if (Value const* progression = top.Object("progression"))
            {
                Reader r(*progression, "progression");
                m.Level = uint32(r.U64("level", 255));
                m.Xp = uint32(r.U64("xp", 0xFFFFFFFFull));
                m.Money = uint32(r.U64("money", MaxMoney));
                if (Value const* honor = r.Object("honor"))
                {
                    Reader h(*honor, "honor");
                    m.ArenaPoints = uint32(h.U64("arena_points", 0xFFFFFFFFull));
                    m.TotalHonor = uint32(h.U64("total_honor", 0xFFFFFFFFull));
                    m.TodayHonor = uint32(h.U64("today_honor", 0xFFFFFFFFull));
                    m.YesterdayHonor = uint32(h.U64("yesterday_honor", 0xFFFFFFFFull));
                    m.TotalKills = uint32(h.U64("total_kills", 0xFFFFFFFFull));
                    m.TodayKills = uint32(h.U64("today_kills", 0xFFFF));
                    m.YesterdayKills = uint32(h.U64("yesterday_kills", 0xFFFF));
                    h.Finish();
                    if (!h.Valid())
                        r.Fail(h.Error());
                }
                m.KnownCurrencies = r.U64("known_currencies", UINT64_MAX);
                m.ChosenTitle = uint32(r.U64("chosen_title", 0xFFFFFFFFull));
                m.KnownTitles = r.Str("known_titles", MaxRaw);
                m.ExploredZones = r.Str("explored_zones", MaxRaw);
                m.TaxiMask = r.Str("taxi_mask", MaxRaw);
                m.BankSlots = uint32(r.U64("bank_slots", 255));
                m.StableSlots = uint32(r.U64("stable_slots", 255));
                m.WatchedFaction = uint32(r.U64("watched_faction", 0xFFFFFFFFull));
                m.ActionBars = uint32(r.U64("action_bars_mask", 255));
                r.Finish();
                if (!r.Valid())
                {
                    error = r.Error();
                    return false;
                }
            }
            if (Value const* build = top.Object("build"))
            {
                Reader r(*build, "build");
                PairList<int>(r, r.Array("spells", 20000), m.Spells, 20000, "spells");
                PairList<int>(r, r.Array("talents", 4096), m.Talents, 4096, "talents");
                if (Value const* skills = r.Array("skills", 512))
                    for (Value const& s : skills->Items)
                    {
                        Reader k(s, "skill");
                        SkillRow row;
                        row.Skill = uint32(k.U64("skill", 0xFFFFFFFFull));
                        row.Value = uint32(k.U64("value", 0xFFFF));
                        row.Max = uint32(k.U64("max", 0xFFFF));
                        k.Finish();
                        if (!k.Valid())
                            r.Fail(k.Error());
                        m.Skills.push_back(row);
                    }
                if (Value const* glyphs = r.Array("glyphs", 8))
                    for (Value const& g : glyphs->Items)
                    {
                        Reader k(g, "glyph set");
                        GlyphRow row;
                        row.Group = uint32(k.U64("talent_group", 7));
                        Value const* list = k.Array("glyphs", 6);
                        if (list && list->Items.size() == 6)
                            for (std::size_t i = 0; i < 6; ++i)
                            {
                                if (list->Items[i].Kind != CoAPortableJson::Type::Integer || list->Items[i].Negative || list->Items[i].Magnitude > 0xFFFF)
                                    k.Fail("has a bad glyph");
                                else
                                    row.Glyph[i] = uint32(list->Items[i].Magnitude);
                            }
                        else
                            k.Fail("needs six glyphs");
                        k.Finish();
                        if (!k.Valid())
                            r.Fail(k.Error());
                        m.Glyphs.push_back(row);
                    }
                m.TalentGroups = uint32(r.U64("talent_groups_count", 2));
                m.ActiveTalentGroup = uint32(r.U64("active_talent_group", 1));
                m.ExtraBonusTalents = int32(r.I64("extra_bonus_talent_count", -1000, 1000));
                m.ResetTalentsCost = uint32(r.U64("reset_talents_cost", 0xFFFFFFFFull));
                r.Finish();
                if (!r.Valid())
                {
                    error = r.Error();
                    return false;
                }
            }
            if (Value const* items = top.Array("items", 1500))
                for (Value const& it : items->Items)
                {
                    Reader r(it, "item");
                    ItemRow row;
                    row.Id = r.Str("id", 36);
                    if (Value const* container = r.Raw("container"); container && container->Kind == CoAPortableJson::Type::String)
                    {
                        row.Container = container->Text;
                        row.HasContainer = true;
                    }
                    row.Slot = uint32(r.U64("slot", 149));
                    std::string const entry = r.Str("entry", 48);
                    if (!ContentNumber(entry, "item", row.Entry))
                        r.Fail("has an entry that is not a coa item");
                    row.Count = uint32(r.U64("count", 0xFFFFFFFFull));
                    row.Duration = int32(r.I64("duration", INT32_MIN, INT32_MAX));
                    if (Value const* charges = r.Array("charges", 5))
                        for (Value const& c : charges->Items)
                        {
                            if (c.Kind != CoAPortableJson::Type::Integer || c.Magnitude > 0x7FFFFFFFull)
                                r.Fail("has a bad charge");
                            else
                                row.Charges.push_back(c.Negative ? -int32(c.Magnitude) : int32(c.Magnitude));
                        }
                    row.Flags = uint32(r.U64("flags", 0xFFFFFFFFull));
                    if (Value const* enchants = r.Array("enchantments", 12))
                        for (Value const& e : enchants->Items)
                        {
                            Reader k(e, "enchantment");
                            Enchant en;
                            en.Slot = uint8(k.U64("slot", 11));
                            en.Id = uint32(k.U64("id", 0xFFFFFFFFull));
                            en.Duration = uint32(k.U64("duration", 0xFFFFFFFFull));
                            en.Charges = uint32(k.U64("charges", 0xFFFFFFFFull));
                            k.Finish();
                            if (!k.Valid())
                                r.Fail(k.Error());
                            row.Enchants.push_back(en);
                        }
                    row.RandomProperty = int32(r.I64("random_property_id", INT32_MIN, INT32_MAX));
                    row.Durability = uint32(r.U64("durability", 0xFFFFFFFFull));
                    row.PlayedTime = uint32(r.U64("played_time", 0xFFFFFFFFull));
                    row.Text = r.OptStr("text", 16 * 1024, row.HasText);
                    bool hasCreator = false;
                    r.OptStr("creator_name", MaxName * 4, hasCreator);
                    if (Value const* gift = r.Raw("gift"); gift && gift->Kind == CoAPortableJson::Type::Object)
                    {
                        Reader k(*gift, "gift");
                        std::string const giftEntry = k.Str("entry", 48);
                        if (!ContentNumber(giftEntry, "item", row.GiftEntry))
                            k.Fail("has an entry that is not a coa item");
                        row.GiftFlags = uint32(k.U64("flags", 0xFFFFFFFFull));
                        k.Finish();
                        if (!k.Valid())
                            r.Fail(k.Error());
                        row.HasGift = true;
                    }
                    r.Finish();
                    if (!r.Valid())
                    {
                        error = r.Error();
                        return false;
                    }
                    m.Items.push_back(std::move(row));
                }
            if (Value const* quests = top.Object("quests"))
            {
                Reader r(*quests, "quests");
                if (Value const* active = r.Array("active", 512))
                    for (Value const& q : active->Items)
                    {
                        Reader k(q, "quest");
                        QuestRow row;
                        row.Quest = uint32(k.U64("quest", 0xFFFFFFFFull));
                        row.Status = uint32(k.U64("status", 255));
                        row.Explored = k.Bool("explored");
                        row.Timer = uint32(k.U64("timer", 0xFFFFFFFFull));
                        Value const* mob = k.Array("mob_counts", 4);
                        Value const* item = k.Array("item_counts", 6);
                        if (mob && mob->Items.size() == 4 && item && item->Items.size() == 6)
                        {
                            for (std::size_t i = 0; i < 4; ++i)
                                row.Mob[i] = uint32(std::min<uint64>(mob->Items[i].Magnitude, 0xFFFF));
                            for (std::size_t i = 0; i < 6; ++i)
                                row.Item[i] = uint32(std::min<uint64>(item->Items[i].Magnitude, 0xFFFF));
                        }
                        else
                            k.Fail("has bad counters");
                        row.Players = uint32(k.U64("player_count", 0xFFFF));
                        k.Finish();
                        if (!k.Valid())
                            r.Fail(k.Error());
                        m.Quests.push_back(row);
                    }
                if (Value const* rewarded = r.Array("rewarded", 50000))
                    for (Value const& q : rewarded->Items)
                    {
                        if (q.Kind != CoAPortableJson::Type::Integer || q.Negative || q.Magnitude > 0xFFFFFFFFull)
                            r.Fail("has a bad rewarded quest");
                        else
                            m.Rewarded.push_back(uint32(q.Magnitude));
                    }
                r.Finish();
                if (!r.Valid())
                {
                    error = r.Error();
                    return false;
                }
            }
            if (Value const* reputation = top.Array("reputation", 2048))
                for (Value const& e : reputation->Items)
                {
                    Reader k(e, "reputation");
                    ReputationRow row;
                    row.Faction = uint32(k.U64("faction", 0xFFFFFFFFull));
                    row.Standing = int32(k.I64("standing", INT32_MIN, INT32_MAX));
                    row.Flags = uint32(k.U64("flags", 0xFFFFFFFFull));
                    k.Finish();
                    if (!k.Valid())
                    {
                        error = k.Error();
                        return false;
                    }
                    m.Reputation.push_back(row);
                }
            if (Value const* actions = top.Array("actions", 1024))
                for (Value const& e : actions->Items)
                {
                    Reader k(e, "action");
                    ActionRow row;
                    row.Spec = uint32(k.U64("spec", 255));
                    row.Button = uint32(k.U64("button", 255));
                    row.Action = uint32(k.U64("action", 0xFFFFFFFFull));
                    row.Type = uint32(k.U64("kind", 255));
                    k.Finish();
                    if (!k.Valid())
                    {
                        error = k.Error();
                        return false;
                    }
                    m.Actions.push_back(row);
                }
            if (Value const* pets = top.Array("pets", 64))
                for (Value const& p : pets->Items)
                {
                    Reader k(p, "pet");
                    PetRow row;
                    k.Str("id", 36);
                    std::string const entry = k.Str("entry", 48);
                    if (!ContentNumber(entry, "creature", row.Entry))
                        k.Fail("has an entry that is not a coa creature");
                    row.Model = uint32(k.U64("model_id", 0xFFFFFFFFull));
                    row.CreatedBy = uint32(k.U64("created_by_spell", 0xFFFFFFFFull));
                    row.Type = uint32(k.U64("pet_type", 255));
                    row.Level = uint32(k.U64("level", 0xFFFF));
                    row.Exp = uint32(k.U64("exp", 0xFFFFFFFFull));
                    row.React = uint32(k.U64("react_state", 255));
                    row.Name = k.Str("name", 21 * 4);
                    row.Renamed = k.Bool("renamed");
                    row.Slot = uint32(k.U64("slot", 255));
                    row.Health = uint32(k.U64("health", 0xFFFFFFFFull));
                    row.Mana = uint32(k.U64("mana", 0xFFFFFFFFull));
                    row.Happiness = uint32(k.U64("happiness", 0xFFFFFFFFull));
                    row.ActionBar = k.Str("action_bar", MaxRaw);
                    if (Value const* spells = k.Array("spells", 512))
                        for (Value const& s : spells->Items)
                        {
                            Reader q(s, "pet spell");
                            uint32 const spell = uint32(q.U64("spell", 0xFFFFFFFFull));
                            uint32 const active = uint32(q.U64("active", 255));
                            q.Finish();
                            if (!q.Valid())
                                k.Fail(q.Error());
                            row.Spells.emplace_back(spell, active);
                        }
                    if (Value const* declined = k.Raw("declined_names"); declined && declined->Kind == CoAPortableJson::Type::Array)
                    {
                        if (declined->Items.size() != 5)
                            k.Fail("needs five declined names");
                        else
                        {
                            for (std::size_t i = 0; i < 5; ++i)
                            {
                                if (declined->Items[i].Kind != CoAPortableJson::Type::String || declined->Items[i].Text.size() > 84)
                                    k.Fail("has a bad declined name");
                                else
                                    row.Declined[i] = declined->Items[i].Text;
                            }
                            row.HasDeclined = true;
                        }
                    }
                    k.Finish();
                    if (!k.Valid())
                    {
                        error = k.Error();
                        return false;
                    }
                    m.Pets.push_back(std::move(row));
                }
            if (Value const* settings = top.Object("settings"))
            {
                if (settings->Members.size() > 4096)
                {
                    error = "too many settings";
                    return false;
                }
                for (auto const& [source, values] : settings->Members)
                {
                    if (source.size() > 128 || values.Kind != CoAPortableJson::Type::Array || values.Items.size() > 4096)
                    {
                        error = "a setting is out of range";
                        return false;
                    }
                    std::vector<uint32> numbers;
                    for (Value const& v : values.Items)
                    {
                        if (v.Kind != CoAPortableJson::Type::Integer || v.Negative || v.Magnitude > 0xFFFFFFFFull)
                        {
                            error = "a setting value is out of range";
                            return false;
                        }
                        numbers.push_back(uint32(v.Magnitude));
                    }
                    m.Settings.push_back({ source, Numbers(numbers) });
                }
            }
            if (root.Find("wardrobe"))
            {
                Value const* wardrobe = top.Object("wardrobe");
                if (wardrobe && !DecodeWardrobe(*wardrobe, m, error))
                    return false;
            }
            if (Value const* blobs = top.Object("client_data"))
            {
                if (blobs->Members.size() > 8)
                {
                    error = "too many client data blobs";
                    return false;
                }
                for (auto const& [key, blob] : blobs->Members)
                {
                    if (key != "5")
                        continue;
                    Reader k(blob, "client data");
                    m.MacrosTime = uint32(k.U64("time", 0xFFFFFFFFull));
                    std::string const data = k.Str("data", 128 * 1024);
                    k.Finish();
                    if (!k.Valid())
                    {
                        error = k.Error();
                        return false;
                    }
                    Optional<std::vector<uint8>> decoded = Acore::Encoding::Base64::Decode(data);
                    if (!decoded || decoded->size() > 64 * 1024)
                    {
                        error = "the macros blob is not valid";
                        return false;
                    }
                    m.Macros.assign(decoded->begin(), decoded->end());
                    m.HasMacros = true;
                }
            }
            top.Raw("extensions");
            top.Finish();
            if (!top.Valid())
            {
                error = top.Error();
                return false;
            }
            return true;
        }

        bool DecodeWardrobe(Value const& wardrobe, Model& m, std::string& error)
        {
            constexpr uint64 MaxCategory = 68;
            constexpr std::size_t MaxOutfits = 100;
            for (auto const& [key, value] : wardrobe.Members)
            {
                if (key == "active")
                {
                    if (value.Kind != CoAPortableJson::Type::Object || value.Members.size() > MaxCategory)
                    {
                        error = "the wardrobe selection is not valid";
                        return false;
                    }
                    for (auto const& [category, appearance] : value.Members)
                    {
                        Optional<uint32> const number = Acore::StringTo<uint32>(category);
                        if (!number || *number < 1 || *number > MaxCategory || category != std::to_string(*number))
                        {
                            error = "a wardrobe category is out of range";
                            return false;
                        }
                        if (appearance.Kind != CoAPortableJson::Type::Integer || appearance.Negative || appearance.Magnitude == 0 || appearance.Magnitude > 0xFFFFFFFFull)
                        {
                            error = "a selected appearance is out of range";
                            return false;
                        }
                        m.Appearances.emplace_back(*number, uint32(appearance.Magnitude));
                    }
                }
                else if (key == "can_see_item" || key == "can_see_spell")
                {
                    if (value.Kind != CoAPortableJson::Type::Bool)
                    {
                        error = "a wardrobe switch is not valid";
                        return false;
                    }
                    (key == "can_see_item" ? m.CanSeeItem : m.CanSeeSpell) = value.Boolean;
                }
                else if (key == "outfits")
                {
                    if (value.Kind != CoAPortableJson::Type::Object || value.Members.size() > MaxOutfits)
                    {
                        error = "the saved outfits are not valid";
                        return false;
                    }
                    for (auto const& [name, ids] : value.Members)
                    {
                        if (name.empty() || name.size() > 64 || std::any_of(name.begin(), name.end(), [](char c) { return uint8(c) < 0x20; })
                            || ids.Kind != CoAPortableJson::Type::Array || ids.Items.size() > MaxCategory + 1)
                        {
                            error = "a saved outfit is not valid";
                            return false;
                        }
                        std::string text;
                        for (Value const& id : ids.Items)
                        {
                            if (id.Kind != CoAPortableJson::Type::Integer || id.Negative || id.Magnitude > 0xFFFFFFFFull)
                            {
                                error = "an outfit appearance is out of range";
                                return false;
                            }
                            text += (text.empty() ? "" : " ") + std::to_string(id.Magnitude);
                        }
                        m.Outfits.emplace_back(name, std::move(text));
                    }
                }
                else
                {
                    error = "the wardrobe has an unknown field";
                    return false;
                }
            }
            return true;
        }

        bool DecodeHeader(Value const& root, Header& h, std::string& error)
        {
            Reader r(root, "job");
            r.U64("job_format", 0xFFFFFFFFull);
            h.JobId = r.Str("job_id", 36);
            Value const* nonce = r.Array("nonce", 4);
            if (nonce && nonce->Items.size() == 4)
                for (std::size_t i = 0; i < 4; ++i)
                {
                    if (nonce->Items[i].Kind != CoAPortableJson::Type::Integer || nonce->Items[i].Negative || nonce->Items[i].Magnitude > 0xFFFFFFFFull)
                        r.Fail("has a bad nonce");
                    else
                        h.Nonce[i] = uint32(nonce->Items[i].Magnitude);
                }
            else
                r.Fail("needs a four-word nonce");
            h.Account = uint32(r.U64("account", 0xFFFFFFFFull));
            h.Revision = uint32(r.U64("revision", 0xFFFFFFFFull));
            h.MaxCharacters = uint32(r.U64("max_characters_per_account", 50));
            h.SnapshotSha256 = r.Str("snapshot_sha256", 64);
            h.CharacterId = r.Str("character_id", 36);
            if (Value const* session = r.Raw("session"); session && session->Kind == CoAPortableJson::Type::Object)
            {
                Reader k(*session, "session");
                h.SessionId = k.Str("session_id", 36);
                h.Generation = uint32(k.U64("generation", 0xFFFFFFFFull));
                k.Finish();
                if (!k.Valid())
                    r.Fail(k.Error());
                h.HasSession = true;
            }
            if (Value const* projection = r.Raw("projection"); projection && projection->Kind == CoAPortableJson::Type::Object)
            {
                Reader k(*projection, "projection");
                h.ProjectionActive = k.Bool("active");
                h.ProjectionLevel = uint32(k.U64("level", 255));
                h.ProjectionPolicy = uint32(k.U64("policy_version", 0xFFFFFFFFull));
                h.ProjectionSignature = k.Str("progression_signature", 64);
                k.Finish();
                if (!k.Valid())
                    r.Fail(k.Error());
                h.HasProjection = true;
            }
            r.Finish();
            if (!r.Valid())
            {
                error = r.Error();
                return false;
            }
            return true;
        }

        CoAPortableProjection::Input ProjectionInput(Model const& m)
        {
            CoAPortableProjection::Input input;
            input.Class = m.Class;
            input.Level = m.Level;
            for (ItemRow const& item : m.Items)
                input.Items.push_back({ item.Id, item.HasContainer, item.Container, item.Slot, item.Entry });
            for (auto const& [spell, mask] : m.Spells)
                input.Spells.push_back(spell);
            for (ActionRow const& action : m.Actions)
                input.Actions.push_back({ action.Spec, action.Button, action.Action, action.Type });
            for (SettingRow const& setting : m.Settings)
            {
                CoAPortableProjection::SettingIn in;
                in.Source = setting.Source;
                for (std::string_view word : Acore::Tokenize(setting.Data, ' ', false))
                    if (Optional<uint32> value = Acore::StringTo<uint32>(word))
                        in.Values.push_back(*value);
                input.Settings.push_back(std::move(in));
            }
            return input;
        }

        std::set<std::string> ExistingTables()
        {
            std::set<std::string> out;
            if (QueryResult result = CharacterDatabase.Query("SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE()"))
                do
                    out.insert(result->Fetch()[0].Get<std::string>());
                while (result->NextRow());
            return out;
        }

        uint64 DatabaseMax(std::set<std::string> const& tables, std::vector<Row> const& list)
        {
            std::string sql = "SELECT GREATEST(0";
            for (Row const& row : list)
                if (tables.count(row.Table))
                    sql += Acore::StringFormat(", IFNULL((SELECT MAX(`{}`) FROM `{}`), 0)", row.Column, row.Table);
            sql += ")";
            if (QueryResult result = CharacterDatabase.Query(sql))
                return result->Fetch()[0].Get<uint64>();
            return 0;
        }

        std::string TemporaryName(std::string const& original, uint32 guid)
        {
            std::string const suffix = Acore::StringFormat("{:X}", guid);
            std::wstring wide;
            if (!Utf8toWStr(original, wide))
                wide = L"Portable";
            std::size_t const keep = NameColumnChars > suffix.size() ? NameColumnChars - suffix.size() : 0;
            if (wide.size() > keep)
                wide.resize(keep);
            std::string out;
            if (!WStrToUtf8(wide, out))
                out = "Portable";
            return out + suffix;
        }

        bool WriteAtomically(std::filesystem::path const& target, std::string const& text)
        {
            std::filesystem::path temp = target;
            temp += ".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                if (!out)
                    return false;
                out << text;
                out.flush();
                if (!out)
                    return false;
            }
            std::error_code ec;
            std::filesystem::rename(temp, target, ec);
            return !ec;
        }

        struct Bind
        {
            explicit Bind(CharacterDatabasePreparedStatement* s) : Stmt(s) { }

            template <typename T>
            Bind& Next(T value)
            {
                Stmt->SetData(Index++, value);
                return *this;
            }

            Bind& Null()
            {
                Stmt->SetData(Index++, nullptr);
                return *this;
            }

            CharacterDatabasePreparedStatement* Stmt;
            uint8 Index = 0;
        };

        std::string Failure(std::string const& jobId, std::string const& code, std::string const& detail)
        {
            LOG_ERROR("coa.portable", "import {} refused: {} {}", jobId, code, detail);
            return Refused(code, detail);
        }
    }

    bool ValidJobId(std::string const& text)
    {
        return CoAPortableSession::ValidSessionId(text);
    }

    std::string Project(std::string const& jobId)
    {
        namespace fs = std::filesystem;
        if (!ValidJobId(jobId))
            return "ERR bad_job_id";
        fs::path const dir = sConfigMgr->GetOption<std::string>("PortableImport.JobDir", "PortableImport");
        fs::path const jobFile = dir / (jobId + ".job");
        fs::path const resultFile = dir / (jobId + ".result");
        auto finish = [&](std::string const& result, std::string const& reply)
        {
            if (!WriteAtomically(resultFile, result))
                return std::string("ERR result_not_written");
            return reply;
        };

        std::error_code ec;
        std::uintmax_t const size = fs::file_size(jobFile, ec);
        if (ec || size == 0 || size > MaxJobBytes)
            return finish(Refused("job_file", "the job file cannot be read"), "ERR job_file");
        std::string text(size, '\0');
        {
            std::ifstream in(jobFile, std::ios::binary);
            if (!in || !in.read(text.data(), std::streamsize(size)))
                return finish(Refused("job_file", "the job file cannot be read"), "ERR job_file");
        }
        std::size_t const split = text.find('\n');
        if (split == std::string::npos)
            return finish(Refused("job_format", "the job file has no snapshot line"), "ERR job_format");
        std::string_view const headerText(text.data(), split);
        std::string_view snapshotText(text.data() + split + 1, text.size() - split - 1);
        while (!snapshotText.empty() && (snapshotText.back() == '\n' || snapshotText.back() == '\r'))
            snapshotText.remove_suffix(1);

        CoAPortableJson::Parsed headerParsed = CoAPortableJson::Parse(headerText);
        if (!headerParsed.Ok)
            return finish(Failure(jobId, "job_header", headerParsed.Error), "ERR job_header");
        std::string error;
        std::string headerSha;
        {
            Reader r(headerParsed.Root, "job");
            if (r.U64("job_format", 0xFFFFFFFFull) != JobFormat)
                r.Fail("is not a job format this realm reads");
            if (r.Str("job_id", 36) != jobId)
                r.Fail("names another job");
            if (r.Str("query", 16) != "project")
                r.Fail("is not a projection query");
            headerSha = r.Str("snapshot_sha256", 64);
            r.Finish();
            if (!r.Valid())
                return finish(Failure(jobId, "job_header", r.Error()), "ERR job_header");
        }
        auto const digestBytes = Acore::Crypto::SHA256::GetDigestOf(snapshotText);
        std::string digest = Acore::Impl::ByteArrayToHexStr(digestBytes.data(), digestBytes.size(), false);
        std::transform(digest.begin(), digest.end(), digest.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (digest != headerSha)
            return finish(Failure(jobId, "hash", "the snapshot does not match its recorded SHA-256"), "ERR hash");
        CoAPortableJson::Parsed snapshot = CoAPortableJson::Parse(snapshotText);
        Model model;
        if (!snapshot.Ok || !DecodeModel(snapshot.Root, model, error))
            return finish(Failure(jobId, "snapshot", snapshot.Ok ? error : snapshot.Error), "ERR snapshot");

        uint32 const cap = CoAPortableProjection::MaxLevel();
        if (model.Level <= cap)
            return finish(Acore::StringFormat("{{\"status\":\"ok\",\"projection\":{{\"active\":false,\"protocol\":{},\"policy_version\":{},\"progression_signature\":\"{}\",\"max_player_level\":{},\"canonical_level\":{},\"projected_level\":{}}}}}",
                CoAPortableProjection::Protocol, CoAPortableProjection::PolicyVersion, CoAPortableProjection::Signature(), cap, model.Level, model.Level), Acore::StringFormat("OK {}", jobId));
        CoAPortableProjection::Result const result = CoAPortableProjection::Project(ProjectionInput(model), cap);
        if (!result.Ok)
            return finish(Failure(jobId, "projection", result.Error), "ERR projection");
        LOG_INFO("coa.portable", "projection {}: level {} -> {} holds {} items, {} spells, {} actions, {} settings blocked", jobId, model.Level, cap,
            result.HeldItems.size(), result.HeldSpells.size(), result.HeldActions.size(), result.BlockedSettings.size());
        return finish(CoAPortableProjection::ManifestJson(result), Acore::StringFormat("OK {}", jobId));
    }

    std::string Run(std::string const& jobId)
    {
        namespace fs = std::filesystem;
        if (!ValidJobId(jobId))
            return "ERR bad_job_id";
        fs::path const dir = sConfigMgr->GetOption<std::string>("PortableImport.JobDir", "PortableImport");
        fs::path const jobFile = dir / (jobId + ".job");
        fs::path const resultFile = dir / (jobId + ".result");
        auto finish = [&](std::string const& result, std::string const& reply)
        {
            if (!WriteAtomically(resultFile, result))
                return std::string("ERR result_not_written");
            return reply;
        };

        std::error_code ec;
        std::uintmax_t const size = fs::file_size(jobFile, ec);
        if (ec || size == 0 || size > MaxJobBytes)
            return finish(Refused("job_file", ec ? "the job file cannot be read" : "the job file has an unreasonable size"), "ERR job_file");
        std::string text(size, '\0');
        {
            std::ifstream in(jobFile, std::ios::binary);
            if (!in || !in.read(text.data(), std::streamsize(size)))
                return finish(Refused("job_file", "the job file cannot be read"), "ERR job_file");
        }
        std::size_t const split = text.find('\n');
        if (split == std::string::npos)
            return finish(Refused("job_format", "the job file has no snapshot line"), "ERR job_format");
        std::string_view const headerText(text.data(), split);
        std::string_view snapshotText(text.data() + split + 1, text.size() - split - 1);
        while (!snapshotText.empty() && (snapshotText.back() == '\n' || snapshotText.back() == '\r'))
            snapshotText.remove_suffix(1);

        CoAPortableJson::Parsed headerParsed = CoAPortableJson::Parse(headerText);
        Header header;
        std::string error;
        if (headerParsed.Ok)
        {
            Value const* declared = headerParsed.Root.Find("job_format");
            if (!declared || declared->Kind != CoAPortableJson::Type::Integer || declared->Negative || declared->Magnitude != JobFormat)
            {
                LOG_ERROR("coa.portable", "import {} refused: unsupported job format", jobId);
                return finish(Acore::StringFormat("{{\"status\":\"refused\",\"supported_job_formats\":[{}],\"problems\":[{{\"code\":\"unsupported_job_format\",\"detail\":\"this realm reads job format {} only\"}}]}}", JobFormat, JobFormat), "ERR unsupported_job_format");
            }
        }
        if (!headerParsed.Ok || !DecodeHeader(headerParsed.Root, header, error))
            return finish(Failure(jobId, "job_header", headerParsed.Ok ? error : headerParsed.Error), "ERR job_header");
        if (header.JobId != jobId)
            return finish(Failure(jobId, "job_header", "the job id inside the file is not the one asked for"), "ERR job_header");
        auto const digestBytes = Acore::Crypto::SHA256::GetDigestOf(snapshotText);
        std::string digest = Acore::Impl::ByteArrayToHexStr(digestBytes.data(), digestBytes.size(), false);
        std::transform(digest.begin(), digest.end(), digest.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (digest != header.SnapshotSha256)
            return finish(Failure(jobId, "hash", "the snapshot does not match its recorded SHA-256"), "ERR hash");

        std::string const marker = Acore::StringFormat("{} {} {} {} {} ", header.Nonce[0], header.Nonce[1], header.Nonce[2], header.Nonce[3], header.Revision);
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PORTABLE_IMPORT_MARKER);
            stmt->SetData(0, marker);
            if (PreparedQueryResult done = CharacterDatabase.Query(stmt))
            {
                uint32 const guid = done->Fetch()[0].Get<uint32>();
                std::string name;
                sCharacterCache->GetCharacterNameByGuid(ObjectGuid::Create<HighGuid::Player>(guid), name);
                return finish(Acore::StringFormat("{{\"status\":\"ok\",\"local_guid\":{},\"repeated\":true,\"final_name\":\"{}\"}}", guid, JsonEscape(name)), Acore::StringFormat("OK {}", jobId));
            }
        }

        CoAPortableJson::Parsed snapshot = CoAPortableJson::Parse(snapshotText);
        Model model;
        if (!snapshot.Ok || !DecodeModel(snapshot.Root, model, error))
            return finish(Failure(jobId, "snapshot", snapshot.Ok ? error : snapshot.Error), "ERR snapshot");
        if (header.CharacterId != model.CharacterId)
            return finish(Failure(jobId, "snapshot", "the header names another character than the snapshot"), "ERR snapshot");
        if (header.HasSession && !ValidJobId(header.SessionId))
            return finish(Failure(jobId, "job_header", "the session id is not a UUIDv7"), "ERR job_header");
        if (header.HasProjection)
        {
            if (header.ProjectionSignature != CoAPortableProjection::Signature() || header.ProjectionPolicy != CoAPortableProjection::PolicyVersion ||
                header.ProjectionLevel != CoAPortableProjection::MaxLevel())
                return finish(Failure(jobId, "progression_changed", "this realm has another progression profile than the one the character was projected for"), "ERR progression_changed");
            if (header.ProjectionActive)
            {
                if (model.Level != header.ProjectionLevel)
                    return finish(Failure(jobId, "projection_level", "a projected character must be at the level cap of the realm"), "ERR projection_level");
                CoAPortableProjection::Result const check = CoAPortableProjection::Project(ProjectionInput(model), header.ProjectionLevel);
                if (!check.Ok)
                    return finish(Failure(jobId, "projection", check.Error), "ERR projection");
                if (!check.HoldsNothing())
                    return finish(Failure(jobId, "projection_inconsistent", "the projected character still holds state above the level cap of the realm"), "ERR projection_inconsistent");
            }
            else if (model.Level > CoAPortableProjection::MaxLevel())
                return finish(Failure(jobId, "above_level_cap", Acore::StringFormat("the character is level {} and the cap of this realm is {}: project it first", model.Level, CoAPortableProjection::MaxLevel())), "ERR above_level_cap");
        }
        else if (model.Level > CoAPortableProjection::MaxLevel())
            return finish(Failure(jobId, "above_level_cap", Acore::StringFormat("the character is level {} and the cap of this realm is {}: project it first", model.Level, CoAPortableProjection::MaxLevel())), "ERR above_level_cap");

        std::vector<std::string> problems;
        std::string accountName;
        if (!AccountMgr::GetName(header.Account, accountName))
            problems.push_back(Acore::StringFormat("game account {} does not exist", header.Account));
        else
        {
            std::string upper = accountName;
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            if (upper.rfind("COABOT", 0) == 0 || upper == "COAMANAGER")
                problems.push_back(Acore::StringFormat("account {} belongs to a bot or the Manager", header.Account));
            if (AccountMgr::GetCharactersCount(header.Account) >= header.MaxCharacters)
                problems.push_back(Acore::StringFormat("account {} already has {} characters", header.Account, header.MaxCharacters));
        }
        PlayerInfo const* info = sObjectMgr->GetPlayerInfo(model.Race, model.Class);
        if (!info)
            problems.push_back(Acore::StringFormat("this realm has no start position for race {} / class {}", model.Race, model.Class));
        std::set<uint32> missingItems;
        for (ItemRow const& item : model.Items)
        {
            if (!sObjectMgr->GetItemTemplate(item.Entry))
                missingItems.insert(item.Entry);
            if (item.HasGift && !sObjectMgr->GetItemTemplate(item.GiftEntry))
                missingItems.insert(item.GiftEntry);
        }
        if (!missingItems.empty())
        {
            std::string list;
            for (uint32 entry : missingItems)
                list += Acore::StringFormat("{} ", entry);
            problems.push_back("the realm does not know these item entries: " + list);
        }
        for (PetRow const& pet : model.Pets)
            if (!sObjectMgr->GetCreatureTemplate(pet.Entry))
                problems.push_back(Acore::StringFormat("the realm does not know the pet creature {}", pet.Entry));
        std::map<std::string, std::size_t> containers;
        for (std::size_t i = 0; i < model.Items.size(); ++i)
            containers[model.Items[i].Id] = i;
        for (ItemRow const& item : model.Items)
            if (item.HasContainer && !containers.count(item.Container))
                problems.push_back("an item names a container that is not part of the character");
        if (!problems.empty())
        {
            std::string joined;
            for (std::string const& p : problems)
                joined += p + "; ";
            return finish(Failure(jobId, "preflight", joined), "ERR preflight");
        }

        std::set<std::string> const tables = ExistingTables();
        if (header.HasSession && !tables.count("coa_portable_session"))
            return finish(Failure(jobId, "preflight", "this realm has no coa_portable_session table"), "ERR preflight");

        uint32 const guid = uint32(std::max<uint64>(sObjectMgr->GetGenerator<HighGuid::Player>().GetNextAfterMaxUsed(), DatabaseMax(tables, { { "characters", "guid" } }) + 1));
        uint64 const itemBase = std::max<uint64>(sObjectMgr->GetGenerator<HighGuid::Item>().GetNextAfterMaxUsed(), DatabaseMax(tables, ItemKeyed) + 1);
        uint64 const petBase = std::max<uint64>(CoAPortableImportAccess::HighestPet() + 1, DatabaseMax(tables, PetKeyed) + 1);
        if (guid >= 4294967000u || itemBase + model.Items.size() >= 4294967000ull || petBase + model.Pets.size() >= 4294967000ull)
            return finish(Failure(jobId, "allocation", "an id space is exhausted"), "ERR allocation");

        std::string name = model.Name;
        bool rename = !normalizePlayerName(name) || ObjectMgr::CheckPlayerName(name, true) != CHAR_NAME_SUCCESS || sObjectMgr->IsReservedName(name);
        if (!rename)
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
            stmt->SetData(0, name);
            rename = bool(CharacterDatabase.Query(stmt));
        }
        std::string const finalName = rename ? TemporaryName(model.Name, guid) : name;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        for (Row const& row : CharacterKeyed)
            if (tables.count(row.Table))
                trans->Append(Acore::StringFormat("DELETE FROM `{}` WHERE `{}` = {}", row.Table, row.Column, guid).c_str());

        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_CHARACTER));
            b.Next(guid).Next(header.Account).Next(finalName).Next(uint8(model.Race)).Next(uint8(model.Class)).Next(uint8(model.Gender))
                .Next(uint8(model.Level)).Next(model.Xp).Next(model.Money).Next(uint8(model.Skin)).Next(uint8(model.Face))
                .Next(uint8(model.HairStyle)).Next(uint8(model.HairColor)).Next(uint8(model.FacialStyle)).Next(uint8(model.BankSlots))
                .Next(uint8(0)).Next(model.CosmeticFlags).Next(info->positionX).Next(info->positionY).Next(info->positionZ)
                .Next(info->mapId).Next(uint32(0)).Next(uint8(0)).Next(info->orientation).Next(model.TaxiMask).Next(uint8(0))
                .Next(uint8(1)).Next(uint32(0)).Next(uint32(0)).Next(uint32(0)).Next(uint8(0)).Next(float(0)).Next(model.ResetTalentsCost)
                .Next(uint32(0)).Next(uint32(0)).Next(uint8(model.StableSlots)).Next(uint16(rename ? AT_LOGIN_RENAME : 0)).Next(info->areaId)
                .Next(uint32(0)).Next(model.ArenaPoints).Next(model.TotalHonor).Next(model.TodayHonor).Next(model.YesterdayHonor)
                .Next(model.TotalKills).Next(uint16(model.TodayKills)).Next(uint16(model.YesterdayKills)).Next(model.ChosenTitle)
                .Next(model.KnownCurrencies).Next(model.WatchedFaction).Next(uint8(0)).Next(uint32(2000000000)).Next(uint32(2000000000))
                .Next(uint32(0)).Next(uint32(0)).Next(uint32(2000000000)).Next(uint32(0)).Next(uint32(0)).Next(uint32(0)).Next(uint32(0))
                .Next(uint8(model.TalentGroups)).Next(uint8(model.ActiveTalentGroup)).Next(model.ExploredZones).Next(std::string()).Next(uint32(0))
                .Next(model.KnownTitles).Next(uint8(model.ActionBars)).Next(uint8(0)).Next(uint32(0)).Next(model.ExtraBonusTalents);
            trans->Append(b.Stmt);
        }
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_HOMEBIND));
            b.Next(guid).Next(info->mapId).Next(info->areaId).Next(info->positionX).Next(info->positionY).Next(info->positionZ);
            trans->Append(b.Stmt);
        }
        for (std::size_t i = 0; i < model.Items.size(); ++i)
        {
            ItemRow const& item = model.Items[i];
            std::string enchants;
            {
                uint32 slots[12][3] = { };
                for (Enchant const& e : item.Enchants)
                {
                    slots[e.Slot][0] = e.Id;
                    slots[e.Slot][1] = e.Duration;
                    slots[e.Slot][2] = e.Charges;
                }
                for (auto& slot : slots)
                    enchants += Acore::StringFormat("{} {} {} ", slot[0], slot[1], slot[2]);
            }
            uint32 const itemGuid = uint32(itemBase + i);
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_ITEM));
            b.Next(itemGuid).Next(item.Entry).Next(guid).Next(uint32(0)).Next(uint32(0)).Next(item.Count).Next(item.Duration);
            if (item.Charges.empty())
                b.Null();
            else
            {
                std::string charges;
                for (std::size_t c = 0; c < 5; ++c)
                    charges += Acore::StringFormat("{} ", c < item.Charges.size() ? item.Charges[c] : 0);
                b.Next(charges);
            }
            b.Next(item.Flags).Next(enchants).Next(item.RandomProperty).Next(uint16(item.Durability)).Next(item.PlayedTime);
            if (item.HasText)
                b.Next(item.Text);
            else
                b.Null();
            trans->Append(b.Stmt);
            uint32 bag = 0;
            if (item.HasContainer)
                bag = uint32(itemBase + containers[item.Container]);
            Bind inv(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_INVENTORY));
            inv.Next(guid).Next(bag).Next(uint8(item.Slot)).Next(itemGuid);
            trans->Append(inv.Stmt);
            if (item.HasGift)
            {
                Bind gift(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_GIFT));
                gift.Next(guid).Next(itemGuid).Next(item.GiftEntry).Next(item.GiftFlags);
                trans->Append(gift.Stmt);
            }
        }
        for (auto const& [spell, mask] : model.Spells)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_SPELL));
            b.Next(guid).Next(spell).Next(uint8(mask));
            trans->Append(b.Stmt);
        }
        std::size_t talentsWritten = 0;
        std::vector<std::string> notApplied;
        for (auto const& [spell, mask] : model.Talents)
        {
            if (!GetTalentSpellPos(spell))
            {
                notApplied.push_back(Acore::StringFormat("talent {}", spell));
                continue;
            }
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_TALENT));
            b.Next(guid).Next(spell).Next(uint8(mask));
            trans->Append(b.Stmt);
            ++talentsWritten;
        }
        for (SkillRow const& s : model.Skills)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_SKILL));
            b.Next(guid).Next(s.Skill).Next(uint16(s.Value)).Next(uint16(s.Max));
            trans->Append(b.Stmt);
        }
        for (GlyphRow const& g : model.Glyphs)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_GLYPH));
            b.Next(guid).Next(uint8(g.Group));
            for (uint32 glyph : g.Glyph)
                b.Next(uint16(glyph));
            trans->Append(b.Stmt);
        }
        for (ReputationRow const& r : model.Reputation)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_REPUTATION));
            b.Next(guid).Next(uint16(r.Faction)).Next(r.Standing).Next(uint16(r.Flags));
            trans->Append(b.Stmt);
        }
        for (QuestRow const& q : model.Quests)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_QUEST));
            b.Next(guid).Next(q.Quest).Next(uint8(q.Status)).Next(uint8(q.Explored ? 1 : 0)).Next(q.Timer);
            for (uint32 m : q.Mob)
                b.Next(uint16(m));
            for (uint32 i : q.Item)
                b.Next(uint16(i));
            b.Next(uint16(q.Players));
            trans->Append(b.Stmt);
        }
        for (uint32 quest : model.Rewarded)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_QUEST_REWARDED));
            b.Next(guid).Next(quest).Next(uint8(1));
            trans->Append(b.Stmt);
        }
        for (ActionRow const& a : model.Actions)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_ACTION));
            b.Next(guid).Next(uint8(a.Spec)).Next(uint8(a.Button)).Next(a.Action).Next(uint8(a.Type));
            trans->Append(b.Stmt);
        }
        for (std::size_t i = 0; i < model.Pets.size(); ++i)
        {
            PetRow const& pet = model.Pets[i];
            uint32 const number = uint32(petBase + i);
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_PET));
            b.Next(number).Next(pet.Entry).Next(guid).Next(pet.Model).Next(pet.CreatedBy).Next(uint8(pet.Type)).Next(uint16(pet.Level))
                .Next(pet.Exp).Next(uint8(pet.React)).Next(pet.Name).Next(uint8(pet.Renamed ? 1 : 0)).Next(uint16(pet.Slot)).Next(pet.Health)
                .Next(pet.Mana).Next(pet.Happiness).Next(uint32(GameTime::GetGameTime().count()));
            if (pet.ActionBar.empty())
                b.Null();
            else
                b.Next(pet.ActionBar);
            trans->Append(b.Stmt);
            for (auto const& [spell, active] : pet.Spells)
            {
                Bind s(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_PET_SPELL));
                s.Next(number).Next(spell).Next(uint8(active));
                trans->Append(s.Stmt);
            }
            if (pet.HasDeclined)
            {
                Bind d(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_PET_DECLINED));
                d.Next(number).Next(guid);
                for (std::string const& form : pet.Declined)
                    d.Next(form);
                trans->Append(d.Stmt);
            }
        }
        std::size_t settingsWritten = 0;
        for (SettingRow const& s : model.Settings)
        {
            if (!CarriedSetting(s.Source))
            {
                notApplied.push_back(s.Source);
                continue;
            }
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_SETTING));
            b.Next(guid).Next(s.Source).Next(s.Data);
            trans->Append(b.Stmt);
            ++settingsWritten;
        }
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_SETTING));
            b.Next(guid).Next(std::string("coa.portable.import")).Next(marker);
            trans->Append(b.Stmt);
        }
        if (header.HasProjection)
        {
            std::string pin;
            for (uint32 word : CoAPortableProjection::PinWords())
                pin += Acore::StringFormat("{} ", word);
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_SETTING));
            b.Next(guid).Next(std::string("coa.portable.pin")).Next(pin);
            trans->Append(b.Stmt);
        }
        if (model.HasMacros)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_ACCOUNT_DATA));
            b.Next(guid).Next(uint8(5)).Next(model.MacrosTime).Next(model.Macros);
            trans->Append(b.Stmt);
        }
        for (auto const& [category, appearance] : model.Appearances)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_APPEARANCE));
            b.Next(guid).Next(uint8(category)).Next(appearance);
            trans->Append(b.Stmt);
        }
        if (!model.CanSeeItem || !model.CanSeeSpell)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_APPEARANCE_SETTINGS));
            b.Next(guid).Next(uint8(model.CanSeeItem ? 1 : 0)).Next(uint8(model.CanSeeSpell ? 1 : 0));
            trans->Append(b.Stmt);
        }
        for (auto const& [name, appearances] : model.Outfits)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_APPEARANCE_OUTFIT));
            b.Next(guid).Next(name).Next(appearances);
            trans->Append(b.Stmt);
        }
        if (header.HasSession)
        {
            Bind b(CharacterDatabase.GetPreparedStatement(CHAR_INS_PORTABLE_SESSION));
            b.Next(guid).Next(header.SessionId).Next(header.CharacterId).Next(header.Revision).Next(header.Generation).Next(uint8(0))
                .Next(uint32(0)).Next(uint32(0)).Next(uint32(GameTime::GetGameTime().count()));
            trans->Append(b.Stmt);
        }

        CharacterDatabase.DirectCommitTransaction(trans);
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PORTABLE_IMPORT_MARKER);
            stmt->SetData(0, marker);
            PreparedQueryResult committed = CharacterDatabase.Query(stmt);
            if (!committed || committed->Fetch()[0].Get<uint32>() != guid)
                return finish(Failure(jobId, "commit", "the realm did not commit the import"), "ERR commit");
        }

        sCharacterCache->AddCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(guid), header.Account, finalName, uint8(model.Gender), uint8(model.Race), uint8(model.Class), uint8(model.Level));
        sObjectMgr->GetGenerator<HighGuid::Item>().Set(itemBase + model.Items.size());
        CoAPortableImportAccess::SetHighestPet(uint32(petBase + model.Pets.size() - 1));
        sObjectMgr->GetGenerator<HighGuid::Player>().Set(uint64(guid) + 1);
        sWorld->UpdateRealmCharCount(header.Account);

        std::string notAppliedJson;
        for (std::string const& n : notApplied)
            notAppliedJson += Acore::StringFormat("{}\"{}\"", notAppliedJson.empty() ? "" : ",", JsonEscape(n));
        LOG_INFO("coa.portable", "import {} created character {} ({}) with {} items and {} pets", jobId, guid, finalName, model.Items.size(), model.Pets.size());
        return finish(
            Acore::StringFormat("{{\"status\":\"ok\",\"local_guid\":{},\"item_base\":{},\"pet_base\":{},\"renamed\":{},\"final_name\":\"{}\",\"items\":{},\"pets\":{},\"talents\":{},\"settings\":{},\"wardrobe\":{},\"not_applied\":[{}]}}",
                guid, itemBase, petBase, rename ? "true" : "false", JsonEscape(finalName), model.Items.size(), model.Pets.size(), talentsWritten, settingsWritten,
                model.Appearances.size() + model.Outfits.size() + (model.CanSeeItem && model.CanSeeSpell ? 0 : 1), notAppliedJson),
            Acore::StringFormat("OK {}", jobId));
    }
}
