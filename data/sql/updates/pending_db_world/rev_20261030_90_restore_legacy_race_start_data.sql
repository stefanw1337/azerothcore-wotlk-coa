START TRANSACTION;

INSERT INTO `playercreateinfo` (`race`, `class`, `map`, `zone`, `position_x`, `position_y`, `position_z`, `orientation`)
SELECT r.`race`, s.`class`, s.`map`, s.`zone`, s.`position_x`, s.`position_y`, s.`position_z`, s.`orientation`
FROM (SELECT 33 AS `race`, 1 AS `parent` UNION ALL SELECT 45, 8) r
JOIN `playercreateinfo` s ON s.`race` = r.`parent`
LEFT JOIN `playercreateinfo` t ON t.`race` = r.`race` AND t.`class` = s.`class`
WHERE t.`race` IS NULL;

INSERT INTO `playercreateinfo_item` (`race`, `class`, `itemid`, `amount`, `Note`)
SELECT r.`race`, s.`class`, s.`itemid`, s.`amount`, s.`Note`
FROM (SELECT 33 AS `race`, 1 AS `parent` UNION ALL SELECT 45, 8) r
JOIN `playercreateinfo_item` s ON s.`race` = r.`parent`
LEFT JOIN `playercreateinfo_item` t ON t.`race` = r.`race` AND t.`class` = s.`class` AND t.`itemid` = s.`itemid`
WHERE t.`race` IS NULL;

INSERT INTO `playercreateinfo_action` (`race`, `class`, `button`, `action`, `type`)
SELECT r.`race`, s.`class`, s.`button`, s.`action`, s.`type`
FROM (SELECT 33 AS `race`, 1 AS `parent` UNION ALL SELECT 45, 8) r
JOIN `playercreateinfo_action` s ON s.`race` = r.`parent`
LEFT JOIN `playercreateinfo_action` t ON t.`race` = r.`race` AND t.`class` = s.`class` AND t.`button` = s.`button`
WHERE t.`race` IS NULL;

INSERT INTO `player_race_stats` (`Race`, `Strength`, `Agility`, `Stamina`, `Intellect`, `Spirit`)
SELECT r.`race`, s.`Strength`, s.`Agility`, s.`Stamina`, s.`Intellect`, s.`Spirit`
FROM (SELECT 33 AS `race`, 1 AS `parent` UNION ALL SELECT 45, 8) r
JOIN `player_race_stats` s ON s.`Race` = r.`parent`
LEFT JOIN `player_race_stats` t ON t.`Race` = r.`race`
WHERE t.`Race` IS NULL;

INSERT INTO `ascension_custom_class_race` (`class`, `race`)
SELECT s.`class`, r.`race`
FROM (SELECT 33 AS `race`, 1 AS `parent` UNION ALL SELECT 45, 8) r
JOIN `ascension_custom_class_race` s ON s.`race` = r.`parent`
LEFT JOIN `ascension_custom_class_race` t ON t.`class` = s.`class` AND t.`race` = r.`race`
WHERE t.`race` IS NULL;

COMMIT;
