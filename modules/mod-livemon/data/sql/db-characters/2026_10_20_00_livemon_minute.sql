-- mod-livemon: per character totals for each minute, for the LiveMon history view.
CREATE TABLE IF NOT EXISTS `livemon_minute` (
    `minute` INT UNSIGNED NOT NULL,
    `guid` INT UNSIGNED NOT NULL,
    `race` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `class` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `tree` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `sub_class` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `sub_spec` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `damage` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `healing` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `damage_taken` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `healing_taken` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `control_given_ms` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `control_taken_ms` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `deaths` INT UNSIGNED NOT NULL DEFAULT 0,
    `kills` INT UNSIGNED NOT NULL DEFAULT 0,
    `combat_ms` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `moved_yards` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`minute`, `guid`),
    KEY `idx_combo` (`race`, `class`, `sub_class`, `tree`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
