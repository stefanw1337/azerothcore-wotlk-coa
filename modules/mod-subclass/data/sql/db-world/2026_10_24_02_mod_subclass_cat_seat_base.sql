-- mod-subclass: the brother's seat on the Travel Form cat hangs from the body's base point (seat attachment 2, M2
-- attachment 19) at the cat's back height instead of from the rider point. In a shapeshift form without a transform
-- the client resolves the seat on the player's own model, whose rider point sits at the feet, so he sat on the ground
-- between the cat's front paws; the base point is the same spot on both models.

UPDATE `vehicleseat_dbc` SET `AttachmentID` = 2, `AttachmentOffsetX` = -0.22, `AttachmentOffsetY` = 0,
    `AttachmentOffsetZ` = 1 WHERE `ID` = 9533;
