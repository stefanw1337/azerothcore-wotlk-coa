"""Builds patch-ZZZZZZ-SKIN.MPQ, the Skinshifter client patch, from the CoA client's own newest files.
    python mkskinclient.py <client Data folder> <out.MPQ> --server-data <worldserver DataDir>
        [--server-conf <skinshifter.conf>] [--worldserver <worldserver executable>]
It reads Enum.lua, SharedConstants.lua, CharacterCreate.lua and ChrClasses.dbc in the client's load order (leaving out
its own output), adds class 33 to each, and writes them into the output MPQ. Run again after a client update."""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mkmergepatch import verified, written
from mpqpull import Archive, archives

CLASS_ID = 33
CLASS_FILE = "SKINSHIFTER"
CLASS_NAME = "Skinshifter"
ICON_FOLDER = Path(__file__).resolve().parent / "icons"
# Written by the worldserver at startup (mod-skinshifter): the Skinshifter's spells exactly as the server has them.
SERVER_SPELLS = Path("skinshifter_client_spells.txt")
# The worldserver that writes it: a file older than the worldserver is from the server before the last build.
WORLDSERVER = Path("worldserver.exe")
# The Skinshifter's rows of CoA's Character Advancement tables (talent window), written by the server at start-up
# (SkinshifterAdvancement.cpp): table, id, template id, then byte=<u|f|s>value over a copy of the template row.
# The same file carries Root Walk's creature looks for the herb and ore nodes CoA has no look for
# (CreatureModelData, CreatureDisplayInfo).
SERVER_ADVANCEMENT = SERVER_SPELLS.with_name("skinshifter_advancement.txt")
ADVANCEMENT_TABLES = ("CharacterAdvancement", "CharacterAdvancementClassTypes", "CharacterAdvancementTabTypes",
                      "ChrSpecs", "CharacterAdvancementEssence", "CreatureModelData", "CreatureDisplayInfo")
SPELL_CLASS_SET = 16  # the class's spell family, as SkinshifterFamily on the server (CoA's client has room for 0-38 only)
SPELL_FIELDS = 234
SPELL_TEXTS = (136, 153, 170, 187)  # name, rank, description, tooltip (enUS; 16 locales + mask each)
FALLBACK_ICON = "Interface\\Icons\\Ability_Rogue_Disguise"
# Own icons: tools\icons\<name>.blp -> Interface\Icons\<name>.blp, plus a SpellIcon.dbc row with this ID.
# The IDs must match SkinshifterSpells.h on the server. A missing .blp gets the fallback texture, so the row is always there.
ICONS = {
    "Skinshifter_StealForm": 39901, "Skinshifter_ShedSkin": 39902, "Skinshifter_SkinCollection": 39903,
    "Skinshifter_MimicStrike": 39904, "Skinshifter_StolenVigor": 39905, "Skinshifter_SlipAway": 39906,
    "Skinshifter_Class": 39907, "Skinshifter_SheepsClothing": 39908, "Skinshifter_Possess": 39909,
    "Skinshifter_Release": 39910, "Skinshifter_PossessorClass": 39911, "Skinshifter_NotAProfessor": 39912,
    "Skinshifter_SlipBetweenWorlds": 39913,
    # the four specializations: their spellbook tabs and their cards in the specialization window
    "Skinshifter_Beastskin": 39914, "Skinshifter_Courtier": 39915, "Skinshifter_Felstriker": 39916,
    "Skinshifter_Elementalist": 39917,
    # spellbook tabs of stolen abilities, by the creature type they were stolen from (with the Grimoire copies)
    "Skinshifter_TabBeast": 39918, "Skinshifter_TabHumanoid": 39919, "Skinshifter_TabUndead": 39920,
    "Skinshifter_TabDemon": 39921, "Skinshifter_TabElemental": 39922, "Skinshifter_TabDragonkin": 39923,
    "Skinshifter_TabGiant": 39924, "Skinshifter_TabMechanical": 39925,
    # an empty slot node in the talent trees (filled, the addon shows the ability in it)
    "Skinshifter_EmptySlot": 39926,
}
# Spellbook tabs: one per tree. (The spells were once missing with the tabs on; that was CoA's spell stream, which
# only takes effect from the next world load - the spells are in this patch's Spell.dbc now.)
SPELLBOOK_TABS = True
# Spellbook tabs: class skill lines and the spells in them. Must match SkinshifterSpells.h on the server.
SKILL_LINES = (  # id, name, SpellIcon id
    (3330, "Skinshifter", 39907), (3331, "Beastskin", 39914), (3332, "Courtier", 39915), (3333, "Felstriker", 39916),
    (3334, "Elementalist", 39917),
    # stolen abilities (the Grimoire copies): all in "Skins" (3343); the per-type lines are kept but hold nothing
    (3335, "Beast", 39918), (3336, "Dragonkin", 39923), (3337, "Demon", 39921), (3338, "Elemental", 39922),
    (3339, "Giant", 39924), (3340, "Undead", 39920), (3341, "Humanoid", 39919), (3342, "Mechanical", 39925),
    (3343, "Skins", 39903),
)
SKILL_SPELLS = {}  # spell id: skill line, read from SERVER_SPELLS (the server decides which tab a spell is in)
# Until an icon of our own exists, a distinct existing icon instead of the shared fallback.
STAND_IN_ICONS = {
    "Skinshifter_Possess": "Interface\\Icons\\Spell_Shadow_Possession",
    "Skinshifter_Release": "Interface\\Icons\\Spell_Shadow_DemonicCircleTeleport",
    "Skinshifter_SlipBetweenWorlds": "Interface\\Icons\\Spell_Shadow_Twilight",
    "Skinshifter_PossessorClass": "Interface\\Icons\\Spell_Shadow_ShadowWordDominate",
    "Skinshifter_NotAProfessor": "Interface\\Icons\\INV_Misc_Book_09",
}
ICON = ("Interface\\Icons\\Skinshifter_Class" if (ICON_FOLDER / "Skinshifter_Class.blp").exists() else FALLBACK_ICON)
POSSESSOR_ICON = ("Interface\\Icons\\Skinshifter_PossessorClass"
                  if (ICON_FOLDER / "Skinshifter_PossessorClass.blp").exists()
                  else STAND_IN_ICONS["Skinshifter_PossessorClass"])
# Possessor: the Skinshifter's name on ghost-like races. Flavour only, the class is the same. Must match
# Skinshifter.Class.PossessorRaces on the server; listed by the race's file name and by its English name.
POSSESSOR_RACES = ("BANSHEE", "BANSHEEHORDE", "ETHEREAL", "ETHEREALALLIANCE", "Banshee", "Ethereal")
SOURCES = {
    "Interface\\SharedXML\\Enum.lua": None,
    "Interface\\SharedXML\\SharedConstants.lua": None,
    "Interface\\GlueXML\\CharacterCreate.lua": None,
    "DBFilesClient\\ChrClasses.dbc": None,
    "DBFilesClient\\CharBaseInfo.dbc": None,
    "DBFilesClient\\CharStartOutfit.dbc": None,
    "DBFilesClient\\CharacterCreationClassDetails.dbc": None,
    "DBFilesClient\\Vehicle.dbc": None,
    "DBFilesClient\\VehicleSeat.dbc": None,
    "DBFilesClient\\SpellIcon.dbc": None,
    "DBFilesClient\\ChrRaces.dbc": None,
    "DBFilesClient\\Spell.dbc": None,
    "DBFilesClient\\SkillLine.dbc": None,
    "DBFilesClient\\SkillLineAbility.dbc": None,
    "DBFilesClient\\SkillRaceClassInfo.dbc": None,
}
for table in ADVANCEMENT_TABLES:
    SOURCES[f"DBFilesClient\\{table}.dbc"] = None
# CoA's talent window finds a class's and a spec's trees by name through tables in this file
# (ClassConversions): the class tree owner's name (CharacterAdvancementClassTypes) and the tab names
# (CharacterAdvancementTabTypes), as the server writes them in SkinshifterAdvancement.cpp.
# Wraith Running's look (Skinshifter.WraithRunning.Visual = WRAITH_VISUAL on the server): the Death Knight's Wraith Walk
# as far as 3.3.5 can show it. In Legion the body turns see-through and pale blue (a shader) over blue flames at the
# feet (spells\archimonde_blue_fire.m2), with its cast sounds (spell_dk_wrathwalk_cast01-04). 3.3.5's "Ghost Visual"
# (886) does the body the same way: a pale blue tint and 50% transparency (its kit's CharProc 1 and 14) and a ghostly
# mist; the flames and the sounds are already in CoA's client.
WRAITH_FROM = 886                 # Ghost Visual: its state (while on) kit is copied
WRAITH_VISUAL = 9900001           # our SpellVisual; also our state kit and its model attach
WRAITH_IMPACT_KIT = 9900002       # the start: only the Wraith Walk cast sound
WRAITH_FLAMES = 6416              # SpellVisualEffectName: spells\archimonde_blue_fire.m2 at a quarter size
WRAITH_FLAMES_AT = 19             # attachment: the base (the feet), as in Legion
WRAITH_SOUND = 420001             # our SoundEntries row
WRAITH_SOUND_FROM = 27034         # a one-shot spell sound to copy (shadowfury_impact_base)
WRAITH_SOUND_FILES = ("spell_dk_wrathwalk_cast01.ogg", "spell_dk_wrathwalk_cast02.ogg",
                      "spell_dk_wrathwalk_cast03.ogg", "spell_dk_wrathwalk_cast04.ogg")
