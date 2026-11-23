-- Which of the Twins is a Kul Tiran after One Lazy Thin: 0 both thin, 1 the brother, 2 the player (or the body of
-- a Two-Players pair).
CREATE TABLE IF NOT EXISTS `mod_subclass_lazy_thin` (
    `guid` INT UNSIGNED NOT NULL,
    `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
