-- Loot Goblins: the Rift-style countdown for each goblin you fight, and the Need / Greed / Pass window for goblin
-- loot in groups. The server (mod-loot-goblins) talks to us with addon whispers under the prefix "LGOB".

local PREFIX = "LGOB"
local addon = CreateFrame("Frame", "LootGoblinsFrame", UIParent)
local bars, freeBars = {}, {}
local rolls = {}

local function split(text)
    local out = {}
    for part in string.gmatch(text .. "\t", "([^\t]*)\t") do out[#out + 1] = part end
    return out
end

local function send(text)
    SendAddonMessage(PREFIX, text, "WHISPER", UnitName("player"))
end

local function clock(ms)
    local s = math.max(0, math.floor(ms / 1000 + 0.5))
    return string.format("%d:%02d", math.floor(s / 60), s % 60)
end

---------------------------------------------------------------------------------------------------------
-- The timer bars
---------------------------------------------------------------------------------------------------------
local BAR_W, BAR_H = 380, 22

local function layout()
    local i = 0
    for _, bar in pairs(bars) do
        bar:ClearAllPoints()
        bar:SetPoint("TOP", UIParent, "TOP", 0, -110 - i * (BAR_H + 30))
        i = i + 1
    end
end

local function popup(bar, text, r, g, b)
    local fs = bar.pop
    fs:SetText(text)
    fs:SetTextColor(r, g, b)
    fs.t = 1.2
    fs:Show()
end

local onEnd   -- defined below; the bars' OnUpdate needs it

local function newBar()
    local bar = table.remove(freeBars)
    if bar then return bar end
    bar = CreateFrame("Frame", nil, UIParent)
    bar:SetWidth(BAR_W + 8) bar:SetHeight(BAR_H + 8)
    bar:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
    bar:SetBackdropColor(0, 0, 0, 0.75)
    bar:SetBackdropBorderColor(1, 0.82, 0.2, 1)

    bar.potential = bar:CreateTexture(nil, "BORDER")               -- time it can still gain (up to the 5-minute limit)
    bar.potential:SetTexture("Interface\\TargetingFrame\\UI-StatusBar")
    bar.potential:SetVertexColor(1, 0.85, 0.3, 0.18)
    bar.potential:SetPoint("LEFT", bar, "LEFT", 4, 0)
    bar.potential:SetHeight(BAR_H)

    bar.status = CreateFrame("StatusBar", nil, bar)
    bar.status:SetPoint("TOPLEFT", 4, -4)
    bar.status:SetPoint("BOTTOMRIGHT", -4, 4)
    bar.status:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    bar.status:SetMinMaxValues(0, 1)

    bar.spark = bar.status:CreateTexture(nil, "OVERLAY")
    bar.spark:SetTexture("Interface\\CastingBar\\UI-CastingBar-Spark")
    bar.spark:SetBlendMode("ADD")
    bar.spark:SetWidth(20) bar.spark:SetHeight(BAR_H * 2)

    bar.icon = bar:CreateTexture(nil, "ARTWORK")
    bar.icon:SetTexture("Interface\\Icons\\INV_Misc_Bag_10")
    bar.icon:SetWidth(BAR_H + 14) bar.icon:SetHeight(BAR_H + 14)
    bar.icon:SetPoint("RIGHT", bar, "LEFT", -2, 0)

    bar.title = bar.status:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    bar.title:SetPoint("CENTER", 0, 0)
    bar.time = bar.status:CreateFontString(nil, "OVERLAY", "GameFontHighlightLarge")
    bar.time:SetPoint("RIGHT", -8, 0)
    bar.health = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    bar.health:SetPoint("TOPRIGHT", bar, "BOTTOMRIGHT", -4, 0)
    bar.state = bar:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    bar.state:SetPoint("TOP", bar, "BOTTOM", 0, -2)
    bar.pop = bar:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    bar.pop:SetPoint("BOTTOM", bar, "TOP", 0, 2)
    bar.pop:Hide()

    bar:SetScript("OnUpdate", function(self, elapsed)
        if self.ending then
            self.ending = self.ending - elapsed
            if self.ending <= 0 then
                bars[self.id] = nil
                self:Hide()
                table.insert(freeBars, self)
                layout()
            end
            return
        end
        self.left = math.max(0, self.left - elapsed * 1000)
        self.hard = math.max(0, self.hard - elapsed * 1000)
        self.silence = self.silence + elapsed
        if self.silence > 6 and not self.test then self.ending = 0.01 return end
        if self.test and self.left <= 0 then onEnd(self.id, "escaped") return end
        local frac = self.max > 0 and self.left / self.max or 0
        self.status:SetValue(frac)
        self.spark:SetPoint("CENTER", self.status, "LEFT", frac * BAR_W, 0)
        self.potential:SetWidth(math.max(1, (self.max > 0 and math.min(self.hard, self.max) / self.max or 0) * BAR_W))
        self.time:SetText("")
        if self.bossTitle then
            self.title:SetText(clock(self.left) .. " - |cffff8000" .. self.bossTitle .. "|r")
        else
            self.title:SetText(clock(self.left) .. " - Loot Goblin |cffffffff<" .. (self.subname or "?") .. ">|r")
        end
        local r, g, b = 1, 0.75, 0.1
        if self.left < 60000 then r, g, b = 1, 0.45, 0.05 end
        if self.left < 15000 or (self.phase ~= "run" and self.phase ~= "boss" and self.phase ~= "hard") then
            local pulse = 0.6 + 0.4 * math.abs(math.sin(GetTime() * 5))
            r, g, b = pulse, 0.1, 0.05
        end
        self.status:SetStatusBarColor(r, g, b)
        if self.phase == "portal" then self.state:SetText("|cffff4040Opening a portal - hit it!|r")
        elseif self.phase == "escape" then self.state:SetText("|cffff2020Escaping!|r")
        elseif self.phase == "hard" then self.state:SetText("|cffffd000Kill her within " .. clock(self.hard) .. " for HARD MODE!|r")
        else self.state:SetText("") end
        local pop = self.pop
        if pop:IsShown() then
            pop.t = pop.t - elapsed
            pop:SetAlpha(math.min(1, pop.t))
            if pop.t <= 0 then pop:Hide() end
        end
    end)
    return bar
end

local function onTime(id, subname, left, max, hard, phase, health, title)
    local bar = bars[id]
    if not bar then
        bar = newBar()
        bar.id = id
        bars[id] = bar
        layout()
    end
    bar.ending = nil
    bar.test = nil
    bar.silence = 0
    bar.left, bar.max, bar.hard = tonumber(left) or 0, tonumber(max) or 300000, tonumber(hard) or 0
    bar.phase = phase or "run"
    bar.subname = subname
    bar.bossTitle = title ~= "" and title or nil
    bar.health:SetText(health and (health .. "% health") or "")
    bar:Show()
end

onEnd = function(id, reason)
    local bar = bars[id]
    if not bar then return end
    if reason == "killed" then
        bar.state:SetText("|cff40ff40Slain! Grab your loot.|r")
        bar.status:SetStatusBarColor(0.2, 1, 0.2)
    elseif reason == "escaped" then
        bar.state:SetText("|cffff4040It escaped with its loot...|r")
    else
        bar.state:SetText("")
    end
    bar.ending = 3
end

---------------------------------------------------------------------------------------------------------
-- The roll window
---------------------------------------------------------------------------------------------------------
local ROW_H = 46
local rollFrame = CreateFrame("Frame", "LootGoblinsRollFrame", UIParent)
rollFrame:SetWidth(330) rollFrame:SetHeight(40)
rollFrame:SetPoint("CENTER", UIParent, "CENTER", 0, 150)
rollFrame:SetMovable(true)
rollFrame:EnableMouse(true)
rollFrame:RegisterForDrag("LeftButton")
rollFrame:SetScript("OnDragStart", rollFrame.StartMoving)
rollFrame:SetScript("OnDragStop", rollFrame.StopMovingOrSizing)
rollFrame:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background", edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 24, insets = { left = 6, right = 6, top = 6, bottom = 6 } })
rollFrame.title = rollFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
rollFrame.title:SetPoint("TOP", 0, -10)
rollFrame.title:SetText("Loot Goblin - roll for the loot")
rollFrame.hint = rollFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
rollFrame.hint:SetPoint("BOTTOM", 0, 8)
rollFrame.hint:SetText("Roll = 1-100 + your share of the fight / 10")
rollFrame:Hide()

local function relayout()
    local i = 0
    for _, row in ipairs(rolls) do
        row:ClearAllPoints()
        row:SetPoint("TOPLEFT", rollFrame, "TOPLEFT", 12, -28 - i * ROW_H)
        i = i + 1
    end
    rollFrame:SetHeight(48 + i * ROW_H)
    if i == 0 then rollFrame:Hide() else rollFrame:Show() end
end

local function removeRow(row)
    for i, r in ipairs(rolls) do if r == row then table.remove(rolls, i) break end end
    row:Hide()
    relayout()
end

local function makeButton(row, texture, tip, choice, x)
    local b = CreateFrame("Button", nil, row)
    b:SetWidth(28) b:SetHeight(28)
    b:SetPoint("RIGHT", row, "RIGHT", x, 0)
    b:SetNormalTexture("Interface\\Buttons\\" .. texture .. "-Up")
    -- the same highlights WoW's own roll window uses (they're drawn additively, so no box around the icon)
    b:SetHighlightTexture(texture == "UI-GroupLoot-Pass" and "Interface\\Buttons\\UI-Panel-MinimizeButton-Highlight"
        or ("Interface\\Buttons\\" .. texture .. "-Highlight"), "ADD")
    b:SetPushedTexture("Interface\\Buttons\\" .. texture .. "-Down")
    b:SetDisabledTexture("Interface\\Buttons\\" .. texture .. "-Up")
    b:GetDisabledTexture():SetDesaturated(true)
    b:GetDisabledTexture():SetAlpha(0.35)
    b:SetScript("OnEnter", function(self) GameTooltip:SetOwner(self, "ANCHOR_RIGHT") GameTooltip:SetText(tip) GameTooltip:Show() end)
    b:SetScript("OnLeave", GameTooltip_Hide)
    b:SetScript("OnClick", function()
        send(string.format("CHOICE\t%s\t%s\t%s", row.rollId, row.index, choice))
        row.need:Disable() row.greed:Disable() row.pass:Disable()
        b:GetDisabledTexture():SetDesaturated(false)   -- the one you picked stays lit
        b:GetDisabledTexture():SetAlpha(1)
        GameTooltip:Hide()
        row.status:SetText(choice == "pass" and "|cff888888Passed|r" or ("|cffffff00" .. (choice == "need" and "Need" or "Greed") .. "|r - waiting..."))
    end)
    return b
