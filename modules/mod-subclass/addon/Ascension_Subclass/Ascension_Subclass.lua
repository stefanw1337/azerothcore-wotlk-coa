local PREFIX = "SUBCLASS"
local NODE = 34
local LINE_TEXTURE = "Interface\\TaxiFrame\\UI-Taxi-Line"
local STRATA_ABOVE = { BACKGROUND = "LOW", LOW = "MEDIUM", MEDIUM = "HIGH", HIGH = "DIALOG", DIALOG = "FULLSCREEN" }

local state = { eligible = false, insights = 0, trees = {}, ranks = {}, classPoints = 0, specPoints = 0,
    resources = {}, powers = {} }
local POWER_NAMES = { [0] = "Mana", [1] = "Rage", [2] = "Focus", [3] = "Energy", [6] = "Runic Power" }
local POWER_COLORS = { [0] = { 0, 0, 1 }, [1] = { 1, 0, 0 }, [2] = { 1, 0.5, 0.25 }, [3] = { 1, 1, 0 },
    [6] = { 0, 0.82, 1 } }
local resourceFrame
local UpdateResourceFrame
local UpdateLentRunes
local incoming
local pending = false
local panel, tab, window
local advancementTab
local treeFrames = {}

local function Send(message)
    SendAddonMessage(PREFIX, message, "WHISPER", UnitName("player"))
end

