
-- mod-subclass races: Thin Human (96, male) and Two-Headed Ogre (85, female)
CHAR_CREATE_RACE_IDS["Thin Human"] = {96}
CHAR_CREATE_RACE_IDS["Two-Headed Ogre"] = {85}
CHAR_CREATE_MALE_ONLY_RACES["Thin Human"] = true
CHAR_CREATE_FEMALE_ONLY_RACES["Two-Headed Ogre"] = true
if CHAR_CREATE_EXTRA_RACE_ICONS then
	CHAR_CREATE_EXTRA_RACE_ICONS["Thin Human"] = "Interface\\Icons\\CoA_Race_ThinHuman"
	CHAR_CREATE_EXTRA_RACE_ICONS["Two-Headed Ogre"] = "Interface\\Icons\\CoA_Race_Ogre"
end
if CHAR_CREATE_COA_RACIALS then
	CHAR_CREATE_COA_RACIALS[96] = {
		{"Brother, Where Art Thou?", "Spell_Holy_PrayerOfSpirit", "active", "Call your twin brother to your side, or send him home. From level 20 he can follow a class of his own."},
		{"Twin-Linked", "Spell_Holy_SealOfSacrifice", "passive", "You and your brother share one health and mana pool, and whatever befalls you befalls him too."},
		{"Desperate Flight", "Ability_Rogue_Sprint", "active", "Removes roots and snares and increases movement speed by 20% for 6 sec."},
		{"Wiry Frame", "Ability_Racial_ShadowMeld", "passive", "Dodge chance increased by 1%."},
		{"Nervous Energy", "Ability_Rogue_Sprint", "passive", "Haste increased by 1%."},
		{"Make Do", "INV_Misc_Fork&Knife", "passive", "Food, drink and bandages restore 10% more health or mana."},
		{"Field Dressing", "Spell_Holy_SealOfSacrifice", "passive", "First Aid skill increased by 15."},
	}
	CHAR_CREATE_COA_RACIALS[85] = {
		{"Agree or Disagree", "Spell_Shadow_BrainWash", "active", "Your two heads agree for once, or go back to arguing. From level 20 the second head can follow a class of its own."},
		{"Tolerance", "INV_Drink_05", "passive", "Drinks make you half as drunk, the haze wears off twice as fast, and you never get past merely drunk."},
		{"Headbutt", "Ability_Warrior_DecisiveStrike", "active", "Stuns a nearby enemy for 2 sec."},
		{"Brute Strength", "Spell_Nature_UnyeildingStamina", "passive", "Strength increased by 2%."},
		{"Thick Hide", "Spell_Shadow_UnholyStrength", "passive", "Armor from equipped items increased by 2%."},
		{"Heavy Hands", "INV_Axe_02", "passive", "Expertise with maces and fist weapons increased by 3."},
		{"Ogre Cuisine", "Spell_Fire_Fire", "passive", "Cooking skill increased by 5."},
	}
