-- mod-subclass: the Thin Twins brother's own face, hair style and facial hair (255 = the player's own).

CREATE TABLE IF NOT EXISTS `mod_subclass_twin_features` (
    `guid` INT UNSIGNED NOT NULL,
    `face` TINYINT UNSIGNED NOT NULL DEFAULT 255,
    `hair` TINYINT UNSIGNED NOT NULL DEFAULT 255,
    `facial` TINYINT UNSIGNED NOT NULL DEFAULT 255,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
