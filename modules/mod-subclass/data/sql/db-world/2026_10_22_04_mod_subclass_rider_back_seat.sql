-- mod-subclass: on a mount the brother rides on the rider's back, as he piggybacks on foot. The offset from the
-- mount's rider point is where the Thin Human's back sits in his Mount pose, so it is the same on every mount.

UPDATE `vehicleseat_dbc` SET `AttachmentID` = 21, `AttachmentOffsetX` = -0.145, `AttachmentOffsetY` = -0.04,
    `AttachmentOffsetZ` = 0.23 WHERE `ID` = 9531;