end
if CHAR_CREATE_RACE_LORE then
	CHAR_CREATE_RACE_LORE.alliance = CHAR_CREATE_RACE_LORE.alliance or {}
	CHAR_CREATE_RACE_LORE.horde = CHAR_CREATE_RACE_LORE.horde or {}
	CHAR_CREATE_RACE_LORE.alliance["Thin Human"] = [==[Born in the shadow of Stormwind's walls to a stonemason who helped rebuild the city and a Kul Tiran sailor's daughter, the Thin Humans are lean, wiry folk who learned early to slip through crowds and out of trouble. Most of them travel alone. A few were born a pair, and somewhere out there a brother is waiting for the day his twin finds their father's lost Bravery Badge.]==]
	CHAR_CREATE_RACE_LORE.horde["Two-Headed Ogre"] = [==[Two heads, one body, and never a moment's peace. The two-headed ogres of the Horde are mighty brawlers whose heads argue over everything from breakfast to battle plans. When they do agree, they hit twice as hard. Legend says an old book of origins hidden in Shadowfang Keep can teach the second head a craft of its own.]==]
end

COA_SUBCLASS_SMALL_RACES = { ["Two-Headed Ogre"] = 1.9 }
COA_SUBCLASS_SELECT_SCALES = { ["Two-Headed Ogre"] = 1.17 }
do
	local function SmallRaceScale()
		if not (CharacterCreateFrame and CharacterCreateFrame:IsVisible()) then return end
		local ok, name = pcall(GetNameForRace)
		return ok and name and COA_SUBCLASS_SMALL_RACES[name]
	end
	local zoomIn = CHAR_CREATE_CAMERA_ZOOM_IN or {}
	local wheeling = false
	CHAR_CREATE_CAMERA_ZOOM_IN = setmetatable({}, { __index = function(_, fileString)
		if not wheeling and SmallRaceScale() then
			return 0
		end
		return zoomIn[fileString]
	end })
	local wheel = CharacterCreateFrame_OnMouseWheel
	if wheel then
		function CharacterCreateFrame_OnMouseWheel(...)
			wheeling = true
			local ok, problem = pcall(wheel, ...)
			wheeling = false
			if not ok then
				error(problem)
			end
		end
	end
	local function Near(a, b)
		return a and b and math.abs(a[1] - b[1]) + math.abs(a[2] - b[2]) + math.abs(a[3] - b[3]) < 0.01
	end
	local customize = CreateFrame("Frame")
	local since, base
	customize:SetScript("OnUpdate", function(self, elapsed)
		local active = CharacterCreateFrame and CharacterCreateFrame:IsVisible() and Enum and Enum.CharCreateSteps
			and CharacterCreateFrame.state == Enum.CharCreateSteps.Customization
			and C_CharacterCreate and C_CharacterCreate.GetCameraPosition
		local k = active and SmallRaceScale()
		active = active and k
		if not active then
			since, base = nil, nil
			return
		end
		since = (since or 0) + elapsed
		if since < 0.4 then return end
		local ok, x, y, z = pcall(C_CharacterCreate.GetCameraPosition)
		if not ok or not x then return end
		local now = { x, y, z }
		if base and not Near(now, base) then return end
		base = now
		pcall(C_CharacterCreate.SetCameraPosition, x * k, y * k, z * (1 + (k - 1) * 0.6))
	end)
	local selectWatch = CreateFrame("Frame")
	local shownRace, applied
	selectWatch:SetScript("OnUpdate", function(self, elapsed)
		self.wait = (self.wait or 0) + elapsed
		if self.wait < 0.2 then return end
		self.wait = 0
		if not (CharacterSelect and CharacterSelect:IsShown() and CharacterSelect.selectedIndex) then
			shownRace, applied = nil, nil
			return
		end
		local ok, _, race = pcall(GetCharacterInfo, GetCharIDFromIndex and GetCharIDFromIndex(CharacterSelect.selectedIndex) or CharacterSelect.selectedIndex)
		race = ok and race or nil
		if race == shownRace then return end
		shownRace = race
		local k = COA_SUBCLASS_SELECT_SCALES[race or ""]
		if CharacterSelect.SetCamDistanceScale then
			pcall(CharacterSelect.SetCamDistanceScale, CharacterSelect, k or 1)
		end
		if k and not applied and C_CharacterCreate and C_CharacterCreate.GetCameraPosition then
			pcall(function()
				local x, y, z = C_CharacterCreate.GetCameraPosition()
				applied = { x, y, z }
				C_CharacterCreate.SetCameraPosition(x * k, y * k, z * (1 + (k - 1) * 0.6))
			end)
		elseif not k and applied and C_CharacterCreate and C_CharacterCreate.SetCameraPosition then
			pcall(C_CharacterCreate.SetCameraPosition, applied[1], applied[2], applied[3])
			applied = nil
		end
	end)
end

HAIR_EYES_STYLE = ""
HAIR_EYES_COLOR = ""
COA_SUBCLASS_EYE_RACES = { ["Two-Headed Ogre"] = true }
do
	local CUSTOMIZE_FACE = 2
	local function EyesInsteadOfFace()
		if not (CharacterCreateFrame and CharacterCreateFrame:IsVisible()) then return end
		local ok, name = pcall(GetNameForRace)
		return ok and name and COA_SUBCLASS_EYE_RACES[name]
	end
	if C_CharacterCreate and C_CharacterCreate.CycleCustomizationSkipLocked then
		local cycle = C_CharacterCreate.CycleCustomizationSkipLocked
		C_CharacterCreate.CycleCustomizationSkipLocked = function(id, direction, ...)
			if id == CUSTOMIZE_FACE and EyesInsteadOfFace() then
				id = CHAR_CUSTOMIZE_HAIR_COLOR
			end
			return cycle(id, direction, ...)
		end
	end
	for _, name in ipairs({ "CharacterCustomization_Left", "CharacterCustomization_Right" }) do
		local original = _G[name]
		if original then
			_G[name] = function(id, ...)
				if id == CUSTOMIZE_FACE and EyesInsteadOfFace() then
					id = CHAR_CUSTOMIZE_HAIR_COLOR
				end
				return original(id, ...)
			end
		end
	end
	local labels = CreateFrame("Frame")
	labels:SetScript("OnUpdate", function(self, elapsed)
		self.wait = (self.wait or 0) + elapsed
		if self.wait < 0.2 then return end
		self.wait = 0
		local button = CharCreateCustomizationButton2
		if button and button.text and button:IsVisible() then
			local wanted = EyesInsteadOfFace() and "Eye Color" or "Face"
			if button.text:GetText() ~= wanted then
				button.text:SetText(wanted)
			end
		end
	end)
end