# The Possessor's look while possessing a body (Skinshifter.Fx.BlueGlowVisual = GLOW_VISUAL on the server): the body
# fully seen with Ghost Visual's pale blue tint (its CharProc 1, colour 0x8CB9FD) and mist, without its 50%
# transparency (CharProc 14). CoA's "Glowy (Blue)" (22576) turned the body near black (CharProc 1 with colour 0x2D3304)
# under a cloud of sparkles.
GLOW_VISUAL = 9900003             # our SpellVisual and its state kit
# Root Walking's dig (DigVisual in SkinshifterSpells.cpp): Burrow's look (7473) with only its precast kit (the earth
# thrown up); its cast kit plays the "struck hard" flinch.
DIG_FROM = 7473
DIG_VISUAL = 9900004
VISUAL_TABLES = ("SpellVisual", "SpellVisualKit", "SpellVisualKitModelAttach", "SoundEntries")
for table in VISUAL_TABLES:
    SOURCES[f"DBFilesClient\\{table}.dbc"] = None
ADVANCEMENT_UTIL = "Interface\\FrameXML\\Util\\CharacterAdvancementUtil.lua"
SOURCES[ADVANCEMENT_UTIL] = None
# The talent window's pictures: CoA looks up the atlas talents-background-<class>-<spec> (behind both trees) and
# spec-thumbnail-<class>-<spec> (the spec cards), falling back to Druid Restoration and Hero. A picture
# tools\art\<spec>.jpg (or .png) becomes both, in CoA's own sizes: a 1612x774 background on a 2048x1024 texture and
# a 616x372 card on a 1024x512 one. A spec without a picture keeps CoA's fallback.
ATLAS_INFO = "Interface\\SharedXML\\AtlasInfo.lua"
SOURCES[ATLAS_INFO] = None
ART_FOLDER = Path(__file__).resolve().parent / "art"
ART_SPECS = ("Beastskin", "Courtier", "Felstriker", "Elementalist")
ART_DARKEST_MEAN = 0.25  # talent backgrounds are darkened to at most this mean brightness, for the icons on top
# The Skin Collection window's picture, one per flavour: tools\art\collection_skinshifter.jpg and
# collection_possessor.jpg, as a 1024x752 picture on a 1024x1024 texture (the window is 640x470).
COLLECTION_ART = (("skinshifter", "Interface\\SkinCollection\\CollectionSkinshifter"),
                  ("possessor", "Interface\\SkinCollection\\CollectionPossessor"))
ADVANCEMENT_CLASS_NAME = "Skinshifter"
ADVANCEMENT_SPECS = (("Beastskin", "BEASTSKIN"), ("Courtier", "COURTIER"), ("Felstriker", "FELSTRIKER"),
                     ("Elementalist", "ELEMENTALIST"))
# Class-indexed stat tables: (file, rows per class, first index). Class 33 gets a copy of STATS_FROM's block.
GAME_TABLES = (
    ("gtChanceToMeleeCrit", 100, 0), ("gtChanceToMeleeCritBase", 1, 0), ("gtChanceToSpellCrit", 100, 0),
    ("gtChanceToSpellCritBase", 1, 0), ("gtOCTRegenHP", 100, 0), ("gtOCTRegenMP", 100, 0),
    ("gtRegenHPPerSpt", 100, 0), ("gtRegenMPPerSpt", 100, 0), ("gtOCTClassCombatRatingScalar", 32, 1),
)
STATS_FROM = 11  # Druid, as Skinshifter.Class.StatsFromClass on the server
for table, _, _ in GAME_TABLES:
    SOURCES[f"DBFilesClient\\{table}.dbc"] = None
# The races that can be Skinshifters, as Skinshifter.Class.Races on the server: CharBaseInfo gets a row for each
# of them that is in ChrRaces.dbc (the character creation screen offers the class only to them). Skinshifters:
# Void Elf, Grummle, Fungarian (Alliance), Slime Giant, Faceless One, Mantid (Horde); Possessors: Banshee (Horde),
# Ethereal (Alliance). None: every race, as CoA's own classes.
SKINSHIFTER_RACES = (59, 103, 93, 104, 87, 106, 90, 109)


def pull(data_folder, own_name):
    wanted = {name.lower(): name for name in SOURCES}
    found = {}
    folders = [Path(data_folder), Path(data_folder) / "enUS"]
    for folder in folders:
        for path in archives(folder):
            if not wanted:
                break
            if path.name.lower() == own_name.lower():
                continue
            archive = Archive(path)
            for lower, name in list(wanted.items()):
                data = archive.extract(name)
                if data is not None:
                    found[name] = data
                    print(f"  {name}  <-  {path.name}")
                    del wanted[lower]
    if wanted:
        raise SystemExit("Not found in the client: " + ", ".join(wanted.values()))
    return found


def replaced(text, old, new, what, count=1):
    if text.count(old) < count:
        raise SystemExit(f"CoA changed {what}; this tool needs updating (looked for: {old.strip()[:60]})")
    return text.replace(old, new)


def enum_lua(text):
    if f"{CLASS_FILE} " in text:
        return text
    text = replaced(text, "\tSPIRITMAGE   = 32,\n}", f"\tSPIRITMAGE   = 32,\n\t{CLASS_FILE}  = {CLASS_ID},\n}}",
                    "Enum.Class in Enum.lua")
    # Class 33 has no bit in the 32-bit class masks, as on the server (ClassMaskBit).
    return replaced(text, "\tSPIRITMAGE   = 0x80000000,\n", f"\tSPIRITMAGE   = 0x80000000,\n\t{CLASS_FILE}  = 0,\n",
                    "Enum.ClassMask in Enum.lua")


SHARED_CONSTANTS_TAIL = f"""

-- Skinshifter (class {CLASS_ID}), added by mkskinclient.py
RAID_CLASS_COLORS["{CLASS_FILE}"] = CreateColor(1.00, 0.20, 0.60)
LOCALIZED_CLASS_NAMES_MALE["{CLASS_FILE}"] = LOCALIZED_CLASS_NAMES_MALE["{CLASS_FILE}"] or "{CLASS_NAME}"
LOCALIZED_CLASS_NAMES_FEMALE["{CLASS_FILE}"] = LOCALIZED_CLASS_NAMES_FEMALE["{CLASS_FILE}"] or "{CLASS_NAME}"
CLASS_ICON_TCOORDS["{CLASS_FILE}"] = {{ 0, 1, 0, 1 }}
CLASS_ICON_TCOORDS_ROUND["{CLASS_FILE}"] = {{ 0, 1, 0, 1 }}
CLASS_PRIMARY_STAT["{CLASS_FILE}"] = Enum.PrimaryStat.Agility
LOCALIZED_CLASS_SPEC_NAMES["{CLASS_FILE}"] = {{
	["BEASTSKIN"]    = "Beastskin",
	["COURTIER"]     = "Courtier",
	["FELSTRIKER"]   = "Felstriker",
	["ELEMENTALIST"] = "Elementalist",
}}
tinsert(CLASS_SORT_ORDER, "{CLASS_FILE}")
MAX_CLASSES = #CLASS_SORT_ORDER
CLASS_ENUM_TO_CLASS_FILE.CLASS_SKINSHIFTER = "{CLASS_FILE}"
SKINSHIFTER_CLASS_ICON = "{ICON.replace(chr(92), chr(92) * 2)}"

-- Possessor: what the Skinshifter is called on ghost-like races (flavour only, the class is the same)
SKINSHIFTER_POSSESSOR_NAME = "Possessor"
SKINSHIFTER_POSSESSOR_ICON = "{POSSESSOR_ICON.replace(chr(92), chr(92) * 2)}"
SKINSHIFTER_POSSESSOR_RACES = {{ {", ".join(f'["{race}"] = true' for race in POSSESSOR_RACES)} }}
function Skinshifter_IsPossessorRace(raceName, raceFile)
	return (raceFile and SKINSHIFTER_POSSESSOR_RACES[strupper(raceFile)]) or (raceName and SKINSHIFTER_POSSESSOR_RACES[raceName]) or false
end
if UnitClass and UnitRace then
	local originalUnitClass = UnitClass
	function UnitClass(unit)
		local name, file, id = originalUnitClass(unit)
		if file == "{CLASS_FILE}" and Skinshifter_IsPossessorRace(UnitRace(unit)) then
			name = SKINSHIFTER_POSSESSOR_NAME
		end
		return name, file, id
	end
end
-- CoA's character sheet puts the spec in front of the class when they differ; without a spec it gets the class
-- name, which for a Possessor must be "Possessor" too, or the sheet shows "Skinshifter Possessor".
if UnitSpecAndIcon and UnitRace then
	local originalUnitSpecAndIcon = UnitSpecAndIcon
	function UnitSpecAndIcon(unit, ...)
		local spec, icon = originalUnitSpecAndIcon(unit, ...)
		if spec == "{CLASS_NAME}" and Skinshifter_IsPossessorRace(UnitRace(unit)) then
			spec = SKINSHIFTER_POSSESSOR_NAME
		end
		return spec, icon
	end
end
-- The spellbook tab of the class skill line is named and drawn from SkillLine.dbc, the same for every race; a
-- Possessor's tab is "Possessor" with the Possessor icon.
if GetSpellTabInfo and UnitRace then
	local originalGetSpellTabInfo = GetSpellTabInfo
	function GetSpellTabInfo(tab, ...)
		local info = {{ n = select("#", originalGetSpellTabInfo(tab, ...)), originalGetSpellTabInfo(tab, ...) }}
		if info[1] == "{CLASS_NAME}" and Skinshifter_IsPossessorRace(UnitRace("player")) then
			info[1] = SKINSHIFTER_POSSESSOR_NAME
			info[2] = SKINSHIFTER_POSSESSOR_ICON
		end
		return unpack(info, 1, info.n)
	end
end
-- The Possessor's spectral grey; in game the SkinCollection addon (SkinPossessor.lua) colours the class with it.
SKINSHIFTER_POSSESSOR_HEX = "a8b5c7"

if GetCharacterInfo then
	-- Character select colours a class by its name; "Possessor" gets a spectral grey (glue screens only).
	if RAID_CLASS_COLORS and CreateColor then
		RAID_CLASS_COLORS[SKINSHIFTER_POSSESSOR_NAME] = CreateColor(0.66, 0.71, 0.78)  -- = SKINSHIFTER_POSSESSOR_HEX
	end
	local originalGetCharacterInfo = GetCharacterInfo
	function GetCharacterInfo(index)
		local info = {{ n = select("#", originalGetCharacterInfo(index)), originalGetCharacterInfo(index) }}
		-- name, race, class, ...: only the class name is changed, and only when it is ours
		if info[3] == "{CLASS_NAME}" and Skinshifter_IsPossessorRace(info[2]) then
			info[3] = SKINSHIFTER_POSSESSOR_NAME
		end
		return unpack(info, 1, info.n)
	end
end
"""


