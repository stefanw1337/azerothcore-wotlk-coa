local PREFIX = "SUBCLASS"
local BONK_SPELL = 9530133
local BONK_NAME = "Bonk Heads!"
local BAR_BUTTONS = {
    "ActionButton", "MultiBarBottomLeftButton", "MultiBarBottomRightButton", "MultiBarRightButton",
    "MultiBarLeftButton", "BonusActionButton",
}

local ready = false
local glowing = {}
local LBG = LibStub and LibStub("LibButtonGlow-1.0", true)

local function BonkName()
    return GetSpellInfo(BONK_SPELL) or BONK_NAME
end

local function IsBonk(slot)
    if not slot or not HasAction(slot) then
        return false
    end
    local kind, id = GetActionInfo(slot)
    if kind ~= "spell" or not id then
        return false
    end
    if id == BONK_SPELL then
        return true
    end
    local name = GetSpellName(id, BOOKTYPE_SPELL) or GetSpellInfo(id)
    return name == BonkName()
end

local function SlotOf(button)
    local slot = button.action
    if type(slot) ~= "number" and button.GetAttribute then
        slot = tonumber(button:GetAttribute("action"))
    end
    if type(slot) ~= "number" and ActionButton_GetPagedID then
        local ok, paged = pcall(ActionButton_GetPagedID, button)
        slot = ok and paged or nil
    end
    return slot
end

local function OwnGlow(button)
    local border = button.coacBonkGlow
    if not border then
        border = button:CreateTexture(nil, "OVERLAY")
        border:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
        border:SetBlendMode("ADD")
        border:SetVertexColor(1, 0.8, 0.2)
        border:SetPoint("CENTER")
        border:SetWidth(button:GetWidth() * 1.9)
        border:SetHeight(button:GetHeight() * 1.9)
        button.coacBonkGlow = border
    end
    return border
end

local function SetGlow(button, on)
    if LBG then
        if on then
            LBG.ShowOverlayGlow(button)
        else
            LBG.HideOverlayGlow(button)
        end
        return
    end
    if on then
        OwnGlow(button):Show()
    elseif button.coacBonkGlow then
        button.coacBonkGlow:Hide()
    end
end

local function Candidates()
    local seen, list = {}, {}
    local function Add(button)
        if button and not seen[button] then
            seen[button] = true
            list[#list + 1] = button
        end
    end
    for _, prefix in ipairs(BAR_BUTTONS) do
        for index = 1, 12 do
            Add(_G[prefix .. index])
        end
    end
    for index = 1, 120 do
        Add(_G["BT4Button" .. index])
    end
    local frame = EnumerateFrames()
    while frame do
        if frame.action and frame.IsObjectType and frame:IsObjectType("CheckButton") then
            Add(frame)
        end
        frame = EnumerateFrames(frame)
    end
    return list
end

local function Refresh()
    for button in pairs(glowing) do
        SetGlow(button, false)
    end
    glowing = {}
    if not ready then
        return
    end
    for _, button in ipairs(Candidates()) do
        if button:IsVisible() and IsBonk(SlotOf(button)) then
            SetGlow(button, true)
            glowing[button] = true
        end
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("ACTIONBAR_SLOT_CHANGED")
events:RegisterEvent("ACTIONBAR_PAGE_CHANGED")
events:RegisterEvent("UPDATE_BONUS_ACTIONBAR")
events:SetScript("OnEvent", function(_, event, prefix, message, _, sender)
    if event == "CHAT_MSG_ADDON" then
        if prefix ~= PREFIX or sender ~= UnitName("player") then
            return
        end
        if message == "BONKREADY" then
            ready = true
        elseif message == "BONKSPENT" then
            ready = false
        else
            return
        end
    end
    Refresh()
end)

local OGRE_RACE = "Two-Headed Ogre"
local BROTHERS_TEXT = "Fills while both brothers agree on their foes. When ready, both brothers press it within a "
    .. "second: the brother in front throws what he wields and the brother on his back lets his magic fly over the "
    .. "foes in front for 10 seconds."

GameTooltip:HookScript("OnTooltipSetSpell", function(tip)
    if tip:GetSpell() ~= BonkName() then
        return
    end
    local brothers = UnitRace("player") ~= OGRE_RACE
    for index = 2, tip:NumLines() do
        local line = _G[tip:GetName() .. "TextLeft" .. index]
        local text = line and line:GetText()
        if text and text:find("^No script") then
            line:SetText("")
        elseif text and brothers and text:find("both heads agree") then
            line:SetText(BROTHERS_TEXT)
        end
    end
    tip:Show()
end)
