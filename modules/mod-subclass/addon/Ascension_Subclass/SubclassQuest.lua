Ascension_Subclass = Ascension_Subclass or {}

local SECOND_HEAD_RACES = { Ogre = true }
local QUEST_ICON = "Interface\\GossipFrame\\AvailableQuestIcon"
local PULSE_SPEED = 3

local Quest = {}

function Quest.Button()
    if Quest.button then
        return Quest.button
    end
    local button = CreateFrame("Button", "Ascension_SubclassQuestButton", UIParent)
    button:SetSize(28, 28)
    button:SetFrameStrata("HIGH")
    button.icon = button:CreateTexture(nil, "OVERLAY")
    button.icon:SetAllPoints(button)
    button.icon:SetTexture(QUEST_ICON)
    button:SetScript("OnClick", function()
        Ascension_Subclass.Send("QUESTASK")
    end)
    button:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:AddLine("Your second head wants a word", 1, 0.82, 0)
        GameTooltip:AddLine("Click to hear her out.", 1, 1, 1)
        GameTooltip:Show()
    end)
    button:SetScript("OnLeave", function()
        GameTooltip:Hide()
    end)
    button:SetScript("OnUpdate", function(self, elapsed)
        self.time = (self.time or 0) + elapsed
        self.icon:SetAlpha(0.65 + 0.35 * math.abs(math.sin(self.time * PULSE_SPEED)))
    end)
    button:Hide()
    Quest.button = button
    return button
end

function Quest.Place(button)
    button:ClearAllPoints()
    if PlayerFrame and PlayerFrame:IsVisible() then
        button:SetPoint("BOTTOMRIGHT", PlayerPortrait or PlayerFrame, "TOPRIGHT", -2, -14)
        button:SetSize(28, 28)
    else
        button:SetPoint("TOP", UIParent, "TOP", 0, -120)
        button:SetSize(40, 40)
    end
end

function Ascension_Subclass.ShowQuestOffer(offered)
    local _, race = UnitRace("player")
    if offered and SECOND_HEAD_RACES[race] then
        local button = Quest.Button()
        Quest.Place(button)
        button:Show()
    elseif Quest.button then
        Quest.button:Hide()
    end
end
