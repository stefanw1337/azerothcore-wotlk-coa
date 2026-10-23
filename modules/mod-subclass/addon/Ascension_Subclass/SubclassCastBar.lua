Ascension_Subclass = Ascension_Subclass or {}

local WIDTH, HEIGHT = 195, 13
local SPELL_CAST_OK = 255
local HOLD, FADE = 0.4, 0.5
local CASTING_COLOR = { 0.62, 0.36, 1.0 }
local FINISHED_COLOR = { 0.0, 1.0, 0.0 }
local FAILED_COLOR = { 1.0, 0.0, 0.0 }

local bar = CreateFrame("StatusBar", "Ascension_SubclassCastBar", UIParent)
bar:SetWidth(WIDTH)
bar:SetHeight(HEIGHT)
bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
bar:SetFrameStrata("HIGH")
bar:SetMovable(true)
bar:EnableMouse(false)
bar:Hide()

local background = bar:CreateTexture(nil, "BACKGROUND")
background:SetAllPoints(bar)
background:SetTexture(0, 0, 0, 0.5)

local border = bar:CreateTexture(nil, "ARTWORK")
border:SetTexture("Interface\\CastingBar\\UI-CastingBar-Border")
border:SetWidth(256)
border:SetHeight(64)
border:SetPoint("TOP", bar, "TOP", 0, 28)

local spark = bar:CreateTexture(nil, "OVERLAY")
spark:SetTexture("Interface\\CastingBar\\UI-CastingBar-Spark")
spark:SetBlendMode("ADD")
spark:SetWidth(32)
spark:SetHeight(32)

local flash = bar:CreateTexture(nil, "OVERLAY")
flash:SetTexture("Interface\\CastingBar\\UI-CastingBar-Flash")
flash:SetBlendMode("ADD")
flash:SetWidth(256)
flash:SetHeight(64)
flash:SetPoint("TOP", bar, "TOP", 0, 28)
flash:Hide()

local icon = bar:CreateTexture(nil, "ARTWORK")
icon:SetWidth(HEIGHT + 6)
icon:SetHeight(HEIGHT + 6)
icon:SetPoint("RIGHT", bar, "LEFT", -6, 0)

local text = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
text:SetPoint("CENTER", bar, "CENTER", 0, 1)

local state = { spellId = nil, start = 0, duration = 0, casting = false, holdUntil = 0, fadeFrom = 0 }

local function Saved()
    Ascension_SubclassDump = Ascension_SubclassDump or {}
    return Ascension_SubclassDump
end

local function Place()
    local point = Saved().castBarPoint
    bar:ClearAllPoints()
    if point then
        bar:SetPoint(point[1], UIParent, point[2], point[3], point[4])
    elseif CastingBarFrame then
        bar:SetPoint("BOTTOM", CastingBarFrame, "TOP", 0, 22)
    else
        bar:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 200)
    end
end

local function Paint(color)
    bar:SetStatusBarColor(color[1], color[2], color[3])
end

local function Start(spellId, milliseconds)
    local name, _, texture = GetSpellInfo(spellId)
    state.spellId, state.start, state.duration = spellId, GetTime(), milliseconds / 1000
    state.casting, state.holdUntil, state.fadeFrom = true, 0, 0
    bar:SetMinMaxValues(0, state.duration)
    bar:SetValue(0)
    Paint(CASTING_COLOR)
    text:SetText(name or "")
    icon:SetTexture(texture)
    spark:Show()
    flash:Hide()
    bar:SetAlpha(1)
    bar:Show()
end

local function End(label, color)
    state.casting = false
    state.holdUntil = GetTime() + HOLD
    spark:Hide()
    Paint(color)
    if label then
        text:SetText(label)
    else
        bar:SetValue(state.duration)
        flash:SetAlpha(1)
        flash:Show()
    end
end

bar:SetScript("OnUpdate", function(self)
    local now = GetTime()
    if state.casting then
        local elapsed = math.min(now - state.start, state.duration)
        self:SetValue(elapsed)
        local share = state.duration > 0 and elapsed / state.duration or 1
        spark:SetPoint("CENTER", self, "LEFT", share * WIDTH, 2)
        return
    end
    if now < state.holdUntil then
        return
    end
    if state.fadeFrom == 0 then
        state.fadeFrom = now
    end
    local alpha = 1 - (now - state.fadeFrom) / FADE
    if alpha <= 0 then
        self:Hide()
        return
    end
    self:SetAlpha(alpha)
    flash:SetAlpha(alpha)
end)

bar:SetScript("OnMouseDown", function(self)
    if IsAltKeyDown() then
        self:StartMoving()
    end
end)

bar:SetScript("OnMouseUp", function(self)
    self:StopMovingOrSizing()
    local point, _, relative, x, y = self:GetPoint()
    Saved().castBarPoint = { point, relative, x, y }
end)

local messages = Ascension_Subclass.BarMessages or {}
Ascension_Subclass.BarMessages = messages

messages.OTHERCASTING = function(rest)
    local spellId, milliseconds = string.match(rest, "^(%d+) (%d+)$")
    if spellId then
        Start(tonumber(spellId), tonumber(milliseconds))
    end
end

messages.OTHERSTOP = function(rest)
    if state.casting and tonumber(string.match(rest, "^(%d+)")) == state.spellId then
        End(INTERRUPTED or "Interrupted", FAILED_COLOR)
    end
end

local castMessage = messages.OTHERCAST
messages.OTHERCAST = function(rest)
    if castMessage then
        castMessage(rest)
    end
    local spellId, result = string.match(rest, "^(%d+) (%d+)")
    if not state.casting or tonumber(spellId) ~= state.spellId then
        return
    end
    if tonumber(result) == SPELL_CAST_OK then
        End(nil, FINISHED_COLOR)
    else
        End(FAILED or "Failed", FAILED_COLOR)
    end
end

function Ascension_Subclass.UnlockCastBar(unlocked)
    bar:EnableMouse(unlocked)
    if unlocked then
        Start(0, 10000)
        text:SetText("Subclass cast bar: Alt-drag to move")
        icon:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
    else
        End(nil, FINISHED_COLOR)
    end
end

function Ascension_Subclass.ResetCastBar()
    Saved().castBarPoint = nil
    Place()
end

bar:RegisterEvent("PLAYER_ENTERING_WORLD")
bar:SetScript("OnEvent", Place)
