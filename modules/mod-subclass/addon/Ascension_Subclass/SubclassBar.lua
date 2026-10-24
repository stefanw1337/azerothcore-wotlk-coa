local BUTTONS, SIZE, GAP = 12, 36, 4
local BINDING = "ASCENSION_SUBCLASS_BUTTON"
local UPDATE_EVERY = 0.1
local OWN_GLOBAL_COOLDOWN = 1.5
local CHARGED_COOLDOWN_ALPHA = 0.3

BINDING_HEADER_ASCENSION_SUBCLASS = "Subclass Bar 1"
for index = 1, BUTTONS do
    _G["BINDING_NAME_" .. BINDING .. index] = "Other head button " .. index
end

local bar
local buttons = {}
local knownSpells = {}
local lent
local subclassName
local timers = { cooldowns = {}, casting = {}, offGlobal = {}, global = { start = 0, duration = 0 } }

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
        button.shownStart, button.shownDuration = nil, nil
    end
    button.hotkey:SetText(ShortKey(GetBindingKey(BINDING .. button:GetID())))
    if Ascension_Subclass.UpdateGlow then
        Ascension_Subclass.UpdateGlow(button)
    end
end

local function RefreshAll()
    for _, button in ipairs(buttons) do
        Refresh(button)
    end
end

local carried

local function Carry(spellId, from)
    carried = { spell = spellId, from = from }
    local _, _, icon = GetSpellInfo(spellId)
    if icon then
        SetCursor(icon)
    end
end

local function DropCarried()
    carried = nil
    ResetCursor()
end

local function Place(button, spellId)
    local saved = Saved()
    local previous = saved[button:GetID()]
    saved[button:GetID()] = spellId
    ClearCursor()
    if previous and previous ~= spellId then
        local index = not InCombatLockdown() and SpellbookIndex(previous)
        if index then
            PickupSpell(index, BOOKTYPE_SPELL)
        elseif carried and carried.from and not saved[carried.from] then
            saved[carried.from] = previous
        end
    end
    DropCarried()
    RefreshAll()
end

local function PlaceCarried(button)
    if not carried then
        return false
    end
    if button and button:GetID() == carried.from then
        Saved()[carried.from] = carried.spell
        DropCarried()
        RefreshAll()
    elseif button then
        Place(button, carried.spell)
    else
        DropCarried()
    end
    return true
end

local function Press(button)
    local spellId = Saved()[button:GetID()]
    if spellId then
        Ascension_Subclass.Send("PRESS " .. spellId)
    end
end

local function OnClick(button, mouse)
    local dropped = CursorSpell()
    if not dropped and PlaceCarried(button) then
        button:SetChecked(false)
        return
    end
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

local function BarsLocked()
    return LOCK_ACTIONBAR == "1" or GetCVar("lockActionBars") == "1"
end

local function OnDragStart(button)
    if BarsLocked() and not IsShiftKeyDown() then
        return
    end
    local spellId = Saved()[button:GetID()]
    if not spellId then
        return
    end
    Saved()[button:GetID()] = nil
    local index = not InCombatLockdown() and SpellbookIndex(spellId)
    if index then
        PickupSpell(index, BOOKTYPE_SPELL)
    else
        Carry(spellId, button:GetID())
    end
    Refresh(button)
end

local function OnReceiveDrag(button)
    local dropped = CursorSpell()
    if dropped then
        Place(button, dropped)
    else
        PlaceCarried(button)
    end
end

local function OurButton(frame)
    for _, button in ipairs(buttons) do
        if button == frame then
            return button
        end
    end
end

local function OnDragStop()
    if carried then
        PlaceCarried(OurButton(GetMouseFocus()))
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
    GameTooltip:AddLine(BarsLocked() and "Shift-drag takes the spell off, alt-drag moves the bar." or
        "Drag takes the spell off, alt-drag moves the bar.", 0.6, 0.6, 0.6)
    GameTooltip:Show()
end

local function LaterOf(start, duration, otherStart, otherDuration)
    if otherDuration > 0 and otherStart + otherDuration > start + duration then
        return otherStart, otherDuration
    end
    return start, duration
end

local function CooldownOf(spellId, name)
    local start, duration = 0, 0
    local clientStart, clientDuration, enabled = GetSpellCooldown(name)
    if clientStart and clientDuration and clientDuration > OWN_GLOBAL_COOLDOWN and enabled == 1 then
        start, duration = clientStart, clientDuration
    end
    local tracked = timers.cooldowns[spellId]
    if tracked then
        start, duration = LaterOf(start, duration, tracked.start, tracked.duration)
    end
    if not timers.offGlobal[spellId] then
        start, duration = LaterOf(start, duration, timers.global.start, timers.global.duration)
    end
    if start + duration <= GetTime() then
        return 0, 0
    end
    return start, duration
end

