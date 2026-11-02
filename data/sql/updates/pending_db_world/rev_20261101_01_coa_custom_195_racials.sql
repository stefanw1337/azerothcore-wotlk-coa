-- CoA Custom 1.9.5 (ilusixn, CustomRacesCoa races-port 8984c8b8, races-extras/release/16_1.9.5_x64.sql)
REPLACE INTO `spell_dbc` VALUES (110690,0,0,0,16,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,5000,0,0,0,0,0,101,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,3,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4228,0,5772,0,0,'Two Forms',NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,16712190,'Racial',NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,16712190,'Switch between your human and worgen forms.',NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,16712190,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,16712188,0,133,1500,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0);
DELETE FROM `spell_script_names` WHERE `spell_id` IN (14000132, 110690);
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES (110690, 'spell_race_two_forms');

-- Gnoll (race 83) active racial: Scavenger, a copy of Cannibalize (spell 14000832, formerly "Bone Gnaw"; script spell_gen_cannibalize).
-- The race's racial list (CoaCustomRacials.h) now includes it; the client Spell.dbc row is renamed in patch-ZZZZD.
UPDATE spell_dbc SET Name_Lang_enUS = 'Scavenger',
  Description_Lang_enUS = 'A gnoll eats what it kills.\n\nWhen activated, regenerates $20578s1% of total health every $20578t1 sec for $20578d.  Only works on Humanoid or Undead corpses within $a1 yds.  Any movement, action, or damage taken while Scavenging will cancel the effect.'
  WHERE ID = 14000832;
INSERT IGNORE INTO spell_script_names (spell_id, ScriptName) VALUES (14000832, 'spell_gen_cannibalize');
-- the Gnoll's old passive "Scavenger" (+5 Skinning) is now "Skinner"
UPDATE spell_dbc SET Name_Lang_enUS = 'Skinner' WHERE ID = 110923;
