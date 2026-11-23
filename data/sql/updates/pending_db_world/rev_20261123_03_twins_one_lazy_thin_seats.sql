-- One Lazy Thin seats: the Twins' piggyback and saddle seats again for a Kul Tiran brother (piggyback 9720, saddles
-- 9750-9759) and for a Kul Tiran player (piggyback 9721, saddles 9760-9769), each a copy of the normal seat with the
-- offset moved for the bigger body (measured on thintwinsmale.m2 and kultiranmale.m2: the brother's front against
-- the carrier's back as on the thin seats, the Kul Tiran's feet off the ground). Two-Players copies are +100.
DROP TEMPORARY TABLE IF EXISTS `lazy_thin_seat`;
CREATE TEMPORARY TABLE `lazy_thin_seat` AS SELECT * FROM `vehicleseat_dbc` WHERE 0;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9530;
UPDATE `lazy_thin_seat` SET `ID` = 9720,
    `AttachmentOffsetX` = -0.07, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = -0.41
    WHERE `ID` = 9530;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9530;
UPDATE `lazy_thin_seat` SET `ID` = 9721,
    `AttachmentOffsetX` = -0.03, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = -0.15
    WHERE `ID` = 9530;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9531;
UPDATE `lazy_thin_seat` SET `ID` = 9750,
    `AttachmentOffsetX` = -0.41, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9531;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9531;
UPDATE `lazy_thin_seat` SET `ID` = 9760,
    `AttachmentOffsetX` = -0.48, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9531;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9534;
UPDATE `lazy_thin_seat` SET `ID` = 9751,
    `AttachmentOffsetX` = -0.41, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9534;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9534;
UPDATE `lazy_thin_seat` SET `ID` = 9761,
    `AttachmentOffsetX` = -0.48, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9534;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9535;
UPDATE `lazy_thin_seat` SET `ID` = 9752,
    `AttachmentOffsetX` = -0.41, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9535;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9535;
UPDATE `lazy_thin_seat` SET `ID` = 9762,
    `AttachmentOffsetX` = -0.48, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9535;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9536;
UPDATE `lazy_thin_seat` SET `ID` = 9753,
    `AttachmentOffsetX` = -0.41, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9536;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9536;
UPDATE `lazy_thin_seat` SET `ID` = 9763,
    `AttachmentOffsetX` = -0.48, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9536;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9537;
UPDATE `lazy_thin_seat` SET `ID` = 9754,
    `AttachmentOffsetX` = -0.41, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9537;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9537;
UPDATE `lazy_thin_seat` SET `ID` = 9764,
    `AttachmentOffsetX` = -0.48, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9537;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9538;
UPDATE `lazy_thin_seat` SET `ID` = 9755,
    `AttachmentOffsetX` = -0.41, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9538;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9538;
UPDATE `lazy_thin_seat` SET `ID` = 9765,
    `AttachmentOffsetX` = -0.48, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9538;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9539;
UPDATE `lazy_thin_seat` SET `ID` = 9756,
    `AttachmentOffsetX` = -0.41, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9539;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9539;
UPDATE `lazy_thin_seat` SET `ID` = 9766,
    `AttachmentOffsetX` = -0.48, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9539;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9540;
UPDATE `lazy_thin_seat` SET `ID` = 9757,
    `AttachmentOffsetX` = -0.41, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9540;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9540;
UPDATE `lazy_thin_seat` SET `ID` = 9767,
    `AttachmentOffsetX` = -0.48, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9540;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9541;
UPDATE `lazy_thin_seat` SET `ID` = 9758,
    `AttachmentOffsetX` = -0.41, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9541;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9541;
UPDATE `lazy_thin_seat` SET `ID` = 9768,
    `AttachmentOffsetX` = -0.48, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9541;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9542;
UPDATE `lazy_thin_seat` SET `ID` = 9759,
    `AttachmentOffsetX` = -0.41, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9542;
INSERT INTO `lazy_thin_seat` SELECT * FROM `vehicleseat_dbc` WHERE `ID` = 9542;
UPDATE `lazy_thin_seat` SET `ID` = 9769,
    `AttachmentOffsetX` = -0.48, `AttachmentOffsetY` = 0, `AttachmentOffsetZ` = 0
    WHERE `ID` = 9542;
