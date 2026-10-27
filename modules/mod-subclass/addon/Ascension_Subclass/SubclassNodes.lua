Ascension_Subclass = Ascension_Subclass or {}

local Nodes = {}
local ARROW_GAP = 7

function Nodes.Ready()
    if Nodes.ready == nil then
        if not CoACharacterAdvancementUtil and LoadAddOn then
            pcall(LoadAddOn, "Ascension_CoATalents")
        end
        Nodes.ready = CoACharacterAdvancementUtil ~= nil and TalentButtonUtil ~= nil and CATalentBaseMixin ~= nil and
            LineMixin ~= nil and Vector2D ~= nil
    end
    return Nodes.ready
end

function Ascension_Subclass.CoAReady()
    return Nodes.Ready()
end

function Ascension_Subclass.CoANode(canvas, entry, previous, handlers, wanted)
    if not Nodes.Ready() then
        return nil
    end
    local template = wanted or CoACharacterAdvancementUtil.GetDisplayTemplateForEntry(entry)
    if previous and previous.coaTemplate == template then
        return previous
    end
    local ok, button = pcall(CreateFrame, "Button", nil, canvas, template)
    if not ok or not button then
        return nil
    end
    button.coaTemplate = template
    button.showZeroMultiRanks = true
    button:SetScript("OnEnter", handlers.enter)
    button:SetScript("OnLeave", handlers.leave)
    button:SetScript("OnClick", handlers.click)
    for _, script in ipairs({ "OnDragStart", "OnDragStop", "OnReceiveDrag", "OnMouseDown", "OnMouseUp" }) do
        if button:HasScript(script) then
            button:SetScript(script, nil)
        end
    end
    return button
end

function Ascension_Subclass.ShowCoANode(button, entry, rank, maxRank, canLearn, gated)
    button.entry = entry
    button.rank, button.maxRank = rank, maxRank
    button.canLearn, button.isGated = canLearn, gated
    button.spellID = entry.Spells and entry.Spells[math.max(rank, 1)]
    if button.spellID then
        pcall(button.UpdateIcon, button)
    end
    pcall(button.UpdateRank, button)
    local state = button:CalculateVisualState() or TalentButtonUtil.BaseVisualState.Locked
    pcall(button.ApplyVisualState, button, state)
    button:Show()
    return state
end

function Ascension_Subclass.CoALine(canvas, previous)
    if previous or not Nodes.Ready() then
        return previous
    end
    local ok, line = pcall(CreateFrame, "Frame", nil, canvas, "CALineConnectionTemplate")
    if not ok or not line then
        return nil
    end
    line:SetAllPoints(canvas)
    line:SetFrameLevel(canvas:GetFrameLevel())
    return line
end

function Ascension_Subclass.DrawCoALine(line, startX, startY, endX, endY, startRadius, connected)
    local from, to = Vector2D(startX, startY), Vector2D(endX, endY)
    for _, part in ipairs({ line.Line1, line.Line2 }) do
        part:SetStartPoint(from)
        part:SetEndPoint(to)
        part:Draw()
    end
    local angle = math.atan2(endY - startY, endX - startX)
    local reach = startRadius + ARROW_GAP
    line.arrowAngle = angle - math.pi / 2
    line.Arrow:ClearAllPoints()
    line.Arrow:SetPoint("CENTER", line, "BOTTOMLEFT", startX + math.cos(angle) * reach,
        startY + math.sin(angle) * reach)
    line:UpdateConnectionState(connected and TalentButtonUtil.ConnectionState.Connected or
        TalentButtonUtil.ConnectionState.Gated)
    line:Show()
end
