local PREFIX = "SUBCLASS"
local TWO_PLAYERS_RACE = "Two-Headed Ogre"
local MALE = 2
local pairState = "NONE"
local ownHead = "ANY"

local function Send(message)
    SendAddonMessage(PREFIX, message, "WHISPER", UnitName("player"))
end

local function IsTwoPlayersBody()
    return UnitRace("player") == TWO_PLAYERS_RACE and UnitSex("player") == MALE
end

StaticPopupDialogs["SUBCLASS_PAIR_INVITE"] = {
    text = "Pair request from %s: ride on their body as the %s?",
    button1 = "Join",
    button2 = "Cancel",
    OnAccept = function()
        Send("PAIRACCEPT")
    end,
    OnCancel = function(_, _, reason)
        if reason ~= "override" then
            Send("PAIRDECLINE")
        end
    end,
    timeout = 120,
    whileDead = 1,
    hideOnEscape = 1,
}

local MENUS = { PLAYER = true, PARTY = true, RAID_PLAYER = true, FRIEND = true, TARGET = true }

hooksecurefunc("UnitPopup_ShowMenu", function(_, which, unit, name)
    if UIDROPDOWNMENU_MENU_LEVEL ~= 1 or not MENUS[which] or not IsTwoPlayersBody() then
        return
    end
    if pairState ~= "NONE" and pairState ~= "ECHO" then
        return
    end
    local target = name or (unit and UnitName(unit))
    if not target or target == UnitName("player") or (unit and not UnitIsPlayer(unit)) then
        return
    end
    local function AddInvite(text, message)
        local info = UIDropDownMenu_CreateInfo()
        info.text = text
        info.notCheckable = 1
        info.func = function()
            Send(message .. " " .. target)
        end
        UIDropDownMenu_AddButton(info, 1)
    end
    if pairState == "NONE" and ownHead ~= "RIGHT" then
        AddInvite("Pair request", "PAIR")
    end
end)

SLASH_SUBCLASSPAIR1 = "/pair"
SlashCmdList.SUBCLASSPAIR = function(text)
    local word = strtrim(text or "")
    local lower = string.lower(word)
    if lower == "accept" then
        Send("PAIRACCEPT")
    elseif lower == "decline" then
        Send("PAIRDECLINE")
    elseif lower == "echo" then
        Send("PAIRECHO")
    elseif lower == "stop" then
        Send("PAIRSTOP")
    elseif lower == "go" then
        Send("PAIRGO")
    elseif lower == "" or lower == "status" then
        if word == "" and UnitIsPlayer("target") and not UnitIsUnit("target", "player") then
            Send("PAIR " .. UnitName("target"))
        else
            Send("PAIRSTATUS")
        end
    else
        Send("PAIR " .. word)
    end
end

local GEAR_SLOTS = { "Head", "Neck", "Shoulder", "Shirt", "Chest", "Waist", "Legs", "Feet", "Wrist", "Hands",
    "Finger0", "Finger1", "Trinket0", "Trinket1", "Back", "MainHand", "SecondaryHand", "Ranged", "Tabard" }

local GEAR_BUTTONS = {}
for _, name in ipairs(GEAR_SLOTS) do
    GEAR_BUTTONS["Character" .. name .. "Slot"] = true
end

local function FreezeSlot(button)
    if not button or not GEAR_BUTTONS[button:GetName() or ""] then
        return
    end
    local frozen = pairState == "RIGHT"
    if not button.subclassFrost then
        if not frozen then
            return
        end
        local frost = button:CreateTexture(nil, "OVERLAY")
        frost:SetAllPoints(button)
        frost:SetTexture("Interface\\Icons\\Spell_Frost_FrostArmor02")
        frost:SetBlendMode("ADD")
        frost:SetAlpha(0.45)
        local lock = button:CreateTexture(nil, "OVERLAY")
        lock:SetWidth(14)
        lock:SetHeight(14)
        lock:SetPoint("TOPRIGHT", button, "TOPRIGHT", -1, -1)
        lock:SetTexture("Interface\\LFGFrame\\UI-LFG-ICON-LOCK")
        button.subclassFrost = frost
        button.subclassLock = lock
    end
    if frozen then
        button.subclassFrost:Show()
        button.subclassLock:Show()
    else
        button.subclassFrost:Hide()
        button.subclassLock:Hide()
    end
end

local function FreezeGear()
    for _, name in ipairs(GEAR_SLOTS) do
        local button = _G["Character" .. name .. "Slot"]
        if button then
            FreezeSlot(button)
        end
    end
end

if PaperDollItemSlotButton_Update then
    hooksecurefunc("PaperDollItemSlotButton_Update", FreezeSlot)
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:SetScript("OnEvent", function(_, _, prefix, message, _, sender)
    if prefix ~= PREFIX or sender ~= UnitName("player") then
        return
    end
    local inviter = message:match("^PAIRINVITE (.+)$")
    if inviter then
        StaticPopup_Show("SUBCLASS_PAIR_INVITE", inviter, "Ranged Head")
        return
    end
    local head, fits = message:match("^PAIRSTATE (%u+)%s*(%u*)")
    if head then
        pairState = head
        if head == "NONE" then
            ownHead = fits ~= "" and fits or "ANY"
        end
        Ascension_Subclass.SetPartner(head ~= "NONE" and head ~= "ECHO" and message:match("^PAIRSTATE %u+ (.+)$")
            or nil)
        FreezeGear()
    end
end)

local partnerName
local partyFrames = CreateFrame("Frame")

local function HidePartner(partyFrame)
    if not partnerName or not partyFrame or not partyFrame.unit then
        return
    end
    if UnitName(partyFrame.unit) ~= partnerName or not partyFrame:IsShown() then
        return
    end
    if InCombatLockdown() then
        partyFrames:RegisterEvent("PLAYER_REGEN_ENABLED")
        return
    end
    partyFrame:Hide()
end

local function RefreshPartyFrames()
    if InCombatLockdown() then
        partyFrames:RegisterEvent("PLAYER_REGEN_ENABLED")
        return
    end
    for index = 1, MAX_PARTY_MEMBERS or 4 do
        local partyFrame = _G["PartyMemberFrame" .. index]
        if partyFrame and PartyMemberFrame_UpdateMember then
            PartyMemberFrame_UpdateMember(partyFrame)
        end
        HidePartner(partyFrame)
    end
end

function Ascension_Subclass.SetPartner(name)
    if name == partnerName then
        return
    end
    partnerName = name
    RefreshPartyFrames()
end

if PartyMemberFrame_UpdateMember then
    hooksecurefunc("PartyMemberFrame_UpdateMember", HidePartner)
end
partyFrames:RegisterEvent("PARTY_MEMBERS_CHANGED")
partyFrames:SetScript("OnEvent", function(self, event)
    if event == "PLAYER_REGEN_ENABLED" then
        self:UnregisterEvent("PLAYER_REGEN_ENABLED")
    end
    RefreshPartyFrames()
end)

local function HideLeaveButton(button)
    if pairState == "RIGHT" and button and button:IsShown() then
        button:Hide()
    end
end

if MainMenuBarVehicleLeaveButton then
    MainMenuBarVehicleLeaveButton:HookScript("OnShow", HideLeaveButton)
end
if MainMenuBarVehicleLeaveButton_Update then
    hooksecurefunc("MainMenuBarVehicleLeaveButton_Update", function()
        HideLeaveButton(MainMenuBarVehicleLeaveButton)
    end)
end