DELETE FROM `vehicleseat_dbc` WHERE `ID` IN (9720, 9721, 9750, 9751, 9752, 9753, 9754, 9755, 9756, 9757, 9758, 9759,
    9760, 9761, 9762, 9763, 9764, 9765, 9766, 9767, 9768, 9769);
INSERT INTO `vehicleseat_dbc` SELECT * FROM `lazy_thin_seat`;
DROP TEMPORARY TABLE `lazy_thin_seat`;

DROP TEMPORARY TABLE IF EXISTS `lazy_thin_vehicle`;
CREATE TEMPORARY TABLE `lazy_thin_vehicle` AS SELECT * FROM `vehicle_dbc` WHERE 0;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9530;
UPDATE `lazy_thin_vehicle` SET `ID` = 9720, `SeatID_1` = 9720 WHERE `ID` = 9530;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9630;
UPDATE `lazy_thin_vehicle` SET `ID` = 9820, `SeatID_2` = 9720 WHERE `ID` = 9630;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9530;
UPDATE `lazy_thin_vehicle` SET `ID` = 9721, `SeatID_1` = 9721 WHERE `ID` = 9530;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9630;
UPDATE `lazy_thin_vehicle` SET `ID` = 9821, `SeatID_2` = 9721 WHERE `ID` = 9630;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9531;
UPDATE `lazy_thin_vehicle` SET `ID` = 9750, `SeatID_1` = 9750 WHERE `ID` = 9531;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9631;
UPDATE `lazy_thin_vehicle` SET `ID` = 9850, `SeatID_2` = 9750 WHERE `ID` = 9631;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9534;
UPDATE `lazy_thin_vehicle` SET `ID` = 9751, `SeatID_1` = 9751 WHERE `ID` = 9534;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9634;
UPDATE `lazy_thin_vehicle` SET `ID` = 9851, `SeatID_2` = 9751 WHERE `ID` = 9634;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9535;
UPDATE `lazy_thin_vehicle` SET `ID` = 9752, `SeatID_1` = 9752 WHERE `ID` = 9535;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9635;
UPDATE `lazy_thin_vehicle` SET `ID` = 9852, `SeatID_2` = 9752 WHERE `ID` = 9635;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9536;
UPDATE `lazy_thin_vehicle` SET `ID` = 9753, `SeatID_1` = 9753 WHERE `ID` = 9536;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9636;
UPDATE `lazy_thin_vehicle` SET `ID` = 9853, `SeatID_2` = 9753 WHERE `ID` = 9636;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9537;
UPDATE `lazy_thin_vehicle` SET `ID` = 9754, `SeatID_1` = 9754 WHERE `ID` = 9537;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9637;
UPDATE `lazy_thin_vehicle` SET `ID` = 9854, `SeatID_2` = 9754 WHERE `ID` = 9637;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9538;
UPDATE `lazy_thin_vehicle` SET `ID` = 9755, `SeatID_1` = 9755 WHERE `ID` = 9538;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9638;
UPDATE `lazy_thin_vehicle` SET `ID` = 9855, `SeatID_2` = 9755 WHERE `ID` = 9638;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9539;
UPDATE `lazy_thin_vehicle` SET `ID` = 9756, `SeatID_1` = 9756 WHERE `ID` = 9539;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9639;
UPDATE `lazy_thin_vehicle` SET `ID` = 9856, `SeatID_2` = 9756 WHERE `ID` = 9639;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9540;
UPDATE `lazy_thin_vehicle` SET `ID` = 9757, `SeatID_1` = 9757 WHERE `ID` = 9540;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9640;
UPDATE `lazy_thin_vehicle` SET `ID` = 9857, `SeatID_2` = 9757 WHERE `ID` = 9640;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9541;
UPDATE `lazy_thin_vehicle` SET `ID` = 9758, `SeatID_1` = 9758 WHERE `ID` = 9541;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9641;
UPDATE `lazy_thin_vehicle` SET `ID` = 9858, `SeatID_2` = 9758 WHERE `ID` = 9641;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9542;
UPDATE `lazy_thin_vehicle` SET `ID` = 9759, `SeatID_1` = 9759 WHERE `ID` = 9542;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9642;
UPDATE `lazy_thin_vehicle` SET `ID` = 9859, `SeatID_2` = 9759 WHERE `ID` = 9642;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9531;
UPDATE `lazy_thin_vehicle` SET `ID` = 9760, `SeatID_1` = 9760 WHERE `ID` = 9531;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9631;
UPDATE `lazy_thin_vehicle` SET `ID` = 9860, `SeatID_2` = 9760 WHERE `ID` = 9631;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9534;
UPDATE `lazy_thin_vehicle` SET `ID` = 9761, `SeatID_1` = 9761 WHERE `ID` = 9534;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9634;
UPDATE `lazy_thin_vehicle` SET `ID` = 9861, `SeatID_2` = 9761 WHERE `ID` = 9634;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9535;
UPDATE `lazy_thin_vehicle` SET `ID` = 9762, `SeatID_1` = 9762 WHERE `ID` = 9535;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9635;
UPDATE `lazy_thin_vehicle` SET `ID` = 9862, `SeatID_2` = 9762 WHERE `ID` = 9635;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9536;
UPDATE `lazy_thin_vehicle` SET `ID` = 9763, `SeatID_1` = 9763 WHERE `ID` = 9536;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9636;
UPDATE `lazy_thin_vehicle` SET `ID` = 9863, `SeatID_2` = 9763 WHERE `ID` = 9636;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9537;
UPDATE `lazy_thin_vehicle` SET `ID` = 9764, `SeatID_1` = 9764 WHERE `ID` = 9537;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9637;
UPDATE `lazy_thin_vehicle` SET `ID` = 9864, `SeatID_2` = 9764 WHERE `ID` = 9637;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9538;
UPDATE `lazy_thin_vehicle` SET `ID` = 9765, `SeatID_1` = 9765 WHERE `ID` = 9538;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9638;
UPDATE `lazy_thin_vehicle` SET `ID` = 9865, `SeatID_2` = 9765 WHERE `ID` = 9638;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9539;
UPDATE `lazy_thin_vehicle` SET `ID` = 9766, `SeatID_1` = 9766 WHERE `ID` = 9539;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9639;
UPDATE `lazy_thin_vehicle` SET `ID` = 9866, `SeatID_2` = 9766 WHERE `ID` = 9639;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9540;
UPDATE `lazy_thin_vehicle` SET `ID` = 9767, `SeatID_1` = 9767 WHERE `ID` = 9540;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9640;
UPDATE `lazy_thin_vehicle` SET `ID` = 9867, `SeatID_2` = 9767 WHERE `ID` = 9640;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9541;
UPDATE `lazy_thin_vehicle` SET `ID` = 9768, `SeatID_1` = 9768 WHERE `ID` = 9541;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9641;
UPDATE `lazy_thin_vehicle` SET `ID` = 9868, `SeatID_2` = 9768 WHERE `ID` = 9641;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9542;
UPDATE `lazy_thin_vehicle` SET `ID` = 9769, `SeatID_1` = 9769 WHERE `ID` = 9542;
INSERT INTO `lazy_thin_vehicle` SELECT * FROM `vehicle_dbc` WHERE `ID` = 9642;
UPDATE `lazy_thin_vehicle` SET `ID` = 9869, `SeatID_2` = 9769 WHERE `ID` = 9642;
DELETE FROM `vehicle_dbc` WHERE `ID` IN (9720, 9721, 9750, 9751, 9752, 9753, 9754, 9755, 9756, 9757, 9758, 9759, 9760,
    9761, 9762, 9763, 9764, 9765, 9766, 9767, 9768, 9769, 9820, 9821, 9850, 9851, 9852, 9853, 9854, 9855, 9856, 9857,
    9858, 9859, 9860, 9861, 9862, 9863, 9864, 9865, 9866, 9867, 9868, 9869);
INSERT INTO `vehicle_dbc` SELECT * FROM `lazy_thin_vehicle`;
DROP TEMPORARY TABLE `lazy_thin_vehicle`;
