local BUTTONS, SIZE, GAP = 12, 36, 4
local BINDING = "ASCENSION_SUBCLASS_BUTTON"
local UPDATE_EVERY = 0.2

BINDING_HEADER_ASCENSION_SUBCLASS = "Subclass Bar 1"
for index = 1, BUTTONS do
    _G["BINDING_NAME_" .. BINDING .. index] = "Other head button " .. index
end

local bar
local buttons = {}
local knownSpells = {}

local function Saved()
    Ascension_SubclassDump = Ascension_SubclassDump or {}
    Ascension_SubclassDump.subclassBars = Ascension_SubclassDump.subclassBars or {}
    local key = (UnitName("player") or "?") .. "-" .. (GetRealmName() or "?")
    Ascension_SubclassDump.subclassBars[key] = Ascension_SubclassDump.subclassBars[key] or {}
    return Ascension_SubclassDump.subclassBars[key]
end

local function SpellIdOfLink(link)
    return link and tonumber(string.match(link, "spell:(%d+)"))
end

local function SpellbookIndex(spellId)
    local index = 1
    while true do
        local name = GetSpellName(index, BOOKTYPE_SPELL)
        if not name then
            return nil
        end
        if SpellIdOfLink(GetSpellLink(index, BOOKTYPE_SPELL)) == spellId then
            return index
        end
        index = index + 1
    end
end

local function CursorSpell()
    local kind, slot, bookType = GetCursorInfo()
    if kind ~= "spell" then
        return nil
    end
    return SpellIdOfLink(GetSpellLink(slot, bookType))
end

local function ShortKey(key)
    if not key then
        return ""
    end
    key = string.gsub(key, "SHIFT%-", "S")
    key = string.gsub(key, "CTRL%-", "C")
    key = string.gsub(key, "ALT%-", "A")
    key = string.gsub(key, "NUMPAD", "N")
    key = string.gsub(key, "BUTTON", "M")
    return key
end

local function Refresh(button)
    local spellId = Saved()[button:GetID()]
    local name, icon
    if spellId then
        local spellName, _, spellIcon = GetSpellInfo(spellId)
        name, icon = spellName, spellIcon
    end
    button.spellName = name
    button.icon:SetTexture(icon)
    if icon then
        button.icon:Show()
    else
        button.icon:Hide()
        button.cooldown:Hide()
    end
    button.hotkey:SetText(ShortKey(GetBindingKey(BINDING .. button:GetID())))
end

local function RefreshAll()
    for _, button in ipairs(buttons) do
        Refresh(button)
    end
end

local function Place(button, spellId)
    local saved = Saved()
    local previous = saved[button:GetID()]
    saved[button:GetID()] = spellId
    ClearCursor()
    if previous and previous ~= spellId then
        local index = SpellbookIndex(previous)
        if index then
            PickupSpell(index, BOOKTYPE_SPELL)
        end
    end
    Refresh(button)
end

local function Press(button)
    local spellId = Saved()[button:GetID()]
    if spellId then
        Ascension_Subclass.Send("PRESS " .. spellId)
    end
end

local function OnClick(button, mouse)
    local dropped = CursorSpell()
    if dropped then
        Place(button, dropped)
    elseif mouse == "RightButton" and IsShiftKeyDown() then
        Saved()[button:GetID()] = nil
        Refresh(button)
    else
        Press(button)
    end
    button:SetChecked(false)
end

local function OnDragStart(button)
    if InCombatLockdown() and not IsShiftKeyDown() then
        return
    end
    local spellId = Saved()[button:GetID()]
    local index = spellId and SpellbookIndex(spellId)
    if index then
        Saved()[button:GetID()] = nil
        PickupSpell(index, BOOKTYPE_SPELL)
        Refresh(button)
    end
end

local function OnReceiveDrag(button)
    local dropped = CursorSpell()
    if dropped then
        Place(button, dropped)
    end
end

local function OnEnter(button)
    local spellId = Saved()[button:GetID()]
    GameTooltip:SetOwner(button, "ANCHOR_RIGHT")
    if spellId then
        GameTooltip:SetHyperlink("spell:" .. spellId)
        GameTooltip:AddLine("Cast by your other head, even while you cast.", 0.8, 0.6, 1)
    else
        GameTooltip:AddLine("Subclass Bar 1")
        GameTooltip:AddLine("Drop a subclass spell from your spellbook here.", 1, 1, 1)
    end
    GameTooltip:AddLine("Shift-right-click clears, shift-drag moves the bar.", 0.6, 0.6, 0.6)
    GameTooltip:Show()
end

local function UpdateButton(button)
    local name = button.spellName
    if not name then
        return
    end
    local start, duration, enabled = GetSpellCooldown(name)
    if start and duration and duration > 1.5 and enabled == 1 then
        button.cooldown:SetCooldown(start, duration)
        button.cooldown:Show()
    else
        button.cooldown:Hide()
    end
    local usable, noMana = IsUsableSpell(name)
    local inRange = IsSpellInRange(name, "target")
    if inRange == 0 then
        button.icon:SetVertexColor(0.8, 0.1, 0.1)
    elseif noMana then
        button.icon:SetVertexColor(0.5, 0.5, 1)
    elseif usable then
        button.icon:SetVertexColor(1, 1, 1)
    else
        button.icon:SetVertexColor(0.4, 0.4, 0.4)
    end