CUSTOM_CLASS_CHECKS = f"""-- Skinshifter (class {CLASS_ID}, mkskinclient.py): a CoA class without a bit in the 32-bit class masks
-- (Enum.ClassMask.{CLASS_FILE} = 0), so CoA's checks are answered by its class file. Before the file's own uses below.
do
	local coaIsCustomClass, coaIsDefaultClass = IsCustomClass, IsDefaultClass
	local function isSkinshifter(unit)
		if InGlue() or not UnitClass then
			return false
		end
		local _, file = UnitClass(unit or "player")
		return file == "{CLASS_FILE}"
	end
	function IsCustomClass(unit)
		if isSkinshifter(unit) then
			return true
		end
		return coaIsCustomClass(unit)
	end
	function IsDefaultClass(unit)
		if isSkinshifter(unit) then
			return false
		end
		return coaIsDefaultClass(unit)
	end
end

function IsHeroClass(unit)"""


def shared_constants(text):
    if "Skinshifter (class" in text:
        return text
    text = replaced(text, "function IsHeroClass(unit)", CUSTOM_CLASS_CHECKS, "IsHeroClass in SharedConstants.lua")
    return text.rstrip() + "\n" + SHARED_CONSTANTS_TAIL


CHARACTER_CREATE_TAIL = """

-- Skinshifter (class 33), added by mkskinclient.py: its own icon instead of a slot on the class sheet
do
	local classSheet = "Interface\\\\Glues\\\\CharacterCreate\\\\UI-CharacterCreate-Classes-Round"
	local function ShowIcon(texture, path)
		if texture.SetPortraitTexture then
			texture:SetPortraitTexture(path)
		else
			texture:SetTexture(path)
		end
		texture:SetTexCoord(0, 1, 0, 1)
	end
	local originalSetClass = CharCreateClassButtonMixin.SetClass
	function CharCreateClassButtonMixin:SetClass(className, classFileName, enabled)
		if classFileName == "SKINSHIFTER" then
			local icon = SKINSHIFTER_CLASS_ICON
			local raceName, raceFile
			if GetNameForRace then
				raceName, raceFile = GetNameForRace()
			end
			if Skinshifter_IsPossessorRace(raceName, raceFile) then
				className = SKINSHIFTER_POSSESSOR_NAME
				icon = SKINSHIFTER_POSSESSOR_ICON
			end
			ShowIcon(self:GetNormalTexture(), icon)
			ShowIcon(self:GetPushedTexture(), icon)
			self.skinshifterIcon = true
		elseif self.skinshifterIcon then
			self:GetNormalTexture():SetTexture(classSheet)
			self:GetPushedTexture():SetTexture(classSheet)
			self.skinshifterIcon = nil
		end
		local result = originalSetClass(self, className, classFileName, enabled)
		-- The tooltip title is coloured by RAID_CLASS_COLORS of the class file (Skinshifter pink); a Possessor's
		-- is the Possessor's spectral grey, as on character select and the character sheet.
		if classFileName == "SKINSHIFTER" and className == SKINSHIFTER_POSSESSOR_NAME then
			self.tooltipTitle = "|cff" .. SKINSHIFTER_POSSESSOR_HEX .. className .. "|r"
		end
		return result
	end

	local function IsPossessorSelected()
		if not GetNameForRace then
			return false
		end
		return Skinshifter_IsPossessorRace(GetNameForRace())
	end

	-- CoA's own fix lets every race take every custom class (IsRaceClassValid answers yes from 12 up); the
	-- Skinshifter only for its races (Skinshifter.Class.Races on the server, SKINSHIFTER_RACES in the tool).
	-- The race buttons are numbered: their file names are kept as they are set.
	local skinshifterRaces = { SKINSHIFTER_RACE_FILES_HERE }
	local raceFileOfButton = {}
	local originalSetRace = CharCreateRaceButtonMixin.SetRace
	function CharCreateRaceButtonMixin:SetRace(raceName, raceEnglishName, ...)
		raceFileOfButton[self:GetID()] = raceEnglishName and string.upper(raceEnglishName)
		return originalSetRace(self, raceName, raceEnglishName, ...)
	end
	-- Only when the buttons' names are the ones the tool knows (one of them seen); else no filter, as CoA's own.
	local function SkinshifterAllowed(race)
		local file = raceFileOfButton[race]
		if not file then
			return nil
		end
		for _, seen in pairs(raceFileOfButton) do
			if seen and skinshifterRaces[seen] then
				return skinshifterRaces[file] == true
			end
		end
		return nil
	end
	local originalIsRaceClassValid = IsRaceClassValid
	IsRaceClassValid = function(race, classID, ...)
		if classID == 33 then
			local allowed = SkinshifterAllowed(race)
			if allowed ~= nil then
				return allowed
			end
		end
		return originalIsRaceClassValid(race, classID, ...)
	end
	if CharacterCreateUtil and CharacterCreateUtil.IsRaceClassEnabledAndValid then
		local originalEnabledAndValid = CharacterCreateUtil.IsRaceClassEnabledAndValid
		CharacterCreateUtil.IsRaceClassEnabledAndValid = function(race, classID, ...)
			if classID == 33 then
				local allowed = SkinshifterAllowed(race)
				if allowed ~= nil then
					return allowed
				end
			end
			return originalEnabledAndValid(race, classID, ...)
		end
	end

	-- The class tooltip's and the info panel's description (CLASS_SKINSHIFTER) is the Possessor's for its races.
	local originalGetFlavorText = GetFlavorText
	function GetFlavorText(tagname, ...)
		if tagname == "CLASS_SKINSHIFTER" and IsPossessorSelected() then
			return SKINSHIFTER_POSSESSOR_FLAVOR
		end
		return originalGetFlavorText(tagname, ...)
	end

	-- The tooltip lists the resources of every spec's ChrSpecs row: mana, and the class's own tokens as the
	-- secondary resource (Skin Tokens, or Soul Tokens for a Possessor; the token ring in game).
	SKINSHIFTER_TOKENS = "Skin Tokens"
	if ClassInfoUtil then
		local originalPrimary = ClassInfoUtil.GetClassPrimaryResources
		function ClassInfoUtil.GetClassPrimaryResources(class, ...)
			if class == "SKINSHIFTER" then
				return { "MANA" }
			end
			return originalPrimary(class, ...)
		end
		local originalSecondary = ClassInfoUtil.GetClassSecondaryResources
		function ClassInfoUtil.GetClassSecondaryResources(class, ...)
			if class == "SKINSHIFTER" then
				local possessor = IsPossessorSelected()
				SKINSHIFTER_TOKENS = possessor and "Soul Tokens" or "Skin Tokens"
				if POWER_COLORS and CreateColor then
					POWER_COLORS.SKINSHIFTER_TOKENS = possessor and CreateColor(0.66, 0.71, 0.78) or CreateColor(1.00, 0.20, 0.60)
				end
				return { "SKINSHIFTER_TOKENS" }
			end
			return originalSecondary(class, ...)
		end
	end

	-- The class info panel (SetCharacterClass) needs a description and ability bullets for every class.
	CLASS_SKINSHIFTER = "A thief of more than purses. Skinshifters steal the abilities of the creatures and people they "
		.. "study and keep them for good, and collect their very skins to wear at will: a wolf on the hunt, a guard at "
		.. "the gate, a demon in the dark. They grow not from trainers but from everything they rob."
	CLASS_INFO_SKINSHIFTER0 = "- Steal Form: take a creature's abilities into your Grimoire and its look into your Skin Collection."
	CLASS_INFO_SKINSHIFTER1 = "- Wear any skin you have collected, and shed it at will."
	CLASS_INFO_SKINSHIFTER2 = "- Fights in melee, heals, or casts, depending on what you steal."
	CLASS_INFO_SKINSHIFTER3 = "- Uses mana. Leather and cloth, daggers, swords, fist weapons and staves."
	SKINSHIFTER_POSSESSOR_FLAVOR = "Some spirits never stopped wandering. A Possessor slips out of its own form and into "
		.. "the bodies of others, wearing them like borrowed coats and keeping what they knew. It is the Skinshifter's "
		.. "art, practised by the restless dead and by beings of pure energy."
	SKINSHIFTER_POSSESSOR_INFO0 = "- Possess: slip into a creature, steal its abilities and wear its body."
	SKINSHIFTER_POSSESSOR_INFO1 = "- Release the body at will and return to your own form."
	SKINSHIFTER_POSSESSOR_INFO2 = "- Fights in melee, heals, or casts, depending on what you possess."
	SKINSHIFTER_POSSESSOR_INFO3 = CLASS_INFO_SKINSHIFTER3

	local function ShowPossessorInfo()
		local _, classFileName = GetSelectedClass()
		if classFileName ~= "SKINSHIFTER" or not GetNameForRace then
			return
		end
		local raceName, raceFile = GetNameForRace()
		if not Skinshifter_IsPossessorRace(raceName, raceFile) then
			return
		end
		local bullets = ""
		for index = 0, 3 do
			bullets = bullets .. _G["SKINSHIFTER_POSSESSOR_INFO" .. index] .. "\\n\\n"
		end
		local frame = CharCreateClassInfoFrame
		frame.title:SetText(SKINSHIFTER_POSSESSOR_NAME)
		frame.scrollFrame.scrollChild.bulletText:SetText(bullets)
		frame.scrollFrame.scrollChild.infoText:SetText(SKINSHIFTER_POSSESSOR_FLAVOR .. "|n|n")
	end
	local originalSetCharacterClass = SetCharacterClass
	function SetCharacterClass(...)
		originalSetCharacterClass(...)
		ShowPossessorInfo()
	end
	local originalSetCharacterRace = SetCharacterRace
	function SetCharacterRace(...)
		originalSetCharacterRace(...)
		ShowPossessorInfo()
	end
end
"""


