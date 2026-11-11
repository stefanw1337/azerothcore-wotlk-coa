-- Companion Journal: the window. A list on the left (search, filter and sort), a 3D preview on the right.
local addonName, ns = ...
local CJ = ns.CJ

local ROWS, ROW_HEIGHT = 10, 40
local WIDTH, HEIGHT = 780, 560
local TAB_TEXT = { MOUNT = "Mounts", CRITTER = "Pets" }

local function setSize(region, width, height)
    region:SetWidth(width)
    region:SetHeight(height)
end

local frame = CreateFrame("Frame", "CompanionJournalFrame", UIParent)
setSize(frame, WIDTH, HEIGHT)
frame:SetPoint("CENTER")
frame:SetFrameStrata("HIGH")
frame:SetToplevel(true)
frame:EnableMouse(true)
frame:SetMovable(true)
frame:SetClampedToScreen(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", function(self)
    self:StopMovingOrSizing()
    local point, _, relativePoint, x, y = self:GetPoint()
    CompanionJournalDB.point = { point, relativePoint, x, y }
end)
frame:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
frame:Hide()
tinsert(UISpecialFrames, "CompanionJournalFrame")

frame.companionType = "MOUNT"

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -18)
frame.title = title

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -6, -6)

local count = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
count:SetPoint("TOPLEFT", 24, -22)

-- Search, filter, sort ------------------------------------------------------------------------------------------
local search = CreateFrame("EditBox", "CompanionJournalSearch", frame, "InputBoxTemplate")
setSize(search, 170, 20)
search:SetPoint("TOPLEFT", 30, -50)
search:SetAutoFocus(false)
search:SetScript("OnTextChanged", function() frame:Refresh(true) end)
search:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
search:SetScript("OnEnterPressed", function(self) self:ClearFocus() end)
local searchHint = search:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
searchHint:SetPoint("LEFT", 2, 0)
searchHint:SetText("Search")
search:SetScript("OnEditFocusGained", function() searchHint:Hide() end)
search:SetScript("OnEditFocusLost", function(self) if self:GetText() == "" then searchHint:Show() end end)

local filterMenu = CreateFrame("Frame", "CompanionJournalFilterMenu", frame, "UIDropDownMenuTemplate")
local sortMenu = CreateFrame("Frame", "CompanionJournalSortMenu", frame, "UIDropDownMenuTemplate")
local toolsMenu = CreateFrame("Frame", "CompanionJournalToolsMenu", frame, "UIDropDownMenuTemplate")

local function setShown(region, shown)
    if shown then region:Show() else region:Hide() end
end

local function setEnabled(widget, enabled)
    if enabled then widget:Enable() else widget:Disable() end
end

local function button(parent, text, width)
    local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    setSize(b, width or 80, 22)
    b:SetText(text)
    return b
end

local filterButton = button(frame, "Filter", 70)
filterButton:SetPoint("LEFT", search, "RIGHT", 8, 0)
filterButton:SetScript("OnClick", function(self) ToggleDropDownMenu(1, nil, filterMenu, self, 0, 0) end)

local sortButton = button(frame, "Sort", 60)
sortButton:SetPoint("LEFT", filterButton, "RIGHT", 4, 0)
sortButton:SetScript("OnClick", function(self) ToggleDropDownMenu(1, nil, sortMenu, self, 0, 0) end)

local toolsButton = button(frame, "Tools", 60)
toolsButton:SetPoint("LEFT", sortButton, "RIGHT", 4, 0)
toolsButton:SetScript("OnClick", function(self) ToggleDropDownMenu(1, nil, toolsMenu, self, 0, 0) end)

-- List ----------------------------------------------------------------------------------------------------------
local listFrame = CreateFrame("Frame", nil, frame)
listFrame:SetPoint("TOPLEFT", 26, -82)
setSize(listFrame, 356, ROWS * ROW_HEIGHT)

