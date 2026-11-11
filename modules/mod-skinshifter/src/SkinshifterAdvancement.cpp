/*
 * mod-skinshifter - the Skinshifter in CoA's Character Advancement (the talent window, N).
 *
 * CoA reads its classes' talent trees from client tables, on the server too (AscensionCoATalentData.cpp):
 *   CharacterAdvancementClassTypes  the class's tree owner (ours: 47, class 33)
 *   CharacterAdvancementTabTypes    one tab per specialization (95..98; the class tree is CoA's shared tab 87)
 *   ChrSpecs                        the four specializations (102..105): signature spell, identity entry, stat
 *   CharacterAdvancement            the entries: signatures, identity passives, talents (139001..)
 *   CharacterAdvancementEssence     the points per level (Reaper's, key 33)
 *   ChrClasses                      the class token the specializations are matched by (server only; the client
 *                                   patch has its own class row)
 * Each of our rows is a copy of one of CoA's own (a Reaper row of the same kind) with our values over it, so every
 * field we do not know keeps a value CoA uses. The rows are written over the server's tables into
 * DataDir/dbc_skinshifter/ (the server reads those first, skinshifter-core.patch) and as text into
 * DataDir/skinshifter_advancement.txt, from which tools/mkskinclient.py makes the same rows in the client's tables.
 * Text line: table, id, template id, then byte=<u|f|s>value (u: uint32, f: float, s: text, escaped as \\ \t \n \r).
 *
 * The same text file carries the creature looks of the gathering nodes (Root Walk: standing still, you come up as
 * the herb or ore vein that grows near you). Most herbs already have a creature look in CoA's tables; for every
 * node model that has none, a CreatureModelData and a CreatureDisplayInfo row are added (client only: the server
 * does not need a look's row to show it).
 *
 * This code is released under the GNU AGPL v3 like AzerothCore.
 */

#include "Config.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "QueryResult.h"
#include "SharedDefines.h"
#include "SkinshifterSpells.h"
#include "StringFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
bool Installed = false;
std::unordered_map<uint32, SkinshifterSpells::NodeLook> NodeLooks;  // by the node's GameObjectDisplayInfo id
SkinshifterSpells::RootWalkLooks WalkLooks;

constexpr char const* OverlayFolder = "dbc_skinshifter/";
constexpr char const* TextFile = "skinshifter_advancement.txt";

// Template rows (CoA 1.9, the Reaper): found by id and checked to be what we expect.
constexpr uint32 ReaperClass = 30;
constexpr uint32 SharedClassTab = 87;
constexpr uint32 TemplateSignature = 30107;  // Wraithblade: class tab, row 0, ability essence, square
constexpr uint32 TemplateClassTalent = 34461; // Soulfused Constitution: class tab talent, circle
constexpr uint32 TemplateIdentity = 4056;    // Weakened Souls: a spec's identity passive
constexpr uint32 TemplateSpecTalent = 6709;  // Chasing Death: spec tab talent, talent essence, circle

constexpr uint32 OurClassType = SkinshifterSpells::AdvancementClassType;
constexpr uint32 FirstEssence = 7001;

// CharacterAdvancement, by byte (the record is 692 bytes; the column and row are floats at odd offsets).
namespace Ca
{
constexpr uint32 Id = 0, Required = 8, Spells = 20, AECost = 56, TECost = 60, Level = 104, Group = 116, ClassType = 128,
    Tab = 132, AEGate = 152, TEGate = 156, Name = 188, Icon = 256, Column = 398, Row = 402, Connected = 415,
    NodeType = 612;
constexpr uint32 ConnectedCount = 15;  // the entries a node hangs from (a line from each; the client needs one of them)
}

struct Table
{
    uint32 fields = 0;
    uint32 size = 0;
    std::vector<std::vector<char>> rows;
    std::string strings;

    bool Load(std::string const& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (data.size() < 20 || std::memcmp(data.data(), "WDBC", 4) != 0)
            return false;
        uint32 header[4];
        std::memcpy(header, data.data() + 4, sizeof(header));
        uint32 const count = header[0];
        fields = header[1];
        size = header[2];
        if (data.size() < 20 + std::size_t(count) * size + header[3])
            return false;
        rows.clear();
        for (uint32 row = 0; row < count; ++row)
        {
            char const* record = data.data() + 20 + std::size_t(row) * size;
            rows.emplace_back(record, record + size);
        }
        strings.assign(data.data() + 20 + std::size_t(count) * size, header[3]);
        return true;
    }