end

local function SavePosition()
    local point, _, relativePoint, x, y = bar:GetPoint(1)
    Ascension_SubclassDump = Ascension_SubclassDump or {}
    Ascension_SubclassDump.subclassBarPoint = { point, relativePoint, x, y }
end

local function CreateButton(index)
    local button = CreateFrame("CheckButton", "Ascension_SubclassBarButton" .. index, bar, "ActionButtonTemplate")
    button:SetID(index)
    button:SetWidth(SIZE)
    button:SetHeight(SIZE)
    button:SetPoint("LEFT", bar, "LEFT", (index - 1) * (SIZE + GAP), 0)
    button.icon = _G[button:GetName() .. "Icon"]
    button.cooldown = _G[button:GetName() .. "Cooldown"]
    button.hotkey = _G[button:GetName() .. "HotKey"]
    button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    button:RegisterForDrag("LeftButton")
    button:SetScript("OnClick", OnClick)
    button:SetScript("OnDragStart", function(self)
        if IsShiftKeyDown() then
            bar:StartMoving()
        else
            OnDragStart(self)
        end
    end)
    button:SetScript("OnDragStop", function()
        bar:StopMovingOrSizing()
        SavePosition()
    end)
    button:SetScript("OnReceiveDrag", OnReceiveDrag)
    button:SetScript("OnEnter", OnEnter)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    buttons[index] = button
    return button
end

local function HasAnything()
    local saved = Saved()
    for index = 1, BUTTONS do
        if saved[index] then
            return true
        end
    end
    return next(knownSpells) ~= nil
end

local function UpdateShown()
    if bar and HasAnything() then
        bar:Show()
    elseif bar then
        bar:Hide()
    end
end

local function CreateBar()
    bar = CreateFrame("Frame", "Ascension_SubclassBar", UIParent)
    bar:SetWidth(BUTTONS * (SIZE + GAP) - GAP)
    bar:SetHeight(SIZE)
    bar:SetMovable(true)
    bar:SetClampedToScreen(true)
    local point = Ascension_SubclassDump and Ascension_SubclassDump.subclassBarPoint
    if point then
        bar:SetPoint(point[1], UIParent, point[2], point[3], point[4])
    else
        bar:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 170)
    end
    for index = 1, BUTTONS do
        CreateButton(index)
    end
    bar.wait = 0
    bar:SetScript("OnUpdate", function(self, elapsed)
        self.wait = self.wait + elapsed
        if self.wait < UPDATE_EVERY then
            return
        end
        self.wait = 0
        for _, button in ipairs(buttons) do
            UpdateButton(button)
        end
    end)
    RefreshAll()
    UpdateShown()
end

function Ascension_Subclass.PressBarButton(index)
    if buttons[index] then
        Press(buttons[index])
    end
end

function Ascension_Subclass.OnOtherSpells(list)
    local fresh = {}
    for id in string.gmatch(list or "", "%d+") do
        local spellId = tonumber(id)
        if not knownSpells[spellId] then
            knownSpells[spellId] = true
            fresh[#fresh + 1] = spellId
        end
    end
    local saved = Saved()
    if not saved.filled and #fresh > 0 then
        local slot = 1
        for _, spellId in ipairs(fresh) do
            while saved[slot] and slot <= BUTTONS do
                slot = slot + 1
            end
            if slot > BUTTONS then
                break
            end
            saved[slot] = spellId
        end
        saved.filled = true
    end
    if bar then
        RefreshAll()
        UpdateShown()
    end
end

local function Forget(saved)
    for index = 1, BUTTONS do
        saved[index] = nil
    end
    saved.filled = nil
    knownSpells = {}
end

local function HoldsKnownSpell(saved)
    for index = 1, BUTTONS do
        if saved[index] and SpellbookIndex(saved[index]) then
            return true
        end
    end
    return false
end

function Ascension_Subclass.OnSubclass(name)
    local saved = Saved()
    local key = name or "-"
    if saved.subclass == key then
        return
    end
    if saved.subclass or not HoldsKnownSpell(saved) then
        Forget(saved)
    end
    saved.subclass = key
    if bar then
        RefreshAll()
        UpdateShown()
    end
end

SLASH_ASCENSIONSUBCLASSBAR1 = "/subclassbar"
SlashCmdList.ASCENSIONSUBCLASSBAR = function(message)
    if message == "reset" then
        Ascension_SubclassDump = Ascension_SubclassDump or {}
        Ascension_SubclassDump.subclassBarPoint = nil
        bar:ClearAllPoints()
        bar:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 170)
    elseif message == "clear" then
        Forget(Saved())
        RefreshAll()
        UpdateShown()
        Ascension_Subclass.Send("SYNC")
    else
        DEFAULT_CHAT_FRAME:AddMessage("Subclass Bar 1: /subclassbar reset (position), /subclassbar clear (refill "
            .. "the buttons with your subclass spells). Shift-drag a button to move the bar. Bind keys under "
            .. "Key Bindings > Subclass Bar 1.")
    end
end

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("UPDATE_BINDINGS")
events:RegisterEvent("SPELLS_CHANGED")
events:SetScript("OnEvent", function(_, event)
    if event == "PLAYER_LOGIN" then
        CreateBar()
    elseif bar then
        RefreshAll()
        UpdateShown()
    end
end)
