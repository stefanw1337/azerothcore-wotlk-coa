-- One Lazy Thin piggyback seats: the rider sits as far behind the carrier as on the thin piggyback seat 9530 (front of
-- the rider's riding pose 0.04 past the carrier's back, measured on thintwinsmale.m2 and kultiranmale.m2), so the
-- Kul Tiran brother no longer stands inside his thin carrier and the thin brother no longer sinks into a Kul Tiran.
UPDATE `vehicleseat_dbc` SET `AttachmentOffsetX` = -0.75, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = -0.72
    WHERE `ID` = 9720;
UPDATE `vehicleseat_dbc` SET `AttachmentOffsetX` = -0.43, `AttachmentOffsetY` = -0.1, `AttachmentOffsetZ` = -0.28
    WHERE `ID` = 9721;
