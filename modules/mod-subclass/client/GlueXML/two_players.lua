-- Two-Players: begin
CHAR_CREATE_FEMALE_ONLY_RACES["Two-Headed Ogre"] = nil
if CHAR_CREATE_MALE_ONLY_RACES then CHAR_CREATE_MALE_ONLY_RACES["The Twins"] = nil end
CHAR_CREATE_TWO_PLAYERS_RACES = { ["Two-Headed Ogre"] = "male", ["The Twins"] = "female" }
CHAR_CREATE_TWO_PLAYERS_TIPS = {
	["The Twins"] = {
		id = 96,
		faction = "alliance",
		lore = "|cffffd100Hero Race, Two-Players.|r Two brothers, two players. The first brother leads and fights up "
			.. "close; the second follows him everywhere and casts, shoots or heals. They level together, fall "
			.. "together and get back up together.",
		racials = {
			{"Two Players", "Achievement_Arena_2v2_7", "passive", "Two players are brothers: the first moves and "
				.. "fights, the second follows him and casts. He cannot be hurt; the first brother takes the hits."},
			{"Agree or Disagree", "Spell_Shadow_BrainWash", "passive", "Strike the same foe together to agree and hit "
				.. "harder. Pick different targets and the brothers start arguing."},
			{"Brother Toss", "Ability_Warrior_Bladestorm", "active", "When the brothers agree, press together: the "
				.. "first brother hurls the second into the fight, who lands with a burst of his magic."},
			{"Shared Blood", "Spell_Deathknight_BloodPresence", "passive", "Two bodies, one fate: they fall together "
				.. "and rise together."},
		},
	},
	["Two-Headed Ogre"] = {
		id = 85,
		faction = "horde",
		lore = "|cffffd100Hero Race, Two-Players.|r Two heads, two players, one body. The left head walks the body and "
			.. "fights up close; the right head rides along and casts. They level together, share the gold and the "
			.. "repair bill, and argue about both. When they agree, they unleash something neither could do alone.",
		racials = {
			{"Two Players", "Achievement_Arena_2v2_7", "passive", "Two players share this body: the Melee Head moves "
				.. "and fights, the Ranged Head casts from the shoulder. A bot plays a head whose player is away."},
			{"Agree or Disagree", "Spell_Shadow_BrainWash", "passive", "Strike the same foe together to agree and hit "
				.. "harder. Pick different targets and the heads start arguing."},
			{"Bonk Heads!", "Ability_Warrior_Bladestorm", "active", "When the heads agree, press together: the Melee Head "
				.. "hurls its weapons or shield, the Ranged Head breathes its magic over everything in front."},
			{"Shared Blood", "Spell_Deathknight_BloodPresence", "passive", "One body, one health pool."},
			{"Tolerance", "INV_Drink_05", "passive", "Drinks make you half as drunk, the haze wears off twice as "
				.. "fast, and you never get past merely drunk."},
			{"Mutate Me!", "Spell_Shadow_Charm", "active", "Let the eyes come out: switch to the mutated look, "
				.. "eyes everywhere, or back again."},
		},
	},
}
do
	local saved = {}
	local function SelectedRaceName()
		local button = _G["CharCreateRaceButton" .. (CharacterCreate.selectedRace or 0)]
		return button and button.Text and button.Text:GetText(), button
	end
	local function TwoPlayersSlot(name)
		local slot = name and CHAR_CREATE_TWO_PLAYERS_RACES[name]
		if slot == "male" and GetSelectedSex() == SEX_MALE then return slot end
		if slot == "female" and GetSelectedSex() == SEX_FEMALE then return slot end
	end
	local function SwapTips(name, button, active)
		local tips = CHAR_CREATE_TWO_PLAYERS_TIPS[name]
		if not tips or not CHAR_CREATE_RACE_LORE or not CHAR_CREATE_COA_RACIALS then return end
		local lore = CHAR_CREATE_RACE_LORE[tips.faction]
		if not saved[name] then
			saved[name] = { lore = lore and lore[name], racials = CHAR_CREATE_COA_RACIALS[tips.id] }
		end
		local want = active and tips or saved[name]
		if lore then lore[name] = want.lore end
		CHAR_CREATE_COA_RACIALS[tips.id] = want.racials
		if button and button.tooltipText and want.lore then
			button.tooltipText[1] = want.lore .. "\n\n"
			button.coaTip = nil
		end
	end
	CHAR_CREATE_TWO_PLAYERS_CAMERA = 1.35
	local bigRaceSaved = {}
	local function PullCameraBack(active)
		if not CHAR_CREATE_BIG_RACES or not GetNameForRace then return end
		local _, fileString = GetNameForRace()
		if not fileString then return end
		if bigRaceSaved[fileString] == nil then
			bigRaceSaved[fileString] = CHAR_CREATE_BIG_RACES[fileString] or false
		end
		if active then
			CHAR_CREATE_BIG_RACES[fileString] = CHAR_CREATE_TWO_PLAYERS_CAMERA
		else
			CHAR_CREATE_BIG_RACES[fileString] = bigRaceSaved[fileString] or nil
		end
	end
	local function Relabel()
		local name, button = SelectedRaceName()
		local slot = TwoPlayersSlot(name)
		local available = name and CHAR_CREATE_TWO_PLAYERS_RACES[name]
		if CharCreateMaleButton and CharCreateMaleButton.nameFrame then
			CharCreateMaleButton.nameFrame.text:SetText(available == "male" and "Two-Players" or MALE)
		end
		if CharCreateFemaleButton and CharCreateFemaleButton.nameFrame then
			CharCreateFemaleButton.nameFrame.text:SetText(available == "female" and "Two-Players" or FEMALE)
		end
		for raceName in pairs(CHAR_CREATE_TWO_PLAYERS_TIPS) do
			pcall(SwapTips, raceName, raceName == name and button or nil, raceName == name and slot ~= nil)
		end
		pcall(CharacterCreate_ShowHeadButtons, slot ~= nil)
		pcall(PullCameraBack, slot ~= nil)
	end
	CHAR_CREATE_TWO_PLAYERS_HEAD = CHAR_CREATE_TWO_PLAYERS_HEAD or "left"
	local headButtons = {}
	local function MarkHeads()
		for head, headButton in pairs(headButtons) do
			headButton:SetChecked(head == CHAR_CREATE_TWO_PLAYERS_HEAD and 1 or 0)
		end
	end
	local function ChooseHead(head)
		CHAR_CREATE_TWO_PLAYERS_HEAD = head
		MarkHeads()
		if CharacterCreate_RefreshClassButtons then pcall(CharacterCreate_RefreshClassButtons, CharacterCreate) end
		if RefreshSelectedClass then pcall(RefreshSelectedClass) end
		MarkHeads()
	end
	local function HeadButton(head, label, tip, anchor, y)
		local headButton = CreateFrame("CheckButton", nil, CharacterCreate)
		headButton:SetFrameStrata("DIALOG")
		headButton:SetFrameLevel(100)
		headButton:EnableMouse(true)
		headButton:RegisterForClicks("LeftButtonUp")
		headButton:SetSize(24, 24)
		headButton:SetPoint("LEFT", anchor, "RIGHT", 12, y)
		headButton:SetNormalTexture("Interface\\Buttons\\UI-CheckBox-Up")
		headButton:SetPushedTexture("Interface\\Buttons\\UI-CheckBox-Down")
		headButton:SetHighlightTexture("Interface\\Buttons\\UI-CheckBox-Highlight", "ADD")
		headButton:SetCheckedTexture("Interface\\Buttons\\UI-CheckBox-Check")
		local text = headButton:CreateFontString(nil, "OVERLAY")
		text:SetFont("Fonts\\FRIZQT__.TTF", 12, "OUTLINE")
		text:SetTextColor(1, 0.82, 0)
		text:SetPoint("LEFT", headButton, "RIGHT", 1, 0)
		text:SetText(label)
		headButton:SetHitRectInsets(0, -text:GetStringWidth() - 4, 0, 0)
		headButton:SetScript("OnClick", function() ChooseHead(head) end)
		headButton:SetScript("OnEnter", function(self)
			GlueTooltip:SetOwner(self, "ANCHOR_RIGHT")
			GlueTooltip:SetText(label)
			GlueTooltip:AddLine(tip, 1, 1, 1, 1, true)
			GlueTooltip:Show()
		end)
		headButton:SetScript("OnLeave", function() GlueTooltip:Hide() end)
		headButton:Hide()
		headButtons[head] = headButton
	end
	function CharacterCreate_ShowHeadButtons(active)
		if not headButtons.left and CharCreateFemaleButton then
			HeadButton("left", "Melee", "Melee Head: you make the body, move and fight up close. Melee and tank "
				.. "classes.", CharCreateFemaleButton, 13)
			HeadButton("right", "Ranged", "Ranged Head: you ride on a partner's body and cast or heal. Caster, healer "
				.. "and ranged classes.", CharCreateFemaleButton, -13)
		end
		for _, headButton in pairs(headButtons) do
			headButton:SetShown(active and true or false)
		end
		MarkHeads()
	end
	function CharacterCreate_TwoPlayersSlot()
		if not (CharacterCreate and CharacterCreate.IsShown and CharacterCreate:IsShown()) then return end
		return TwoPlayersSlot((SelectedRaceName()))
	end
	local setRace = SetCharacterRace
	function SetCharacterRace(...)
		local result = setRace(...)
		pcall(Relabel)
		return result
	end
	local setGender = SetCharacterGender
	function SetCharacterGender(...)
		local result = setGender(...)
		pcall(Relabel)
		return result
	end