def race_files(data, races):
    """The ChrRaces file names (clientFileString, upper case, as the creation screen's race buttons get them)."""
    magic, count, fields, size, string_size = struct.unpack("<4s4I", data[:20])
    strings = data[20 + count * size:]
    names = []
    for index in range(count):
        record = struct.unpack_from(f"<{fields}I", data, 20 + index * size)
        if record[0] in races:
            names.append(strings[record[11]:strings.index(b"\0", record[11])].decode("utf-8").upper())
    return sorted(names)


def character_create(text, race_files_allowed):
    if "Skinshifter (class 33)" in text:
        return text
    allowed = ", ".join(f'["{name}"] = true' for name in race_files_allowed)
    tail = CHARACTER_CREATE_TAIL.replace("SKINSHIFTER_RACE_FILES_HERE", allowed)
    text = replaced(text, '\t"RANGER",\n}\n\nCLASSIC_CLASS_ORDER', f'\t"RANGER",\n\t"{CLASS_FILE}",\n}}\n\nCLASSIC_CLASS_ORDER',
                    "COA_CLASS_ORDER in CharacterCreate.lua")
    loops = text.count("for i = 1, 21 do")
    if loops < 3:
        raise SystemExit(f"CoA changed the class loops in CharacterCreate.lua ({loops} found); this tool needs updating")
    text = text.replace("for i = 1, 21 do", "for i = 1, #COA_CLASS_ORDER do")
    text = replaced(text, "local numInRow1 = 10\n", "local numInRow1 = math.ceil(#COA_CLASS_ORDER / 2)\n",
                    "the class rows in CharacterCreate.lua")
    return text.rstrip() + "\n" + tail


def chr_classes(data):
    magic, count, fields, size, string_size = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or fields != 60 or size != 240:
        raise SystemExit("ChrClasses.dbc does not have the WotLK layout (60 fields)")
    records = [list(struct.unpack_from("<60I", data, 20 + index * size)) for index in range(count)]
    strings = bytearray(data[20 + count * size:20 + count * size + string_size])
    records = [record for record in records if record[0] != CLASS_ID]
    template = next(record for record in records if record[0] == 32)

    def add(text):
        offset = len(strings)
        strings.extend(text.encode("utf-8") + b"\0")
        return offset

    record = list(template)
    record[0] = CLASS_ID
    record[1], record[2], record[3] = 1, 0, 1                    # flags, DisplayPower = mana, pet name token
    record[4:20] = [blank_text(strings)] * 16
    record[4] = add(CLASS_NAME)                                  # enUS name
    record[55] = add(CLASS_FILE)
    record[56], record[57], record[58], record[59] = SPELL_CLASS_SET, 0x2, 0, 0  # spell class set, flags, cinematic, expansion
    records.append(record)
    header = struct.pack("<4s4I", b"WDBC", len(records), fields, size, len(strings))
    return header + b"".join(struct.pack("<60I", *record) for record in records) + bytes(strings)


def race_ids(data):
    magic, count, fields, size, _ = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or size != fields * 4:
        raise SystemExit("ChrRaces.dbc is not a WDBC file")
    return sorted(struct.unpack_from("<I", data, 20 + index * size)[0] for index in range(count))


def char_base_info(data, races):
    magic, count, fields, size, string_size = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or fields != 2 or size != 2:
        raise SystemExit("CharBaseInfo.dbc does not have the WotLK layout (race, class)")
    pairs = [struct.unpack_from("<BB", data, 20 + index * size) for index in range(count)]
    pairs = [pair for pair in pairs if pair[1] != CLASS_ID]
    pairs += [(race, CLASS_ID) for race in races if race < 256]
    strings = data[20 + count * size:20 + count * size + string_size] or b"\0"
    header = struct.pack("<4s4I", b"WDBC", len(pairs), fields, size, len(strings))
    return header + b"".join(struct.pack("<BB", *pair) for pair in pairs) + strings


# The outfit row the character creation screen looks up for a Skinshifter, per race and sex: OUTFIT_CLASS's (its
# clothes fit the custom bodies; an empty row showed the Slime Giant's bare body torn), else an empty row shaped like
# the Rogue's of the same race and sex (else the race's first class's). Every other class has an outfit
# row for every race; without one the preview of a custom race's outfit read a missing row and the client crashed
# (Slime Giant, Banshee). Every race gets one, not only the Skinshifter races: picking another race while
# Skinshifter is still the chosen class previews it on that race first (Centaur crashed).
OUTFIT_FROM = 4
# The clothes the creation screen shows: CoA's own per class, race and sex in CharacterCreationClassDetails (its
# preview items, worn properly by the custom races too, e.g. the Slime Giant). The Skinshifter's are a copy of
# OUTFIT_CLASS's rows (31: Venomancer, leather and daggers).
OUTFIT_CLASS = 31


def class_details(data):
    magic, count, fields, size, string_size = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or size != fields * 4 or fields < 3:
        raise SystemExit("CharacterCreationClassDetails.dbc is not a WDBC file")
    rows = [bytearray(data[20 + index * size:20 + (index + 1) * size]) for index in range(count)]
    rows = [row for row in rows if struct.unpack_from("<I", row, 4)[0] != CLASS_ID]
    next_id = max(struct.unpack_from("<I", row)[0] for row in rows) + 1
    copies = []
    for row in rows:
        if struct.unpack_from("<I", row, 4)[0] == OUTFIT_CLASS:
            copy = bytearray(row)
            struct.pack_into("<II", copy, 0, next_id, CLASS_ID)
            copies.append(copy)
            next_id += 1
    if not copies:
        raise SystemExit(f"CharacterCreationClassDetails.dbc has no rows of class {OUTFIT_CLASS} to copy")
    rows += copies
    strings = data[20 + count * size:20 + count * size + string_size] or b"\0"
    header = struct.pack("<4s4I", b"WDBC", len(rows), fields, size, len(strings))
    print(f"  CharacterCreationClassDetails.dbc: {len(copies)} Skinshifter outfits (from class {OUTFIT_CLASS})")
    return header + b"".join(bytes(row) for row in rows) + strings


def char_start_outfit(data, races):
    magic, count, fields, size, string_size = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or size != 296:
        raise SystemExit("CharStartOutfit.dbc does not have the WotLK layout (296-byte rows)")
    rows = [bytearray(data[20 + index * size:20 + (index + 1) * size]) for index in range(count)]
    rows = [row for row in rows if row[5] != CLASS_ID]
    next_id = max(struct.unpack_from("<I", row)[0] for row in rows) + 1
    added = []
    dressed_count = 0
    for race in races:
        own = [row for row in rows if row[4] == race]
        for sex in sorted({row[6] for row in own}):
            dressed = [row for row in own if row[6] == sex and row[5] == OUTFIT_CLASS]
            donors = dressed or [row for row in own if row[6] == sex and row[5] == OUTFIT_FROM]
            if not donors:
                first = min(row[5] for row in own if row[6] == sex)
                donors = [row for row in own if row[6] == sex and row[5] == first]
            row = bytearray(donors[0])
            struct.pack_into("<I", row, 0, next_id)
            row[5] = CLASS_ID
            if not dressed:
                struct.pack_into("<72i", row, 8, *([-1] * 72))
            else:
                dressed_count += 1
            added.append(row)
            next_id += 1
    rows += added
    strings = data[20 + count * size:20 + count * size + string_size] or b"\0"
    header = struct.pack("<4s4I", b"WDBC", len(rows), fields, size, len(strings))
    print(f"  CharStartOutfit.dbc: {len(added)} Skinshifter outfits ({dressed_count} dressed as class {OUTFIT_CLASS})")
    return header + b"".join(bytes(row) for row in rows) + strings