end

local function addRoll(rollId, index, itemId, count, timeout)
    local row = CreateFrame("Frame", nil, rollFrame)
    row:SetWidth(306) row:SetHeight(ROW_H - 4)
    row.rollId, row.index, row.itemId = rollId, index, tonumber(itemId)
    row.icon = CreateFrame("Button", nil, row)
    row.icon:SetWidth(36) row.icon:SetHeight(36)
    row.icon:SetPoint("LEFT", 0, 0)
    row.icon.tex = row.icon:CreateTexture(nil, "ARTWORK")
    row.icon.tex:SetAllPoints()
    row.icon:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetHyperlink("item:" .. row.itemId)
        GameTooltip:Show()
    end)
    row.icon:SetScript("OnLeave", GameTooltip_Hide)
    row.icon:SetScript("OnClick", function() if IsModifiedClick() then HandleModifiedItemClick("item:" .. row.itemId) end end)
    row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    row.name:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 6, -2)
    row.name:SetWidth(170)
    row.name:SetJustifyH("LEFT")
    row.status = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.status:SetPoint("BOTTOMLEFT", row.icon, "BOTTOMRIGHT", 6, 2)
    row.count = tonumber(count) or 1
    row.ends = GetTime() + (tonumber(timeout) or 40)
    row.need = makeButton(row, "UI-GroupLoot-Dice", "Need", "need", -66)
    row.greed = makeButton(row, "UI-GroupLoot-Coin", "Greed", "greed", -34)
    row.pass = makeButton(row, "UI-GroupLoot-Pass", "Pass", "pass", -2)
    row:SetScript("OnUpdate", function(self)
        local name, link, quality, _, _, _, _, _, _, texture = GetItemInfo(self.itemId)
        if name then
            local r, g, b = GetItemQualityColor(quality)
            self.name:SetText(name .. (self.count > 1 and (" x" .. self.count) or ""))
            self.name:SetTextColor(r, g, b)
            self.icon.tex:SetTexture(texture)
        else
            self.name:SetText("item " .. self.itemId)
            self.icon.tex:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
        end
        local left = self.ends - GetTime()
        if self.done then
            if left < -4 then removeRow(self) end
        else
            if self.need:IsEnabled() == 1 then
                self.status:SetText(string.format("%d s", math.max(0, math.ceil(left))))
            end
            -- time's up and no result came (the server decides; a test roll never gets one): close it
            if left < -8 then removeRow(self) end
        end
    end)
    -- ask the server about the item so the name and icon show
    GameTooltip:SetOwner(UIParent, "ANCHOR_NONE")
    GameTooltip:SetHyperlink("item:" .. row.itemId)
    GameTooltip:Hide()
    table.insert(rolls, row)
    relayout()
