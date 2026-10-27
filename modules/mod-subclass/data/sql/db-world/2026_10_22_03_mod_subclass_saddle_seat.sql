-- mod-subclass: the brother sits behind the rider on any mount (seat attachment 21 is the mount's rider point;
-- 0 was the mount's head).

UPDATE `vehicleseat_dbc` SET `AttachmentID` = 21, `AttachmentOffsetX` = -0.5 WHERE `ID` = 9531;
