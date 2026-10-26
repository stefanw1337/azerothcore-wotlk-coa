-- CoA Custom 1.9.2: Gallop pose-only visual; Hozen (112) start next to Tauren.

UPDATE `spell_dbc`
SET `SpellVisualID_1` = 7245, `SpellVisualID_2` = 0
WHERE `ID` = 14001006;

UPDATE `playercreateinfo`
SET `map` = 1, `zone` = 215,
    `position_x` = -2917.58, `position_y` = -257.98, `position_z` = 52.9968, `orientation` = 0
WHERE `race` = 112 AND `map` <> 609;
