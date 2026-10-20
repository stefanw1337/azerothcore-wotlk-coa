-- mod-subclass: which aura shows a helmet display on the Two-Headed Ogre's cyclops head.

DROP TABLE IF EXISTS `mod_subclass_cyclops_helmet`;
CREATE TABLE `mod_subclass_cyclops_helmet` (
    `DisplayId` INT UNSIGNED NOT NULL,
    `SpellId` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`DisplayId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
INSERT INTO `mod_subclass_cyclops_helmet` (`DisplayId`, `SpellId`) VALUES
(17740, 9531000),
(17805, 9531000),
(20639, 9531000),
(21451, 9531000),
(29392, 9531000),
(39401, 9531000),
(49093, 9531000);
