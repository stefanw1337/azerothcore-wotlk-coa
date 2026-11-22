-- Help text for the GM command that tunes a twin seat live, so a wrong argument shows its syntax.
DELETE FROM `command` WHERE `name` = 'subclass twin seat';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('subclass twin seat', 2, 'Syntax: .subclass twin seat <seat> <x> <y> <z> [attachment]\nMoves a twin vehicle seat''s offset (and optionally its attachment point, -1 for the body''s root) and stores it in vehicleseat_dbc. Seats: 9530 piggyback, 9630 Two-Players piggyback, 9531 saddle, 9532 Ghost Wolf, 9533 cat form.');
