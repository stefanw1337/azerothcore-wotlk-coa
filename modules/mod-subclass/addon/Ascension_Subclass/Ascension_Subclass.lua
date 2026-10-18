local PREFIX = "SUBCLASS"
local NODE = 34
local LINE_TEXTURE = "Interface\\TaxiFrame\\UI-Taxi-Line"

local state = { eligible = false, insights = 0, trees = {}, ranks = {}, classPoints = 0, specPoints = 0,
    resources = {}, powers = {} }
local POWER_NAMES = { [0] = "Mana", [1] = "Rage", [2] = "Focus", [3] = "Energy", [6] = "Runic Power" }
local POWER_COLORS = { [0] = { 0, 0, 1 }, [1] = { 1, 0, 0 }, [2] = { 1, 0.5, 0.25 }, [3] = { 1, 1, 0 },
    [6] = { 0, 0.82, 1 } }
local resourceFrame
local UpdateResourceFrame
local incoming
local pending = false
local panel, tab, window
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
        GameTooltip:AddLine("Locked: spend more points in this tree or learn the talents above it.", 1, 0.3, 0.3, true)
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

local function Position(entry)
    local x, y = entry.PositionX or 0, entry.PositionY or 0
    if x == 0 and y == 0 then
        x, y = entry.Column or 0, entry.Row or 0
    end
    return x, y
end

local function RenderTree(tree, canvas)
    canvas.title:SetText(tree.title or "")
    local ae, te = SpentIn(tree)
    canvas.points:SetText(string.format("%d point(s) spent", ae + te))
    local minX, maxX, minY, maxY
    for _, node in ipairs(tree.nodes) do
        local x, y = Position(node.entry)
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
        local x, y = Position(node.entry)
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
        for _, id in ipairs(tree.ids) do
            local entry = Entry(id)
            if entry and (entry.Type == "Talent" or entry.Type == "TalentAbility") then
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

local function Receive(message)
    local command, rest = string.match(message, "^(%S+)%s?(.*)$")
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
            classPoints = 0, specPoints = 0, resources = {}, powers = {} }
    elseif not incoming then
        return
    elseif command == "TREE" then
        local index, spec, title = string.match(rest, "^(%d+) (%d+) (.*)$")
        incoming.trees[tonumber(index)] = { spec = tonumber(spec), title = title, ids = {} }
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
    elseif command == "DONE" then
        state = incoming
        incoming = nil
        pending = false
        BuildTrees()
        Render()
        UpdateResourceFrame()
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
    table.sort(tabs, function(a, b) return (a:GetLeft() or 0) < (b:GetLeft() or 0) end)
    return tabs, parent
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
    panel.header = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    panel.header:SetPoint("TOP", panel, "TOP", 0, -16)
    panel.header:SetText(Naming().tab)
    panel.points = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    panel.points:SetPoint("BOTTOM", panel, "BOTTOM", 0, 18)
    panel.status = panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightLarge")
    panel.status:SetPoint("CENTER", panel, "CENTER", 0, 0)
    panel.status:SetWidth(500)
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

local function CopyBackdrop(source, copy)
    if not (source.GetBackdrop and source:GetBackdrop()) then
        return
    end
    copy:SetBackdrop(source:GetBackdrop())
    copy:SetBackdropColor(source:GetBackdropColor())
    copy:SetBackdropBorderColor(source:GetBackdropBorderColor())
end

local function CopyContents(source, copy, mapped, label, text)
    CopyBackdrop(source, copy)
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
            own:SetTextColor(region:GetTextColor())
            local shown = region:GetText()
            own:SetText(shown == label and Naming().tab or shown)
            mapped[region] = own
        end
    end
    for _, child in ipairs({ source:GetChildren() }) do
        local own = CreateFrame("Frame", nil, copy)
        own:SetWidth(child:GetWidth())
        own:SetHeight(child:GetHeight())
        own:SetFrameLevel(copy:GetFrameLevel() + math.max(child:GetFrameLevel() - source:GetFrameLevel(), 0))
        own:EnableMouse(false)
        mapped[child] = own
        if not child:IsShown() then
            own:Hide()
        end
        CopyContents(child, own, mapped, label, text)
    end
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

