Ascension_Subclass = Ascension_Subclass or {}

local Choices = {}
Ascension_Subclass.Choices = Choices

local FACE_TEMPLATE = "CoATalentButtonHexTemplate"
local LEFT_MASK = "Interface\\TalentFrame\\TalentsMaskNodeChoiceLeft.blp"
local RIGHT_MASK = "Interface\\TalentFrame\\TalentsMaskNodeChoiceRight.blp"
local HIDE_AFTER = 0.15

local function IconPath(icon)
    if not icon or icon == "" then
        return "Interface\\Icons\\INV_Misc_QuestionMark"
    end
    if string.find(icon, "\\", 1, true) then
        return icon
    end
    return "Interface\\Icons\\" .. icon
end

function Choices.Available()
    return Ascension_Subclass.CoAReady ~= nil and Ascension_Subclass.CoAReady() and
        CoACharacterAdvancementUtil.NodeArtSet ~= nil
end

local function Over(face)
    local focus = GetMouseFocus()
    if focus == face then
        return true
    end
    for _, option in ipairs(face.options or {}) do
        if focus == option then
            return true
        end
    end
    return false
end

local function ShowOptions(face, shown)
    face.open = shown
    for _, option in ipairs(face.options or {}) do
        if shown then
            option:Show()
        else
            option:Hide()
        end
    end
end

local function Watch(watcher, elapsed)
    local face = watcher.face
    if Over(face) then
        watcher.idle = 0
        return
    end
    watcher.idle = watcher.idle + elapsed
    if watcher.idle > HIDE_AFTER then
        ShowOptions(face, false)
        watcher:SetScript("OnUpdate", nil)
    end
end

local function Open(face)
    ShowOptions(face, true)
    face.watcher.idle = 0
    face.watcher:SetScript("OnUpdate", Watch)
end

local function Enter(face)
    Open(face)
    if face.chosen and face.handlers then
        face.node, face.tree = face.chosen.node, face.chosen.tree
        face.handlers.enter(face)
        return
    end
    GameTooltip:SetOwner(face, "ANCHOR_RIGHT")
    GameTooltip:SetText("Choose one", 1, 0.82, 0)
    for _, option in ipairs(face.options or {}) do
        GameTooltip:AddLine(option.node and option.node.entry.Name or "?", 1, 1, 1)
    end
    GameTooltip:AddLine("Pick it from the options above.", 0.8, 0.8, 0.8, true)
    GameTooltip:Show()
end

local function Click(face, mouse)
    if face.chosen and face.handlers then
        face.node, face.tree = face.chosen.node, face.chosen.tree
        face.handlers.click(face, mouse)
    else
        Open(face)
    end
end

local function NewFace(canvas, entry, handlers)
    local face = Ascension_Subclass.CoANode(canvas, entry, nil, handlers, FACE_TEMPLATE)
    if not face then
        return nil
    end
    face:SetScript("OnEnter", Enter)
    face:SetScript("OnClick", Click)
    face.watcher = CreateFrame("Frame", nil, face)
    face.watcher.face = face
    face.splitLeft = face.Icon:CreateTexture(nil, "BORDER")
    face.splitLeft:SetAllPoints(face.Icon)
    face.splitRight = face.Icon:CreateTexture(nil, "BORDER")
    face.splitRight:SetAllPoints(face.Icon)
    return face
end

local function Face(canvas, index, entry, handlers)
    canvas.choiceFaces = canvas.choiceFaces or {}
    local face = canvas.choiceFaces[index] or NewFace(canvas, entry, handlers)
    canvas.choiceFaces[index] = face
    if face then
        face.handlers = handlers
    end
    return face
end

local function Paint(texture, icon, mask, left)
    texture:SetTexCoord(0, 1, 0, 1)
    local size = TextureUtil and TextureUtil.MaskSize and TextureUtil.MaskSize.Small
    if texture.SetMaskedTexture and pcall(texture.SetMaskedTexture, texture, icon, mask, size) then
        return
    end
    texture:SetTexture(icon)
    if left then
        texture:SetTexCoord(0, 0.5, 0, 1)
    else
        texture:SetTexCoord(0.5, 1, 0, 1)
    end
end

local function Split(face, left, right, dimmed)
    local icon = face.Icon and face.Icon.Icon
    if not (left and right) then
        face.splitLeft:Hide()
        face.splitRight:Hide()
        if icon then
            icon:SetAlpha(1)
        end
        return
    end
    Paint(face.splitLeft, IconPath(left.Icon), LEFT_MASK, true)
    Paint(face.splitRight, IconPath(right.Icon), RIGHT_MASK, false)
    if not face.splitLeft.SetMaskedTexture then
        face.splitLeft:ClearAllPoints()
        face.splitLeft:SetPoint("TOPLEFT", face.Icon, "TOPLEFT")
        face.splitLeft:SetPoint("BOTTOMRIGHT", face.Icon, "BOTTOM")
        face.splitRight:ClearAllPoints()
        face.splitRight:SetPoint("TOPLEFT", face.Icon, "TOP")
        face.splitRight:SetPoint("BOTTOMRIGHT", face.Icon, "BOTTOMRIGHT")
    end
    face.splitLeft:SetDesaturated(dimmed)
    face.splitRight:SetDesaturated(dimmed)
    face.splitLeft:Show()
    face.splitRight:Show()
    if icon then
        icon:SetAlpha(0)
    end
end

local function Show(face, spot, tree, x, y)
    face.options, face.tree = spot.buttons, tree
    local chosen
    local anyLearnable, allGated = false, true
    for _, option in ipairs(face.options) do
        option.tree = option.tree or tree
        if (option.rank or 0) > 0 then
            chosen = option
        end
        anyLearnable = anyLearnable or option.canLearn and true or false
        allGated = allGated and option.isGated and true or false
    end
    face.chosen = chosen
    local shown = chosen or face.options[1]
    face:ClearAllPoints()
    face:SetPoint("CENTER", face:GetParent(), "BOTTOMLEFT", x, y)
    pcall(face.SetArtSet, face, CoACharacterAdvancementUtil.NodeArtSet.Choice)
    local learnable = chosen and chosen.canLearn or (not chosen and anyLearnable)
    Ascension_Subclass.ShowCoANode(face, shown.entry, shown.rank or 0, shown.maxRank or 1, learnable,
        not chosen and allGated)
    Split(face, not chosen and face.options[1].entry, not chosen and face.options[2] and face.options[2].entry,
        not anyLearnable)
    ShowOptions(face, face.open and Over(face))
end

function Choices.Render(canvas, tree, sharing, handlers)
    local used = 0
    if Choices.Available() then
        for _, spot in pairs(sharing) do
            if #spot > 1 and spot.buttons and spot.center then
                local face = Face(canvas, used + 1, spot[1].entry, handlers)
                if face then
                    used = used + 1
                    Show(face, spot, tree, spot.center[1], spot.center[2])
                else
                    ShowOptions({ options = spot.buttons }, true)
                end
            end
        end
    end
    for index = used + 1, #(canvas.choiceFaces or {}) do
        canvas.choiceFaces[index]:Hide()
        canvas.choiceFaces[index].options = nil
    end
end
