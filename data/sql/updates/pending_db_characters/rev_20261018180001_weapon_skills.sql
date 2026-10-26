INSERT IGNORE INTO `character_skills` (`guid`, `skill`, `value`, `max`)
SELECT `guid`, `s`.`skill`, LEAST(`level` * 5, 400), LEAST(`level` * 5, 400)
FROM `characters` `c` JOIN (SELECT 45 AS `skill` UNION SELECT 46 UNION SELECT 226) `s` ON 1 = 1
WHERE `c`.`class` = 3;
INSERT IGNORE INTO `character_skills` (`guid`, `skill`, `value`, `max`)
SELECT `guid`, 160, LEAST(`level` * 5, 400), LEAST(`level` * 5, 400) FROM `characters` WHERE `class` IN (1, 2);
INSERT IGNORE INTO `character_skills` (`guid`, `skill`, `value`, `max`)
SELECT `guid`, 173, LEAST(`level` * 5, 400), LEAST(`level` * 5, 400) FROM `characters` WHERE `class` IN (1, 3, 4, 9, 11);
