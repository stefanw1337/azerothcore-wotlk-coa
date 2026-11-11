local PREFIX = "SUBCLASS"
local ROUTE_ROWS = 6
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
    line.text:SetPoint("RIGHT", -60, 0)
    line.text:SetJustifyH("LEFT")
    line:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    line.action = CreateFrame("Button", nil, line, "UIPanelButtonTemplate")
    line.action:SetWidth(56)
    line.action:SetHeight(ROW_HEIGHT)
    line.action:SetPoint("RIGHT")
    lines[index] = line
    return line
end

local function SetLine(index, text, actionText, onAction, onClick)
    local line = Line(index)
    line.text:SetText(text)
    line.action:SetText(actionText or "")
    Shown(line.action, actionText ~= nil)
    line.action:SetScript("OnClick", onAction)
    line:SetScript("OnClick", onClick)
    line:EnableMouse(onClick ~= nil)
    line:Show()
end

local function Render()
    local index = 0
    for _, plan in ipairs(plans) do
        index = index + 1
        local doing = plan.doing and ("  |cffffd100" .. plan.doing .. "|r") or ""
        local sheet = SquidBotsLite_OpenSheet and function() SquidBotsLite_OpenSheet(plan.name) end or nil
        SetLine(index, "|cff66ccff" .. plan.name .. "|r  " .. plan.status .. doing, sheet and "Sheet" or nil, sheet)
        for position, quest in ipairs(plan.route) do
            if position > ROUTE_ROWS then
                index = index + 1
                SetLine(index, "    ... " .. (#plan.route - ROUTE_ROWS) .. " more")
                break
            end
            index = index + 1
            local name, id = plan.name, quest.id
            SetLine(index, "    " .. position .. ". " .. quest.title, position > 1 and "Do now" or nil,
                function() Send("BOTPLANNOW " .. name .. " " .. id) end)
        end
        if #plan.route == 0 then
            index = index + 1
            SetLine(index, "    no quests planned")
        end
    end
    for extra = index + 1, #lines do
        lines[extra]:Hide()
    end
    Shown(empty, index == 0)
    panel:SetHeight(math.max(80, 52 + index * ROW_HEIGHT))
end

local function Read(name, rest)
    local status, doingId, list = string.match(rest, "^(.-)|(%d+)|(.*)$")
    if not status then
        return
    end
    local plan = { name = name, status = status, route = {} }
    for id, questTitle in string.gmatch(list, "(%d+):([^;]*)") do
        plan.route[#plan.route + 1] = { id = tonumber(id), title = questTitle }
        if id == doingId then
            plan.doing = questTitle
        end
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

SLASH_COACBOTPLAN1 = "/botplan"
SLASH_COACBOTPLAN2 = "/bp"
SlashCmdList.COACBOTPLAN = function()
    if panel:IsShown() then
        panel:Hide()
    else
        panel:Show()
    end
end
