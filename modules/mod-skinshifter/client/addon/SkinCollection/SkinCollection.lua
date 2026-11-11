-- Skin Collection: the Skinshifter's window of stolen skins. It talks to the server's mod-skinshifter through
-- hidden addon whispers to yourself with the prefix "SKIN" (nothing shows in the chat).

local PREFIX = "SKIN"
local ROWS, ROW_HEIGHT = 12, 30

local TYPE_NAMES = {
	[1] = "Beast", [2] = "Dragonkin", [3] = "Demon", [4] = "Elemental", [5] = "Giant", [6] = "Undead",
	[7] = "Humanoid", [8] = "Critter", [9] = "Mechanical", [10] = "Unknown", [11] = "Totem", [12] = "Companion",
	[13] = "Gas Cloud",
}
-- What a skin gives while you wear it, and the Echo it leaves for 20 sec when you change out of it (the server's
-- numbers, SkinshifterSpells.cpp TypeBuffs).
local ECHOES = {
	[1] = "Worn: +20% health, +50% armor. Echo: +10% attack and movement speed.",
	[2] = "Worn: +8% Fire, Frost and Nature damage, +8% health. Echo: +8% Fire, Frost and Nature damage.",
	[3] = "Worn: +10% Fire and Shadow damage. Echo: +8% Fire and Shadow damage.",
	[4] = "Worn: +10% Fire, Frost and Nature damage. Echo: +8%.",
	[5] = "Worn: +15% health. Echo: +10% health.",
	[6] = "Worn: +10% Shadow damage, +50 Shadow resistance. Echo: +8% Shadow damage, +50 Shadow resistance.",
	[7] = "Worn: +10% healing, +5% all stats. Echo: +15% healing.",
	[9] = "Worn: +25% armor. Echo: +20% armor.",
}
-- The Skinshifter's own creature-type icons (client patch), with a stock icon until the patch has them.
local TYPE_ICONS = {
	[1] = { "Beast", "Interface\\Icons\\Ability_Hunter_Pet_Wolf" },
	[2] = { "Dragonkin", "Interface\\Icons\\INV_Misc_Head_Dragon_01" },
	[3] = { "Demon", "Interface\\Icons\\Spell_Shadow_SummonFelHunter" },
	[4] = { "Elemental", "Interface\\Icons\\Spell_Fire_Elemental_Totem" },
	[5] = { "Giant", "Interface\\Icons\\Ability_Warrior_Rampage" },
	[6] = { "Undead", "Interface\\Icons\\Spell_Shadow_RaiseDead" },
	[7] = { "Humanoid", "Interface\\Icons\\INV_Misc_GroupNeedMore" },
	[9] = { "Mechanical", "Interface\\Icons\\INV_Gizmo_02" },
}

-- A texture of our own when the client patch has it, else the stand-in.
local function SetOwnTexture(texture, own, standIn)
	if not texture:SetTexture(own) then
		texture:SetTexture(standIn)
	end
end

-- Beasts by family: the hunter pet icon of their kind.
local FAMILY_ICONS = {
	[1] = "Wolf", [2] = "Cat", [3] = "Spider", [4] = "Bear", [5] = "Boar", [6] = "Crocolisk", [7] = "Vulture",
	[8] = "Crab", [9] = "Gorilla", [11] = "Raptor", [12] = "TallStrider", [20] = "Scorpid", [21] = "Turtle",
	[24] = "Bat", [25] = "Hyena", [26] = "Owl", [27] = "WindSerpent", [30] = "DragonHawk", [31] = "Ravager",
	[32] = "WarpStalker", [33] = "Sporebat", [34] = "NetherRay", [35] = "Serpent", [37] = "Moth", [38] = "Chimera",
	[39] = "Devilsaur", [41] = "Silithid", [42] = "Worm", [43] = "Rhino", [44] = "Wasp", [45] = "CoreHound",
}

local function IconOf(skin)
	if skin.type == 1 and FAMILY_ICONS[skin.family] then
		return "Interface\\Icons\\Ability_Hunter_Pet_" .. FAMILY_ICONS[skin.family]
	end
	local typeIcon = TYPE_ICONS[skin.type]
	if typeIcon then
		return "Interface\\Icons\\Skinshifter_Tab" .. typeIcon[1], typeIcon[2]
	end
	return "Interface\\Icons\\INV_Misc_Head_Dragon_Black"
end

local skins, abilities, worn = {}, {}, 0
local worldMax  -- the skins of the creatures in this game's world (server: MAX)
local filtered, selected = {}, nil
local incoming, incomingAbilities

local function Shown(region, shown)
	if shown then region:Show() else region:Hide() end
end

local function Send(message)
	SendAddonMessage(PREFIX, message, "WHISPER", UnitName("player"))
end