# Steed Shift carries a passenger (SkinshifterSpells::SteedVehicleId / SteedSeatId, the same rows the server writes to
# vehicle_dbc and vehicleseat_dbc): a copy of the Mechano-hog's vehicle (one passenger) with a copy of the X-53 Touring
# Rocket's passenger seat (riding pose), the seat on the mount model's rider point, without the
# vehicle's own enter and exit animations (flags 0x004C0000).
STEED_VEHICLE, STEED_VEHICLE_FROM = 9910, 318
STEED_SEAT, STEED_SEAT_FROM = 10010, 7770
# Where the passenger sits from the rider's point: Skinshifter.SteedShift.SeatOffset in the server's conf (the server
# writes the same into vehicleseat_dbc).
SERVER_CONF = Path("skinshifter.conf")
STEED_SEAT_OFFSET = (0.0, 0.0, 0.0)
# A seat's attachment is an index into the client's seat table: 21 is model attachment 0, where a normal rider sits
# (index 0 is attachment 20, which most mounts lack: the passenger sat on the head or the neck).
STEED_SEAT_ATTACHMENT = 21


def steed_seat_offset():
    offset = list(STEED_SEAT_OFFSET)
    if SERVER_CONF.exists():
        for line in SERVER_CONF.read_text(encoding="utf-8", errors="replace").splitlines():
            key, _, value = line.partition("=")
            if key.strip() != "Skinshifter.SteedShift.SeatOffset":
                continue
            for index, part in enumerate(value.strip().strip('"').split()[:3]):
                try:
                    offset[index] = float(part)
                except ValueError:
                    pass
    return offset


def steed_rows(data, own_id, from_id, fields, change, what):
    magic, count, field_count, size, string_size = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or field_count != fields or size != fields * 4:
        raise SystemExit(f"{what} does not have the WotLK layout ({fields} fields)")
    rows = [bytearray(data[20 + index * size:20 + (index + 1) * size]) for index in range(count)]
    rows = [row for row in rows if struct.unpack_from("<I", row)[0] != own_id]
    source = next((row for row in rows if struct.unpack_from("<I", row)[0] == from_id), None)
    if source is None:
        raise SystemExit(f"{what} has no row {from_id} to copy Steed Shift's from")
    row = bytearray(source)
    struct.pack_into("<I", row, 0, own_id)
    change(row)
    rows.append(row)
    strings = data[20 + count * size:20 + count * size + string_size] or b"\0"
    header = struct.pack("<4s4I", b"WDBC", len(rows), field_count, size, len(strings))
    return header + b"".join(bytes(row) for row in rows) + strings


def steed_vehicle(data):
    def change(row):
        struct.pack_into("<8I", row, 6 * 4, 0, STEED_SEAT, 0, 0, 0, 0, 0, 0)  # seat 0 is the rider (you)
    return steed_rows(data, STEED_VEHICLE, STEED_VEHICLE_FROM, 40, change, "Vehicle.dbc")


def steed_seat(data):
    def change(row):
        # the whole body rides (0x4, upper anim Mount too): with the lower body alone the hands hung off the reins
        flags = (struct.unpack_from("<I", row, 4)[0] & ~0x004C0000) | 0x4
        x, y, z = steed_seat_offset()
        struct.pack_into("<IIfff", row, 4, flags, STEED_SEAT_ATTACHMENT, x, y, z)
        struct.pack_into("<ii", row, 17 * 4, -1, 91)  # RideUpperAnimStart, RideUpperAnimLoop
        print(f"  Steed Shift passenger seat: the rider's point, moved {x:g} {y:g} {z:g}")
    return steed_rows(data, STEED_SEAT, STEED_SEAT_FROM, 58, change, "VehicleSeat.dbc")


def game_table(data, rows_per_class, first):
    magic, count, fields, size, string_size = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or fields not in (1, 2) or size != fields * 4:
        raise SystemExit("A gt table does not have the WotLK layout")
    records = [data[20 + index * size:20 + (index + 1) * size] for index in range(count)]
    # One-field tables are indexed by row; two-field tables carry the index as their first field.
    by_index = {struct.unpack_from("<I", record)[0] if fields == 2 else position: record
                for position, record in enumerate(records)}
    target = first + (CLASS_ID - 1) * rows_per_class
    if target in by_index:
        return data
    source = [by_index.get(first + (STATS_FROM - 1) * rows_per_class + row) for row in range(rows_per_class)]
    if None in source or (fields == 1 and count != target):
        raise SystemExit(f"A gt table ({count} rows) is not laid out as one block per class for 32 classes")
    for row, record in enumerate(source):
        records.append(struct.pack("<I", target + row) + record[4:] if fields == 2 else record)
    strings = data[20 + count * size:20 + count * size + string_size] or b"\0"
    header = struct.pack("<4s4I", b"WDBC", len(records), fields, size, len(strings))
    return header + b"".join(records) + strings


def spell_icons(data):
    magic, count, fields, size, string_size = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or fields != 2 or size != 8:
        raise SystemExit("SpellIcon.dbc does not have the WotLK layout (ID, texture)")
    rows = [struct.unpack_from("<2I", data, 20 + index * size) for index in range(count)]
    strings = bytearray(data[20 + count * size:20 + count * size + string_size] or b"\0")
    ours = set(ICONS.values())
    for icon_id, offset in rows:
        if icon_id in ours:
            texture = strings[offset:strings.index(b"\0", offset)].decode("latin1")
            if "Skinshifter_" not in texture and texture != FALLBACK_ICON and texture not in STAND_IN_ICONS.values():
                raise SystemExit(f"SpellIcon ID {icon_id} is already used by the client ({texture}); pick other IDs")
    rows = [row for row in rows if row[0] not in ours]
    for name, icon_id in ICONS.items():
        texture = (f"Interface\\Icons\\{name}" if (ICON_FOLDER / f"{name}.blp").exists()
                   else STAND_IN_ICONS.get(name, FALLBACK_ICON))
        rows.append((icon_id, len(strings)))
        strings.extend(texture.encode("latin1") + b"\0")
    rows.sort()
    header = struct.pack("<4s4I", b"WDBC", len(rows), fields, size, len(strings))
    return header + b"".join(struct.pack("<2I", *row) for row in rows) + bytes(strings)


BAD_ICONS = SERVER_SPELLS.parent / "skinshifter_bad_icons.txt"
SCHOOL_ICONS = ("Ability_MeleeDamage", "Spell_Holy_HolyBolt", "Spell_Fire_FlameBolt", "Spell_Nature_Lightning",
                "Spell_Frost_FrostBolt02", "Spell_Shadow_ShadowBolt", "Spell_Arcane_Blast")
SPELL_ICON_FIELD, SPELL_SCHOOL_FIELD, SPELL_EFFECT_FIELD, SPELL_AURA_FIELD = 133, 225, 71, 95


def icon_textures(data):
    count, size = struct.unpack_from("<4s4I", data)[1], 8
    strings = data[20 + count * size:]
    textures = {}
    for index in range(count):
        icon_id, offset = struct.unpack_from("<2I", data, 20 + index * size)
        textures[icon_id] = strings[offset:strings.index(b"\0", offset)].decode("latin1")
    return textures


def bad_icons(data, data_folder, own_name):
    """SpellIcon ids whose texture is in none of the client's archives (a spell with one shows an empty square that
    cannot be dragged). Written for the server too, which gives its copies other icons at its next start."""
    textures = icon_textures(data)
    left = {icon_id for icon_id, texture in textures.items() if texture and icon_id not in ICONS.values()}
    for folder in (Path(data_folder), Path(data_folder) / "enUS"):
        for path in archives(folder):
            if path.name.lower() == own_name.lower() or not left:
                continue
            archive = Archive(path)
            left = {icon_id for icon_id in left if archive.locate(textures[icon_id] + ".blp") is None}
            archive.close()
    loose = (Path(data_folder).parent, Path(data_folder))  # textures may also lie loose in the client folder
    left = {icon_id for icon_id in left
            if not any((root / (textures[icon_id].replace("\\", "/") + ".blp")).exists() for root in loose)}
    if len(left) > len(textures) // 4:
        print(f"  SpellIcon.dbc: {len(left)} of {len(textures)} icons not found in the archives: the client must keep "
              "its textures elsewhere, so no icon is called missing")
        left = set()
    bad = left | {icon_id for icon_id, texture in textures.items() if not texture}
    BAD_ICONS.write_text("".join(f"{icon_id}\n" for icon_id in sorted(bad)))
    print(f"  SpellIcon.dbc: {len(bad)} icons with no texture in the client (listed in {BAD_ICONS.name})")
    return textures, bad


