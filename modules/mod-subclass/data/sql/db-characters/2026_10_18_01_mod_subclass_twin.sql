-- mod-subclass: the second twin's own wardrobe (item appearances per visible slot).

CREATE TABLE IF NOT EXISTS `mod_subclass_twin` (
    `guid` INT UNSIGNED NOT NULL,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `head` INT UNSIGNED NOT NULL DEFAULT 0,
    `shoulders` INT UNSIGNED NOT NULL DEFAULT 0,
    `shirt` INT UNSIGNED NOT NULL DEFAULT 0,
    `chest` INT UNSIGNED NOT NULL DEFAULT 0,
    `waist` INT UNSIGNED NOT NULL DEFAULT 0,
    `legs` INT UNSIGNED NOT NULL DEFAULT 0,
    `feet` INT UNSIGNED NOT NULL DEFAULT 0,
    `wrists` INT UNSIGNED NOT NULL DEFAULT 0,
    `hands` INT UNSIGNED NOT NULL DEFAULT 0,
    `back` INT UNSIGNED NOT NULL DEFAULT 0,
    `tabard` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
