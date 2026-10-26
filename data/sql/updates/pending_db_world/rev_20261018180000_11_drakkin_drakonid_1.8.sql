-- Esteria Drakkin / Drakonid (C:/CoA-Build/esteria_racials/gen_race_ports.py): re-runnable
DELETE FROM `playercreateinfo` WHERE `race` IN (98,99,100);
INSERT INTO `playercreateinfo` SELECT 98,`class`,`map`,`zone`,`position_x`,`position_y`,`position_z`,`orientation` FROM `playercreateinfo` WHERE `race`=2
UNION ALL
SELECT 99,`class`,`map`,`zone`,`position_x`,`position_y`,`position_z`,`orientation` FROM `playercreateinfo` WHERE `race`=1
UNION ALL
SELECT 100,`class`,`map`,`zone`,`position_x`,`position_y`,`position_z`,`orientation` FROM `playercreateinfo` WHERE `race`=1;

DELETE FROM `playercreateinfo_item` WHERE `race` IN (98,99,100);
INSERT INTO `playercreateinfo_item` SELECT 98,`class`,`itemid`,`amount`,`Note` FROM `playercreateinfo_item` WHERE `race`=2
UNION ALL
SELECT 99,`class`,`itemid`,`amount`,`Note` FROM `playercreateinfo_item` WHERE `race`=1
UNION ALL
SELECT 100,`class`,`itemid`,`amount`,`Note` FROM `playercreateinfo_item` WHERE `race`=1;

DELETE FROM `playercreateinfo_action` WHERE `race` IN (98,99,100);
INSERT INTO `playercreateinfo_action` SELECT 98,`class`,`button`,`action`,`type` FROM `playercreateinfo_action` WHERE `race`=2
UNION ALL
SELECT 99,`class`,`button`,`action`,`type` FROM `playercreateinfo_action` WHERE `race`=1
UNION ALL
SELECT 100,`class`,`button`,`action`,`type` FROM `playercreateinfo_action` WHERE `race`=1;

DELETE FROM `player_race_stats` WHERE `Race` IN (98,99,100);
INSERT INTO `player_race_stats` SELECT 98,`Strength`,`Agility`,`Stamina`,`Intellect`,`Spirit` FROM `player_race_stats` WHERE `Race`=2
UNION ALL
SELECT 99,`Strength`,`Agility`,`Stamina`,`Intellect`,`Spirit` FROM `player_race_stats` WHERE `Race`=1
UNION ALL
SELECT 100,`Strength`,`Agility`,`Stamina`,`Intellect`,`Spirit` FROM `player_race_stats` WHERE `Race`=1;

DELETE FROM `ascension_custom_class_race` WHERE `race` IN (98,99,100);
INSERT INTO `ascension_custom_class_race` SELECT DISTINCT `class`,98 FROM `ascension_custom_class_race`
UNION ALL
SELECT DISTINCT `class`,99 FROM `ascension_custom_class_race`
UNION ALL
SELECT DISTINCT `class`,100 FROM `ascension_custom_class_race`;

-- 2026-10-18: Alliance dragon races (Dracthyr 54, Drakkin 99, Drakonid 100) start at the Night Elf start (Shadowglen)
UPDATE `playercreateinfo` SET `map`=1, `zone`=141, `position_x`=10311.3, `position_y`=832.463, `position_z`=1326.41, `orientation`=5.69632 WHERE `race` IN (54,99,100) AND `class` <> 6;
