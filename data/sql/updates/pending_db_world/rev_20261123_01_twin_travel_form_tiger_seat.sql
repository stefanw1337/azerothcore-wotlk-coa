-- Thin Twins: plain Travel Form draws Creature\Tiger\Tiger.m2 in the client, not the druid travel cat that seat
-- 9533 was fitted to; that seat hangs from the chest, which drops 0.9 when the cat lies down to yawn. Seat 9719 uses
-- the tiger's rider point, which stays on the back through its stand, idle, walk and run animations.
DELETE FROM `vehicleseat_dbc` WHERE `ID` = 9719;
INSERT INTO `vehicleseat_dbc` (`ID`, `Flags`, `AttachmentID`, `AttachmentOffsetX`, `AttachmentOffsetY`,
    `AttachmentOffsetZ`, `EnterPreDelay`, `EnterSpeed`, `EnterGravity`, `EnterMinDuration`, `EnterMaxDuration`,
    `EnterMinArcHeight`, `EnterMaxArcHeight`, `EnterAnimStart`, `EnterAnimLoop`, `RideAnimStart`, `RideAnimLoop`,
    `RideUpperAnimStart`, `RideUpperAnimLoop`, `ExitPreDelay`, `ExitSpeed`, `ExitGravity`, `ExitMinDuration`,
    `ExitMaxDuration`, `ExitMinArcHeight`, `ExitMaxArcHeight`, `ExitAnimStart`, `ExitAnimLoop`, `ExitAnimEnd`,
    `PassengerYaw`, `PassengerPitch`, `PassengerRoll`, `PassengerAttachmentID`, `VehicleEnterAnim`, `VehicleExitAnim`,
    `VehicleRideAnimLoop`, `VehicleEnterAnimBone`, `VehicleExitAnimBone`, `VehicleRideAnimLoopBone`,
    `VehicleEnterAnimDelay`, `VehicleExitAnimDelay`, `VehicleAbilityDisplay`, `EnterUISoundID`, `ExitUISoundID`,
    `UiSkin`, `FlagsB`, `CameraEnteringDelay`, `CameraEnteringDuration`, `CameraExitingDelay`, `CameraExitingDuration`,
    `CameraOffsetX`, `CameraOffsetY`, `CameraOffsetZ`, `CameraPosChaseRate`, `CameraFacingChaseRate`,
    `CameraEnteringZoom`, `CameraSeatZoomMin`, `CameraSeatZoomMax`) VALUES
(9719, -671055861, 21, 0.07, -0.05, 0.13, 0.0, 10.0, 19.290001, 0.6, 1.0, 1.5, 4.0, 37, 38, -1, 91, 128,
    123, 0.0, 10.0, 19.290001, 0.6, 2.0, 1.5, 2.0, 37, 38, 39, 0.0, 0.11, 0.0, -1, 226, 107, 158, 1, 1, 1, 0.0, 0.0, 1,
    0, 0, 0, 1073741872, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

DELETE FROM `vehicle_dbc` WHERE `ID` IN (9719, 9819);
INSERT INTO `vehicle_dbc` (`ID`, `Flags`, `TurnSpeed`, `PitchSpeed`, `PitchMin`, `PitchMax`, `SeatID_1`, `SeatID_2`,
    `SeatID_3`, `SeatID_4`, `SeatID_5`, `SeatID_6`, `SeatID_7`, `SeatID_8`, `MouseLookOffsetPitch`,
    `CameraFadeDistScalarMin`, `CameraFadeDistScalarMax`, `CameraPitchOffset`, `FacingLimitRight`, `FacingLimitLeft`,
    `MsslTrgtTurnLingering`, `MsslTrgtPitchLingering`, `MsslTrgtMouseLingering`, `MsslTrgtEndOpacity`,
    `MsslTrgtArcSpeed`, `MsslTrgtArcRepeat`, `MsslTrgtArcWidth`, `MsslTrgtImpactRadius_1`, `MsslTrgtImpactRadius_2`,
    `MsslTrgtArcTexture`, `MsslTrgtImpactTexture`, `MsslTrgtImpactModel_1`, `MsslTrgtImpactModel_2`, `CameraYawOffset`,
    `UilocomotionType`, `MsslTrgtImpactTexRadius`, `VehicleUIIndicatorID`, `PowerDisplayID_1`, `PowerDisplayID_2`,
    `PowerDisplayID_3`) VALUES
(9719, 1073741824, 3.142, 3.142, 0.0, 0.0, 0, 9719, 0, 0, 0, 0, 0, 0, 0.0, 1.0, 1.5, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, '', '', '', '', 0.0, 0, 0.0, 0, 0, 0, 0),
(9819, 1073741824, 3.142, 3.142, 0.0, 0.0, 9600, 9719, 0, 0, 0, 0, 0, 0, 0.0, 1.0, 1.5, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, '', '', '', '', 0.0, 0, 0.0, 0, 0, 0, 0);

DELETE FROM `mod_subclass_form_seat` WHERE `DisplayId` = 918;
INSERT INTO `mod_subclass_form_seat` (`DisplayId`, `VehicleId`) VALUES (918, 9719);