    bool Save(std::string const& path) const
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        uint32 const header[4] = { uint32(rows.size()), fields, size, uint32(strings.size()) };
        file.write("WDBC", 4);
        file.write(reinterpret_cast<char const*>(header), sizeof(header));
        for (std::vector<char> const& row : rows)
            file.write(row.data(), row.size());
        file.write(strings.data(), strings.size());
        return bool(file);
    }

    static uint32 U32(std::vector<char> const& row, uint32 byte)
    {
        uint32 value = 0;
        std::memcpy(&value, row.data() + byte, 4);
        return value;
    }

    std::string Text(std::vector<char> const& row, uint32 byte) const
    {
        uint32 const offset = U32(row, byte);
        return offset < strings.size() ? std::string(strings.c_str() + offset) : std::string();
    }

    std::vector<char> const* Find(uint32 id) const
    {
        for (std::vector<char> const& row : rows)
            if (U32(row, 0) == id)
                return &row;
        return nullptr;
    }

    std::vector<char> const* FindIf(std::function<bool(std::vector<char> const&)> const& match) const
    {
        for (std::vector<char> const& row : rows)
            if (match(row))
                return &row;
        return nullptr;
    }
};

struct Value
{
    uint32 byte;
    char kind;  // 'u', 'f', 's'
    uint32 number = 0;
    float real = 0.0f;
    std::string text;
};

struct RowSpec
{
    std::string table;
    uint32 id;
    uint32 templateId;
    std::vector<Value> values;
    bool clientToo = true;
};

Value U(uint32 byte, uint32 number) { return { byte, 'u', number }; }
Value F(uint32 byte, float real) { Value v{ byte, 'f' }; v.real = real; return v; }
Value S(uint32 byte, std::string text) { Value v{ byte, 's' }; v.text = std::move(text); return v; }

// A node's lines: the entries it hangs from, the rest of the 15 cleared (a template's own lines lead to CoA's nodes).
void Lines(RowSpec& row, std::array<uint32, 4> const& from)
{
    for (uint32 index = 0; index < Ca::ConnectedCount; ++index)
        row.values.push_back(U(Ca::Connected + index * 4, index < from.size() ? from[index] : 0));
}

