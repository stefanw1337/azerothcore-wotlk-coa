-- mod-subclass: on a mount the brother sits close behind the rider, chest to his back (he already piggybacks on
-- foot). 0.28 is the Thin Human's depth from back to chest in his Mount pose.

UPDATE `vehicleseat_dbc` SET `AttachmentID` = 21, `AttachmentOffsetX` = -0.28, `AttachmentOffsetY` = 0,
    `AttachmentOffsetZ` = 0 WHERE `ID` = 9531;
