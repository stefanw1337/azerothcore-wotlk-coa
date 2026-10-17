CREATE TABLE IF NOT EXISTS `mod_subclass_pair` (
    `race` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = any race',
    `gender_mask` TINYINT UNSIGNED NOT NULL DEFAULT 3 COMMENT '1 = male, 2 = female, 3 = both',
    `main_class` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = any CoA class',
    `sub_class` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = any other CoA class',
    `sub_spec` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = any specialization of the subclass',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `comment` VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`race`, `gender_mask`, `main_class`, `sub_class`, `sub_spec`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DELETE FROM `mod_subclass_pair`
WHERE `race` = 53 AND `gender_mask` = 2 AND `main_class` = 0 AND `sub_class` = 0 AND `sub_spec` = 0;
INSERT INTO `mod_subclass_pair`
(`race`, `gender_mask`, `main_class`, `sub_class`, `sub_spec`, `enabled`, `comment`) VALUES
(53, 2, 0, 0, 0, 1, 'Two-headed Ogre (only female Ogres in this build): adds another class tree and spec');
