local PREFIX = "DressUpBackground-"

local fallbacks = {
    ["DressUpBackground-Ogre"] = "DressUpBackground-Orc",
}

local DEFAULT = "DressUpBackground-Orc"

local function Patch()
    if type(AtlasUtil) ~= "table" or type(AtlasUtil.Unpack) ~= "function" or AtlasUtil.RaceBackgroundFix then
        return type(AtlasUtil) == "table" and AtlasUtil.RaceBackgroundFix
    end

    local original = AtlasUtil.Unpack
    AtlasUtil.Unpack = function(self, name, ...)
        local result = { pcall(original, self, name, ...) }
        if result[1] then
            return select(2, unpack(result, 1, table.maxn(result)))
        end
        if type(name) == "string" and name:sub(1, #PREFIX) == PREFIX then
            return original(self, fallbacks[name] or DEFAULT, ...)
        end
        error(result[2], 2)
    end
    AtlasUtil.RaceBackgroundFix = true
    return true
end

if not Patch() then
    local frame = CreateFrame("Frame")
    frame:RegisterEvent("ADDON_LOADED")
    frame:RegisterEvent("PLAYER_LOGIN")
    frame:SetScript("OnEvent", function(self)
        if Patch() then
            self:UnregisterAllEvents()
        end
    end)
end