local function ShowCooldown(button, start, duration, alpha)
    button.cooldown:SetAlpha(alpha)
    if button.shownStart == start and button.shownDuration == duration then
        return
    end
    button.shownStart, button.shownDuration = start, duration
    CooldownFrame_SetTimer(button.cooldown, start, duration, duration > 0 and 1 or 0)
end

local function UpdateButton(button)
    local name = button.spellName
    local spellId = Saved()[button:GetID()]
    if not name or not spellId then
        button.count:SetText("")
        button:SetChecked(false)
        ShowCooldown(button, 0, 0, 1)
        return
    end
    local start, duration = CooldownOf(spellId, name)
    local charges, maxCharges, chargeStart, chargeDuration
    if GetSpellCharges then
        charges, maxCharges, chargeStart, chargeDuration = GetSpellCharges(spellId)
    end
    local charged = maxCharges and maxCharges > 0
    if charged and charges < maxCharges and chargeStart and chargeStart > 0 then
        start, duration = chargeStart, chargeDuration
    end
    ShowCooldown(button, start, duration, charged and charges > 0 and CHARGED_COOLDOWN_ALPHA or 1)
    button.count:SetText(charged and charges or "")
    local castUntil = timers.casting[spellId]
    button:SetChecked(castUntil ~= nil and castUntil > GetTime())
    local usable, noMana = IsUsableSpell(name)
    local form = Ascension_Subclass.OtherForm
    if not usable and not noMana and form and form.usable[spellId] then
        usable = true
    end
    if charged and charges == 0 then
        usable = false
    end
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
    button.count = _G[button:GetName() .. "Count"]
    button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    button:RegisterForDrag("LeftButton")
    button:SetScript("OnClick", OnClick)
    button:SetScript("OnDragStart", function(self)
        if IsAltKeyDown() then
            if not lent then
                bar:StartMoving()
            end
        else
            OnDragStart(self)
        end
    end)
    button:SetScript("OnDragStop", function()
        if carried then
            return OnDragStop()
        end
        if not lent then
            bar:StopMovingOrSizing()
            SavePosition()
        end
    end)
    button:SetScript("OnReceiveDrag", OnReceiveDrag)
    button:SetScript("OnEnter", OnEnter)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    buttons[index] = button
    if Ascension_Subclass.GlowButton then
        Ascension_Subclass.GlowButton(button, function()
            return Saved()[button:GetID()]
        end)
    end
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
    if bar and HasAnything() and not lent then
        bar:Show()
    elseif bar then
        bar:Hide()
    end
end

local function ClearSetPoint(button, ...)
    button:ClearAllPoints()
    button:SetPoint(...)
end

local function Lend(btBar)
    lent = btBar
    if not btBar or not bar then
        return
    end
    btBar.buttons = {}
    for index, button in ipairs(buttons) do
        button:SetParent(btBar)
        button.ClearSetPoint = ClearSetPoint
        btBar.buttons[index] = button
    end
    bar:Hide()
    btBar:UpdateButtonLayout()
end

