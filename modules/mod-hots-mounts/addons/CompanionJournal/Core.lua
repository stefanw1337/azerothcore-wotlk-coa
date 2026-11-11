-- Companion Journal: data side. Reads the known mounts and pets from the companion API, joins the data that
-- other addons register (sources), and answers the journal's filter and sort questions.
local addonName, ns = ...

CompanionJournal = CompanionJournal or {}
local CJ = CompanionJournal
ns.CJ = CJ

CJ.TYPES = { "MOUNT", "CRITTER" }
CJ.sources = {}        -- name -> source table (see CJ:RegisterSource)
CJ.sourceOrder = {}
CJ.spellSource = {}    -- spellID -> source name
CJ.entries = { MOUNT = {}, CRITTER = {} }

local defaults = {
    favorites = {},            -- spellID -> true
    filters = {
        MOUNT = { collected = true, uncollected = false, favoritesOnly = false, ground = true, flying = true,
                  sources = {}, facets = {} },
        CRITTER = { collected = true, uncollected = false, favoritesOnly = false, sources = {}, facets = {} },
    },
    sort = { MOUNT = "name", CRITTER = "name" },
    favoritesFirst = true,
    point = nil,
}

local function copyDefaults(target, source)
    for key, value in pairs(source) do
        if type(value) == "table" then
            if type(target[key]) ~= "table" then target[key] = {} end
            copyDefaults(target[key], value)
        elseif target[key] == nil then
            target[key] = value
        end
    end
end

-- Sources -------------------------------------------------------------------------------------------------------
-- CompanionJournal:RegisterSource({
--     name = "Heroes of the Storm",          -- shown in the Source filter and on each entry
--     type = "MOUNT",                         -- or "CRITTER"
--     spells = { [spellID] = { name = "...", creature = id, item = id, facets = { Collection = "Beast" } } },
--     complete = true,                        -- spells lists every one, so the uncollected ones can be shown
--     facets = { "Collection", "Rarity" },    -- extra filter groups, values come from the spells' facets
--     actions = { { text = "Learn all (GM)", func = function() end } },
-- })
function CJ:RegisterSource(source)
    assert(type(source) == "table" and source.name and source.spells, "RegisterSource needs name and spells")
    source.type = source.type or "MOUNT"
    if not self.sources[source.name] then
        table.insert(self.sourceOrder, source.name)
    end
    self.sources[source.name] = source
    for spellID in pairs(source.spells) do
        self.spellSource[spellID] = source.name
    end
    self:MarkDirty()
end

function CJ:SourceNames(companionType)
    local names = {}
    for _, name in ipairs(self.sourceOrder) do
        if self.sources[name].type == companionType then
            table.insert(names, name)
        end
    end
    return names
end

-- Spell text and flying -----------------------------------------------------------------------------------------
local scanner = CreateFrame("GameTooltip", "CompanionJournalScanTooltip", nil, "GameTooltipTemplate")
scanner:SetOwner(WorldFrame, "ANCHOR_NONE")
local descriptionCache = {}

function CJ:SpellDescription(spellID)
    local cached = descriptionCache[spellID]
    if cached then return cached end
    scanner:ClearLines()
    scanner:SetHyperlink("spell:" .. spellID)
    local lines = scanner:NumLines()
    local text = ""
    if lines and lines > 1 then
        local region = _G["CompanionJournalScanTooltipTextLeft" .. lines]
        text = region and region:GetText() or ""
    end
    if text ~= "" then descriptionCache[spellID] = text end
    return text
end

local FLYING_WORDS = { "flying", "Outland or Northrend", "Outland and Northrend" }

local flyingCache = {}

function CJ:IsFlying(entry)
    local cached = flyingCache[entry.spellID]
    if cached ~= nil then return cached end
    local text = self:SpellDescription(entry.spellID)
    if text == "" then return false end  -- not answered yet: asked again next time
    local lower = text:lower()
    local flying = false
    for _, word in ipairs(FLYING_WORDS) do
        if lower:find(word:lower(), 1, true) then
            flying = true
            break
        end
    end
    flyingCache[entry.spellID] = flying
    return flying
end

-- Building the lists --------------------------------------------------------------------------------------------
function CJ:MarkDirty()
    self.dirty = true
    if self.OnDataChanged then self:OnDataChanged() end
end

