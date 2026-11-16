-- mod-subclass: Two-Headed Ogres (85) start in Mulgore with the Tauren (6), the Thin Twins (96) in Northshire with
-- the Humans (1), class by class. Their rows were copies of races 53 and 32, which take only the start those races had
-- when the copy ran. Re-runnable; the Death Knight start (map 609) is not touched; new characters only.
UPDATE `playercreateinfo` `p`
JOIN (SELECT 85 AS `race`, 6 AS `template` UNION ALL SELECT 96, 1) `r` ON `r`.`race` = `p`.`race`
JOIN `playercreateinfo` `t` ON `t`.`race` = `r`.`template` AND `t`.`class` = `p`.`class` AND `t`.`map` <> 609
SET `p`.`map` = `t`.`map`, `p`.`zone` = `t`.`zone`, `p`.`position_x` = `t`.`position_x`,
    `p`.`position_y` = `t`.`position_y`, `p`.`position_z` = `t`.`position_z`, `p`.`orientation` = `t`.`orientation`
WHERE `p`.`map` <> 609;
