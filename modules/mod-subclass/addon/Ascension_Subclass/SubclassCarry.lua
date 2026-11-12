local TWO_PLAYERS_RACE = "Two-Headed Ogre"
local MALE = 2
local FIELDS = {
    { key = "scale", label = "Size", step = 0.05 },
    { key = "yaw", label = "Yaw", step = 5 },
    { key = "pitch", label = "Pitch", step = 5 },
    { key = "roll", label = "Roll", step = 5 },
    { key = "x", label = "Forward (X)", step = 0.1 },
    { key = "y", label = "Side (Y)", step = 0.1 },
    { key = "z", label = "Up (Z)", step = 0.1 },
}
local EXTRA_FIELDS = { "as", "pose", "frame" }
local PROFILES = {
    { name = "WoW horse", scale = 1, yaw = 0, pitch = 0, roll = 0, x = 0, y = 0.5, z = -1 },
    { name = "HotS horse", scale = 1, yaw = 10, pitch = 0, roll = 0, x = -0.2, y = 0, z = -0.6 },
    { name = "Sheep", scale = 1, yaw = 10, pitch = 0, roll = 0, x = -0.2, y = 0, z = -0.3 },
    { name = "Battle Beast", scale = 0.7, yaw = 10, pitch = 0, roll = -90, x = 0, y = -0.7, z = 0.5 },
    { name = "Billie", scale = 1, yaw = 15, pitch = 0, roll = 0, x = 0, y = -0.1, z = 0 },
    { name = "Seeker", scale = 0.8, yaw = 15, pitch = 0, roll = 0, x = 0, y = 0, z = -0.2 },
    { name = "Crimson Hare", scale = 0.8, yaw = 15, pitch = 0, roll = 0, x = 0, y = 0, z = -0.5 },
    { name = "Phoenix", scale = 0.8, yaw = 15, pitch = 0, roll = 0, x = -0.2, y = -0.1, z = -0.9 },
    { name = "Charger", scale = 0.8, yaw = 15, pitch = 0, roll = 0, x = -0.2, y = -0.1, z = -0.4 },
    { name = "Surfboard", scale = 1, yaw = 10, pitch = 0, roll = -90, x = 0, y = -0.6, z = 0.5 },
    { name = "Turtle", scale = 0.5, yaw = 10, pitch = 0, roll = -90, x = 0, y = -0.5, z = 0.5 },
    { name = "Reaver", scale = 0.7, yaw = 15, pitch = 0, roll = 0, x = -0.4, y = -0.2, z = -0.2 },
    { name = "Bar / Card", scale = 0.8, yaw = 15, pitch = 0, roll = 90, x = -0.2, y = 0.4, z = 0.7 },
    { name = "Disc (Nexagon)", scale = 1, yaw = 0.7, pitch = 0, roll = 90, x = 0, y = 0.3, z = 1 },
    { name = "D.R.I.V.E. car", scale = 0.8, yaw = 0, pitch = 0, roll = 90, x = 0, y = 0.5, z = 1.3 },
}

local function CVarName(key)
    return "coaCarry" .. key:sub(1, 1):upper() .. key:sub(2)
end

local function Read(key)
    local ok, value = pcall(GetCVar, CVarName(key))
    return ok and value or nil
end

local function Write(key, value)
    pcall(SetCVar, CVarName(key), tostring(value))
end

local function Number(value)
    return tonumber(("%.3f"):format(tonumber(value) or 0))
end

local function IsTwoPlayersBody()
    return UnitRace("player") == TWO_PLAYERS_RACE and UnitSex("player") == MALE
end

local function Say(text)
    DEFAULT_CHAT_FRAME:AddMessage("|cff66ccffCarry:|r " .. text)
end

local panel = CreateFrame("Frame", "SubclassCarryPanel", UIParent)
panel:SetWidth(230)
panel:SetHeight(398)
panel:SetPoint("CENTER")
panel:SetFrameStrata("HIGH")
panel:SetMovable(true)
panel:EnableMouse(true)
panel:RegisterForDrag("LeftButton")
panel:SetScript("OnDragStart", panel.StartMoving)
panel:SetScript("OnDragStop", panel.StopMovingOrSizing)
panel:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 24,
    insets = { left = 6, right = 6, top = 6, bottom = 6 },
})
panel:Hide()
tinsert(UISpecialFrames, "SubclassCarryPanel")

local title = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOP", 0, -14)
title:SetText("Carried mount")

local close = CreateFrame("Button", nil, panel, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -2, -2)

local status = panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
status:SetPoint("TOP", title, "BOTTOM", 0, -4)

local rows = {}
for index, field in ipairs(FIELDS) do
    local row = CreateFrame("Frame", nil, panel)
    row:SetWidth(210)
    row:SetHeight(24)
    row:SetPoint("TOPLEFT", 12, -44 - (index - 1) * 26)
    local label = row:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    label:SetPoint("LEFT", 0, 0)
    label:SetText(field.label)
    local box = CreateFrame("EditBox", "SubclassCarry" .. field.key, row, "InputBoxTemplate")
    box:SetWidth(50)
    box:SetHeight(20)
    box:SetPoint("LEFT", 92, 0)
    box:SetAutoFocus(false)
    box:SetScript("OnEnterPressed", function(self)
        Write(field.key, Number(self:GetText()))
        self:ClearFocus()
    end)
    box:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    local minus = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    minus:SetWidth(24)
    minus:SetHeight(20)
    minus:SetPoint("LEFT", box, "RIGHT", 4, 0)
    minus:SetText("-")
    minus:SetScript("OnClick", function()
        Write(field.key, Number((tonumber(Read(field.key)) or 0) - field.step))
    end)
    local plus = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    plus:SetWidth(24)
    plus:SetHeight(20)
    plus:SetPoint("LEFT", minus, "RIGHT", 2, 0)
    plus:SetText("+")
    plus:SetScript("OnClick", function()
        Write(field.key, Number((tonumber(Read(field.key)) or 0) + field.step))
    end)
    rows[field.key] = box
