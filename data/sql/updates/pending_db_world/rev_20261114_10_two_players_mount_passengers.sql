-- mod-subclass: the Two-Players ogre body keeps its own vehicle (seat 9600 holds the Ranged Head), so a mount's
-- passengers had no seat. While the body carries the D.R.I.V.E Kart or a Traveler's Tundra Mammoth it switches to
-- one of these: seat 0 is still the Ranged Head, seats 1 and 2 are that mount's own passenger seats. The client
-- links those seats to the carried mount model, which has their attachment points.

DELETE FROM `vehicle_dbc` WHERE `ID` IN (9602, 9603, 9604);
INSERT INTO `vehicle_dbc` VALUES
(9602, 1073741824, 3.142, 3.142, 0, 0, 9600, 2764, 2765, 0, 0, 0, 0, 0, 0, 1, 1.5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', '', '', '', 0, 0, 0, 0, 0, 0, 0),
(9603, 1073741824, 3.142, 3.142, 0, 0, 9600, 2767, 2768, 0, 0, 0, 0, 0, 0, 1, 1.5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', '', '', '', 0, 0, 0, 0, 0, 0, 0),
(9604, 1073741824, 3.142, 3.142, 0, 0, 9600, 9996, 9997, 0, 0, 0, 0, 0, 0, 1, 1.5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', '', '', '', 0, 0, 0, 0, 0, 0, 0);
