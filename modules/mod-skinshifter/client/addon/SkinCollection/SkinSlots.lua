-- The slots of the Skinshifter's talent trees (CoA's Character Advancement window, N).
-- A slot node (an empty square: an active ability) holds a stolen ability. Point at it and a list of the stolen
-- abilities it takes (by the creature type they were stolen from) opens next to it; click one to put it in the slot.
-- An unlearned slot is learned with it (1 point, saved with Save Changes like any talent). The node then shows the
-- ability's icon. The server keeps the slots (mod-skinshifter, SkinshifterSlots.cpp).

local slotOfEntry = {}   -- [CA entry id] = { slot = n, types = mask, name = text }
local slots = {}         -- [slot] = { spell = id, open = bool }
local wantList = false

local function Shared() return SkinCollectionShared end

local function Shown(region, shown)
	if shown then region:Show() else region:Hide() end
end

local function TypeBit(creatureType)
	if not creatureType or creatureType == 0 then return 2147483648 end  -- "Other": the server's bit 31
	return 2 ^ (creatureType - 1)
end

local function HasBit(mask, bit)
	return mask and bit and (mask % (bit * 2)) >= bit
end

-- The entry id a talent window button keeps (CoA's buttons keep their entry; the field name is looked for).
local function OwnEntryId(button)
	if type(button) ~= "table" then return nil end
	for _, key in ipairs({ "entry", "entryInfo", "Entry", "nodeInfo" }) do
		local value = rawget(button, key)
		if type(value) == "table" then
			local id = value.ID or value.EntryID or value.entryID or value.Id
			if type(id) == "number" then return id end
		end
	end
	local id = rawget(button, "entryID") or rawget(button, "EntryID")
	return type(id) == "number" and id or nil
end

-- The node button under the mouse: the button itself or the button of one of its parts.
local function NodeOf(frame)
	for _ = 1, 4 do
		if not frame then return nil end
		if OwnEntryId(frame) then return frame end
		frame = frame.GetParent and frame:GetParent()
	end
	return nil
end

local function EntryIdOf(frame)
	local node = NodeOf(frame)
	return node and OwnEntryId(node)
end

-- Why the game will not learn a node now (C_CharacterAdvancement.CanAddByEntryID's reason, Enum.CALearnResult).
local LEARN_REASONS = {
	CA_LEARN_LOW_LEVEL = "your level is too low for it",
	CA_LEARN_MISSING_TE = "you have no talent points left",
	CA_LEARN_MISSING_AE = "you have no ability points left",
	CA_LEARN_MISSING_CONNECTED_ENTRIES = "learn a node leading to it first",
	CA_LEARN_NOT_ENOUGH_INVESTED_TE = "spend more points in this tree first",
	CA_LEARN_NOT_ENOUGH_INVESTED_AE = "spend more points in this tree first",
	CA_LEARN_NOT_IN_COMBAT = "not in combat",
	CA_LEARN_NOT_WHILE_DEAD = "not while dead",
	CA_LEARN_GROUP = "you took the other choice here",
	CA_LEARN_WRONG_CLASS = "it is not for your class or spec",
}

-- 0 = learned or pending, true = can be learned now, or the text of why not.
local function LearnState(button)
	local entry = button and button.entry
	local ca = C_CharacterAdvancement
	if not (entry and ca and ca.GetPendingRankByEntryID and ca.CanAddByEntryID) then return nil end
	if (ca.GetPendingRankByEntryID(entry.ID) or 0) > 0 then return 0 end
	local canLearn, reason = ca.CanAddByEntryID(entry.ID, 1)
	if canLearn then return true end
	reason = reason and tostring(reason) or "unknown"
	local global = _G[reason]
	return LEARN_REASONS[reason] or (type(global) == "string" and global) or reason
end

-- ---- the picker -----------------------------------------------------------------------------------------------
local ROWS, ROW_HEIGHT = 12, 22
local picker = CreateFrame("Frame", "SkinSlotPicker", UIParent)
picker:SetSize(250, 64 + ROWS * ROW_HEIGHT)
picker:SetFrameStrata("FULLSCREEN_DIALOG")  -- under the tooltip strata: an ability's tooltip shows on top of it
picker:SetClampedToScreen(true)
picker:SetBackdrop({
	bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
	edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
	tile = true, tileSize = 16, edgeSize = 16,
	insets = { left = 4, right = 4, top = 4, bottom = 4 },
})
picker:SetBackdropColor(0, 0, 0, 0.92)
picker:EnableMouse(true)
picker:EnableMouseWheel(true)
picker:Hide()

local pickerTitle = picker:CreateFontString(nil, "OVERLAY", "GameFontNormal")
pickerTitle:SetPoint("TOPLEFT", 10, -10)
pickerTitle:SetPoint("RIGHT", -10, 0)
pickerTitle:SetJustifyH("LEFT")
local pickerNote = picker:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
pickerNote:SetPoint("TOPLEFT", pickerTitle, "BOTTOMLEFT", 0, -3)
pickerNote:SetPoint("RIGHT", -10, 0)
pickerNote:SetJustifyH("LEFT")

-- Search: every word must be in the name or the description.
local SEARCH_HEIGHT = 20
local pickerSearch = CreateFrame("EditBox", "SkinSlotPickerSearch", picker, "InputBoxTemplate")
pickerSearch:SetSize(220, SEARCH_HEIGHT)
pickerSearch:SetPoint("TOPLEFT", pickerNote, "BOTTOMLEFT", 6, -4)
pickerSearch:SetAutoFocus(false)
pickerSearch:SetFontObject(ChatFontSmall)
local searchHint = pickerSearch:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
searchHint:SetPoint("LEFT", 2, 0)
searchHint:SetText("Search...")
pickerSearch:SetScript("OnEscapePressed", function(self) self:SetText("") self:ClearFocus() end)
pickerSearch:SetScript("OnEnterPressed", function(self) self:ClearFocus() end)

local rows = {}
for index = 1, ROWS do
	local row = CreateFrame("Button", nil, picker)
	row:SetSize(230, ROW_HEIGHT - 2)
	if index == 1 then
		row:SetPoint("TOPLEFT", pickerSearch, "BOTTOMLEFT", -6, -4)  -- below the search box
	else
		row:SetPoint("TOPLEFT", rows[index - 1], "BOTTOMLEFT", 0, -2)
	end
	row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
	row.current = row:CreateTexture(nil, "BACKGROUND")
	row.current:SetAllPoints()
	row.current:SetTexture(0.2, 0.6, 0.2, 0.35)
	row.icon = row:CreateTexture(nil, "ARTWORK")
	row.icon:SetSize(18, 18)
	row.icon:SetPoint("LEFT", 2, 0)
	row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	row.text:SetPoint("LEFT", row.icon, "RIGHT", 6, 0)
	row.text:SetPoint("RIGHT", -2, 0)
	row.text:SetJustifyH("LEFT")
	row:SetScript("OnClick", function(self)
		if not (picker.slot and self.spell) then return end
		Shared().Send("SLOT;" .. picker.slot .. ";" .. self.spell)
		-- choosing in an unlearned slot learns it (pending, like a click on the node)
		if self.spell > 0 and LearnState(picker.button) == true then
			if CharacterAdvancementUtil and CharacterAdvancementUtil.MarkForSwap then CharacterAdvancementUtil.MarkForSwap(nil) end
			C_CharacterAdvancement.AddByEntryID(picker.button.entry.ID, 1)
			PlaySound(SOUNDKIT and SOUNDKIT.UI_CLASS_TALENT_NODE_SPEND or "igMainMenuOptionCheckBoxOn")
		end
	end)
	row:SetScript("OnEnter", function(self)
		if self.spell and self.spell > 0 then
			GameTooltip:SetOwner(picker, "ANCHOR_NONE")
			-- beside the list, on the side with room, so the tooltip never lies over the list
			if (picker:GetRight() or 0) * picker:GetEffectiveScale() + 320 > GetScreenWidth() * UIParent:GetEffectiveScale() then
				GameTooltip:SetPoint("TOPRIGHT", picker, "TOPLEFT", -2, 0)
			else
				GameTooltip:SetPoint("TOPLEFT", picker, "TOPRIGHT", 2, 0)
			end
			GameTooltip:SetHyperlink("spell:" .. self.spell)
			GameTooltip:Show()
		end
	end)
	row:SetScript("OnLeave", function() GameTooltip:Hide() end)
	rows[index] = row
end

local function Matches(choice, words)
	if #words == 0 then return true end
	local text = strlower(choice.name .. " " .. (GetSpellDescription and GetSpellDescription(choice.spell) or ""))
	for _, word in ipairs(words) do
		if not text:find(word, 1, true) then return false end
	end
	return true
end

-- The stolen abilities a slot takes, one of each name, sorted by name, with the empty choice first. Of a name the
-- one in the slot is kept, else the one you know (the server keeps only the strongest in your book).
local function Choices(types, slotted, filter)
	local list, byName = { { spell = 0, name = "(empty the slot)" } }, {}
	local words = {}
	for word in string.gmatch(strlower(filter or ""), "%S+") do tinsert(words, word) end
	local skins, abilities = Shared().Abilities()
	local typeOfEntry = {}
	for _, skin in ipairs(skins or {}) do typeOfEntry[skin.entry] = skin.type end
	local function rank(spell)
		if spell == slotted then return 3 end
		if IsSpellKnown and IsSpellKnown(spell) then return 2 end
		return 1
	end
	for entry, spells in pairs(abilities or {}) do
		if HasBit(types, TypeBit(typeOfEntry[entry])) or types >= 4294967295 then
			for _, spell in ipairs(spells) do
				local name, _, icon = GetSpellInfo(spell)
				name = name or ("Spell " .. spell)
				local held = byName[name]
				if not held or rank(spell) > rank(held.spell) then
					byName[name] = { spell = spell, name = name, icon = icon }
				end
			end
		end
	end
	for _, choice in pairs(byName) do
		if Matches(choice, words) then tinsert(list, choice) end
	end
	table.sort(list, function(a, b)
		if a.spell == 0 or b.spell == 0 then return a.spell == 0 and b.spell ~= 0 end
		return a.name < b.name
	end)
	return list
end

local function RefreshPicker()
	local def = picker.def
	if not def then return end
	local state = slots[def.slot] or {}
	pickerTitle:SetText(def.name)
	local list = Choices(def.types, state.spell, pickerSearch:GetText())
	Shown(searchHint, pickerSearch:GetText() == "" and not pickerSearch:HasFocus())
	picker.list = list
	picker.offset = math.max(0, math.min(picker.offset or 0, #list - ROWS))
	local learn = LearnState(picker.button)
	if #list <= 1 then
		pickerNote:SetText(wantList and "Loading your Grimoire..." or "You have stolen no ability it takes yet.")
	elseif not state.open and learn == 0 then
		pickerNote:SetText("|cff80ff80Save Changes to learn the slot.|r")
	elseif not state.open and learn == true then
		pickerNote:SetText("Choose an ability: it costs 1 point.")
	elseif not state.open and learn then
		pickerNote:SetText("|cffff6060Can't learn the slot yet: " .. learn .. ".|r Your choice waits in it.")
	else
		pickerNote:SetText("Click an ability to put it in the slot.")
	end
	for index, row in ipairs(rows) do
		local choice = list[index + picker.offset]
		if choice then
			row.spell = choice.spell
			row.icon:SetTexture(choice.icon or (choice.spell == 0 and "Interface\\Buttons\\UI-EmptySlot-Disabled") or
				"Interface\\Icons\\INV_Misc_QuestionMark")
			row.text:SetText(choice.name)
			Shown(row.current, state.spell == choice.spell)
			row:Show()
		else
			row.spell = nil
			row:Hide()
		end
	end
	picker:SetHeight(10 + pickerTitle:GetStringHeight() + 3 + pickerNote:GetStringHeight() + 4 + SEARCH_HEIGHT + 4
		+ math.min(ROWS, #list) * ROW_HEIGHT + 8)
end

picker:SetScript("OnMouseWheel", function(self, delta)
	self.offset = (self.offset or 0) - delta
	RefreshPicker()
end)

pickerSearch:SetScript("OnTextChanged", function() picker.offset = 0 RefreshPicker() end)
pickerSearch:SetScript("OnEditFocusGained", function() RefreshPicker() end)
pickerSearch:SetScript("OnEditFocusLost", function() RefreshPicker() end)

local function ShowPicker(button, def)
	if picker.def ~= def or picker.button ~= button then picker.offset = 0 end
	picker.def, picker.slot, picker.button = def, def.slot, button
	local skins = Shared().Abilities()
	if (not skins or #skins == 0) and not wantList then
		wantList = true
		Shared().Send("LIST")
	end
	picker:ClearAllPoints()
	picker:SetPoint("TOPLEFT", button, "TOPRIGHT", 6, 0)
	RefreshPicker()
	picker:Show()
end

-- ---- the nodes: the slotted ability's icon on its node --------------------------------------------------------
local function ForEachButton(parent, visit, depth)
	depth = depth or 0
	if depth > 16 or not parent or not parent.GetChildren then return end
	for _, child in ipairs({ parent:GetChildren() }) do
		visit(child)
		ForEachButton(child, visit, depth + 1)
	end
end

-- The node shows what is in the slot (CoA's buttons draw their icon from a spell: Icon:SetSpell).
local function ShowSlotIcon(button)
	local id = OwnEntryId(button)
	local def = id and slotOfEntry[id]
	if not def or not button.Icon or not button.Icon.SetSpell then return end
	local state = slots[def.slot]
	local spell = state and state.spell and state.spell > 0 and state.spell
	if spell and button.Icon.GetSpellID and button.Icon:GetSpellID() ~= spell then
		button.Icon:SetSpell(spell)
	elseif not spell and button.spellID and button.Icon.GetSpellID and button.Icon:GetSpellID() ~= button.spellID then
		button.Icon:SetSpell(button.spellID)
	end
end

-- Every node redraws its icon from its spell (UpdateIcon); ours put the slotted ability back right after. CoA's
-- buttons got their own copy of UpdateIcon when they were made (Mixin copies functions), so each slot node button is
-- hooked itself, once, as it is found.
local slotButtons = {}

-- The ability in a slot node's slot (nil: empty).
local function SlottedSpell(button)
	local id = OwnEntryId(button)
	local def = id and slotOfEntry[id]
	local state = def and slots[def.slot]
	return state and state.spell and state.spell > 0 and state.spell or nil
end

-- Picks a spell up by its id: from its spellbook slot (the 3.3.5 way), else by id where the client takes one.
local function PickupSpellById(spell)
	local slot = FindSpellBookSlotByID and FindSpellBookSlotByID(spell)
	if slot then
		PickupSpell(slot, BOOKTYPE_SPELL)
		return true
	end
	local name = GetSpellInfo(spell)
	if name and pcall(PickupSpell, name) and GetCursorInfo() then
		return true
	end
	print("|cffff8080" .. (name or ("Spell " .. spell)) .. " is not in your spellbook, so it cannot go on a bar.|r")
	return false
end

local function TakeSlotButton(button)
	local id = OwnEntryId(button)
	if not (id and slotOfEntry[id]) or slotButtons[button] then return end
	slotButtons[button] = true
	if type(button.UpdateIcon) == "function" then hooksecurefunc(button, "UpdateIcon", ShowSlotIcon) end
	-- dragging the node puts the slotted ability on a bar (not the slot's own marker spell)
	if button.RegisterForDrag then button:RegisterForDrag("LeftButton") end
	button:HookScript("OnDragStart", function(self)
		local spell = SlottedSpell(self)
		if not spell or InCombatLockdown() then return end
		ClearCursor()
		PickupSpellById(spell)
	end)
	-- its tooltip says what is in the slot
	button:HookScript("OnEnter", function(self)
		if not GameTooltip:IsOwned(self) then return end
		local spell = SlottedSpell(self)
		GameTooltip:AddLine(" ")
		if spell then
			local name, _, icon = GetSpellInfo(spell)
			GameTooltip:AddLine("In the slot: |T" .. (icon or "") .. ":16|t " .. (name or ("Spell " .. spell)), 0.6, 1, 0.6)
			GameTooltip:AddLine("Drag this node to your action bar to use it.", 0.7, 0.7, 0.7, true)
		else
			GameTooltip:AddLine("The slot is empty.", 0.7, 0.7, 0.7)
		end
		GameTooltip:Show()
	end)
end

local function RefreshNodes()
	local window = _G.CoATalentFrame
	if window and window:IsShown() then ForEachButton(window, TakeSlotButton) end
	for button in pairs(slotButtons) do ShowSlotIcon(button) end
end

-- A Possessor's class tree says Possessor (CoA writes the class name).
local function PossessorLabel()
	local tree = _G.CoATalentFrame and CoATalentFrame.TreeView and CoATalentFrame.TreeView.ClassTree
	local label = tree and tree.Label
	if label and label:GetText() == "SKINSHIFTER" and Skinshifter_IsPossessorRace
		and Skinshifter_IsPossessorRace(UnitRace("player")) then
		label:SetText("POSSESSOR")
	end
end

-- ---- watching the window --------------------------------------------------------------------------------------
local watcher = CreateFrame("Frame")
local elapsedNodes, leftAt = 0, nil
watcher:SetScript("OnUpdate", function(_, elapsed)
	local window = _G.CoATalentFrame
	if not window or not window:IsShown() then
		if picker:IsShown() then pickerSearch:ClearFocus() picker:Hide() end
		return
	end
	elapsedNodes = elapsedNodes + elapsed
	if elapsedNodes > 0.5 then
		elapsedNodes = 0
		RefreshNodes()
		PossessorLabel()
	end
	local focus = GetMouseFocus()
	if pickerSearch:HasFocus() or (focus and (focus == picker or (focus.GetParent and focus:GetParent() == picker))) then
		leftAt = nil
		return
	end
	local id = focus and EntryIdOf(focus)
	local def = id and slotOfEntry[id]
	if def then
		leftAt = nil
		local node = NodeOf(focus)
		TakeSlotButton(node)
		ShowSlotIcon(node)
		if not picker:IsShown() or picker.def ~= def or picker.button ~= node then
			if picker.def ~= def then pickerSearch:SetText("") end  -- a new slot starts unfiltered
			ShowPicker(node, def)
		end
		return
	end
	if picker:IsShown() then
		leftAt = leftAt or GetTime()
		if GetTime() - leftAt > 0.4 then pickerSearch:ClearFocus() picker:Hide() end
	end
end)

-- ---- messages (from SkinCollection.lua) -----------------------------------------------------------------------
function SkinSlots_OnMessage(fields)
	local kind = fields[1]
	if kind == "SLOTDEF" then
		local entry, slot = tonumber(fields[2]), tonumber(fields[3])
		if entry and slot then
			slotOfEntry[entry] = { slot = slot, types = tonumber(fields[4]) or 0, name = fields[5] or "Slot" }
		end
	elseif kind == "SLOT" then
		local slot = tonumber(fields[2])
		if slot then slots[slot] = { spell = tonumber(fields[3]) or 0, open = fields[4] == "1" } end
	elseif kind == "SLOTEND" then
		RefreshNodes()
		if picker:IsShown() then RefreshPicker() end
	elseif kind == "END" then
		wantList = false
		if picker:IsShown() then RefreshPicker() end
	end
end

-- /skinslots: what the picker knows (for when it does not open).
SLASH_SKINSLOTS1 = "/skinslots"
SlashCmdList.SKINSLOTS = function()
	local defs, filled = 0, 0
	for _ in pairs(slotOfEntry) do defs = defs + 1 end
	for _, state in pairs(slots) do if state.spell and state.spell > 0 then filled = filled + 1 end end
	local skins = Shared().Abilities()
	local focus = GetMouseFocus()
	local buttons = 0
	for _ in pairs(slotButtons) do buttons = buttons + 1 end
	local node = focus and NodeOf(focus)
	local learn = node and LearnState(node)
	local unlearn = "-"
	local ca = C_CharacterAdvancement
	if node and node.entry and ca and ca.CanRemoveByEntryID then
		local ok, reason = ca.CanRemoveByEntryID(node.entry.ID)
		unlearn = ok and "yes" or (reason and (type(_G[tostring(reason)]) == "string" and _G[tostring(reason)] or
			tostring(reason)) or "no")
	end
	-- the pointed node's icon against what is in its slot (the icon should be the slotted ability's)
	local id = node and OwnEntryId(node)
	local def = id and slotOfEntry[id]
	if node and node.Icon then
		local shown = node.Icon.GetSpellID and node.Icon:GetSpellID()
		local state = def and slots[def.slot]
		local held = state and state.spell or 0
		print(string.format("Node %s: own spell %s, icon shows spell %s (%s), slot %s holds %s (%s), node type %s.",
			tostring(id), tostring(node.spellID), tostring(shown), tostring(shown and GetSpellTexture(shown)),
			tostring(def and def.slot or "-"), tostring(held), tostring(held > 0 and GetSpellTexture(held) or "-"),
			node.nodes and "choice" or "talent"))
	end
	print(string.format("Skin slots: %d slot nodes known, %d found in the window, %d filled, %d skins in the Grimoire "
		.. "list, talent window %s, mouse over entry %s, learn: %s, unlearn: %s.", defs, buttons, filled, skins and #skins or 0,
		(_G.CoATalentFrame and CoATalentFrame:IsShown()) and "open" or "closed", tostring(node and OwnEntryId(node)),
		learn == 0 and "learned" or tostring(learn), unlearn))
	Shared().Send("SLOTS")
end
