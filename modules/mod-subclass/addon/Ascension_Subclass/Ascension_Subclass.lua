local PREFIX = "SUBCLASS"
local NODE = 34
local LINE_TEXTURE = "Interface\\TaxiFrame\\UI-Taxi-Line"

local state = { eligible = false, insights = 0, trees = {}, ranks = {}, classPoints = 0, specPoints = 0 }
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

local function Render()
    if not panel or not panel:IsShown() then
        return
    end
    panel.points:SetText(string.format("Borrowed points left: |cffffffff%d|r class, |cffffffff%d|r specialization",
        state.classPoints, state.specPoints))
    if #state.trees == 0 then
        if state.eligible then
            panel.status:SetText(state.insights > 0 and
                "Your second head holds an insight. Visit any class trainer to choose a second way." or
                "Your second head has no second way yet. Find the Book of Origins in Shadowfang Keep.")
        else
            panel.status:SetText("Only two-headed characters can learn a second way.")
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
            classPoints = 0, specPoints = 0 }
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
    elseif command == "DONE" then
        state = incoming
        incoming = nil
        pending = false
        BuildTrees()
        Render()
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
    panel.header:SetText("Second Head - Character Advancement")
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
    local last = tabs[#tabs]
    tab = CreateFrame("Button", "Ascension_SubclassTab", parent, "UIPanelButtonTemplate")
    tab:SetText("Second Head")
    tab:SetWidth(math.max(120, last:GetWidth()))
    tab:SetHeight(last:GetHeight())
    tab:SetPoint("LEFT", last, "RIGHT", 6, 0)
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

local function Toggle()
    if not panel then
        if not AttachToWindow() then
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

SLASH_ASCENSIONSUBCLASS1 = "/subclass"
SlashCmdList.ASCENSIONSUBCLASS = Toggle

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
            if (panel and tab) or AttachToWindow() or tries >= 24 then
                self:SetScript("OnUpdate", nil)
            end
        end)
    end
end)
