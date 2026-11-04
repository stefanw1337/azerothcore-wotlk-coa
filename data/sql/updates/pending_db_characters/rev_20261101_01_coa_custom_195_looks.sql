-- CoA Custom 1.9.5 (ilusixn, CustomRacesCoa races-port 8984c8b8, races-extras/release/16_1.9.5_x64.sql)
CREATE TABLE IF NOT EXISTS `character_body_shape` (`guid` int unsigned NOT NULL, `data` binary(49) NOT NULL, PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS `character_retail_look` (`guid` int unsigned NOT NULL, `data` binary(132) NOT NULL, PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE TABLE IF NOT EXISTS `character_form_look` (`guid` int unsigned NOT NULL, `data` binary(132) NOT NULL, PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
DELETE FROM `character_spell` WHERE `spell` = 110687;

INSERT IGNORE INTO `character_spell` (`guid`, `spell`, `specMask`) SELECT `guid`, 110690, 255 FROM `characters` WHERE `race` = 13;