function CJ:Rebuild(companionType)
    local list = {}
    local known = {}
    local count = GetNumCompanions(companionType) or 0
    for index = 1, count do
        local creatureID, name, spellID, icon, active = GetCompanionInfo(companionType, index)
        if spellID then
            known[spellID] = true
            local sourceName = self.spellSource[spellID]
            local sourceSpell = sourceName and self.sources[sourceName].spells[spellID]
            table.insert(list, {
                type = companionType, index = index, creatureID = creatureID, name = name or "?", spellID = spellID,
                displayID = sourceSpell and sourceSpell.display,
                icon = icon, active = active and true or false, collected = true, learnedOrder = index,
                source = sourceName, facets = sourceSpell and sourceSpell.facets or nil,
            })
        end
    end
    for _, sourceName in ipairs(self.sourceOrder) do
        local source = self.sources[sourceName]
        if source.type == companionType and source.complete then
            for spellID, data in pairs(source.spells) do
                if not known[spellID] then
                    local spellName, _, spellIcon = GetSpellInfo(spellID)
                    table.insert(list, {
                        type = companionType, creatureID = data.creature, displayID = data.display,
                        name = spellName or data.name or "?",
                        spellID = spellID, icon = spellIcon or data.icon or "Interface\\Icons\\INV_Misc_QuestionMark",
                        collected = false, learnedOrder = 0, source = sourceName, facets = data.facets,
                        owned = data.item and IsCollectionItemOwned and IsCollectionItemOwned(data.item) and true or false,
                    })
                end
            end
        end
    end
    self.entries[companionType] = list
    self.collectedCount = self.collectedCount or {}
    self.collectedCount[companionType] = count
    return list
end

function CJ:CollectedCount(companionType)
    return GetNumCompanions(companionType) or 0
end

-- Filtering and sorting -----------------------------------------------------------------------------------------
local OTHER = "Other"
CJ.OTHER_SOURCE = OTHER

function CJ:IsFavorite(spellID)
    return CompanionJournalDB.favorites[spellID] and true or false
end

function CJ:SetFavorite(spellID, value)
    CompanionJournalDB.favorites[spellID] = value and true or nil
    self:MarkDirty()
end

local function passes(self, entry, filters, search)
    if entry.collected and not filters.collected then return false end
    if not entry.collected and not filters.uncollected then return false end
    if filters.favoritesOnly and not self:IsFavorite(entry.spellID) then return false end
    if entry.type == "MOUNT" and (filters.ground == false or filters.flying == false) then
        local flying = self:IsFlying(entry)
        if flying and filters.flying == false then return false end
        if not flying and filters.ground == false then return false end
    end
    local sourceName = entry.source or OTHER
    if filters.sources[sourceName] == false then return false end
    if entry.facets then
        for facet, value in pairs(entry.facets) do
            local hidden = filters.facets[facet]
            if hidden and hidden[value] == false then return false end
        end
    end
    if search ~= "" then
        local haystack = (entry.name .. " " .. (entry.source or "")):lower()
        if entry.facets then
            for _, value in pairs(entry.facets) do haystack = haystack .. " " .. tostring(value):lower() end
        end
        for word in search:gmatch("%S+") do
            if not haystack:find(word, 1, true) then return false end
        end
    end
    return true
end

CJ.SORTS = {
    { key = "name", text = "Name (A-Z)" },
    { key = "namedesc", text = "Name (Z-A)" },
    { key = "newest", text = "Newest learned" },
    { key = "oldest", text = "Oldest learned" },
    { key = "source", text = "Source" },
}

local function sorter(self, sortKey)
    local favoritesFirst = CompanionJournalDB.favoritesFirst
    return function(a, b)
        if a.collected ~= b.collected then return a.collected end
        if favoritesFirst then
            local fa, fb = self:IsFavorite(a.spellID), self:IsFavorite(b.spellID)
            if fa ~= fb then return fa end
        end
        if sortKey == "namedesc" then
            if a.name ~= b.name then return a.name > b.name end
        elseif sortKey == "newest" then
            if a.learnedOrder ~= b.learnedOrder then return a.learnedOrder > b.learnedOrder end
        elseif sortKey == "oldest" then
            if a.learnedOrder ~= b.learnedOrder then return a.learnedOrder < b.learnedOrder end
        elseif sortKey == "source" then
            local sa, sb = a.source or "~", b.source or "~"
            if sa ~= sb then return sa < sb end
        end
        if a.name ~= b.name then return a.name < b.name end
        return a.spellID < b.spellID
    end
end

function CJ:View(companionType, search)
    if self.dirty or not self.entries[companionType] or #self.entries[companionType] == 0 then
        self:Rebuild(companionType)
        if companionType == "MOUNT" then self:Rebuild("CRITTER") else self:Rebuild("MOUNT") end
        self.dirty = false
    end
    local filters = CompanionJournalDB.filters[companionType]
    local view = {}
    search = (search or ""):lower()
    for _, entry in ipairs(self.entries[companionType]) do
        if passes(self, entry, filters, search) then
            table.insert(view, entry)
        end
    end
    table.sort(view, sorter(self, CompanionJournalDB.sort[companionType]))
    return view
