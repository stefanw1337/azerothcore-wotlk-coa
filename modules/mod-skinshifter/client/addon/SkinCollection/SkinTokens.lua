-- Soul Tokens (Possessor) / Skin Tokens (Skinshifter): the token counter, a ring of holes by the player frame: the
-- Possessor's has 20, the Skinshifter's 10 (2 tokens each; a hole with one is half lit). A filled hole shows the
-- toughest creature of its token's kills; the middle shows the count. The ring takes the colour of the spec. The ring burns
-- brighter at the costs of a takeover (a rare, an elite and a boss: 5, 10 and 20 tokens) and chimes when it gets
-- there. Like CoA's resource displays: drag to move, right click for the menu (lock, reset position, scale).
-- The server (mod-skinshifter, SkinshifterTokens.cpp) keeps the tokens; the art comes from tools\mktokenring.py
-- (SkinTokensArt.lua and the Textures folder).

local SIZE = 240                 -- the ring's texture on screen at 100%
local SIZE_OPTIONS = { "50%", "75%", "100%", "125%", "150%" }
local DEFAULT_POINT = { "TOP", "PlayerFrame", "BOTTOM", -20, 50 }
local SOUL = "Interface\\Icons\\Spell_Shadow_SoulGem"
-- The ring's colour by specialization (the textures are grey).
local SPEC_COLOURS = {
	[0] = { 0.70, 0.95, 1.00 },  -- none yet: the ghost's pale blue
	[1] = { 1.00, 0.35, 0.30 },  -- Beastskin: blood
	[2] = { 1.00, 0.90, 0.55 },  -- Courtier: gold
	[3] = { 0.55, 1.00, 0.25 },  -- Felstriker: fel
	[4] = { 1.00, 0.55, 0.20 },  -- Elementalist: fire
}

local state = { tokens = 0, kills = 0, per = 10, cap = 20, rare = 5, elite = 10, boss = 20, flavour = nil, spec = 0,
	mobs = {} }
local incoming

local function Shared() return SkinCollectionShared end

local function IsSkinshifter()
	local _, file = UnitClass("player")
	return file == "SKINSHIFTER"
end

local function IsPossessor()
	return Skinshifter_IsPossessorRace and Skinshifter_IsPossessorRace(UnitRace("player")) and true or false
end

local function Title()
	return IsPossessor() and "Soul Tokens" or "Skin Tokens"
end

local function DB()
	SkinTokensDB = type(SkinTokensDB) == "table" and SkinTokensDB or {}
	return SkinTokensDB
end

-- ---- the ring ------------------------------------------------------------------------------------------------------
local ring = CreateFrame("Button", "SkinTokenRing", UIParent)
ring:SetSize(SIZE, SIZE)
ring:SetFrameStrata("LOW")
ring:SetMovable(true)
ring:SetClampedToScreen(true)
ring:EnableMouse(true)
ring:SetHitRectInsets(SIZE * 0.2, SIZE * 0.2, SIZE * 0.2, SIZE * 0.2)  -- only the ring takes the mouse, not its flames
ring:RegisterForClicks("LeftButtonUp", "RightButtonUp")
ring:Hide()

local ringTexture = ring:CreateTexture(nil, "BORDER")
ringTexture:SetAllPoints()
local flames = ring:CreateTexture(nil, "ARTWORK")
flames:SetPoint("CENTER")
flames:SetSize(SIZE, SIZE)
flames:SetBlendMode("ADD")
flames:SetAlpha(0)

local title = ring:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
title:SetPoint("CENTER", 0, SIZE * 0.085)
local count = ring:CreateFontString(nil, "OVERLAY", "GameFontHighlightLarge")
count:SetPoint("CENTER", 0, SIZE * 0.01)
local rate = ring:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
rate:SetPoint("CENTER", 0, -SIZE * 0.07)
rate:SetTextColor(0.75, 0.75, 0.75)

-- the holes: the creature (its portrait in a model), on a soul stone, with a glow when it comes in
local holes = {}
local ShowDropDown

