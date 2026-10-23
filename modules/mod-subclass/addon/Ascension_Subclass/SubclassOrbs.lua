Ascension_Subclass = Ascension_Subclass or {}

local FILL_FRAMES, LOOP_SPEED, FPS = 64, 0.05, 60
local SUNSET = 804584
local SUNSET_TINT = { 1, 0.5, 0 }
local SUNSET_BASE_MAXIMUM = 5

local LOOKS = {
    [500149] = {
        size = { 64, 64 },
        loop = { atlas = "SunPowerOrbFlipBook", cell = { 128, 128 }, size = { 44, 44 }, layer = "BORDER" },
        fill = { atlas = "SunPowerFillFlipBook", cell = { 128, 128 }, size = { 64, 64 }, at = { 0, -2 },
            layer = "ARTWORK" },
        glow = { atlas = "SunPowerEmpoweredFlipBook", cell = { 256, 256 }, size = { 96, 96 }, layer = "OVERLAY",
            blend = "ADD" },
        art = { texture = "INTERFACE\\CoAResource\\sunpwr_art", size = { 128, 128 }, layer = "BACKGROUND" },
        partner = SUNSET,
        maximum = 20,
    },
    [805813] = {
        size = { 64, 64 },
        fill = { atlas = "BarbarianMugFlipBook", cell = { 128, 128 }, size = { 112, 112 }, at = { 10, 10 },
            layer = "BACKGROUND" },
        text = { -4, -28 },
        maximum = 10,
    },
    [706613] = {
        size = { 170, 85 },
        fill = { atlas = "BloodMageFlipBookFill", texture = "Interface\\CoAResource\\atlas_full_bloodmage",
            cell = { 256, 128 }, size = { 170, 85 }, at = { 0, -6 }, layer = "BACKGROUND" },
        glow = { atlas = "BloodMageFlipBookOverpower", texture = "Interface\\CoAResource\\atlas_full_bloodmage",
            cell = { 256, 128 }, size = { 170, 85 }, at = { 0, -6 }, layer = "ARTWORK" },
        maximum = 10,
    },
    [680687] = {
        size = { 104, 104 },
        fill = { atlas = "WorgenOrbFillFlipBook", texture = "Interface\\CoAResource\\SoA_Flipbook_filling",
            cell = { 256, 256 }, size = { 104, 104 }, layer = "BACKGROUND", emptiesUpward = true },
        glow = { atlas = "WorgenEmpoveredFlipBook", texture = "Interface\\CoAResource\\SoA_Empovered_flipbook",
            cell = { 128, 128 }, size = { 104, 104 }, layer = "ARTWORK", blend = "ADD" },
        art = { texture = "INTERFACE\\CoAResource\\SoA_Wolf", size = { 104, 104 }, layer = "BORDER" },
        text = { 0, 6 },
        maximum = 10,
    },
    [500706] = {
        size = { 128, 43 },
        loop = { atlas = "InsanityNewEffectFlipbook", cell = { 512, 128 }, size = { 173, 43 }, layer = "BORDER" },
        fill = { atlas = "InsanityNewFillFlipbook", cell = { 512, 128 }, size = { 173, 43 }, layer = "ARTWORK" },
        art = { texture = "INTERFACE\\CoAResource\\cultistInsanityBorder", size = { 256, 64 }, layer = "BACKGROUND" },
        maximum = 100,
    },
}

local Orbs = { shown = {} }

local function IgnoreAtlasSize()
    return Const and Const.TextureKit and Const.TextureKit.IgnoreAtlasSize
end

function Orbs.Flipbook(orb, part)
    local texture = orb:CreateTexture(nil, part.layer)
    texture:SetAtlas(part.atlas, IgnoreAtlasSize())
    if part.texture then
        texture:SetTexture(part.texture)
    end
    texture:SetSize(part.size[1], part.size[2])
    texture:SetPoint("CENTER", orb, "CENTER", part.at and part.at[1] or 0, part.at and part.at[2] or 0)
    MixinAndLoad(texture, AtlasVerticalFlipbookMixin)
    C_Flipbook:CreateAtlasFlipbook(texture, part.atlas, part.cell[1], part.cell[2], FILL_FRAMES, FPS, false)
    texture:SetDrawLayer(part.layer)
    if part.blend then
        texture:SetBlendMode(part.blend)
    end
    texture:Init()
    texture.fps = LOOP_SPEED
    return texture
end

function Orbs.Container()
    if Orbs.container then
        return Orbs.container
    end
    local container = CreateFrame("Frame", "Ascension_SubclassOrbs", UIParent)
    container:SetSize(1, 1)
    container:SetMovable(true)
    container:SetClampedToScreen(true)
    local saved = Ascension_SubclassDump and Ascension_SubclassDump.orbPoint
    if saved then
        container:SetPoint(saved[1], UIParent, saved[2], saved[3], saved[4])
    elseif Ascension_SubclassBar then
        container:SetPoint("LEFT", Ascension_SubclassBar, "RIGHT", 70, 10)
    else
        container:SetPoint("BOTTOM", UIParent, "BOTTOM", 260, 190)
    end
    Orbs.container = container
    return container
