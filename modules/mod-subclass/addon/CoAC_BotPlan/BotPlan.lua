local PREFIX = "SUBCLASS"
local ROW_HEIGHT = 16
local REFRESH_SECONDS = 3

local plans, incoming = {}, {}
local oneBody = false

local panel = CreateFrame("Frame", "CoACBotPlanFrame", UIParent)
panel:SetWidth(440)
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

local function IgnoredOf(name)
    CoACBotPlanIgnored = CoACBotPlanIgnored or {}
    CoACBotPlanIgnored[name] = CoACBotPlanIgnored[name] or {}
    return CoACBotPlanIgnored[name]
end

local function Choose(command, name, id)
    IgnoredOf(name)[id] = command == "BOTPLANIGNORE" or nil
    Send(command .. " " .. name .. " " .. id)
end

local function ActionButton(line, width)
    local button = CreateFrame("Button", nil, line, "UIPanelButtonTemplate")
    button:SetWidth(width or 44)
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
    line.text:SetPoint("RIGHT", -156, 0)
    line.text:SetJustifyH("LEFT")
    line:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    line.third = ActionButton(line, 56)
    line.third:SetPoint("RIGHT")
    line.action = ActionButton(line)
    line.action:SetPoint("RIGHT", line.third, "LEFT", -4, 0)
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