end

function CJ:FacetValues(companionType)
    local facets = {}
    for _, sourceName in ipairs(self.sourceOrder) do
        local source = self.sources[sourceName]
        if source.type == companionType and source.facets then
            for _, facet in ipairs(source.facets) do
                facets[facet] = facets[facet] or {}
            end
            for _, data in pairs(source.spells) do
                if data.facets then
                    for facet, value in pairs(data.facets) do
                        if facets[facet] then facets[facet][value] = true end
                    end
                end
            end
        end
    end
    local ordered = {}
    for facet, values in pairs(facets) do
        local list = {}
        for value in pairs(values) do table.insert(list, value) end
        table.sort(list)
        table.insert(ordered, { name = facet, values = list })
    end
    table.sort(ordered, function(a, b) return a.name < b.name end)
    return ordered
end

-- Summoning -----------------------------------------------------------------------------------------------------
function CJ:FindIndex(companionType, spellID)
    for index = 1, GetNumCompanions(companionType) or 0 do
        local _, _, id = GetCompanionInfo(companionType, index)
        if id == spellID then return index end
    end
end

-- A known spell can be missing from the companion list (CoA builds that list from its own collection).
function CJ:IsKnown(companionType, spellID)
    if self:FindIndex(companionType, spellID) then return true end
    if IsSpellKnown and IsSpellKnown(spellID) then return true end
    local sourceName = self.spellSource[spellID]
    local data = sourceName and self.sources[sourceName].spells[spellID]
    return data and data.item and IsCollectionItemOwned and IsCollectionItemOwned(data.item) and true or false
end

function CJ:Summon(entry)
    if not entry then return end
    local index = self:FindIndex(entry.type, entry.spellID)
    if not index then
        DEFAULT_CHAT_FRAME:AddMessage(("|cff66ccffCompanion Journal:|r %s is not in the game's %s list yet; use the "
            .. "Summon button."):format(entry.name, entry.type == "MOUNT" and "mount" or "pet"))
        return
    end
    local _, _, _, _, active = GetCompanionInfo(entry.type, index)
    if active then
        if entry.type == "MOUNT" then Dismount() else DismissCompanion(entry.type) end
    else
        CallCompanion(entry.type, index)
    end
end

function CJ:SummonRandomFavorite(companionType)
    companionType = companionType or "MOUNT"
    if companionType == "MOUNT" and IsMounted() then
        Dismount()
        return
    end
    local favorites, all = {}, {}
    local wantFlying = companionType == "MOUNT" and IsFlyableArea and IsFlyableArea()
    local flyingFavorites = {}
    for index = 1, GetNumCompanions(companionType) or 0 do
        local _, _, spellID = GetCompanionInfo(companionType, index)
        table.insert(all, index)
        if self:IsFavorite(spellID) then
            table.insert(favorites, index)
            if wantFlying and self:IsFlying({ spellID = spellID }) then table.insert(flyingFavorites, index) end
        end
    end
    local pool = (#flyingFavorites > 0 and flyingFavorites) or (#favorites > 0 and favorites) or all
    if #pool == 0 then return end
    CallCompanion(companionType, pool[math.random(#pool)])
end

-- Events --------------------------------------------------------------------------------------------------------
local events = CreateFrame("Frame")
events:RegisterEvent("ADDON_LOADED")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("COMPANION_LEARNED")
events:RegisterEvent("COMPANION_UPDATE")
events:RegisterEvent("SPELLS_CHANGED")
pcall(events.RegisterEvent, events, "COMPANION_UNLEARNED")
events:SetScript("OnEvent", function(_, event, arg1)
    if event == "ADDON_LOADED" and arg1 == addonName then
        CompanionJournalDB = CompanionJournalDB or {}
        copyDefaults(CompanionJournalDB, defaults)
    elseif event == "PLAYER_LOGIN" then
        CJ:MarkDirty()
    elseif event ~= "ADDON_LOADED" then
        CJ:MarkDirty()
    end
end)

BINDING_HEADER_COMPANIONJOURNAL = "Companion Journal"
BINDING_NAME_COMPANIONJOURNAL_TOGGLE = "Toggle the journal"
BINDING_NAME_COMPANIONJOURNAL_RANDOMMOUNT = "Random favourite mount"
BINDING_NAME_COMPANIONJOURNAL_RANDOMPET = "Random favourite pet"