std::string Escaped(std::string const& text)
{
    std::string out;
    for (char c : text)
    {
        switch (c)
        {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string Line(RowSpec const& spec)
{
    std::string line = Acore::StringFormat("{}\t{}\t{}", spec.table, spec.id, spec.templateId);
    for (Value const& value : spec.values)
    {
        if (value.kind == 'u')
            line += Acore::StringFormat("\t{}=u{}", value.byte, value.number);
        else if (value.kind == 'f')
            line += Acore::StringFormat("\t{}=f{}", value.byte, value.real);
        else
            line += Acore::StringFormat("\t{}=s{}", value.byte, Escaped(value.text));
    }
    return line + "\n";
}

// The texture of a SpellIcon row without its folder: CharacterAdvancement and ChrSpecs name icons that way.
std::map<uint32, std::string> IconNames(std::string const& path)
{
    std::map<uint32, std::string> names;
    Table icons;
    if (!icons.Load(path) || icons.size < 8)
        return names;
    for (std::vector<char> const& row : icons.rows)
    {
        std::string texture = icons.Text(row, 4);
        if (std::size_t const slash = texture.find_last_of("\\/"); slash != std::string::npos)
            texture = texture.substr(slash + 1);
        names[Table::U32(row, 0)] = texture;
    }
    return names;
}

struct SpecLook
{
    char const* name;
    char const* icon;
    char const* templateClass;  // the ChrSpecs row this spec is copied from: a spec of the same role
    char const* templateSpec;
    bool melee, caster, healer; // the roles the creation screen lists (the template's own are replaced)
};

// Beastskin melee (Monk Zealot), Courtier healer (Chronomancer Time), the casters (Necromancer Death, Stormbringer
// Lightning).
std::array<SpecLook, 4> const Looks = { {
    { "Beastskin", "Skinshifter_Beastskin", "MONK", "FIGHTING", true, false, false },
    { "Courtier", "Skinshifter_Courtier", "CHRONOMANCER", "DISPLACEMENT", false, false, true },
    { "Felstriker", "Skinshifter_Felstriker", "NECROMANCER", "DEATH", true, true, false },
    { "Elementalist", "Skinshifter_Elementalist", "STORMBRINGER", "LIGHTNING", false, true, false },
} };

std::array<char const*, 6> const Tables = { "CharacterAdvancement", "CharacterAdvancementClassTypes",
    "CharacterAdvancementTabTypes", "ChrSpecs", "CharacterAdvancementEssence", "ChrClasses" };

// The rows, worked out from the server's own tables (the templates are found there).
bool BuildRows(std::map<std::string, Table> const& base, std::map<uint32, uint32> const& iconOfSpell,
    std::map<uint32, std::string> const& iconNames, std::vector<RowSpec>& out)
{
    using namespace SkinshifterSpells;
    Table const& advancement = base.at("CharacterAdvancement");
    Table const& classTypes = base.at("CharacterAdvancementClassTypes");
    Table const& tabTypes = base.at("CharacterAdvancementTabTypes");
    Table const& specs = base.at("ChrSpecs");
    Table const& essence = base.at("CharacterAdvancementEssence");
    Table const& classes = base.at("ChrClasses");

    struct Expected { Table const& table; uint32 size; char const* name; };
    for (Expected const& expected : { Expected{ advancement, 692, "CharacterAdvancement" },
             Expected{ classTypes, 92, "CharacterAdvancementClassTypes" },
             Expected{ tabTypes, 76, "CharacterAdvancementTabTypes" }, Expected{ specs, 260, "ChrSpecs" },
             Expected{ essence, 36, "CharacterAdvancementEssence" }, Expected{ classes, 240, "ChrClasses" } })
        if (expected.table.size != expected.size)
        {
            LOG_ERROR("module", "Skinshifter: {}.dbc has {}-byte rows, not {}; Character Advancement is not set up.",
                expected.name, expected.table.size, expected.size);
            return false;
        }

    auto iconOf = [&](uint32 spellId) -> std::string
    {
        auto const icon = iconOfSpell.find(spellId);
        auto const name = icon != iconOfSpell.end() ? iconNames.find(icon->second) : iconNames.end();
        return name != iconNames.end() ? name->second : std::string("INV_Misc_QuestionMark");
    };

    std::vector<char> const* reaperType = classTypes.FindIf([](std::vector<char> const& row)
        { return Table::U32(row, 8) == ReaperClass && Table::U32(row, 16) != 0; });
    std::vector<char> const* reaperTab = tabTypes.FindIf([&](std::vector<char> const& row)
        { return tabTypes.Text(row, 4) == "Reaping"; });
    std::vector<char> const* runemaster = classes.Find(32);
    std::vector<char> const* signature = advancement.Find(TemplateSignature);
    std::vector<char> const* classTalent = advancement.Find(TemplateClassTalent);
    std::vector<char> const* identity = advancement.Find(TemplateIdentity);
    std::vector<char> const* specTalent = advancement.Find(TemplateSpecTalent);
    if (!reaperType || !reaperTab || !runemaster || !signature || !classTalent || !identity || !specTalent ||
        Table::U32(*signature, Ca::Tab) != SharedClassTab || Table::U32(*classTalent, Ca::Tab) != SharedClassTab ||
        Table::U32(*identity, Ca::Tab) == SharedClassTab || Table::U32(*specTalent, Ca::Tab) == SharedClassTab)
    {
        LOG_ERROR("module", "Skinshifter: the Reaper rows the Character Advancement rows are copied from are not in "
            "the server's tables (CoA changed them?); Character Advancement is not set up.");
        return false;
    }
    uint32 const reaperTypeId = Table::U32(*reaperType, 0);

    out.push_back({ "CharacterAdvancementClassTypes", OurClassType, reaperTypeId,
        { U(0, OurClassType), S(4, "Skinshifter"), U(8, ClassId), S(24, "Skinshifter") } });
    out.push_back({ "ChrClasses", ClassId, 32,
        { U(0, ClassId), S(16, "Skinshifter"), S(220, "SKINSHIFTER"), U(224, SkinshifterFamily) }, false });

    std::vector<AdvancementSpec> const& specRows = AdvancementSpecs();
    for (AdvancementSpec const& spec : specRows)
    {
        SpecLook const& look = Looks[spec.spec - 1];
        SpecInfo const& info = SpecOf(spec.spec);
        std::vector<char> const* specTemplate = specs.FindIf([&](std::vector<char> const& row)
            { return specs.Text(row, 4) == look.templateClass && specs.Text(row, 8) == look.templateSpec; });
        if (!specTemplate)
        {
            LOG_ERROR("module", "Skinshifter: ChrSpecs has no {} {} to copy {} from; Character Advancement is not set "
                "up.", look.templateClass, look.templateSpec, look.name);
            return false;
        }
        out.push_back({ "CharacterAdvancementTabTypes", spec.tab, Table::U32(*reaperTab, 0),
            { U(0, spec.tab), S(4, look.name), S(8, look.name) } });
        out.push_back({ "ChrSpecs", spec.chrSpec, Table::U32(*specTemplate, 0),
            // id, class, spec, icon; cloth/leather/mail/plate (leather, as the class wears); primary stat; roles
            // (melee, ranged, caster, tank, healer, support); power: mana only, none of the template's extra or
            // secondary resources (the Necromancer's Runic Power, the Stormbringer's Static: the creation screen lists
            // them); the example spells (the signature), the passive, the identity entry; name, description, order.
            { U(0, spec.chrSpec), S(4, "SKINSHIFTER"), S(8, spec.token), S(12, look.icon), U(16, 0), U(20, 1),
              U(24, 0), U(28, 0), S(32, spec.primaryStat), U(44, look.melee), U(48, 0), U(52, look.caster), U(56, 0),
              U(60, look.healer), U(64, 0), S(72, "MANA"), S(76, "NONE"), S(80, "NONE"), S(84, "NONE"),
              S(88, "NONE"), S(92, "NONE"), U(96, info.signature), U(100, 0),
              U(104, 0), U(108, info.passive), U(112, spec.identityEntry), S(116, look.name),
              S(184, spec.description), U(252, spec.spec) } });

        // The signature: class tree, row 0 (columns 1, 3, 5, 7), one ability essence; buying it is choosing the spec.
        out.push_back({ "CharacterAdvancement", spec.signatureEntry, TemplateSignature,
            { U(Ca::Id, spec.signatureEntry), U(Ca::Required, 0), U(Ca::Required + 4, 0), U(Ca::Required + 8, 0),
              U(Ca::Spells, info.signature), U(Ca::Spells + 4, 0), U(Ca::Spells + 8, 0), U(Ca::Spells + 12, 0),
              U(Ca::Spells + 16, 0), U(Ca::AECost, 1), U(Ca::TECost, 0), U(Ca::ClassType, OurClassType),
              U(Ca::Tab, SharedClassTab), U(Ca::AEGate, 0), U(Ca::TEGate, 0),
              S(Ca::Name, NameOf(info.signature)), S(Ca::Icon, iconOf(info.signature)),
              F(Ca::Column, float(spec.spec * 2 - 1)), F(Ca::Row, 0.0f) } });
        Lines(out.back(), {});
        // The identity passive: the spec tree's corner (column 10, row 0), free with the spec.
        out.push_back({ "CharacterAdvancement", spec.identityEntry, TemplateIdentity,
            { U(Ca::Id, spec.identityEntry), U(Ca::Required, spec.signatureEntry), U(Ca::Required + 4, 0),
              U(Ca::Required + 8, 0), U(Ca::Spells, info.passive), U(Ca::Spells + 4, 0), U(Ca::Spells + 8, 0),
              U(Ca::Spells + 12, 0), U(Ca::Spells + 16, 0), U(Ca::AECost, 0), U(Ca::TECost, 0),
              U(Ca::ClassType, OurClassType), U(Ca::Tab, spec.tab), U(Ca::AEGate, 0), U(Ca::TEGate, 0),
              S(Ca::Name, NameOf(info.passive)), S(Ca::Icon, look.icon), F(Ca::Column, 10.0f), F(Ca::Row, 0.0f) } });
        Lines(out.back(), {});
    }

    // Talents: the class tree costs ability essence and opens rows 4 and 8 at 8 and 16 points spent in it; a spec
    // tree costs talent essence and opens rows 4 and 7 at 5 and 10.
    for (TalentDef const& talent : Talents())
    {
        bool const classTree = talent.tree == 0;
        uint32 const tab = classTree ? SharedClassTab : specRows[talent.tree - 1].tab;
        uint32 const gate = classTree ? (talent.row >= 8 ? 16 : talent.row >= 4 ? 8 : 0) :
            (talent.row >= 7 ? 10 : talent.row >= 4 ? 5 : 0);
        RowSpec row{ "CharacterAdvancement", talent.entry, classTree ? TemplateClassTalent : TemplateSpecTalent,
            { U(Ca::Id, talent.entry), U(Ca::Required, 0), U(Ca::Required + 4, 0), U(Ca::Required + 8, 0) } };
        for (uint32 rank = 0; rank < 5; ++rank)
            row.values.push_back(U(Ca::Spells + rank * 4, rank < talent.ranks ? talent.firstSpell + rank : 0));
        for (Value const& value : { U(Ca::AECost, classTree ? 1 : 0), U(Ca::TECost, classTree ? 0 : 1),
                 U(Ca::ClassType, OurClassType), U(Ca::Tab, tab), U(Ca::AEGate, classTree ? gate : 0),
                 U(Ca::TEGate, classTree ? 0 : gate), S(Ca::Name, talent.name), S(Ca::Icon, iconOf(talent.iconFrom)),
                 F(Ca::Column, float(talent.column)), F(Ca::Row, float(talent.row)), U(Ca::Group, talent.group) })
            row.values.push_back(value);
        if (talent.level)
            row.values.push_back(U(Ca::Level, talent.level));
        if (talent.nodeType)
            row.values.push_back(S(Ca::NodeType, talent.nodeType));
        if (talent.slot)
            row.values.push_back(S(Ca::Icon, "Skinshifter_EmptySlot"));
        Lines(row, talent.from);
        out.push_back(std::move(row));
    }

    // Essence: the Reaper's points per level (every mode row), as key 33.
    uint32 nextEssence = FirstEssence;
    for (std::vector<char> const& row : essence.rows)
        if (Table::U32(row, 8) == ReaperClass)
        {
            out.push_back({ "CharacterAdvancementEssence", nextEssence, Table::U32(row, 0),
                { U(0, nextEssence), U(8, ClassId) } });
            ++nextEssence;
        }
    if (nextEssence == FirstEssence)
    {
        LOG_ERROR("module", "Skinshifter: CharacterAdvancementEssence.dbc has no Reaper rows to copy; Character "
            "Advancement is not set up.");
        return false;
    }
    return true;
}

// Our rows over a table: a copy of the template row, our values on top, strings added at the end.
bool Apply(Table& table, std::vector<RowSpec> const& rows, std::string const& name)
{
    std::vector<std::vector<char>> added;
    for (RowSpec const& spec : rows)
    {
        if (spec.table != name)
            continue;
        std::vector<char> const* source = table.Find(spec.templateId);
        if (!source)
        {
            LOG_ERROR("module", "Skinshifter: {}.dbc has no row {} to copy row {} from.", name, spec.templateId, spec.id);
            return false;
        }
        if (table.Find(spec.id))
        {
            LOG_ERROR("module", "Skinshifter: {}.dbc already has a row {} (CoA uses that id now); Character "
                "Advancement is not set up.", name, spec.id);
            return false;
        }
        std::vector<char> row = *source;
        for (Value const& value : spec.values)
        {
            if (value.byte + 4 > row.size())
                return false;
            uint32 number = value.number;
            if (value.kind == 'f')
                std::memcpy(&number, &value.real, 4);
            else if (value.kind == 's')
            {
                number = uint32(table.strings.size());
                table.strings += value.text;
                table.strings += '\0';
            }
            std::memcpy(row.data() + value.byte, &number, 4);
        }
        added.push_back(std::move(row));
    }
    for (std::vector<char>& row : added)
        table.rows.push_back(std::move(row));
    return true;
}
}

namespace
{
// The Character Advancement rows: written over the server's tables into DataDir/dbc_skinshifter/, and added to rows
// for the text file.
bool InstallTalentRows(std::string const& dataDir, std::map<uint32, uint32> const& iconOfSpell,
    std::vector<RowSpec>& rows)
{
    std::map<std::string, Table> tables;
    for (char const* name : Tables)
        if (!tables[name].Load(dataDir + "dbc/" + name + ".dbc"))
        {
            LOG_ERROR("module", "Skinshifter: could not read {}dbc/{}.dbc; Character Advancement is not set up.",
                dataDir, name);
            return false;
        }
    if (!BuildRows(tables, iconOfSpell, IconNames(dataDir + "dbc/SpellIcon.dbc"), rows))
        return false;
    for (auto& [name, table] : tables)
        if (!Apply(table, rows, name))
            return false;

    std::error_code error;
    std::filesystem::create_directories(dataDir + OverlayFolder, error);
    for (auto const& [name, table] : tables)
        if (!table.Save(dataDir + OverlayFolder + name + ".dbc"))
        {
            LOG_ERROR("module", "Skinshifter: could not write {}{}{}.dbc.", dataDir, OverlayFolder, name);
            std::filesystem::remove_all(dataDir + OverlayFolder, error);
            return false;
        }
    return true;
}

// Root Walk's looks: ids of the rows we add (well above CoA's own, which end below 500000). Every look is a display
// row of its own at the size it is shown at, so the player's own size stays 1.0 and a change of look is instant
// (a change of size would blend slowly in the client).
constexpr uint32 FirstNodeModel = 9910001;
constexpr uint32 FirstNodeDisplay = 9911001;
constexpr uint32 LastNodeDisplay = 9911999;
constexpr uint32 WormDisplay = 9912001;
constexpr uint32 BurrowDisplay = 9912002;
constexpr char const* NodeFolder = "\\skillactivated\\tradeskillnodes\\";

std::string ModelKey(std::string path)
{
    std::transform(path.begin(), path.end(), path.begin(),
        [](unsigned char c) { return c == '/' ? '\\' : char(std::tolower(c)); });
    for (char const* extension : { ".mdx", ".mdl", ".m2" })
        if (path.size() > std::strlen(extension) && path.ends_with(extension))
            return path.substr(0, path.size() - std::strlen(extension));
    return path;
}

// Herbalism and mining locks (Lock.dbc: Type[8] at byte 4, Index[8] at byte 36).
std::set<uint32> GatheringLocks(Table const& locks)
{
    std::set<uint32> ids;
    for (std::vector<char> const& row : locks.rows)
        for (uint32 slot = 0; slot < 8; ++slot)
        {
            uint32 const type = Table::U32(row, 4 + 4 * slot);
            uint32 const index = Table::U32(row, 36 + 4 * slot);
            if (type == LOCK_KEY_SKILL && (index == LOCKTYPE_HERBALISM || index == LOCKTYPE_MINING))
                ids.insert(Table::U32(row, 0));
        }
    return ids;
}

// The looks of the gathering nodes in the world database (chests with a herbalism or mining lock and a model from
// the trade skill node folder): CoA's own creature look of the same model, or a new row for the client.
bool NodeLookRows(std::string const& dataDir, std::vector<RowSpec>& out)
{
    NodeLooks.clear();
    WalkLooks = {};
    Table locks, objects, models, displays;
    std::array<std::pair<Table*, char const*>, 4> const sources = { { { &locks, "Lock" },
        { &objects, "GameObjectDisplayInfo" }, { &models, "CreatureModelData" }, { &displays, "CreatureDisplayInfo" } } };
    for (auto const& [table, name] : sources)
        if (!table->Load(dataDir + "dbc/" + name + ".dbc"))
        {
            LOG_ERROR("module", "Skinshifter: could not read {}dbc/{}.dbc; Root Walk comes up as the worm only.",
                dataDir, name);
            return false;
        }
    std::set<uint32> const gathering = GatheringLocks(locks);
    std::map<std::string, uint32> modelOfKey;
    for (std::vector<char> const& row : models.rows)
        modelOfKey.emplace(ModelKey(models.Text(row, 8)), Table::U32(row, 0));
    std::unordered_map<uint32, uint32> displayOfModel;
    std::unordered_map<uint32, float> scaleOfDisplay;
    for (std::vector<char> const& row : displays.rows)
    {
        uint32 const id = Table::U32(row, 0), model = Table::U32(row, 4), extended = Table::U32(row, 12);
        float scale = 1.0f;
        std::memcpy(&scale, row.data() + 16, 4);
        scaleOfDisplay[id] = scale > 0.0f ? scale : 1.0f;
        auto const known = displayOfModel.find(model);
        if (!extended && (known == displayOfModel.end() || id < known->second))
            displayOfModel[model] = id;
    }

    std::map<std::string, std::vector<std::pair<uint32, float>>> objectsOfKey;  // node model -> its displays, sizes
    std::map<std::string, std::string> pathOfKey;
    QueryResult result = WorldDatabase.Query("SELECT `displayId`, `Data0`, MAX(`size`) FROM `gameobject_template` "
        "WHERE `type` = {} GROUP BY `displayId`, `Data0`", uint32(GAMEOBJECT_TYPE_CHEST));
    if (result)
        do
        {
            Field* fields = result->Fetch();
            uint32 const objectDisplay = fields[0].Get<uint32>();
            if (!gathering.count(fields[1].Get<uint32>()))
                continue;
            std::vector<char> const* row = objects.Find(objectDisplay);
            if (!row)
                continue;
            std::string const path = objects.Text(*row, 4);
            std::string const key = ModelKey(path);
            if (key.find(NodeFolder) == std::string::npos)
                continue;
            float const size = fields[2].Get<float>();
            objectsOfKey[key].emplace_back(objectDisplay, size > 0.0f ? size : 1.0f);
            pathOfKey.emplace(key, path);
        } while (result->NextRow());

    // the template of a new look: a node of CoA's own that already has one
    std::vector<char> const* templateModel = models.FindIf([&](std::vector<char> const& row)
    {
        return ModelKey(models.Text(row, 8)).find(NodeFolder) != std::string::npos &&
            displayOfModel.count(Table::U32(row, 0));
    });
    if (!templateModel)
    {
        LOG_ERROR("module", "Skinshifter: CreatureModelData.dbc has no trade skill node with a look to copy; Root Walk "
            "comes up as the worm only.");
        return false;
    }
    uint32 const templateModelId = Table::U32(*templateModel, 0);
    uint32 const templateDisplayId = displayOfModel.at(templateModelId);
    for (uint32 id = FirstNodeDisplay; id <= LastNodeDisplay; ++id)
        if (displays.Find(id))
        {
            LOG_ERROR("module", "Skinshifter: creature look row {} is already taken; Root Walk comes up as the worm only.",
                id);
            return false;
        }

    // a look of its own: a copy of the base row at the given size, always fully opaque
    auto look = [&](uint32 id, uint32 base, std::vector<Value> values, float size)
    {
        values.insert(values.begin(), U(0, id));
        values.push_back(F(16, size));
        values.push_back(U(20, 255));
        out.push_back({ "CreatureDisplayInfo", id, base, std::move(values) });
    };
    uint32 nextModel = FirstNodeModel, nextDisplay = FirstNodeDisplay, addedModels = 0;
    for (auto const& [key, objectDisplays] : objectsOfKey)
    {
        uint32 base = 0;
        std::vector<Value> overrides;
        if (auto const model = modelOfKey.find(key); model != modelOfKey.end() && displayOfModel.count(model->second))
            base = displayOfModel.at(model->second);
        else
        {
            if (models.Find(nextModel))
            {
                LOG_ERROR("module", "Skinshifter: creature model row {} is already taken; the node looks stop there.",
                    nextModel);
                break;
            }
            out.push_back({ "CreatureModelData", nextModel, templateModelId,
                { U(0, nextModel), S(8, pathOfKey.at(key)) } });
            base = templateDisplayId;
            overrides = { U(4, nextModel), U(12, 0), S(24, ""), S(28, ""), S(32, ""), S(36, "") };
            ++nextModel;
            ++addedModels;
        }
        std::map<float, uint32> displayOfSize;  // one look per size this model is placed at
        for (auto const& [objectDisplay, size] : objectDisplays)
        {
            float const shown = std::clamp(size, 0.1f, 10.0f);
            auto known = displayOfSize.find(shown);
            if (known == displayOfSize.end())
            {
                if (nextDisplay > LastNodeDisplay)
                {
                    LOG_ERROR("module", "Skinshifter: more than {} Root Walk node looks; the rest come up as the worm.",
                        LastNodeDisplay - FirstNodeDisplay + 1);
                    break;
                }
                look(nextDisplay, base, overrides, shown);
                known = displayOfSize.emplace(shown, nextDisplay++).first;
            }
            NodeLooks[objectDisplay] = { known->second, 1.0f };
        }
    }

    // the worm and the mound of earth, at their sizes from the config
    uint32 const worm = sConfigMgr->GetOption<uint32>("Skinshifter.RootWalk.Display", 16892);
    float const wormScale = sConfigMgr->GetOption<float>("Skinshifter.RootWalk.Scale", 0.05f);
    uint32 const burrow = sConfigMgr->GetOption<uint32>("Skinshifter.RootWalk.BurrowDisplay", 462175);
    float const burrowScale = sConfigMgr->GetOption<float>("Skinshifter.RootWalk.BurrowScale", 0.1f);
    for (auto const& [id, base, scale, slot] : { std::tuple{ WormDisplay, worm, wormScale, &WalkLooks.worm },
                                                 std::tuple{ BurrowDisplay, burrow, burrowScale, &WalkLooks.burrow } })
    {
        if (displays.Find(id) || !displays.Find(base))
        {
            LOG_ERROR("module", "Skinshifter: Root Walk look {} cannot be made from display {} (row taken or display "
                "missing); the config's display is used at its size.", id, base);
            continue;
        }
        look(id, base, {}, (scaleOfDisplay.count(base) ? scaleOfDisplay.at(base) : 1.0f) * scale);
        *slot = id;
    }
    LOG_INFO("module", "Skinshifter: Root Walk looks: {} node models ({} new to the client), {} node looks, worm {}, "
        "mound of earth {}.", objectsOfKey.size(), addedModels, nextDisplay - FirstNodeDisplay, WalkLooks.worm,
        WalkLooks.burrow);
    return true;
}
}

namespace SkinshifterSpells
{
RootWalkLooks RootWalkLooksOf()
{
    return WalkLooks;
}

NodeLook NodeLookOf(uint32 objectDisplayId)
{
    auto const look = NodeLooks.find(objectDisplayId);
    return look == NodeLooks.end() ? NodeLook{} : look->second;
}

bool AdvancementInstalled()
{
    return Installed;
}

void RemoveAdvancement(std::string const& dataDir)
{
    std::error_code error;
    std::filesystem::remove_all(dataDir + OverlayFolder, error);
    std::filesystem::remove(dataDir + TextFile, error);
    Installed = false;
}

bool InstallAdvancement(std::string const& dataDir, std::map<uint32, uint32> const& iconOfSpell, bool talents)
{
    RemoveAdvancement(dataDir);
    std::vector<RowSpec> rows;
    if (talents)
        talents = InstallTalentRows(dataDir, iconOfSpell, rows);
    if (!talents)
        rows.clear();
    NodeLookRows(dataDir, rows);

    std::string text;
    for (RowSpec const& spec : rows)
        if (spec.clientToo)
            text += Line(spec);
    if (std::ofstream out(dataDir + TextFile, std::ios::binary | std::ios::trunc); out)
        out << text;
    else
        LOG_ERROR("module", "Skinshifter: could not write {}{} for the client patch.", dataDir, TextFile);

    Installed = talents;
    if (talents)
        LOG_INFO("module", "Skinshifter: Character Advancement set up: {} talents, {} specializations, in {}{}.",
            Talents().size(), AdvancementSpecs().size(), dataDir, OverlayFolder);
    return talents;
}
}