-- Main frame ------------------------------------------------------------------------------------------------
local frame = CreateFrame("Frame", "SkinCollectionFrame", UIParent)
frame:SetSize(640, 470)
frame:SetPoint("CENTER")
frame:SetFrameStrata("HIGH")
frame:SetMovable(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
local DIALOG_BACKDROP = {
	bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background-Dark",
	edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
	tile = true, tileSize = 32, edgeSize = 32,
	insets = { left = 11, right = 12, top = 12, bottom = 11 },
}
frame:SetBackdrop(DIALOG_BACKDROP)
frame:Hide()

-- The flavour's picture behind the window (built into the SKIN patch by mkskinclient.py; without it the
-- window keeps the plain dark backdrop). The picture is 1024x752 on its texture; the inside is 617x447, so
-- a few rows are cut from top and bottom to keep its shape. Dimmed so the cards stay readable (the client builder
-- brings a dark picture up to the others' brightness first).
local picture = frame:CreateTexture(nil, "BACKGROUND")
picture:SetPoint("TOPLEFT", 11, -12)
picture:SetPoint("BOTTOMRIGHT", -12, 11)
picture:SetTexCoord(0, 1, 5 / 1024, 747 / 1024)
picture:SetVertexColor(0.55, 0.55, 0.55)
picture:Hide()

local function ShowFlavourPicture()
	local art = SKINSHIFTER_COLLECTION_ART
	if type(art) ~= "table" then return end
	local possessor = Skinshifter_IsPossessorRace and Skinshifter_IsPossessorRace(UnitRace("player"))
	local texture = possessor and art.possessor or art.skinshifter
	if texture and picture:SetTexture(texture) then
		picture:Show()
	else
		picture:Hide()
	end
end
tinsert(UISpecialFrames, "SkinCollectionFrame")

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -18)
title:SetText("Skin Collection")

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -6, -6)

local count = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
count:SetPoint("TOPLEFT", 24, -44)

local search = CreateFrame("EditBox", "SkinCollectionSearch", frame, "InputBoxTemplate")
search:SetSize(180, 20)
search:SetPoint("TOPLEFT", 28, -62)
search:SetAutoFocus(false)
local searchHint = search:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
searchHint:SetPoint("LEFT", 4, 0)
searchHint:SetText("Search")

-- List --------------------------------------------------------------------------------------------------------
local list = CreateFrame("ScrollFrame", "SkinCollectionList", frame, "FauxScrollFrameTemplate")
list:SetPoint("TOPLEFT", 20, -90)
list:SetSize(230, ROWS * ROW_HEIGHT)

local rows = {}
local Refresh, ShowSelected

-- Wears a collected skin (a macro on the action bar calls this).
function SkinCollection_Wear(display)
	Send("WEAR;" .. tostring(display))
end

-- The macro icon index of a texture (the list of macro icons), or 1 (the question mark).
local function MacroIconIndex(texture)
	texture = texture and texture:lower()
	if texture then
		for index = 1, GetNumMacroIcons() do
			local icon = GetMacroIconInfo(index)
			if icon and icon:lower() == texture then return index end
		end
	end
	return 1
end

-- Dragging a skin out of the list puts a macro for it on the cursor, for the action bar: one character macro per
-- skin, named after it (made the first time, then reused).
local function PickupSkin(skin)
	if not skin or InCombatLockdown() then return end
	local name = (skin.name or "Skin"):sub(1, 16)
	local body = "/run SkinCollection_Wear(" .. skin.display .. ")"
	local index = GetMacroIndexByName(name)
	if index == 0 then
		local _, characterCount = GetNumMacros()
		if characterCount >= (MAX_CHARACTER_MACROS or 18) then
			print("Skin Collection: no room for another character macro (delete one to drag a skin to the bar).")
			return
		end
		index = CreateMacro(name, MacroIconIndex((IconOf(skin))), body, 1)
	else
		EditMacro(index, name, MacroIconIndex((IconOf(skin))), body)
	end
	if index and index > 0 then PickupMacro(index) end
end

for index = 1, ROWS do
	local row = CreateFrame("Button", nil, frame)
	row:SetSize(230, ROW_HEIGHT - 2)
	row:SetPoint("TOPLEFT", list, "TOPLEFT", 0, -(index - 1) * ROW_HEIGHT)
	row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
	row.selectedTexture = row:CreateTexture(nil, "BACKGROUND")
	row.selectedTexture:SetAllPoints()
	row.selectedTexture:SetTexture("Interface\\QuestFrame\\UI-QuestLogTitleHighlight")
	row.selectedTexture:SetVertexColor(1.0, 0.2, 0.6, 0.5)
	row.icon = row:CreateTexture(nil, "ARTWORK")
	row.icon:SetSize(24, 24)
	row.icon:SetPoint("LEFT", 2, 0)
	row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	row.name:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 6, 0)
	row.name:SetPoint("RIGHT", -4, 0)
	row.name:SetJustifyH("LEFT")
	row.info = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	row.info:SetPoint("BOTTOMLEFT", row.icon, "BOTTOMRIGHT", 6, 0)
	row:SetScript("OnClick", function(self)
		selected = self.skin
		Refresh()
	end)
	row:SetScript("OnDoubleClick", function(self)
		Send("WEAR;" .. self.skin.display)
	end)
	row:RegisterForDrag("LeftButton")
	row:SetScript("OnDragStart", function(self) PickupSkin(self.skin) end)
	rows[index] = row
