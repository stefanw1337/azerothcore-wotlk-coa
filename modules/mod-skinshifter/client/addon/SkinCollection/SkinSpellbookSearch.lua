-- The spellbook search hides what does not match (CoA's only dims it) and pages through the matches. Every word
-- must match, in the name, rank, description or the extra lines of a tooltip already seen; "-word" excludes.
-- Where: the Skins tab by default; /skinsearch all (every tab, any class), /skinsearch skins, /skinsearch off.

SkinCollectionDB = SkinCollectionDB or {}

local SKIN_TABS = { Skins = true }

local function Mode()
	return SkinCollectionDB.searchFilter or "skins"
end

local function Filters(tab)
	local mode = Mode()
	return mode == "all" or (mode == "skins" and tab and SKIN_TABS[tab.tooltip or ""]) or false
end

local function Words(text)
	local wanted, unwanted = {}, {}
	for word in string.gmatch(text, "%S+") do
		if #word > 1 and word:sub(1, 1) == "-" then
			tinsert(unwanted, word:sub(2))
		else
			tinsert(wanted, word)
		end
	end
	return wanted, unwanted
end

local function Matches(slot, wanted, unwanted)
	local name, rank = GetSpellInfo(slot, BOOKTYPE_SPELL)
	if not name then return false end
	local id = C_Spell and C_Spell.GetSpellID and C_Spell:GetSpellID(slot, BOOKTYPE_SPELL)
	local text = strlower(name .. " " .. (rank or "") .. " " .. (id and GetSpellDescription(id) or ""))
	if id and SkinTooltips_Text then
		local extra = SkinTooltips_Text(id)
		if extra then
			text = text .. " " .. extra  -- once the spell's tooltip has been seen
		end
	end
	for _, word in ipairs(wanted) do
		if not text:find(word, 1, true) then return false end
	end
	for _, word in ipairs(unwanted) do
		if text:find(word, 1, true) then return false end
	end
	return true
end

local function Refilter(book)
	if book.bookType ~= BOOKTYPE_SPELL or strlen(book.searchText or "") == 0 then return end
	local tab = book.selectedSideTab
	if not tab or not tab.HasInfo or not Filters(tab) then return end
	local showAllRanks = GetCVarBool("ShowAllSpellRanks") and C_Spell.HasMultipleSpellRanks()
	-- the matches are kept until the search or the tab changes: the book updates often (SPELLS_CHANGED), and reading
	-- thousands of descriptions each time stalled the game
	local key = table.concat({ book.searchText, tab.offset, tab.numSpells, tostring(showAllRanks), GetNumSpellTabs() }, "|")
	local found = book.SkinSearchKey == key and book.SkinSearchFound
	if not found then
		local wanted, unwanted = Words(book.searchText)
		found = {}
		for index = tab.offset + 1, tab.offset + tab.numSpells do
			local slot = showAllRanks and index or GetKnownSlotFromHighestRankSlot(index)
			if slot and slot > 0 and Matches(slot, wanted, unwanted) then
				tinsert(found, slot)
			end
		end
		book.SkinSearchKey, book.SkinSearchFound = key, found
	end
	local pages = math.max(1, math.ceil(#found / 12))
	if book.spellPage > pages then book.spellPage = pages end
	local first = (book.spellPage - 1) * 12
	for index = 1, 12 do
		local button = book.Content.Spells["SpellButton" .. index]
		if found[first + index] then
			button:SetSpell(found[first + index], BOOKTYPE_SPELL)
		else
			button:SetSpell()
		end
	end
	book.PreviousPageButton:SetEnabled(book.spellPage > 1)
	book.NextPageButton:SetEnabled(book.spellPage < pages)
end

-- CoA lights every side tab that holds a match by reading the descriptions of all its spells, on every book update; with
-- thousands of stolen abilities that stalled the game. The same answer, worked out once per search and tab.
local tabHits, tabHitsText = {}, nil
local function WrapSideTab(tab)
	if not tab or tab.SkinSearchWrapped or type(tab.SetInfo) ~= "function" then return end
	tab.SkinSearchWrapped = true
	local original = tab.SetInfo
	tab.SetInfo = function(self, id, ...)
		local book = AscensionSpellbookFrame
		local text = book and book.searchText or ""
		if not id or text == "" then
			return original(self, id, ...)
		end
		book.searchText = ""  -- CoA's own scan skipped; the tab is set up as without a search
		original(self, id, ...)
		book.searchText = text
		if tabHitsText ~= text then
			tabHits, tabHitsText = {}, text
		end
		local showAllRanks = GetCVarBool("ShowAllSpellRanks") and C_Spell.HasMultipleSpellRanks()
		local key = table.concat({ id, self.offset or 0, self.numSpells or 0, tostring(showAllRanks) }, "|")
		local hit = tabHits[key]
		if hit == nil then
			hit = false
			local wanted, unwanted = Words(text)
			for index = (self.offset or 0) + 1, (self.offset or 0) + (self.numSpells or 0) do
				local slot = showAllRanks and index or GetKnownSlotFromHighestRankSlot(index)
				if slot and slot > 0 and Matches(slot, wanted, unwanted) then
					hit = true
					break
				end
			end
			tabHits[key] = hit
		end
		self.NormalTexture:SetDesaturated(not hit)
		if hit then self.SearchHighlight:Show() else self.SearchHighlight:Hide() end
	end
end

local checkbox
local function Install()
	local book = AscensionSpellbookFrame
	if not book or book.SkinSearchInstalled then return end
	book.SkinSearchInstalled = true
	hooksecurefunc(book, "UpdateSpells", Refilter)
	if book.SideBar and book.SideBar.Tabs then
		for _, tab in ipairs(book.SideBar.Tabs) do WrapSideTab(tab) end
	end
	-- a new search starts on its first page
	hooksecurefunc(book, "UpdateSearch", function(self)
		if self.SkinSearchLast ~= self.searchText then
			self.SkinSearchLast = self.searchText
			if self.spellPage ~= 1 then
				self.spellPage = 1
				self:UpdateSpells()
			end
		end
	end)
	local search = book.Content and book.Content.Spells and book.Content.Spells.Search
	if search then
		checkbox = CreateFrame("CheckButton", nil, search, "UICheckButtonTemplate")
		checkbox:SetSize(22, 22)
		checkbox:SetPoint("LEFT", search, "RIGHT", 4, 0)
		local label = checkbox:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
		label:SetPoint("LEFT", checkbox, "RIGHT", 0, 1)
		label:SetText("All tabs")
		checkbox:SetScript("OnEnter", function(self)
			GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
			GameTooltip:SetText("Search hides spells that do not match")
			GameTooltip:AddLine("Ticked: in every tab. Unticked: in the Skins tab only.", 1, 1, 1, true)
			GameTooltip:AddLine("Every word must match; -word leaves those out.", 1, 1, 1, true)
			GameTooltip:Show()
		end)
		checkbox:SetScript("OnLeave", GameTooltip_Hide)
		checkbox:SetChecked(Mode() == "all")
		checkbox:SetScript("OnClick", function(self)
			SkinCollectionDB.searchFilter = self:GetChecked() and "all" or "skins"
			book:UpdateSpells()
		end)
	end
end

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:SetScript("OnEvent", Install)
Install()

SLASH_SKINSEARCH1 = "/skinsearch"
SlashCmdList.SKINSEARCH = function(text)
	text = strlower(strtrim(text or ""))
	if text == "all" or text == "skins" or text == "off" then
		SkinCollectionDB.searchFilter = text
		if checkbox then checkbox:SetChecked(text == "all") end
		if AscensionSpellbookFrame and AscensionSpellbookFrame:IsShown() then AscensionSpellbookFrame:UpdateSpells() end
	end
	print("Spellbook search hides non-matching spells in: " ..
		({ all = "every tab", skins = "the Skins tab", off = "no tab (CoA's dimming only)" })[Mode()] ..
		". /skinsearch all | skins | off")
end
