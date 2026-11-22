-- Two-Players Thin Twins: the stand-in's piggyback gets its own seat, so it can sit closer to the first brother
-- without moving the male Thin Twins' piggyback (seat 9530). It starts where 9530 sits;
-- `.subclass twin seat 9630 <x> <y> <z>` tunes it live and stores the result here.

DELETE FROM `vehicleseat_dbc` WHERE `ID` = 9630;
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
(9630, -671055861, 10, 0.05, 0.0, -0.35, 0.0, 10.0, 19.290001, 0.6, 1.0, 1.5, 4.0, 37, 38, -1, 91, 128, 123, 0.0,
    10.0, 19.290001, 0.6, 2.0, 1.5, 2.0, 37, 38, 39, 0.0, -0.35, 0.0, -1, 226, 107, 158, 1, 1, 1, 0.0, 0.0, 1, 0, 0, 0,
    1073741872, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

UPDATE `vehicle_dbc` SET `SeatID_2` = 9630 WHERE `ID` = 9630;
