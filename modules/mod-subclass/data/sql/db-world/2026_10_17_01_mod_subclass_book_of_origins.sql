-- mod-subclass: the Book of Origins, the second head's quest, its menus and Arugal's drop.

REPLACE INTO `item_template` (`entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`, `Quality`,
    `Flags`, `BuyCount`, `ItemLevel`, `RequiredLevel`, `AllowableClass`, `AllowableRace`, `maxcount`, `stackable`,
    `spellid_1`, `spelltrigger_1`, `spellcharges_1`, `spellcooldown_1`, `spellcategorycooldown_1`, `bonding`,
    `description`, `Material`, `ScriptName`, `VerifiedBuild`)
VALUES (9530010, 12, 0, -1, 'Book of Origins', 1246, 3, 0, 1, 25, 0, -1, -1, 1, 1, 93216, 0, 0, -1, -1, 1,
    'Dalaran research into the ogre magi of Gul''dan. Scrawled in the margins: two heads, two minds.', 4,
    'item_subclass_book_of_origins', 0);

REPLACE INTO `quest_template` (`ID`, `QuestType`, `QuestLevel`, `MinLevel`, `QuestSortID`, `QuestInfoID`,
    `SuggestedGroupNum`, `RewardXPDifficulty`, `Flags`, `AllowableRaces`, `LogTitle`, `LogDescription`,
    `QuestDescription`, `AreaDescription`, `QuestCompletionLog`, `RequiredItemId1`, `RequiredItemCount1`,
    `VerifiedBuild`)
VALUES (9530010, 2, 22, 20, 209, 81, 5, 5, 0, 0, 'Two Heads, One Book',
    'Take the Book of Origins from Archmage Arugal in Shadowfang Keep, then read it together with your second head.',
    'Oi. Other head. You always do the thinking and me always do the nodding. Not fair!$B$BLong ago the ogre magi of Gul''dan learned the tricks of other folk. Humans in Dalaran wrote it all down in a book, the Book of Origins. Then the mad mage Arugal stole it for his wolf-men. Me can smell it from here: Shadowfang Keep.$B$BWe get the book, we read it, we learn a second way. Two heads, two ways. Good deal, yes?',
    '', 'Read the Book of Origins.', 9530010, 1, 0);

DELETE FROM `quest_template_addon` WHERE `ID` = 9530010;
INSERT INTO `quest_template_addon` (`ID`, `MaxLevel`, `AllowableClasses`, `SpecialFlags`)
VALUES (9530010, 0, 0, 0);

DELETE FROM `npc_text` WHERE `ID` IN (9530010, 9530011, 9530012);
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `BroadcastTextID0`, `lang0`, `Probability0`, `VerifiedBuild`)
VALUES
(9530010,
    'Your second head clears its throat, loudly, right next to your ear.$B$B"Oi. Other head. Me been thinking. You always do the thinking, me always do the nodding. Not fair!$B$BOgre magi of old learned the ways of other folk. It all written in a book, the Book of Origins. The mad mage Arugal has it, in Shadowfang Keep. Me can smell it.$B$BWe get it, we learn a second way. Yes?"',
    'Your second head clears its throat, loudly, right next to your ear.$B$B"Oi. Other head. Me been thinking. You always do the thinking, me always do the nodding. Not fair!$B$BOgre magi of old learned the ways of other folk. It all written in a book, the Book of Origins. The mad mage Arugal has it, in Shadowfang Keep. Me can smell it.$B$BWe get it, we learn a second way. Yes?"',
    0, 0, 1, 0),
(9530011,
    'Before the trainer can say a word, your second head leans in.$B$B"This one knows things. Ask about the others! Which class we learn from?"',
    'Before the trainer can say a word, your second head leans in.$B$B"This one knows things. Ask about the others! Which class we learn from?"',
    0, 0, 1, 0),
(9530012,
    '"Good, good. Now which way of theirs? Choose careful, other head. Next time we need a new book."',
    '"Good, good. Now which way of theirs? Choose careful, other head. Next time we need a new book."',
    0, 0, 1, 0);

DELETE FROM `creature_loot_template` WHERE `Item` = 9530010 AND `Entry` IN (4275, 104275, 204275);
INSERT INTO `creature_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`,
    `MinCount`, `MaxCount`, `Comment`)
VALUES
(4275, 9530010, 0, 100, 1, 1, 0, 1, 1, 'Archmage Arugal - Book of Origins'),
(104275, 9530010, 0, 100, 1, 1, 0, 1, 1, 'Archmage Arugal (Heroic) - Book of Origins'),
(204275, 9530010, 0, 100, 1, 1, 0, 1, 1, 'Archmage Arugal (Mythic) - Book of Origins');