def fixed_icon(values, textures, bad):
    icon = values[SPELL_ICON_FIELD]
    texture = textures.get(icon, "")
    if icon and icon not in bad and texture and not texture.lower().endswith("\\temp"):
        return icon
    by_texture = {texture.lower(): icon_id for icon_id, texture in textures.items() if icon_id not in bad}
    wanted = []
    if any(values[SPELL_EFFECT_FIELD + index] == 10 or values[SPELL_AURA_FIELD + index] == 8 for index in range(3)):
        wanted.append("Spell_Holy_Heal")
    wanted += [name for bit, name in enumerate(SCHOOL_ICONS) if values[SPELL_SCHOOL_FIELD] & (1 << bit)]
    wanted += [SCHOOL_ICONS[0], "INV_Misc_QuestionMark"]
    for name in wanted:
        found = by_texture.get(f"interface\\icons\\{name}".lower())
        if found:
            return found
    return icon


def blank_text(strings):
    """The offset of an empty string in a DBC string block. Not always 0: CoA's Spell.dbc starts its block with
    "UPDATE YOUR CLIENT!" (the text of its unused locales), so a text field left at 0 shows that."""
    if not strings:
        strings.extend(b"\0")
    if strings[0] == 0:
        return 0
    found = strings.find(b"\0\0")
    if found >= 0:
        return found + 1
    strings.extend(b"\0")
    return len(strings) - 1


def dbc_rows(data, fields, what):
    magic, count, field_count, size, string_size = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or field_count != fields or size != fields * 4:
        raise SystemExit(f"{what} does not have the WotLK layout ({fields} fields)")
    rows = [list(struct.unpack_from(f"<{fields}I", data, 20 + index * size)) for index in range(count)]
    strings = bytearray(data[20 + count * size:20 + count * size + string_size] or b"\0")
    return rows, strings


def dbc_written(rows, fields, strings):
    rows = sorted(rows, key=lambda row: row[0])
    header = struct.pack("<4s4I", b"WDBC", len(rows), fields, fields * 4, len(strings))
    return header + b"".join(struct.pack(f"<{fields}I", *row) for row in rows) + bytes(strings)


def skill_lines(data):
    rows, strings = dbc_rows(data, 56, "SkillLine.dbc")
    ours = {line_id for line_id, _, _ in SKILL_LINES}
    rows = [row for row in rows if row[0] not in ours]
    for line_id, name, icon in SKILL_LINES:
        row = [0] * 56
        row[0], row[1] = line_id, 7                   # class skill line: a spellbook tab
        blank = blank_text(strings)
        row[3:19] = [blank] * 16                      # name, description, alternate verb: empty in every locale
        row[20:36] = [blank] * 16
        row[38:54] = [blank] * 16
        row[3] = len(strings)                         # enUS name
        strings.extend(name.encode("utf-8") + b"\0")
        row[19] = 0xFF01FE                            # name locale mask, as CoA's own rows
        row[37] = icon
        rows.append(row)
    return dbc_written(rows, 56, strings)


def skill_line_abilities(data):
    rows, strings = dbc_rows(data, 14, "SkillLineAbility.dbc")
    rows = [row for row in rows if not 1990001 <= row[0] <= 2000999]  # our spells and the Grimoire copies
    for spell_id, line_id in SKILL_SPELLS.items():
        row = [0] * 14
        row[0], row[1], row[2], row[7] = spell_id, line_id, spell_id, 1
        rows.append(row)
    if not SKILL_SPELLS:
        raise SystemExit(f"{SERVER_SPELLS.name} names no spellbook tab for any spell; start the new worldserver once")
    print(f"  spellbook tabs: {len(SKILL_SPELLS)} spells placed in {len(set(SKILL_SPELLS.values()))} tabs")
    return dbc_written(rows, 14, strings)


def skill_race_class_info(data):
    rows, strings = dbc_rows(data, 8, "SkillRaceClassInfo.dbc")
    ours = {line_id for line_id, _, _ in SKILL_LINES}
    rows = [row for row in rows if row[0] not in ours]
    for line_id, _, _ in SKILL_LINES:
        # every race and class bit, as CoA's own rows list theirs: only Skinshifters are given the skill
        rows.append([line_id, line_id, 0xFFFFFFFF, 0xFFFFFFFF, 1040, 0, 0, 0])
    return dbc_written(rows, 8, strings)


def unescaped(text):
    """A text as the server wrote it: \\, \\n, \\r and \\t stand for a backslash, a line break, a return and a tab."""
    out, index = [], 0
    while index < len(text):
        if text[index] == "\\" and index + 1 < len(text):
            out.append({"n": "\n", "r": "\r", "t": "\t", "\\": "\\"}.get(text[index + 1], text[index + 1]))
            index += 2
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


def spells(data, textures=None, bad=frozenset()):
    """The client's Spell.dbc with the Skinshifter's spells added (or replaced), so the client knows them from the
    start instead of only after CoA's spell stream has taken effect (the next world load)."""
    if not SERVER_SPELLS.exists():
        raise SystemExit(f"{SERVER_SPELLS} is missing. Start the server once (it writes this file), "
                         "close the game, then run Build-Skinshifter-Client.bat again.")
    if WORLDSERVER.exists() and SERVER_SPELLS.stat().st_mtime < WORLDSERVER.stat().st_mtime:
        raise SystemExit(f"{SERVER_SPELLS.name} is older than worldserver.exe: it was written by the server from "
                         "before the last worldserver build. Start the server once (it writes the file again), "
                         "close the game, then run Build-Skinshifter-Client.bat again.")
    rows = []
    # one row per "\n": spell texts may hold a "\r" (CoA's own tooltips do), which splitlines() would break on
    with open(SERVER_SPELLS, encoding="utf-8", newline="") as file:  # newline="": keep a "\r" as it is
        text = file.read()
    for line in text.split("\n"):
        line = line.rstrip("\r")
        if not line.strip():
            continue
        parts = line.split("\t")
        values = [int(value) for value in parts[0].split(",")]
        if len(values) != SPELL_FIELDS or len(parts) not in (5, 6):
            raise SystemExit(f"{SERVER_SPELLS} has a line in the wrong format; start the new worldserver once")
        rows.append((values, [unescaped(text) for text in parts[1:5]]))
        if len(parts) == 6 and int(parts[5]):
            SKILL_SPELLS[values[0]] = int(parts[5])  # the spellbook tab, as the server has it
    magic, count, fields, size, string_size = struct.unpack("<4s4I", data[:20])
    if magic != b"WDBC" or fields != SPELL_FIELDS or size != SPELL_FIELDS * 4:
        raise SystemExit("Spell.dbc does not have the WotLK layout (234 fields)")
    ours = {values[0] for values, _ in rows}
    body = bytearray()
    kept = 0
    for index in range(count):
        record = data[20 + index * size:20 + (index + 1) * size]
        if struct.unpack_from("<I", record)[0] in ours:
            continue
        body += record
        kept += 1
    strings = bytearray(data[20 + count * size:20 + count * size + string_size])
    blank = blank_text(strings)
    fixed = 0
    for values, texts in rows:
        if textures:
            icon = fixed_icon(values, textures, bad)
            fixed += icon != values[SPELL_ICON_FIELD]
            values[SPELL_ICON_FIELD] = icon
        for field, text in zip(SPELL_TEXTS, texts):
            values[field:field + 16] = [blank] * 16  # every locale empty (not offset 0), then the enUS text
            if text:
                values[field] = len(strings)
                strings.extend(text.encode("utf-8") + b"\0")
        body += struct.pack(f"<{SPELL_FIELDS}I", *values)
    print(f"  Spell.dbc: {fixed} Skinshifter spells had an icon the client cannot show; given one of their school")
    print(f"  Spell.dbc: {len(rows)} Skinshifter spells added to the client's {kept} spells")
    header = struct.pack("<4s4I", b"WDBC", kept + len(rows), fields, size, len(strings))
    return header + bytes(body) + bytes(strings)


