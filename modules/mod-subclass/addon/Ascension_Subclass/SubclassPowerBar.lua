Ascension_Subclass = Ascension_Subclass or {}

local TOKENS = { [0] = "MANA", [1] = "RAGE", [2] = "FOCUS", [3] = "ENERGY", [6] = "RUNIC_POWER" }
local ORDER = { 0, 1, 2, 3, 6 }
local LEFT, BOTTOM, STEP = 114, 23, 14

local bars = {}
local wanted = {}
local split
local brotherBar

local function Native()
    local native = PlayerFrameAlternateManaBar
    return native and native.powerIndex ~= nil and native or nil
end

local function Covers(power)
    local native = Native()
    return native ~= nil and native.powerIndex == power
end

local function Lent(power)
    return wanted[power] and UnitPowerType("player") ~= power and UnitPowerMax("player", power) > 0
end

local function Retarget(bar, power)
    local token = TOKENS[power]
    if bar.UnitEvent then
        bar:UnregisterEvent(bar.UnitEvent)
    end
    if bar.UnitMaxEvent then
        bar:UnregisterEvent(bar.UnitMaxEvent)
    end
    bar.powerName, bar.powerIndex = token, power
    bar.UnitEvent, bar.UnitMaxEvent = "UNIT_" .. token, "UNIT_MAX" .. token
    bar:RegisterEvent(bar.UnitEvent)
    bar:RegisterEvent(bar.UnitMaxEvent)
    local color = PowerBarColor and PowerBarColor[token]
    if color then
        bar:SetStatusBarColor(color.r, color.g, color.b)
    end
end

local function BarFor(power)
    if bars[power] then
        return bars[power]
    end
    if not (TOKENS[power] and PlayerFrame and AlternatePowerBar_OnLoad and AlternatePowerBar_UpdateValue) then
        return nil
    end
    local ok, bar = pcall(CreateFrame, "StatusBar", "Ascension_SubclassPowerBar" .. power, PlayerFrame,
        "AlternatePowerBarTemplate")
    if not ok or not bar then
        return nil
    end
    Retarget(bar, power)
    bar:HookScript("OnEvent", function(self)
        if not Lent(self.powerIndex) then
            self:Hide()
        end
    end)
    bar:Hide()
    bars[power] = bar
    return bar
end

local function BrotherBar()
    if brotherBar or not (PlayerFrame and AlternatePowerBar_OnLoad) then
        return brotherBar
    end
    local ok, bar = pcall(CreateFrame, "StatusBar", "Ascension_SubclassBrotherPowerBar", PlayerFrame,
        "AlternatePowerBarTemplate")
    if not ok or not bar then
        return nil
    end
    bar:UnregisterAllEvents()
    bar:RegisterEvent("CVAR_UPDATE")
    bar:EnableMouse(true)
    bar:SetScript("OnUpdate", nil)
    bar:SetScript("OnEvent", TextStatusBar_OnEvent)
    bar:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(string.format("Your brother's %s", string.lower(_G[self.powerName] or self.powerName)))
        GameTooltip:AddLine("One pool for both of you, shown in two halves.", 0.8, 0.8, 0.8, true)
        GameTooltip:Show()
    end)
    bar:SetScript("OnLeave", function() GameTooltip:Hide() end)
    bar:Hide()
    brotherBar = bar
    return bar
end

local function ShowOwnShare()
    local bar = PlayerFrameManaBar
    if not split or not bar or bar.powerType ~= split.power then
        return
    end
    local mine = math.max(0, math.min(split.half, UnitPower("player", split.power) - split.share))
    bar:SetMinMaxValues(0, math.max(split.half, 1))
    bar:SetValue(mine)
    if TextStatusBar_UpdateTextString then
        TextStatusBar_UpdateTextString(bar)
    end
end