end
do
	local PAIR_PAYLOAD_ID = 1398096451
	local function Plain(text)
		return (string.gsub(string.gsub(text, "|c%x%x%x%x%x%x%x%x", ""), "|r", ""))
	end
	local function SentPairs()
		local found = {}
		if not GetJsonCacheData then return found end
		local ok, sent = pcall(GetJsonCacheData, "MISC_PLAYER_DATA_PAYLOAD", PAIR_PAYLOAD_ID)
		if not ok or type(sent) ~= "string" then return found end
		local pattern = '"([^"]+)":{"name":"([^"]+)","color":"(%x*)","pair":"([^"]+)"}'
		for name, class, color, joined in string.gmatch(sent, pattern) do
			found[name] = { class = class, color = color ~= "" and color or nil, joined = joined }
		end
		return found
	end
	local function ClassLineBeside(label)
		local parent = label:GetParent()
		for _, region in ipairs({ parent:GetRegions() }) do
			if region ~= label and region.GetObjectType and region:GetObjectType() == "FontString"
				and region:GetText() and string.find(region:GetText(), LEVEL or "Level", 1, true) then
				return region
			end
		end
	end
	local pairLines = {}
	local function PairLine(classLine)
		local content = classLine:GetParent()
		local line = pairLines[content]
		if not line then
			line = content:CreateFontString(nil, "OVERLAY")
			line.coaSubclassLine = true
			pairLines[content] = line
		end
		local font, size, flags = classLine:GetFont()
		if font then
			line:SetFont(font, size, flags)
		end
		line:SetShadowOffset(classLine:GetShadowOffset())
		line:SetShadowColor(classLine:GetShadowColor())
		line:SetJustifyH(classLine:GetJustifyH())
		line.layoutIndex = (classLine.layoutIndex or 2) + 0.5
		if not line:GetPoint(1) then
			line:SetPoint("TOPLEFT", classLine, "BOTTOMLEFT", 0, -1)
		end
		return line, content
	end
	local measure
	local function ClassIndent(classLine)
		local prefix = string.match(classLine:GetText() or "", "^(.-)|c")
		if not prefix or prefix == "" then
			return 0
		end
		if not measure then
			measure = classLine:GetParent():CreateFontString(nil, "OVERLAY")
			measure.coaSubclassLine = true
		end
		local font, size, flags = classLine:GetFont()
		if font then
			measure:SetFont(font, size, flags)
		end
		measure:SetText(prefix)
		local width = measure:GetStringWidth() or 0
		measure:Hide()
		return width
	end
	local function Relayout(content)
		if content.Layout then
			pcall(content.Layout, content)
		end
	end
	local function ShowPair(label, name, pair)
		local text = label:GetText()
		local from, last = string.find(text, name, 1, true)
		if from and not string.find(Plain(text), pair.joined, 1, true) then
			label:SetText(string.sub(text, 1, from - 1) .. pair.joined .. string.sub(text, last + 1))
		end
		label.twoPlayersName = name
		local classLine = ClassLineBeside(label)
		if not classLine then return end
		local line, content = PairLine(classLine)
		local wanted = "& " .. (pair.color and ("|cff" .. pair.color .. pair.class .. "|r") or pair.class)
		line.seen = true
		if line:GetText() ~= wanted or not line:IsShown() then
			line:SetText(wanted)
			line:Show()
			Relayout(content)
		end
		local indent = ClassIndent(classLine)
		local _, anchor, _, x = line:GetPoint(1)
		if anchor ~= classLine or math.abs((x or 0) - indent) > 0.5 then
			line:ClearAllPoints()
			line:SetPoint("TOPLEFT", classLine, "BOTTOMLEFT", indent, -1)
		end
		local below = content.Location
		if below and below:IsShown() and select(2, below:GetPoint(1)) ~= line then
			below:ClearAllPoints()
			below:SetPoint("TOPLEFT", line, "BOTTOMLEFT", -indent, -1)
		end
	end
	local watch = CreateFrame("Frame")
	watch:SetScript("OnUpdate", function()
		if not (CharacterSelect and CharacterSelect:IsShown() and EnumerateFrames) then return end
		for _, line in pairs(pairLines) do
			line.seen = false
		end
		local sent = SentPairs()
		if not next(sent) then return end
		local frame = EnumerateFrames()
		while frame do
			if frame:IsVisible() and frame ~= CharacterCreate then
				for _, region in ipairs({ frame:GetRegions() }) do
					local text = region.GetObjectType and region:GetObjectType() == "FontString" and region:IsVisible()
						and region:GetText()
					local plain = text and Plain(text)
					local name = plain and sent[plain] and plain
					local before = region.twoPlayersName
					if not name and plain and before and sent[before] and plain == sent[before].joined then
						name = before
					end
					if name then
						pcall(ShowPair, region, name, sent[name])
					end
				end
			end
			frame = EnumerateFrames(frame)
		end
		for content, line in pairs(pairLines) do
			if not line.seen and line:IsShown() then
				line:Hide()
				Relayout(content)
			end
		end
	end)