end

-- Preview -----------------------------------------------------------------------------------------------------
-- a DressUpModel, not a PlayerModel: only it dresses a character-model NPC (its baked skin, face, hair and items);
-- a PlayerModel gives such a creature its bare model, white
local preview = CreateFrame("DressUpModel", nil, frame)
preview:SetPoint("TOPLEFT", 270, -44)
preview:SetSize(340, 260)
-- no panel behind the model: the window's picture shows through
preview.facing = 0
preview:EnableMouse(true)
preview:SetScript("OnMouseDown", function(self) self.dragX = GetCursorPosition() end)
preview:SetScript("OnMouseUp", function(self) self.dragX = nil end)
-- The mouse wheel steps the camera back or closer; each skin starts at a distance from its model's size.
preview:EnableMouseWheel(true)
preview:SetScript("OnMouseWheel", function(self, delta)
	self.zoom = math.max(-1, math.min(30, (self.zoom or 0) - delta * math.max(0.25, (self.zoom or 0) * 0.15 + 0.25)))
	self:SetPosition(-self.zoom, 0, 0)
end)
-- A model set in the same frame the window opens is often not drawn in 3.3.5, so it is set once a moment later.
-- SetCreature loads the model in the background: it must not be cleared again while it loads.
local modelNote = "not set yet"
-- The ways to show a skin, tried in order until one gives the frame a model (GetModel names a file), each given its
-- time to load. CoA's client replaces PlayerModel:SetDisplayInfo with its own: it takes a CREATURE id, asks the
-- server for the creature when the client has not seen it, and then shows it as in the world (a character-model
-- NPC with its race, face and worn items too; given a display id it showed most humanoids white). A plain 3.3.5
-- client's SetDisplayInfo takes the display. SetCreature needs the creature in the client's cache.
local coaLoader = Creature and Creature.CreateFromID and true
local byLoadedCreature = { "SetDisplayInfo (creature)", function(self, skin)
	return skin.entry and self:SetDisplayInfo(skin.entry) end, 3 }
local byDisplay = { "SetDisplayInfo", function(self, skin) return self.SetDisplayInfo and self:SetDisplayInfo(skin.display) end, 0.5 }
local byCreature = { "SetCreature", function(self, skin) return self.SetCreature and skin.entry and self:SetCreature(skin.entry) end, 0.5 }
local methods = coaLoader and { byLoadedCreature, byCreature } or { byDisplay, byCreature }
local function HasModel(self)
	local ok, model = pcall(self.GetModel, self)
	return ok and type(model) == "string" and model ~= "", ok and tostring(model) or "no GetModel"
end
local function ApplyModel(self, skin, step)
	-- The worn skin is drawn from its display too: SetUnit("player") on a player in a creature's skin draws only
	-- the player's weapon.
	local method = methods[step]
	if not method then
		modelNote = modelNote .. "; no way left to show this skin"
		return
	end
	self:ClearModel()
	local ok, problem = pcall(method[2], self, skin)
	modelNote = (step > 1 and modelNote .. "; " or "") .. method[1] .. (ok and " ok" or (" error " .. tostring(problem)))
	self:SetFacing(self.facing)
	self:SetPosition(-(self.zoom or 0), 0, 0)
	self.checking, self.checkTime, self.checkStep, self.checkWait = skin, 0, step, method[3] or 0.5
end

preview:SetScript("OnUpdate", function(self, elapsed)
	if self.pending then
		self.pendingTime = (self.pendingTime or 0) + (elapsed or 0)
		if self.pendingTime >= 0.1 then
			local skin = self.pending
			self.pending, self.pendingTime = nil, nil
			ApplyModel(self, skin, 1)
		end
	elseif self.checking then
		-- the model loads in the background: give it a moment, then try the next way if none came
		self.checkTime = self.checkTime + (elapsed or 0)
		local has = HasModel(self)
		if has or self.checkTime >= self.checkWait then
			local skin, step = self.checking, self.checkStep
			self.checking = nil
			local has, model = HasModel(self)
			modelNote = modelNote .. " -> " .. model
			if not has then
				ApplyModel(self, skin, step + 1)
			end
		end
	end
	if self.dragX then
		local x = GetCursorPosition()
		self.facing = self.facing + (x - self.dragX) * 0.02
		self.dragX = x
		self:SetFacing(self.facing)
	end
end)