local function HoleModel(hole, mob)
	local model = hole.model
	model:ClearModel()
	hole.wanted, hole.tries, hole.wait = mob, 0, 0.1
	model:Hide()
end

local function TryModel(hole)
	local mob, model = hole.wanted, hole.model
	hole.tries = hole.tries + 1
	model:Show()
	hole.wait = 0.5
	local ok
	-- CoA's SetDisplayInfo takes a creature id and loads the creature first (see SkinCollection.lua); a plain
	-- client's takes the display
	local coaLoader = Creature and Creature.CreateFromID
	if hole.tries == 1 and model.SetDisplayInfo and (coaLoader and mob.entry > 0 or not coaLoader and mob.display > 0) then
		ok = pcall(model.SetDisplayInfo, model, coaLoader and mob.entry or mob.display)
		hole.wait = coaLoader and 3 or 0.5
	elseif model.SetCreature and mob.entry > 0 then
		ok = pcall(model.SetCreature, model, mob.entry)
	end
	if not ok and hole.tries >= 2 then
		model:Hide()
		hole.wanted = nil
		return
	end
end

local function HasModel(model)
	local ok, file = pcall(model.GetModel, model)
	return ok and type(file) == "string" and file ~= ""
end

for index = 1, 20 do  -- the most holes a ring has; a ring with fewer hides the rest
	local hole = CreateFrame("Button", nil, ring)
	hole.index = index
	hole:RegisterForClicks("LeftButtonUp", "RightButtonUp")
	hole.soul = hole:CreateTexture(nil, "BACKGROUND")
	hole.soul:SetAllPoints()
	SetPortraitToTexture(hole.soul, SOUL)
	hole.soul:SetVertexColor(0.6, 0.6, 0.6)
	hole.glow = hole:CreateTexture(nil, "OVERLAY")
	hole.glow:SetPoint("CENTER")
	hole.glow:SetTexture("Interface\\Cooldown\\ping4")
	hole.glow:SetBlendMode("ADD")
	hole.glow:SetAlpha(0)
	hole.model = CreateFrame("DressUpModel", nil, hole)  -- dresses character-model NPCs (a PlayerModel left them white)
	hole.model:SetPoint("CENTER")
	hole.model:Hide()
	hole:SetScript("OnUpdate", function(self, elapsed)
		if self.flash and self.flash > 0 then
			self.flash = math.max(0, self.flash - elapsed * 0.8)
			self.glow:SetAlpha(math.min(1, self.flash))
		end
		if not self.wanted then return end
		self.wait = self.wait - elapsed
		if self.wait > 0 then return end
		if self.tries > 0 and HasModel(self.model) then
			pcall(self.model.SetCamera, self.model, 0)  -- the portrait camera: the head, as on a unit frame
			self.wanted = nil
			return
		end
		if self.tries >= 2 then
			self.model:Hide()  -- no model: the soul stone stays
			self.wanted = nil
			return
		end
		TryModel(self)
	end)
	hole:SetScript("OnEnter", function(self)
		local first, last = self.first, self.last
		if not first or state.tokens < first then
			ring:GetScript("OnEnter")(ring)
			return
		end
		GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
		for token = first, math.min(last, state.tokens) do
			local mob = state.mobs[token]
			GameTooltip:AddDoubleLine(mob and mob.name ~= "" and mob.name or "A soul", "token " .. token, 1, 1, 1,
				0.75, 0.75, 0.75)
		end
		GameTooltip:Show()
	end)
	hole:SetScript("OnLeave", function() GameTooltip:Hide() end)
	hole:SetScript("OnClick", function(_, button) if button == "RightButton" then ShowDropDown() end end)
	hole:SetScript("OnMouseDown", function(_, button)
		if button == "LeftButton" and not DB().locked then ring:StartMoving() end
	end)
	hole:SetScript("OnMouseUp", function() ring:GetScript("OnDragStop")(ring) end)
	holes[index] = hole