local function Layout()
    local native = Native()
    if native and AlternatePowerBar_UpdatePowerType then
        AlternatePowerBar_UpdatePowerType(native)
    end
    local slot = native and native:IsShown() and 1 or 0
    local brother = split and BrotherBar()
    if brother then
        local token = TOKENS[split.power]
        brother.powerName, brother.powerIndex = token, split.power
        local color = PowerBarColor and PowerBarColor[token]
        if color then
            brother:SetStatusBarColor(color.r, color.g, color.b)
        end
        brother:SetMinMaxValues(0, math.max(split.half, 1))
        brother:SetValue(split.share)
        brother:ClearAllPoints()
        brother:SetPoint("BOTTOMLEFT", PlayerFrame, "BOTTOMLEFT", LEFT, BOTTOM - slot * STEP)
        brother:Show()
        slot = slot + 1
    elseif brotherBar then
        brotherBar:Hide()
    end
    ShowOwnShare()
    for _, power in ipairs(ORDER) do
        local bar = bars[power]
        if bar and Lent(power) then
            AlternatePowerBar_UpdateMaxValues(bar)
            AlternatePowerBar_UpdateValue(bar)
            bar:ClearAllPoints()
            bar:SetPoint("BOTTOMLEFT", PlayerFrame, "BOTTOMLEFT", LEFT, BOTTOM - slot * STEP)
            bar.pauseUpdates = false
            bar:Show()
            slot = slot + 1
        elseif bar then
            bar:Hide()
        end
    end
end

function Ascension_Subclass.ShowsPowerBar(power)
    return Covers(power) or BarFor(power) ~= nil
end

function Ascension_Subclass.UpdatePowerBars(powers)
    wanted = {}
    for _, power in ipairs(powers or {}) do
        wanted[power] = true
        if not Covers(power) then
            BarFor(power)
        end
    end
    Layout()
end

function Ascension_Subclass.DescribePowers()
    local say = function(text) DEFAULT_CHAT_FRAME:AddMessage("|cffffd100Subclass power:|r " .. text) end
    local native = Native()
    say(string.format("main power %s; PlayerFrame %s; CoA's second bar %s", tostring(UnitPowerType("player")),
        PlayerFrame and (PlayerFrame:IsVisible() and "visible" or "hidden") or "missing",
        native and string.format("power %s, %s", tostring(native.powerIndex), native:IsShown() and "shown" or
            "hidden") or "none"))
    for _, power in ipairs(ORDER) do
        local bar = bars[power]
        say(string.format("%s: lent %s, %d/%d, bar %s", TOKENS[power], wanted[power] and "yes" or "no",
            UnitPower("player", power) or 0, UnitPowerMax("player", power) or 0,
            bar and (bar:IsVisible() and "visible" or (bar:IsShown() and "shown, parent hidden" or "hidden")) or
            (Covers(power) and "CoA's own" or "none")))
    end
end

local messages = Ascension_Subclass.BarMessages or {}
Ascension_Subclass.BarMessages = messages

messages.OTHERPOWER = function(rest)
    local power, share, half = string.match(rest, "^(%d+) (%d+) (%d+)$")
    local ended = split and not power
    split = power and TOKENS[tonumber(power)] and
        { power = tonumber(power), share = tonumber(share), half = tonumber(half) } or nil
    if ended and PlayerFrameManaBar and UnitFrameManaBar_Update then
        UnitFrameManaBar_Update(PlayerFrameManaBar, "player")
    end
    Layout()
end

if UnitFrameManaBar_Update then
    hooksecurefunc("UnitFrameManaBar_Update", function(bar)
        if bar == PlayerFrameManaBar then
            ShowOwnShare()
        end
    end)
end

local watcher = CreateFrame("Frame")
watcher:SetScript("OnUpdate", function()
    if split then
        ShowOwnShare()
    end
end)
watcher:RegisterEvent("PLAYER_ENTERING_WORLD")
watcher:RegisterEvent("UNIT_DISPLAYPOWER")
for _, power in ipairs(ORDER) do
    watcher:RegisterEvent("UNIT_MAX" .. TOKENS[power])
end
watcher:SetScript("OnEvent", function(_, event, unit)
    if event == "PLAYER_ENTERING_WORLD" or unit == "player" then
        Layout()
    end
end)
