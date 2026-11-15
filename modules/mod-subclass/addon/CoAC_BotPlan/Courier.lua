local PREFIX = "SUBCLASS"
local SLOT = 36
local COLUMNS = 8
local REFRESH_SECONDS = 3
local SECTIONS = {
    { load = "J", title = "Grey junk" },
    { load = "M", title = "Marked to sell" },
    { load = "Q", title = "Leftover quest items (thrown away, no coins)" },
    { load = "C", title = "Kept - click one to always sell that kind" },
}

local QUALITY_BASE = 4294967040
local QUALITIES = {
    { quality = 1, name = "white", color = "ffffffff" },
    { quality = 2, name = "green", color = "ff1eff00" },
    { quality = 3, name = "blue", color = "ff0070dd", warn = true },
    { quality = 4, name = "epic", color = "ffa335ee", warn = true },
}

local loads, marked, incoming, incomingMarked = {}, {}, {}, {}
local worth, alone = 0, false

local function Send(message)
    SendAddonMessage(PREFIX, message, "WHISPER", UnitName("player"))
end

local frame = CreateFrame("Frame", "CoACCourierFrame", UIParent)
frame:SetWidth(COLUMNS * SLOT + 40)
frame:SetHeight(200)
frame:SetPoint("CENTER", 260, 60)
frame:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 24,
    insets = { left = 6, right = 6, top = 6, bottom = 6 },
})
frame:SetMovable(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:SetFrameStrata("DIALOG")
frame:Hide()
tinsert(UISpecialFrames, "CoACCourierFrame")

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOP", 0, -14)
title:SetText("Courier bag")

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -4, -4)

local hint = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
hint:SetPoint("TOP", 0, -32)
hint:SetWidth(COLUMNS * SLOT)
hint:SetText("Drop one of your items here, or click one under Kept, to always sell that item. " ..
    "Click a marked item to keep it again. Right-click an item for more.")

local total = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
total:SetPoint("BOTTOMLEFT", 20, 18)

local send = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
send:SetWidth(110)
send:SetHeight(22)
send:SetPoint("BOTTOMRIGHT", -16, 14)
send:SetText("Send courier")
send:SetScript("OnClick", function()
    Send("PAIRJUNK")
    frame.wait = REFRESH_SECONDS - 0.5
end)

local headers, slots, qualityButtons = {}, {}, {}
local menu = CreateFrame("Frame", "CoACCourierMenu", UIParent, "UIDropDownMenuTemplate")

local function QualityText(entry)
    return "|c" .. entry.color .. entry.name .. "|r"
end

local function SellsQuality(quality)
    return marked[QUALITY_BASE + quality] == true
end

local function ToggleQuality(entry)
    if SellsQuality(entry.quality) then
        Send("COURIERUNMARK " .. (QUALITY_BASE + entry.quality))
    elseif entry.warn then
        local popup = StaticPopup_Show("COACCOURIER_QUALITY", QualityText(entry))
        if popup then
            popup.data = entry.quality
        end
    else
        Send("COURIERMARK " .. (QUALITY_BASE + entry.quality))
    end
end

function CoACCourierQualityMenu()
    local lines = {}
    for _, entry in ipairs(QUALITIES) do
        lines[#lines + 1] = {
            text = "Every " .. QualityText(entry) .. " item" .. (entry.warn and " (asks first)" or ""),
            checked = SellsQuality(entry.quality),
            func = function() ToggleQuality(entry) end,
        }
    end
    return lines
end

local function Mark(entry, link)
    local _, _, quality = GetItemInfo(entry)
    if quality and quality >= 4 then
        local popup = StaticPopup_Show("COACCOURIER_EPIC", link or ("item " .. entry))
        if popup then
            popup.data = entry
        end
        return
    end
    Send("COURIERMARK " .. entry)
end

local SellOne

local function ItemMenu(slot)
    local _, link = GetItemInfo(slot.entry)
    local lines = { { text = link or ("item " .. slot.entry), isTitle = true, notCheckable = true } }
    if slot.owner and slot.load ~= "Q" then
        lines[#lines + 1] = { text = "Sell this one now", notCheckable = true,
            func = function() SellOne(slot.entry, slot.owner, link) end }
    end
    if slot.load == "M" then
        lines[#lines + 1] = { text = "Keep this item again", notCheckable = true,
            func = function() Send("COURIERUNMARK " .. slot.entry) end }
    elseif slot.load ~= "J" and slot.load ~= "Q" then
        lines[#lines + 1] = { text = "Always sell this item", notCheckable = true,
            func = function() Mark(slot.entry, link) end }
    end
    lines[#lines + 1] = { text = "Courier sells every...", notCheckable = true, hasArrow = true,
        menuList = CoACCourierQualityMenu() }
    EasyMenu(lines, menu, "cursor", 0, 0, "MENU")
