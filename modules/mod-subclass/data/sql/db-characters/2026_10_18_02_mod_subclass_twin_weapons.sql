-- mod-subclass: the twin's own weapon appearances.

ALTER TABLE `mod_subclass_twin`
    ADD COLUMN `mainhand` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `tabard`,
    ADD COLUMN `offhand` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `mainhand`,
    ADD COLUMN `ranged` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `offhand`;
