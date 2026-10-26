-- 1.7.1: classic hunters (and other classic weapon users) on CUSTOM races could not equip bows, guns, crossbows or daggers,
-- so they had no Auto Shot (issues #28, #32). The stock rows grant those skills only for a fixed list of races: raceMask is a
-- 32-bit mask and custom race ids wrap or are not in it. raceMask 0 = every race. Safe to run again.

INSERT IGNORE INTO `playercreateinfo_skills` (`raceMask`, `classMask`, `skill`, `rank`, `comment`) VALUES
 (0, 4,    45,  0, 'Bows - hunters, every race (1.7.1)'),
 (0, 4,    46,  0, 'Guns - hunters, every race (1.7.1)'),
 (0, 4,    226, 0, 'Crossbows - hunters, every race (1.7.1)'),
 (0, 3,    160, 0, 'Two-Handed Maces - warrior and paladin, every race (1.7.1)'),
 (0, 1293, 173, 0, 'Daggers - warrior, hunter, rogue, warlock, druid, every race (1.7.1)');

-- Characters that already exist (hunters made before this fix): give them the skills too, at their level's maximum.
