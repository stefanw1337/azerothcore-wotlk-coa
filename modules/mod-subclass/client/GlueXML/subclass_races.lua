
-- mod-subclass races: Thin Human (96, male) and Two-Headed Ogre (85, female)
CHAR_CREATE_LEVEL_REQUIREMENT = "Death Knights and Hero Races need one of your characters on this realm to reach a higher level first."
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
		{"Twin-Linked", "Spell_Holy_SealOfSacrifice", "passive", "Twin-Link powers ensure they have one shared health pool. All crowd control effects will affect both at the same time. If resource pools are of different types, Twin-Link powers will make sure they are equal in power."},
		{"Brother from the Same Mother", "Spell_Holy_PrayerofSpirit", "passive", "Your brother has a resource pool of his own. If you both use the same resource type, the pool is twice as big, like your Mother. But it is split in two, like her behind. Each Thin Twin spends from his own half, and buffs and effects that refill the pool fill both halves."},
		{"Desperate Flight", "Ability_Rogue_Sprint", "active", "Removes roots and snares and increases movement speed by 20% for 6 sec."},
		{"Wiry Frame", "Ability_Racial_ShadowMeld", "passive", "Dodge chance increased by 1%."},
		{"Nervous Energy", "Ability_Rogue_Sprint", "passive", "Haste increased by 1%."},
		{"Make Do", "INV_Misc_Fork&Knife", "passive", "Food, drink and bandages restore 10% more health or mana."},
		{"Field Dressing", "Spell_Holy_SealOfSacrifice", "passive", "First Aid skill increased by 15."},
	}
	CHAR_CREATE_COA_RACIALS[85] = {
		{"Agree or Disagree", "Spell_Shadow_BrainWash", "active", "Your two heads agree for once, or go back to arguing. From level 20 the second head can follow a class of its own."},
		{"Shared Blood", "Spell_Deathknight_BloodPresence", "passive", "Your body shares blood, one health pool. When both heads use the same resource pool type, they will share one bigger pool of it. It will be twice as big and refill twice as fast. If the resource pools are of different types, they will also share their strength with each other."},
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
	CHAR_CREATE_RACE_LORE.alliance["Thin Human"] = "|cffffd100Hero Race.|r " .. [==[Born in the shadow of Stormwind's walls to a stonemason who helped rebuild the city and a Kul Tiran sailor's daughter, the Thin Humans are lean, wiry folk who learned early to slip through crowds and out of trouble. Most of them travel alone. A few were born a pair, and somewhere out there a brother is waiting for the day his twin finds their father's lost Bravery Badge.]==]
	CHAR_CREATE_RACE_LORE.horde["Two-Headed Ogre"] = "|cffffd100Hero Race.|r " .. [==[Two heads, one body, and never a moment's peace. The two-headed ogres of the Horde are mighty brawlers whose heads argue over everything from breakfast to battle plans. When they do agree, they hit twice as hard. Legend says an old book of origins hidden in Shadowfang Keep can teach the second head a craft of its own.]==]
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
	local placed, seen, steady, unscaled, cleared
	selectWatch:SetScript("OnUpdate", function(self, elapsed)
		self.wait = (self.wait or 0) + elapsed
		if self.wait < 0.2 then return end
		self.wait = 0
		if not (CharacterSelect and CharacterSelect:IsShown() and CharacterSelect.selectedIndex
			and C_CharacterCreate and C_CharacterCreate.GetCameraPosition) then
			seen, steady, unscaled, cleared = nil, 0, nil, nil
			return
		end
		if not unscaled and CharacterSelect.SetCamDistanceScale then
			pcall(CharacterSelect.SetCamDistanceScale, CharacterSelect, 1)
		end
		unscaled = true
		local ok, _, race = pcall(GetCharacterInfo, GetCharIDFromIndex and GetCharIDFromIndex(CharacterSelect.selectedIndex) or CharacterSelect.selectedIndex)
		local k = ok and COA_SUBCLASS_SELECT_SCALES[race or ""]
		if not k then
			if placed and C_CharacterCreate.ResetCameraPosition then
				pcall(C_CharacterCreate.ResetCameraPosition)
			end
			placed = nil
			return
		end
		if not placed and not cleared and C_CharacterCreate.ResetCameraPosition then
			pcall(C_CharacterCreate.ResetCameraPosition)
			cleared, seen, steady = true, nil, 0
			return
		end
		local got, x, y, z = pcall(C_CharacterCreate.GetCameraPosition)
		if not got or not x then return end
		local now = { x, y, z }
		if placed and Near(now, placed.target) then
			return
		end
		steady = Near(now, seen) and (steady or 0) + 1 or 0
		seen = now
		if steady < 1 then return end
		local target = { x * k, y * k, z * (1 + (k - 1) * 0.6) }
		placed = { target = target }
		pcall(C_CharacterCreate.SetCameraPosition, target[1], target[2], target[3])
	end)
end

HAIR_EYES_STYLE = ""
HAIR_EYES_COLOR = ""
COA_SUBCLASS_EYE_RACES = { ["Two-Headed Ogre"] = true }
COA_SUBCLASS_EYES = { plain = 15, mixed = 7, firstMixed = 120, firstMixedBlind = 127,
	variants = { 4, 1, 2, 3, 5, 6, 7 }, blind = { "Left", "Middle", "Right" } }
do
	local CUSTOMIZE_FACE = 2
	local EYES = COA_SUBCLASS_EYES
	local LOOKS = EYES.plain + EYES.mixed
	local VARIANTS = #EYES.variants
	local cycle = C_CharacterCreate and C_CharacterCreate.CycleCustomizationSkipLocked
	local function EyesInsteadOfFace()
		if not (CharacterCreateFrame and CharacterCreateFrame:IsVisible()) then return end
		local ok, name = pcall(GetNameForRace)
		return ok and name and COA_SUBCLASS_EYE_RACES[name]
	end
	local function Blind(mask, eye)
		return mask % (eye * 2) >= eye
	end
	local function EyeIndex(look, mask)
		local variant
		for index, bits in ipairs(EYES.variants) do
			if bits == mask then
				variant = index - 1
			end
		end
		if look < EYES.plain then
			return variant and EYES.plain + look * VARIANTS + variant or look
		end
		if variant then
			return EYES.firstMixedBlind + (look - EYES.plain) * VARIANTS + variant
		end
		return EYES.firstMixed + look - EYES.plain
	end
	local function EyeLook(index)
		if index < EYES.plain then
			return index, 0
		elseif index < EYES.firstMixed then
			local offset = index - EYES.plain
			return math.floor(offset / VARIANTS), EYES.variants[offset % VARIANTS + 1]
		elseif index < EYES.firstMixedBlind then
			return EYES.plain + index - EYES.firstMixed, 0
		end
		local offset = index - EYES.firstMixedBlind
		return EYES.plain + math.floor(offset / VARIANTS), EYES.variants[offset % VARIANTS + 1]
	end
	local function CurrentEyes()
		local ok, value, count = pcall(CycleCharCustomization, "EA_STOCK_GET", CHAR_CUSTOMIZE_HAIR_COLOR)
		if ok and type(value) == "number" and type(count) == "number" and count > 0 then
			return value, count
		end
	end
	local function StepEyes(direction)
		if cycle then
			cycle(CHAR_CUSTOMIZE_HAIR_COLOR, direction)
		else
			CycleCharCustomization(CHAR_CUSTOMIZE_HAIR_COLOR, direction)
		end
	end
	local function WearEyes(target)
		local value, count = CurrentEyes()
		if not value then
			return false
		end
		local direction = (target - value) % count <= count / 2 and 1 or -1
		for _ = 1, count do
			if value == target then
				break
			end
			StepEyes(direction)
			local now = CurrentEyes()
			if not now or now == value then
				break
			end
			value = now
		end
		return true
	end
	local function ChangeEyes(lookStep, eye)
		local value = CurrentEyes()
		if not value then
			return false
		end
		local look, mask = EyeLook(value)
		look = (look + lookStep) % LOOKS
		if eye then
			mask = Blind(mask, eye) and mask - eye or mask + eye
		end
		return WearEyes(EyeIndex(look, mask))
	end
	if cycle then
		C_CharacterCreate.CycleCustomizationSkipLocked = function(id, direction, ...)
			if (id == CUSTOMIZE_FACE or id == CHAR_CUSTOMIZE_HAIR_COLOR) and EyesInsteadOfFace() then
				if ChangeEyes((direction or 1) < 0 and -1 or 1) then
					return
				end
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
	local strip
	local function BlindStrip(row)
		if strip then
			return strip
		end
		strip = CreateFrame("Frame", "CoASubclassBlindEyes", row:GetParent())
		strip:SetSize(250, 24)
		strip.label = strip:CreateFontString(nil, "ARTWORK", "GlueFontNormalSmall")
		strip.label:SetPoint("LEFT", strip, "LEFT", 0, 0)
		strip.label:SetText("Blind:")
		strip.boxes = {}
		local previous
		for index, label in ipairs(EYES.blind) do
			local box = CreateFrame("CheckButton", nil, strip)
			box:SetSize(24, 24)
			box:SetPoint("LEFT", previous and previous.text or strip.label, "RIGHT", 6, 0)
			box:SetNormalTexture("Interface\\Buttons\\UI-CheckBox-Up")
			box:SetPushedTexture("Interface\\Buttons\\UI-CheckBox-Down")
			box:SetHighlightTexture("Interface\\Buttons\\UI-CheckBox-Highlight", "ADD")
			box:SetCheckedTexture("Interface\\Buttons\\UI-CheckBox-Check")
			box.text = box:CreateFontString(nil, "ARTWORK", "GlueFontHighlightSmall")
			box.text:SetPoint("LEFT", box, "RIGHT", 0, 1)
			box.text:SetText(label)
			box.eye = 2 ^ (index - 1)
			box:SetScript("OnClick", function(self)
				PlaySound("gsCharacterCreationLook")
				ChangeEyes(0, self.eye)
				local value = CurrentEyes()
				local _, mask = EyeLook(value or 0)
				self:SetChecked(value ~= nil and Blind(mask, self.eye))
			end)
			strip.boxes[index] = box
			previous = box
		end
		strip:SetPoint("TOP", row, "BOTTOM", 0, -2)
		return strip
	end
	local labels = CreateFrame("Frame")
	labels:SetScript("OnUpdate", function(self, elapsed)
		self.wait = (self.wait or 0) + elapsed
		if self.wait < 0.2 then return end
		self.wait = 0
		local button = CharCreateCustomizationButton2
		local eyes = EyesInsteadOfFace()
		if button and button.text and button:IsVisible() then
			local wanted = eyes and "Eye Color" or "Face"
			if button.text:GetText() ~= wanted then
				button.text:SetText(wanted)
			end
		end
		local value = eyes and button and button:IsVisible() and CurrentEyes()
		if not value then
			if strip then
				strip:Hide()
			end
			return
		end
		local shown = BlindStrip(button)
		local _, mask = EyeLook(value)
		for _, box in ipairs(shown.boxes) do
			box:SetChecked(Blind(mask, box.eye))
		end
		shown:Show()
	end)
end
do
	local REALM_ONLINE = "Realm is Online"
	local SELECT_REALM = "Select Realm"
	local function ShownText(region)
		return region and region.IsVisible and region:IsVisible() and region.GetText and region:GetText()
	end
	local function FindShown(objectType, wanted)
		if not EnumerateFrames then return end
		local frame = EnumerateFrames()
		while frame do
			if frame:IsVisible() then
				if objectType == "Button" and frame:GetObjectType() == "Button" and ShownText(frame) == wanted then
					return frame
				end
				if objectType == "FontString" then
					for _, region in ipairs({ frame:GetRegions() }) do
						local text = region.GetObjectType and region:GetObjectType() == "FontString" and ShownText(region)
						if text and string.find(text, wanted, 1, true) then
							return region
						end
					end
				end
			end
			frame = EnumerateFrames(frame)
		end
	end
	local realmWatch = CreateFrame("Frame")
	realmWatch:SetScript("OnUpdate", function(self, elapsed)
		self.wait = (self.wait or 0) + elapsed
		if self.wait < 0.5 or COA_SUBCLASS_REALM_ENTERED then return end
		self.wait = 0
		local button = FindShown("Button", SELECT_REALM)
		if button and button:IsEnabled() and FindShown("FontString", REALM_ONLINE) then
			COA_SUBCLASS_REALM_ENTERED = true
			button:Click()
		end
	end)
end
do
	local SUBCLASS_CVAR = "coaSubclassNames"
	local function Plain(text)
		return (string.gsub(string.gsub(text, "|c%x%x%x%x%x%x%x%x", ""), "|r", ""))
	end
	local function Subclasses()
		local found = {}
		local ok, value = pcall(GetCVar, SUBCLASS_CVAR)
		for key, subclass in string.gmatch(ok and value or "", "([^;=]+)=([^;]+)") do
			found[key] = subclass
		end
		return found
	end
	local function TextsIn(frame, found)
		for _, region in ipairs({ frame:GetRegions() }) do
			if region.GetObjectType and region:GetObjectType() == "FontString" and region:IsVisible() and region:GetText() then
				found[#found + 1] = region
			end
		end
		for _, child in ipairs({ frame:GetChildren() }) do
			TextsIn(child, found)
		end
		return found
	end
	local function NameLabels(names)
		local labels = {}
		local frame = EnumerateFrames()
		while frame do
			if frame:IsVisible() then
				for _, region in ipairs({ frame:GetRegions() }) do
					local text = region.GetObjectType and region:GetObjectType() == "FontString" and region:IsVisible()
						and region:GetText()
					if text and names[Plain(text)] then
						labels[#labels + 1] = { region = region, name = Plain(text) }
					end
				end
			end
			frame = EnumerateFrames(frame)
		end
		return labels
	end
	local function WithSubclass(text, class, subclass)
		local tag = "(" .. subclass .. ")"
		local _, last = string.find(text, class, 1, true)
		if not last or string.find(text, tag, 1, true) then return end
		return string.sub(text, 1, last) .. " " .. tag .. string.sub(text, last + 1)
	end
	local function ClassLinesOf(label, everyone, class)
		local holder = label.region:GetParent()
		while holder and holder ~= CharacterSelect do
			local lines, crowded = {}, false
			for _, line in ipairs(TextsIn(holder, {})) do
				local text = Plain(line:GetText())
				if line ~= label.region and everyone[text] and text ~= label.name then
					crowded = true
				elseif line ~= label.region and string.find(text, class, 1, true) then
					lines[#lines + 1] = line
				end
			end
			if crowded then return {} end
			if #lines > 0 then return lines end
			holder = holder:GetParent()
		end
		return {}
	end
	local listWatch = CreateFrame("Frame")
	listWatch:SetScript("OnUpdate", function(self, elapsed)
		self.wait = (self.wait or 0) + elapsed
		if self.wait < 0.3 then return end
		self.wait = 0
		if not (CharacterSelect and CharacterSelect:IsShown() and GetNumCharacters and EnumerateFrames) then return end
		local subclasses = Subclasses()
		if not next(subclasses) then return end
		local realm = GetServerName and GetServerName() or ""
		local everyone, subclassed = {}, {}
		for index = 1, GetNumCharacters() do
			local name, _, class = GetCharacterInfo(index)
			if name then
				everyone[name] = true
				local subclass = subclasses[name .. "@" .. realm]
				if subclass and class then
					subclassed[name] = { class = class, subclass = subclass }
				end
			end
		end
		if not next(subclassed) then return end
		for _, label in ipairs(NameLabels(subclassed)) do
			local wanted = subclassed[label.name]
			for _, line in ipairs(ClassLinesOf(label, everyone, wanted.class)) do
				local shown = WithSubclass(line:GetText(), wanted.class, wanted.subclass)
				if shown then
					line:SetText(shown)
				end
			end
		end
	end)
end
