-- mod-subclass: the twin's seats: piggyback on his brother's back, and right behind him on any mount.

DELETE FROM `vehicleseat_dbc` WHERE `ID` IN (9530, 9531);
INSERT INTO `vehicleseat_dbc` (`ID`, `Flags`, `AttachmentID`, `AttachmentOffsetX`, `AttachmentOffsetY`,
    `AttachmentOffsetZ`, `EnterPreDelay`, `EnterSpeed`, `EnterGravity`, `EnterMinDuration`, `EnterMaxDuration`,
    `EnterMinArcHeight`, `EnterMaxArcHeight`, `EnterAnimStart`, `EnterAnimLoop`, `RideAnimStart`, `RideAnimLoop`,
    `RideUpperAnimStart`, `RideUpperAnimLoop`, `ExitPreDelay`, `ExitSpeed`, `ExitGravity`, `ExitMinDuration`,
    `ExitMaxDuration`, `ExitMinArcHeight`, `ExitMaxArcHeight`, `ExitAnimStart`, `ExitAnimLoop`, `ExitAnimEnd`,
    `PassengerYaw`, `PassengerPitch`, `PassengerRoll`, `PassengerAttachmentID`, `VehicleEnterAnim`,
    `VehicleExitAnim`, `VehicleRideAnimLoop`, `VehicleEnterAnimBone`, `VehicleExitAnimBone`,
    `VehicleRideAnimLoopBone`, `VehicleEnterAnimDelay`, `VehicleExitAnimDelay`, `VehicleAbilityDisplay`,
    `EnterUISoundID`, `ExitUISoundID`, `UiSkin`, `FlagsB`, `CameraEnteringDelay`, `CameraEnteringDuration`,
    `CameraExitingDelay`, `CameraExitingDuration`, `CameraOffsetX`, `CameraOffsetY`, `CameraOffsetZ`,
    `CameraPosChaseRate`, `CameraFacingChaseRate`, `CameraEnteringZoom`, `CameraSeatZoomMin`,
    `CameraSeatZoomMax`) VALUES
(9530, -671055861, 10, -0.4, 0.18, -0.5, 0.0, 10.0, 19.290001, 0.6, 1.0, 1.5, 4.0, 37, 38, -1, 91, 128, 123, 0.0,
    10.0, 19.290001, 0.6, 2.0, 1.5, 2.0, 37, 38, 39, 0.0, 0.0, 0.0, -1, 226, 107, 158, 1, 1, 1, 0.0, 0.0, 1, 0, 0, 0,
    1073741872, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
(9531, -671055861, 0, -0.55, 0.0, 0.05, 0.0, 10.0, 19.290001, 0.6, 1.0, 1.5, 4.0, 37, 38, -1, 91, 128, 123, 0.0,
    10.0, 19.290001, 0.6, 2.0, 1.5, 2.0, 37, 38, 39, 0.0, 0.0, 0.0, -1, 226, 107, 158, 1, 1, 1, 0.0, 0.0, 1, 0, 0, 0,
    1073741872, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

DELETE FROM `vehicle_dbc` WHERE `ID` IN (9530, 9531);
INSERT INTO `vehicle_dbc` (`ID`, `Flags`, `TurnSpeed`, `PitchSpeed`, `PitchMin`, `PitchMax`, `SeatID_1`, `SeatID_2`,
    `SeatID_3`, `SeatID_4`, `SeatID_5`, `SeatID_6`, `SeatID_7`, `SeatID_8`, `MouseLookOffsetPitch`,
    `CameraFadeDistScalarMin`, `CameraFadeDistScalarMax`, `CameraPitchOffset`, `FacingLimitRight`, `FacingLimitLeft`,
    `MsslTrgtTurnLingering`, `MsslTrgtPitchLingering`, `MsslTrgtMouseLingering`, `MsslTrgtEndOpacity`,
    `MsslTrgtArcSpeed`, `MsslTrgtArcRepeat`, `MsslTrgtArcWidth`, `MsslTrgtImpactRadius_1`, `MsslTrgtImpactRadius_2`,
    `MsslTrgtArcTexture`, `MsslTrgtImpactTexture`, `MsslTrgtImpactModel_1`, `MsslTrgtImpactModel_2`,
    `CameraYawOffset`, `UilocomotionType`, `MsslTrgtImpactTexRadius`, `VehicleUIIndicatorID`, `PowerDisplayID_1`,
    `PowerDisplayID_2`, `PowerDisplayID_3`) VALUES
(9530, 1073741824, 3.142, 3.142, 0.0, 0.0, 0, 9530, 0, 0, 0, 0, 0, 0, 0.0, 1.0, 1.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, '', '', '', '', 0.0, 0, 0.0, 0, 0, 0, 0),
(9531, 1073741824, 3.142, 3.142, 0.0, 0.0, 0, 9531, 0, 0, 0, 0, 0, 0, 0.0, 1.0, 1.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, '', '', '', '', 0.0, 0, 0.0, 0, 0, 0, 0);