local listInset = CreateFrame("Frame", nil, frame)
listInset:SetPoint("TOPLEFT", listFrame, "TOPLEFT", -6, 6)
listInset:SetPoint("BOTTOMRIGHT", listFrame, "BOTTOMRIGHT", 28, -6)
listInset:SetBackdrop({
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 },
})
listInset:SetBackdropColor(0, 0, 0, 0.6)
listInset:SetFrameLevel(frame:GetFrameLevel() + 1)
listFrame:SetFrameLevel(listInset:GetFrameLevel() + 1)

local scroll = CreateFrame("ScrollFrame", "CompanionJournalScroll", frame, "FauxScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", listFrame, "TOPLEFT", 0, 0)
scroll:SetPoint("BOTTOMRIGHT", listFrame, "BOTTOMRIGHT", 0, 0)
scroll:SetScript("OnVerticalScroll", function(self, offset)
    FauxScrollFrame_OnVerticalScroll(self, offset, ROW_HEIGHT, function() frame:Refresh() end)
end)

frame.view = {}
frame.rows = {}

local function showTooltip(row)
    local entry = row.entry
    if not entry then return end
    GameTooltip:SetOwner(row, "ANCHOR_RIGHT")
    GameTooltip:SetHyperlink("spell:" .. entry.spellID)
    if entry.source then GameTooltip:AddLine(entry.source, 0.4, 0.8, 1) end
    if not entry.collected then GameTooltip:AddLine("Not collected", 1, 0.3, 0.3) end
    GameTooltip:Show()
end

for i = 1, ROWS do
    local row = CreateFrame("Button", nil, listFrame)
    setSize(row, 352, ROW_HEIGHT - 2)
    row:SetPoint("TOPLEFT", 0, -(i - 1) * ROW_HEIGHT)
    row:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    row:RegisterForDrag("LeftButton")
    row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")

    local selected = row:CreateTexture(nil, "BACKGROUND")
    selected:SetAllPoints()
    selected:SetTexture("Interface\\QuestFrame\\UI-QuestLogTitleHighlight")
    selected:SetBlendMode("ADD")
    selected:SetVertexColor(1, 0.82, 0, 0.6)
    selected:Hide()
    row.selected = selected

    local icon = row:CreateTexture(nil, "ARTWORK")
    setSize(icon, 34, 34)
    icon:SetPoint("LEFT", 2, 0)
    row.icon = icon

    local star = row:CreateTexture(nil, "OVERLAY")
    setSize(star, 14, 14)
    star:SetPoint("TOPLEFT", icon, "TOPLEFT", -3, 3)
    star:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcon_1")
    row.star = star

    local name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    name:SetPoint("TOPLEFT", icon, "TOPRIGHT", 6, -2)
    name:SetPoint("RIGHT", -4, 0)
    name:SetJustifyH("LEFT")
    row.name = name

    local info = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    info:SetPoint("BOTTOMLEFT", icon, "BOTTOMRIGHT", 6, 2)
    info:SetPoint("RIGHT", -4, 0)
    info:SetJustifyH("LEFT")
    row.info = info

    row:SetScript("OnClick", function(self, mouse)
        if not self.entry then return end
        if mouse == "RightButton" then
            CJ:SetFavorite(self.entry.spellID, not CJ:IsFavorite(self.entry.spellID))
        elseif IsModifiedClick("CHATLINK") then
            local link = GetSpellLink(self.entry.spellID)
            if link then ChatEdit_InsertLink(link) end
        else
            frame.selected = self.entry.spellID
            frame:Refresh()
        end
    end)
    row:SetScript("OnDoubleClick", function(self) CJ:Summon(self.entry) end)
    row:SetScript("OnDragStart", function(self)
        local entry = self.entry
        if entry and entry.collected then
            local index = CJ:FindIndex(entry.type, entry.spellID)
            if index then PickupCompanion(entry.type, index) end
        end
    end)
    row:SetScript("OnEnter", showTooltip)
    row:SetScript("OnLeave", GameTooltip_Hide)
    frame.rows[i] = row