end
do
	local settle = CreateFrame("Frame")
	settle:SetScript("OnUpdate", function(self, elapsed)
		if not (CharacterSelect and CharacterSelect:IsShown()) then
			self.done, self.wait = nil, 0
			return
		end
		if self.done or not (GetNumCharacters and GetNumCharacters() > 0) then return end
		self.wait = (self.wait or 0) + elapsed
		if self.wait < 0.2 then return end
		self.done = true
		local list = CharacterSelect.list
		if list and list.SetSelectedIndex and list.ScrollToSelection and (CharacterSelect.selectedIndex or 0) > 0 then
			pcall(list.SetSelectedIndex, list, CharacterSelect.selectedIndex)
			pcall(list.ScrollToSelection, list)
		end
	end)
end
CHAR_CREATE_TWO_PLAYERS_SPECS = {
	BARBARIAN = { left = {"Brutality", "Ancestry"}, right = {"Headhunting"} },
	CHRONOMANCER = { left = {}, right = {"Time", "Infinite", "Artificer"} },
	CULTIST = { left = {"Godblade", "Dreadnought"}, right = {"Heretic", "Corruption"} },
	DEMONHUNTER = { left = {"Slayer", "Tyrant"}, right = {"Infernal"} },
	FLESHWARDEN = { left = {"Hellfire", "Defiance", "War"}, right = {} },
	GUARDIAN = { left = {"Gladiator", "Inspiration", "Vanguard"}, right = {} },
	MONK = { left = {"Oathkeeper", "Zealot", "Crusader"}, right = {} },
	NECROMANCER = { left = {}, right = {"Death", "Animation", "Rime"} },
	PROPHET = { left = {"Fortitude", "Stalking"}, right = {"Rot", "Vizier"} },
	PYROMANCER = { left = {}, right = {"Flameweaving", "Incineration", "Draconic"} },
	RANGER = { left = {"Brigand"}, right = {"Archery", "Farstrider"} },
	REAPER = { left = {"Soul", "Harvest", "Domination"}, right = {} },
	SONOFARUGAL = { left = {"Accursed", "Eternal"}, right = {"Fleshweaver", "Sanguine", "Accursed"} },
	SPIRITMAGE = { left = {"Engravement", "Riftblade"}, right = {"Glyphic"} },
	STARCALLER = { left = {"Warden", "Moon Guard"}, right = {"Moon Priest", "Sentinel"} },
	STORMBRINGER = { left = {}, right = {"Wind", "Maelstrom", "Lightning"} },
	SUNCLERIC = { left = {"Valkyrie", "Seraphim"}, right = {"Piety", "Blessings"} },
	TINKER = { left = {}, right = {"Demolition", "Mechanics", "Invention"} },
	WILDWALKER = { left = {"Wildwalker", "Mountain King"}, right = {"Geomancy"} },
	WITCHDOCTOR = { left = {}, right = {"Shadowhunting", "Voodoo", "Brewing"} },
	WITCHHUNTER = { left = {"Inquisition", "Black Knight"}, right = {"Boltslinger", "Houndmaster"} },
}
do
	local function SpecList(names)
		if #names == 0 then return "|cff9d9d9dnone|r" end
		return "|cffffffff" .. table.concat(names, ", ") .. "|r"
	end
	local function AddTwoPlayers(tip)
		if not CharacterCreate_TwoPlayersSlot() then return end
		local owner = tip.GetOwner and tip:GetOwner()
		local specs = owner and owner.class and CHAR_CREATE_TWO_PLAYERS_SPECS[owner.class]
		if not specs or tip.twoPlayersFor == owner then return end
		tip.twoPlayersFor = owner
		local right = CHAR_CREATE_TWO_PLAYERS_HEAD == "right"
		tip:AddLine(" ")
		tip:AddLine("Two-Players", 1, 0.82, 0)
		tip:AddLine("Melee Head" .. (right and "" or ", you") .. " (moves and fights up close): "
			.. SpecList(specs.left), 1, 0.82, 0, 1, true)
		tip:AddLine("Ranged Head" .. (right and ", you" or "") .. " (rides along, casts and heals): "
			.. SpecList(specs.right), 1, 0.82, 0, 1, true)
		if #(right and specs.right or specs.left) == 0 then
			tip:AddLine((right and "Melee Head only" or "Ranged Head only") .. ": pick the other head to make this "
				.. "class, or let a partner with this class join you.", 1, 0.3, 0.3, 1, true)
		end
		tip:Show()
	end
	local function NotForChosenHead(classID)
		local _, token = GetClassInfo(classID)
		local specs = token and CHAR_CREATE_TWO_PLAYERS_SPECS[token]
		if not specs then return true end
		return #(CHAR_CREATE_TWO_PLAYERS_HEAD == "right" and specs.right or specs.left) == 0
	end
	if C_CharacterCreate and C_CharacterCreate.CanCreateClass then
		local canCreateClass = C_CharacterCreate.CanCreateClass
		C_CharacterCreate.CanCreateClass = function(classID, ...)
			local allowed = canCreateClass(classID, ...)
			local active, slot = pcall(CharacterCreate_TwoPlayersSlot)
			if allowed and active and slot then
				local ok, notForHead = pcall(NotForChosenHead, classID)
				if ok and notForHead then return false end
			end
			return allowed
		end
	end
	if CharacterCreateUtil and CharacterCreateUtil.IsRaceClassEnabledAndValid then
		local enabledAndValid = CharacterCreateUtil.IsRaceClassEnabledAndValid
		CharacterCreateUtil.IsRaceClassEnabledAndValid = function(race, classID, ...)
			local valid = enabledAndValid(race, classID, ...)
			local active, slot = pcall(CharacterCreate_TwoPlayersSlot)
			if valid and active and slot then
				local ok, notForHead = pcall(NotForChosenHead, classID)
				if ok and notForHead then return false end
			end
			return valid
		end
	end
	if ClassInfoUtil and ClassInfoUtil.GetClassRoles then
		local classRoles = ClassInfoUtil.GetClassRoles
		ClassInfoUtil.GetClassRoles = function(...)
			local roles = classRoles(...)
			local ok, slot = pcall(CharacterCreate_TwoPlayersSlot)
			if roles and ok and slot then
				if CHAR_CREATE_TWO_PLAYERS_HEAD == "right" then
					return { RangedDPS = roles.RangedDPS, CasterDPS = roles.CasterDPS, Healer = roles.Healer }
				end
				return { MeleeDPS = roles.MeleeDPS, Tank = roles.Tank }
			end
			return roles
		end
	end
	if GlueTooltip and GlueTooltip.HookScript then
		GlueTooltip:HookScript("OnShow", function(tip) pcall(AddTwoPlayers, tip) end)
		GlueTooltip:HookScript("OnHide", function(tip) tip.twoPlayersFor = nil end)
	end
end
do
	local HEADS_PAYLOAD_ID = 1398096451
	local function ServerTakesHeads()
		if not GetJsonCacheData then return false end
		local ok, sent = pcall(GetJsonCacheData, "MISC_PLAYER_DATA_PAYLOAD", HEADS_PAYLOAD_ID)
		return ok and type(sent) == "string" and string.find(sent, '"#heads"', 1, true) ~= nil
	end
	local createCharacter = CreateCharacter
	function CreateCharacter(name, ...)
		local ok, slot = pcall(CharacterCreate_TwoPlayersSlot)
		if type(name) == "string" and name ~= "" and ok and slot and not PAID_SERVICE_TYPE
			and ServerTakesHeads() then
			name = name .. (CHAR_CREATE_TWO_PLAYERS_HEAD == "right" and "#R" or "#M")
		end
		return createCharacter(name, ...)
	end
end
-- Two-Players: end