def wraith_visual(files):
    """SpellVisual, SpellVisualKit, SpellVisualKitModelAttach and SoundEntries with Wraith Running's look added."""
    def table(name, fields):
        rows, strings = dbc_rows(files[f"DBFilesClient\\{name}.dbc"], fields, f"{name}.dbc")
        return rows, strings, {row[0]: row for row in rows}

    def add(strings, text):
        offset = len(strings)
        strings.extend(text.encode("utf-8") + b"\0")
        return offset

    def fresh(by_id, row_id, what):
        if row_id in by_id:
            raise SystemExit(f"CoA's {what} now has a row {row_id} (Wraith Running's); this tool needs other ids")

    visuals, visual_strings, visual_by_id = table("SpellVisual", 32)
    kits, kit_strings, kit_by_id = table("SpellVisualKit", 38)
    attaches, attach_strings, attach_by_id = table("SpellVisualKitModelAttach", 10)
    sounds, sound_strings, sound_by_id = table("SoundEntries", 30)
    fresh(visual_by_id, WRAITH_VISUAL, "SpellVisual.dbc")
    fresh(kit_by_id, WRAITH_VISUAL, "SpellVisualKit.dbc")
    fresh(kit_by_id, WRAITH_IMPACT_KIT, "SpellVisualKit.dbc")
    fresh(attach_by_id, WRAITH_VISUAL, "SpellVisualKitModelAttach.dbc")
    fresh(sound_by_id, WRAITH_SOUND, "SoundEntries.dbc")
    fresh(visual_by_id, GLOW_VISUAL, "SpellVisual.dbc")
    fresh(kit_by_id, GLOW_VISUAL, "SpellVisualKit.dbc")
    source = visual_by_id.get(WRAITH_FROM)
    if not source or source[4] not in kit_by_id or WRAITH_SOUND_FROM not in sound_by_id:
        raise SystemExit(f"CoA changed ghost visual {WRAITH_FROM} or sound {WRAITH_SOUND_FROM}; this tool needs updating")

    sound = list(sound_by_id[WRAITH_SOUND_FROM])
    sound[0] = WRAITH_SOUND
    sound[2] = add(sound_strings, "spell_dk_wrathwalk_cast")
    for index in range(10):  # File[10], Freq[10]: one of the four at random
        sound[3 + index] = add(sound_strings, WRAITH_SOUND_FILES[index]) if index < len(WRAITH_SOUND_FILES) \
            else blank_text(sound_strings)
        sound[13 + index] = 1 if index < len(WRAITH_SOUND_FILES) else 0
    sound[23] = add(sound_strings, "Sound\\Spells")
    sounds.append(sound)

    state = list(kit_by_id[source[4]])
    # Kit: ID, StartAnim, Anim, Head/Chest/Base/LeftHand/RightHand/Breath/LeftWeapon/RightWeapon/Special x3/World
    # effects, SoundID, ShakeID, CharProc x4, CharParam x16, Flags. The start kit is the state kit emptied, with a sound.
    impact = list(state)
    impact[0] = WRAITH_IMPACT_KIT
    impact[3:15] = [0] * 12
    impact[15], impact[16] = WRAITH_SOUND, 0
    impact[17:21] = [0xFFFFFFFF] * 4
    impact[21:37] = [0] * 16
    state[0] = WRAITH_VISUAL
    kits += [impact, state]

    attach = [0] * 10  # ID, kit, effect name, attachment, offset x/y/z, yaw, pitch, roll
    attach[0], attach[1], attach[2], attach[3] = WRAITH_VISUAL, WRAITH_VISUAL, WRAITH_FLAMES, WRAITH_FLAMES_AT
    attaches.append(attach)

    visual = list(source)
    visual[0], visual[3], visual[4] = WRAITH_VISUAL, WRAITH_IMPACT_KIT, WRAITH_VISUAL  # impact kit, state kit
    visual[1], visual[2], visual[5], visual[6] = 0, 0, 0, 0  # no precast, cast, state-done or channel kit
    visuals.append(visual)

    # The possessed body's glow: Ghost Visual's state kit with only its tint. CharProc[4] at 17-20, then the params
    # by index: CharParamZero[4] at 21-24, One at 25-28, Two at 29-32, Three at 33-36 (one column per CharProc).
    glow = list(kit_by_id[source[4]])
    glow[0] = GLOW_VISUAL
    procs = list(glow[17:21])
    for proc in range(4):
        if procs[proc] == 14:  # the transparency
            glow[17 + proc] = 0xFFFFFFFF
            for param in range(4):
                glow[21 + param * 4 + proc] = 0
    kits.append(glow)
    glow_visual = list(source)
    glow_visual[0], glow_visual[4] = GLOW_VISUAL, GLOW_VISUAL
    glow_visual[1], glow_visual[2], glow_visual[3], glow_visual[5], glow_visual[6] = 0, 0, 0, 0, 0
    visuals.append(glow_visual)
    fresh(visual_by_id, DIG_VISUAL, "SpellVisual.dbc")
    if DIG_FROM not in visual_by_id:
        raise SystemExit(f"CoA's SpellVisual.dbc has no row {DIG_FROM} (Burrow's look); this tool needs updating")
    dig = list(visual_by_id[DIG_FROM])
    dig[0] = DIG_VISUAL
    dig[2:7] = [0] * 5  # no cast, impact, state, state-done or channel kit
    visuals.append(dig)
    print(f"  Root Walking's dig: visual {DIG_VISUAL} (Burrow's earth, no flinch)")
    print(f"  possessed body's glow: visual {GLOW_VISUAL} (pale blue tint, fully seen)")
    print(f"  Wraith Running look: visual {WRAITH_VISUAL} (see-through pale blue body, blue flames at the feet, "
          "Wraith Walk sound)")
    return [("DBFilesClient\\SpellVisual.dbc", dbc_written(visuals, 32, visual_strings)),
            ("DBFilesClient\\SpellVisualKit.dbc", dbc_written(kits, 38, kit_strings)),
            ("DBFilesClient\\SpellVisualKitModelAttach.dbc", dbc_written(attaches, 10, attach_strings)),
            ("DBFilesClient\\SoundEntries.dbc", dbc_written(sounds, 30, sound_strings))]


def art_picture(spec):
    for suffix in (".jpg", ".jpeg", ".png"):
        path = ART_FOLDER / f"{spec.lower()}{suffix}"
        if path.exists():
            return path
    return None


def picture_tools(what):
    """mkicons' picture functions, or None without Pillow (the art is then left out)."""
    try:
        from mkicons import blp_rect, placed
    except (ImportError, SystemExit):
        print(f"  {what}: Pillow is not installed (python -m pip install pillow), so it is left out")
        return None
    return blp_rect, placed


def talent_art():
    """The pictures as textures, and their atlas lines."""
    painter = picture_tools("talent art")
    if not painter:
        return [], []
    blp_rect, placed = painter
    files, lines = [], []
    for spec in ART_SPECS:
        picture = art_picture(spec)
        if not picture:
            print(f"  talent art: no {ART_FOLDER.name}\\{spec.lower()}.jpg, {spec} keeps CoA's fallback picture")
            continue
        background = f"Interface\\TalentFrame\\Backgrounds\\SkinshifterTalentBackground{spec}"
        card = f"Interface\\TalentFrame\\Thumbnails\\SkinshifterThumbnail{spec}"
        files.append((background + ".blp", blp_rect(placed(picture, (2048, 1024), (1612, 774), ART_DARKEST_MEAN))))
        files.append((card + ".blp", blp_rect(placed(picture, (1024, 512), (616, 372)))))
        token = f"{CLASS_FILE.lower()}-{spec.lower()}"
        lines.append(f'    ["talents-background-{token}"] = {{ "{background.replace(chr(92), chr(92) * 2)}", 1612, 774, '
                     '0, 0.787109375, 0, 0.755859375, false, false },')
        lines.append(f'    ["spec-thumbnail-{token}"] = {{ "{card.replace(chr(92), chr(92) * 2)}", 616, 372, '
                     '0, 0.6015625, 0, 0.7265625, false, false },')
        print(f"  talent art: {spec} from {picture.name}")
    return files, lines


COLLECTION_LIGHT = 0.28  # the pictures' mean brightness at least (0-1): the Skinshifter's dungeon came out near black


def lifted(image, area):
    """A dark picture brightened (gamma, so its highlights keep) until its mean in the area is COLLECTION_LIGHT."""
    import math
    from PIL import ImageStat
    mean = ImageStat.Stat(image.crop((0, 0) + area).convert("L")).mean[0] / 255.0
    if mean <= 0.01 or mean >= COLLECTION_LIGHT:
        return image
    gamma = math.log(COLLECTION_LIGHT) / math.log(mean)
    print(f"  collection art: brightened (mean {mean:.2f} -> {COLLECTION_LIGHT})")
    return image.point([round(255 * (v / 255) ** gamma) for v in range(256)] * 3)


def collection_art():
    """The Skin Collection window's pictures, and the Lua line that tells the addon which exist."""
    files, known = [], []
    painter = picture_tools("collection art")
    if not painter:
        return files, "SKINSHIFTER_COLLECTION_ART = {  }\n"
    blp_rect, placed = painter
    for flavour, texture in COLLECTION_ART:
        picture = art_picture(f"collection_{flavour}")
        if not picture:
            print(f"  collection art: no {ART_FOLDER.name}\\collection_{flavour}.jpg, the window keeps its plain backdrop")
            continue
        files.append((texture + ".blp", blp_rect(lifted(placed(picture, (1024, 1024), (1024, 752)), (1024, 752)))))
        known.append(f'{flavour} = "{texture.replace(chr(92), chr(92) * 2)}"')
        print(f"  collection art: {flavour} from {picture.name}")
    line = "SKINSHIFTER_COLLECTION_ART = { " + ", ".join(known) + " }\n"
    return files, line


def atlas_info(text, lines):
    if not lines or "talents-background-skinshifter" in text:
        return text
    anchor = text.index('["talents-background-reaper-soul"]') if '["talents-background-reaper-soul"]' in text else -1
    if anchor < 0:
        raise SystemExit("CoA changed AtlasInfo.lua (no talents-background-reaper-soul); this tool needs updating")
    end = text.index("\n", anchor) + 1
    return text[:end] + "\n".join(lines) + "\n" + text[end:]


def advancement_util(text):
    if "Skinshifter (class" in text:
        return text
    lines = [f"-- Skinshifter (class {CLASS_ID}), added by mkskinclient.py: its trees for CoA's talent window",
             f'ClassConversions.ClassDBCToFile["{ADVANCEMENT_CLASS_NAME}"] = "{CLASS_FILE}"']
    lines += [f'ClassConversions.SpecDBCToFile["{tab}"] = "{token}"' for tab, token in ADVANCEMENT_SPECS]
    anchor = "ClassConversions.ClassFileToDBC = table.invert(ClassConversions.ClassDBCToFile)"
    return replaced(text, anchor, "\n".join(lines) + "\n\n" + anchor, "ClassConversions in CharacterAdvancementUtil.lua")