end

local function onResult(rollId, index, winner, itemId, total, upgraded)
    for _, row in ipairs(rolls) do
        if row.rollId == rollId and row.index == index then
            row.itemId = tonumber(itemId) or row.itemId
            row.need:Disable() row.greed:Disable() row.pass:Disable()
            row.status:SetText(string.format("|cff40ff40%s|r wins (%s)%s", winner, total, upgraded == "1" and " |cffffff00upgraded!|r" or ""))
            row.done = true
            row.ends = GetTime()
        end
    end
end


---------------------------------------------------------------------------------------------------------
-- Map pins: every goblin on your map (world, continent and zone views). LootGoblins_Maps.lua is written
-- by the installer from your own client's WorldMapArea table: [map area id] = { map, left, right, top, bottom }.
---------------------------------------------------------------------------------------------------------
local spots = {}
local pins = {}
local MAPS = LootGoblinsMaps or {}
local MAP_NAMES = LootGoblinsMapNames or {}

-- the world-map area being shown: by its file name (GetMapInfo), which is the same in every client
local function shownArea()
    local file = GetMapInfo()
    local id = file and MAP_NAMES[string.lower(file)]
    if id and MAPS[id] then return MAPS[id] end
    return MAPS[GetCurrentMapAreaID()]
end

local function pinFor(id)
    local pin = pins[id]
    if pin then return pin end
    pin = CreateFrame("Button", nil, WorldMapButton)
    pin:SetWidth(22) pin:SetHeight(22)
    pin:SetFrameLevel(WorldMapButton:GetFrameLevel() + 5)
    pin.icon = pin:CreateTexture(nil, "OVERLAY")
    pin.icon:SetAllPoints()
    pin.icon:SetTexture("Interface\\Icons\\INV_Misc_Bag_10")
    pin.glow = pin:CreateTexture(nil, "BACKGROUND")
    pin.glow:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
    pin.glow:SetBlendMode("ADD")
    pin.glow:SetVertexColor(1, 0.8, 0.1)
    pin.glow:SetPoint("CENTER") pin.glow:SetWidth(44) pin.glow:SetHeight(44)
    pin:SetScript("OnEnter", function(self)
        local s = spots[self.id]
        if not s then return end
        WorldMapTooltip:SetOwner(self, "ANCHOR_RIGHT")
        local left = math.max(0, s.ends - GetTime() * 1000)
        if s.state == "boss" then
            WorldMapTooltip:AddLine("|cffff8000" .. s.sub .. "|r - World Boss")
            WorldMapTooltip:AddLine(clock(left) .. " left - everyone, to arms!", 1, 0.4, 0.2)
            WorldMapTooltip:Show()
            return
        end
        WorldMapTooltip:AddLine("Loot Goblin |cffffffff<" .. s.sub .. ">|r")
        if s.state == "run" then
            WorldMapTooltip:AddLine("Running! " .. clock(left) .. " left - come help", 1, 0.4, 0.2)
        else
            WorldMapTooltip:AddLine("Not hit yet - wanders off in " .. clock(left), 1, 0.82, 0)
        end
        WorldMapTooltip:Show()
    end)
    pin:SetScript("OnLeave", function() WorldMapTooltip:Hide() end)
    pin:SetScript("OnUpdate", function(self)
        local s = spots[self.id]
        if s and s.state ~= "idle" then self.glow:SetAlpha(0.4 + 0.6 * math.abs(math.sin(GetTime() * 4))) else self.glow:SetAlpha(0.5) end
    end)
    pins[id] = pin
    return pin
