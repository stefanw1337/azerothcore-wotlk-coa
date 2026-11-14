-- A Dead Rebel Warrior 2300514 sparkled and stayed lootable for the whole of Death and Dishonor 1660032, also once the
-- player held all five Grimtotem Armor Pieces 559151, because its chest questId (Data8) names the quest; bots kept
-- looting them. Without it, and with GO_FLAG_INTERACT_COND, a warrior is active only while its looter needs a piece.
UPDATE `gameobject_template` SET `Data8` = 0 WHERE `entry` = 2300514;

DELETE FROM `gameobject_template_addon` WHERE `entry` = 2300514;
INSERT INTO `gameobject_template_addon` (`entry`, `faction`, `flags`, `mingold`, `maxgold`, `artkit0`, `artkit1`,
    `artkit2`, `artkit3`) VALUES
(2300514, 0, 4, 0, 0, 0, 0, 0, 0);