local function SetLine(index, text, actionText, onAction, secondText, onSecond, thirdText, onThird)
    local line = Line(index)
    line.text:SetText(text)
    SetButton(line.action, actionText, onAction)
    SetButton(line.second, secondText, onSecond)
    SetButton(line.third, thirdText, onThird)
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
                (position > 1 or id ~= plan.doingId) and "Now" or nil,
                function() Choose("BOTPLANNOW", name, id) end,
                position > 1 and "Up" or nil, function() Send("BOTPLANUP " .. name .. " " .. id) end,
                "Ignore", function() Choose("BOTPLANIGNORE", name, id) end)
        end
        if #plan.route == 0 then
            index = index + 1
            SetLine(index, "    no quests planned")
        end
        for _, quest in ipairs(plan.later) do
            index = index + 1
            local id = quest.id
            local text = "    |cff999999- " .. quest.title .. " (" .. quest.reason .. ")|r"
            if quest.reason == "ignored" then
                SetLine(index, text, nil, nil, nil, nil, "Allow", function() Choose("BOTPLANALLOW", name, id) end)
            else
                SetLine(index, text, "Now", function() Choose("BOTPLANNOW", name, id) end, nil, nil,
                    "Ignore", function() Choose("BOTPLANIGNORE", name, id) end)
            end
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
        plan.doingId = tonumber(doingId)
    end
    for id, questTitle in string.gmatch(list, "(%d+):([^;]*)") do
        plan.route[#plan.route + 1] = { id = tonumber(id), title = questTitle }
    end
    for id, reason, questTitle in string.gmatch(later, "(%d+):([^:;]*):([^;]*)") do
        plan.later[#plan.later + 1] = { id = tonumber(id), reason = reason, title = questTitle }
    end
    incoming[#incoming + 1] = plan
end

local function RestoreIgnored()
    for _, plan in ipairs(plans) do
        local saved, inLog, listed = IgnoredOf(plan.name), {}, 0
        for _, quest in ipairs(plan.route) do
            inLog[quest.id], listed = "planned", listed + 1
        end
        for _, quest in ipairs(plan.later) do
            inLog[quest.id], listed = quest.reason == "ignored" and "ignored" or "planned", listed + 1
        end
        if plan.doingId then
            inLog[plan.doingId] = inLog[plan.doingId] or "planned"
        end
        for id in pairs(saved) do
            if inLog[id] == "planned" then
                Send("BOTPLANIGNORE " .. plan.name .. " " .. id)
            elseif not inLog[id] and listed > 0 then
                saved[id] = nil
            end
        end
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("PARTY_MEMBERS_CHANGED")
events:SetScript("OnEvent", function(self, event, prefix, message, _, sender)
    if event ~= "CHAT_MSG_ADDON" then
        self.ask = 5
        if event == "PLAYER_ENTERING_WORLD" then
            Send("PAIRSTATEASK")
        end
        return
    end
    if prefix ~= PREFIX or sender ~= UnitName("player") then
        return
    end
    local head = string.match(message, "^PAIRSTATE (%u+)")
    if head then
        oneBody = head == "LEFT" or head == "RIGHT"
        if not oneBody and CoACCourierFrame then
            CoACCourierFrame:Hide()
        end
        return
    end
    local name, rest = string.match(message, "^BOTPLAN (%S+) (.*)$")
    if name then
        Read(name, rest)
    elseif message == "BOTPLANEND" then
        plans, incoming = incoming, {}
        RestoreIgnored()
        if panel:IsShown() then
            Render()
        end
    end
end)

events:SetScript("OnUpdate", function(self, elapsed)
    if not self.ask then
        return
    end
    self.ask = self.ask - elapsed
    if self.ask > 0 then
        return
    end
    self.ask = nil
    if not panel:IsShown() then
        incoming = {}
        Send("BOTPLAN")
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
    GameTooltip:AddLine("Your bots' quest routes: what they do now, what comes next, the order, and which quests " ..
        "they ignore.", 1, 1, 1, true)
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

local function RideTooltip(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetText("Ride")
    GameTooltip:AddLine("Your bot body mounts up now, or puts the mount down. He also mounts by himself for long " ..
        "trips and gets down for fights.", 1, 1, 1, true)
    GameTooltip:Show()
end

local function JunkTooltip(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:SetText("Junk")
    GameTooltip:AddLine("Both heads hand their grey and marked items to a courier, who comes back with the coins " ..
        "in a few minutes, and throw away leftover quest items. No vendor needed. Your bot body sends one by " ..
        "himself when his bags are full.", 1, 1, 1, true)
    GameTooltip:AddLine("Right-click: open the courier bag to see what goes, or mark more to sell.", 0, 1, 0, true)
    GameTooltip:Show()
end

local barButtons = {}
local function BarButton(bar, name, icon, caption, onClick, onEnter, pairOnly)
    local button = CreateFrame("Button", name, bar)
    button:SetWidth(34)
    button:SetHeight(34)
    button.icon = button:CreateTexture(nil, "ARTWORK")
    button.icon:SetAllPoints()
    button.icon:SetTexture(icon)
    button.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)
    button.ring = button:CreateTexture(nil, "OVERLAY")
    button.ring:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
    button.ring:SetBlendMode("ADD")
    button.ring:SetWidth(62)
    button.ring:SetHeight(62)
    button.ring:SetPoint("CENTER")
    button.ring:SetAlpha(0.8)
    button.caption = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    local like = bar.last and bar.last.caption
    if like then
        local font, size, flags = like:GetFont()
        button.caption:SetFont(font, size, flags)
    end
    button.caption:SetPoint("TOP", button, "BOTTOM", 0, -1)
    button.caption:SetText(caption)
    button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    button:SetScript("OnClick", onClick)
    button:SetScript("OnEnter", onEnter)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    barButtons[#barButtons + 1] = button
    button.ours = true
    button.pairOnly = pairOnly
end

local function PlaceBarButtons(bar)
    for _, button in ipairs(barButtons) do
        Shown(button, oneBody or not button.pairOnly)
    end
    local places = {}
    for _, child in ipairs({ bar:GetChildren() }) do
        if not child.ours and child.caption and child:IsShown() then
            local point, _, _, x = child:GetPoint()
            if point == "TOPLEFT" and x then
                places[#places + 1] = x
            end
        end
    end
    if #places < 2 then
        return
    end
    table.sort(places)
    local step
    for index = 2, #places do
        local gap = places[index] - places[index - 1]
        if gap > 0 and (not step or gap < step) then
            step = gap
        end
    end
    local index = 0
    for _, button in ipairs(barButtons) do
        if oneBody or not button.pairOnly then
            index = index + 1
            local x = places[#places] + (step or 0) * index
            if button.x ~= x then
                button.x = x
                button:ClearAllPoints()
                button:SetPoint("TOPLEFT", bar, "TOPLEFT", x, 0)
            end
        end
    end
end

local barWatch = CreateFrame("Frame")
barWatch.wait = 0
barWatch:SetScript("OnUpdate", function(self, elapsed)
    self.wait = self.wait - elapsed
    if self.wait > 0 then
        return
    end
    self.wait = 1
    if #barButtons == 0 and SquidBotsLiteBar and SquidBotsLiteBar.last then
        BarButton(SquidBotsLiteBar, "CoACBotPlanBarButton", "Interface\\Icons\\INV_Misc_Map_01", "Plans",
            TogglePlans, PlansTooltip)
        BarButton(SquidBotsLiteBar, "CoACBotPlanRideButton", "Interface\\Icons\\Ability_Mount_RidingHorse", "Ride",
            function() Send("PAIRMOUNT") end, RideTooltip, true)
        BarButton(SquidBotsLiteBar, "CoACBotPlanJunkButton", "Interface\\Icons\\INV_Misc_Coin_02", "Junk",
            function(_, mouse)
                if mouse == "RightButton" and CoACCourierToggle then
                    CoACCourierToggle()
                else
                    Send("PAIRJUNK")
                end
            end, JunkTooltip, true)
        CoACBotPlanJunkButton:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    end
    if #barButtons > 0 then
        PlaceBarButtons(SquidBotsLiteBar)
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
