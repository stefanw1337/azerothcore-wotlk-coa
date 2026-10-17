ALTER TABLE `mod_subclass_character`
    ADD COLUMN `tokens` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `sub_spec`;