end

local function updatePins()
    for id, pin in pairs(pins) do pin:Hide() end
    if not WorldMapFrame:IsShown() then return end
    if GetCurrentMapDungeonLevel and GetCurrentMapDungeonLevel() > 0 then return end
    local area = shownArea()
    if not area then return end
    local w, h = WorldMapButton:GetWidth(), WorldMapButton:GetHeight()
    local now = GetTime() * 1000
    for id, s in pairs(spots) do
        if now > s.ends + 15000 or now > s.seen + 20000 then
            spots[id] = nil
        elseif area[1] == s.map then
            -- WoW world coordinates: x points north, y points west; the map runs left->right, top->bottom
            local u = (area[2] - s.y) / (area[2] - area[3])
            local v = (area[4] - s.x) / (area[4] - area[5])
            if u >= 0 and u <= 1 and v >= 0 and v <= 1 then
                local pin = pinFor(id)
                pin.id = id
                if s.state == "boss" then
                    pin.icon:SetTexture("Interface\\TargetingFrame\\UI-TargetingFrame-Skull")
                    pin:SetWidth(28) pin:SetHeight(28)
                else
                    pin.icon:SetTexture("Interface\\Icons\\INV_Misc_Bag_10")
                    pin:SetWidth(22) pin:SetHeight(22)
                end
                pin:ClearAllPoints()
                pin:SetPoint("CENTER", WorldMapButton, "TOPLEFT", u * w, -v * h)
                pin:Show()
            end
        end
    end
end

local updateTracker

local function onSpot(id, map, x, y, sub, state, left, health, zone)
    spots[id] = { map = tonumber(map), x = tonumber(x), y = tonumber(y), sub = sub or "?", state = state,
                  ends = GetTime() * 1000 + (tonumber(left) or 0), seen = GetTime() * 1000,
                  health = tonumber(health), zone = zone }
    updatePins()
    updateTracker()
end

