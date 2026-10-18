-- mod-subclass: the racials that call the twin brother or settle the ogre's heads.

DELETE FROM `spell_script_names` WHERE `spell_id` IN (9530100, 9530101);
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(9530100, 'spell_subclass_second_self'),
(9530101, 'spell_subclass_second_self');