end

function Orbs.SavePosition()
    local point, _, relative, x, y = Orbs.container:GetPoint(1)
    Ascension_SubclassDump = Ascension_SubclassDump or {}
    Ascension_SubclassDump.orbPoint = { point, relative, x, y }
end

function Orbs.Create(spell, look)
    local orb = CreateFrame("Button", nil, Orbs.Container())
    orb:Hide()
    orb:SetSize(look.size[1], look.size[2])
    orb:EnableMouse(true)
    orb:RegisterForDrag("LeftButton")
    orb:SetScript("OnDragStart", function() Orbs.container:StartMoving() end)
    orb:SetScript("OnDragStop", function()
        Orbs.container:StopMovingOrSizing()
        Orbs.SavePosition()
    end)
    if look.art then
        orb.art = orb:CreateTexture(nil, look.art.layer)
        orb.art:SetTexture(look.art.texture)
        orb.art:SetSize(look.art.size[1], look.art.size[2])
        orb.art:SetPoint("CENTER")
    end
    orb.loop = look.loop and Orbs.Flipbook(orb, look.loop)
    orb.fill = Orbs.Flipbook(orb, look.fill)
    orb.glow = look.glow and Orbs.Flipbook(orb, look.glow)
    if orb.glow then
        orb.glow:Hide()
    end
    orb.text = orb:CreateFontString(nil, "OVERLAY", "TextStatusBarText")
    orb.text:SetPoint("CENTER", orb, "CENTER", look.text and look.text[1] or 0, look.text and look.text[2] or 0)
    orb.look, orb.spell, orb.progress = look, spell, 0
    orb:SetScript("OnUpdate", Orbs.Animate)
    return orb
end

function Orbs.Animate(orb, elapsed)
    for _, part in ipairs({ orb.loop, orb.glow }) do
        if part and part:IsShown() then
            AtlasVerticalFlipbookMixin.Update(part, elapsed)
            if part.frame > (part.frames or FILL_FRAMES) then
                part.frame = 1
            end
        end
    end
    local wanted = math.max(1, math.floor(orb.progress * FILL_FRAMES / 100 + 0.5))
    if orb.look.fill.emptiesUpward then
        wanted = math.max(1, FILL_FRAMES - wanted + 1)
    end
    local frame = orb.fill.frame or 1
    if frame ~= wanted then
        orb.fill.frame = frame + (wanted > frame and 1 or -1)
        AtlasVerticalFlipbookMixin.SetFrame(orb.fill, orb.fill.frame)
    end
end

function Orbs.Tint(orb, color)
    for _, part in ipairs({ orb.loop, orb.fill, orb.glow }) do
        if part then
            part:SetVertexColor(color[1], color[2], color[3])
        end
    end
end

function Orbs.Show(orb, value, maximum, tinted)
    orb.progress = maximum > 0 and math.min(100, value * 100 / maximum) or 0
    orb.text:SetText(string.format("%d/%d", value, maximum))
    Orbs.Tint(orb, tinted and SUNSET_TINT or { 1, 1, 1 })
    if orb.glow then
        if orb.progress >= 100 or tinted then
            orb.glow:Show()
        else
            orb.glow:Hide()
        end
    end
    orb:Show()
end

function Orbs.Refresh(resources, stacksOf)
    local handled = {}
    local wanted = {}
    for _, resource in ipairs(resources) do
        if LOOKS[resource.spell] then
            wanted[resource.spell] = resource
        end
    end
    local placed = 0
    for spell, resource in pairs(wanted) do
        local look = LOOKS[spell]
        local orb = Orbs.shown[spell]
        if not orb then
            local ok, created = pcall(Orbs.Create, spell, look)
            orb = ok and created or nil
            Orbs.shown[spell] = orb or false
        end
        if orb then
            handled[spell] = true
            local value = stacksOf(spell)
            local maximum = resource.maximum > 0 and resource.maximum or look.maximum
            local sunset = look.partner and stacksOf(look.partner) or 0
            if look.partner then
                handled[look.partner] = true
            end
            orb:ClearAllPoints()
            orb:SetPoint("LEFT", Orbs.container, "LEFT", placed, 0)
            placed = placed + orb:GetWidth() + 16
            if sunset > 0 then
                Orbs.Show(orb, sunset, math.max(sunset, SUNSET_BASE_MAXIMUM), true)
            else
                Orbs.Show(orb, value, maximum, false)
            end
        end
    end
    for spell, orb in pairs(Orbs.shown) do
        if orb and not wanted[spell] then
            orb:Hide()
        end
    end
    return handled
end

function Ascension_Subclass.ResetOrbs()
    if Ascension_SubclassDump then
        Ascension_SubclassDump.orbPoint = nil
    end
    if Orbs.container then
        Orbs.container:ClearAllPoints()
        if Ascension_SubclassBar then
            Orbs.container:SetPoint("LEFT", Ascension_SubclassBar, "RIGHT", 70, 10)
        else
            Orbs.container:SetPoint("BOTTOM", UIParent, "BOTTOM", 260, 190)
        end
    end
end

Ascension_Subclass.RefreshOrbs = Orbs.Refresh
