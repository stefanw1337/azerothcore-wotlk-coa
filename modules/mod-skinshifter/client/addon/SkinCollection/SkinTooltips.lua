-- What a spell really does, at the bottom of every spell tooltip: the server reads it from the spell itself
-- (damage, healing, how long, what it summons or casts, whether a part does nothing), with your level.
-- Messages: TIP;<spell id> to the server; TIP;<id>;<line> ... TIPEND;<id> back.

local cache, pending, asked = {}, {}, {}
local shownId

local function SpellIdOf(tooltip)
	local name, rank, id = tooltip:GetSpell()
	if not id and name and C_Spell and C_Spell.GetSpellID then
		id = C_Spell:GetSpellID(name, rank)
	end
	return tonumber(id)
end

-- The creatures a stolen ability came from (the Skin Collection's list).
local function StolenFrom(id)
	local shared = SkinCollectionShared
	if not shared then return nil end
	local skins, abilities = shared.Abilities()
	local names = {}
	for _, skin in ipairs(skins or {}) do
		for _, spell in ipairs((abilities or {})[skin.entry] or {}) do
			if spell == id and #names < 3 then
				names[#names + 1] = string.format("%s (%d)", skin.name, skin.level or 0)
				break
			end
		end
	end
	return #names > 0 and table.concat(names, ", ") or nil
end

local function AddLines(tooltip, id)
	local lines = cache[id]
	if not lines or #lines == 0 then return end
	tooltip:AddLine(" ")
	for _, line in ipairs(lines) do
		local indent, first, rest = line:match("^(%s*)(.)(.*)$")
		if first == "!" then
			tooltip:AddLine(indent .. rest, 1, 0.38, 0.38, true)
		elseif first == "~" then
			tooltip:AddLine(indent .. rest, 0.5, 0.5, 0.5, true)
		else
			tooltip:AddLine(line, 0.6, 0.85, 1, true)
		end
	end
	local from = StolenFrom(id)
	if from then
		tooltip:AddLine("Stolen from: " .. from, 0.5, 0.5, 0.5, true)
	end
	tooltip:Show()
end

GameTooltip:HookScript("OnTooltipSetSpell", function(tooltip)
	local id = SpellIdOf(tooltip)
	shownId = id
	if not id then return end
	if cache[id] then
		AddLines(tooltip, id)
	elseif not asked[id] and SkinCollectionShared then
		asked[id] = true
		pending[id] = {}
		SkinCollectionShared.Send("TIP;" .. id)
	end
end)
GameTooltip:HookScript("OnHide", function() shownId = nil end)

function SkinTooltips_OnMessage(fields)
	local id = tonumber(fields[2])
	if not id then return end
	if fields[1] == "TIP" then
		pending[id] = pending[id] or {}
		-- a line may hold ";" no more (the server swaps them), but keep anything after the third field
		tinsert(pending[id], table.concat(fields, ";", 3))
	elseif fields[1] == "TIPEND" then
		cache[id] = pending[id] or {}
		pending[id] = nil
		if shownId == id and GameTooltip:IsShown() then
			AddLines(GameTooltip, id)
		end
	end
end

-- The lines as one text (lower case), for the spellbook search; nil until the server has answered.
function SkinTooltips_Text(id)
	local lines = cache[id]
	return lines and strlower(table.concat(lines, " ")) or nil
end

-- Asks for a spell's lines without a tooltip (the spellbook search reads them).
function SkinTooltips_Ask(id)
	if id and not cache[id] and not asked[id] and SkinCollectionShared then
		asked[id] = true
		pending[id] = {}
		SkinCollectionShared.Send("TIP;" .. id)
	end
end
