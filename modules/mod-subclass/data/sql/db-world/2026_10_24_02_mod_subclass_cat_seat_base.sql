-- mod-subclass: the brother's seat on the Travel Form cat. The client draws Travel Form with
-- Creature\Tiger\druidtravel.m2, which has no rider point (attachment 0), so the old seat fell back to a spot on the
-- ground between the cat's front paws. The seat now hangs from the cat's chest (seat attachment 1, M2 attachment 34,
-- bone 61 on the front spine, turned 20 degrees nose-up) and reaches back and up to the dip behind the shoulders, so
-- he moves with the cat. Positive pitch leans him forward: 0.55 rad cancels the bone's 20 degrees and adds a
-- slight forward lean, and he sits 0.22 higher so his legs clear the cat's flanks.

UPDATE `vehicleseat_dbc` SET `AttachmentID` = 1, `AttachmentOffsetX` = -0.53, `AttachmentOffsetY` = 0,
    `AttachmentOffsetZ` = 0.81, `PassengerPitch` = 0.55 WHERE `ID` = 9533;
