-- mod-subclass: the relic a subclassed Paladin, Shaman, Druid or Death Knight holds beside a ranged weapon.

CREATE TABLE IF NOT EXISTS `mod_subclass_relic` (
    `guid` INT UNSIGNED NOT NULL,
    `item` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
