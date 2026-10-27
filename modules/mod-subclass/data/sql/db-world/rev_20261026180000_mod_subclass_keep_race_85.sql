-- mod-subclass: race 85 is the Two-Headed Ogre (reserved for this module). CoA Custom 1.9.1's
-- rev_20261025070000_custom_races_19.sql writes its own creation rows for 85; this file sorts after it and copies
-- the Ogre's rows from race 53 again, without the Hero class.
DELETE FROM `playercreateinfo` WHERE `race` = 85;
INSERT INTO `playercreateinfo`
SELECT 85, `class`, `map`, `zone`, `position_x`, `position_y`, `position_z`, `orientation`
FROM `playercreateinfo` WHERE `race` = 53 AND `class` <> 10;

DELETE FROM `playercreateinfo_item` WHERE `race` = 85;
INSERT INTO `playercreateinfo_item`
SELECT 85, `class`, `itemid`, `amount`, `Note`
FROM `playercreateinfo_item` WHERE `race` = 53 AND `class` <> 10;

DELETE FROM `playercreateinfo_action` WHERE `race` = 85;
INSERT INTO `playercreateinfo_action`
SELECT 85, `class`, `button`, `action`, `type`
FROM `playercreateinfo_action` WHERE `race` = 53 AND `class` <> 10;

DELETE FROM `player_race_stats` WHERE `Race` = 85;
INSERT INTO `player_race_stats`
SELECT 85, `Strength`, `Agility`, `Stamina`, `Intellect`, `Spirit`
FROM `player_race_stats` WHERE `Race` = 53;

DELETE FROM `ascension_custom_class_race` WHERE `race` = 85;
INSERT INTO `ascension_custom_class_race`
SELECT DISTINCT `class`, 85
FROM `ascension_custom_class_race` WHERE `race` = 53;