---------------------------------------------------------------------------------------------------------
-- Zone tracker: goblins and bosses in your zone that you're not fighting yourself - time left, health, an arrow
-- pointing to them with the distance, and a click on a line targets the goblin (when it's close enough to see)
---------------------------------------------------------------------------------------------------------
local LINE_W, LINE_H, LINE_GAP = 300, 22, 4

-- where you are, in world coordinates (from your position on your zone's map)
local lastX, lastY, lastMap
local function playerWorldPos()
    if WorldMapFrame:IsShown() then return lastX, lastY, lastMap end   -- don't flip the map someone is looking at
    SetMapToCurrentZone()
    local px, py = GetPlayerMapPosition("player")
    local area = (px ~= 0 or py ~= 0) and shownArea()
    if not area then return nil end
    lastX = area[4] - py * (area[4] - area[5])
    lastY = area[2] - px * (area[2] - area[3])
    lastMap = area[1]
    return lastX, lastY, lastMap
end

-- rotate a square texture by `angle` (radians, counter-clockwise)
local function rotate(tex, angle)
    local c, s = math.cos(angle), math.sin(angle)
    local function p(x, y) return 0.5 + (x * c - y * s), 0.5 + (x * s + y * c) end
    local ulx, uly = p(-0.5, -0.5)
    local llx, lly = p(-0.5, 0.5)
    local urx, ury = p(0.5, -0.5)
    local lrx, lry = p(0.5, 0.5)
    tex:SetTexCoord(ulx, uly, llx, lly, urx, ury, lrx, lry)
end

local tracker = CreateFrame("Frame", "LootGoblinsTracker", UIParent)
tracker:SetWidth(LINE_W) tracker:SetHeight(24)
tracker:SetPoint("TOPRIGHT", UIParent, "TOPRIGHT", -200, -210)
tracker:SetMovable(true) tracker:EnableMouse(true) tracker:RegisterForDrag("LeftButton")
tracker:SetScript("OnDragStart", tracker.StartMoving)
tracker:SetScript("OnDragStop", tracker.StopMovingOrSizing)
tracker.title = tracker:CreateFontString(nil, "OVERLAY", "GameFontNormal")
tracker.title:SetPoint("TOPLEFT", 0, 0)
tracker.title:SetText("Loot Goblins in this zone")
tracker.hint = tracker:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
tracker.hint:SetPoint("TOPRIGHT", 0, -2)
tracker.hint:SetText("click: target")
tracker.lines = {}
tracker:SetAlpha(0)

-- Lines are secure buttons (a click may target), so in combat they are faded in and out instead of shown and hidden.
local function visible(frame, on)
    frame:SetAlpha(on and 1 or 0)
    if not InCombatLockdown() then
        if on then frame:Show() else frame:Hide() end
    end
end

local function targetMacro(boss, name)
    if boss then return "/targetexact " .. name end
    return "/targetexact Loot Goblin"
end

local function trackerLine(i)
    local line = tracker.lines[i]
    if line then return line end
    if InCombatLockdown() then return nil end   -- new secure buttons only out of combat
    line = CreateFrame("Button", "LootGoblinsTrackerLine" .. i, tracker, "SecureActionButtonTemplate")
    line:SetWidth(LINE_W) line:SetHeight(LINE_H)
    line:SetPoint("TOPLEFT", tracker, "TOPLEFT", 0, -20 - (i - 1) * (LINE_H + LINE_GAP))
    line:RegisterForClicks("AnyUp")
    line:SetAttribute("type", "macro")
    line:SetAttribute("macrotext", targetMacro(false))
    line.bar = CreateFrame("StatusBar", nil, line)
    line.bar:SetPoint("TOPLEFT", 26, 0) line.bar:SetPoint("BOTTOMRIGHT", 0, 0)
    line.bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    line.bar:SetMinMaxValues(0, 100)
    line.bg = line.bar:CreateTexture(nil, "BACKGROUND")
    line.bg:SetAllPoints() line.bg:SetTexture(0, 0, 0, 0.65)
    line.hl = line.bar:CreateTexture(nil, "HIGHLIGHT")
    line.hl:SetAllPoints() line.hl:SetTexture(1, 1, 1, 0.12)
    line.arrow = line:CreateTexture(nil, "OVERLAY")
    line.arrow:SetWidth(24) line.arrow:SetHeight(24)
    line.arrow:SetPoint("LEFT", 0, 0)
    line.arrow:SetTexture("Interface\\Minimap\\MinimapArrow")
    line.text = line.bar:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    line.text:SetPoint("LEFT", 5, 0)
    line.text:SetJustifyH("LEFT")
    line.right = line.bar:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    line.right:SetPoint("RIGHT", -5, 0)
    line:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:AddLine(self.tip or "Loot Goblin")
        GameTooltip:AddLine("Click to target it (once it's close enough to see).", 1, 1, 1)
        GameTooltip:AddLine("The arrow points the way; drag the title to move this list.", 0.7, 0.7, 0.7)
        GameTooltip:Show()
    end)
    line:SetScript("OnLeave", function() GameTooltip:Hide() end)
    tracker.lines[i] = line
    return line
end

updateTracker = function()
    local zone = GetRealZoneText()
    local now = GetTime() * 1000
    local px, py, pmap
    local located = false
    local facing = GetPlayerFacing and GetPlayerFacing() or 0
    local i = 0
    for id, s in pairs(spots) do
        if s.zone == zone and not bars[id] and now <= s.ends + 5000 and now <= s.seen + 12000 then
            local line = trackerLine(i + 1)
            if line then
                i = i + 1
                local left = clock(math.max(0, s.ends - now))
                local boss = s.state == "boss"
                if boss then
                    line.text:SetText("|cffff8000" .. s.sub .. "|r - World Boss")
                    line.bar:SetStatusBarColor(0.8, 0.1, 0.1)
                    line.tip = s.sub .. " (World Boss)"
                elseif s.state == "run" then
                    line.text:SetText("Loot Goblin |cffffffff<" .. s.sub .. ">|r")
                    line.bar:SetStatusBarColor(0.9, 0.6, 0.1)
                    line.tip = "Loot Goblin <" .. s.sub .. "> - running!"
                else
                    line.text:SetText("Loot Goblin |cffffffff<" .. s.sub .. ">|r")
                    line.bar:SetStatusBarColor(0.45, 0.45, 0.45)
                    line.tip = "Loot Goblin <" .. s.sub .. "> - not hit yet"
                end
                if not located then px, py, pmap = playerWorldPos() located = true end
                local dist = ""
                line.bar:SetValue(s.health or 100)
                if not InCombatLockdown() then line:SetAttribute("macrotext", targetMacro(boss, s.sub)) end
                if px and pmap == s.map then
                    local dx, dy = s.x - px, s.y - py              -- x north, y west
                    local yards = math.sqrt(dx * dx + dy * dy)
                    rotate(line.arrow, math.atan2(dy, dx) - facing)
                    line.arrow:Show()
                    dist = string.format("|cffffd000%d yd|r   ", yards)
                else
                    line.arrow:Hide()
                end
                line.right:SetText(dist .. left .. (s.health and ("   " .. s.health .. "%") or ""))
                visible(line, true)
            end
        end
    end
    for j = i + 1, #tracker.lines do visible(tracker.lines[j], false) end
    tracker:SetAlpha(i > 0 and 1 or 0)
    -- the list holds secure buttons: in combat its size and mouse can't change (fixed after combat)
    if not InCombatLockdown() then
        tracker:SetHeight(20 + math.max(1, i) * (LINE_H + LINE_GAP) + 10)
        tracker:EnableMouse(i > 0)
    end
end

local trackerTicker = CreateFrame("Frame")
trackerTicker.t = 0
trackerTicker:SetScript("OnUpdate", function(self, e)
    self.t = self.t + e
    if self.t > 0.1 then self.t = 0 updateTracker() end   -- 10x a second: the arrow turns with you
end)

local pinTicker = CreateFrame("Frame")
pinTicker.t = 0
pinTicker:SetScript("OnUpdate", function(self, e)
    self.t = self.t + e
    if self.t > 1 then self.t = 0 if WorldMapFrame:IsShown() then updatePins() end end
end)

---------------------------------------------------------------------------------------------------------
-- Item labels: every goblin drop has a nameplate (the server marks them; needs a client with the Loot Goblins
-- nameplate support). We dress those plates like Diablo III's ground labels: a dark box with the item's name in
-- its quality colour, no health bar or level. Other plates are left alone.
---------------------------------------------------------------------------------------------------------
local PILE_FIRST, PILE_SLOTS = 1044480, 100      -- the server sends the real range (PILES) when we say hello
local plateOf = {}                               -- nameplate unit token -> its plate frame
local MONEY_HEX = "ffd100"

local function entryOf(unit)
    local guid = UnitGUID(unit)
    if not guid or string.len(guid) < 18 then return nil end
    return tonumber(string.sub(guid, 7, 12), 16)  -- 0xF130 EEEEEE CCCCCC
end

local function isDrop(unit)
    local e = entryOf(unit)
    return e and e >= PILE_FIRST and e < PILE_FIRST + PILE_SLOTS
end

local function labelText(unit)
    local raw = UnitName(unit) or ""
    local hex = string.match(raw, "|c%x%x(%x%x%x%x%x%x)")
    local text = string.gsub(string.gsub(raw, "|c%x%x%x%x%x%x%x%x", ""), "|r", "")
    return text, hex or MONEY_HEX
end

local function hideOriginal(plate)
    for _, r in ipairs({ plate:GetRegions() }) do
        if r:GetAlpha() > 0 then r.lgAlpha = r.lgAlpha or r:GetAlpha() r:SetAlpha(0) end
    end
    for _, c in ipairs({ plate:GetChildren() }) do
        if c ~= plate.lgLabel and c:GetAlpha() > 0 then c.lgAlpha = c.lgAlpha or c:GetAlpha() c:SetAlpha(0) end
    end
end

local function restoreOriginal(plate)
    for _, r in ipairs({ plate:GetRegions() }) do
        if r.lgAlpha then r:SetAlpha(r.lgAlpha) r.lgAlpha = nil end
    end
    for _, c in ipairs({ plate:GetChildren() }) do
        if c.lgAlpha then c:SetAlpha(c.lgAlpha) c.lgAlpha = nil end
    end
end

local function label(plate)
    if plate.lgLabel then return plate.lgLabel end
    local l = CreateFrame("Frame", nil, plate)
    l:SetFrameLevel(plate:GetFrameLevel() + 10)
    l:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\Buttons\\WHITE8X8", edgeSize = 1,
                    insets = { left = 1, right = 1, top = 1, bottom = 1 } })
    l:SetBackdropColor(0.02, 0.02, 0.03, 0.82)
    l.text = l:CreateFontString(nil, "OVERLAY")
    l.text:SetFont("Fonts\\FRIZQT__.TTF", 13)
    l.text:SetShadowColor(0, 0, 0, 1) l.text:SetShadowOffset(1, -1)
    l.text:SetPoint("CENTER", 0, 0)
    l:SetPoint("CENTER", plate, "CENTER", 0, 4)
    l.t = 0
    l:SetScript("OnUpdate", function(self, e)
        -- the client (mouseover highlight) and other nameplate addons show the plate's own parts again:
        -- hide them every frame so they never flash through, and follow name changes a few times a second
        if not self.unit then return end
        hideOriginal(plate)
        self.t = self.t + e
        if self.t < 0.25 then return end
        self.t = 0
        if UnitExists(self.unit) then self:Refresh() end
    end)
    function l:Refresh()
        local text, hex = labelText(self.unit)
        local r, g, b = tonumber(string.sub(hex, 1, 2), 16) / 255, tonumber(string.sub(hex, 3, 4), 16) / 255, tonumber(string.sub(hex, 5, 6), 16) / 255
        self.text:SetText(text)
        self.text:SetTextColor(r, g, b)
        local hot = plate.IsMouseOver and plate:IsMouseOver() or (GetMouseFocus and GetMouseFocus() == plate)
        self:SetBackdropBorderColor(hot and r or r * 0.6, hot and g or g * 0.6, hot and b or b * 0.6, hot and 1 or 0.9)
        self:SetBackdropColor(hot and 0.12 or 0.02, hot and 0.12 or 0.02, hot and 0.14 or 0.03, hot and 0.92 or 0.82)
        self:SetWidth(self.text:GetStringWidth() + 18)
        self:SetHeight(self.text:GetStringHeight() + 9)
    end
    plate.lgLabel = l
    return l
