-- Written by tools\mktokenring.py: each flavour's token ring (grey textures, coloured by spec in the game)
-- and where its holes are (0..1 across the texture, from the top left; clockwise from the top), with a
-- hole's radius on the same scale.
SKIN_TOKEN_ART = {
	possessor = {
		ring = "Interface\\AddOns\\SkinCollection\\Textures\\TokenRing_Possessor",
		flames = "Interface\\AddOns\\SkinCollection\\Textures\\TokenFlames_Possessor",
		holeRadius = 0.0295,
		tint = true,  -- true: the ring itself takes the spec's colour
		holes = { { 0.5024, 0.2322 }, { 0.5885, 0.2433 }, { 0.6563, 0.2833 }, { 0.7181, 0.3371 }, { 0.7521, 0.4123 }, { 0.7656, 0.4974 }, { 0.7561, 0.5816 }, { 0.7189, 0.6586 }, { 0.6639, 0.7195 }, { 0.5848, 0.7602 }, { 0.4946, 0.7736 }, { 0.4110, 0.7598 }, { 0.3355, 0.7194 }, { 0.2818, 0.6568 }, { 0.2413, 0.5815 }, { 0.2313, 0.4982 }, { 0.2450, 0.4153 }, { 0.2836, 0.3407 }, { 0.3465, 0.2809 }, { 0.4159, 0.2418 } },
	},
	skinshifter = {
		ring = "Interface\\AddOns\\SkinCollection\\Textures\\TokenRing_Skinshifter",
		flames = "Interface\\AddOns\\SkinCollection\\Textures\\TokenFlames_Skinshifter",
		holeRadius = 0.0338,
		tint = false,  -- true: the ring itself takes the spec's colour
		holes = { { 0.5002, 0.1990 }, { 0.5944, 0.2147 }, { 0.6777, 0.2578 }, { 0.7445, 0.3246 }, { 0.7889, 0.4078 }, { 0.8031, 0.4995 }, { 0.7882, 0.5928 }, { 0.7466, 0.6766 }, { 0.6778, 0.7428 }, { 0.5940, 0.7860 }, { 0.4998, 0.8008 }, { 0.4056, 0.7856 }, { 0.3209, 0.7418 }, { 0.2537, 0.6754 }, { 0.2111, 0.5914 }, { 0.1955, 0.4987 }, { 0.2121, 0.4072 }, { 0.2550, 0.3235 }, { 0.3228, 0.2567 }, { 0.4064, 0.2139 } },
	},
}
