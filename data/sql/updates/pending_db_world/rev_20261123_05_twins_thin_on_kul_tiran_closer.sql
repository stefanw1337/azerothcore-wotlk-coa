-- One Lazy Thin: the thin brother on a Kul Tiran's back sits 0.25 further forward, knees into the Kul Tiran's back
-- (Stefan: arms and legs going through the bigger body are fine, the gap is not).
UPDATE `vehicleseat_dbc` SET `AttachmentOffsetX` = -0.21, `AttachmentOffsetY` = -0.01, `AttachmentOffsetZ` = -0.21
    WHERE `ID` = 9721;
