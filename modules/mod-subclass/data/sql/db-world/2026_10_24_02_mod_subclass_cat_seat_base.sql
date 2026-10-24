-- mod-subclass: the brother's seat on the Travel Form cat. The client draws Travel Form with
-- Creature\Tiger\druidtravel.m2, which has no rider point (attachment 0), so the old seat fell back to a spot on the
-- ground between the cat's front paws. The seat now hangs from the cat's chest (seat attachment 1, M2 attachment 34,
-- bone 61 on the front spine, turned 20 degrees nose-up) and reaches back and up to the dip behind the shoulders, so
-- he moves with the cat; a 20 degree forward lean levels him on it.

UPDATE `vehicleseat_dbc` SET `AttachmentID` = 1, `AttachmentOffsetX` = -0.53, `AttachmentOffsetY` = 0,
    `AttachmentOffsetZ` = 0.59, `PassengerPitch` = -0.35 WHERE `ID` = 9533;