end

function SellOne(entry, owner, link)
    local _, _, quality = GetItemInfo(entry)
    if quality and quality >= 4 then
        local popup = StaticPopup_Show("COACCOURIER_ONE", link or ("item " .. entry))
        if popup then
            popup.data = { entry = entry, owner = owner }
        end
        return
    end
    Send("COURIERSELLONE " .. entry .. " " .. owner)
end

local function Coins(money)
    local text = ""
    if money >= 10000 then
        text = text .. floor(money / 10000) .. "g "
    end
    if money >= 100 then
        text = text .. floor(money % 10000 / 100) .. "s "
    end
    return text .. (money % 100) .. "c"
end

local function Header(index)
    local header = headers[index]
    if not header then
        header = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
        header:SetJustifyH("LEFT")
        headers[index] = header
    end
    return header
end

local function ShowSlotTooltip(slot)
    GameTooltip:SetOwner(slot, "ANCHOR_RIGHT")
    GameTooltip:SetHyperlink("item:" .. slot.entry)
    if slot.owner then
        GameTooltip:AddLine("Carried by " .. slot.owner, 0.6, 0.8, 1)
    end
    if slot.load == "M" then
        GameTooltip:AddLine("Click to keep this kind of item again.", 0, 1, 0)
    elseif slot.load == "C" then
        GameTooltip:AddLine("Click to always sell this kind of item.", 0, 1, 0)
    end
    GameTooltip:Show()
end

local function Slot(index)
    local slot = slots[index]
    if slot then
        return slot
    end
    slot = CreateFrame("Button", nil, frame)
    slot:SetWidth(SLOT - 4)
    slot:SetHeight(SLOT - 4)
    slot.icon = slot:CreateTexture(nil, "ARTWORK")
    slot.icon:SetAllPoints()
    slot.count = slot:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
    slot.count:SetPoint("BOTTOMRIGHT", -2, 2)
    slot:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    slot:SetScript("OnEnter", ShowSlotTooltip)
    slot:SetScript("OnLeave", function() GameTooltip:Hide() end)
    slot:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    slot:SetScript("OnClick", function(self, mouse)
        if mouse == "RightButton" then
            ItemMenu(self)
        elseif CursorHasItem() then
            frame:GetScript("OnReceiveDrag")(frame)
        elseif self.load == "M" then
            Send("COURIERUNMARK " .. self.entry)
        elseif self.load == "C" then
            local _, link = GetItemInfo(self.entry)
            Mark(self.entry, link)
        end
    end)
    slot:SetScript("OnReceiveDrag", function() frame:GetScript("OnReceiveDrag")(frame) end)
    slots[index] = slot
    return slot
end

local qualityLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
qualityLabel:SetPoint("TOPLEFT", 20, -58)
qualityLabel:SetText("Always sell:")
for index, entry in ipairs(QUALITIES) do
    local button = CreateFrame("Button", nil, frame)
    button:SetWidth(52)
    button:SetHeight(16)
    button:SetPoint("LEFT", qualityLabel, "RIGHT", 6 + (index - 1) * 56, 0)
    button.text = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    button.text:SetPoint("CENTER")
    button.text:SetText(QualityText(entry))
    button.mark = button:CreateTexture(nil, "BACKGROUND")
    button.mark:SetAllPoints()
    button.mark:SetTexture(1, 0.82, 0, 0.35)
    button:SetHighlightTexture("Interface\\Buttons\\UI-Panel-Button-Highlight", "ADD")
    button:SetScript("OnClick", function() ToggleQuality(entry) end)
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(SellsQuality(entry.quality) and "The courier sells every " .. entry.name ..
            " item both heads carry. Click to stop." or "Click to have the courier sell every " .. entry.name ..
            " item both heads carry.", nil, nil, nil, nil, true)
        GameTooltip:Show()
    end)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    qualityButtons[index] = button
end

