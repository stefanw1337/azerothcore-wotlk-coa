local PREFIX = "SUBCLASS"
local ROW_HEIGHT = 16
local REFRESH_SECONDS = 3

local plans, incoming = {}, {}

local panel = CreateFrame("Frame", "CoACBotPlanFrame", UIParent)
panel:SetWidth(380)
panel:SetHeight(120)
panel:SetPoint("CENTER", 0, 120)
panel:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 24,
    insets = { left = 6, right = 6, top = 6, bottom = 6 },
})
panel:SetMovable(true)
panel:EnableMouse(true)
panel:RegisterForDrag("LeftButton")
panel:SetScript("OnDragStart", panel.StartMoving)
panel:SetScript("OnDragStop", panel.StopMovingOrSizing)
panel:SetFrameStrata("DIALOG")
panel:Hide()
tinsert(UISpecialFrames, "CoACBotPlanFrame")

local title = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOP", 0, -14)
title:SetText("Bot plans")

local close = CreateFrame("Button", nil, panel, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -4, -4)

local empty = panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
empty:SetPoint("TOP", 0, -44)
empty:SetText("No bots in your group.")

local lines = {}

local function Shown(region, shown)
    if shown then
        region:Show()
    else
        region:Hide()
    end
end

local function Send(message)
    SendAddonMessage(PREFIX, message, "WHISPER", UnitName("player"))
end

local function ActionButton(line)
    local button = CreateFrame("Button", nil, line, "UIPanelButtonTemplate")
    button:SetWidth(44)
    button:SetHeight(ROW_HEIGHT)
    return button
end

local function Line(index)
    local line = lines[index]
    if line then
        return line
    end
    line = CreateFrame("Button", nil, panel)
    line:SetHeight(ROW_HEIGHT)
    line:SetPoint("TOPLEFT", 16, -36 - (index - 1) * ROW_HEIGHT)
    line:SetPoint("RIGHT", panel, "RIGHT", -16, 0)
    line.text = line:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    line.text:SetPoint("LEFT")
    line.text:SetPoint("RIGHT", -96, 0)
    line.text:SetJustifyH("LEFT")
    line:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    line.action = ActionButton(line)
    line.action:SetPoint("RIGHT")
    line.second = ActionButton(line)
    line.second:SetPoint("RIGHT", line.action, "LEFT", -4, 0)
    lines[index] = line
    return line
end

local function SetButton(button, text, onClick)
    button:SetText(text or "")
    button:SetScript("OnClick", onClick)
    Shown(button, text ~= nil)
end

local function SetLine(index, text, actionText, onAction, secondText, onSecond)
    local line = Line(index)
    line.text:SetText(text)
    SetButton(line.action, actionText, onAction)
    SetButton(line.second, secondText, onSecond)
    line:EnableMouse(false)
    line:Show()
end

local function Render()
    local index = 0
    for _, plan in ipairs(plans) do
        index = index + 1
        local name = plan.name
        local sheet = SquidBotsLite_OpenSheet and function() SquidBotsLite_OpenSheet(name) end or nil
        SetLine(index, "|cff66ccff" .. name .. "|r  " .. plan.status, sheet and "Sheet" or nil, sheet)
        if plan.doing then
            index = index + 1
            SetLine(index, "    Now: |cffffd100" .. plan.doing .. "|r")
        end
        for position, quest in ipairs(plan.route) do
            index = index + 1
            local id = quest.id
            SetLine(index, "    " .. position .. ". " .. quest.title,
                position > 1 and "Now" or nil, function() Send("BOTPLANNOW " .. name .. " " .. id) end,
                position > 1 and "Up" or nil, function() Send("BOTPLANUP " .. name .. " " .. id) end)
        end
        if #plan.route == 0 then
            index = index + 1
            SetLine(index, "    no quests planned")
        end
        for _, quest in ipairs(plan.later) do
            index = index + 1
            local id = quest.id
            SetLine(index, "    |cff999999- " .. quest.title .. " (" .. quest.reason .. ")|r",
                "Now", function() Send("BOTPLANNOW " .. name .. " " .. id) end)
        end
    end
    for extra = index + 1, #lines do
        lines[extra]:Hide()
    end
    Shown(empty, index == 0)
    panel:SetHeight(math.max(80, 52 + index * ROW_HEIGHT))
end

