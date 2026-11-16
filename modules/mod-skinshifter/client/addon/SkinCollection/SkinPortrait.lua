-- The worn skin in the character window. The client draws its model and its portrait from your model as a player's
-- (the character camera), which crops a creature's model or leaves the portrait empty. While you wear a skin, a model
-- of that creature is drawn over them instead, as the Skin Collection preview and the token ring draw it: the
-- portrait with the creature's own portrait camera, the model whole.

local coaLoader = Creature and Creature.CreateFromID and true

local function HasModel(model)
	local ok, file = pcall(model.GetModel, model)
	return ok and type(file) == "string" and file ~= ""
end

-- Loads a skin into a model: CoA's SetDisplayInfo takes the creature id (and loads the creature first), a plain
-- client's takes the display; SetCreature is the fallback. `done` runs once the model is there.
local function Load(model, skin, done)
	model.wanted, model.tries, model.wait, model.done = skin, 0, 0.1, done
	model:Show()
end

local function Step(model, elapsed)
	local skin = model.wanted
	if not skin then return end
	model.wait = model.wait - elapsed
	if model.wait > 0 then return end
	if model.tries > 0 and HasModel(model) then
		model.wanted = nil
		if model.done then model.done(model) end
		return
	end
	if model.tries >= 2 then
		model.wanted = nil
		model:Hide()
		return
	end
	model.tries = model.tries + 1
	model:ClearModel()
	if model.tries == 1 and model.SetDisplayInfo and (coaLoader and skin.entry or not coaLoader and skin.display) then
		pcall(model.SetDisplayInfo, model, coaLoader and skin.entry or skin.display)
		model.wait = coaLoader and 3 or 0.5
	elseif model.SetCreature and skin.entry then
		pcall(model.SetCreature, model, skin.entry)
		model.wait = 0.5
	end
end

-- Portrait in the character window -------------------------------------------------------------------------------
-- (the unit frame's portrait is drawn right by the client) a square model inside the round portrait, whose corners
-- would cover the ring, on the portrait darkened
local framePortrait = CharacterFramePortrait
local portrait = framePortrait and CreateFrame("DressUpModel", nil, CharacterFrame)
if portrait then
	portrait:SetSize(framePortrait:GetWidth() * 0.72, framePortrait:GetHeight() * 0.72)
	portrait:SetPoint("CENTER", framePortrait, "CENTER", 0, 0)
	portrait:SetFrameLevel(CharacterFrame:GetFrameLevel() + 2)
	portrait:Hide()
	portrait:SetScript("OnUpdate", Step)
end

local function PortraitReady(model)
	pcall(model.SetCamera, model, 0)  -- the creature's portrait camera: its head, as on a unit frame
end

-- Character window ------------------------------------------------------------------------------------------------
local paperdoll = CharacterModelFrame and CreateFrame("DressUpModel", nil, CharacterModelFrame)
if paperdoll then
	paperdoll:SetAllPoints(CharacterModelFrame)
	paperdoll:SetFrameLevel(CharacterModelFrame:GetFrameLevel() + 1)
	paperdoll:Hide()
	paperdoll:SetScript("OnUpdate", function(self, elapsed)
		Step(self, elapsed)
		-- the window's turn buttons turn the character model: this one follows it
		local ok, facing = pcall(CharacterModelFrame.GetFacing, CharacterModelFrame)
		if ok and facing then self:SetFacing(facing) end
	end)
end

local wornSkin

local function HidePlayerModel()
	if wornSkin and CharacterModelFrame then
		pcall(CharacterModelFrame.ClearModel, CharacterModelFrame)
	end
end

if CharacterModelFrame then
	hooksecurefunc(CharacterModelFrame, "SetUnit", HidePlayerModel)
end

local function Refresh()
	local skin = SkinCollection_WornSkin and SkinCollection_WornSkin()
	if skin and wornSkin and skin.display == wornSkin.display then return end
	wornSkin = skin
	if skin then
		if portrait then
			framePortrait:SetVertexColor(0, 0, 0)
			Load(portrait, skin, PortraitReady)
		end
		if paperdoll then
			Load(paperdoll, skin)
			HidePlayerModel()
		end
	else
		if portrait then
			framePortrait:SetVertexColor(1, 1, 1)
			portrait.wanted = nil
			portrait:Hide()
		end
		if paperdoll then
			paperdoll.wanted = nil
			paperdoll:Hide()
			pcall(CharacterModelFrame.SetUnit, CharacterModelFrame, "player")
		end
	end
end

SkinPortrait_Refresh = Refresh

-- /skinportrait: what this found (CoA's character window may name its frames differently)
SLASH_SKINPORTRAIT1 = "/skinportrait"
SlashCmdList["SKINPORTRAIT"] = function()
	local skin = SkinCollection_WornSkin and SkinCollection_WornSkin()
	print(string.format("Skin portrait: CharacterModelFrame %s, CharacterFramePortrait %s, worn %s (creature %s)",
		CharacterModelFrame and "found" or "MISSING", CharacterFramePortrait and "found" or "MISSING",
		skin and tostring(skin.display) or "none", skin and tostring(skin.entry) or "-"))
	wornSkin = nil
	Refresh()
end