local function Render()
    for index, entry in ipairs(QUALITIES) do
        local button = qualityButtons[index]
        if SellsQuality(entry.quality) then
            button.mark:Show()
        else
            button.mark:Hide()
        end
    end
    local y, used, shownHeaders = -74, 0, 0
    local rows = {}
    for _, section in ipairs(SECTIONS) do
        local list = {}
        for _, entry in ipairs(loads) do
            if entry.load == section.load then
                list[#list + 1] = entry
            end
        end
        if section.load == "M" then
            local carried = {}
            for _, entry in ipairs(list) do
                carried[entry.entry] = true
            end
            for entry in pairs(marked) do
                if not carried[entry] and entry < QUALITY_BASE then
                    list[#list + 1] = { load = "M", entry = entry, count = 0, money = 0 }
                end
            end
        end
        if #list > 0 then
            rows[#rows + 1] = { section = section, list = list }
        end
    end
    for _, row in ipairs(rows) do
        shownHeaders = shownHeaders + 1
        local header = Header(shownHeaders)
        header:ClearAllPoints()
        header:SetPoint("TOPLEFT", 20, y)
        header:SetText(row.section.title)
        header:Show()
        y = y - 14
        for index, entry in ipairs(row.list) do
            used = used + 1
            local slot = Slot(used)
            local column = (index - 1) % COLUMNS
            if index > 1 and column == 0 then
                y = y - SLOT
            end
            slot:ClearAllPoints()
            slot:SetPoint("TOPLEFT", 20 + column * SLOT, y)
            slot.entry, slot.load, slot.owner = entry.entry, entry.load, entry.owner
            slot.icon:SetTexture(GetItemIcon(entry.entry) or "Interface\\Icons\\INV_Misc_QuestionMark")
            slot.icon:SetDesaturated(entry.count == 0)
            slot.count:SetText(entry.count > 1 and entry.count or "")
            slot:Show()
        end
        y = y - SLOT - 6
    end
    for index = shownHeaders + 1, #headers do
        headers[index]:Hide()
    end
    for index = used + 1, #slots do
        slots[index]:Hide()
    end
    if #rows == 0 then
        shownHeaders = 1
        local header = Header(1)
        header:ClearAllPoints()
        header:SetPoint("TOPLEFT", 20, y)
        header:SetText(alone and "The courier only serves the two heads of one body." or "Nothing for the courier yet.")
        header:Show()
        y = y - 20
    end
    total:SetText("Worth: " .. Coins(worth))
    frame:SetHeight(-y + 50)
end

StaticPopupDialogs["COACCOURIER_QUALITY"] = {
    text = "Have the courier sell every %s item both heads carry, including gear in your bags?",
    button1 = YES,
    button2 = NO,
    OnAccept = function(_, quality)
        Send("COURIERMARK " .. (QUALITY_BASE + quality))
    end,
    timeout = 0,
    whileDead = true,
    hideOnEscape = true,
}

StaticPopupDialogs["COACCOURIER_ONE"] = {
    text = "Sell %s with the courier? It is epic or better.",
    button1 = YES,
    button2 = NO,
    OnAccept = function(_, data)
        Send("COURIERSELLONE " .. data.entry .. " " .. data.owner)
    end,
    timeout = 0,
    whileDead = true,
    hideOnEscape = true,
}

StaticPopupDialogs["COACCOURIER_EPIC"] = {
    text = "Always sell every %s? It is epic or better.",
    button1 = YES,
    button2 = NO,
    OnAccept = function(_, entry)
        Send("COURIERMARK " .. entry)
    end,
    timeout = 0,
    whileDead = true,
    hideOnEscape = true,
}

frame:SetScript("OnReceiveDrag", function()
    local kind, entry, link = GetCursorInfo()
    if kind ~= "item" or not entry then
        return
    end
    ClearCursor()
    Mark(entry, link)
end)
frame:SetScript("OnMouseUp", function(self)
    if CursorHasItem() then
        self:GetScript("OnReceiveDrag")(self)
    end
end)

frame:SetScript("OnUpdate", function(self, elapsed)
    self.wait = (self.wait or REFRESH_SECONDS) + elapsed
    if self.wait < REFRESH_SECONDS then
        return
    end
    self.wait = 0
    Send("COURIERLIST")
end)

frame:SetScript("OnShow", function(self)
    self.wait = REFRESH_SECONDS
    Render()
end)

local events = CreateFrame("Frame")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function(_, _, prefix, message, _, sender)
    if prefix ~= PREFIX or sender ~= UnitName("player") then
        return
    end
    local load, entry, count, money, owner = string.match(message, "^COURIER (%u) (%d+) (%d+) (%d+) (.+)$")
    if load then
        incoming[#incoming + 1] = { load = load, entry = tonumber(entry), count = tonumber(count),
            money = tonumber(money), owner = owner }
        return
    end
    local markedEntry = string.match(message, "^COURIERMARKED (%d+)$")
    if markedEntry then
        incomingMarked[tonumber(markedEntry)] = true
        return
    end
    local sum, flag = string.match(message, "^COURIEREND (%d+)%s*(%u*)$")
    if sum then
        loads, marked, incoming, incomingMarked = incoming, incomingMarked, {}, {}
        worth, alone = tonumber(sum), flag == "ALONE"
        if frame:IsShown() then
            Render()
        end
    end
end)

function CoACCourierToggle()
    if frame:IsShown() then
        frame:Hide()
    else
        frame:Show()
    end
end

local SHEET_SELL_JUNK = { ["Sell junk"] = true, ["Vendre la camelote"] = true }
local SHEET_REFRESH = { ["Refresh"] = true, ["Actualiser"] = true }
local refreshSheetAt

local function SheetShowsPartner()
    local partner = Ascension_Subclass and Ascension_Subclass.PartnerName and Ascension_Subclass.PartnerName()
    local title = SquidBotsLiteSheet and SquidBotsLiteSheet.title and SquidBotsLiteSheet.title:GetText()
    return partner and title and title:match(" : (.+)$") == partner
end

local function SendCourierFromSheet()
    Send("PAIRJUNK")
    refreshSheetAt = GetTime() + 1.5
    if CoACCourierFrame:IsShown() then
        CoACCourierFrame.wait = REFRESH_SECONDS
    end
end

local function ClickRefresh(parent)
    for _, child in ipairs({ parent:GetChildren() }) do
        if child:GetObjectType() == "Button" and child:IsShown() and SHEET_REFRESH[child:GetText() or ""] then
            child:Click()
            return true
        end
        if ClickRefresh(child) then
            return true
        end
    end
end

local function HookSellJunk(parent)
    for _, child in ipairs({ parent:GetChildren() }) do
        if child:GetObjectType() == "Button" and not child.coacCourier and child.GetText and
            SHEET_SELL_JUNK[child:GetText() or ""] then
            child.coacCourier = true
            local original = child:GetScript("OnClick")
            child:SetScript("OnClick", function(self, ...)
                if SheetShowsPartner() then
                    SendCourierFromSheet()
                elseif original then
                    original(self, ...)
                end
            end)
        end
        HookSellJunk(child)
    end
end

local sheetWatch = CreateFrame("Frame")
sheetWatch:SetScript("OnUpdate", function(self, elapsed)
    self.wait = (self.wait or 0) + elapsed
    if self.wait < 0.5 then
        return
    end
    self.wait = 0
    if SquidBotsLiteSheet and SquidBotsLiteSheet:IsShown() then
        HookSellJunk(SquidBotsLiteSheet)
        if refreshSheetAt and GetTime() >= refreshSheetAt then
            refreshSheetAt = nil
            ClickRefresh(SquidBotsLiteSheet)
        end
    end
end)

local originalEasyMenu = EasyMenu
function EasyMenu(menuList, menuFrame, ...)
    if menuFrame == SquidBotsLiteSheetMenu and SheetShowsPartner() and menuList and menuList[1] then
        local entry = tonumber(string.match(menuList[1].text or "", "item:(%d+)"))
        for index, line in ipairs(menuList) do
            if entry and type(line) == "table" and line.func and string.find(line.text or "", "vendor") then
                local _, link = GetItemInfo(entry)
                local owner = Ascension_Subclass.PartnerName()
                table.insert(menuList, index, { text = "Courier: sell this one now", notCheckable = true,
                    func = function()
                        SellOne(entry, owner, link)
                        refreshSheetAt = GetTime() + 1.5
                    end })
                line.text = "Courier: always sell this item"
                line.func = function()
                    Mark(entry, link)
                end
                line.hasArrow = true
                line.menuList = { { text = "Courier sells every...", isTitle = true, notCheckable = true } }
                for _, quality in ipairs(CoACCourierQualityMenu()) do
                    line.menuList[#line.menuList + 1] = quality
                end
            end
        end
    end
    return originalEasyMenu(menuList, menuFrame, ...)
end