-- Size: your own size for the selected skin (50-200% of the size the game works out), kept per skin; the skin you
-- wear changes at once.
local sizeSlider = CreateFrame("Slider", "SkinCollectionSizeSlider", frame, "OptionsSliderTemplate")
sizeSlider:SetWidth(150)
-- below the preview, not on it: the preview takes the mouse to turn the model
sizeSlider:SetPoint("TOPRIGHT", preview, "BOTTOMRIGHT", -10, -18)
sizeSlider:SetFrameLevel(preview:GetFrameLevel() + 5)
sizeSlider:SetMinMaxValues(50, 200)
sizeSlider:SetValueStep(5)
_G["SkinCollectionSizeSliderLow"]:SetText("50%")
_G["SkinCollectionSizeSliderHigh"]:SetText("200%")
local sizeText = _G["SkinCollectionSizeSliderText"]
sizeSlider:SetScript("OnValueChanged", function(self, value)
	value = math.floor(value / 5 + 0.5) * 5
	sizeText:SetText(string.format("Size: %d%%", value))
	if self.settingUp or not selected or selected.size == value then return end
	selected.size = value
	self.sendAt, self.sendValue, self.sendDisplay = GetTime() + 0.3, value, selected.display
end)
sizeSlider:SetScript("OnUpdate", function(self)
	if self.sendAt and GetTime() >= self.sendAt then
		Send("SIZE;" .. self.sendDisplay .. ";" .. self.sendValue)
		self.sendAt = nil
	end
end)

local skinName = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
skinName:SetPoint("TOPLEFT", preview, "BOTTOMLEFT", 0, -10)
skinName:SetPoint("RIGHT", preview, "RIGHT", -170, 0)  -- room for the Size slider on the right
skinName:SetJustifyH("LEFT")
local skinType = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
skinType:SetPoint("TOPLEFT", skinName, "BOTTOMLEFT", 0, -4)
local echo = frame:CreateFontString(nil, "OVERLAY", "GameFontGreen")
echo:SetPoint("TOPLEFT", skinType, "BOTTOMLEFT", 0, -6)
echo:SetPoint("RIGHT", preview, "RIGHT", -170, 0)
echo:SetJustifyH("LEFT")
local stolenLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
stolenLabel:SetPoint("TOPLEFT", echo, "BOTTOMLEFT", 0, -8)
stolenLabel:SetText("Abilities stolen from it:")

local abilityButtons = {}
for index = 1, 8 do
	local button = CreateFrame("Button", nil, frame)
	button:SetSize(28, 28)
	button:SetPoint("TOPLEFT", stolenLabel, "BOTTOMLEFT", (index - 1) * 32, -4)
	button.icon = button:CreateTexture(nil, "ARTWORK")
	button.icon:SetAllPoints()
	button:SetScript("OnEnter", function(self)
		GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
		GameTooltip:SetHyperlink("spell:" .. self.spell)
		GameTooltip:Show()
	end)
	button:SetScript("OnLeave", function() GameTooltip:Hide() end)
	button:Hide()
	abilityButtons[index] = button
end
local noAbilities = frame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
noAbilities:SetPoint("TOPLEFT", stolenLabel, "BOTTOMLEFT", 0, -8)
noAbilities:SetText("None: only its skin.")

local wear = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
wear:SetSize(110, 24)
wear:SetPoint("BOTTOMRIGHT", -24, 20)
wear:SetText("Wear")
wear:SetScript("OnClick", function() if selected then Send("WEAR;" .. selected.display) end end)

-- A Possessor possesses and releases bodies; a Skinshifter wears and sheds skins.
local NameButtons  -- set below, once both buttons exist
local shed = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
shed:SetSize(110, 24)
shed:SetPoint("RIGHT", wear, "LEFT", -8, 0)
shed:SetText("Shed")
shed:SetScript("OnClick", function() Send("SHED") end)

NameButtons = function()
	local possessor = Skinshifter_IsPossessorRace and Skinshifter_IsPossessorRace(UnitRace("player"))
	wear:SetText(possessor and "Possess" or "Wear")
	shed:SetText(possessor and "Release" or "Shed")
end

local empty = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
empty:SetPoint("CENTER", list, "CENTER")
empty:SetWidth(210)
empty:SetText("Your collection is empty. Use Steal Form (or Possess) on a creature to take its skin.")

-- Filling -----------------------------------------------------------------------------------------------------
local function TypeOf(skin)
	return TYPE_NAMES[skin.type] or "Creature"
end

