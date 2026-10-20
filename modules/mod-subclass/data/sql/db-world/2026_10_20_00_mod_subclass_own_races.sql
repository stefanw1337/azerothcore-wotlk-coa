-- mod-subclass: our own races. Thin Human (96) is a male copy of race 32, Two-Headed Ogre (85) a female copy of
-- race 53 with her own display 977600; the subclass rules move from 32/53 to 96/85.

DELETE FROM `playercreateinfo` WHERE `race` = 96;
INSERT INTO `playercreateinfo`
SELECT 96, `class`, `map`, `zone`, `position_x`, `position_y`, `position_z`, `orientation`
FROM `playercreateinfo` WHERE `race` = 32;

DELETE FROM `playercreateinfo` WHERE `race` = 85;
INSERT INTO `playercreateinfo`
SELECT 85, `class`, `map`, `zone`, `position_x`, `position_y`, `position_z`, `orientation`
FROM `playercreateinfo` WHERE `race` = 53;

DELETE FROM `playercreateinfo_item` WHERE `race` = 96;
INSERT INTO `playercreateinfo_item`
SELECT 96, `class`, `itemid`, `amount`, `Note`
FROM `playercreateinfo_item` WHERE `race` = 32;

DELETE FROM `playercreateinfo_item` WHERE `race` = 85;
INSERT INTO `playercreateinfo_item`
SELECT 85, `class`, `itemid`, `amount`, `Note`
FROM `playercreateinfo_item` WHERE `race` = 53;

DELETE FROM `playercreateinfo_action` WHERE `race` = 96;
INSERT INTO `playercreateinfo_action`
SELECT 96, `class`, `button`, `action`, `type`
FROM `playercreateinfo_action` WHERE `race` = 32;

DELETE FROM `playercreateinfo_action` WHERE `race` = 85;
INSERT INTO `playercreateinfo_action`
SELECT 85, `class`, `button`, `action`, `type`
FROM `playercreateinfo_action` WHERE `race` = 53;

DELETE FROM `player_race_stats` WHERE `Race` = 96;
INSERT INTO `player_race_stats`
SELECT 96, `Strength`, `Agility`, `Stamina`, `Intellect`, `Spirit`
FROM `player_race_stats` WHERE `Race` = 32;

DELETE FROM `player_race_stats` WHERE `Race` = 85;
INSERT INTO `player_race_stats`
SELECT 85, `Strength`, `Agility`, `Stamina`, `Intellect`, `Spirit`
FROM `player_race_stats` WHERE `Race` = 53;

DELETE FROM `ascension_custom_class_race` WHERE `race` = 96;
INSERT INTO `ascension_custom_class_race`
SELECT DISTINCT `class`, 96
FROM `ascension_custom_class_race` WHERE `race` = 32;

DELETE FROM `ascension_custom_class_race` WHERE `race` = 85;
INSERT INTO `ascension_custom_class_race`
SELECT DISTINCT `class`, 85
FROM `ascension_custom_class_race` WHERE `race` = 53;

DELETE FROM `creature_model_info` WHERE `DisplayID` = 977600;
INSERT INTO `creature_model_info` (`DisplayID`, `BoundingRadius`, `CombatReach`, `Gender`, `DisplayID_Other_Gender`)
SELECT 977600, `BoundingRadius`, `CombatReach`, 1, 0
FROM `creature_model_info` WHERE `DisplayID` = 977468;

DELETE FROM `mod_subclass_pair` WHERE `race` IN (85, 96) OR (`race` = 53 AND `gender_mask` = 2) OR
    (`race` = 32 AND `gender_mask` = 1);
INSERT INTO `mod_subclass_pair` (`race`, `gender_mask`, `main_class`, `sub_class`, `sub_spec`, `enabled`, `comment`)
VALUES
(85, 2, 0, 0, 0, 1, 'Two-Headed Ogre (female): adds another class tree and spec'),
(96, 1, 0, 0, 0, 1, 'Thin Twins (male Thin Humans): the second twin adds another class tree and spec');
DELETE FROM `mod_subclass_story` WHERE `race` IN (85, 96);
UPDATE `mod_subclass_story` SET `race` = 85 WHERE `race` = 53 AND `gender_mask` = 2;
UPDATE `mod_subclass_story` SET `race` = 96 WHERE `race` = 32 AND `gender_mask` = 1;