end

local function dress(unit)
    local plate = GetNamePlateForUnit and GetNamePlateForUnit(unit)
    if not plate then return end
    plateOf[unit] = plate
    if isDrop(unit) then
        local l = label(plate)
        l.unit = unit
        hideOriginal(plate)
        l:Refresh()
        l:Show()
    elseif plate.lgLabel and plate.lgLabel:IsShown() then
        plate.lgLabel:Hide() plate.lgLabel.unit = nil
        restoreOriginal(plate)
    end
end

local function undress(unit)
    local plate = plateOf[unit]
    plateOf[unit] = nil
    if plate and plate.lgLabel then
        plate.lgLabel:Hide() plate.lgLabel.unit = nil
        restoreOriginal(plate)
    end
end

-- Labels that would overlap are stacked upwards (like Diablo III): the lowest on screen keeps its place, the next one
-- moves up just above it, and so on. Only our labels move; other nameplates are left where the client puts them.
local GAP = 2
local stacker = CreateFrame("Frame")
stacker.t = 0
stacker:SetScript("OnUpdate", function(self, e)
    self.t = self.t + e
    if self.t < 0.05 then return end
    self.t = 0
    local list = {}
    for _, plate in pairs(plateOf) do
        local l = plate.lgLabel
        if l and l:IsShown() and plate:IsVisible() then
            local x, y = plate:GetCenter()
            if x then
                local ps, ls = plate:GetEffectiveScale(), l:GetEffectiveScale()
                list[#list + 1] = { l = l, plate = plate, x = x * ps, y = (y + 4) * ps, w = l:GetWidth() * ls, h = l:GetHeight() * ls, s = ls }
            end
        end
    end
    table.sort(list, function(a, b) return a.y < b.y end)
    local placed = {}
    for _, it in ipairs(list) do
        local y = it.y
        local moved = true
        while moved do
            moved = false
            for _, o in ipairs(placed) do
                if math.abs(it.x - o.x) < (it.w + o.w) / 2 + GAP and math.abs(y - o.y) < (it.h + o.h) / 2 + GAP then
                    y = o.y + (it.h + o.h) / 2 + GAP
                    moved = true
                end
            end
        end
        it.y = y
        placed[#placed + 1] = it
        it.l:ClearAllPoints()
        it.l:SetPoint("CENTER", it.plate, "CENTER", 0, (y / it.s) - select(2, it.plate:GetCenter()) * it.plate:GetEffectiveScale() / it.s)
    end
end)

local plates = CreateFrame("Frame")
-- clients without nameplate units (stock 3.3.5) don't know these events: then there are no labels, nothing breaks
pcall(plates.RegisterEvent, plates, "NAME_PLATE_UNIT_ADDED")
pcall(plates.RegisterEvent, plates, "NAME_PLATE_UNIT_REMOVED")
plates:SetScript("OnEvent", function(_, event, unit)
    if event == "NAME_PLATE_UNIT_ADDED" then dress(unit) else undress(unit) end
end)

---------------------------------------------------------------------------------------------------------
-- Messages
---------------------------------------------------------------------------------------------------------
addon:RegisterEvent("CHAT_MSG_ADDON")
addon:RegisterEvent("PLAYER_ENTERING_WORLD")
addon:RegisterEvent("MIRROR_TIMER_START")
addon:RegisterEvent("WORLD_MAP_UPDATE")
addon:SetScript("OnEvent", function(self, event, prefix, message)
    if event == "PLAYER_ENTERING_WORLD" then
        send("HELLO\t1")
        return
    end
    if event == "MIRROR_TIMER_START" then
        -- a fatigue bar from the server means it doesn't know we're here: say hello again, it switches to our bar
        if prefix == "EXHAUSTION" then send("HELLO\t1") end
        return
    end
    if event == "WORLD_MAP_UPDATE" then updatePins() return end
    if prefix ~= PREFIX or not message then return end
    local p = split(message)
    local kind = p[1]
    if kind == "TIME" then onTime(p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9])
    elseif kind == "ADD" then
        local bar = bars[p[2]]
        if bar then popup(bar, string.format("+%.1fs", (tonumber(p[3]) or 0) / 1000), 0.3, 1, 0.3) end
    elseif kind == "END" then onEnd(p[2], p[3])
    elseif kind == "ROLL" then addRoll(p[2], p[3], p[4], p[5], p[6])
    elseif kind == "RESULT" then onResult(p[2], p[3], p[4], p[5], p[6], p[7])
    elseif kind == "SPOT" then onSpot(p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10])
    elseif kind == "GONE" then spots[p[2]] = nil updatePins() updateTracker()
    elseif kind == "PILES" then PILE_FIRST, PILE_SLOTS = tonumber(p[2]) or PILE_FIRST, tonumber(p[3]) or PILE_SLOTS
    end
end)