local function Read(name, rest)
    local status, doingId, doingTitle, list, later = string.match(rest, "^(.-)|(%d+):([^|]*)|([^|]*)|?(.*)$")
    if not status then
        status, doingId, list = string.match(rest, "^(.-)|(%d+)|(.*)$")
        doingTitle, later = "", ""
        if doingId and doingId ~= "0" then
            doingTitle = string.match(";" .. list, ";" .. doingId .. ":([^;]*)") or ""
        end
    end
    if not status then
        return
    end
    local plan = { name = name, status = status, route = {}, later = {} }
    if doingId ~= "0" and doingTitle ~= "" then
        plan.doing = doingTitle
    end
    for id, questTitle in string.gmatch(list, "(%d+):([^;]*)") do
        plan.route[#plan.route + 1] = { id = tonumber(id), title = questTitle }
    end
    for id, reason, questTitle in string.gmatch(later, "(%d+):([^:;]*):([^;]*)") do
        plan.later[#plan.later + 1] = { id = tonumber(id), reason = reason, title = questTitle }
    end
    incoming[#incoming + 1] = plan
end

local events = CreateFrame("Frame")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function(_, _, prefix, message, _, sender)
    if prefix ~= PREFIX or sender ~= UnitName("player") then
        return
    end
    local name, rest = string.match(message, "^BOTPLAN (%S+) (.*)$")
    if name then
        Read(name, rest)
    elseif message == "BOTPLANEND" then
        plans, incoming = incoming, {}
        if panel:IsShown() then
            Render()
        end
    end
end)

panel:SetScript("OnUpdate", function(self, elapsed)
    self.wait = (self.wait or REFRESH_SECONDS) + elapsed
    if self.wait < REFRESH_SECONDS then
        return
    end
    self.wait = 0
    incoming = {}
    Send("BOTPLAN")
end)

panel:SetScript("OnShow", function(self)
    self.wait = REFRESH_SECONDS
    Render()
end)

local function TogglePlans()
    if panel:IsShown() then
        panel:Hide()
    else
        panel:Show()
    end
end

SLASH_COACBOTPLAN1 = "/botplan"
SLASH_COACBOTPLAN2 = "/bp"
SlashCmdList.COACBOTPLAN = TogglePlans

local function PlansTooltip(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetText("Bot plans")
    GameTooltip:AddLine("Your bots' quest routes: what they do now, what comes next, and the order.", 1, 1, 1, true)
    GameTooltip:Show()
end

if QuestLogFrame then
    local questButton = CreateFrame("Button", "CoACBotPlanQuestLogButton", QuestLogFrame, "UIPanelButtonTemplate")
    questButton:SetWidth(80)
    questButton:SetHeight(20)
    questButton:SetText("Bot plans")
    if QuestLogFrameShowMapButton then
        questButton:SetPoint("RIGHT", QuestLogFrameShowMapButton, "LEFT", -4, 0)
    else
        questButton:SetPoint("TOPRIGHT", QuestLogFrame, "TOPRIGHT", -110, -38)
    end
    questButton:SetScript("OnClick", TogglePlans)
    questButton:SetScript("OnEnter", PlansTooltip)
    questButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
end

local barButton
local function BarButton(bar)
    barButton = CreateFrame("Button", "CoACBotPlanBarButton", bar)
    barButton:SetWidth(34)
    barButton:SetHeight(34)
    barButton:SetPoint("TOPLEFT", bar, "TOPRIGHT", 3, 0)
    barButton.icon = barButton:CreateTexture(nil, "ARTWORK")
    barButton.icon:SetAllPoints()
    barButton.icon:SetTexture("Interface\\Icons\\INV_Misc_Map_01")
    barButton.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
    barButton.ring = barButton:CreateTexture(nil, "OVERLAY")
    barButton.ring:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
    barButton.ring:SetBlendMode("ADD")
    barButton.ring:SetWidth(62)
    barButton.ring:SetHeight(62)
    barButton.ring:SetPoint("CENTER")
    barButton.ring:SetAlpha(0.8)
    barButton.caption = barButton:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    local like = bar.last and bar.last.caption
    if like then
        local font, size, flags = like:GetFont()
        barButton.caption:SetFont(font, size, flags)
    end
    barButton.caption:SetPoint("TOP", barButton, "BOTTOM", 0, -1)
    barButton.caption:SetText("Plans")
    barButton:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    barButton:SetScript("OnClick", TogglePlans)
    barButton:SetScript("OnEnter", PlansTooltip)
    barButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
end

local barWatch = CreateFrame("Frame")
barWatch.wait = 0
barWatch:SetScript("OnUpdate", function(self, elapsed)
    self.wait = self.wait - elapsed
    if self.wait > 0 then
        return
    end
    self.wait = 1
    if SquidBotsLiteBar and SquidBotsLiteBar.last then
        BarButton(SquidBotsLiteBar)
        self:SetScript("OnUpdate", nil)
    end
end)

local function IsSquidMenu(menuList, menuFrame)
    return menuFrame ~= nil and menuFrame == SquidBotsLiteMenu and type(menuList) == "table" and
        menuList[1] ~= nil and menuList[1].isTitle
end

if EasyMenu then
    hooksecurefunc("EasyMenu", function(menuList, menuFrame)
        if not IsSquidMenu(menuList, menuFrame) or not DropDownList1 or not DropDownList1:IsShown() then
            return
        end
        UIDropDownMenu_AddButton({
            text = "Bot plans (quest route)",
            notCheckable = true,
            func = TogglePlans,
        }, 1)
    end)
end