function ShowSelected()
	local skin = selected
	if not skin then
		preview:ClearModel()
		preview.shownSkin, preview.pending, preview.checking = nil, nil, nil
		skinName:SetText("")
		skinType:SetText("")
		echo:SetText("")
		stolenLabel:Hide()
		noAbilities:Hide()
		for _, button in ipairs(abilityButtons) do button:Hide() end
		wear:Disable()
		sizeSlider:Hide()
		return
	end
	sizeSlider.settingUp = true
	sizeSlider:SetValue(skin.size or 100)
	sizeText:SetText(string.format("Size: %d%%", skin.size or 100))
	sizeSlider.settingUp = nil
	sizeSlider:Show()
	if preview.shownSkin ~= skin then
		preview.shownSkin = skin
		preview:ClearModel()
		preview.pending, preview.pendingTime, preview.checking = skin, 0, nil
		preview.zoom = math.min(30, (skin.radius or 0) * 1.5)
	end
	skinName:SetText(skin.name .. (skin.display == worn and "  |cffff3399(worn)|r" or ""))
	skinType:SetText(string.format("%s, level %d", TypeOf(skin), skin.level))
	echo:SetText(ECHOES[skin.type] or "Nothing for this kind of creature.")
	stolenLabel:Show()
	local spells = abilities[skin.entry] or {}
	for index, button in ipairs(abilityButtons) do
		local spell = spells[index]
		if spell then
			local _, _, icon = GetSpellInfo(spell)
			button.spell = spell
			button.icon:SetTexture(icon or "Interface\\Icons\\INV_Misc_QuestionMark")
			button:Show()
		else
			button:Hide()
		end
	end
	Shown(noAbilities, #spells == 0)
	if skin.display == worn then wear:Disable() else wear:Enable() end
end

function Refresh()
	local text = strlower(search:GetText() or "")
	Shown(searchHint, text == "" and not search:HasFocus())
	wipe(filtered)
	for _, skin in ipairs(skins) do
		if text == "" or strfind(strlower(skin.name), text, 1, true) or strfind(strlower(TypeOf(skin)), text, 1, true) then
			tinsert(filtered, skin)
		end
	end
	if worldMax and worldMax > 0 then
		local inWorld = 0
		for _, skin in ipairs(skins) do
			if skin.world then inWorld = inWorld + 1 end
		end
		local beyond = #skins - inWorld
		count:SetText(string.format("%d / %d skins collected (%d%%)%s", inWorld, worldMax,
			math.floor(inWorld * 100 / worldMax), beyond > 0 and string.format("  |cff9d9d9d+%d from beyond this world|r", beyond) or ""))
	else
		count:SetText(string.format("%d skins collected", #skins))
	end
	Shown(empty, #skins == 0)
	FauxScrollFrame_Update(list, #filtered, ROWS, ROW_HEIGHT)
	local offset = FauxScrollFrame_GetOffset(list)
	for index, row in ipairs(rows) do
		local skin = filtered[index + offset]
		if skin then
			row.skin = skin
			SetOwnTexture(row.icon, IconOf(skin))
			row.name:SetText(skin.name)
			row.info:SetText(string.format("%s %d%s", TypeOf(skin), skin.level, skin.display == worn and "  |cffff3399worn|r" or ""))
			Shown(row.selectedTexture, skin == selected)
			row:Show()
		else
			row:Hide()
		end
	end
	if shed then
		if worn ~= 0 then shed:Enable() else shed:Disable() end
	end
	ShowSelected()
end

list:SetScript("OnVerticalScroll", function(self, offset)
	FauxScrollFrame_OnVerticalScroll(self, offset, ROW_HEIGHT, Refresh)
end)
search:SetScript("OnTextChanged", Refresh)
search:SetScript("OnEditFocusGained", Refresh)
search:SetScript("OnEditFocusLost", Refresh)
search:SetScript("OnEscapePressed", search.ClearFocus)
frame:SetScript("OnShow", function() preview.shownSkin = nil Send("LIST") Refresh() end)

-- Specialization window (level 10) ----------------------------------------------------------------------------
local SPECS = {
	{ name = "Beastskin", role = "Melee", signature = 1990201, passive = 1990211,
		icon = "Interface\\Icons\\Ability_Druid_CatForm",
		text = "Fight with tooth and claw in the skins of beasts. Bleeds, speed and pack instinct." },
	{ name = "Courtier", role = "Healer and disguise", signature = 1990202, passive = 1990212,
		icon = "Interface\\Icons\\Spell_Holy_FlashHeal",
		text = "Heal with the arts of the healers you study, and walk among people in their faces." },
	{ name = "Felstriker", role = "Caster: demons and the dead", signature = 1990203, passive = 1990213,
		icon = "Interface\\Icons\\Spell_Shadow_LifeDrain02",
		text = "Wield the fire and shadow of demons and the cold of the dead. Drains and curses." },
	{ name = "Elementalist", role = "Caster: elementals and dragons", signature = 1990204, passive = 1990214,
		icon = "Interface\\Icons\\Spell_Nature_Lightning",
		text = "Wear fire, water, earth and air, and the breath of dragons. Every element you hold makes you stronger." },
}
-- advancement: the server sets the specialization in CoA's Character Advancement (Skinshifter.Advancement.Enable);
-- then N is CoA's own talent window and this window only shows what you chose there.
local specState = { current = 0, level = 10, cost = 0, advancement = false }

local specFrame = CreateFrame("Frame", "SkinshifterSpecFrame", UIParent)
specFrame:SetSize(760, 330)
specFrame:SetPoint("CENTER", 0, 60)
specFrame:SetFrameStrata("DIALOG")
specFrame:SetMovable(true)
specFrame:EnableMouse(true)
specFrame:RegisterForDrag("LeftButton")
specFrame:SetScript("OnDragStart", specFrame.StartMoving)
specFrame:SetScript("OnDragStop", specFrame.StopMovingOrSizing)
specFrame:SetBackdrop(DIALOG_BACKDROP)
specFrame:Hide()
tinsert(UISpecialFrames, "SkinshifterSpecFrame")
local specTitle = specFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
specTitle:SetPoint("TOP", 0, -18)
specTitle:SetText("Choose your specialization")
local specClose = CreateFrame("Button", nil, specFrame, "UIPanelCloseButton")
specClose:SetPoint("TOPRIGHT", -6, -6)
local specNote = specFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
specNote:SetPoint("BOTTOM", 0, 18)

local function SpellLine(spell)
	local name, _, icon = GetSpellInfo(spell)
	return name and ("|T" .. icon .. ":16|t " .. name) or ("spell " .. spell)
end

local cards = {}
for index, spec in ipairs(SPECS) do
	local card = CreateFrame("Frame", nil, specFrame)
	card:SetSize(172, 240)
	card:SetPoint("TOPLEFT", 22 + (index - 1) * 180, -48)
	card:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
		tile = true, tileSize = 16, edgeSize = 16, insets = { left = 4, right = 4, top = 4, bottom = 4 } })
	card:SetBackdropColor(0, 0, 0, 0.6)
	card.icon = card:CreateTexture(nil, "ARTWORK")
	card.icon:SetSize(40, 40)
	card.icon:SetPoint("TOP", 0, -12)
	SetOwnTexture(card.icon, "Interface\\Icons\\Skinshifter_" .. spec.name, spec.icon)
	card.name = card:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
	card.name:SetPoint("TOP", card.icon, "BOTTOM", 0, -6)
	card.name:SetText(spec.name)
	card.role = card:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	card.role:SetPoint("TOP", card.name, "BOTTOM", 0, -2)
	card.role:SetText(spec.role)
	card.text = card:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	card.text:SetPoint("TOPLEFT", 10, -100)
	card.text:SetPoint("TOPRIGHT", -10, -100)
	card.text:SetHeight(56)  -- room for four lines, so the description wraps instead of being cut off
	card.text:SetJustifyH("LEFT")
	card.text:SetJustifyV("TOP")
	card.text:SetText(spec.text)
	card.spells = card:CreateFontString(nil, "OVERLAY", "GameFontGreenSmall")
	card.spells:SetPoint("TOPLEFT", card.text, "BOTTOMLEFT", 0, -8)
	card.spells:SetPoint("RIGHT", -10, 0)
	card.spells:SetJustifyH("LEFT")
	card.choose = CreateFrame("Button", nil, card, "UIPanelButtonTemplate")
	card.choose:SetSize(120, 22)
	card.choose:SetPoint("BOTTOM", 0, 10)
	card.choose:SetScript("OnClick", function() Send("SPEC;" .. index) end)
	cards[index] = card
end

local function RefreshSpecs()
	local level = UnitLevel("player")
	for index, card in ipairs(cards) do
		local spec = SPECS[index]
		card.spells:SetText(SpellLine(spec.signature) .. "\n" .. SpellLine(spec.passive))
		if index == specState.current then
			card.choose:SetText("Chosen")
			card.choose:Disable()
			card:SetBackdropBorderColor(1, 0.2, 0.6)
		else
			card.choose:SetText(specState.current == 0 and "Choose" or "Change")
			if level >= specState.level and not specState.advancement then card.choose:Enable() else card.choose:Disable() end
			card:SetBackdropBorderColor(0.6, 0.6, 0.6)
		end
	end
	if specState.advancement then
		specNote:SetText("Choose your specialization in Character Advancement (N): buy its signature in the class tree.")
	elseif level < specState.level then
		specNote:SetText(string.format("You can choose at level %d.", specState.level))
	elseif specState.current == 0 then
		specNote:SetText("Your first choice is free. Stolen abilities stay whatever you choose.")
	elseif specState.cost > 0 then
		specNote:SetText(string.format("Changing costs %s and works out of combat.", GetCoinTextureString(specState.cost)))
	else
		specNote:SetText("You can change out of combat.")
	end
end
specFrame:SetScript("OnShow", function() Send("SPECS") RefreshSpecs() end)

local specButton = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
specButton:SetSize(120, 24)
specButton:SetPoint("BOTTOMLEFT", 24, 20)
specButton:SetText("Specialization")
specButton:SetScript("OnClick", function() if specFrame:IsShown() then specFrame:Hide() else specFrame:Show() end end)
specButton:Hide()  -- the spec is chosen in Character Advancement now (its signature in the class tree)

SLASH_SKINSHIFTERSPEC1 = "/skinspec"
SlashCmdList.SKINSHIFTERSPEC = function()
	if specFrame:IsShown() then specFrame:Hide() else specFrame:Show() end
end

-- Talents (N) ----------------------------------------------------------------------------------------------------
-- With Character Advancement (the server says so in its SPEC line) N is CoA's own window: the client patch makes
-- IsCustomClass true for a Skinshifter, so CoA opens its CoA-class talents. Without it, CoA would open the classic
-- talent window (class 33 has no class bit; the game reads it as the Warrior bit), so a Skinshifter's N opens the
-- specialization window instead; Shift+N still opens Collections, as for every class.
local function IsSkinshifter()
	local _, file = UnitClass("player")
	return file == "SKINSHIFTER"
end
-- Only replaced for a Skinshifter, so no other class runs CoA's window code through this addon.
local function TakeOverTalentKey()
	if not ToggleCollections or not IsSkinshifter() then
		return
	end
	local originalToggleCollections = ToggleCollections
	ToggleCollections = function(...)
		if not specState.advancement and not IsShiftKeyDown() and not (Collections and Collections:IsShown()) then
			if specFrame:IsShown() then specFrame:Hide() else specFrame:Show() end
			return
		end
		return originalToggleCollections(...)
	end
end
-- Any other way into the Warrior trees (a micro button of another addon) lands in the specialization window too.
local talentGuard = CreateFrame("Frame")
talentGuard:RegisterEvent("ADDON_LOADED")
talentGuard:RegisterEvent("PLAYER_LOGIN")
talentGuard:SetScript("OnEvent", function(self, event)
	if event == "PLAYER_LOGIN" then
		TakeOverTalentKey()
	end
	if self.hooked or not PlayerTalentFrame then
		return
	end
	self.hooked = true
	PlayerTalentFrame:HookScript("OnShow", function(talents)
		if IsSkinshifter() and not talents.pet then
			HideUIPanel(talents)  -- the Warrior trees are never a Skinshifter's
			if not specState.advancement then
				specFrame:Show()
			end
		end
	end)
end)

-- Messages from the server -----------------------------------------------------------------------------------
local function Fields(message)
	local fields = {}
	for field in string.gmatch(message .. ";", "([^;]*);") do
		tinsert(fields, field)
	end
	return fields
end

local events = CreateFrame("Frame")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:SetScript("OnEvent", function(_, event, prefix, message)
	if event == "PLAYER_ENTERING_WORLD" then
		ShowFlavourPicture()
		NameButtons()
		Send("HELLO")
		Send("SPECS")
		Send("SLOTS")
		Send("TOKENS")
		return
	end
	if prefix ~= PREFIX then return end
	local fields = Fields(message)
	local kind = fields[1]
	if kind == "BEGIN" then
		incoming, incomingAbilities = {}, {}
	elseif kind == "S" and incoming then
		tinsert(incoming, { display = tonumber(fields[2]), entry = tonumber(fields[3]), level = tonumber(fields[4]) or 0,
			type = tonumber(fields[5]) or 0, name = fields[6] or "?", family = tonumber(fields[7]) or 0,
			radius = tonumber(fields[8]) or 0, size = tonumber(fields[9]) or 100, character = fields[10] == "1",
			world = fields[11] ~= "0" })  -- an older server sends no flag: every skin counts
	elseif kind == "A" and incomingAbilities then
		local entry, spell = tonumber(fields[2]), tonumber(fields[3])
		incomingAbilities[entry] = incomingAbilities[entry] or {}
		tinsert(incomingAbilities[entry], spell)
	elseif kind == "MAX" then
		worldMax = tonumber(fields[2])
	elseif kind == "END" and incoming then
		skins, abilities, worn = incoming, incomingAbilities, tonumber(fields[2]) or 0
		preview.shownSkin = nil
		incoming, incomingAbilities = nil, nil
		local keep
		for _, skin in ipairs(skins) do
			if selected and skin.display == selected.display then keep = skin end
		end
		selected = keep or skins[1]
		if frame:IsShown() then Refresh() end
		if SkinSlots_OnMessage then SkinSlots_OnMessage(fields) end
	elseif kind == "WORN" then
		worn = tonumber(fields[2]) or 0
		if frame:IsShown() then Refresh() end
	elseif kind == "SPEC" then
		specState.current = tonumber(fields[2]) or 0
		specState.level = tonumber(fields[3]) or 10
		specState.cost = tonumber(fields[4]) or 0
		specState.advancement = fields[5] == "1"
		if specFrame:IsShown() then RefreshSpecs() end
		Send("TOKENS")  -- the token ring takes the spec's colour
	elseif kind == "SPECOFFER" then
		if not specState.advancement then specFrame:Show() end
	elseif kind == "OPEN" then
		if frame:IsShown() then frame:Hide() else frame:Show() end
	elseif kind == "SLOTDEF" or kind == "SLOT" or kind == "SLOTEND" then
		if SkinSlots_OnMessage then SkinSlots_OnMessage(fields) end
	elseif kind == "REINS" then
		if SkinReins_OnMessage then SkinReins_OnMessage(fields) end
	elseif kind == "TIP" or kind == "TIPEND" then
		if SkinTooltips_OnMessage then SkinTooltips_OnMessage(fields) end
	elseif kind == "TOKENS" or kind == "TOKENMOB" or kind == "TOKENEND" or kind == "TOKENNEW" or kind == "TAKEOVER" or
		kind == "TAKEOVEREND" then
		if SkinTokens_OnMessage then SkinTokens_OnMessage(fields) end
	end
end)

-- For SkinSlots.lua (the slot picker of the talent window): the messages and the stolen abilities.
SkinCollectionShared = {
	Send = Send,
	Abilities = function() return skins, abilities end,
}

SLASH_SKINCOLLECTION1 = "/skins"
SLASH_SKINCOLLECTION2 = "/skincollection"
SlashCmdList.SKINCOLLECTION = function(text)
	if text == "debug" then
		print(string.format("Skin Collection model (zoom %.2f): SetDisplayInfo=%s SetCreature=%s SetUnit=%s; last: %s; shown=%s size=%dx%d",
			preview.zoom or 0, tostring(preview.SetDisplayInfo ~= nil), tostring(preview.SetCreature ~= nil), tostring(preview.SetUnit ~= nil),
			modelNote, tostring(preview:IsVisible()), preview:GetWidth() or 0, preview:GetHeight() or 0))
		local tabs = {}
		for tab = 1, GetNumSpellTabs() do
			local name, _, _, count = GetSpellTabInfo(tab)
			tabs[#tabs + 1] = string.format("%s(%d)", tostring(name), count or 0)
		end
		local ours = { Skinshifter = true, Beastskin = true, Courtier = true, Felstriker = true, Elementalist = true, Skins = true,
			Beast = true, Humanoid = true, Undead = true, Demon = true, Elemental = true, Dragonkin = true, Giant = true,
			Mechanical = true, Other = true }
		local skills = {}
		for index = 1, GetNumSkillLines() do
			local name, header = GetSkillLineInfo(index)
			if not header and name and ours[name] then
				skills[#skills + 1] = name
			end
		end
		print("Spellbook tabs: " .. table.concat(tabs, ", ") .. ". Skinshifter skills: " ..
			(#skills > 0 and table.concat(skills, ", ") or "none"))
		-- which tab each spell is shown in (the first spells of every tab)
		for tab = 1, GetNumSpellTabs() do
			local name, _, offset, count = GetSpellTabInfo(tab)
			local spells = {}
			for index = offset + 1, offset + math.min(count or 0, 6) do
				spells[#spells + 1] = tostring(GetSpellName(index, BOOKTYPE_SPELL))
			end
			print("  " .. tostring(name) .. ": " .. table.concat(spells, ", "))
		end
		local usable, noMana = IsUsableSpell("Lure")
		print("Lure: usable " .. tostring(usable) .. ", no mana " .. tostring(noMana) .. ", waiting for a target " ..
			tostring(SpellIsTargeting()))
		return
	end
	if frame:IsShown() then frame:Hide() else frame:Show() end
end

-- Portraits. The Root Walk look (a tiny plant or worm) turns a 3D portrait into stripes (the portrait camera ends
-- up inside the leaves), so a unit in Root Walking shows the Root Walking icon in its portrait instead: the player
-- frame, the target frame and every other frame that draws portraits the usual way.
local ICON_PORTRAIT_AURAS = { 1990018 }  -- Root Walking

local function PortraitIcon(unit)
	for _, id in ipairs(ICON_PORTRAIT_AURAS) do
		local name, _, icon = GetSpellInfo(id)
		if name and UnitAura(unit, name) then return icon end
	end
end

hooksecurefunc("SetPortraitTexture", function(texture, unit)
	if not texture or not unit or not UnitExists(unit) then return end
	local icon = PortraitIcon(unit)
	if icon then SetPortraitToTexture(texture, icon) end
end)

-- The portrait is drawn again when the aura comes or goes (the look can change before the aura is there).
local portraitWatch = CreateFrame("Frame")
local hadIcon = {}
portraitWatch:RegisterEvent("UNIT_AURA")
portraitWatch:SetScript("OnEvent", function(_, _, unit)
	if unit ~= "player" and unit ~= "target" and unit ~= "focus" then return end
	local has = PortraitIcon(unit) ~= nil
	if has == (hadIcon[unit] or false) then return end
	hadIcon[unit] = has
	local portrait = (unit == "player" and PlayerPortrait) or (unit == "target" and TargetFramePortrait) or
		(unit == "focus" and FocusFramePortrait)
	if portrait then SetPortraitTexture(portrait, unit) end
end)
