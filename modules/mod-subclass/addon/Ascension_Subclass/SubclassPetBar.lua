Ascension_Subclass = Ascension_Subclass or {}

local PetBar = { size = 30, gap = 8, slots = NUM_PET_ACTION_SLOTS or 10, owner = "-", pending = false }

function PetBar.Place()
    local saved = Ascension_SubclassDump and Ascension_SubclassDump.petBarPoint
    PetBar.bar:ClearAllPoints()
    if saved then
        PetBar.bar:SetPoint(saved[1], UIParent, saved[2], saved[3], saved[4])
    elseif Ascension_SubclassStanceBar then
        PetBar.bar:SetPoint("BOTTOMLEFT", Ascension_SubclassStanceBar, "TOPLEFT", 0, PetBar.gap)
    elseif Ascension_SubclassBar then
        PetBar.bar:SetPoint("BOTTOMLEFT", Ascension_SubclassBar, "TOPLEFT", 0, PetBar.gap)
    else
        PetBar.bar:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 160)
    end
end

function PetBar.StartMoving()
    if IsAltKeyDown() and not InCombatLockdown() then
        PetBar.bar:StartMoving()
        return true
    end
end

function PetBar.StopMoving()
    PetBar.bar:StopMovingOrSizing()
    local point, _, relative, x, y = PetBar.bar:GetPoint()
    Ascension_SubclassDump = Ascension_SubclassDump or {}
    Ascension_SubclassDump.petBarPoint = { point, relative, x, y }
end

function PetBar.Button(index)
    local name = "Ascension_SubclassPetButton" .. index
    local button = CreateFrame("CheckButton", name, PetBar.bar, "SecureActionButtonTemplate")
    button:SetID(index)
    button:SetWidth(PetBar.size)
    button:SetHeight(PetBar.size)
    button:SetPoint("LEFT", PetBar.bar, "LEFT", (index - 1) * (PetBar.size + PetBar.gap), 0)
    button:RegisterForClicks("AnyUp")
    button:RegisterForDrag("LeftButton", "RightButton")
    button:SetAttribute("type1", "pet")
    button:SetAttribute("action1", index)
    button.icon = button:CreateTexture(nil, "BACKGROUND")
    button.icon:SetAllPoints(button)
    button:SetNormalTexture("Interface\\Buttons\\UI-Quickslot2")
    local normal = button:GetNormalTexture()
    normal:ClearAllPoints()
    normal:SetPoint("CENTER", button, "CENTER", 0, 0)
    normal:SetWidth(PetBar.size * 1.8)
    normal:SetHeight(PetBar.size * 1.8)
    button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    button:SetCheckedTexture("Interface\\Buttons\\CheckButtonHilight", "ADD")
    button.allowed = button:CreateTexture(nil, "OVERLAY")
    button.allowed:SetTexture("Interface\\Buttons\\UI-AutoCastableOverlay")
    button.allowed:SetPoint("CENTER", button, "CENTER", 0, 0)
    button.allowed:SetWidth(PetBar.size * 1.9)
    button.allowed:SetHeight(PetBar.size * 1.9)
    button.shine = CreateFrame("Frame", name .. "Shine", button, "AutoCastShineTemplate")
    button.shine:SetAllPoints(button)
    button.cooldown = CreateFrame("Cooldown", name .. "Cooldown", button, "CooldownFrameTemplate")
    button.cooldown:SetAllPoints(button)
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetPetAction(self:GetID())
        GameTooltip:Show()
    end)
    button:SetScript("OnLeave", function() GameTooltip:Hide() end)
    button:SetScript("PostClick", function(self) PetBar.Refresh() end)
    button:SetScript("OnDragStart", function(self)
        if not PetBar.StartMoving() and not InCombatLockdown() then
            PickupPetAction(self:GetID())
            PetBar.Refresh()
        end
    end)
    button:SetScript("OnDragStop", PetBar.StopMoving)
    button:SetScript("OnReceiveDrag", function(self)
        if not InCombatLockdown() then
            PickupPetAction(self:GetID())
            PetBar.Refresh()
        end
    end)
    return button
end