end

local function Art()
	local art = SKIN_TOKEN_ART
	return art and (art[state.flavour or ""] or art.skinshifter)
end

local function PlaceHoles()
	local art = Art()
	if not art then return end
	ringTexture:SetTexture(art.ring)
	flames:SetTexture(art.flames)
	local diameter = art.holeRadius * 2 * SIZE
	for index, hole in ipairs(holes) do
		local spot = art.holes[index]
		hole.spot = spot
		if spot then hole:Show() else hole:Hide() end
		hole:ClearAllPoints()
		spot = spot or { 0.5, 0.5 }
		hole:SetPoint("CENTER", ring, "TOPLEFT", spot[1] * SIZE, -spot[2] * SIZE)
		hole:SetSize(diameter * 1.15, diameter * 1.15)
		hole.glow:SetSize(diameter * 2.6, diameter * 2.6)
		hole.model:SetSize(diameter * 0.95, diameter * 0.95)
	end
end

local function Colour()
	local colour = SPEC_COLOURS[state.spec] or SPEC_COLOURS[0]
	local art = Art()
	if art and art.tint == false then
		ringTexture:SetVertexColor(1, 1, 1)  -- art with its own colours: only the halo takes the spec's
	else
		ringTexture:SetVertexColor(colour[1], colour[2], colour[3])
	end
	flames:SetVertexColor(colour[1], colour[2], colour[3])
	for _, hole in ipairs(holes) do hole.glow:SetVertexColor(colour[1], colour[2], colour[3]) end
	title:SetTextColor(colour[1], colour[2], colour[3])
end

-- The holes share the cap: with 20 holes and a cap of 20 one token each, with 10 holes two. A hole shows the creature
-- of its last token once full; partly full it is half lit, with the creature of its first token.
local takeover  -- while you are inside a creature: { left, pvp, earned, name, hostile } (TAKEOVER messages)

local function Clock(seconds)
	return string.format("%d:%02d", math.floor(seconds / 60), seconds % 60)
end

-- During a takeover the middle of the ring is its timer: the time left (or how long you have been inside), and
-- in a hostile one how long you have fought players and the tokens that earned.
local function ShowTakeover()
	if not takeover then return false end
	title:SetText(takeover.name)
	count:SetText(Clock(takeover.left > 0 and takeover.left or takeover.inside))
	if takeover.hostile then
		rate:SetText(string.format("vs players %s  +%d", Clock(takeover.pvp), takeover.earned))
	else
		rate:SetText(takeover.left > 0 and "left inside" or "inside")
	end
	return true
end

local function Refresh()
	title:SetText(Title())
	count:SetText(string.format("%d/%d", state.tokens, state.cap))
	rate:SetText(string.format("1 token = %d kills", state.per))
	ShowTakeover()
	Colour()
	local art = Art()
	local holeCount = art and #art.holes or 20
	local perHole = math.max(1, math.floor(state.cap / holeCount + 0.5))
	for index, hole in ipairs(holes) do
		hole.first, hole.last = (index - 1) * perHole + 1, index * perHole
		local filled = hole.spot and state.tokens >= hole.last
		local partly = hole.spot and not filled and state.tokens >= hole.first
		local mob = state.mobs[filled and hole.last or hole.first]
		hole:SetAlpha(partly and 0.5 or 1)
		if filled or partly then
			hole.soul:Show()
			hole:Show()
			local key = mob and (mob.entry .. ":" .. mob.display) or "none"
			if hole.mobKey ~= key then
				hole.mobKey = key
				if mob and (mob.entry > 0 or mob.display > 0) then HoleModel(hole, mob) else hole.model:Hide() end
			end
		else
			hole.mobKey = nil
			hole.wanted = nil
			hole.model:Hide()
			hole.soul:Hide()
		end
	end
end

