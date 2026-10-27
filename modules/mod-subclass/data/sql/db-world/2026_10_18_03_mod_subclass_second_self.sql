-- mod-subclass: the racials that call the twin brother, settle the ogre's heads, or let the other half act alone.

DELETE FROM `spell_script_names` WHERE `spell_id` IN (9530100, 9530101, 9530106, 9530107);
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(9530100, 'spell_subclass_second_self'),
(9530101, 'spell_subclass_second_self'),
(9530106, 'spell_subclass_easy'),
(9530107, 'spell_subclass_easy');
