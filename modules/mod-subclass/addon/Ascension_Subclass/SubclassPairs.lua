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

local function ShowFrozenTooltip(ice)
    GameTooltip:SetOwner(ice, "ANCHOR_RIGHT")
    if not GameTooltip:SetInventoryItem("player", ice.slot:GetID()) then
        GameTooltip:SetText((ice.slot:GetName():gsub("^Character", ""):gsub("Slot$", "")))
    end
    GameTooltip:AddLine("Frozen: the body wears this gear for both heads.", 0.5, 0.8, 1, true)
    GameTooltip:Show()
end

local function LinkFrozenItem(ice)
    local link = GetInventoryItemLink("player", ice.slot:GetID())
    if link and IsModifiedClick("CHATLINK") then
        ChatEdit_InsertLink(link)
    end
end

local function IceOver(button)
    if button.subclassIce then
        return button.subclassIce
    end
    local ice = CreateFrame("Button", nil, button)
    ice.slot = button
    ice:SetAllPoints(button)
    ice:SetFrameLevel(button:GetFrameLevel() + 5)
    ice:RegisterForClicks("AnyUp")
    ice:SetScript("OnClick", LinkFrozenItem)
    ice:SetScript("OnEnter", ShowFrozenTooltip)
    ice:SetScript("OnLeave", function() GameTooltip:Hide() end)
    local frost = ice:CreateTexture(nil, "OVERLAY")
    frost:SetAllPoints(ice)
    frost:SetTexture("Interface\\AddOns\\Ascension_Subclass\\Textures\\IceFrame")
    local lock = ice:CreateTexture(nil, "OVERLAY")
    lock:SetWidth(16)
    lock:SetHeight(16)
    lock:SetPoint("TOPRIGHT", ice, "TOPRIGHT", 0, 0)
    lock:SetTexture("Interface\\LFGFrame\\UI-LFG-ICON-LOCK")
    button.subclassIce = ice
    return ice
end

local function FreezeSlot(button)
    if not button or not GEAR_BUTTONS[button:GetName() or ""] then
        return
    end
    if pairState == "RIGHT" then
        IceOver(button):Show()
    elseif button.subclassIce then
        button.subclassIce:Hide()
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
    local body, rider = message:match("^TWOPLAYERS (%S+) (%S+)$")
    if body then
        Ascension_Subclass.SetOneBody(body, rider)
        return
    end
    local parted = message:match("^TWOPLAYERSOFF (%S+)$")
    if parted then
        Ascension_Subclass.SetOneBody(parted, nil)
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
local riders, joinedNames = {}, {}
local partyFrames = CreateFrame("Frame")

local function ShowJoinedName(unitFrame)
    local name = unitFrame and unitFrame.unit and unitFrame.name and UnitName(unitFrame.unit)
    if name and joinedNames[name] then
        unitFrame.name:SetText(joinedNames[name])
    end
end

local function HidePartner(partyFrame)
    if not partyFrame or not partyFrame.unit then
        return
    end
    ShowJoinedName(partyFrame)
    local name = UnitName(partyFrame.unit)
    if not name or (name ~= partnerName and not riders[name]) or not partyFrame:IsShown() then
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

local function JoinRaidNames()
    for index = 1, MAX_RAID_MEMBERS or 40 do
        local text = _G["RaidGroupButton" .. index .. "Name"]
        local name = text and text:GetText()
        if name and joinedNames[name] then
            text:SetText(joinedNames[name])
        end
    end
end

function Ascension_Subclass.SetOneBody(body, rider)
    for name, of in pairs(riders) do
        if of == body then
            riders[name] = nil
            joinedNames[name] = nil
        end
    end
    joinedNames[body] = rider and body .. "'" .. rider or nil
    if rider then
        riders[rider] = body
        joinedNames[rider] = joinedNames[body]
    end
    RefreshPartyFrames()
    if PlayerFrame and UnitFrame_Update then
        UnitFrame_Update(PlayerFrame)
    end
    if RaidGroupFrame_Update and RaidFrame and RaidFrame:IsShown() then
        RaidGroupFrame_Update()
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
if UnitFrame_Update then
    hooksecurefunc("UnitFrame_Update", ShowJoinedName)
end
if UnitFrame_OnEvent then
    hooksecurefunc("UnitFrame_OnEvent", ShowJoinedName)
end
partyFrames:RegisterEvent("PARTY_MEMBERS_CHANGED")
partyFrames:RegisterEvent("ADDON_LOADED")
partyFrames:SetScript("OnEvent", function(self, event, addon)
    if event == "ADDON_LOADED" then
        if addon == "Blizzard_RaidUI" and RaidGroupFrame_Update then
            hooksecurefunc("RaidGroupFrame_Update", JoinRaidNames)
        end
        return
    end
    if event == "PLAYER_REGEN_ENABLED" then
        self:UnregisterEvent("PLAYER_REGEN_ENABLED")
    end
    RefreshPartyFrames()
end)
if IsAddOnLoaded and IsAddOnLoaded("Blizzard_RaidUI") and RaidGroupFrame_Update then
    hooksecurefunc("RaidGroupFrame_Update", JoinRaidNames)
end

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
