CREATE TABLE IF NOT EXISTS `character_manastorm_completion_reward_state`
(
    `guid` INT UNSIGNED NOT NULL,
    `mode` TINYINT UNSIGNED NOT NULL,
    `track` TINYINT UNSIGNED NOT NULL,
    `reward_key` VARCHAR(32) NOT NULL,
    `remainder_bp` INT UNSIGNED NOT NULL DEFAULT 0,
    `earned` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`, `mode`, `track`, `reward_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `character_manastorm_pending_experience`
(
    `guid` INT UNSIGNED NOT NULL,
    `base_xp` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