-- ---- burning -------------------------------------------------------------------------------------------------------
-- 0 below a rare's cost; then 1, 2 and 3 at a rare's, an elite's and a boss's.
local function BurnLevel()
	if state.tokens >= state.boss then return 3 end
	if state.tokens >= state.elite then return 2 end
	if state.tokens >= state.rare then return 1 end
	return 0
end

local BURN = {  -- base alpha, swing, speed, swell
	[0] = { 0, 0, 1, 0 },
	[1] = { 0.30, 0.12, 1.8, 0 },
	[2] = { 0.55, 0.20, 2.6, 0.015 },
	[3] = { 0.85, 0.25, 3.6, 0.035 },
}
local burnTime, flare = 0, 0
ring:SetScript("OnUpdate", function(_, elapsed)
	burnTime = burnTime + elapsed
	flare = math.max(0, flare - elapsed * 0.5)
	local burn = BURN[BurnLevel()]
	local wave = math.sin(burnTime * burn[3]) * 0.6 + math.sin(burnTime * burn[3] * 2.3) * 0.4
	flames:SetAlpha(math.max(0, math.min(1, burn[1] + burn[2] * wave + flare)))
	local swell = 1 + burn[4] * (1 + wave) + flare * 0.05
	flames:SetSize(SIZE * swell, SIZE * swell)
end)

local function Milestone(before, after)
	for _, cost in ipairs({ state.boss, state.elite, state.rare }) do
		if before < cost and after >= cost then
			flare = 1
			if cost == state.boss then
				PlaySoundFile("Sound\\Interface\\LevelUp.wav")
			else
				PlaySound("igQuestListComplete")
			end
			return
		end
	end
end

