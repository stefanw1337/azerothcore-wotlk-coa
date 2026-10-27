Ascension_Subclass = Ascension_Subclass or {}

local Glow = {}

function Glow.ClassToken(name)
    for _, names in ipairs({ LOCALIZED_CLASS_NAMES_MALE or {}, LOCALIZED_CLASS_NAMES_FEMALE or {} }) do
        for token, localized in pairs(names) do
            if localized == name then
                return token
            end
        end
    end
    return string.upper((string.gsub(name, "[^%a]", "")))
end

function Ascension_Subclass.GlowButton(button, spellOf)
    if SpellActivationOverlay_AddButton then
        pcall(SpellActivationOverlay_AddButton, button, spellOf)
    end
end

function Ascension_Subclass.UpdateGlow(button)
    if SpellActivationOverlay_UpdateButton then
        pcall(SpellActivationOverlay_UpdateButton, button)
    end
end

function Ascension_Subclass.GlowSubclass(name)
    if not name or not SpellActivationOverlay_AddClass or Glow.added == name then
        return
    end
    Glow.added = name
    pcall(SpellActivationOverlay_AddClass, Glow.ClassToken(name))
end
