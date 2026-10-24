-- Eyes In The Night (806723): "Party and raid members within $a1 yds of you are protected by Shadow Colony,
-- giving them a $h% chance to trigger a Call of the Darkwing when they take direct damage. This effect can only
-- occur once every 5 sec." Effect 0 is SPELL_EFFECT_APPLY_AREA_AURA_RAID (65) with aura 42 triggering Call of
-- the Darkwing 801958, but Spell.dbc gives the record ProcFlags 4 (PROC_FLAG_DONE_MELEE_AUTO_ATTACK) and no
-- `spell_proc` row existed, so every protected unit's own white swings rolled the 40% instead of the hits it took,
-- with no cooldown at all.
--
-- The Shadow Bats themselves are protected units: Call of the Darkwing summons them as the Bloodmage's guardians
-- (SummonProperties 61), Unit::IsInPartyWith treats a guardian as grouped with its owner, and
-- HandleProcTriggerSpellAuraProc casts the triggered Call with the aura caster as original caster, so every bat a
-- bat called was again the Bloodmage's and again protected. Each bat swings every 2 sec for its 15 sec, so one bat
-- called about 4.5 more and the swarm grew without bound in any fight. aura_ascension_bloodmage_eyes_in_the_night
-- keeps Shadow Colony off the Shadow Bats (creature 50069).
--
-- `ProcFlags` 139944 = TAKEN_MELEE_AUTO_ATTACK 0x8 | TAKEN_SPELL_MELEE_DMG_CLASS 0x20 |
-- TAKEN_RANGED_AUTO_ATTACK 0x80 | TAKEN_SPELL_RANGED_DMG_CLASS 0x200 | TAKEN_SPELL_NONE_DMG_CLASS_NEG 0x2000 |
-- TAKEN_SPELL_MAGIC_DMG_CLASS_NEG 0x20000: direct damage from any attack class. `SpellTypeMask` 1 (DAMAGE) and
-- `SpellPhaseMask` 2 (HIT) leave periodic ticks out. `HitMask` 0 keeps the taken defaults (NORMAL|CRITICAL).
-- `Cooldown` 5000 is the tooltip's "once every 5 sec". `Chance` 0 defers to the record's own ProcChance 40.
DELETE FROM `spell_proc` WHERE `SpellId` = 806723;
INSERT INTO `spell_proc` (`SpellId`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`, `SpellFamilyMask2`, `ProcFlags`, `SpellTypeMask`, `SpellPhaseMask`, `HitMask`, `AttributesMask`, `DisableEffectsMask`, `ProcsPerMinute`, `Chance`, `Cooldown`, `Charges`) VALUES
(806723, 0, 0, 0, 0, 0, 139944, 1, 2, 0, 0, 0, 0, 0, 5000, 0);

DELETE FROM `spell_script_names` WHERE `spell_id` = 806723 AND `ScriptName` = 'aura_ascension_bloodmage_eyes_in_the_night';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(806723, 'aura_ascension_bloodmage_eyes_in_the_night');
