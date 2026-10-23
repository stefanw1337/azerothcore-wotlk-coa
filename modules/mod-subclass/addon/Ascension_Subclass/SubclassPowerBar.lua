Ascension_Subclass = Ascension_Subclass or {}

local TOKENS = { [0] = "MANA", [1] = "RAGE", [2] = "FOCUS", [3] = "ENERGY", [6] = "RUNIC_POWER" }
local ORDER = { 0, 1, 2, 3, 6 }
local LEFT, BOTTOM, STEP = 114, 23, 14

local bars = {}
local wanted = {}

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

local function Layout()
    local native = Native()
    if native and AlternatePowerBar_UpdatePowerType then
        AlternatePowerBar_UpdatePowerType(native)
    end
    local slot = native and native:IsShown() and 1 or 0
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

local watcher = CreateFrame("Frame")
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
