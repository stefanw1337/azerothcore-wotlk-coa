-- mod-subclass: Thin Humans (96) and Two-Headed Ogres (85) cannot be created as Hero (class 10). Their creation rows
-- were copied from races 32 and 53, which carry a Hero row; the client already offers no Hero for these races.
DELETE FROM `playercreateinfo` WHERE `race` IN (85, 96) AND `class` = 10;
DELETE FROM `playercreateinfo_item` WHERE `race` IN (85, 96) AND `class` = 10;
DELETE FROM `playercreateinfo_action` WHERE `race` IN (85, 96) AND `class` = 10;
