-- Steed Shift's reins (experimental): the passenger and the Skinshifter steed take turns steering. While you ride
-- without the reins, or are the steed while your passenger has them, your movement keys do nothing of their own;
-- here they ask the server for the reins instead (REINS), and the next press moves you.
-- Server lines: REINS;rider (riding, not steering), REINS;steed (you are the steed and do not steer), REINS;off.

local ACTIONS = { "MOVEFORWARD", "MOVEBACKWARD", "STRAFELEFT", "STRAFERIGHT", "TURNLEFT", "TURNRIGHT" }
local owner = CreateFrame("Frame", "SkinReinsFrame", UIParent)
local mode, wanted = "off", "off"

local button = CreateFrame("Button", "SkinReinsButton", UIParent)
button:Hide()
button:SetScript("OnClick", function()
	if mode ~= "off" and SkinCollectionShared then
		SkinCollectionShared.Send("REINS")
	end
end)

-- key bindings cannot change in combat: the change waits for its end (combat itself clears them, below)
local function Apply()
	if InCombatLockdown() then return end
	ClearOverrideBindings(owner)
	mode = wanted
	if mode == "off" then return end
	for _, action in ipairs(ACTIONS) do
		for _, key in ipairs({ GetBindingKey(action) }) do
			SetOverrideBindingClick(owner, true, key, "SkinReinsButton")
		end
	end
end

function SkinReins_OnMessage(fields)
	wanted = fields[2] == "rider" and "rider" or fields[2] == "steed" and "steed" or "off"
	Apply()
end

owner:RegisterEvent("PLAYER_REGEN_ENABLED")
owner:RegisterEvent("PLAYER_REGEN_DISABLED")
owner:RegisterEvent("UNIT_ENTERED_VEHICLE")
owner:RegisterEvent("UNIT_EXITED_VEHICLE")
owner:SetScript("OnEvent", function(_, event, unit)
	if event == "PLAYER_REGEN_DISABLED" then
		-- the last moment bindings may change: in combat the keys stay plain movement (thrown off the steed in a
		-- fight, you must be able to run), and the server gives the steed its reins back
		ClearOverrideBindings(owner)
		mode = "off"
	elseif event == "PLAYER_REGEN_ENABLED" then
		if mode ~= wanted then Apply() end
	elseif unit == "player" and event == "UNIT_EXITED_VEHICLE" then
		wanted = "off"
		Apply()
	elseif unit == "player" and SkinCollectionShared then
		SkinCollectionShared.Send("REINS?")  -- the server answers whether this is a Skinshifter steed's seat
	end
end)