end

-- Preview -------------------------------------------------------------------------------------------------------
local preview = CreateFrame("Frame", nil, frame)
preview:SetPoint("TOPLEFT", listInset, "TOPRIGHT", 10, 0)
preview:SetPoint("BOTTOMRIGHT", -22, 76)
preview:SetBackdrop({
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 },
})
preview:SetBackdropColor(0, 0, 0, 0.6)

local model = CreateFrame("PlayerModel", nil, preview)
model:SetPoint("TOPLEFT", 6, -6)
model:SetPoint("BOTTOMRIGHT", -6, 60)
model.facing = 0.6
model:EnableMouse(true)
model:EnableMouseWheel(true)
model:SetScript("OnMouseDown", function(self) self.dragX = GetCursorPosition() end)
model:SetScript("OnMouseUp", function(self) self.dragX = nil end)
model:SetScript("OnUpdate", function(self)
    if self.dragX then
        local x = GetCursorPosition()
        self.facing = self.facing + (x - self.dragX) * 0.02
        self.dragX = x
        self:SetFacing(self.facing)
    end
end)
model:SetScript("OnMouseWheel", function(self, delta)
    self.zoom = math.max(-2, math.min(1.5, (self.zoom or 0) + delta * 0.25))
    self:SetPosition(self.zoom, 0, 0)
end)

local previewName = preview:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
previewName:SetPoint("BOTTOMLEFT", 10, 40)
previewName:SetPoint("RIGHT", -10, 0)
previewName:SetJustifyH("LEFT")

local previewInfo = preview:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
previewInfo:SetPoint("TOPLEFT", previewName, "BOTTOMLEFT", 0, -4)
previewInfo:SetPoint("RIGHT", -10, 0)
previewInfo:SetJustifyH("LEFT")

local summon = CreateFrame("Button", "CompanionJournalSummonButton", frame, "SecureActionButtonTemplate,UIPanelButtonTemplate")
setSize(summon, 110, 22)
summon:SetText("Summon")
summon:SetPoint("BOTTOMRIGHT", -26, 40)
summon:SetAttribute("type", "spell")

local function aimSummon(entry)
    if InCombatLockdown() then return end
    local spellName = entry and entry.collected and GetSpellInfo(entry.spellID)
    summon:SetAttribute("spell", spellName or nil)
end

local favorite = button(frame, "Favourite", 110)
favorite:SetPoint("RIGHT", summon, "LEFT", -6, 0)
favorite:SetScript("OnClick", function()
    local entry = frame:SelectedEntry()
    if entry then CJ:SetFavorite(entry.spellID, not CJ:IsFavorite(entry.spellID)) end
end)

local random = button(frame, "Random favourite", 140)
random:SetPoint("BOTTOMLEFT", 26, 40)
random:SetScript("OnClick", function() CJ:SummonRandomFavorite(frame.companionType) end)

local favoritesFirst = CreateFrame("CheckButton", "CompanionJournalFavoritesFirst", frame, "OptionsCheckButtonTemplate")
favoritesFirst:SetPoint("LEFT", random, "RIGHT", 10, 0)
_G[favoritesFirst:GetName() .. "Text"]:SetText("Favourites first")
favoritesFirst:SetScript("OnClick", function(self)
    CompanionJournalDB.favoritesFirst = self:GetChecked() and true or false
    frame:Refresh(true)
end)

local hint = frame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
hint:SetPoint("BOTTOMLEFT", 28, 22)
hint:SetText("Double-click: summon   Right-click: favourite   Drag: action bar   Shift-click: link")