local function Split(text, separator)
    local parts = {}
    for part in string.gmatch(text, "([^" .. separator .. "]+)") do
        parts[#parts + 1] = part
    end
    return parts
end

local function Entry(id)
    if not C_CharacterAdvancement or not C_CharacterAdvancement.GetEntryByInternalID then
        return nil
    end
    local ok, entry = pcall(C_CharacterAdvancement.GetEntryByInternalID, id)
    if ok then
        return entry
    end
end

local function IconPath(icon)
    if not icon or icon == "" then
        return "Interface\\Icons\\INV_Misc_QuestionMark"
    end
    if string.find(icon, "\\", 1, true) then
        return icon
    end
    return "Interface\\Icons\\" .. icon
end

local function DrawLine(texture, canvas, sx, sy, ex, ey, width)
    local dx, dy = ex - sx, ey - sy
    local cx, cy = (sx + ex) / 2, (sy + ey) / 2
    if dx < 0 then
        dx, dy = -dx, -dy
    end
    local length = math.sqrt(dx * dx + dy * dy)
    texture:ClearAllPoints()
    if length == 0 then
        texture:Hide()
        return
    end
    local factor = 128 / 126 / 2
    local s, c = -dy / length, dx / length
    local sc = s * c
    local bw, bh, blx, bly, tlx, tly, trx, try, brx, bry
    if dy >= 0 then
        bw = ((length * c) - (width * s)) * factor
        bh = ((width * c) - (length * s)) * factor
        blx, bly, bry = (width / length) * sc, s * s, (length / width) * sc
        brx, tlx, tly, trx = 1 - bly, bly, 1 - bry, 1 - blx
        try = brx
    else
        bw = ((length * c) + (width * s)) * factor
        bh = ((width * c) + (length * s)) * factor
        blx, bly, brx = s * s, -(length / width) * sc, 1 + (width / length) * sc
        bry, tlx, tly, try = blx, 1 - brx, 1 - blx, 1 - bly
        trx = tly
    end
    texture:SetTexCoord(tlx, tly, blx, bly, trx, try, brx, bry)
    texture:SetPoint("BOTTOMLEFT", canvas, "BOTTOMLEFT", cx - bw, cy - bh)
    texture:SetPoint("TOPRIGHT", canvas, "BOTTOMLEFT", cx + bw, cy + bh)
    texture:Show()
end

local function RankOf(id)
    return state.ranks[id] or 0
end

local function SpentIn(tree)
    local ae, te = 0, 0
    for _, node in ipairs(tree.nodes) do
        local rank = RankOf(node.id)
        ae = ae + rank * (node.entry.AECost or 0)
        te = te + rank * (node.entry.TECost or 0)
    end
    return ae, te
end

local function Locked(tree, node)
    local entry = node.entry
    if (entry.RequiredLevel or 0) > UnitLevel("player") then
        return true
    end
    if node.ability then
        return false
    end
    local ae, te = SpentIn(tree)
    if (entry.RequiredTabAEInvestment or 0) > ae or (entry.RequiredTabTEInvestment or 0) > te then
        return true
    end
    for _, required in ipairs(entry.RequiredIDs or {}) do
        if RankOf(required) == 0 then
            return true
        end
    end
    return false
end

local function GrantedByLevel(entry)
    return (entry.AECost or 0) == 0 and (entry.TECost or 0) == 0
end

local function NodeEnter(button)
    local node = button.node
    local entry = node.entry
    local rank = RankOf(node.id)
    local maxRank = #(entry.Spells or {})
    GameTooltip:SetOwner(button, "ANCHOR_RIGHT")
    local spell = entry.Spells and (entry.Spells[rank > 0 and rank or 1])
    if spell then
        GameTooltip:SetHyperlink("spell:" .. spell)
    else
        GameTooltip:SetText(entry.Name or ("Entry " .. node.id), 1, 1, 1)
    end
    GameTooltip:AddLine(" ")
    GameTooltip:AddLine(string.format("Rank %d/%d", rank, maxRank), 1, 0.82, 0)
    if (entry.AECost or 0) > 0 then
        GameTooltip:AddLine(string.format("Costs %d class point(s) per rank", entry.AECost), 0.8, 0.8, 0.8)
    end
    if (entry.TECost or 0) > 0 then
        GameTooltip:AddLine(string.format("Costs %d specialization point(s) per rank", entry.TECost), 0.8, 0.8, 0.8)
    end
    if (entry.RequiredLevel or 0) > UnitLevel("player") then
        GameTooltip:AddLine(string.format("Requires level %d", entry.RequiredLevel), 1, 0.3, 0.3)
    elseif Locked(button.tree, node) then
        local ae, te = SpentIn(button.tree)
        if (entry.RequiredTabTEInvestment or 0) > te or (entry.RequiredTabAEInvestment or 0) > ae then
            GameTooltip:AddLine(string.format("Locked: needs %d point(s) spent in this tree (%d now).",
                math.max(entry.RequiredTabTEInvestment or 0, entry.RequiredTabAEInvestment or 0), ae + te), 1, 0.3, 0.3,
                true)
        end
        for _, required in ipairs(entry.RequiredIDs or {}) do
            if RankOf(required) == 0 then
                local needed = C_CharacterAdvancement and C_CharacterAdvancement.GetEntryByInternalID
                    and C_CharacterAdvancement.GetEntryByInternalID(required)
                GameTooltip:AddLine(string.format("Locked: needs %s first (entry %d).",
                    needed and needed.Name or "another talent", required), 1, 0.3, 0.3, true)
            end
        end
    end
    if GrantedByLevel(entry) then
        GameTooltip:AddLine(string.format("Learned automatically at level %d", math.max(entry.RequiredLevel or 0, 1)),
            0.8, 0.8, 0.8)
        GameTooltip:Show()
        return
    end
    if rank < maxRank then
        GameTooltip:AddLine("Left-click: learn the next rank", 0, 1, 0)
    end
    if rank > 0 then
        GameTooltip:AddLine("Right-click: unlearn one rank (may cost gold or marks)", 1, 0.5, 0)
    end
    GameTooltip:Show()
end

local function NodeClick(button, mouse)
    if pending then
        return
    end
    local node = button.node
    if GrantedByLevel(node.entry) then
        return
    end
    local rank = RankOf(node.id)
    local maxRank = #(node.entry.Spells or {})
    local wanted
    if mouse == "RightButton" then
        wanted = rank > 0 and rank - 1 or nil
    else
        wanted = rank < maxRank and rank + 1 or nil
    end
    if wanted then
        pending = true
        Send("LEARN " .. node.id .. " " .. wanted)
    end
end

local function Canvas(index)
    local frame = treeFrames[index]
    if frame then
        return frame
    end
    frame = CreateFrame("Frame", nil, panel)
    frame.title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalHuge")
    frame.title:SetPoint("TOP", frame, "TOP", 0, -6)
    frame.points = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    frame.points:SetPoint("TOP", frame.title, "BOTTOM", 0, -2)
    frame.buttons = {}
    frame.lines = {}
    treeFrames[index] = frame
    return frame
end

local function NodeButton(canvas, index)
    local button = canvas.buttons[index]
    if button then
        return button
    end
    button = CreateFrame("Button", nil, canvas)
    button:SetWidth(NODE)
    button:SetHeight(NODE)
    button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    button.icon = button:CreateTexture(nil, "ARTWORK")
    button.icon:SetAllPoints(button)
    button.border = button:CreateTexture(nil, "OVERLAY")
    button.border:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
    button.border:SetBlendMode("ADD")
    button.border:SetWidth(NODE * 1.8)
    button.border:SetHeight(NODE * 1.8)
    button.border:SetPoint("CENTER", button, "CENTER", 0, 0)
    button.rank = button:CreateFontString(nil, "OVERLAY", "NumberFontNormalSmall")
    button.rank:SetPoint("BOTTOMRIGHT", button, "BOTTOMRIGHT", 2, -2)
    button:SetScript("OnEnter", NodeEnter)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    button:SetScript("OnClick", NodeClick)
    canvas.buttons[index] = button
    return button
end

local function Line(canvas, index)
    local line = canvas.lines[index]
    if not line then
        line = canvas:CreateTexture(nil, "BORDER")
        line:SetTexture(LINE_TEXTURE)
        canvas.lines[index] = line
    end
    return line
end

local function Position(entry, grid)
    if grid then
        return entry.Column or 0, entry.Row or 0
    end
    local x, y = entry.PositionX or 0, entry.PositionY or 0
    if x == 0 and y == 0 then
        x, y = entry.Column or 0, entry.Row or 0
    end
    return x, y
end

local STANDARD_TREE_ART = {
    ["Warrior:Arms"] = "bg-warrior-arms", ["Warrior:Fury"] = "bg-warrior-fury",
    ["Warrior:Protection"] = "bg-warrior-protection", ["Paladin:Holy"] = "bg-paladin-holy",
    ["Paladin:Protection"] = "bg-paladin-protection", ["Paladin:Retribution"] = "bg-paladin-retribution",
    ["Hunter:Beast Mastery"] = "bg-hunter-beastmaster", ["Hunter:Marksmanship"] = "bg-hunter-marksman",
    ["Hunter:Survival"] = "bg-hunter-survival", ["Rogue:Assassination"] = "bg-rogue-assassination",
    ["Rogue:Combat"] = "bg-rogue-combat", ["Rogue:Subtlety"] = "bg-rogue-subtlety",
    ["Priest:Discipline"] = "bg-priest-discipline", ["Priest:Holy"] = "bg-priest-holy",
    ["Priest:Shadow"] = "bg-priest-shadow", ["Death Knight:Blood"] = "bg-deathknight-blood",
    ["Death Knight:Frost"] = "bg-deathknight-frost", ["Death Knight:Unholy"] = "bg-deathknight-unholy",
    ["Shaman:Elemental"] = "bg-shaman-elemental", ["Shaman:Enhancement"] = "bg-shaman-enhancement",
    ["Shaman:Restoration"] = "bg-shaman-restoration", ["Mage:Arcane"] = "bg-mage-arcane",
    ["Mage:Fire"] = "bg-mage-fire", ["Mage:Frost"] = "bg-mage-frost", ["Warlock:Affliction"] = "bg-warlock-affliction",
    ["Warlock:Demonology"] = "bg-warlock-demonology", ["Warlock:Destruction"] = "bg-warlock-destruction",
    ["Druid:Balance"] = "bg-druid-balance", ["Druid:Feral"] = "bg-druid-cat", ["Druid:Feral Combat"] = "bg-druid-cat",
    ["Druid:Restoration"] = "bg-druid-restoration",
}
local STANDARD_CLASS_NAMES = { Warrior = true, Paladin = true, Hunter = true, Rogue = true, Priest = true,
    ["Death Knight"] = true, Shaman = true, Mage = true, Warlock = true, Druid = true }
local TREE_ART_FOLDER = "Interface\\AddOns\\Ascension_Subclass\\Textures\\"
local TREE_ART_WIDTH, TREE_ART_HEIGHT, TREE_ART_SIZE = 512, 360, 512

local function StandardTree(tree)
    return tree.className and STANDARD_CLASS_NAMES[tree.className]
end

local function ShowTreeArt(tree, canvas)
    if not canvas.art then
        canvas.art = canvas:CreateTexture(nil, "BACKGROUND")
        canvas.art:SetAllPoints(canvas)
        canvas.art:SetAlpha(0.8)
    end
    local art = tree.className and STANDARD_TREE_ART[tree.className .. ":" .. (tree.title or "")]
    if art and canvas.art:SetTexture(TREE_ART_FOLDER .. art) then
        local width, height = canvas:GetWidth(), canvas:GetHeight()
        local shown = math.min(TREE_ART_WIDTH, TREE_ART_HEIGHT * width / height)
        local left = (TREE_ART_WIDTH - shown) / 2
        canvas.art:SetTexCoord(left / TREE_ART_SIZE, (left + shown) / TREE_ART_SIZE, 0, TREE_ART_HEIGHT / TREE_ART_SIZE)
        canvas.art:Show()
    else
        canvas.art:Hide()
    end
end

local function UsesGrid(tree)
    local placed, gridded = false, true
    for _, node in ipairs(tree.nodes) do
        local entry = node.entry
        if (entry.PositionX or 0) ~= 0 or (entry.PositionY or 0) ~= 0 then
            placed = true
        end
        if entry.Column == nil or entry.Row == nil then
            gridded = false
        end
    end
    return gridded and (not placed or tree.className and STANDARD_CLASS_NAMES[tree.className] ~= nil)
end

local function RenderTree(tree, canvas)
    ShowTreeArt(tree, canvas)
    canvas.title:SetText(tree.title or "")
    local ae, te = SpentIn(tree)
    canvas.points:SetText(string.format("%d point(s) spent", ae + te))
    local grid = UsesGrid(tree)
    local minX, maxX, minY, maxY
    for _, node in ipairs(tree.nodes) do
        local x, y = Position(node.entry, grid)
        minX, maxX = math.min(minX or x, x), math.max(maxX or x, x)
        minY, maxY = math.min(minY or y, y), math.max(maxY or y, y)
    end
    local perRow = math.max(1, math.floor((canvas:GetWidth() - 8) / (NODE + 6)))
    local abilityRows = math.ceil(#tree.abilities / perRow)
    local strip = abilityRows * (NODE + 6) + (abilityRows > 0 and 18 or 0)
    local width, height = canvas:GetWidth() - NODE * 2, canvas:GetHeight() - NODE * 2 - 50 - strip
    local spanX, spanY = (maxX or 0) - (minX or 0), (maxY or 0) - (minY or 0)
    local centers = {}
    for index, node in ipairs(tree.nodes) do
        local x, y = Position(node.entry, grid)
        local px = NODE + (spanX > 0 and (x - minX) / spanX * width or width / 2)
        local py = strip + NODE + (spanY > 0 and (maxY - y) / spanY * height or height / 2)
        centers[node.id] = { px, py }
        local button = NodeButton(canvas, index)
        button.node, button.tree = node, tree
        button:ClearAllPoints()
        button:SetPoint("CENTER", canvas, "BOTTOMLEFT", px, py)
        button.icon:SetTexture(IconPath(node.entry.Icon))
        local rank, maxRank = RankOf(node.id), #(node.entry.Spells or {})
        button.rank:SetText(rank .. "/" .. maxRank)
        local locked = Locked(tree, node)
        button.icon:SetDesaturated(rank == 0 and locked)
        if rank >= maxRank and maxRank > 0 then
            button.border:SetVertexColor(1, 0.82, 0)
            button.border:Show()
        elseif not locked then
            button.border:SetVertexColor(0, 1, 0)
            button.border:Show()
        else
            button.border:Hide()
        end
        button:Show()
    end
    for index, node in ipairs(tree.abilities) do
        local slot = index - 1
        local px = 4 + NODE / 2 + (slot % perRow) * (NODE + 6)
        local py = NODE / 2 + 2 + (abilityRows - 1 - math.floor(slot / perRow)) * (NODE + 6)
        local button = NodeButton(canvas, #tree.nodes + index)
        button.node, button.tree = node, tree
        button:ClearAllPoints()
        button:SetPoint("CENTER", canvas, "BOTTOMLEFT", px, py)
        button.icon:SetTexture(IconPath(node.entry.Icon))
        local rank, maxRank = RankOf(node.id), #(node.entry.Spells or {})
        button.rank:SetText(rank .. "/" .. maxRank)
        local locked = Locked(tree, node)
        button.icon:SetDesaturated(rank == 0)
        if rank >= maxRank and maxRank > 0 then
            button.border:SetVertexColor(1, 0.82, 0)
            button.border:Show()
        elseif not locked then
            button.border:SetVertexColor(0, 1, 0)
            button.border:Show()
        else
            button.border:Hide()
        end
        button:Show()
    end
    if not canvas.abilityLabel then
        canvas.abilityLabel = canvas:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    end
    canvas.abilityLabel:ClearAllPoints()
    canvas.abilityLabel:SetPoint("BOTTOMLEFT", canvas, "BOTTOMLEFT", 4, strip - 14)
    canvas.abilityLabel:SetText(abilityRows > 0 and "Abilities" or "")
    for index = #tree.nodes + #tree.abilities + 1, #canvas.buttons do
        canvas.buttons[index]:Hide()
    end
    local lineCount = 0
    for _, node in ipairs(tree.nodes) do
        local from = centers[node.id]
        for _, target in ipairs(node.entry.ConnectedNodes or {}) do
            local to = centers[target]
            if to then
                lineCount = lineCount + 1
                DrawLine(Line(canvas, lineCount), canvas, from[1], from[2], to[1], to[2], 24)
            end
        end
    end
    for index = lineCount + 1, #canvas.lines do
        canvas.lines[index]:Hide()
    end
end

local NAMINGS = {
    Ogre = {
        tab = "Two-Head",
        waiting = "Your second head has no second way yet. Find the Book of Origins in Shadowfang Keep.",
        insight = "Your second head holds an insight. Visit any class trainer to choose a second way.",
    },
    ThinHuman = {
        tab = "Two-Thins",
        waiting = "Your twin has no way of his own yet. Find your father's Bravery Badge in the Deadmines.",
        insight = "Your twin wears the Bravery Badge. Visit any class trainer so he can choose his own way.",
    },
}
local DEFAULT_NAMING = {
    tab = "Second Head",
    waiting = "No second way yet.",
    insight = "You hold an insight. Visit any class trainer to choose a second way.",
}

local function Naming()
    local _, race = UnitRace("player")
    return NAMINGS[race] or DEFAULT_NAMING
end

local function Render()
    if not panel or not panel:IsShown() then
        return
    end
    panel.points:SetText(string.format("Borrowed points left: |cffffffff%d|r class, |cffffffff%d|r specialization",
        state.classPoints, state.specPoints))
    if #state.trees == 0 then
        if state.eligible then
            panel.status:SetText(state.insights > 0 and Naming().insight or Naming().waiting)
        else
            panel.status:SetText("Only two-headed Ogres and the Thin Twins can learn a second way.")
        end
        panel.status:Show()
    else
        panel.status:Hide()
    end
    local count = #state.trees
    local inner = panel:GetWidth() - 40
    for index, tree in ipairs(state.trees) do
        local canvas = Canvas(index)
        canvas:ClearAllPoints()
        canvas:SetPoint("TOPLEFT", panel, "TOPLEFT", 20 + (index - 1) * inner / count, -40)
        canvas:SetWidth(inner / count - 10)
        canvas:SetHeight(panel:GetHeight() - 80)
        canvas:Show()
        RenderTree(tree, canvas)
    end
    for index = count + 1, #treeFrames do
        treeFrames[index]:Hide()
    end
end

local function BuildTrees()
    for _, tree in ipairs(state.trees) do
        tree.nodes = {}
        tree.abilities = {}
        local candidates = {}
        for _, id in ipairs(tree.ids) do
            candidates[#candidates + 1] = id
        end
        if not StandardTree(tree) then
            for _, id in ipairs(tree.abilityIds or {}) do
                candidates[#candidates + 1] = id
            end
        end
        for _, id in ipairs(candidates) do
            local entry = Entry(id)
            if entry and entry.PositionX and entry.PositionY then
                tree.nodes[#tree.nodes + 1] = { id = id, entry = entry }
            elseif entry then
                tree.abilities[#tree.abilities + 1] = { id = id, entry = entry, ability = true }
            end
        end
        table.sort(tree.abilities, function(a, b)
            return (a.entry.RequiredLevel or 0) < (b.entry.RequiredLevel or 0)
        end)
    end
end

local function AuraStacks(spell)
    for _, filter in ipairs({ "HELPFUL", "HARMFUL" }) do
        for index = 1, 40 do
            local name, _, _, count, _, _, _, _, _, _, id = UnitAura("player", index, filter)
            if not name then
                break
            end
            if id == spell then
                return (count and count > 0) and count or 1
            end
        end
    end
    return 0
end

local function ResourceRow(index)
    local row = resourceFrame.rows[index]
    if row then
        return row
    end
    row = CreateFrame("StatusBar", nil, resourceFrame)
    row:SetHeight(14)
    row:SetPoint("TOPLEFT", resourceFrame, "TOPLEFT", 8, -8 - (index - 1) * 18)
    row:SetPoint("TOPRIGHT", resourceFrame, "TOPRIGHT", -8, -8 - (index - 1) * 18)
    row:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    row.background = row:CreateTexture(nil, "BACKGROUND")
    row.background:SetAllPoints(row)
    row.background:SetTexture(0, 0, 0, 0.6)
    row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.text:SetPoint("CENTER", row, "CENTER", 0, 0)
    resourceFrame.rows[index] = row
    return row
end

local function RefreshResources()
    if not resourceFrame or not resourceFrame:IsShown() then
        return
    end
    local index = 0
    for _, resource in ipairs(state.resources) do
        index = index + 1
        local row = ResourceRow(index)
        local stacks = AuraStacks(resource.spell)
        local maximum = resource.maximum > 0 and resource.maximum or math.max(stacks, 1)
        row:SetMinMaxValues(0, maximum)
        row:SetValue(stacks)
        row:SetStatusBarColor(0.6, 0.3, 0.9)
        row.text:SetText(resource.maximum > 0 and string.format("%s %d/%d", resource.name, stacks, resource.maximum)
            or string.format("%s %d", resource.name, stacks))
        row:Show()
    end
    for _, power in ipairs(state.powers) do
        index = index + 1
        local row = ResourceRow(index)
        local value, maximum = UnitPower("player", power), UnitPowerMax("player", power)
        local color = POWER_COLORS[power] or { 0.7, 0.7, 0.7 }
        row:SetMinMaxValues(0, math.max(maximum, 1))
        row:SetValue(value)
        row:SetStatusBarColor(color[1], color[2], color[3])
        local divisor = (power == 1 or power == 6) and 10 or 1
        row.text:SetText(string.format("%s %d/%d", POWER_NAMES[power] or ("Power " .. power), value / divisor,
            maximum / divisor))
        row:Show()
    end
    for extra = index + 1, #resourceFrame.rows do
        resourceFrame.rows[extra]:Hide()
    end
    resourceFrame:SetHeight(16 + index * 18)
end

UpdateResourceFrame = function()
    if not resourceFrame then
        resourceFrame = CreateFrame("Frame", "Ascension_SubclassResourceFrame", UIParent)
        resourceFrame:SetWidth(180)
        resourceFrame:SetBackdrop({
            bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
            tile = true, tileSize = 16, edgeSize = 12,
            insets = { left = 3, right = 3, top = 3, bottom = 3 },
        })
        resourceFrame:SetBackdropColor(0, 0, 0, 0.7)
        resourceFrame:SetPoint("TOPLEFT", PlayerFrame, "BOTTOMLEFT", 100, 30)
        resourceFrame:SetMovable(true)
        resourceFrame:EnableMouse(true)
        resourceFrame:RegisterForDrag("LeftButton")
        resourceFrame:SetScript("OnDragStart", resourceFrame.StartMoving)
        resourceFrame:SetScript("OnDragStop", resourceFrame.StopMovingOrSizing)
        resourceFrame:SetUserPlaced(true)
        resourceFrame.rows = {}
        resourceFrame:SetScript("OnUpdate", function(self, elapsed)
            self.wait = (self.wait or 0) + elapsed
            if self.wait >= 0.2 then
                self.wait = 0
                RefreshResources()
            end
        end)
    end
    if #state.resources + #state.powers > 0 then
        resourceFrame:Show()
        RefreshResources()
    else
        resourceFrame:Hide()
    end
end

UpdateLentRunes = function()
    local frame = _G.RuneFrame
    if not frame then
        return
    end
    if not state.runes then
        frame:Hide()
        return
    end
    frame:Show()
    for index = 1, 6 do
        local button = _G["RuneButtonIndividual" .. index]
        if button then
            button:Show()
            if _G.RuneButton_Update then
                pcall(_G.RuneButton_Update, button, index, true)
            end
        end
    end
end

local twinOutfit = {}
local ShowTwinPreview
local twinState = { has = false, out = false, split = false }
local ShowTwinState

local function ColoredSubclass(subclass)
    local token = string.upper((string.gsub(subclass, " ", "")))
    local color = RAID_CLASS_COLORS and RAID_CLASS_COLORS[token]
    if not color then
        return "|cffffffff(" .. subclass .. ")|r"
    end
    return string.format("|cff%02x%02x%02x(%s)|r", color.r * 255, color.g * 255, color.b * 255, subclass)
end

local function SubclassedLevelLine()
    local text = CharacterLevelText and CharacterLevelText:GetText()
    local subclass = state and state.subclass
    if text and subclass and subclass ~= "" and not text:find("(" .. subclass .. ")", 1, true) then
        CharacterLevelText:SetText(text .. " " .. ColoredSubclass(subclass))
    end
end

local function ShowSubclassInLevelLine()
    if PaperDollFrame_SetLevel and CharacterFrame and CharacterFrame:IsShown() then
        PaperDollFrame_SetLevel()
    end
end

if PaperDollFrame_SetLevel then
    hooksecurefunc("PaperDollFrame_SetLevel", SubclassedLevelLine)
end

local relicState = { eligible = false, entry = 0 }
local relicButton
local RELIC_EMPTY = "Interface\\Paperdoll\\UI-PaperDoll-Slot-Relic"
local RANGED_EMPTY = "Interface\\Paperdoll\\UI-PaperDoll-Slot-Ranged"

local function ServerSlotOf(bag, slot)
    if bag == 0 then
        return 255, 22 + slot
    end
    return 18 + bag, slot - 1
end

local function CursorBagSlot()
    local kind, _, link = GetCursorInfo()
    if kind ~= "item" then
        return
    end
    for bag = 0, 4 do
        for slot = 1, GetContainerNumSlots(bag) do
            local _, _, locked = GetContainerItemInfo(bag, slot)
            if locked and GetContainerItemLink(bag, slot) == link then
                return bag, slot
            end
        end
    end
end

local function WearCursorRelic()
    local bag, slot = CursorBagSlot()
    if not bag then
        return false
    end
    local serverBag, serverSlot = ServerSlotOf(bag, slot)
    ClearCursor()
    Send("RELICWEAR " .. serverBag .. " " .. serverSlot)
    return true
end

local function ShowRelic()
    if not relicButton then
        return
    end
    if not relicState.eligible then
        relicButton:Hide()
        return
    end
    local icon = relicState.entry > 0 and (GetItemIcon and GetItemIcon(relicState.entry)
        or select(10, GetItemInfo(relicState.entry)))
    relicButton.icon:SetTexture(icon or RELIC_EMPTY)
    if relicButton.anchor then
        relicButton:Show()
    else
        relicButton:Hide()
    end
end

local function CreateRelicButton()
    if relicButton then
        return
    end
    relicButton = CreateFrame("Button", "Ascension_SubclassRelicSlot", UIParent, "ItemButtonTemplate")
    relicButton:Hide()
    relicButton.icon = _G["Ascension_SubclassRelicSlotIconTexture"]
    relicButton:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    relicButton:RegisterForDrag("LeftButton")
    relicButton:SetScript("OnReceiveDrag", WearCursorRelic)
    relicButton:SetScript("OnClick", function(_, mouse)
        if WearCursorRelic() then
            return
        end
        if relicState.entry > 0 then
            Send(mouse == "RightButton" and "RELICUSE" or "RELICTAKE")
        end
    end)
    relicButton:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        if relicState.entry > 0 then
            GameTooltip:SetHyperlink("item:" .. relicState.entry)
            GameTooltip:AddLine("Left-click to take it off. Right-click to use it.", 0.5, 0.8, 1, true)
        else
            GameTooltip:SetText("Relic", 1, 1, 1)
            GameTooltip:AddLine("Drop a libram, idol, totem or sigil here to hold it beside your ranged weapon.",
                nil, nil, nil, true)
        end
        GameTooltip:Show()
    end)
    relicButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
    ShowRelic()
end

local RANGED_SLOT_ID = 18

local function VisibleRegions(frame, found, wanted)
    for _, region in ipairs({ frame:GetRegions() }) do
        if region:GetObjectType() == wanted and region:IsVisible() then
            found[#found + 1] = region
        end
    end
    for _, child in ipairs({ frame:GetChildren() }) do
        if child:IsVisible() then
            if (wanted == "Button" and child:GetObjectType() == "Button") or (wanted == "PlayerModel"
                and (child:GetObjectType() == "PlayerModel" or child:GetObjectType() == "DressUpModel")) then
                found[#found + 1] = child
            end
            VisibleRegions(child, found, wanted)
        end
    end
    return found
end

local SHEET_MIN_SIZE = 300
local SLOT_MIN, SLOT_MAX = 30, 48
local CORNER_PORTRAIT_REACH = 90
local sheet

local function LevelLineIn(frame)
    local className = UnitClass("player")
    for _, line in ipairs(VisibleRegions(frame, {}, "FontString")) do
        local text = line:GetText()
        if text and className and text:find(className, 1, true) and text:find(LEVEL or "Level", 1, true)
            and text:find("%d") then
            return line
        end
    end
end

local function LargeTopFrame(frame)
    local parent = frame:GetParent()
    return frame ~= UIParent and frame ~= WorldFrame and frame:IsVisible() and (not parent or parent == UIParent)
        and frame ~= panel
        and (frame:GetWidth() or 0) >= SHEET_MIN_SIZE and (frame:GetHeight() or 0) >= SHEET_MIN_SIZE
end

local function FindSheet()
    if sheet and sheet:IsVisible() then
        return sheet
    end
    sheet = nil
    for _, known in ipairs({ AscensionCharacterFrame, CharacterFrame }) do
        if known and known:IsVisible() and LevelLineIn(known) then
            sheet = known
            return sheet
        end
    end
    if CharacterFrame and CharacterFrame:IsVisible() and LevelLineIn(CharacterFrame) then
        sheet = CharacterFrame
        return sheet
    end
    local frame = EnumerateFrames()
    while frame do
        if LargeTopFrame(frame) and LevelLineIn(frame) then
            sheet = frame
            return sheet
        end
        frame = EnumerateFrames(frame)
    end
end

local function SlotSized(button)
    local width, height = button:GetWidth() or 0, button:GetHeight() or 0
    return width >= SLOT_MIN and width <= SLOT_MAX and math.abs(width - height) < 4
end

local function ShownRangedSlot(frame)
    for _, known in ipairs({ AscensionCharacterRangedSlot, CharacterRangedSlot }) do
        if known and known:IsVisible() then
            return known
        end
    end
    local buttons = VisibleRegions(frame, {}, "Button")
    for _, button in ipairs(buttons) do
        local name = button:GetName()
        if button ~= relicButton and name and name:find("Ranged") then
            return button
        end
    end
    for _, button in ipairs(buttons) do
        if button ~= relicButton and button.GetID and button:GetID() == RANGED_SLOT_ID and SlotSized(button) then
            return button
        end
    end
    local lowest
    for _, button in ipairs(buttons) do
        if button ~= relicButton and SlotSized(button) and button:GetBottom() and button:GetLeft() and (not lowest
            or button:GetBottom() < lowest:GetBottom() - 2
            or (math.abs(button:GetBottom() - lowest:GetBottom()) <= 2 and button:GetLeft() > lowest:GetLeft())) then
            lowest = button
        end
    end
    return lowest
end

local function CornerPortrait(frame)
    if frame == AscensionCharacterFrame and AscensionCharacterFramePortrait then
        return AscensionCharacterFramePortrait
    end
    local left, top = frame:GetLeft(), frame:GetTop()
    if not left or not top then
        return
    end
    local best
    for _, region in ipairs(VisibleRegions(frame, {}, "Texture")) do
        local width, height = region:GetWidth() or 0, region:GetHeight() or 0
        if region:GetLeft() and region:GetTop() and region:GetLeft() < left + CORNER_PORTRAIT_REACH
            and region:GetTop() > top - CORNER_PORTRAIT_REACH and math.abs(width - height) < PORTRAIT_SQUARENESS
            and width >= PORTRAIT_MIN and width <= PORTRAIT_MAX and (not best or width > best:GetWidth()) then
            best = region
        end
    end
    return best
end

local function ShowPlayerPortrait(texture)
    if not texture or texture.subclassPortrait == texture:GetTexture() then
        return
    end
    SetPortraitTexture(texture, "player")
    texture:SetTexCoord(0, 1, 0, 1)
    texture.subclassPortrait = texture:GetTexture()
end

local function ShowMicroPortrait()
    if MicroButtonPortrait then
        MicroButtonPortrait:Show()
        MicroButtonPortrait.subclassPortrait = nil
        ShowPlayerPortrait(MicroButtonPortrait)
    end
end

local CYCLOPS_CAMERA = 1
local cyclops = { model = nil, previews = {}, asked = {} }

local function CyclopsCamera()
    return Ascension_SubclassDump and Ascension_SubclassDump.cyclopsCamera or CYCLOPS_CAMERA
end

local function CyclopsNudge()
    local nudge = Ascension_SubclassDump and Ascension_SubclassDump.cyclopsNudge
    return nudge and nudge[1] or 0, nudge and nudge[2] or 0, nudge and nudge[3] or 0
end

local function FollowHost(overlay, elapsed)
    local host = overlay.host
    overlay.loading = (overlay.loading or 0) + (elapsed or 0)
    if overlay.loading < 3 then
        overlay:SetCamera(CyclopsCamera())
    end
    if host.GetModelScale then
        overlay:SetModelScale(host:GetModelScale())
    end
    overlay:SetFacing(host:GetFacing())
    local x, y, z = host:GetPosition()
    local nx, ny, nz = CyclopsNudge()
    overlay:SetPosition(x + nx, y + ny, z + nz)
end

local function ShowCyclopsHelmetOn(host, path)
    if not host then
        return
    end
    local overlay = host.cyclopsOverlay
    if not path then
        if overlay then
            overlay:Hide()
        end
        return
    end
    if not overlay then
        overlay = CreateFrame("Model", nil, host)
        overlay:SetAllPoints(host)
        overlay:EnableMouse(false)
        overlay.host = host
        overlay:SetScript("OnUpdate", FollowHost)
        host.cyclopsOverlay = overlay
    end
    overlay:SetFrameLevel(host:GetFrameLevel() + 1)
    if overlay.path ~= path or overlay.camera ~= CyclopsCamera() then
        overlay:SetModel(path)
        overlay:SetCamera(CyclopsCamera())
        overlay.path, overlay.camera, overlay.loading = path, CyclopsCamera(), 0
    end
    FollowHost(overlay)
    overlay:Show()
end

local function CyclopsPreviewOf(itemId)
    if not itemId or itemId == 0 then
        return nil
    end
    local path = cyclops.previews[itemId]
    if path == nil and not cyclops.asked[itemId] then
        cyclops.asked[itemId] = true
        Send("CYCLOPSPREVIEW " .. itemId)
    end
    return path or nil
end

local SHEET_FIT = { zoom = 0.8, right = 0.45, up = -0.47 }

local function SheetFit()
    local saved = Ascension_SubclassDump and Ascension_SubclassDump.sheetFit
    return saved or SHEET_FIT
end

local function SheetModel(frame)
    local best, bestArea
    for _, child in ipairs(VisibleRegions(frame, {}, "PlayerModel")) do
        local area = (child:GetWidth() or 0) * (child:GetHeight() or 0)
        if not best or area > bestArea then
            best, bestArea = child, area
        end
    end
    return best
end

local function FitSheetModel(frame)
    if (UnitRace("player")) ~= "Two-Headed Ogre" then
        return
    end
    local model = SheetModel(frame)
    if not model then
        return
    end
    local fit = SheetFit()
    local x, y, z = model:GetPosition()
    if math.abs(x - fit.zoom) > 0.001 or math.abs(y - fit.right) > 0.001 or math.abs(z - fit.up) > 0.001 then
        model:SetPosition(fit.zoom, fit.right, fit.up)
    end
end

local sheetWatch = CreateFrame("Frame")
sheetWatch:RegisterEvent("PLAYER_ENTERING_WORLD")
sheetWatch:RegisterEvent("UNIT_PORTRAIT_UPDATE")
sheetWatch:RegisterEvent("UNIT_MODEL_CHANGED")
sheetWatch:SetScript("OnEvent", function(_, _, unit)
    if not unit or unit == "player" then
        ShowMicroPortrait()
        if sheet then
            local portrait = CornerPortrait(sheet)
            if portrait then
                portrait.subclassPortrait = nil
            end
        end
    end
end)
sheetWatch:SetScript("OnUpdate", function(self, elapsed)
    self.wait = (self.wait or 0) + elapsed
    if self.wait < 0.5 then
        return
    end
    self.wait = 0
    if MicroButtonPortrait and not MicroButtonPortrait:IsShown() then
        ShowMicroPortrait()
    end
    local frame = FindSheet()
    if not frame then
        return
    end
    ShowPlayerPortrait(CornerPortrait(frame))
    FitSheetModel(frame)
    if (UnitRace("player")) == "Two-Headed Ogre" then
        ShowCyclopsHelmetOn(SheetModel(frame), cyclops.model)
    end
    local subclass = state and state.subclass
    local line = subclass and subclass ~= "" and LevelLineIn(frame)
    if line and not line:GetText():find("(" .. subclass .. ")", 1, true) then
        line:SetText(line:GetText() .. " " .. ColoredSubclass(subclass))
    end
    if relicButton and relicState.eligible then
        local slot = ShownRangedSlot(frame)
        if slot and relicButton.anchor ~= slot then
            relicButton:SetParent(slot:GetParent())
            relicButton:ClearAllPoints()
            relicButton:SetPoint("LEFT", slot, "RIGHT", 8, 0)
            relicButton:SetFrameStrata(slot:GetFrameStrata())
            relicButton:SetFrameLevel(slot:GetFrameLevel() + 2)
            relicButton.anchor = slot
        end
        relicButton:Show()
        local icon = slot and (slot.icon or (slot:GetName() and _G[slot:GetName() .. "IconTexture"]))
        if icon and not GetInventoryItemTexture("player", RANGED_SLOT_ID) and icon:GetTexture() ~= RANGED_EMPTY then
            icon:SetTexture(RANGED_EMPTY)
        end
    end
end)


local Stances = { size = 30, gap = 8, spells = {}, pending = false }

function Stances.StanceBarAnchor()
    local last
    for index = 1, 10 do
        local button = _G["ShapeshiftButton" .. index]
        if button and button:IsVisible() then
            last = button
        end
    end
    return last
end

function Stances.PlaceStanceBar()
    local saved = Ascension_SubclassDump and Ascension_SubclassDump.stanceBarPoint
    Stances.bar:ClearAllPoints()
    if saved then
        Stances.bar:SetPoint(saved[1], UIParent, saved[2], saved[3], saved[4])
        return
    end
    local anchor = Stances.StanceBarAnchor()
    if anchor then
        Stances.bar:SetPoint("LEFT", anchor, "RIGHT", Stances.gap * 2, 0)
    elseif ShapeshiftBarFrame then
        Stances.bar:SetPoint("BOTTOMLEFT", ShapeshiftBarFrame, "BOTTOMLEFT", 0, 0)
    else
        Stances.bar:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 120)
    end
end

function Stances.StanceButton(index)
    local name = "Ascension_SubclassStanceButton" .. index
    local button = _G[name] or CreateFrame("CheckButton", name, Stances.bar, "SecureActionButtonTemplate")
    if not button.icon then
        button:SetWidth(Stances.size)
        button:SetHeight(Stances.size)
        button:RegisterForClicks("AnyUp")
        button.icon = button:CreateTexture(nil, "BACKGROUND")
        button.icon:SetAllPoints(button)
        button:SetNormalTexture("Interface\\Buttons\\UI-Quickslot2")
        local normal = button:GetNormalTexture()
        normal:ClearAllPoints()
        normal:SetPoint("CENTER", button, "CENTER", 0, 0)
        normal:SetWidth(Stances.size * 1.8)
        normal:SetHeight(Stances.size * 1.8)
        button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
        button:SetCheckedTexture("Interface\\Buttons\\CheckButtonHilight", "ADD")
        button.cooldown = CreateFrame("Cooldown", name .. "Cooldown", button, "CooldownFrameTemplate")
        button.cooldown:SetAllPoints(button)
        button:SetScript("OnEnter", function(self)
            if self.spellId then
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:SetHyperlink("spell:" .. self.spellId)
                GameTooltip:Show()
            end
        end)
        button:SetScript("OnLeave", function() GameTooltip:Hide() end)
        button:SetScript("PostClick", function(self) self:SetChecked(self.active) end)
        button:RegisterForDrag("LeftButton")
        button:SetScript("OnDragStart", function()
            if IsAltKeyDown() and not InCombatLockdown() and not Stances.btBar then
                Stances.bar:StartMoving()
            end
        end)
        button:SetScript("OnDragStop", function()
            if not Stances.btBar then
                Stances.bar:GetScript("OnDragStop")(Stances.bar)
            end
        end)
    end
    return button
end

function Stances.BuildStanceBar()
    if InCombatLockdown() then
        Stances.pending = true
        return
    end
    Stances.pending = false
    if not Stances.bar then
        Stances.bar = CreateFrame("Frame", "Ascension_SubclassStanceBar", UIParent)
        Stances.bar:SetHeight(Stances.size)
        Stances.bar:SetMovable(true)
        Stances.bar:EnableMouse(true)
        Stances.bar:RegisterForDrag("LeftButton")
        Stances.bar:SetScript("OnDragStart", function(self)
            if IsAltKeyDown() then
                self:StartMoving()
            end
        end)
        Stances.bar:SetScript("OnDragStop", function(self)
            self:StopMovingOrSizing()
            local point, _, relative, x, y = self:GetPoint()
            Ascension_SubclassDump = Ascension_SubclassDump or {}
            Ascension_SubclassDump.stanceBarPoint = { point, relative, x, y }
        end)
    end
    local shown = 0
    local named = {}
    for index, spellId in ipairs(Stances.spells) do
        local name, _, icon = GetSpellInfo(spellId)
        if name and not named[name] then
            named[name] = true
            shown = shown + 1
            local button = Stances.StanceButton(shown)
            button:SetAttribute("type", "spell")
            button:SetAttribute("spell", name)
            button.spellId, button.spellName, button.spellIcon = spellId, name, icon
            button.icon:SetTexture(icon)
            button:ClearAllPoints()
            button:SetPoint("LEFT", Stances.bar, "LEFT", (shown - 1) * (Stances.size + Stances.gap), 0)
            button:Show()
        end
    end
    for index = shown + 1, 20 do
        local button = _G["Ascension_SubclassStanceButton" .. index]
        if button then
            button:Hide()
            button.spellId, button.spellName = nil, nil
        end
    end
    if Stances.btBar then
        Stances.LendToBartender(shown)
        return
    end
    Stances.bar:SetWidth(math.max(1, shown * (Stances.size + Stances.gap)))
    if not Stances.placed then
        Stances.PlaceStanceBar()
        Stances.placed = true
    end
    if shown > 0 then
        Stances.bar:Show()
    else
        Stances.bar:Hide()
    end
end

local function ClearSetPoint(button, ...)
    button:ClearAllPoints()
    button:SetPoint(...)
end

function Stances.LendToBartender(shown)
    local bar = Stances.btBar
    bar.buttons = {}
    for index = 1, shown do
        local button = _G["Ascension_SubclassStanceButton" .. index]
        button:SetParent(bar)
        button.ClearSetPoint = ClearSetPoint
        bar.buttons[index] = button
    end
    Stances.bar:Hide()
    bar:UpdateButtonLayout()
end

if Bartender4 and Bartender4.ButtonBar and Bartender4.NewModule then
    local module = Bartender4:NewModule("SubclassStances")
    local defaults = { profile = Bartender4:Merge({ enabled = true, visibility = { possess = false } },
        Bartender4.ButtonBar.defaults) }
    function module:OnInitialize()
        self.db = Bartender4.db:RegisterNamespace("SubclassStances", defaults)
        self:SetEnabledState(self.db.profile.enabled)
    end
    function module:SetupOptions()
        if not self.options then
            local prototype = Bartender4.ButtonBar.prototype
            self.optionobject = prototype:GetOptionObject()
            self.optionobject.table.general.args.rows.max = 10
            local enabled = {
                type = "toggle",
                order = 1,
                name = "Enabled",
                desc = "Show the subclass's stances, aspects, auras and presences on their own bar",
                get = function() return self.db.profile.enabled end,
                set = "ToggleModule",
                handler = self,
            }
            self.optionobject:AddElement("general", "enabled", enabled)
            self.disabledoptions = { general = { type = "group", name = "General Settings", cmdInline = true,
                order = 1, args = { enabled = enabled } } }
            self.options = { order = 31, type = "group", name = "Subclass Stance Bar",
                desc = "Configure the subclass stance bar", childGroups = "tab" }
            Bartender4:RegisterBarOptions("SubclassStances", self.options)
        end
        self.options.args = self:IsEnabled() and self.optionobject.table or self.disabledoptions
    end
    function module:OnEnable()
        if not self.bar then
            self.bar = Bartender4.ButtonBar:Create("SubclassStances", self.db.profile, "Subclass Stance Bar")
            self.bar.button_width, self.bar.button_height = Stances.size, Stances.size
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
            self.bar:ClearSetPoint("CENTER", 0, -160)
            self.bar:SavePosition()
        end
        Stances.btBar = self.bar
        Stances.BuildStanceBar()
    end
    function module:OnDisable()
        if self.bar then
            self.bar:Disable()
        end
        if self.ToggleOptions then
            self:ToggleOptions()
        end
        Stances.btBar = nil
        Stances.BuildStanceBar()
    end
end

Stances.watch = CreateFrame("Frame")
Stances.watch:RegisterEvent("PLAYER_REGEN_ENABLED")
Stances.watch:RegisterEvent("UPDATE_SHAPESHIFT_FORMS")
Stances.watch:SetScript("OnEvent", function(_, event)
    if Stances.pending or (event == "UPDATE_SHAPESHIFT_FORMS" and Stances.bar and not InCombatLockdown()) then
        Stances.BuildStanceBar()
    end
end)
Stances.watch:SetScript("OnUpdate", function(self, elapsed)
    self.wait = (self.wait or 0) + elapsed
    local bar = Stances.btBar or Stances.bar
    if self.wait < 0.25 or not (bar and bar:IsShown()) then
        return
    end
    self.wait = 0
    for index = 1, 20 do
        local button = _G["Ascension_SubclassStanceButton" .. index]
        if not button or not button:IsShown() or not button.spellName then
            break
        end
        button.active = UnitBuff("player", button.spellName) ~= nil
        button:SetChecked(button.active)
        button.icon:SetTexture(button.active and "Interface\\Icons\\Spell_Nature_WispSplode" or button.spellIcon)
        local start, duration, enabled = GetSpellCooldown(button.spellName)
        if start and duration and duration > 1.5 and enabled == 1 then
            button.cooldown:SetCooldown(start, duration)
        end
        button.icon:SetVertexColor(1, 1, 1)
        if not IsUsableSpell(button.spellName) then
            button.icon:SetVertexColor(0.4, 0.4, 0.4)
        end
    end
end)

local function Receive(message)
    local command, rest = string.match(message, "^(%S+)%s?(.*)$")
    if command == "TWINSTATE" then
        local has, out, split = string.match(rest, "^(%d) (%d) (%d)$")
        twinState = { has = has == "1", out = out == "1", split = split == "1" }
        if ShowTwinState then
            ShowTwinState()
        end
        return
    end
    if command == "OTHERCAST" then
        local spellId, result = string.match(rest, "^(%d+) (%d+)$")
        local name = spellId and GetSpellInfo(tonumber(spellId))
        local who = (UnitRace("player")) == "Two-Headed Ogre" and "Other head" or "Brother"
        if name and result == "0" then
            local line = who .. ": " .. name
            if CombatText_AddMessage and SHOW_COMBAT_TEXT == "1" then
                CombatText_AddMessage(line, CombatText_StandardScroll, 0.8, 0.6, 1)
            else
                UIErrorsFrame:AddMessage(line, 0.8, 0.6, 1)
            end
        elseif name and Ascension_SubclassDump and Ascension_SubclassDump.debugCasts then
            DEFAULT_CHAT_FRAME:AddMessage(string.format("%s could not cast %s (result %s)", who, name, result))
        end
        return
    end
    if command == "STANCES" then
        Stances.spells = {}
        for spellId in string.gmatch(rest, "%d+") do
            Stances.spells[#Stances.spells + 1] = tonumber(spellId)
        end
        Stances.BuildStanceBar()
        return
    end
    if command == "CYCLOPSMODEL" then
        cyclops.model = rest ~= "-" and rest ~= "" and rest or nil
        return
    end
    if command == "CYCLOPSPREVIEW" then
        local itemId, path = string.match(rest, "^(%d+) (.+)$")
        if itemId then
            cyclops.previews[tonumber(itemId)] = path ~= "-" and path or false
            if ShowTwinPreview then
                ShowTwinPreview()
            end
        end
        return
    end
    if command == "RELIC" then
        local eligible, entry = string.match(rest, "^(%d) (%d+)$")
        relicState = { eligible = eligible == "1", entry = tonumber(entry) or 0 }
        CreateRelicButton()
        ShowRelic()
        return
    end
    if command == "TWINWEAR" then
        twinOutfit = {}
        for itemId in string.gmatch(rest, "%d+") do
            twinOutfit[#twinOutfit + 1] = tonumber(itemId)
        end
        if ShowTwinPreview then
            ShowTwinPreview()
        end
        return
    end
    if command == "RESULT" then
        pending = false
        local ok, reason = string.match(rest, "^(%d)%s?(.*)$")
        if ok == "0" and reason and reason ~= "" then
            UIErrorsFrame:AddMessage(reason, 1, 0.1, 0.1)
        end
        return
    end
    if command == "RESET" then
        local eligible, _, insights = string.match(rest, "^(%d+) (%d+) (%d+)$")
        incoming = { eligible = eligible == "1", insights = tonumber(insights) or 0, trees = {}, ranks = {},
            classPoints = 0, specPoints = 0, resources = {}, powers = {}, runes = false }
    elseif command == "SUBCLASS" and incoming then
        incoming.subclass = rest
    elseif not incoming then
        return
    elseif command == "TREE" then
        local index, spec, title = string.match(rest, "^(%d+) (%d+) (.*)$")
        incoming.trees[tonumber(index)] = { spec = tonumber(spec), title = title, ids = {} }
    elseif command == "ART" then
        local index, className = string.match(rest, "^(%d+) (.*)$")
        local tree = incoming.trees[tonumber(index)]
        if tree then
            tree.className = className
        end
    elseif command == "ABIL" then
        local index, list = string.match(rest, "^(%d+) (.*)$")
        local tree = incoming.trees[tonumber(index)]
        if tree then
            tree.abilityIds = tree.abilityIds or {}
            for _, id in ipairs(Split(list, ",")) do
                tree.abilityIds[#tree.abilityIds + 1] = tonumber(id)
            end
        end
    elseif command == "IDS" then
        local index, list = string.match(rest, "^(%d+) (.*)$")
        local tree = incoming.trees[tonumber(index)]
        if tree then
            for _, id in ipairs(Split(list, ",")) do
                tree.ids[#tree.ids + 1] = tonumber(id)
            end
        end
    elseif command == "RANKS" then
        for _, pair in ipairs(Split(rest, ",")) do
            local id, rank = string.match(pair, "^(%d+):(%d+)$")
            if id then
                incoming.ranks[tonumber(id)] = tonumber(rank)
            end
        end
    elseif command == "POINTS" then
        local ae, te = string.match(rest, "^(%d+) (%d+)$")
        incoming.classPoints, incoming.specPoints = tonumber(ae) or 0, tonumber(te) or 0
    elseif command == "RES" then
        local spell, maximum, name = string.match(rest, "^(%d+) (%d+) (.*)$")
        if spell then
            incoming.resources[#incoming.resources + 1] = { spell = tonumber(spell), maximum = tonumber(maximum),
                name = name }
        end
    elseif command == "POWER" then
        incoming.powers[#incoming.powers + 1] = tonumber(rest)
    elseif command == "RUNES" then
        incoming.runes = true
    elseif command == "DONE" then
        state = incoming
        incoming = nil
        pending = false
        BuildTrees()
        Render()
        UpdateResourceFrame()
        UpdateLentRunes()
        ShowSubclassInLevelLine()
    end
end

local function TextOf(frame)
    if frame.GetText then
        local ok, text = pcall(frame.GetText, frame)
        if ok then
            return text
        end
    end
end

local function FindAdvancementTabs()
    local tabs = {}
    local frame = EnumerateFrames()
    while frame do
        local kind = frame.GetObjectType and frame:GetObjectType()
        if kind == "Button" or kind == "CheckButton" then
            local text = TextOf(frame)
            if text == "Character Advancement" or text == "Vanity" or text == "Wardrobe" then
                tabs[#tabs + 1] = frame
            end
        end
        frame = EnumerateFrames(frame)
    end
    local parent = tabs[1] and tabs[1]:GetParent()
    for _, other in ipairs(tabs) do
        if other:GetParent() ~= parent then
            return nil
        end
    end
    if #tabs < 2 then
        return nil
    end
    local height = tabs[1]:GetHeight()
    tabs = {}
    for _, child in ipairs({ parent:GetChildren() }) do
        local kind = child:GetObjectType()
        local text = (kind == "Button" or kind == "CheckButton") and TextOf(child)
        if text and text ~= "" and child ~= tab and math.abs(child:GetHeight() - height) < 2 then
            tabs[#tabs + 1] = child
        end
    end
    table.sort(tabs, function(a, b) return (a:GetLeft() or 0) < (b:GetLeft() or 0) end)
    return tabs, parent
end

ShowTwinState = function()
    if not (panel and panel.brother) then
        return
    end
    if not twinState.has then
        panel.brother:Hide()
        return
    end
    panel.brother:SetText(twinState.out and "Send your brother home" or "Call your brother back")
    panel.brother:Show()
end

local function CreatePanel(parent)
    panel = CreateFrame("Frame", "Ascension_SubclassPanel", parent or UIParent)
    panel:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background-Dark",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 },
    })
    panel:EnableMouse(true)
    panel.fill = panel:CreateTexture(nil, "BACKGROUND")
    panel.fill:SetPoint("TOPLEFT", panel, "TOPLEFT", 11, -12)
    panel.fill:SetPoint("BOTTOMRIGHT", panel, "BOTTOMRIGHT", -12, 11)
    panel.fill:SetTexture(0.03, 0.03, 0.04, 1)
    panel.header = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    panel.header:SetPoint("TOP", panel, "TOP", 0, -16)
    panel.header:SetText(Naming().tab)
    panel.points = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    panel.points:SetPoint("BOTTOM", panel, "BOTTOM", 0, 18)
    panel.status = panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightLarge")
    panel.status:SetPoint("CENTER", panel, "CENTER", 0, 0)
    panel.status:SetWidth(500)
    panel.brother = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
    panel.brother:SetWidth(190)
    panel.brother:SetHeight(22)
    panel.brother:SetPoint("BOTTOMLEFT", panel, "BOTTOMLEFT", 18, 14)
    panel.brother:SetScript("OnClick", function() Send("TWINTOGGLE") end)
    panel.brother:Hide()
    panel:SetScript("OnShow", function()
        Send("SYNC")
        Render()
    end)
    panel:Hide()
    if not parent then
        panel:SetWidth(900)
        panel:SetHeight(580)
        panel:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
        panel:SetMovable(true)
        panel:RegisterForDrag("LeftButton")
        panel:SetScript("OnDragStart", panel.StartMoving)
        panel:SetScript("OnDragStop", panel.StopMovingOrSizing)
        panel:SetFrameStrata("HIGH")
        local close = CreateFrame("Button", nil, panel, "UIPanelCloseButton")
        close:SetPoint("TOPRIGHT", panel, "TOPRIGHT", -4, -4)
        tinsert(UISpecialFrames, "Ascension_SubclassPanel")
    end
end

local TAB_ICON = "Interface\\Icons\\INV_Misc_GroupNeedMore"

local function CopyPoints(region, mapped, copy)
    for index = 1, region:GetNumPoints() do
        local point, relative, relativePoint, x, y = region:GetPoint(index)
        copy:SetPoint(point, mapped[relative] or relative, relativePoint, x, y)
    end
end

local function CopyTexturePath(path)
    if type(path) == "string" and string.find(string.lower(path), "interface\\icons\\", 1, true) then
        return TAB_ICON
    end
    return path
end

local function CopyContents(source, copy, mapped, label, text)
    for _, region in ipairs({ source:GetRegions() }) do
        if region:GetObjectType() == "Texture" and region:GetTexture() then
            local texture = copy:CreateTexture(nil, region:GetDrawLayer())
            texture:SetTexture(CopyTexturePath(region:GetTexture()))
            texture:SetTexCoord(region:GetTexCoord())
            texture:SetVertexColor(region:GetVertexColor())
            texture:SetBlendMode(region:GetBlendMode())
            texture:SetAlpha(region:GetAlpha())
            texture:SetWidth(region:GetWidth())
            texture:SetHeight(region:GetHeight())
            mapped[region] = texture
            if not region:IsShown() then
                texture:Hide()
            end
        elseif region:GetObjectType() == "FontString" and region ~= text then
            local own = copy:CreateFontString(nil, region:GetDrawLayer())
            own:SetFontObject(region:GetFontObject())
            local shown = region:GetText()
            own:SetText(shown == label and Naming().tab or shown)
            mapped[region] = own
        end
    end
end

local function TabFromTemplate(source, parent)
    local ok, made = pcall(CreateFrame, "CheckButton", "Ascension_SubclassTab", parent, "CollectionTabTemplate")
    if not ok or not made then
        return nil
    end
    made:SetWidth(source:GetWidth())
    made:SetHeight(source:GetHeight())
    made:SetPoint("LEFT", source, "RIGHT", 2, 0)
    made:SetText(Naming().tab)
    local icon = _G[made:GetName() .. "IconIcon"]
    if icon then
        icon:SetTexture(TAB_ICON)
    end
    return made
end

local function CopyTab(source, parent)
    local copy = CreateFrame("Button", "Ascension_SubclassTab", parent)
    copy:SetWidth(source:GetWidth())
    copy:SetHeight(source:GetHeight())
    copy:SetFrameLevel(source:GetFrameLevel())
    copy:SetPoint("LEFT", source, "RIGHT", 2, 0)
    local text = source.GetFontString and source:GetFontString()
    local mapped = { [source] = copy }
    CopyContents(source, copy, mapped, TextOf(source), text)
    for region, own in pairs(mapped) do
        if region ~= source then
            if region:GetNumPoints() > 0 then
                CopyPoints(region, mapped, own)
            else
                own:SetAllPoints(own:GetParent())
            end
        end
    end
    for _, kind in ipairs({ "Normal", "Pushed", "Highlight", "Disabled" }) do
        local texture = source["Get" .. kind .. "Texture"](source)
        if texture and texture:GetTexture() then
            copy["Set" .. kind .. "Texture"](copy, texture:GetTexture())
            local own = copy["Get" .. kind .. "Texture"](copy)
            own:SetTexCoord(texture:GetTexCoord())
            if kind == "Highlight" then
                own:SetBlendMode(texture:GetBlendMode())
            end
        end
    end
    if text then
        copy:SetNormalFontObject(source:GetNormalFontObject() or text:GetFontObject())
        copy:SetHighlightFontObject(source:GetHighlightFontObject() or text:GetFontObject())
        copy:SetText(Naming().tab)
        local own = copy:GetFontString()
        own:ClearAllPoints()
        CopyPoints(text, mapped, own)
        if own:GetNumPoints() == 0 then
            own:SetPoint("CENTER", copy, "CENTER", 0, 0)
        end
    end
    return copy
end

local function TabsAfter(first, height)
    local found = {}
    for _, child in ipairs({ first:GetParent():GetChildren() }) do
        local kind = child:GetObjectType()
        local text = (kind == "Button" or kind == "CheckButton") and TextOf(child)
        if text and text ~= "" and child ~= tab and child ~= first and math.abs(child:GetHeight() - height) < 2 then
            if not child.subclassOrder then
                local left, start = child:GetLeft(), first:GetLeft()
                child.subclassOrder = (left and start and left < start) and -1 or (left or 1e6)
            end
            if child.subclassOrder >= 0 then
                found[#found + 1] = child
            end
        end
    end
    table.sort(found, function(a, b) return a.subclassOrder < b.subclassOrder end)
    return found
end

local function KeepTabsInRow(first, gap)
    local height = first:GetHeight()
    local function Arrange()
        local previous = tab
        for _, other in ipairs(TabsAfter(first, height)) do
            if not other.subclassHooked then
                other.subclassHooked = true
                other:HookScript("OnClick", function()
                    panel:Hide()
                    if tab.SetChecked then
                        tab:SetChecked(false)
                    end
                end)
            end
            if other:IsShown() then
                local left, right = other:GetLeft(), previous:GetRight()
                if not left or not right or math.abs(left - right - gap) > 1 then
                    other:ClearAllPoints()
                    other:SetPoint("LEFT", previous, "RIGHT", gap, 0)
                end
                previous = other
            end
        end
    end
    local keeper = CreateFrame("Frame", nil, tab)
    keeper:SetScript("OnUpdate", function(self, elapsed)
        self.wait = (self.wait or 0) + elapsed
        if self.wait >= 0.2 then
            self.wait = 0
            Arrange()
        end
    end)
    tab:SetPoint("LEFT", first, "RIGHT", gap, 0)
    Arrange()
end

local function AttachToWindow()
    if tab then
        return true
    end
    local tabs, parent = FindAdvancementTabs()
    if not tabs then
        return false
    end
    window = parent
    while window:GetParent() and window:GetParent() ~= UIParent do
        window = window:GetParent()
    end
    CreatePanel(window)
    panel:SetPoint("TOPLEFT", window, "TOPLEFT", 8, -24)
    panel:SetPoint("BOTTOMRIGHT", window, "BOTTOMRIGHT", -8, 8)
    panel:SetFrameLevel(window:GetFrameLevel() + 40)
    panel:SetFrameStrata(STRATA_ABOVE[window:GetFrameStrata()] or "HIGH")
    local first, after = tabs[1], tabs[2]
    for index, other in ipairs(tabs) do
        if TextOf(other) == "Character Advancement" then
            first, after = other, tabs[index + 1]
        end
    end
    advancementTab = first
    tab = TabFromTemplate(first, parent) or CopyTab(first, parent)
    local gap = 4
    if after and after:GetLeft() and first:GetRight() then
        gap = math.max(after:GetLeft() - first:GetRight(), 2)
    end
    KeepTabsInRow(first, gap)
    tab:SetScript("OnClick", function()
        if advancementTab and advancementTab:IsVisible() then
            advancementTab:Click()
        end
        for _, other in ipairs(tabs) do
            if other.SetChecked then
                other:SetChecked(false)
            end
            if other.Enable then
                other:Enable()
            end
        end
        if tab.SetChecked then
            tab:SetChecked(true)
        end
        panel:Show()
    end)
    local function LeavePanel()
        panel:Hide()
        if tab.SetChecked then
            tab:SetChecked(false)
        end
    end
    for _, other in ipairs(tabs) do
        other:HookScript("OnClick", LeavePanel)
        other:HookScript("OnMouseDown", LeavePanel)
    end
    window:HookScript("OnHide", function() panel:Hide() end)
    return true
end

local function IsStandardClass()
    local _, _, classId = UnitClass("player")
    return classId and classId <= 11
end

local CLASSIC_TIERS, CLASSIC_COLUMNS = 11, 4
local TALENT_POINTS_PER_TIER = 5
local CLASSIC_NODE = 37
local ART_FOLDER = "Interface\\AddOns\\Ascension_Subclass\\Textures\\"
local ART_WIDTH, ART_HEIGHT, ART_SIZE = 512, 360, 512
local CLASSIC_ART = {
    deathknightblood = "bg-deathknight-blood", deathknightfrost = "bg-deathknight-frost",
    deathknightunholy = "bg-deathknight-unholy", druidbalance = "bg-druid-balance", druidferalcombat = "bg-druid-cat",
    druidrestoration = "bg-druid-restoration", hunterbeastmastery = "bg-hunter-beastmaster",
    huntermarksmanship = "bg-hunter-marksman", huntersurvival = "bg-hunter-survival", magearcane = "bg-mage-arcane",
    magefire = "bg-mage-fire", magefrost = "bg-mage-frost", paladinholy = "bg-paladin-holy",
    paladinprotection = "bg-paladin-protection", paladincombat = "bg-paladin-retribution",
    priestdiscipline = "bg-priest-discipline", priestholy = "bg-priest-holy", priestshadow = "bg-priest-shadow",
    rogueassassination = "bg-rogue-assassination", roguecombat = "bg-rogue-combat",
    roguesubtlety = "bg-rogue-subtlety", shamanelementalcombat = "bg-shaman-elemental",
    shamanenhancement = "bg-shaman-enhancement", shamanrestoration = "bg-shaman-restoration",
    warlockcurses = "bg-warlock-affliction", warlocksummoning = "bg-warlock-demonology",
    warlockdestruction = "bg-warlock-destruction", warriorarms = "bg-warrior-arms", warriorfury = "bg-warrior-fury",
    warriorprotection = "bg-warrior-protection",
}
local BACKGROUND_PIECES = {
    { suffix = "-TopLeft", x = 0, y = 0, w = 256 / 300, h = 256 / 331, u = 1, v = 1 },
    { suffix = "-TopRight", x = 256 / 300, y = 0, w = 44 / 300, h = 256 / 331, u = 0.6875, v = 1 },
    { suffix = "-BottomLeft", x = 0, y = 256 / 331, w = 256 / 300, h = 75 / 331, u = 1, v = 0.5859375 },
    { suffix = "-BottomRight", x = 256 / 300, y = 256 / 331, w = 44 / 300, h = 75 / 331, u = 0.6875, v = 0.5859375 },
}
local book, ownPage
local classicFrames = {}

local function ClassicCanvas(index)
    local frame = classicFrames[index]
    if frame then
        return frame
    end
    frame = CreateFrame("Frame", nil, ownPage)
    frame.background = {}
    for _, piece in ipairs(BACKGROUND_PIECES) do
        local texture = frame:CreateTexture(nil, "BACKGROUND")
        texture:SetTexCoord(0, piece.u, 0, piece.v)
        texture:SetAlpha(0.7)
        frame.background[#frame.background + 1] = texture
    end
    frame.art = frame:CreateTexture(nil, "BACKGROUND")
    frame.art:SetAllPoints(frame)
    frame.art:SetAlpha(0.8)
    frame.title = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightHuge")
    frame.title:SetPoint("TOP", frame, "TOP", 0, -10)
    frame.title:SetShadowOffset(2, -2)
    frame.points = frame:CreateFontString(nil, "OVERLAY", "GameFontGreenLarge")
    frame.points:SetPoint("TOP", frame.title, "BOTTOM", 0, -2)
    frame.buttons = {}
    frame.lines = {}
    classicFrames[index] = frame
    return frame
end

local ADVANCEMENT_COST_LINES = { "^Cost:", "^Level:" }

local function IsAdvancementCostLine(text)
    for _, pattern in ipairs(ADVANCEMENT_COST_LINES) do
        if text and string.find(text, pattern) then
            return true
        end
    end
    return false
end

local function WithoutAdvancementCosts(tooltip)
    local name = tooltip:GetName()
    local lines, dropped = {}, false
    for index = 1, tooltip:NumLines() do
        local left, right = _G[name .. "TextLeft" .. index], _G[name .. "TextRight" .. index]
        local text = left and left:GetText()
        if IsAdvancementCostLine(text) then
            dropped = true
        else
            local shownRight = right and right:IsShown() and right:GetText()
            lines[#lines + 1] = { text = text or " ", color = { left:GetTextColor() }, right = shownRight,
                rightColor = right and { right:GetTextColor() } }
        end
    end
    if not dropped then
        return
    end
    while #lines > 0 and lines[#lines].text == " " and not lines[#lines].right do
        lines[#lines] = nil
    end
    tooltip:ClearLines()
    for index, line in ipairs(lines) do
        if index == 1 and not line.right then
            tooltip:SetText(line.text, unpack(line.color))
        elseif line.right then
            local r, g, b = unpack(line.color)
            local rr, rg, rb = unpack(line.rightColor)
            tooltip:AddDoubleLine(line.text, line.right, r, g, b, rr, rg, rb)
        else
            local r, g, b = unpack(line.color)
            tooltip:AddLine(line.text, r, g, b, true)
        end
    end
end

local costStripper = CreateFrame("Frame")
costStripper:Hide()
costStripper:SetScript("OnUpdate", function(self)
    self:Hide()
    if GameTooltip:IsShown() and GameTooltip:GetOwner() == self.owner then
        WithoutAdvancementCosts(GameTooltip)
        GameTooltip:Show()
    end
end)

local function ClassicButton(canvas, index)
    local button = canvas.buttons[index]
    if button then
        return button
    end
    button = CreateFrame("Button", nil, canvas)
    button:SetWidth(CLASSIC_NODE)
    button:SetHeight(CLASSIC_NODE)
    button.icon = button:CreateTexture(nil, "BORDER")
    button.icon:SetAllPoints(button)
    button:SetNormalTexture("Interface\\Buttons\\UI-Quickslot2")
    local frame = button:GetNormalTexture()
    frame:ClearAllPoints()
    frame:SetWidth(CLASSIC_NODE * 64 / 37)
    frame:SetHeight(CLASSIC_NODE * 64 / 37)
    frame:SetPoint("CENTER", button, "CENTER", 0, 0)
    button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    button.border = button:CreateTexture(nil, "OVERLAY")
    button.border:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
    button.border:SetBlendMode("ADD")
    button.border:SetWidth(CLASSIC_NODE * 1.7)
    button.border:SetHeight(CLASSIC_NODE * 1.7)
    button.border:SetPoint("CENTER", button, "CENTER", 0, 0)
    button.rankBorder = button:CreateTexture(nil, "OVERLAY")
    button.rankBorder:SetTexture("Interface\\TalentFrame\\TalentFrame-RankBorder")
    button.rankBorder:SetWidth(32)
    button.rankBorder:SetHeight(32)
    button.rankBorder:SetPoint("CENTER", button, "BOTTOMRIGHT", 0, 0)
    button.rank = button:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    button.rank:SetPoint("CENTER", button.rankBorder, "CENTER", 0, 0)
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetTalent(self.tabIndex, self.talentIndex)
        WithoutAdvancementCosts(GameTooltip)
        GameTooltip:Show()
        costStripper.owner = self
        costStripper:Show()
    end)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    button:SetScript("OnClick", function(self) LearnTalent(self.tabIndex, self.talentIndex) end)
    canvas.buttons[index] = button
    return button
end

local function FreeTalentPoints()
    if GetUnspentTalentPoints then
        return GetUnspentTalentPoints() or 0
    end
    return UnitCharacterPoints("player") or 0
end

local function RenderClassicTree(tabIndex, canvas)
    local name, _, spent, background = GetTalentTabInfo(tabIndex)
    canvas.title:SetText(string.upper(name or ""))
    canvas.points:SetText(spent or 0)
    local width, height = canvas:GetWidth(), canvas:GetHeight() - 64
    local art = background and CLASSIC_ART[string.lower(background)]
    if art and canvas.art:SetTexture(ART_FOLDER .. art) then
        local shown = math.min(ART_WIDTH, ART_HEIGHT * width / canvas:GetHeight())
        local left = (ART_WIDTH - shown) / 2
        canvas.art:SetTexCoord(left / ART_SIZE, (left + shown) / ART_SIZE, 0, ART_HEIGHT / ART_SIZE)
        canvas.art:Show()
    else
        canvas.art:Hide()
        art = nil
    end
    for index, piece in ipairs(BACKGROUND_PIECES) do
        local texture = canvas.background[index]
        if background and background ~= "" and not art then
            texture:SetTexture("Interface\\TalentFrame\\" .. background .. piece.suffix)
            texture:ClearAllPoints()
            texture:SetPoint("TOPLEFT", canvas, "TOPLEFT", piece.x * width, -piece.y * canvas:GetHeight())
            texture:SetWidth(piece.w * width)
            texture:SetHeight(piece.h * canvas:GetHeight())
            texture:Show()
        else
            texture:Hide()
        end
    end
    local stepX, stepY = width / CLASSIC_COLUMNS, height / CLASSIC_TIERS
    local centers, indexAt = {}, {}
    local count = GetNumTalents(tabIndex) or 0
    for index = 1, count do
        local _, icon, tier, column, rank, maxRank, _, meets = GetTalentInfo(tabIndex, index)
        local px, py = (column - 0.5) * stepX, height - (tier - 0.5) * stepY
        centers[index] = { px, py }
        indexAt[tier .. ":" .. column] = index
        local button = ClassicButton(canvas, index)
        button.tabIndex, button.talentIndex = tabIndex, index
        button:ClearAllPoints()
        button:SetPoint("CENTER", canvas, "BOTTOMLEFT", px, py)
        button.icon:SetTexture(icon)
        local reachable = meets and (spent or 0) >= (tier - 1) * TALENT_POINTS_PER_TIER
        local available = reachable and rank < maxRank and FreeTalentPoints() > 0
        local dark = rank == 0 and not available
        button.icon:SetDesaturated(dark)
        button.icon:SetVertexColor(dark and 0.6 or 1, dark and 0.6 or 1, dark and 0.6 or 1)
        button.rank:SetText(rank)
        if dark then
            button.rank:Hide()
            button.rankBorder:Hide()
        else
            button.rank:Show()
            button.rankBorder:Show()
        end
        if rank >= maxRank then
            button.rank:SetTextColor(1, 0.82, 0)
        elseif available then
            button.rank:SetTextColor(0.1, 1, 0.1)
        else
            button.rank:SetTextColor(1, 0.82, 0)
        end
        if available then
            button.border:SetVertexColor(0, 1, 0)
            button.border:SetAlpha(0.6)
            button.border:Show()
        else
            button.border:Hide()
        end
        button:Show()
    end
    for index = count + 1, #canvas.buttons do
        canvas.buttons[index]:Hide()
    end
    local lineCount = 0
    for index = 1, count do
        local tier, column = GetTalentPrereqs(tabIndex, index)
        local from = tier and centers[indexAt[tier .. ":" .. column]]
        if from then
            lineCount = lineCount + 1
            DrawLine(Line(canvas, lineCount), canvas, from[1], from[2], centers[index][1], centers[index][2], 24)
        end
    end
    for index = lineCount + 1, #canvas.lines do
        canvas.lines[index]:Hide()
    end
end

local CORNER_REACH = 110
local PORTRAIT_SQUARENESS, PORTRAIT_MIN, PORTRAIT_MAX = 4, 40, 90
local CLASS_CIRCLES = "Interface\\TargetingFrame\\UI-Classes-Circles"

local function CornerTextures(frame, page, found)
    local left, top = window:GetLeft(), window:GetTop()
    local pageLeft, pageTop = page:GetLeft(), page:GetTop()
    for _, region in ipairs({ frame:GetRegions() }) do
        if region:GetObjectType() == "Texture" and region:IsVisible() and region:GetTexture()
            and region:GetLeft() and region:GetRight() < left + CORNER_REACH and region:GetBottom() > top - CORNER_REACH
            and (region:GetLeft() < pageLeft or region:GetTop() > pageTop) then
            table.insert(found, region)
        end
    end
    for _, child in ipairs({ frame:GetChildren() }) do
        if child ~= page and child ~= panel and child ~= ownPage and child:IsVisible()
            and child:GetObjectType() ~= "Button" and child:GetObjectType() ~= "CheckButton" then
            CornerTextures(child, page, found)
        end
    end
    return found
end

local function LiftWindowCorner(page)
    if page.corner or not window:GetLeft() then
        return
    end
    page.corner = CreateFrame("Frame", nil, page)
    page.corner:SetAllPoints(window)
    page.corner:SetFrameLevel(page:GetFrameLevel() + 30)
    page.cornerLevels = {}
    page.cornerCopies = {}
    local function LevelFor(source)
        local owner = source:GetParent()
        local depth = math.max(0, (owner and owner:GetFrameLevel() or 0) - window:GetFrameLevel())
        if not page.cornerLevels[depth] then
            local level = CreateFrame("Frame", nil, page.corner)
            level:SetAllPoints(window)
            level:SetFrameLevel(page.corner:GetFrameLevel() + depth)
            page.cornerLevels[depth] = level
        end
        return page.cornerLevels[depth]
    end
    local left, top = window:GetLeft(), window:GetTop()
    for _, source in ipairs(CornerTextures(window, page, {})) do
        local copy = LevelFor(source):CreateTexture(nil, source:GetDrawLayer())
        copy:SetTexture(source:GetTexture())
        copy:SetTexCoord(source:GetTexCoord())
        copy:SetVertexColor(source:GetVertexColor())
        copy:SetBlendMode(source:GetBlendMode())
        copy:SetWidth(source:GetWidth())
        copy:SetHeight(source:GetHeight())
        copy:SetPoint("TOPLEFT", window, "TOPLEFT", source:GetLeft() - left, source:GetTop() - top)
        copy.subclassSource = string.format("%s %dx%d at %d,%d layer %s level %d %s",
            source:GetName() or "unnamed", source:GetWidth(), source:GetHeight(), source:GetLeft() - left,
            source:GetTop() - top, tostring(source:GetDrawLayer()),
            source:GetParent() and source:GetParent():GetFrameLevel() or -1, tostring(source:GetTexture()))
        table.insert(page.cornerCopies, copy)
        local width, height = source:GetWidth(), source:GetHeight()
        if math.abs(width - height) < PORTRAIT_SQUARENESS and width >= PORTRAIT_MIN and width <= PORTRAIT_MAX
            and (not page.portrait or width > page.portrait:GetWidth()) then
            page.portrait = copy
        end
    end
    if page.portrait then
        page.portraitTexture = page.portrait:GetTexture()
        page.portraitCoords = { page.portrait:GetTexCoord() }
    end
end

local function ShowSubclassPortrait(page)
    local portrait = page and page.portrait
    if not portrait then
        return
    end
    local token = state.subclass and string.upper(string.gsub(state.subclass, " ", ""))
    local coords = token and CLASS_ICON_TCOORDS and CLASS_ICON_TCOORDS[token]
    if coords then
        portrait:SetTexture(CLASS_CIRCLES)
        portrait:SetTexCoord(unpack(coords))
    else
        portrait:SetTexture(page.portraitTexture)
        portrait:SetTexCoord(unpack(page.portraitCoords))
    end
end

local portraitWatch = CreateFrame("Frame")
portraitWatch:SetScript("OnUpdate", function(self, elapsed)
    self.wait = (self.wait or 0) + elapsed
    if self.wait < 0.5 then
        return
    end
    self.wait = 0
    if panel and panel:IsShown() then
        ShowSubclassPortrait(panel)
    end
end)

local function RenderClassic()
    if not ownPage or not ownPage:IsShown() then
        return
    end
    LiftWindowCorner(ownPage)
    ownPage.points:SetText(string.format("Talent points left: |cffffffff%d|r", FreeTalentPoints()))
    local tabs = GetNumTalentTabs() or 0
    local inner = ownPage:GetWidth() - 20
    for index = 1, tabs do
        local canvas = ClassicCanvas(index)
        canvas:ClearAllPoints()
        canvas:SetPoint("TOPLEFT", ownPage, "TOPLEFT", 10 + (index - 1) * inner / tabs, -8)
        canvas:SetWidth(inner / tabs - 10)
        canvas:SetHeight(ownPage:GetHeight() - 36)
        canvas:Show()
        RenderClassicTree(index, canvas)
    end
    for index = tabs + 1, #classicFrames do
        classicFrames[index]:Hide()
    end
end

local blizzardToggleTalentFrame

local function ShowPage(own)
    if own then
        panel:Hide()
        ownPage:Show()
    else
        ownPage:Hide()
        panel:Show()
    end
    PanelTemplates_SetTab(book, own and 1 or 2)
end

local function BookTab(index, text, own)
    local button = CreateFrame("Button", "Ascension_SubclassBookTab" .. index, book, "CharacterFrameTabButtonTemplate")
    button:SetID(index)
    button:SetText(text)
    PanelTemplates_TabResize(button, 0)
    button:SetScript("OnClick", function() ShowPage(own) end)
    return button
end

local function CreateBook()
    book = CreateFrame("Frame", "Ascension_SubclassBook", UIParent)
    book:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background-Dark",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 },
    })
    book:SetWidth(960)
    book:SetHeight(640)
    book:SetPoint("CENTER", UIParent, "CENTER", 0, 20)
    book:EnableMouse(true)
    book:SetMovable(true)
    book:RegisterForDrag("LeftButton")
    book:SetScript("OnDragStart", book.StartMoving)
    book:SetScript("OnDragStop", book.StopMovingOrSizing)
    book:SetFrameStrata("HIGH")
    book:Hide()
    tinsert(UISpecialFrames, "Ascension_SubclassBook")
    local close = CreateFrame("Button", nil, book, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", book, "TOPRIGHT", -4, -4)
    book.title = book:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    book.title:SetPoint("TOP", book, "TOP", 0, -16)
    book.title:SetText("Character Advancement")

    ownPage = CreateFrame("Frame", nil, book)
    ownPage:SetPoint("TOPLEFT", book, "TOPLEFT", 12, -36)
    ownPage:SetPoint("BOTTOMRIGHT", book, "BOTTOMRIGHT", -12, 12)
    ownPage.points = ownPage:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    ownPage.points:SetPoint("BOTTOM", ownPage, "BOTTOM", 0, 8)
    ownPage:SetScript("OnShow", RenderClassic)

    window = book
    CreatePanel(book)
    panel:SetPoint("TOPLEFT", book, "TOPLEFT", 12, -36)
    panel:SetPoint("BOTTOMRIGHT", book, "BOTTOMRIGHT", -12, 12)
    panel:SetFrameLevel(book:GetFrameLevel() + 5)

    local own = BookTab(1, "Character Advancement", true)
    own:SetPoint("TOPLEFT", book, "BOTTOMLEFT", 12, 8)
    tab = BookTab(2, Naming().tab, false)
    tab:SetPoint("LEFT", own, "RIGHT", -15, 0)
    PanelTemplates_SetNumTabs(book, 2)
    book:SetScript("OnShow", function() ShowPage(true) end)

    local talentEvents = CreateFrame("Frame", nil, book)
    talentEvents:RegisterEvent("CHARACTER_POINTS_CHANGED")
    talentEvents:RegisterEvent("PLAYER_TALENT_UPDATE")
    talentEvents:SetScript("OnEvent", RenderClassic)
end

local function ToggleBook()
    if not book then
        CreateBook()
    end
    if book:IsShown() then
        book:Hide()
    else
        book:Show()
    end
end

local function SetOwnShown(shown)
    if not ownPage then
        return
    end
    if shown and not ownPage:IsShown() then
        ownPage:Show()
    elseif not shown and ownPage:IsShown() then
        ownPage:Hide()
    end
end

local function AdvancementTabActive()
    if advancementTab and advancementTab.GetChecked then
        return advancementTab:GetChecked() and true or false
    end
    return window and window.subclassOwnActive
end

local function AdoptOwnPage()
    if not window or window == book or (ownPage and ownPage:GetParent() == window) then
        return
    end
    for _, frame in ipairs(classicFrames) do
        frame:Hide()
    end
    classicFrames = {}
    ownPage = CreateFrame("Frame", "Ascension_SubclassOwnPage", window)
    ownPage:SetPoint("TOPLEFT", window, "TOPLEFT", 8, -24)
    ownPage:SetPoint("BOTTOMRIGHT", window, "BOTTOMRIGHT", -8, 8)
    ownPage:SetFrameStrata(STRATA_ABOVE[window:GetFrameStrata()] or "HIGH")
    local cover = ownPage:CreateTexture(nil, "BACKGROUND", nil, -8)
    cover:SetAllPoints(ownPage)
    cover:SetTexture(0.03, 0.03, 0.04, 1)
    ownPage:EnableMouse(true)
    ownPage.points = ownPage:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    ownPage.points:SetPoint("BOTTOM", ownPage, "BOTTOM", 0, 10)
    ownPage:SetScript("OnShow", RenderClassic)
    ownPage:Hide()
    local talentEvents = CreateFrame("Frame", nil, ownPage)
    talentEvents:RegisterEvent("CHARACTER_POINTS_CHANGED")
    talentEvents:RegisterEvent("PLAYER_TALENT_UPDATE")
    talentEvents:SetScript("OnEvent", RenderClassic)
    advancementTab:HookScript("OnClick", function() window.subclassOwnActive = true end)
    window:HookScript("OnHide", function() window.subclassOwnActive = false end)
    local watcher = CreateFrame("Frame", nil, window)
    watcher:SetScript("OnUpdate", function(self, elapsed)
        self.wait = (self.wait or 0) + elapsed
        if self.wait < 0.1 then
            return
        end
        self.wait = 0
        SetOwnShown(IsStandardClass() and window:IsShown() and AdvancementTabActive() and not panel:IsShown())
    end)
end

local function Attach()
    if not AttachToWindow() then
        return false
    end
    if not panel.cornerHooked then
        panel.cornerHooked = true
        panel:HookScript("OnShow", function()
            LiftWindowCorner(panel)
            ShowSubclassPortrait(panel)
        end)
    end
    if IsStandardClass() then
        AdoptOwnPage()
    end
    return true
end

local function OpenOwnPage()
    if not (window and advancementTab and window ~= book) and not Attach() then
        ToggleBook()
        return
    end
    if window:IsShown() and ownPage and ownPage:IsShown() then
        HideUIPanel(window)
        if window:IsShown() then
            window:Hide()
        end
        return
    end
    if not window:IsShown() then
        ShowUIPanel(window)
        if not window:IsShown() then
            window:Show()
        end
    end
    if panel then
        panel:Hide()
    end
    advancementTab:Click()
    window.subclassOwnActive = true
end

local function Toggle()
    if IsStandardClass() then
        OpenOwnPage()
        return
    end
    if not panel then
        if not Attach() then
            CreatePanel(nil)
        end
    end
    if window and not window:IsShown() then
        window:Show()
    end
    if panel:IsShown() then
        panel:Hide()
    else
        panel:Show()
    end
end

local function OwnTalentToggle(...)
    if IsStandardClass() then
        OpenOwnPage()
    elseif blizzardToggleTalentFrame then
        blizzardToggleTalentFrame(...)
    end
end

local classicTalentFrames = { "PlayerTalentFrame", "TalentFrame" }
local talentRedirect = CreateFrame("Frame")
talentRedirect:SetScript("OnUpdate", function(self, elapsed)
    self.wait = (self.wait or 0) + elapsed
    if self.wait < 0.1 then
        return
    end
    self.wait = 0
    if ToggleTalentFrame ~= OwnTalentToggle then
        blizzardToggleTalentFrame = ToggleTalentFrame
        ToggleTalentFrame = OwnTalentToggle
    end
    if not IsStandardClass() then
        return
    end
    for _, name in ipairs(classicTalentFrames) do
        local frame = _G[name]
        if frame and frame:IsShown() and not frame.subclassAllowed then
            HideUIPanel(frame)
            if frame:IsShown() then
                frame:Hide()
            end
            OpenOwnPage()
        end
    end
end)

local dressingTwin = false
local twinRedressPending = false

local function DressesCyclops()
    return (UnitRace("player")) == "Two-Headed Ogre"
end
local twinButton

local TWIN_SLOT_OF_CATEGORY = { 1, 2, 10, 4, 11, 3, 8, 9, 5, 6, 7, 14, 12, 13 }

local function SendTwinLook()
    local looks = {}
    for category = 1, 14 do
        local appearance = C_Appearance.GetPendingAppearance(category)
        if appearance == 0 then
            looks[#looks + 1] = category .. ":0"
        elseif appearance then
            local _, itemId = C_Appearance.GetAppearanceDisplayInfo(appearance)
            if itemId and itemId > 0 then
                looks[#looks + 1] = category .. ":" .. itemId
            end
        end
    end
    if #looks == 0 then
        return false
    end
    Send("TWINLOOK " .. table.concat(looks, " "))
    C_Appearance.ClearPendingAppearances()
    return true
end

local function HookWardrobeApply()
    if not C_Appearance or not C_Appearance.ApplyPendingAppearances or C_Appearance.SubclassTwinHooked then
        return
    end
    local apply = C_Appearance.ApplyPendingAppearances
    C_Appearance.ApplyPendingAppearances = function(...)
        if dressingTwin then
            return SendTwinLook()
        end
        return apply(...)
    end
    C_Appearance.SubclassTwinHooked = true
end

local function FindButton(text)
    local frame = EnumerateFrames()
    while frame do
        if frame.GetObjectType and frame:GetObjectType() == "Button" and TextOf(frame) == text then
            return frame
        end
        frame = EnumerateFrames(frame)
    end
end

local function IsInside(frame, ancestor)
    while frame do
        if frame == ancestor then
            return true
        end
        frame = frame:GetParent()
    end
    return false
end

local applyButtons = {}

local function HookApplyButton(wardrobe)
    local frame = EnumerateFrames()
    while frame do
        if frame.GetObjectType and frame:GetObjectType() == "Button" and TextOf(frame) == "Apply"
            and IsInside(frame, wardrobe) and not frame.subclassTwinHooked then
            applyButtons[#applyButtons + 1] = frame
            local original = frame:GetScript("OnClick")
            frame:SetScript("OnClick", function(...)
                if dressingTwin then
                    SendTwinLook()
                elseif original then
                    original(...)
                end
            end)
            frame.subclassTwinHooked = true
        end
        frame = EnumerateFrames(frame)
    end
end

local wardrobeFrame

local function FindPreviewModels(root)
    local largest, largestArea = nil, 0
    local frame = EnumerateFrames()
    while frame do
        if frame.GetObjectType and frame:GetObjectType() == "DressUpModel" and IsInside(frame, root)
            and frame:IsVisible() then
            local area = (frame:GetWidth() or 0) * (frame:GetHeight() or 0)
            if area > largestArea then
                largest, largestArea = frame, area
            end
        end
        frame = EnumerateFrames(frame)
    end
    return largest and { largest } or {}
end

local function PendingTwinItems()
    local items, stripped = {}, {}
    if not (C_Appearance and C_Appearance.GetPendingAppearance and C_Appearance.GetAppearanceDisplayInfo) then
        return items, stripped
    end
    for category = 1, 14 do
        local appearance = C_Appearance.GetPendingAppearance(category)
        if appearance == 0 then
            stripped[TWIN_SLOT_OF_CATEGORY[category]] = true
        elseif appearance then
            local _, itemId = C_Appearance.GetAppearanceDisplayInfo(appearance)
            if itemId and itemId > 0 then
                items[#items + 1] = itemId
            end
        end
    end
    return items, stripped
end

local function DressTwin(model)
    local pending, stripped = PendingTwinItems()
    model.subclassDressing = true
    if DressesCyclops() then
        model:Dress()
    else
        model:Undress()
    end
    for slot, itemId in ipairs(twinOutfit) do
        if itemId > 0 and not stripped[slot] then
            model:TryOn(itemId)
        end
    end
    for _, itemId in ipairs(pending) do
        model:TryOn(itemId)
    end
    model.subclassDressing = false
    if DressesCyclops() then
        local pendingHead = C_Appearance and C_Appearance.GetPendingAppearance and C_Appearance.GetPendingAppearance(1)
        local head
        if pendingHead and pendingHead ~= 0 then
            local _, itemId = C_Appearance.GetAppearanceDisplayInfo(pendingHead)
            head = itemId
        elseif pendingHead ~= 0 and not stripped[1] then
            head = twinOutfit[1]
        end
        ShowCyclopsHelmetOn(model, head and CyclopsPreviewOf(head) or (not head and pendingHead ~= 0 and cyclops.model)
            or nil)
    end
end

local function WatchPreviewModel(model)
    if model.subclassTwinWatched then
        return
    end
    local function Redress()
        if dressingTwin and not model.subclassDressing then
            twinRedressPending = true
        end
    end
    for _, method in ipairs({ "Dress", "SetUnit", "RefreshUnit" }) do
        if type(model[method]) == "function" then
            hooksecurefunc(model, method, Redress)
        end
    end
    model:HookScript("OnShow", Redress)
    model.subclassTwinWatched = true
end

ShowTwinPreview = function()
    if not (dressingTwin and wardrobeFrame) then
        return
    end
    for _, model in ipairs(FindPreviewModels(wardrobeFrame)) do
        WatchPreviewModel(model)
        DressTwin(model)
    end
end

local function ShowOwnPreview()
    if not wardrobeFrame then
        return
    end
    for _, model in ipairs(FindPreviewModels(wardrobeFrame)) do
        model:Dress()
        if DressesCyclops() then
            ShowCyclopsHelmetOn(model, cyclops.model)
        end
    end
end

local function OwnSlotWarnings(root)
    local warnings = {}
    local frame = EnumerateFrames()
    while frame do
        if IsInside(frame, root) then
            if frame.GetObjectType and frame:GetObjectType() == "Button" and TextOf(frame) == "Clear invalid slots" then
                warnings[#warnings + 1] = frame
            end
            for _, region in ipairs({ frame:GetRegions() }) do
                if region.GetObjectType and region:GetObjectType() == "FontString" then
                    local text = region:GetText()
                    if text and text:find("^Invalid appearance selection") then
                        warnings[#warnings + 1] = region
                    end
                end
            end
        end
        frame = EnumerateFrames(frame)
    end
    return warnings
end

local twinApplyKeeper = CreateFrame("Frame")
twinApplyKeeper:Hide()
twinApplyKeeper:SetScript("OnUpdate", function(self, elapsed)
    if twinRedressPending then
        twinRedressPending = false
        ShowTwinPreview()
    end
    for _, button in ipairs(applyButtons) do
        if button:IsVisible() and not button:IsEnabled() then
            button:Enable()
        end
    end
    self.wait = (self.wait or 0) + elapsed
    if self.wait < 0.1 then
        return
    end
    self.wait = 0
    self.scan = (self.scan or 0) + 1
    if self.scan % 10 == 1 and wardrobeFrame then
        self.warnings = OwnSlotWarnings(wardrobeFrame)
        for _, model in ipairs(FindPreviewModels(wardrobeFrame)) do
            if not model.subclassTwinWatched then
                WatchPreviewModel(model)
                twinRedressPending = true
            end
        end
    end
    for _, warning in ipairs(self.warnings or {}) do
        if warning:IsShown() then
            warning:Hide()
        end
    end
end)

local TWIN_SLOT_NAMES = { "Head", "Shoulders", "Shirt", "Chest", "Waist", "Legs", "Feet", "Wrists", "Hands",
    "Back", "Tabard", "Main hand", "Off hand", "Ranged" }
local twinUndressButton
local twinUndressMenu

local function CategoryOfTwinSlot(slot)
    for category, twinSlot in ipairs(TWIN_SLOT_OF_CATEGORY) do
        if twinSlot == slot then
            return category
        end
    end
end

local function TakeOffTwin(slots)
    local looks = {}
    for _, slot in ipairs(slots) do
        looks[#looks + 1] = CategoryOfTwinSlot(slot) .. ":0"
    end
    if #looks > 0 then
        Send("TWINLOOK " .. table.concat(looks, " "))
    end
end

local function ShowCyclopsMenu(anchor)
    local entries = {
        { text = "Your left head", isTitle = true, notCheckable = true },
        { text = "Bare head", notCheckable = true, func = function() TakeOffTwin({ 1 }) end },
        { text = "Same helmet as the right head", notCheckable = true, func = function() Send("CYCLOPSECHO") end },
    }
    twinUndressMenu = twinUndressMenu or CreateFrame("Frame", "Ascension_SubclassTwinUndressMenu", UIParent,
        "UIDropDownMenuTemplate")
    EasyMenu(entries, twinUndressMenu, anchor, 0, 0, "MENU")
end

local function ShowUndressMenu(anchor)
    if DressesCyclops() then
        return ShowCyclopsMenu(anchor)
    end
    local entries = { { text = "Take off your twin's", isTitle = true, notCheckable = true } }
    local worn = {}
    for slot, name in ipairs(TWIN_SLOT_NAMES) do
        if (twinOutfit[slot] or 0) > 0 then
            worn[#worn + 1] = slot
            entries[#entries + 1] = { text = name, notCheckable = true,
                func = function() TakeOffTwin({ slot }) end }
        end
    end
    if #worn == 0 then
        entries[#entries + 1] = { text = "He wears nothing you can take off", disabled = true, notCheckable = true }
    else
        entries[#entries + 1] = { text = "Everything", notCheckable = true, func = function() TakeOffTwin(worn) end }
    end
    twinUndressMenu = twinUndressMenu or CreateFrame("Frame", "Ascension_SubclassTwinUndressMenu", UIParent,
        "UIDropDownMenuTemplate")
    EasyMenu(entries, twinUndressMenu, anchor, 0, 0, "MENU")
end

local function ShowDressing()
    local other = DressesCyclops() and "Dressing: Left head" or "Dressing: Your twin"
    local own = DressesCyclops() and "Dressing: Right head" or "Dressing: You"
    twinButton:SetText(dressingTwin and other or own)
    if twinUndressButton then
        if dressingTwin then
            twinUndressButton:Show()
        else
            twinUndressButton:Hide()
        end
    end
    if dressingTwin then
        twinApplyKeeper.scan = 0
        twinApplyKeeper:Show()
    else
        twinApplyKeeper:Hide()
    end
end

local function AttachTwinButton()
    if twinButton then
        return true
    end
    local _, race = UnitRace("player")
    if race ~= "ThinHuman" and not DressesCyclops() then
        return true
    end
    local anchor = FindButton("Disable Transmog") or FindButton("Disable Spell Visuals")
    if not anchor then
        return false
    end
    HookWardrobeApply()
    local wardrobe = anchor
    while wardrobe:GetParent() and wardrobe:GetParent() ~= UIParent do
        wardrobe = wardrobe:GetParent()
    end
    wardrobeFrame = wardrobe
    HookApplyButton(wardrobe)
    wardrobe:HookScript("OnShow", function() HookApplyButton(wardrobe) end)
    twinButton = CreateFrame("Button", "Ascension_SubclassTwinButton", anchor:GetParent(), "UIPanelButtonTemplate")
    twinButton:SetWidth(150)
    twinButton:SetHeight(anchor:GetHeight())
    twinButton:SetPoint("BOTTOMLEFT", anchor, "TOPLEFT", 0, 4)
    twinButton:SetFrameStrata(anchor:GetFrameStrata())
    twinButton:SetFrameLevel(anchor:GetFrameLevel() + 20)
    twinButton:SetScript("OnClick", function()
        dressingTwin = not dressingTwin
        if C_Appearance and C_Appearance.ClearPendingAppearances then
            C_Appearance.ClearPendingAppearances()
        end
        ShowDressing()
        if dressingTwin then
            Send("TWINWEARING")
        else
            ShowOwnPreview()
        end
        local other = DressesCyclops() and "|cffffd100Wardrobe:|r Apply now puts the helmet on your left head."
            or "|cffffd100Wardrobe:|r Apply now dresses your twin."
        DEFAULT_CHAT_FRAME:AddMessage(dressingTwin and other or "|cffffd100Wardrobe:|r Apply now dresses you.")
    end)
    twinButton:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_BOTTOM")
        GameTooltip:SetText("Who the Wardrobe dresses", 1, 1, 1)
        GameTooltip:AddLine(DressesCyclops()
            and "Switch to your left head, pick a helmet and apply it as usual. She wears it without needing "
            .. "the item. The preview shows it on your right head."
            or "Switch to your twin, pick his look and apply it as usual. "
            .. "He wears it without needing the items.", nil, nil, nil, true)
        GameTooltip:Show()
    end)
    twinButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
    twinUndressButton = CreateFrame("Button", "Ascension_SubclassTwinUndressButton", anchor:GetParent(),
        "UIPanelButtonTemplate")
    twinUndressButton:SetWidth(90)
    twinUndressButton:SetHeight(anchor:GetHeight())
    twinUndressButton:SetPoint("LEFT", twinButton, "RIGHT", 4, 0)
    twinUndressButton:SetFrameStrata(anchor:GetFrameStrata())
    twinUndressButton:SetFrameLevel(anchor:GetFrameLevel() + 20)
    twinUndressButton:SetText(DressesCyclops() and "Helmet" or "Take off")
    twinUndressButton:SetScript("OnClick", function(self) ShowUndressMenu(self) end)
    anchor:GetParent():HookScript("OnHide", function()
        dressingTwin = false
        ShowDressing()
    end)
    ShowDressing()
    return true
end

local twinWatcher = CreateFrame("Frame")
twinWatcher:SetScript("OnUpdate", function(self, elapsed)
    self.wait = (self.wait or 0) + elapsed
    if self.wait < 2 then
        return
    end
    self.wait = 0
    local collections = _G.Collections
    if not (collections and collections.IsShown and collections:IsShown()) then
        return
    end
    if AttachTwinButton() then
        self:SetScript("OnUpdate", nil)
    end
end)

local function DescribeRegion(region)
    local entry = { kind = region:GetObjectType(), name = region:GetName(), shown = region:IsShown(),
        width = region:GetWidth(), height = region:GetHeight(), points = {} }
    for index = 1, region:GetNumPoints() do
        local point, relative, relativePoint, x, y = region:GetPoint(index)
        entry.points[index] = table.concat({ point, relative and (relative:GetName() or tostring(relative)) or "nil",
            relativePoint, x, y }, " ")
    end
    if entry.kind == "Texture" then
        entry.texture = region:GetTexture()
        entry.atlas = region.GetAtlas and region:GetAtlas()
        entry.layer = region:GetDrawLayer()
        entry.coords = { region:GetTexCoord() }
        entry.color = { region:GetVertexColor() }
        entry.blend = region:GetBlendMode()
    elseif entry.kind == "FontString" then
        entry.text = region:GetText()
        entry.font = { region:GetFont() }
    end
    return entry
end

local function DescribeFrame(frame, depth)
    local entry = DescribeRegion(frame)
    entry.level = frame:GetFrameLevel()
    entry.strata = frame:GetFrameStrata()
    if frame.GetBackdrop then
        entry.backdrop = frame:GetBackdrop()
    end
    for _, kind in ipairs({ "Normal", "Pushed", "Highlight", "Disabled", "Checked" }) do
        local getter = frame["Get" .. kind .. "Texture"]
        local texture = getter and getter(frame)
        if texture then
            entry[kind] = DescribeRegion(texture)
        end
    end
    entry.regions = {}
    for _, region in ipairs({ frame:GetRegions() }) do
        entry.regions[#entry.regions + 1] = DescribeRegion(region)
    end
    entry.children = {}
    if depth < 4 then
        for _, child in ipairs({ frame:GetChildren() }) do
            entry.children[#entry.children + 1] = DescribeFrame(child, depth + 1)
        end
    end
    return entry
end

local function DumpTabs()
    local tabs = FindAdvancementTabs()
    if not tabs then
        DEFAULT_CHAT_FRAME:AddMessage("Ascension Subclass: open the Character Advancement window first.")
        return
    end
    Ascension_SubclassDump = { tabs = {}, trees = {} }
    for index, other in ipairs(tabs) do
        Ascension_SubclassDump.tabs[index] = DescribeFrame(other, 0)
    end
    for index, tree in ipairs(state.trees) do
        local entries = {}
        for _, id in ipairs(tree.ids or {}) do
            local entry = Entry(id)
            if type(entry) == "table" then
                local plain = { id = id }
                for key, value in pairs(entry) do
                    local kind = type(value)
                    if kind == "string" or kind == "number" or kind == "boolean" then
                        plain[key] = value
                    elseif kind == "table" then
                        local list = {}
                        for _, item in ipairs(value) do
                            if type(item) ~= "table" and type(item) ~= "function" and type(item) ~= "userdata" then
                                list[#list + 1] = item
                            end
                        end
                        plain[key] = list
                    end
                end
                entries[#entries + 1] = plain
            end
        end
        Ascension_SubclassDump.trees[index] = { title = tree.title, entries = entries }
    end
    Ascension_SubclassDump.api = {}
    for name, value in pairs(C_CharacterAdvancement or {}) do
        Ascension_SubclassDump.api[#Ascension_SubclassDump.api + 1] = name .. " " .. type(value)
    end
    table.sort(Ascension_SubclassDump.api)
    DEFAULT_CHAT_FRAME:AddMessage("Ascension Subclass: tab layout saved. /reload to write it to disk.")
end

local SHARE_ICONS = {
    "Achievement_Reputation_01", "INV_Misc_GroupNeedMore", "INV_Misc_GroupLooking", "Spell_Shadow_DeathPact",
    "Spell_Shadow_SoulLeech_3", "Spell_Holy_SealOfSacrifice", "Spell_Shadow_UnholyStrength", "Spell_Nature_Strength",
    "Spell_Shadow_LifeDrain02", "Spell_Holy_PrayerOfFortitude", "Spell_Holy_PrayerOfSpirit", "Spell_Nature_Reincarnation",
    "Ability_Rogue_FeignDeath", "Spell_Magic_LesserInvisibilty", "Achievement_Character_Human_Male",
    "Spell_Shadow_CurseOfMannoroth", "Spell_Holy_SpiritualGuidence", "Ability_Warrior_Rampage",
    "Spell_Shadow_Charm", "Spell_Holy_Heal02", "Ability_Hunter_Pet_Bear", "Spell_Misc_Drink", "INV_Drink_05",
    "Spell_Shadow_BrainWash",
}

local iconSheet

local function ShowIcons()
    if iconSheet then
        if iconSheet:IsShown() then
            iconSheet:Hide()
        else
            iconSheet:Show()
        end
        return
    end
    iconSheet = CreateFrame("Frame", "Ascension_SubclassIcons", UIParent)
    iconSheet:SetWidth(6 * 120 + 24)
    iconSheet:SetHeight(math.ceil(#SHARE_ICONS / 6) * 92 + 48)
    iconSheet:SetPoint("CENTER")
    iconSheet:SetFrameStrata("DIALOG")
    iconSheet:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 } })
    iconSheet:EnableMouse(true)
    iconSheet:SetMovable(true)
    iconSheet:RegisterForDrag("LeftButton")
    iconSheet:SetScript("OnDragStart", iconSheet.StartMoving)
    iconSheet:SetScript("OnDragStop", iconSheet.StopMovingOrSizing)
    local title = iconSheet:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOP", 0, -16)
    title:SetText("Brotherly Share - pick an icon")
    local close = CreateFrame("Button", nil, iconSheet, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", -4, -4)
    for index, name in ipairs(SHARE_ICONS) do
        local column, row = (index - 1) % 6, math.floor((index - 1) / 6)
        local icon = iconSheet:CreateTexture(nil, "ARTWORK")
        icon:SetWidth(48)
        icon:SetHeight(48)
        icon:SetPoint("TOPLEFT", 12 + column * 120 + 36, -40 - row * 92)
        icon:SetTexture("Interface\\Icons\\" .. name)
        local label = iconSheet:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        label:SetWidth(116)
        label:SetPoint("TOP", icon, "BOTTOM", 0, -2)
        label:SetText(index .. ". " .. name)
    end
end

SLASH_ASCENSIONSUBCLASS1 = "/subclass"
SlashCmdList.ASCENSIONSUBCLASS = function(message)
    if message == "dump" then
        DumpTabs()
    elseif message == "watch" then
        Ascension_SubclassDump = Ascension_SubclassDump or {}
        Ascension_SubclassDump.watching = true
        Ascension_SubclassDump.hovered = {}
        DEFAULT_CHAT_FRAME:AddMessage("Ascension Subclass: hover the nodes you want me to see, then /reload.")
    elseif message == "state" then
        DEFAULT_CHAT_FRAME:AddMessage(string.format(
            "Subclass: %s, relic allowed %s, relic %d, relic slot %s, window %s, ranged slot %s, level line %s, "
            .. "corner portrait %s, micro portrait %s",
            tostring(state.subclass), tostring(relicState.eligible), relicState.entry or 0,
            relicButton and (relicButton:IsShown() and "shown" or "hidden") or "missing",
            sheet and (sheet:GetName() or "unnamed") or "not found",
            sheet and (ShownRangedSlot(sheet) and (ShownRangedSlot(sheet):GetName() or "unnamed") or "missing") or "-",
            sheet and tostring(LevelLineIn(sheet) and LevelLineIn(sheet):GetText()) or "-",
            sheet and (CornerPortrait(sheet) and "found" or "missing") or "-",
            MicroButtonPortrait and "found" or "missing"))
    elseif message == "corner" then
        for _, page in ipairs({ panel, ownPage }) do
            for _, copy in ipairs(page and page.cornerCopies or {}) do
                DEFAULT_CHAT_FRAME:AddMessage((copy == page.portrait and "PORTRAIT " or "") .. copy.subclassSource)
            end
        end
    elseif message == "casts" then
        Ascension_SubclassDump = Ascension_SubclassDump or {}
        Ascension_SubclassDump.debugCasts = not Ascension_SubclassDump.debugCasts
        DEFAULT_CHAT_FRAME:AddMessage("Subclass: failed casts of the other half are "
            .. (Ascension_SubclassDump.debugCasts and "shown" or "hidden") .. ".")
    elseif message == "stancebar reset" then
        Ascension_SubclassDump = Ascension_SubclassDump or {}
        Ascension_SubclassDump.stanceBarPoint = nil
        Stances.placed = false
        Stances.BuildStanceBar()
    elseif message:find("^helm") then
        local x, y, z = message:match("^helm%s+(%S+)%s+(%S+)%s+(%S+)$")
        Ascension_SubclassDump = Ascension_SubclassDump or {}
        if x then
            Ascension_SubclassDump.cyclopsNudge = { tonumber(x) or 0, tonumber(y) or 0, tonumber(z) or 0 }
        elseif message == "helm reset" then
            Ascension_SubclassDump.cyclopsNudge = nil
        end
        local nx, ny, nz = CyclopsNudge()
        DEFAULT_CHAT_FRAME:AddMessage(string.format("Subclass: cyclops helmet overlay nudged %.2f forward, %.2f right, "
            .. "%.2f up (/subclass helm <forward> <right> <up>, /subclass helm reset)", nx, ny, nz))
    elseif message:find("^cam") then
        Ascension_SubclassDump = Ascension_SubclassDump or {}
        Ascension_SubclassDump.cyclopsCamera = tonumber(message:match("^cam%s+(%d+)")) or nil
        DEFAULT_CHAT_FRAME:AddMessage("Subclass: cyclops helmet overlay uses camera " .. CyclopsCamera()
            .. " (/subclass cam <0-1>), model " .. tostring(cyclops.model))
    elseif message:find("^sheet") then
        local zoom, right, up = message:match("^sheet%s+(%S+)%s+(%S+)%s+(%S+)$")
        Ascension_SubclassDump = Ascension_SubclassDump or {}
        if zoom then
            Ascension_SubclassDump.sheetFit = { zoom = tonumber(zoom) or 0, right = tonumber(right) or 0,
                up = tonumber(up) or 0 }
        elseif message == "sheet reset" then
            Ascension_SubclassDump.sheetFit = nil
        end
        local fit = SheetFit()
        DEFAULT_CHAT_FRAME:AddMessage(string.format("Subclass: character window model zoom %.2f, right %.2f, up %.2f "
            .. "(/subclass sheet <zoom> <right> <up>, /subclass sheet reset)", fit.zoom, fit.right, fit.up))
    elseif message == "icons" then
        ShowIcons()
    elseif message == "applytwin" then
        if not SendTwinLook() then
            DEFAULT_CHAT_FRAME:AddMessage("|cffffd100Wardrobe:|r pick something for your twin first.")
        end
    else
        Toggle()
    end
end

local KNOWN_TOOLTIP_FAULT = "GameTooltipMods.lua:109"

local function QuietKnownTooltipFault()
    local report = geterrorhandler()
    seterrorhandler(function(message, ...)
        if type(message) == "string" and string.find(message, KNOWN_TOOLTIP_FAULT, 1, true) then
            return
        end
        return report(message, ...)
    end)
end

local function PlainFields(object)
    local plain = {}
    for key, value in pairs(object) do
        local kind = type(value)
        if type(key) == "string" and (kind == "string" or kind == "number" or kind == "boolean") then
            plain[key] = value
        elseif type(key) == "string" and kind == "table" and not value.GetObjectType then
            local inner = {}
            for innerKey, innerValue in pairs(value) do
                local innerKind = type(innerValue)
                if innerKind == "string" or innerKind == "number" or innerKind == "boolean" then
                    inner[tostring(innerKey)] = innerValue
                end
            end
            plain[key] = inner
        end
    end
    return plain
end

local function RecordHoveredNode()
    local owner = GameTooltip:GetOwner()
    if not owner or owner == UIParent or not owner.GetName then
        return
    end
    Ascension_SubclassDump = Ascension_SubclassDump or {}
    Ascension_SubclassDump.hovered = Ascension_SubclassDump.hovered or {}
    local chain = {}
    local frame = owner
    while frame and #chain < 6 do
        chain[#chain + 1] = (frame.GetName and frame:GetName()) or (frame.GetObjectType and frame:GetObjectType()) or "?"
        frame = frame.GetParent and frame:GetParent()
    end
    local record = { chain = table.concat(chain, " < "), fields = PlainFields(owner),
        tooltip = GameTooltipTextLeft1 and GameTooltipTextLeft1:GetText() }
    local list = Ascension_SubclassDump.hovered
    list[#list + 1] = record
    if #list > 12 then
        table.remove(list, 1)
    end
end

GameTooltip:HookScript("OnShow", function()
    if Ascension_SubclassDump and Ascension_SubclassDump.watching then
        RecordHoveredNode()
    end
end)

local tries = 0
local quietened = false
local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function(_, event, prefix, message, _, sender)
    if event == "CHAT_MSG_ADDON" then
        if prefix == PREFIX and sender == UnitName("player") then
            Receive(message)
        end
    elseif event == "PLAYER_ENTERING_WORLD" then
        if not quietened then
            quietened = true
            QuietKnownTooltipFault()
        end
        Send("SYNC")
        tries = 0
        events:SetScript("OnUpdate", function(self, elapsed)
            self.wait = (self.wait or 0) + elapsed
            if self.wait < 5 then
                return
            end
            self.wait = 0
            tries = tries + 1
            if (panel and tab) or Attach() or tries >= 24 then
                self:SetScript("OnUpdate", nil)
            end
        end)
    end
end)