function PetBar.Create()
    PetBar.bar = CreateFrame("Frame", "Ascension_SubclassPetBar", UIParent, "SecureHandlerStateTemplate")
    PetBar.bar:SetWidth(PetBar.slots * (PetBar.size + PetBar.gap))
    PetBar.bar:SetHeight(PetBar.size)
    PetBar.bar:SetMovable(true)
    PetBar.bar:EnableMouse(true)
    PetBar.bar:RegisterForDrag("LeftButton")
    PetBar.bar:SetScript("OnDragStart", PetBar.StartMoving)
    PetBar.bar:SetScript("OnDragStop", PetBar.StopMoving)
    PetBar.buttons = {}
    for index = 1, PetBar.slots do
        PetBar.buttons[index] = PetBar.Button(index)
    end
    PetBar.bar:Hide()
    PetBar.Place()
end

function PetBar.Refresh()
    if not PetBar.bar then
        return
    end
    for index, button in ipairs(PetBar.buttons) do
        local name, _, texture, isToken, isActive, autoCastAllowed, autoCastEnabled = GetPetActionInfo(index)
        if isToken then
            texture, name = _G[texture], _G[name]
        end
        button.icon:SetTexture(texture)
        button.icon:SetVertexColor(1, 1, 1)
        button:SetChecked(isActive and 1 or nil)
        if autoCastAllowed and not autoCastEnabled then
            button.allowed:Show()
        else
            button.allowed:Hide()
        end
        if autoCastEnabled then
            AutoCastShine_AutoCastStart(button.shine)
        else
            AutoCastShine_AutoCastStop(button.shine)
        end
        if not InCombatLockdown() then
            button:SetAttribute("type2", autoCastAllowed and not isToken and "macro" or nil)
            button:SetAttribute("macrotext2", autoCastAllowed and not isToken and name and
                "/petautocasttoggle " .. name or nil)
        end
        local start, duration, enable = GetPetActionCooldown(index)
        CooldownFrame_SetTimer(button.cooldown, start, duration, enable)
    end
end

function PetBar.Lend(active)
    if not InCombatLockdown() then
        for index = 1, PetBar.slots do
            local button = _G["PetActionButton" .. index]
            if button then
                button:EnableMouse(not active)
            end
        end
    end
    if PetActionBarFrame then
        PetActionBarFrame:SetAlpha(active and 0 or 1)
    end
end

function PetBar.Apply()
    if InCombatLockdown() then
        PetBar.pending = true
        return
    end
    PetBar.pending = false
    if not PetBar.bar then
        PetBar.Create()
    end
    local active = PetBar.owner == "brother"
    if active then
        RegisterStateDriver(PetBar.bar, "visibility", "[pet,nobonusbar:5] show; hide")
    else
        UnregisterStateDriver(PetBar.bar, "visibility")
        PetBar.bar:Hide()
    end
    PetBar.Lend(active)
    PetBar.Refresh()
end

function Ascension_Subclass.OnPetOwner(owner)
    PetBar.owner = owner
    PetBar.Apply()
end

function Ascension_Subclass.DescribePetBar()
    DEFAULT_CHAT_FRAME:AddMessage("Pet bar owner from the server: " .. PetBar.owner, 0.8, 0.6, 1)
end

PetBar.watcher = CreateFrame("Frame")
for _, event in ipairs({ "PLAYER_ENTERING_WORLD", "UNIT_PET", "PET_BAR_UPDATE", "PET_BAR_UPDATE_COOLDOWN",
    "PET_UI_UPDATE", "PLAYER_CONTROL_LOST", "PLAYER_CONTROL_GAINED", "PLAYER_REGEN_ENABLED", "UNIT_FLAGS" }) do
    PetBar.watcher:RegisterEvent(event)
end
PetBar.watcher:SetScript("OnEvent", function(self, event, unit)
    if event == "PLAYER_REGEN_ENABLED" then
        if PetBar.pending then
            PetBar.Apply()
        end
        return
    end
    if (event == "UNIT_PET" and unit == "player") or event == "PLAYER_ENTERING_WORLD" then
        if Ascension_Subclass.Send then
            Ascension_Subclass.Send("PETOWNER")
        end
        return
    end
    if (event == "UNIT_FLAGS" and unit ~= "pet") then
        return
    end
    if PetBar.owner == "brother" then
        PetBar.Lend(true)
        PetBar.Refresh()
    end
end)