SLASH_LOOTGOBLINS1 = "/lootgoblins"
SLASH_LOOTGOBLINS2 = "/lgob"
SlashCmdList.LOOTGOBLINS = function(msg)
    if msg == "plates" then
        -- what the item labels see: each nameplate unit, its GUID, entry and whether it counts as a drop
        local n = 0
        for i = 1, 60 do
            local unit = "nameplate" .. i
            if UnitExists(unit) then
                n = n + 1
                local plate = GetNamePlateForUnit and GetNamePlateForUnit(unit)
                DEFAULT_CHAT_FRAME:AddMessage(string.format("%s: %s guid=%s entry=%s drop=%s plate=%s", unit, tostring(UnitName(unit)),
                    tostring(UnitGUID(unit)), tostring(entryOf(unit)), tostring(isDrop(unit) and true or false), tostring(plate ~= nil)))
            end
        end
        DEFAULT_CHAT_FRAME:AddMessage(string.format("|cffffd000Loot Goblins|r: %d nameplate units; GetNamePlateForUnit %s; drops %d-%d",
            n, GetNamePlateForUnit and "exists" or "MISSING", PILE_FIRST, PILE_FIRST + PILE_SLOTS - 1))
        return
    end
    if msg == "test" then
        onTime("test", "Epic loot", 30000, 300000, 220000, "run", 63)
        bars["test"].test = true   -- no server behind it: it runs down on its own
        onSpot("testspot", -1, 0, 0, "Gold", "run", 25000, 70, GetRealZoneText())   -- a sample zone-tracker line
        addRoll("0", "0", "19019", "1", "15")
    else
        send("HELLO\t1")
        DEFAULT_CHAT_FRAME:AddMessage("|cffffd000Loot Goblins|r: connected. |cffffffff/lgob test|r shows a sample bar and roll.")
    end
end
