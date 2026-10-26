#include "CoAPortableProjection.h"
#include "AscensionCoATalentData.h"
#include "AscensionCoATalentState.h"
#include "AscensionCustomClassData.h"
#include "AscensionFelswornRifts.h"
#include "AscensionLiveBaselineData.h"
#include "AscensionSpellProgressionData.h"
#include "AscensionTalentReplacementData.h"
#include "AscensionTaughtAbilityData.h"
#include "Config.h"
#include "CryptoHash.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Util.h"
#include "World.h"
#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace CoAPortableProjection
{
    namespace
    {
        enum class Kind : uint8
        {
            Table,
            Entry,
            Dependent
        };

        struct SpellSource
        {
            Kind Type = Kind::Table;
            uint32 Level = 0;
            uint32 EntryId = 0;
            uint32 Parent = 0;
        };

        struct Index
        {
            std::unordered_map<uint64, std::vector<SpellSource>> Sources;
            std::unordered_set<uint64> Exempt;
            std::unordered_map<uint32, AscensionCompatData::CoATalentEntry const*> Entries;
            std::unordered_set<uint32> SelectableFree;
            std::unordered_map<uint32, std::array<uint32, 2>> Dependencies;
        };

        uint64 Key(uint32 classId, uint32 spell)
        {
            return (uint64(classId) << 32) | spell;
        }

        Index const& Data()
        {
            static Index const index = []
            {
                Index out;
                for (auto const& s : AscensionCompatData::ClassSpells)
                    out.Sources[Key(s.ClassId, s.SpellId)].push_back({ Kind::Table, s.RequiredLevel, 0, 0 });
                for (auto const& s : AscensionCompatData::LegacyGeneratedClassSpells)
                    out.Sources[Key(s.ClassId, s.SpellId)].push_back({ Kind::Table, s.RequiredLevel, 0, 0 });
                for (auto const& r : AscensionProgression::Ranks)
                    out.Sources[Key(r.ClassId, r.SpellId)].push_back({ Kind::Table, r.RequiredLevel, 0, 0 });
                for (auto const& r : AscensionCompatData::FelswornHordeCapitalRifts)
                    out.Sources[Key(CLASS_DEMON_HUNTER, r.SpellId)].push_back({ Kind::Table, r.RequiredLevel, 0, 0 });
                for (auto const& t : AscensionCompatData::TaughtAbilities)
                    out.Sources[Key(t.ClassId, t.SpellId)].push_back({ Kind::Dependent, t.RequiredLevel, 0, t.ParentSpellId });
                for (auto const& t : AscensionCompatData::TalentReplacements)
                    for (auto const& rank : t.Ranks)
                        if (rank.SpellId)
                            out.Sources[Key(t.ClassId, rank.SpellId)].push_back({ Kind::Dependent, rank.RequiredLevel, 0, t.ParentSpellId });
                for (auto const& e : AscensionCompatData::CoATalentEntries)
                {
                    out.Entries[e.EntryId] = &e;
                    for (uint32 i = 0; i < e.SpellCount && i < e.SpellIds.size(); ++i)
                        if (e.SpellIds[i])
                            out.Sources[Key(e.ClassId, e.SpellIds[i])].push_back({ Kind::Entry, e.RequiredLevel, e.EntryId, 0 });
                }
                for (auto const& s : AscensionLiveBaseline::Spells)
                    out.Exempt.insert(Key(s.ClassId, s.SpellId));
                for (auto const& p : AscensionLiveBaseline::Proficiencies)
                    out.Exempt.insert(Key(p.ClassId, p.SpellId));
                for (auto const& s : AscensionCompatData::UnresolvedTrainerSpells)
                    out.Exempt.insert(Key(s.ClassId, s.SpellId));
                for (auto const& f : AscensionCompatData::CoASelectableFreeEntries)
                    out.SelectableFree.insert(f.EntryId);
                for (auto const& d : AscensionCompatData::CoAAutomaticDependencies)
                    out.Dependencies[d.EntryId] = d.RequiredEntryIds;
                return out;
            }();
            return index;
        }

        struct Engine
        {
            uint32 Class;
            uint32 Level;
            Index const& Idx = Data();
            std::string Failure;

            bool Automatic(AscensionCompatData::CoATalentEntry const& entry) const
            {
                return !entry.AECost && !entry.TECost && !Idx.SelectableFree.count(entry.EntryId);
            }

            std::set<uint32> HeldEntries(std::vector<AscensionCoATalentState::KnownEntry> const& known)
            {
                std::set<uint32> held;
                std::vector<AscensionCoATalentState::KnownEntry> kept;
                std::vector<AscensionCompatData::CoATalentEntry const*> paid;
                for (auto const& item : known)
                {
                    auto found = Idx.Entries.find(item.EntryId);
                    if (found == Idx.Entries.end() || found->second->ClassId != Class)
                        continue;
                    AscensionCompatData::CoATalentEntry const& entry = *found->second;
                    if (entry.RequiredLevel > Level)
                        held.insert(entry.EntryId);
                    else if (entry.AECost || entry.TECost)
                        paid.push_back(&entry);
                }
                uint32 ae = 0, te = 0;
                if (!paid.empty() && !AscensionCompatData::GetCoATalentBudget(uint8(Class), uint8(Level), ae, te))
                {
                    Failure = Acore::StringFormat("this realm has no talent budget for class {} at level {}", Class, Level);
                    return held;
                }
                std::sort(paid.begin(), paid.end(), [](auto const* a, auto const* b)
                {
                    return std::tie(a->RequiredLevel, a->EntryId) < std::tie(b->RequiredLevel, b->EntryId);
                });
                std::map<uint32, uint32> rankOf;
                for (auto const& item : known)
                    rankOf[item.EntryId] = item.Rank;
                for (auto const* entry : paid)
                {
                    kept.push_back({ entry->EntryId, rankOf[entry->EntryId] });
                    auto const spent = AscensionCoATalentState::Spent(kept);
                    if (spent.AE > ae || spent.TE > te)
                    {
                        kept.pop_back();
                        held.insert(entry->EntryId);
                    }
                }
                bool changed = true;
                while (changed)
                {
                    changed = false;
                    for (auto const& item : known)
                    {
                        auto found = Idx.Entries.find(item.EntryId);
                        if (found == Idx.Entries.end() || found->second->ClassId != Class || held.count(item.EntryId) || !Automatic(*found->second))
                            continue;
                        auto dependency = Idx.Dependencies.find(item.EntryId);
                        if (dependency == Idx.Dependencies.end())
                            continue;
                        for (uint32 required : dependency->second)
                            if (required && held.count(required))
                            {
                                held.insert(item.EntryId);
                                changed = true;
                                break;
                            }
                    }
                }
                return held;
            }

            bool Exempt(uint32 spell) const
            {
                return Idx.Exempt.count(Key(Class, spell)) != 0;
            }

            bool Allowed(uint32 spell, std::set<uint32> const& heldEntries, std::set<uint32> const& heldSpells) const
            {
                auto found = Idx.Sources.find(Key(Class, spell));
                if (found == Idx.Sources.end() || Exempt(spell))
                    return true;
                for (SpellSource const& source : found->second)
                {
                    if (source.Level > Level)
                        continue;
                    if (source.Type == Kind::Entry && heldEntries.count(source.EntryId))
                        continue;
                    if (source.Type == Kind::Dependent && heldSpells.count(source.Parent))
                        continue;
                    return true;
                }
                return false;
            }

            std::set<uint32> HeldSpells(std::vector<uint32> const& spells, std::set<uint32> const& heldEntries)
            {
                std::set<uint32> held;
                bool changed = true;
                while (changed)
                {
                    changed = false;
                    for (uint32 spell : spells)
                        if (!held.count(spell) && !Allowed(spell, heldEntries, held))
                        {
                            held.insert(spell);
                            changed = true;
                        }
                }
                return held;
            }

            bool SpellHeldAnywhere(uint32 spell, std::set<uint32> const& heldSpells, std::set<uint32> const& heldEntries) const
            {
                return heldSpells.count(spell) || !Allowed(spell, heldEntries, heldSpells);
            }
        };

        bool Digits(std::string_view text, std::size_t& pos, uint32& out)
        {
            std::size_t start = pos;
            uint64 value = 0;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' && pos - start < 9)
                value = value * 10 + uint64(text[pos++] - '0');
            if (pos == start)
                return false;
            out = uint32(value);
            return true;
        }

        enum class SettingKind : uint8
        {
            None,
            Slot,
            Build,
            Bar
        };

        SettingKind Classify(std::string_view source)
        {
            auto after = [&](std::string_view prefix, std::size_t& pos)
            {
                if (source.substr(0, prefix.size()) != prefix)
                    return false;
                pos = prefix.size();
                return true;
            };
            std::size_t pos = 0;
            uint32 ignored = 0;
            if (after("core.ascension_build.", pos))
                return Digits(source, pos, ignored) && pos == source.size() ? SettingKind::Build : SettingKind::None;
            if (after("core.ascension_bar.", pos))
                return Digits(source, pos, ignored) && pos == source.size() ? SettingKind::Bar : SettingKind::None;
            if (after("core.ascension_slot.", pos))
            {
                if (!Digits(source, pos, ignored))
                    return SettingKind::None;
                if (pos == source.size())
                    return SettingKind::Slot;
                std::string_view const build = ".build.";
                std::string_view const bar = ".bar.";
                SettingKind kind = SettingKind::None;
                if (source.substr(pos, build.size()) == build)
                {
                    pos += build.size();
                    kind = SettingKind::Build;
                }
                else if (source.substr(pos, bar.size()) == bar)
                {
                    pos += bar.size();
                    kind = SettingKind::Bar;
                }
                return kind != SettingKind::None && Digits(source, pos, ignored) && pos == source.size() ? kind : SettingKind::None;
            }
            return SettingKind::None;
        }

        bool Zeros(std::vector<uint32> const& values, std::size_t from)
        {
            return std::all_of(values.begin() + std::min(from, values.size()), values.end(), [](uint32 v) { return v == 0; });
        }

        template <typename T>
        void SortUnique(std::vector<T>& list)
        {
            std::sort(list.begin(), list.end());
            list.erase(std::unique(list.begin(), list.end()), list.end());
        }

        std::string Quote(std::string const& text)
        {
            std::string out = "\"";
            for (char c : text)
            {
                if (c == '"' || c == '\\')
                    out += '\\';
                if (static_cast<unsigned char>(c) < 0x20)
                    continue;
                out += c;
            }
            return out + "\"";
        }

        template <typename T>
        std::string Numbers(std::vector<T> const& values)
        {
            std::string out;
            for (std::size_t i = 0; i < values.size(); ++i)
                out += Acore::StringFormat("{}{}", i ? "," : "", values[i]);
            return out;
        }

        void Feed(Acore::Crypto::SHA256& hash, uint64 value)
        {
            uint8 bytes[8];
            for (int i = 0; i < 8; ++i)
                bytes[i] = uint8(value >> (8 * i));
            hash.UpdateData(bytes, sizeof(bytes));
        }

        void Feed(Acore::Crypto::SHA256& hash, std::string const& text)
        {
            Feed(hash, uint64(text.size()));
            hash.UpdateData(text);
        }

        std::string ComputeSignature()
        {
            Acore::Crypto::SHA256 hash;
            Feed(hash, std::string("coa-progression-signature"));
            Feed(hash, uint64(PolicyVersion));
            Feed(hash, uint64(MaxLevel()));
            for (char const* key : { "CoAContentScaling.Enable", "CoAContentScaling.Progression.Mode", "CoAContentScaling.Progression.ClassicEnd",
                "CoAContentScaling.Progression.TbcEnd", "CoAContentScaling.ScaleItems" })
                Feed(hash, sConfigMgr->GetOption<std::string>(key, ""));
            for (auto const& s : AscensionCompatData::ClassSpells)
            {
                Feed(hash, s.ClassId);
                Feed(hash, s.RequiredLevel);
                Feed(hash, s.SpellId);
            }
            Feed(hash, uint64(1));
            for (auto const& s : AscensionCompatData::LegacyGeneratedClassSpells)
            {
                Feed(hash, s.ClassId);
                Feed(hash, s.RequiredLevel);
                Feed(hash, s.SpellId);
            }
            Feed(hash, uint64(2));
            for (auto const& r : AscensionProgression::Ranks)
            {
                Feed(hash, r.ClassId);
                Feed(hash, r.FirstSpellId);
                Feed(hash, r.SpellId);
                Feed(hash, r.RequiredLevel);
            }
            Feed(hash, uint64(3));
            for (auto const& r : AscensionCompatData::FelswornHordeCapitalRifts)
            {
                Feed(hash, r.SpellId);
                Feed(hash, r.RequiredLevel);
            }
            Feed(hash, uint64(4));
            for (auto const& t : AscensionCompatData::TaughtAbilities)
            {
                Feed(hash, t.ClassId);
                Feed(hash, t.SpecId);
                Feed(hash, t.RequiredLevel);
                Feed(hash, t.ParentSpellId);
                Feed(hash, t.SpellId);
            }
            Feed(hash, uint64(5));
            for (auto const& t : AscensionCompatData::TalentReplacements)
            {
                Feed(hash, t.ClassId);
                Feed(hash, t.SpecId);
                Feed(hash, t.ParentSpellId);
                Feed(hash, t.OriginalSpellId);
                for (auto const& rank : t.Ranks)
                {
                    Feed(hash, rank.SpellId);
                    Feed(hash, rank.RequiredLevel);
                }
            }
            Feed(hash, uint64(6));
            for (auto const& e : AscensionCompatData::CoATalentEntries)
            {
                Feed(hash, e.EntryId);
                Feed(hash, e.ClassId);
                Feed(hash, e.SpecId);
                Feed(hash, e.SpellCount);
                Feed(hash, e.AECost);
                Feed(hash, e.TECost);
                Feed(hash, e.RequiredLevel);
                for (uint32 spell : e.SpellIds)
                    Feed(hash, spell);
            }
            Feed(hash, uint64(7));
            for (auto const& f : AscensionCompatData::CoASelectableFreeEntries)
            {
                Feed(hash, f.EntryId);
                Feed(hash, f.GroupId);
            }
            Feed(hash, uint64(8));
            for (auto const& d : AscensionCompatData::CoAAutomaticDependencies)
            {
                Feed(hash, d.EntryId);
                for (uint32 required : d.RequiredEntryIds)
                    Feed(hash, required);
            }
            Feed(hash, uint64(9));
            for (auto const& b : AscensionCompatData::CoATalentBudgets)
            {
                Feed(hash, b.ClassId);
                Feed(hash, b.Level);
                Feed(hash, b.AE);
                Feed(hash, b.TE);
            }
            Feed(hash, uint64(10));
            std::vector<std::pair<uint32, uint32>> levels;
            if (auto const* store = sObjectMgr->GetItemTemplateStore())
            {
                levels.reserve(store->size());
                for (auto const& [entry, tpl] : *store)
                    levels.emplace_back(entry, tpl.RequiredLevel);
            }
            std::sort(levels.begin(), levels.end());
            for (auto const& [entry, level] : levels)
            {
                Feed(hash, entry);
                Feed(hash, level);
            }
            hash.Finalize();
            auto const digest = hash.GetDigest();
            std::string hex = Acore::Impl::ByteArrayToHexStr(digest.data(), digest.size(), false);
            std::transform(hex.begin(), hex.end(), hex.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return hex;
        }
    }

    bool Result::HoldsNothing() const
    {
        if (!HeldItems.empty() || !HeldSpells.empty() || !HeldActions.empty() || !BlockedSettings.empty())
            return false;
        return std::all_of(Settings.begin(), Settings.end(), [](SettingHold const& s) { return s.Entries.empty() && s.Buttons.empty(); });
    }

    uint32 MaxLevel()
    {
        return sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    }

    std::string const& Signature()
    {
        static std::mutex lock;
        static std::string value;
        std::lock_guard<std::mutex> guard(lock);
        if (value.empty())
            value = ComputeSignature();
        return value;
    }

    std::vector<uint32> PinWords()
    {
        std::vector<uint32> words = { PolicyVersion, MaxLevel() };
        std::string const& signature = Signature();
        for (std::size_t i = 0; i + 8 <= signature.size() && words.size() < PinWordCount; i += 8)
            words.push_back(uint32(std::stoul(signature.substr(i, 8), nullptr, 16)));
        return words;
    }

    bool PinMatches(std::vector<uint32> const& words)
    {
        return words == PinWords();
    }

    std::string ProgressionJson()
    {
        bool const scaling = sConfigMgr->GetOption<bool>("CoAContentScaling.Enable", true);
        return Acore::StringFormat("{{\"max_player_level\":{},\"projection_protocol\":{},\"projection_policy_version\":{},\"progression_signature\":\"{}\",\"scaling_enabled\":{}}}",
            MaxLevel(), Protocol, PolicyVersion, Signature(), scaling ? "true" : "false");
    }

    Result Project(Input const& input, uint32 level)
    {
        Result result;
        result.CanonicalLevel = input.Level;
        result.ProjectedLevel = level;
        Engine engine{ input.Class, level };

        std::unordered_set<uint32> have(input.Spells.begin(), input.Spells.end());
        auto const known = AscensionCoATalentState::KnownEntries(uint8(input.Class), [&have](uint32 spell) { return have.count(spell) != 0; });
        std::set<uint32> const heldEntries = engine.HeldEntries(known);
        std::set<uint32> const heldSpells = engine.HeldSpells(input.Spells, heldEntries);
        if (!engine.Failure.empty())
        {
            result.Error = engine.Failure;
            return result;
        }

        std::unordered_map<std::string, ItemIn const*> byId;
        for (ItemIn const& item : input.Items)
            byId[item.Id] = &item;
        std::set<std::string> heldItems;
        for (ItemIn const& item : input.Items)
        {
            if (item.HasContainer || item.Slot > 22)
                continue;
            ItemTemplate const* tpl = sObjectMgr->GetItemTemplate(item.Entry);
            if (!tpl)
            {
                result.Error = Acore::StringFormat("this realm does not know item {}", item.Entry);
                return result;
            }
            if (tpl->RequiredLevel > level)
                heldItems.insert(item.Id);
        }
        for (ItemIn const& item : input.Items)
            if (item.HasContainer && heldItems.count(item.Container))
                heldItems.insert(item.Id);
        result.HeldItems.assign(heldItems.begin(), heldItems.end());
        result.HeldSpells.assign(heldSpells.begin(), heldSpells.end());

        for (ActionIn const& action : input.Actions)
            if (action.Type == 0 && engine.SpellHeldAnywhere(action.Action, heldSpells, heldEntries))
                result.HeldActions.emplace_back(action.Spec, action.Button);
        SortUnique(result.HeldActions);

        for (SettingIn const& setting : input.Settings)
        {
            SettingKind const kind = Classify(setting.Source);
            if (kind == SettingKind::None)
                continue;
            SettingHold hold;
            hold.Source = setting.Source;
            std::vector<uint32> const& v = setting.Values;
            bool valid = false;
            if (kind == SettingKind::Slot)
            {
                AscensionCoATalentState::SpecializationSlot slot;
                if (AscensionCoATalentState::ParseSpecializationSlot(v, slot) && slot.ClassId == input.Class)
                {
                    valid = true;
                    hold.Kind = "slot";
                    std::set<uint32> held = engine.HeldEntries(slot.Entries);
                    hold.Entries.assign(held.begin(), held.end());
                    std::set<uint32> recordSpells;
                    for (auto const& entry : slot.Entries)
                        if (auto found = engine.Idx.Entries.find(entry.EntryId); found != engine.Idx.Entries.end())
                            for (uint32 i = 0; i < found->second->SpellCount && i < found->second->SpellIds.size(); ++i)
                                if (found->second->SpellIds[i])
                                    recordSpells.insert(found->second->SpellIds[i]);
                    std::set<uint32> recordHeld = engine.HeldSpells({ recordSpells.begin(), recordSpells.end() }, held);
                    for (auto const& [button, action] : slot.Actions)
                        if (engine.SpellHeldAnywhere(action, recordHeld, held) || heldSpells.count(action))
                            hold.Buttons.push_back(button);
                }
            }
            else if (kind == SettingKind::Build)
            {
                if (!v.empty() && v[0] <= v.size() - 1 && Zeros(v, std::size_t(v[0]) + 1))
                {
                    valid = true;
                    hold.Kind = "build";
                    std::vector<AscensionCoATalentState::KnownEntry> picks;
                    for (std::size_t i = 1; i <= v[0]; ++i)
                        if (v[i])
                            picks.push_back({ v[i] / 10, v[i] % 10 });
                    std::set<uint32> held = engine.HeldEntries(picks);
                    hold.Entries.assign(held.begin(), held.end());
                }
            }
            else
            {
                if (!v.empty() && v[0] <= (v.size() - 1) / 2 && Zeros(v, 2 * std::size_t(v[0]) + 1))
                {
                    valid = true;
                    hold.Kind = "bar";
                    for (std::size_t i = 0; i < v[0]; ++i)
                        if (v[2 * i + 2] && engine.SpellHeldAnywhere(v[2 * i + 2], heldSpells, heldEntries))
                            hold.Buttons.push_back(v[2 * i + 1]);
                }
            }
            if (!engine.Failure.empty())
            {
                result.Error = engine.Failure;
                return result;
            }
            if (!valid)
            {
                result.BlockedSettings.push_back(setting.Source);
                continue;
            }
            SortUnique(hold.Entries);
            SortUnique(hold.Buttons);
            result.Settings.push_back(std::move(hold));
        }
        std::sort(result.BlockedSettings.begin(), result.BlockedSettings.end());
        result.Ok = true;
        return result;
    }

    std::string ManifestJson(Result const& r)
    {
        std::string items;
        for (std::size_t i = 0; i < r.HeldItems.size(); ++i)
            items += Acore::StringFormat("{}{}", i ? "," : "", Quote(r.HeldItems[i]));
        std::string actions;
        for (std::size_t i = 0; i < r.HeldActions.size(); ++i)
            actions += Acore::StringFormat("{}[{},{}]", i ? "," : "", r.HeldActions[i].first, r.HeldActions[i].second);
        std::string settings;
        for (std::size_t i = 0; i < r.Settings.size(); ++i)
        {
            SettingHold const& s = r.Settings[i];
            if (s.Entries.empty() && s.Buttons.empty())
                continue;
            settings += Acore::StringFormat("{}{{\"source\":{},\"kind\":\"{}\",\"entries\":[{}],\"buttons\":[{}]}}", settings.empty() ? "" : ",", Quote(s.Source), s.Kind, Numbers(s.Entries), Numbers(s.Buttons));
        }
        std::string blocked;
        for (std::size_t i = 0; i < r.BlockedSettings.size(); ++i)
            blocked += Acore::StringFormat("{}{}", i ? "," : "", Quote(r.BlockedSettings[i]));
        return Acore::StringFormat(
            "{{\"status\":\"ok\",\"projection\":{{\"active\":true,\"protocol\":{},\"policy_version\":{},\"progression_signature\":\"{}\",\"max_player_level\":{},"
            "\"canonical_level\":{},\"projected_level\":{},\"held_items\":[{}],\"held_spells\":[{}],\"held_actions\":[{}],\"settings\":[{}],\"blocked_settings\":[{}]}}}}",
            Protocol, PolicyVersion, Signature(), MaxLevel(), r.CanonicalLevel, r.ProjectedLevel, items, Numbers(r.HeldSpells), actions, settings, blocked);
    }
}
