-- The Twins' racial One Lazy Thin (9530135), streamed to the client at login: each use turns the brother into a
-- Kul Tiran, then the player, then both thin again. It copies Bonk Heads!' row (an instant racial without effects).
DROP TEMPORARY TABLE IF EXISTS `lazy_thin_row`;
CREATE TEMPORARY TABLE `lazy_thin_row` AS SELECT * FROM `spell_dbc` WHERE `ID` = 9530133;
UPDATE `lazy_thin_row` SET `ID` = 9530135, `SpellIconID` = 6150, `Name_Lang_enUS` = 'One Lazy Thin',
    `NameSubtext_Lang_enUS` = 'Racial',
    `Description_Lang_enUS` = CONCAT('The Forgotten were Kul Tirans once. Go back to your old ways for a while: ',
        'each use turns your brother into a Kul Tiran, then you, then makes you both thin again. ',
        'Never both at once, there is no room for that.');
DELETE FROM `spell_dbc` WHERE `ID` = 9530135;
INSERT INTO `spell_dbc` SELECT * FROM `lazy_thin_row`;
DROP TEMPORARY TABLE `lazy_thin_row`;