local function KeepTabsInRow(first, following, gap)
    local function Arrange()
        local previous = tab
        for _, other in ipairs(following) do
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
    local first, after = tabs[1], tabs[2]
    for index, other in ipairs(tabs) do
        if TextOf(other) == "Character Advancement" then
            first, after = other, tabs[index + 1]
        end
    end
    tab = CopyTab(first, parent)
    local gap = 4
    if after and after:GetLeft() and first:GetRight() then
        gap = math.max(after:GetLeft() - first:GetRight(), 2)
    end
    local following = {}
    local seen = false
    for _, other in ipairs(tabs) do
        if seen then
            following[#following + 1] = other
        end
        seen = seen or other == first
    end
    KeepTabsInRow(first, following, gap)
    tab:SetScript("OnClick", function()
        if panel:IsShown() then
            panel:Hide()
        else
            panel:Show()
        end
    end)
    for _, other in ipairs(tabs) do
        other:HookScript("OnClick", function() panel:Hide() end)
    end
    window:HookScript("OnHide", function() panel:Hide() end)
    return true
end

local function IsStandardClass()
    local _, _, classId = UnitClass("player")
    return classId and classId <= 11
end

local CLASSIC_TIERS, CLASSIC_COLUMNS = 11, 4
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
    frame.title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalHuge")
    frame.title:SetPoint("TOP", frame, "TOP", 0, -6)
    frame.points = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    frame.points:SetPoint("TOP", frame.title, "BOTTOM", 0, -2)
    frame.buttons = {}
    frame.lines = {}
    classicFrames[index] = frame
    return frame
end

local function ClassicButton(canvas, index)
    local button = canvas.buttons[index]
    if button then
        return button
    end
    button = CreateFrame("Button", nil, canvas)
    button:SetWidth(NODE)
    button:SetHeight(NODE)
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
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetTalent(self.tabIndex, self.talentIndex)
        GameTooltip:Show()
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
    canvas.title:SetText(name or "")
    canvas.points:SetText(string.format("%d point(s) spent", spent or 0))
    local width, height = canvas:GetWidth(), canvas:GetHeight() - 50
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
        button.rank:SetText(rank .. "/" .. maxRank)
        button.icon:SetDesaturated(rank == 0 and not meets)
        if rank >= maxRank then
            button.border:SetVertexColor(1, 0.82, 0)
            button.border:Show()
        elseif meets and FreeTalentPoints() > 0 then
            button.border:SetVertexColor(0, 1, 0)
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

local function RenderClassic()
    if not ownPage or not ownPage:IsShown() then
        return
    end
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
    local classic = CreateFrame("Button", nil, book, "UIPanelButtonTemplate")
    classic:SetWidth(120)
    classic:SetHeight(22)
    classic:SetPoint("TOPLEFT", book, "TOPLEFT", 16, -12)
    classic:SetText("Classic window")
    classic:SetScript("OnClick", function()
        book:Hide()
        if blizzardToggleTalentFrame then
            blizzardToggleTalentFrame()
        end
    end)

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

local function Attach()
    if IsStandardClass() then
        return true
    end
    return AttachToWindow()
end

local function Toggle()
    if IsStandardClass() then
        ToggleBook()
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

blizzardToggleTalentFrame = ToggleTalentFrame
ToggleTalentFrame = function(...)
    if IsStandardClass() then
        ToggleBook()
    elseif blizzardToggleTalentFrame then
        blizzardToggleTalentFrame(...)
    end
end

local dressingTwin = false
local twinButton

local function SendTwinLook()
    local looks = {}
    for category = 1, 11 do
        local appearance = C_Appearance.GetPendingAppearance(category)
        if appearance then
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

local function HookApplyButton(wardrobe)
    local frame = EnumerateFrames()
    while frame do
        if frame.GetObjectType and frame:GetObjectType() == "Button" and TextOf(frame) == "Apply"
            and IsInside(frame, wardrobe) and not frame.subclassTwinHooked then
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

local function ShowDressing()
    twinButton:SetText(dressingTwin and "Dressing: Your twin" or "Dressing: You")
end

local function AttachTwinButton()
    if twinButton then
        return true
    end
    local _, race = UnitRace("player")
    if race ~= "ThinHuman" then
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
        DEFAULT_CHAT_FRAME:AddMessage(dressingTwin
            and "|cffffd100Wardrobe:|r Apply now dresses your twin."
            or "|cffffd100Wardrobe:|r Apply now dresses you.")
    end)
    twinButton:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_BOTTOM")
        GameTooltip:SetText("Who the Wardrobe dresses", 1, 1, 1)
        GameTooltip:AddLine("Switch to your twin, pick his look and apply it as usual. "
            .. "He wears it without needing the items.", nil, nil, nil, true)
        GameTooltip:Show()
    end)
    twinButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
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
    Ascension_SubclassDump = {}
    for index, other in ipairs(tabs) do
        Ascension_SubclassDump[index] = DescribeFrame(other, 0)
    end
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
    elseif message == "icons" then
        ShowIcons()
    else
        Toggle()
    end
end

local tries = 0
local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function(_, event, prefix, message, _, sender)
    if event == "CHAT_MSG_ADDON" then
        if prefix == PREFIX and sender == UnitName("player") then
            Receive(message)
        end
    elseif event == "PLAYER_ENTERING_WORLD" then
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
