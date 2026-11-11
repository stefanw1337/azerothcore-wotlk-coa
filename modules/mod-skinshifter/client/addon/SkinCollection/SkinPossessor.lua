-- The Possessor's spectral grey in game. The class colour comes from RAID_CLASS_COLORS by class file, and
-- Possessor and Skinshifter share one (SKINSHIFTER, pink), so the grey is put in where the class is written:
-- the character sheet, unit tooltips and names in chat.

local HEX = SKINSHIFTER_POSSESSOR_HEX or "a8b5c7"
local NAME = SKINSHIFTER_POSSESSOR_NAME or "Possessor"

local function IsPossessor(raceName, raceFile)
	return Skinshifter_IsPossessorRace and Skinshifter_IsPossessorRace(raceName, raceFile) and true or false
end

local function Recoloured(text)
	if not text or not text:find(NAME, 1, true) then return text end
	local out, count = text:gsub("|c%x%x%x%x%x%x%x%x([^|]-" .. NAME .. ")", "|cff" .. HEX .. "%1")
	if count == 0 then
		out = text:gsub(NAME, "|cff" .. HEX .. NAME .. "|r")
	end
	return out
end

-- Character sheet: CoA writes "Level 60 <race> <class>" into ClassLevelLabel; recoloured right after.
local hookedLabel
local function HookSheet()
	local label = CharacterFrame and CharacterFrame.ClassLevelLabel
	if not label or hookedLabel == label then return end
	hookedLabel = label
	local busy
	local function fix(self)
		if busy or not IsPossessor(UnitRace("player")) then return end
		busy = true
		self:SetText(Recoloured(self:GetText()))
		busy = false
	end
	hooksecurefunc(label, "SetFormattedText", fix)
	hooksecurefunc(label, "SetText", fix)
	fix(label)
end
HookSheet()

-- Unit tooltips: the line with the class.
GameTooltip:HookScript("OnTooltipSetUnit", function(tooltip)
	local _, unit = tooltip:GetUnit()
	if not unit or not UnitIsPlayer(unit) then return end
	local _, classFile = UnitClass(unit)
	if classFile ~= "SKINSHIFTER" or not IsPossessor(UnitRace(unit)) then return end
	for index = 2, tooltip:NumLines() do
		local line = _G["GameTooltipTextLeft" .. index]
		local text = line and line:GetText()
		if text and text:find(NAME, 1, true) then
			line:SetText(Recoloured(text))
		end
	end
end)

-- Chat: a name is coloured by its class file; a Possessor's gets the grey.
local function HookChat()
	if not GetColoredName or SkinPossessor_ChatHooked then return end
	SkinPossessor_ChatHooked = true
	local original = GetColoredName
	function GetColoredName(event, ...)
		local text = original(event, ...)
		local guid = select(12, ...)
		if text and guid and guid ~= "" then
			local _, classFile, _, raceFile = GetPlayerInfoByGUID(guid)
			if classFile == "SKINSHIFTER" and IsPossessor(nil, raceFile) then
				text = text:gsub("^|c%x%x%x%x%x%x%x%x", "|cff" .. HEX)
			end
		end
		return text
	end
end
HookChat()

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:SetScript("OnEvent", function()
	HookSheet()
	HookChat()
end)