-- Tabs ----------------------------------------------------------------------------------------------------------
for i, companionType in ipairs(CJ.TYPES) do
    local tab = CreateFrame("Button", "CompanionJournalFrameTab" .. i, frame, "CharacterFrameTabButtonTemplate")
    tab:SetID(i)
    tab:SetText(TAB_TEXT[companionType])
    if i == 1 then
        tab:SetPoint("TOPLEFT", frame, "BOTTOMLEFT", 16, 12)
    else
        tab:SetPoint("LEFT", "CompanionJournalFrameTab" .. (i - 1), "RIGHT", -16, 0)
    end
    tab:SetScript("OnClick", function(self)
        PanelTemplates_SetTab(frame, self:GetID())
        frame.companionType = CJ.TYPES[self:GetID()]
        frame.selected = nil
        CloseDropDownMenus()
        frame:Refresh(true)
    end)
end
PanelTemplates_SetNumTabs(frame, #CJ.TYPES)
PanelTemplates_SetTab(frame, 1)

-- Drop-down menus -----------------------------------------------------------------------------------------------
local function check(text, checked, func, keepShown)
    local info = UIDropDownMenu_CreateInfo()
    info.text = text
    info.checked = checked
    info.func = func
    info.keepShownOnClick = keepShown ~= false
    info.isNotRadio = true
    return info
end

local function header(text)
    local info = UIDropDownMenu_CreateInfo()
    info.text = text
    info.isTitle = true
    info.notCheckable = true
    return info
end

local facetMenus = {}

local function menuInitializer(menu, build)
    menu.displayMode = "MENU"
    menu.initialize = function()
        if CompanionJournalDB then build(UIDROPDOWNMENU_MENU_LEVEL or 1) end
    end
end

menuInitializer(filterMenu, function(level)
    local filters = CompanionJournalDB.filters[frame.companionType]
    local menuList = UIDROPDOWNMENU_MENU_VALUE
    if level == 2 and type(menuList) == "string" and menuList ~= "SOURCE" then menuList = facetMenus[menuList] end
    if level == 1 then
        UIDropDownMenu_AddButton(check("Collected", filters.collected, function()
            filters.collected = not filters.collected; frame:Refresh(true) end), level)
        UIDropDownMenu_AddButton(check("Not collected", filters.uncollected, function()
            filters.uncollected = not filters.uncollected; frame:Refresh(true) end), level)
        UIDropDownMenu_AddButton(check("Favourites only", filters.favoritesOnly, function()
            filters.favoritesOnly = not filters.favoritesOnly; frame:Refresh(true) end), level)
        if frame.companionType == "MOUNT" then
            UIDropDownMenu_AddButton(header("Type"), level)
            UIDropDownMenu_AddButton(check("Ground", filters.ground ~= false, function()
                filters.ground = filters.ground == false; frame:Refresh(true) end), level)
            UIDropDownMenu_AddButton(check("Flying", filters.flying ~= false, function()
                filters.flying = filters.flying == false; frame:Refresh(true) end), level)
        end
        local info = UIDropDownMenu_CreateInfo()
        info.text = "Source"
        info.hasArrow = true
        info.notCheckable = true
        info.value = "SOURCE"
        UIDropDownMenu_AddButton(info, level)
        wipe(facetMenus)
        for _, facet in ipairs(CJ:FacetValues(frame.companionType)) do
            facetMenus[facet.name] = facet
            info = UIDropDownMenu_CreateInfo()
            info.text = facet.name
            info.hasArrow = true
            info.notCheckable = true
            info.value = facet.name
            UIDropDownMenu_AddButton(info, level)
        end
        info = UIDropDownMenu_CreateInfo()
        info.text = "Reset filters"
        info.notCheckable = true
        info.func = function()
            filters.collected, filters.uncollected, filters.favoritesOnly = true, false, false
            filters.ground, filters.flying = true, true
            wipe(filters.sources); wipe(filters.facets)
            search:SetText("")
            frame:Refresh(true)
        end
        UIDropDownMenu_AddButton(info, level)
    elseif level == 2 and menuList == "SOURCE" then
        local names = CJ:SourceNames(frame.companionType)
        table.insert(names, CJ.OTHER_SOURCE)
        for _, name in ipairs(names) do
            UIDropDownMenu_AddButton(check(name, filters.sources[name] ~= false, function()
                if filters.sources[name] == false then filters.sources[name] = nil else filters.sources[name] = false end
                frame:Refresh(true)
            end), level)
        end
        for _, only in ipairs(names) do
            local info = UIDropDownMenu_CreateInfo()
            info.text = "|cff66ccffShow only " .. only .. "|r"
            info.notCheckable = true
            info.func = function()
                for _, name in ipairs(names) do filters.sources[name] = false end
                filters.sources[only] = nil
                CloseDropDownMenus()
                frame:Refresh(true)
            end
            UIDropDownMenu_AddButton(info, level)
        end
    elseif level == 2 and type(menuList) == "table" then
        local hidden = filters.facets[menuList.name] or {}
        filters.facets[menuList.name] = hidden
        for _, value in ipairs(menuList.values) do
            UIDropDownMenu_AddButton(check(value, hidden[value] ~= false, function()
                if hidden[value] == false then hidden[value] = nil else hidden[value] = false end
                frame:Refresh(true)
            end), level)
        end
    end
end)

menuInitializer(sortMenu, function(level)
    for _, sort in ipairs(CJ.SORTS) do
        local info = UIDropDownMenu_CreateInfo()
        info.text = sort.text
        info.checked = CompanionJournalDB.sort[frame.companionType] == sort.key
        info.func = function()
            CompanionJournalDB.sort[frame.companionType] = sort.key
            frame:Refresh(true)
        end
        UIDropDownMenu_AddButton(info, level)
    end
end)

local function sourceActions()
    local actions = {}
    for _, name in ipairs(CJ:SourceNames(frame.companionType)) do
        for _, action in ipairs(CJ.sources[name].actions or {}) do
            table.insert(actions, action)
        end
    end
    return actions
end

menuInitializer(toolsMenu, function(level)
    for _, action in ipairs(sourceActions()) do
        local info = UIDropDownMenu_CreateInfo()
        info.text = action.text
        info.notCheckable = true
        info.func = action.func
        UIDropDownMenu_AddButton(info, level)
    end
end)

-- Refresh -------------------------------------------------------------------------------------------------------
function frame:SelectedEntry()
    for _, entry in ipairs(self.view) do
        if entry.spellID == self.selected then return entry end
    end
end

function frame:Refresh(rebuildView)
    if not self:IsShown() then return end
    local companionType = self.companionType
    if rebuildView or CJ.dirty or not self.viewType or self.viewType ~= companionType then
        self.view = CJ:View(companionType, search:GetText())
        self.viewType = companionType
    end
    local view = self.view
    title:SetText(companionType == "MOUNT" and "Mount Journal" or "Pet Journal")
    count:SetText(("Collected: %d   Shown: %d"):format(CJ:CollectedCount(companionType), #view))
    favoritesFirst:SetChecked(CompanionJournalDB.favoritesFirst)
    setShown(toolsButton, #sourceActions() > 0)

    if not self.selected and view[1] then self.selected = view[1].spellID end
    local offset = FauxScrollFrame_GetOffset(scroll)
    FauxScrollFrame_Update(scroll, #view, ROWS, ROW_HEIGHT)
    for i, row in ipairs(self.rows) do
        local entry = view[offset + i]
        row.entry = entry
        if entry then
            row.icon:SetTexture(entry.icon)
            row.icon:SetDesaturated(not entry.collected)
            row.name:SetText(entry.name)
            if entry.collected then
                row.name:SetTextColor(1, 0.82, 0)
            else
                row.name:SetTextColor(0.5, 0.5, 0.5)
            end
            local parts = {}
            if entry.source then table.insert(parts, entry.source) end
            if entry.facets then
                for _, value in pairs(entry.facets) do table.insert(parts, value) end
            end
            if entry.active then table.insert(parts, "|cff00ff00active|r") end
            if not entry.collected and entry.owned then table.insert(parts, "|cffffd100owned, not learned|r") end
            row.info:SetText(table.concat(parts, " · "))
            setShown(row.star, CJ:IsFavorite(entry.spellID))
            setShown(row.selected, entry.spellID == self.selected)
            row:Show()
        else
            row:Hide()
        end
    end

    local entry = self:SelectedEntry()
    if entry then
        local shown = entry.displayID or entry.creatureID
        if model.shown ~= shown then
            model:ClearModel()
            if entry.displayID and model.SetDisplayInfo then
                model:SetDisplayInfo(entry.displayID)
            elseif entry.creatureID then
                model:SetCreature(entry.creatureID)
            end
            model:SetFacing(model.facing)
            model.shown = shown
        end
        previewName:SetText(entry.name)
        local text = CJ:SpellDescription(entry.spellID)
        if entry.source then text = "|cff66ccff" .. entry.source .. "|r\n" .. text end
        previewInfo:SetText(text)
        summon:SetText(entry.active and (companionType == "MOUNT" and "Dismount" or "Dismiss") or "Summon")
        if not InCombatLockdown() then setEnabled(summon, entry.collected) end
        aimSummon(entry)
        favorite:SetText(CJ:IsFavorite(entry.spellID) and "Unfavourite" or "Favourite")
        favorite:Enable()
        model:Show()
    else
        model:Hide()
        model.shown = nil
        previewName:SetText("")
        previewInfo:SetText("")
        if not InCombatLockdown() then summon:Disable() end
        aimSummon(nil)
        favorite:Disable()
    end
end

function CJ:OnDataChanged()
    frame.pending = true
end

frame.since = 0
frame:SetScript("OnUpdate", function(self, elapsed)
    if self.pending then
        self.since = (self.since or 0) + elapsed
        if self.since > 0.3 then
            self.pending, self.since = false, 0
            self:Refresh(true)
        end
    end
end)

frame:SetScript("OnShow", function(self)
    PlaySound("igCharacterInfoOpen")
    local point = CompanionJournalDB.point
    if point then
        self:ClearAllPoints()
        self:SetPoint(point[1], UIParent, point[2], point[3], point[4])
    end
    self:Refresh(true)
end)
frame:SetScript("OnHide", function() PlaySound("igCharacterInfoClose"); CloseDropDownMenus() end)

function CJ:Toggle(companionType)
    if companionType then
        local id = companionType == "CRITTER" and 2 or 1
        _G["CompanionJournalFrameTab" .. id]:Click()
    end
    if frame:IsShown() and not companionType then frame:Hide() else frame:Show() end
end

-- Open the journal from the default Mounts / Pets tab too.
local function addOpenButton()
    local parent = PetPaperDollFrameCompanionFrame
    if not parent or parent.companionJournalButton then return end
    local open = button(parent, "Journal", 80)
    open:SetPoint("TOPRIGHT", parent, "TOPRIGHT", -40, -40)
    open:SetScript("OnClick", function()
        CJ:Toggle(PetPaperDollFrameCompanionFrame.mode == "CRITTER" and "CRITTER" or "MOUNT")
    end)
    parent.companionJournalButton = open
end

local loader = CreateFrame("Frame")
loader:RegisterEvent("PLAYER_LOGIN")
loader:SetScript("OnEvent", addOpenButton)

SLASH_COMPANIONJOURNAL1 = "/cj"
SLASH_COMPANIONJOURNAL2 = "/mounts"
SLASH_COMPANIONJOURNAL3 = "/pets"
SlashCmdList.COMPANIONJOURNAL = function(message)
    message = (message or ""):lower():trim()
    if message == "random" or message == "randommount" then
        CJ:SummonRandomFavorite("MOUNT")
    elseif message == "randompet" then
        CJ:SummonRandomFavorite("CRITTER")
    elseif message == "pets" then
        CJ:Toggle("CRITTER")
    elseif message == "" then
        CJ:Toggle()
    else
        CJ:Toggle("MOUNT")
    end
end
