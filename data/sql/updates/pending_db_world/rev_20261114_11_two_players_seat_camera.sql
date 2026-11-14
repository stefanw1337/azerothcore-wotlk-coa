-- Two-Players: the hidden Ranged Head seat (9600) and the Twins' piggyback seat (9601) ease the camera after
-- the body's turns (position chase 5, facing chase 3, as the multi-passenger mount seats do) instead of snapping.
-- The rest of each row is the client table's own. AscensionCompat streams these rows to clients at login.

DELETE FROM `vehicleseat_dbc` WHERE `ID` IN (9600, 9601);
INSERT INTO `vehicleseat_dbc` VALUES
(9600, 34620928, -1, 0.0, 0.0, 0.0, 0.0, 10.0, 19.290001, 0.6, 1.0, 1.5, 4.0, 37, 38, -1, 91, 128, 123, 0.0, 10.0, 19.290001, 0.6, 2.0, 1.5, 2.0, 37, 38, 39, 0.0, 0.0, 0.0, -1, -1, -1, -1, 1, 1, 1, 0.0, 0.0, 1, 0, 0, 0, 131072, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 5.0, 3.0, 0.0, 0.0, 0.0),
(9601, 34620416, 10, 0.02, 0.01, 0.26, 0.0, 10.0, 19.290001, 0.6, 1.0, 1.5, 4.0, 37, 38, -1, 91, 128, 123, 0.0, 10.0, 19.290001, 0.6, 2.0, 1.5, 2.0, 37, 38, 39, 0.0, 0.0, 0.0, -1, -1, -1, -1, 1, 1, 1, 0.0, 0.0, 1, 0, 0, 0, 131072, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 5.0, 3.0, 0.0, 0.0, 0.0);
