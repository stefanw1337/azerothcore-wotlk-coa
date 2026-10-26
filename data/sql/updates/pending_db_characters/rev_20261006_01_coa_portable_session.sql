-- A portable character on this realm: the runtime session that follows it from its automatic baseline to its final
-- checkpoint. `state`: 0 waiting for the baseline, 1 baseline written (the player is held), 2 running, 3 ended by the logout
-- save. Every save of the character bumps `save_seq` in the same transaction.
CREATE TABLE IF NOT EXISTS `coa_portable_session` (
  `guid` INT UNSIGNED NOT NULL,
  `session_id` CHAR(36) NOT NULL,
  `character_id` CHAR(36) NOT NULL,
  `imported_revision` INT UNSIGNED NOT NULL,
  `baseline_generation` INT UNSIGNED NOT NULL DEFAULT 1,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `checkpoint_seq` INT UNSIGNED NOT NULL DEFAULT 0,
  `save_seq` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`),
  UNIQUE KEY `idx_session_id` (`session_id`)
);
