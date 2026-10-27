-- mod-subclass: the twin brother's seats on a wolf (Ghost Wolf, Wargstride) and on the Travel Form cat. Both
-- models carry a rider point on the back (attachment 0), which sits on the back surface in their stand, walk
-- and run animations; the cat's point is 0.04 under its fur, so his seat is raised by that much.

DELETE FROM `vehicleseat_dbc` WHERE `ID` IN (9532, 9533);
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
(9532, -671055861, 21, 0.0, 0.0, 0.0, 0.0, 10.0, 19.290001, 0.6, 1.0, 1.5, 4.0, 37, 38, -1, 91, 128, 123, 0.0, 10.0,
    19.290001, 0.6, 2.0, 1.5, 2.0, 37, 38, 39, 0.0, 0.0, 0.0, -1, 226, 107, 158, 1, 1, 1, 0.0, 0.0, 1, 0, 0, 0,
    1073741872, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
(9533, -671055861, 21, 0.0, 0.0, 0.04, 0.0, 10.0, 19.290001, 0.6, 1.0, 1.5, 4.0, 37, 38, -1, 91, 128, 123, 0.0, 10.0,
    19.290001, 0.6, 2.0, 1.5, 2.0, 37, 38, 39, 0.0, 0.0, 0.0, -1, 226, 107, 158, 1, 1, 1, 0.0, 0.0, 1, 0, 0, 0,
    1073741872, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

DELETE FROM `vehicle_dbc` WHERE `ID` IN (9532, 9533);
INSERT INTO `vehicle_dbc` (`ID`, `Flags`, `TurnSpeed`, `PitchSpeed`, `PitchMin`, `PitchMax`, `SeatID_1`, `SeatID_2`,
    `SeatID_3`, `SeatID_4`, `SeatID_5`, `SeatID_6`, `SeatID_7`, `SeatID_8`, `MouseLookOffsetPitch`,
    `CameraFadeDistScalarMin`, `CameraFadeDistScalarMax`, `CameraPitchOffset`, `FacingLimitRight`, `FacingLimitLeft`,
    `MsslTrgtTurnLingering`, `MsslTrgtPitchLingering`, `MsslTrgtMouseLingering`, `MsslTrgtEndOpacity`,
    `MsslTrgtArcSpeed`, `MsslTrgtArcRepeat`, `MsslTrgtArcWidth`, `MsslTrgtImpactRadius_1`, `MsslTrgtImpactRadius_2`,
    `MsslTrgtArcTexture`, `MsslTrgtImpactTexture`, `MsslTrgtImpactModel_1`, `MsslTrgtImpactModel_2`, `CameraYawOffset`,
    `UilocomotionType`, `MsslTrgtImpactTexRadius`, `VehicleUIIndicatorID`, `PowerDisplayID_1`, `PowerDisplayID_2`,
    `PowerDisplayID_3`) VALUES
(9532, 1073741824, 3.142, 3.142, 0.0, 0.0, 0, 9532, 0, 0, 0, 0, 0, 0, 0.0, 1.0, 1.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, '', '', '', '', 0.0, 0, 0.0, 0, 0, 0, 0),
(9533, 1073741824, 3.142, 3.142, 0.0, 0.0, 0, 9533, 0, 0, 0, 0, 0, 0, 0.0, 1.0, 1.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0, 0.0, '', '', '', '', 0.0, 0, 0.0, 0, 0, 0, 0);