def advancement(files):
    """CoA's Character Advancement tables with the Skinshifter's rows: each a copy of the template row of the same
    table (so every value we do not set is CoA's own), our values on top, new texts added at the end."""
    if not SERVER_ADVANCEMENT.exists():
        raise SystemExit(f"{SERVER_ADVANCEMENT} is missing. Start the server once (it writes this file; "
                         "Skinshifter.Advancement.Enable = 1), close the game, then run Build-Skinshifter-Client.bat "
                         "again.")
    if WORLDSERVER.exists() and SERVER_ADVANCEMENT.stat().st_mtime < WORLDSERVER.stat().st_mtime:
        raise SystemExit(f"{SERVER_ADVANCEMENT.name} is older than worldserver.exe. Start the server once (it writes "
                         "the file again), close the game, then run Build-Skinshifter-Client.bat again.")
    wanted = {}
    with open(SERVER_ADVANCEMENT, encoding="utf-8", newline="") as file:
        for line in file.read().split("\n"):
            line = line.rstrip("\r")
            if not line.strip():
                continue
            parts = line.split("\t")
            if len(parts) < 3 or parts[0] not in ADVANCEMENT_TABLES:
                raise SystemExit(f"{SERVER_ADVANCEMENT} has a line in the wrong format; start the new worldserver once")
            values = []
            for part in parts[3:]:
                byte, value = part.split("=", 1)
                values.append((int(byte), value[0], value[1:]))
            wanted.setdefault(parts[0], []).append((int(parts[1]), int(parts[2]), values))
    out = []
    for table in ADVANCEMENT_TABLES:
        name = f"DBFilesClient\\{table}.dbc"
        data = files[name]
        magic, count, fields, size, string_size = struct.unpack("<4s4I", data[:20])
        if magic != b"WDBC":
            raise SystemExit(f"{table}.dbc is not a WDBC file")
        records = [bytearray(data[20 + index * size:20 + (index + 1) * size]) for index in range(count)]
        strings = bytearray(data[20 + count * size:20 + count * size + string_size] or b"\0")
        rows = wanted.get(table, [])
        ours = {row_id for row_id, _, _ in rows}
        by_id = {struct.unpack_from("<I", record)[0]: record for record in records}
        kept = [record for record in records if struct.unpack_from("<I", record)[0] not in ours]
        for row_id, template, values in rows:
            if template not in by_id:
                raise SystemExit(f"The client's {table}.dbc has no row {template} to copy row {row_id} from "
                                 "(the client and the server have different CoA versions?)")
            record = bytearray(by_id[template])
            struct.pack_into("<I", record, 0, row_id)  # the row's own id, also when the line does not set byte 0
            for byte, kind, value in values:
                if kind == "u":
                    struct.pack_into("<I", record, byte, int(value))
                elif kind == "f":
                    struct.pack_into("<f", record, byte, float(value))
                elif kind == "s":
                    struct.pack_into("<I", record, byte, len(strings))
                    strings.extend(unescaped(value).encode("utf-8") + b"\0")
                else:
                    raise SystemExit(f"{SERVER_ADVANCEMENT} has an unknown value kind {kind!r}")
            kept.append(record)
        kept.sort(key=lambda record: struct.unpack_from("<I", record)[0])
        header = struct.pack("<4s4I", b"WDBC", len(kept), fields, size, len(strings))
        out.append((name, header + b"".join(bytes(record) for record in kept) + bytes(strings)))
        print(f"  {table}.dbc: {len(rows)} Skinshifter rows")
    return out


def main(data_folder, out_path):
    out = Path(out_path)
    print("[1/3] Reading CoA's newest client files")
    files = pull(data_folder, out.name)
    print("[2/3] Adding class 33 (Skinshifter)")
    built = {
        "Interface\\SharedXML\\Enum.lua": enum_lua(files["Interface\\SharedXML\\Enum.lua"].decode("utf-8")),
        "Interface\\SharedXML\\SharedConstants.lua": shared_constants(files["Interface\\SharedXML\\SharedConstants.lua"].decode("utf-8")),
        "Interface\\GlueXML\\CharacterCreate.lua": character_create(files["Interface\\GlueXML\\CharacterCreate.lua"].decode("utf-8"),
                                                                race_files(files["DBFilesClient\\ChrRaces.dbc"], SKINSHIFTER_RACES)),
        ADVANCEMENT_UTIL: advancement_util(files[ADVANCEMENT_UTIL].decode("utf-8")),
    }
    art_files, art_lines = talent_art()
    collection_files, collection_line = collection_art()
    art_files += collection_files
    built["Interface\\SharedXML\\SharedConstants.lua"] += "\n" + collection_line
    if art_lines:
        built[ATLAS_INFO] = atlas_info(files[ATLAS_INFO].decode("utf-8"), art_lines)
    entries = [(name, text.encode("utf-8")) for name, text in built.items()]
    entries.append(("DBFilesClient\\ChrClasses.dbc", chr_classes(files["DBFilesClient\\ChrClasses.dbc"])))
    entries.append(("DBFilesClient\\CharBaseInfo.dbc", char_base_info(files["DBFilesClient\\CharBaseInfo.dbc"],
                                                                    [race for race in race_ids(files["DBFilesClient\\ChrRaces.dbc"])
                                                                     if not SKINSHIFTER_RACES or race in SKINSHIFTER_RACES])))
    entries.append(("DBFilesClient\\CharStartOutfit.dbc",
                    char_start_outfit(files["DBFilesClient\\CharStartOutfit.dbc"],
                                      race_ids(files["DBFilesClient\\ChrRaces.dbc"]))))
    entries.append(("DBFilesClient\\CharacterCreationClassDetails.dbc",
                    class_details(files["DBFilesClient\\CharacterCreationClassDetails.dbc"])))
    entries.append(("DBFilesClient\\Vehicle.dbc", steed_vehicle(files["DBFilesClient\\Vehicle.dbc"])))
    entries.append(("DBFilesClient\\VehicleSeat.dbc", steed_seat(files["DBFilesClient\\VehicleSeat.dbc"])))
    for table, rows_per_class, first in GAME_TABLES:
        name = f"DBFilesClient\\{table}.dbc"
        entries.append((name, game_table(files[name], rows_per_class, first)))
    entries.append(("DBFilesClient\\SpellIcon.dbc", spell_icons(files["DBFilesClient\\SpellIcon.dbc"])))
    textures, bad = bad_icons(files["DBFilesClient\\SpellIcon.dbc"], data_folder, out.name)
    spell_table = spells(files["DBFilesClient\\Spell.dbc"], textures, bad)
    if spell_table:
        entries.append(("DBFilesClient\\Spell.dbc", spell_table))
    if SPELLBOOK_TABS:
        entries.append(("DBFilesClient\\SkillLine.dbc", skill_lines(files["DBFilesClient\\SkillLine.dbc"])))
        entries.append(("DBFilesClient\\SkillLineAbility.dbc",
                        skill_line_abilities(files["DBFilesClient\\SkillLineAbility.dbc"])))
        entries.append(("DBFilesClient\\SkillRaceClassInfo.dbc",
                        skill_race_class_info(files["DBFilesClient\\SkillRaceClassInfo.dbc"])))
    entries.extend(advancement(files))
    entries.extend(wraith_visual(files))
    entries.extend(art_files)
    for name in ICONS:
        path = ICON_FOLDER / f"{name}.blp"
        if path.exists():
            entries.append((f"Interface\\Icons\\{name}.blp", path.read_bytes()))
            print(f"  icon {name}")
        else:
            print(f"  icon {name}: no {path.name} yet, using the fallback texture")
    listing = "\r\n".join(sorted(name for name, _ in entries)).encode("latin1")
    entries.append(("(listfile)", listing))
    entries.sort(key=lambda entry: entry[0].lower())
    print(f"[3/3] Writing {out}")
    tmp = out.with_suffix(".tmp")
    tmp.write_bytes(written(entries))
    verified(tmp, entries)
    tmp.replace(out)
    print(f"Done: {out} ({out.stat().st_size} bytes, {len(entries)} files, every file read back intact)")


def use_server(server_data, server_conf, worldserver):
    global SERVER_SPELLS, SERVER_ADVANCEMENT, BAD_ICONS, SERVER_CONF, WORLDSERVER
    SERVER_SPELLS = Path(server_data) / "skinshifter_client_spells.txt"
    SERVER_ADVANCEMENT = SERVER_SPELLS.with_name("skinshifter_advancement.txt")
    BAD_ICONS = SERVER_SPELLS.parent / "skinshifter_bad_icons.txt"
    SERVER_CONF = Path(server_conf) if server_conf else Path("-")
    WORLDSERVER = Path(worldserver) if worldserver else Path("-")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Builds the Skinshifter client patch.")
    parser.add_argument("data_folder", help="the client's Data folder")
    parser.add_argument("out_path", help="the MPQ to write, normally <Data>/patch-ZZZZZZ-SKIN.MPQ")
    parser.add_argument("--server-data", required=True,
                        help="the worldserver's DataDir, where it writes skinshifter_client_spells.txt")
    parser.add_argument("--server-conf", help="skinshifter.conf, for Skinshifter.SteedShift.SeatOffset")
    parser.add_argument("--worldserver", help="the worldserver executable, to spot files older than the build")
    arguments = parser.parse_args()
    use_server(arguments.server_data, arguments.server_conf, arguments.worldserver)
    main(arguments.data_folder, arguments.out_path)
