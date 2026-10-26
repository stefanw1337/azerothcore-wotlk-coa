-- Guardian Tower Formation (shapeshift form 36, applied by spell 803907) lost the STANCE flag in the world DB override.
-- The client SpellShapeshiftForm.dbc gives all three Guardian formations the same flags (13: STANCE | PERSIST_ON_DEATH |
-- CAN_NPC_INTERACT): Line Formation (35), Tower Formation (36) and Assault Formation (37). The DB row for 36 had Flags = 0,
-- so the core treated Tower as a model-changing shapeshift and rejected every NOT_SHAPESHIFTED shield ability (Ram, Pulverize,
-- Raise Shield, Standard of Valiance, ...) with SPELL_FAILED_NOT_SHAPESHIFT, and NPC/item interaction was blocked in the form.
-- Only the flag is restored; the model (CreatureDisplayID_1), CreatureType, CombatRoundTime and BonusActionBar of the row
-- are separate decisions and stay as they are. A no-op where the row is missing or already corrected.
UPDATE `spellshapeshiftform_dbc` SET `Flags` = 13 WHERE `ID` = 36 AND `Flags` = 0;
