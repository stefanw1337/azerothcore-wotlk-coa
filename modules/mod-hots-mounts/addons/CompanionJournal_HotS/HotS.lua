-- Heroes of the Storm mounts for Companion Journal. Data.lua (written by the HotS mounts package) lists every
-- mount spell; this file registers them as a source and adds GM tools that learn or unlearn them in one go.
local SOURCE = "Heroes of the Storm"
local DATA = CompanionJournalHotSData or {}
local STYLES = { Ride = "Steed", Ridebeast = "Big beast", Ridebike = "Vehicle", Ridesurf = "Hover" }
local COLLECTIONS = { SeasonalEvents = "Seasonal" }

local spells, ids = {}, {}
for spellID, mount in pairs(DATA) do
    spells[spellID] = {
        name = mount.name, creature = mount.creature, display = mount.creature, item = mount.creature,
        facets = { Collection = COLLECTIONS[mount.collection] or mount.collection or "Other",
                   Rarity = mount.rarity or "Common", Style = STYLES[mount.category] or "Steed" },
    }
    table.insert(ids, spellID)
end
table.sort(ids)

-- GM learn / unlearn queue: one chat command every 0.2 s, the server's replies hidden while it runs.
local queue, verb, done, hideUntil = {}, nil, 0, 0
local runner = CreateFrame("Frame")
runner.wait = 0
runner:Hide()
local function hideReplies(_, _, message)
    if GetTime() >= hideUntil or not message then return false end
    return (message:find("^You have learned") or message:find("^You have unlearned")
        or message:find("already know") or message:find("added the pet")) and true or false
end
ChatFrame_AddMessageEventFilter("CHAT_MSG_SYSTEM", hideReplies)

runner:SetScript("OnUpdate", function(self, elapsed)
    self.wait = (self.wait or 0) - elapsed
    if self.wait > 0 then return end
    self.wait = 0.2
    local spellID = table.remove(queue, 1)
    if not spellID then
        self:Hide()
        DEFAULT_CHAT_FRAME:AddMessage(("|cff66ccffHotS mounts:|r %s %d mount(s)."):format(verb, done))
        return
    end
    hideUntil = GetTime() + 3
    SendChatMessage((verb == "learned" and ".learn " or ".unlearn ") .. spellID, "SAY")
    done = done + 1
    if done % 50 == 0 then
        DEFAULT_CHAT_FRAME:AddMessage(("|cff66ccffHotS mounts:|r %d / %d"):format(done, done + #queue))
    end
end)

local function run(learn, filter)
    if runner:IsShown() then
        DEFAULT_CHAT_FRAME:AddMessage("|cff66ccffHotS mounts:|r already running.")
        return
    end
    wipe(queue)
    done, verb = 0, learn and "learned" or "unlearned"
    filter = filter and filter:lower() or ""
    for _, spellID in ipairs(ids) do
        local known = CompanionJournal:FindIndex("MOUNT", spellID) ~= nil
        local name = (spells[spellID].name or ""):lower()
        if known ~= learn and (filter == "" or name:find(filter, 1, true)) then
            table.insert(queue, spellID)
        end
    end
    DEFAULT_CHAT_FRAME:AddMessage(("|cff66ccffHotS mounts:|r %s %d mount(s), about %d s (needs GM rights; clear your target first, the server teaches the selected player)."):format(
        learn and "learning" or "unlearning", #queue, math.ceil(#queue * 0.2)))
    if #queue > 0 then runner:Show() end
end

CompanionJournal:RegisterSource({
    name = SOURCE,
    type = "MOUNT",
    spells = spells,
    complete = true,
    facets = { "Collection", "Rarity", "Style" },
    actions = {
        { text = "HotS: learn all (GM)", func = function() run(true) end },
        { text = "HotS: unlearn all (GM)", func = function() run(false) end },
    },
})

SLASH_HOTSMOUNTS1 = "/hotsmounts"
SlashCmdList.HOTSMOUNTS = function(message)
    local command, rest = (message or ""):match("^(%S*)%s*(.-)$")
    command = command:lower()
    if command == "learn" then
        run(true, rest)
    elseif command == "unlearn" then
        run(false, rest)
    elseif command == "stop" then
        wipe(queue)
    else
        DEFAULT_CHAT_FRAME:AddMessage("|cff66ccffHotS mounts:|r /hotsmounts learn [name], /hotsmounts unlearn [name], "
            .. "/hotsmounts stop. " .. #ids .. " mounts; /cj opens the journal.")
    end
end