local function TakeBack()
    lent = nil
    if not bar then
        return
    end
    for index, button in ipairs(buttons) do
        button:SetParent(bar)
        button:ClearAllPoints()
        button:SetPoint("LEFT", bar, "LEFT", (index - 1) * (SIZE + GAP), 0)
    end
    UpdateShown()
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
    bar.ticker = CreateFrame("Frame", nil, UIParent)
    bar.ticker.wait = 0
    bar.ticker:SetScript("OnUpdate", function(self, elapsed)
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
    if lent then
        Lend(lent)
    end
end

function Ascension_Subclass.PressBarButton(index, keystate)
    local button = buttons[index]
    if not button then
        return
    end
    if keystate == "up" then
        button:SetButtonState("NORMAL")
        return
    end
    button:SetButtonState("PUSHED")
    Press(button)
end

function Ascension_Subclass.GetSubclass()
    return subclassName
end

Ascension_Subclass.BarMessages = {
    OTHERFORM = function(rest)
        local spellId, list = string.match(rest, "^(%d+)%s?(.*)$")
        spellId = tonumber(spellId)
        if not spellId or spellId == 0 then
            Ascension_Subclass.OtherForm = nil
            return
        end
        local form = { spell = spellId, name = GetSpellInfo(spellId), usable = {} }
        for id in string.gmatch(list or "", "(%d+)") do
            form.usable[tonumber(id)] = true
        end
        Ascension_Subclass.OtherForm = form
    end,
    OTHERSTART = function(rest)
        local spellId, global, cast = string.match(rest, "^(%d+) (%d+) (%d+)$")
        if not spellId then
            return
        end
        timers.global = { start = GetTime(), duration = tonumber(global) / 1000 }
        if tonumber(cast) > 0 then
            timers.casting[tonumber(spellId)] = GetTime() + tonumber(cast) / 1000
        end
    end,
    OTHERCAST = function(rest)
        local spellId = tonumber(string.match(rest, "^(%d+)"))
        if spellId then
            timers.casting[spellId] = nil
        end
    end,
    OTHERCD = function(rest)
        local spellId, remaining = string.match(rest, "^(%d+) (%d+)$")
        if spellId then
            timers.cooldowns[tonumber(spellId)] = { start = GetTime(), duration = tonumber(remaining) / 1000 }
        end
    end,
    OFFGCD = function(rest)
        for id in string.gmatch(rest, "%d+") do
            timers.offGlobal[tonumber(id)] = true
        end
    end,
}

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

local function HoldsOnlyKnownSpells(saved)
    for index = 1, BUTTONS do
        if saved[index] and not SpellbookIndex(saved[index]) then
            return false
        end
    end
    return true
end

function Ascension_Subclass.OnSubclass(name)
    timers.offGlobal = {}
    if Ascension_Subclass.GlowSubclass then
        Ascension_Subclass.GlowSubclass(name)
    end
    if subclassName ~= name then
        subclassName = name
        if WeakAuras and WeakAuras.ScanEvents then
            WeakAuras.ScanEvents("ASCENSION_SUBCLASS_CHANGED", name)
        end
    end
    local saved = Saved()
    local key = name or "-"
    if saved.subclass == key then
        return
    end
    if saved.subclass or not HoldsOnlyKnownSpells(saved) then
        Forget(saved)
    end
    saved.subclass = key
    if bar then
        RefreshAll()
        UpdateShown()
    end
end

local castBarUnlocked = false

SLASH_ASCENSIONSUBCLASSBAR1 = "/subclassbar"
SlashCmdList.ASCENSIONSUBCLASSBAR = function(message)
    if message == "reset" then
        Ascension_SubclassDump = Ascension_SubclassDump or {}
        Ascension_SubclassDump.subclassBarPoint = nil
        bar:ClearAllPoints()
        bar:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 170)
        for _, reset in ipairs({ Ascension_Subclass.ResetStanceBar, Ascension_Subclass.ResetOrbs,
            Ascension_Subclass.ResetCastBar }) do
            reset()
        end
    elseif message == "stances" then
        Ascension_Subclass.DescribeStances()
    elseif message == "castbar" then
        castBarUnlocked = not castBarUnlocked
        Ascension_Subclass.UnlockCastBar(castBarUnlocked)
    elseif message == "clear" then
        Forget(Saved())
        RefreshAll()
        UpdateShown()
        Ascension_Subclass.Send("SYNC")
    else
        DEFAULT_CHAT_FRAME:AddMessage("Subclass Bar 1: /subclassbar reset (position), /subclassbar clear (refill "
            .. "the buttons with your subclass spells), /subclassbar castbar (show the subclass cast bar to Alt-drag "
            .. "it, again to lock it). Alt-drag a button to move the bar. Bind keys under Key Bindings > Subclass "
            .. "Bar 1.")
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

if Bartender4 and Bartender4.ButtonBar and Bartender4.NewModule then
    local module = Bartender4:NewModule("SubclassBar")
    local defaults = { profile = Bartender4:Merge({ enabled = true, visibility = { possess = false } },
        Bartender4.ButtonBar.defaults) }
    function module:OnInitialize()
        self.db = Bartender4.db:RegisterNamespace("SubclassBar", defaults)
        self:SetEnabledState(self.db.profile.enabled)
    end
    function module:SetupOptions()
        if not self.options then
            self.optionobject = Bartender4.ButtonBar.prototype:GetOptionObject()
            self.optionobject.table.general.args.rows.max = BUTTONS
            local enabled = {
                type = "toggle",
                order = 1,
                name = "Enabled",
                desc = "Show Subclass Bar 1, the spells your other head or twin casts",
                get = function() return self.db.profile.enabled end,
                set = "ToggleModule",
                handler = self,
            }
            self.optionobject:AddElement("general", "enabled", enabled)
            self.disabledoptions = { general = { type = "group", name = "General Settings", cmdInline = true,
                order = 1, args = { enabled = enabled } } }
            self.options = { order = 30, type = "group", name = "Subclass Bar 1",
                desc = "Configure Subclass Bar 1", childGroups = "tab" }
            Bartender4:RegisterBarOptions("SubclassBar", self.options)
        end
        self.options.args = self:IsEnabled() and self.optionobject.table or self.disabledoptions
    end
    function module:OnEnable()
        if not self.bar then
            self.bar = Bartender4.ButtonBar:Create("SubclassBar", self.db.profile, "Subclass Bar 1")
            self.bar.button_width, self.bar.button_height = SIZE, SIZE
        end
        self.bar:Enable()
        if self.ToggleOptions then
            self:ToggleOptions()
        end
        self:ApplyConfig()
    end
    function module:ApplyConfig()
        self.bar:ApplyConfig(self.db.profile)
        if not self.db.profile.position.x then
            self.bar:ClearSetPoint("CENTER", 0, -200)
            self.bar:SavePosition()
        end
        Lend(self.bar)
    end
    function module:OnDisable()
        if self.bar then
            self.bar:Disable()
        end
        if self.ToggleOptions then
            self:ToggleOptions()
        end
        TakeBack()
    end
end
