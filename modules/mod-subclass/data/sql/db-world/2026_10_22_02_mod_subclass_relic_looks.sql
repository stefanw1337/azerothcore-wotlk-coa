-- mod-subclass: the Bravery Badge looks like a badge and is pinned on the brother, the Book of Origins looks like a
-- book and is read together, instead of both showing "Use: Drink up!".

UPDATE `item_template` SET `displayid` = 30947, `spellid_1` = 9530114 WHERE `entry` = 9530020;
UPDATE `item_template` SET `displayid` = 299, `spellid_1` = 9530115 WHERE `entry` = 9530010;
