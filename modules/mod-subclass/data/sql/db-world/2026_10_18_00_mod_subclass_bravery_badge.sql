-- mod-subclass: race stories, and the Thin Twins' story: the Bravery Badge, its quest, its menus and VanCleef's drop.

CREATE TABLE IF NOT EXISTS `mod_subclass_story` (
    `race` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = any race',
    `gender_mask` TINYINT UNSIGNED NOT NULL DEFAULT 3 COMMENT '1 = male, 2 = female, 3 = both',
    `quest_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `relic_item_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `prompt_text` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'npc_text of the level 20 prompt',
    `class_text` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'npc_text of the trainer class menu',
    `spec_text` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'npc_text of the trainer specialization menu',
    `speaker` VARCHAR(64) NOT NULL DEFAULT '',
    `accept_option` VARCHAR(255) NOT NULL DEFAULT '',
    `later_option` VARCHAR(255) NOT NULL DEFAULT '',
    `accepted` VARCHAR(255) NOT NULL DEFAULT '',
    `later` VARCHAR(255) NOT NULL DEFAULT '',
    `not_yet` VARCHAR(255) NOT NULL DEFAULT '',
    `used` VARCHAR(255) NOT NULL DEFAULT '',
    `unreadable` VARCHAR(255) NOT NULL DEFAULT '',
    `trainer_option` VARCHAR(255) NOT NULL DEFAULT '',
    `chosen` VARCHAR(255) NOT NULL DEFAULT '',
    `found` VARCHAR(255) NOT NULL DEFAULT '',
    `refused` VARCHAR(255) NOT NULL DEFAULT '',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`race`, `gender_mask`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DELETE FROM `mod_subclass_pair`
WHERE `race` = 32 AND `gender_mask` = 1 AND `main_class` = 0 AND `sub_class` = 0 AND `sub_spec` = 0;
INSERT INTO `mod_subclass_pair`
(`race`, `gender_mask`, `main_class`, `sub_class`, `sub_spec`, `enabled`, `comment`) VALUES
(32, 1, 0, 0, 0, 1, 'Thin Twins (male Thin Humans): the second twin adds another class tree and spec');

DELETE FROM `mod_subclass_story` WHERE `race` = 32 AND `gender_mask` = 1;
INSERT INTO `mod_subclass_story` (`race`, `gender_mask`, `quest_id`, `relic_item_id`, `prompt_text`, `class_text`,
    `spec_text`, `speaker`, `accept_option`, `later_option`, `accepted`, `later`, `not_yet`, `used`, `unreadable`,
    `trainer_option`, `chosen`, `found`, `refused`, `enabled`)
VALUES (32, 1, 9530020, 9530020, 9530020, 9530021, 9530022, 'Your twin',
    'Then go first. For once, I will be the one behind you.',
    'Not yet, brother. Stay behind me a little longer.',
    'The Deadmines, under Moonbrook. VanCleef has it. I lead, you follow. Strange, isn''t it?',
    'Always "not yet". I will ask again.',
    'Not like this. I have to earn it first.',
    'He pins the badge to his own coat, not yours. "From the same, not the same. Find a class trainer. This time I choose my own way."',
    'A dented badge of the Stonemasons'' Guild. It means nothing to you.',
    '[Your twin] I want to learn a way of my own.',
    'My way, at last.',
    'Another Bravery Badge, from one more mason who never came home. I can choose again.',
    'Your twin shakes his head. That is not his way.',
    1);

REPLACE INTO `item_template` (`entry`, `class`, `subclass`, `SoundOverrideSubclass`, `name`, `displayid`, `Quality`,
    `Flags`, `BuyCount`, `ItemLevel`, `RequiredLevel`, `AllowableClass`, `AllowableRace`, `maxcount`, `stackable`,
    `spellid_1`, `spelltrigger_1`, `spellcharges_1`, `spellcooldown_1`, `spellcategorycooldown_1`, `bonding`,
    `description`, `Material`, `ScriptName`, `VerifiedBuild`)
VALUES (9530020, 12, 0, -1, 'Bravery Badge', 7271, 3, 0, 1, 25, 0, -1, -1, 1, 1, 93216, 0, 0, -1, -1, 1,
    'Stormwind Guard, for courage in the rebuilding. Scratched on the back: two names, one surname.', 1,
    'item_subclass_bravery_badge', 0);

REPLACE INTO `quest_template` (`ID`, `QuestType`, `QuestLevel`, `MinLevel`, `QuestSortID`, `QuestInfoID`,
    `SuggestedGroupNum`, `RewardXPDifficulty`, `Flags`, `AllowableRaces`, `LogTitle`, `LogDescription`,
    `QuestDescription`, `AreaDescription`, `QuestCompletionLog`, `RequiredItemId1`, `RequiredItemCount1`,
    `VerifiedBuild`)
VALUES (9530020, 2, 22, 20, 1581, 81, 5, 5, 0, 0, 'One From the Same',
    'Take your father''s Bravery Badge from Edwin VanCleef in the Deadmines, then let your twin pin it on.',
    'All our lives they have called us by one name. One bed, one bowl, one shadow. Mother was a sailor''s daughter from Kul Tiras, and she always said we came from the same. She never said we were the same.$B$BFather earned his Bravery Badge pulling three masons out of the rubble while the Stonemasons rebuilt Stormwind. Then the nobles refused to pay the guild, and he followed Edwin VanCleef into the dark. VanCleef keeps the badge of every mason who joined him, as proof of what the city owes them.$B$BThe Deadmines, under Moonbrook. I want that badge back, brother. And this time, I go first.',
    '', 'Let your twin pin on the Bravery Badge.', 9530020, 1, 0);

DELETE FROM `quest_template_addon` WHERE `ID` = 9530020;
INSERT INTO `quest_template_addon` (`ID`, `MaxLevel`, `AllowableClasses`, `SpecialFlags`)
VALUES (9530020, 0, 0, 0);

DELETE FROM `npc_text` WHERE `ID` IN (9530020, 9530021, 9530022);
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `BroadcastTextID0`, `lang0`, `Probability0`, `VerifiedBuild`)
VALUES
(9530020,
    'Your twin has been quiet all day. Now he steps out from behind your shoulder, which he never does.$B$B"They have always called us one name. Mother never did. She said we came from the same, not that we are the same. I think I finally understand her.$B$BFather''s Bravery Badge is in the Deadmines, with Edwin VanCleef. I want it back. And this time, I go first."',
    'Your twin has been quiet all day. Now he steps out from behind your shoulder, which he never does.$B$B"They have always called us one name. Mother never did. She said we came from the same, not that we are the same. I think I finally understand her.$B$BFather''s Bravery Badge is in the Deadmines, with Edwin VanCleef. I want it back. And this time, I go first."',
    0, 0, 1, 0),
(9530021,
    'Your twin steps past you to the trainer before you can say a word.$B$B"I will ask this time. Which of you teaches something my brother does not know?"',
    'Your twin steps past you to the trainer before you can say a word.$B$B"I will ask this time. Which of you teaches something my brother does not know?"',
    0, 0, 1, 0),
(9530022,
    '"That one. And if we find another badge, I may choose again. I am allowed to change my mind now."',
    '"That one. And if we find another badge, I may choose again. I am allowed to change my mind now."',
    0, 0, 1, 0);

DELETE FROM `creature_loot_template` WHERE `Item` = 9530020 AND `Entry` IN (639, 100639, 200639);
INSERT INTO `creature_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`,
    `MinCount`, `MaxCount`, `Comment`)
VALUES
(639, 9530020, 0, 100, 1, 1, 0, 1, 1, 'Edwin VanCleef - Bravery Badge'),
(100639, 9530020, 0, 100, 1, 1, 0, 1, 1, 'Edwin VanCleef (Heroic) - Bravery Badge'),
(200639, 9530020, 0, 100, 1, 1, 0, 1, 1, 'Edwin VanCleef (Mythic) - Bravery Badge');