end

local function Button(text, width, anchor, x, y)
    local button = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
    button:SetWidth(width)
    button:SetHeight(22)
    button:SetPoint("TOPLEFT", anchor, "BOTTOMLEFT", x, y)
    button:SetText(text)
    return button
end

local profileMenu = CreateFrame("Frame", "SubclassCarryProfileMenu", panel, "UIDropDownMenuTemplate")
UIDropDownMenu_Initialize(profileMenu, function()
    for _, profile in ipairs(PROFILES) do
        local info = UIDropDownMenu_CreateInfo()
        info.text = profile.name
        info.notCheckable = 1
        info.func = function()
            for _, field in ipairs(FIELDS) do
                Write(field.key, profile[field.key])
            end
            Say(profile.name .. " fit applied.")
        end
        UIDropDownMenu_AddButton(info)
    end
end, "MENU")

local lastRow = rows.z:GetParent()
local profiles = Button("Profiles", 100, lastRow, 0, -10)
profiles:SetScript("OnClick", function(self) ToggleDropDownMenu(1, nil, profileMenu, self, 0, 0) end)

local reset = Button("Reset", 100, lastRow, 106, -10)
reset:SetScript("OnClick", function()
    Write("reset", 1)
    Say("back to the built-in fit for this mount.")
end)

local codeLabel = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
codeLabel:SetPoint("TOPLEFT", profiles, "BOTTOMLEFT", 0, -12)
codeLabel:SetText("Share code (Ctrl+C / Ctrl+V)")

local code = CreateFrame("EditBox", "SubclassCarryCode", panel, "InputBoxTemplate")
code:SetWidth(200)
code:SetHeight(20)
code:SetPoint("TOPLEFT", codeLabel, "BOTTOMLEFT", 6, -4)
code:SetAutoFocus(false)
code:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)

local function ExportCode()
    local parts = { Read("display") or "0" }
    for _, field in ipairs(FIELDS) do
        parts[#parts + 1] = ("%s=%s"):format(field.key, Read(field.key) or "0")
    end
    for _, key in ipairs(EXTRA_FIELDS) do
        parts[#parts + 1] = ("%s=%s"):format(key, Read(key) or "0")
    end
    return table.concat(parts, " ")
end

local function ImportCode(text)
    local display = tonumber(text:match("^%s*(%d+)"))
    local carried = tonumber(Read("display"))
    if display and display ~= 0 and carried and display ~= carried then
        Say(("that code is for mount display %d; you carry %d."):format(display, carried))
        return
    end
    local applied = 0
    for key, value in text:gmatch("(%a+)=(%-?[%d%.]+)") do
        key = key:lower()
        if key ~= "display" and key ~= "reset" then
            Write(key, Number(value))
            applied = applied + 1
        end
    end
    Say(applied > 0 and "code applied." or "no values in that code.")
end

local export = Button("Export", 100, code, -6, -6)
export:SetScript("OnClick", function()
    code:SetText(ExportCode())
    code:SetFocus()
    code:HighlightText()
end)

local import = Button("Import", 100, code, 100, -6)
import:SetScript("OnClick", function()
    ImportCode(code:GetText())
    code:ClearFocus()
end)

local hint = panel:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
hint:SetPoint("TOPLEFT", export, "BOTTOMLEFT", 0, -8)
hint:SetWidth(206)
hint:SetJustifyH("LEFT")
hint:SetText("Changes apply to the mount you carry and are saved for it on this PC.")

local elapsed = 0
panel:SetScript("OnUpdate", function(self, delta)
    elapsed = elapsed + delta
    if elapsed < 0.3 then
        return
    end
    elapsed = 0
    local display = Read("display")
    if display and display ~= "0" then
        status:SetText("Mount display " .. display)
    else
        status:SetText(Read("scale") and "Carried mount" or "Pick up a mount first")
    end
    for key, box in pairs(rows) do
        if not box:HasFocus() then
            box:SetText(Read(key) or "")
        end
    end
end)

local function Dock()
    local journal = CompanionJournalFrame
    panel:ClearAllPoints()
    if journal and journal:IsShown() then
        panel:SetPoint("TOPLEFT", journal, "TOPRIGHT", -4, 0)
    else
        panel:SetPoint("CENTER")
    end
end

local function Toggle()
    if panel:IsShown() then
        panel:Hide()
        return
    end
    if not IsTwoPlayersBody() then
        Say("only the Two-Players Two-Headed Ogre carries his mount.")
        return
    end
    Dock()
    panel:Show()
end

local watcher = CreateFrame("Frame")
watcher:RegisterEvent("PLAYER_LOGIN")
watcher:SetScript("OnEvent", function()
    local journal = CompanionJournalFrame
    if not journal then
        return
    end
    journal:HookScript("OnShow", function()
        if IsTwoPlayersBody() then
            Dock()
            panel:Show()
        end
    end)
    journal:HookScript("OnHide", function() panel:Hide() end)
end)

SLASH_SUBCLASSCARRY1 = "/carry"
SlashCmdList.SUBCLASSCARRY = Toggle
