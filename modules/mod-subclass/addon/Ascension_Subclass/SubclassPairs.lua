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
    text = "%s invites you to be the %s of their body.",
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
        AddInvite("Invite as Ranged Head", "PAIR")
    end
    if pairState == "ECHO" or ownHead ~= "LEFT" then
        AddInvite("Invite as Melee Head", "PAIRLEFT")
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
    elseif lower == "left" or lower:match("^left%s") then
        local name = strtrim(word:sub(5))
        if name == "" and UnitIsPlayer("target") and not UnitIsUnit("target", "player") then
            name = UnitName("target")
        end
        if name ~= "" then
            Send("PAIRLEFT " .. name)
        end
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
    local body = message:match("^PAIRINVITELEFT (.+)$")
    if body then
        StaticPopup_Show("SUBCLASS_PAIR_INVITE", body, "Melee Head")
        return
    end
    local head, fits = message:match("^PAIRSTATE (%u+)%s*(%u*)")
    if head then
        pairState = head
        if head == "NONE" then
            ownHead = fits ~= "" and fits or "ANY"
        end
    end
end)