-- ---- moving and the menu (as CoA's resource displays) --------------------------------------------------------------
local function ApplyPosition()
	local point = DB().point or DEFAULT_POINT
	ring:ClearAllPoints()
	ring:SetPoint(point[1], _G[point[2]] or UIParent, point[3], point[4] or 0, point[5] or 0)
end

local function SetLocked(locked)
	DB().locked = locked and true or nil
	if locked then ring:RegisterForDrag() else ring:RegisterForDrag("LeftButton") end
end

local function SetSizeOption(index)
	DB().size = index
	ring:SetScale(0.25 + index * 0.25)
end

ring:SetScript("OnDragStart", function(self) if not DB().locked then self:StartMoving() end end)
ring:SetScript("OnDragStop", function(self)
	self:StopMovingOrSizing()
	local point, relativeTo, relativePoint, x, y = self:GetPoint(1)
	if point then
		local name = type(relativeTo) == "table" and relativeTo:GetName() or "UIParent"
		DB().point = { point, name, relativePoint, x, y }
	end
end)

local dropDown = CreateFrame("Frame", "SkinTokenRingDropDown", ring, "UIDropDownMenuTemplate")
UIDropDownMenu_Initialize(dropDown, function()
	local info = UIDropDownMenu_CreateInfo()
	info.text = DB().locked and (UNLOCK_FOCUS_FRAME or "Unlock Frame") or (LOCK_FOCUS_FRAME or "Lock Frame")
	info.checked = DB().locked
	info.func = function() SetLocked(not DB().locked) end
	UIDropDownMenu_AddButton(info)

	info = UIDropDownMenu_CreateInfo()
	info.text = RESET_POSITION or "Reset Position"
	info.notCheckable = 1
	info.func = function()
		DB().point = nil
		ApplyPosition()
	end
	UIDropDownMenu_AddButton(info)

	info = UIDropDownMenu_CreateInfo()
	info.text = "Scale"
	info.isTitle = true
	UIDropDownMenu_AddButton(info)

	for index, label in ipairs(SIZE_OPTIONS) do
		info = UIDropDownMenu_CreateInfo()
		info.text = label
		info.checked = (DB().size or 3) == index
		info.func = function() SetSizeOption(index) end
		UIDropDownMenu_AddButton(info)
	end
end, "MENU")

ShowDropDown = function()
	HideDropDownMenu(1)
	ToggleDropDownMenu(1, nil, dropDown, "cursor")
end

ring:SetScript("OnClick", function(_, button)
	if button == "RightButton" then ShowDropDown() else HideDropDownMenu(1) end
end)

ring:SetScript("OnEnter", function(self)
	GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
	GameTooltip:AddDoubleLine(Title(), string.format("%d/%d", state.tokens, state.cap), 1, 0.82, 0, 1, 1, 1)
	GameTooltip:AddLine(string.format("1 token = %d kills that give experience.", state.per), 1, 1, 1, true)
	if state.tokens >= state.cap then
		GameTooltip:AddLine("Full: kills count again once tokens are spent.", 1, 0.5, 0.5, true)
	else
		GameTooltip:AddLine(string.format("Next token: %d/%d kills.", state.kills, state.per), 0.75, 0.75, 0.75)
	end
	GameTooltip:AddLine(string.format("Taking over a rare costs %d, an elite %d, a boss %d.", state.rare, state.elite,
		state.boss), 0.75, 0.75, 0.75, true)
	GameTooltip:AddLine(COA_RESOURCE_CUSTOMIZE_HELPER or "Right click to customize menu\nDrag to move", 0.5, 0.5, 0.5, true)
	GameTooltip:Show()
end)
ring:SetScript("OnLeave", function() GameTooltip:Hide() end)

local loader = CreateFrame("Frame")
loader:RegisterEvent("PLAYER_LOGIN")
loader:SetScript("OnEvent", function()
	ApplyPosition()
	SetLocked(DB().locked)
	SetSizeOption(DB().size or 3)
end)

-- ---- messages (from SkinCollection.lua) ----------------------------------------------------------------------------
function SkinTokens_OnMessage(fields)
	local kind = fields[1]
	if kind == "TOKENS" then
		incoming = {
			tokens = tonumber(fields[2]) or 0, kills = tonumber(fields[3]) or 0, per = tonumber(fields[4]) or 10,
			cap = tonumber(fields[5]) or 20, rare = tonumber(fields[6]) or 5, elite = tonumber(fields[7]) or 10,
			boss = tonumber(fields[8]) or 20, flavour = fields[9], spec = tonumber(fields[10]) or 0, mobs = {},
		}
	elseif kind == "TOKENMOB" and incoming then
		local token = tonumber(fields[2])
		if token then
			incoming.mobs[token] = { entry = tonumber(fields[3]) or 0, display = tonumber(fields[4]) or 0, name = fields[5] or "" }
		end
	elseif kind == "TOKENEND" and incoming then
		local before, flavourBefore = state.tokens, state.flavour
		state, incoming = incoming, nil
		if state.flavour ~= flavourBefore then PlaceHoles() end
		Refresh()
		if before ~= state.tokens and flavourBefore then Milestone(before, state.tokens) end
		if IsSkinshifter() then ring:Show() else ring:Hide() end
	elseif kind == "TAKEOVER" then
		takeover = { left = tonumber(fields[2]) or 0, inside = tonumber(fields[3]) or 0, pvp = tonumber(fields[4]) or 0,
			earned = tonumber(fields[5]) or 0, hostile = fields[6] == "1", name = fields[7] or "" }
		ShowTakeover()
		if IsSkinshifter() then ring:Show() end
	elseif kind == "TAKEOVEREND" then
		takeover = nil
		Refresh()
	elseif kind == "TOKENNEW" then
		local token = tonumber(fields[2]) or 0
		for _, hole in ipairs(holes) do
			if hole.first and token >= hole.first and token <= hole.last then hole.flash = 1.6 end
		end
	end
end

-- /skintokens: what the counter knows.
SLASH_SKINTOKENS1 = "/skintokens"
SlashCmdList.SKINTOKENS = function()
	print(string.format("%s: %d/%d, %d/%d kills to the next, %s ring, spec %d, art %s.", Title(), state.tokens,
		state.cap, state.kills, state.per, tostring(state.flavour), state.spec or 0, Art() and "found" or "missing"))
	Shared().Send("TOKENS")
end
