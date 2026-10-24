"""Append the twin's vehicles to Vehicle.dbc / VehicleSeat.dbc and emit their SQL and the mount rider poses."""
import os
import struct
import sys

SRC = sys.argv[1]
OUT = sys.argv[2]
PIGGYBACK, SADDLE, WOLF_BACK, CAT_BACK = 9530, 9531, 9532, 9533
PIGGYBACK_SEAT, SADDLE_SEAT, WOLF_BACK_SEAT, CAT_BACK_SEAT = 9530, 9531, 9532, 9533
SHOULDER_RIDE = SHOULDER_RIDE_SEAT = 9598
TEMPLATE_VEHICLE, TEMPLATE_SEAT = 312, 2764
SEAT_CAN_ENTER_OR_EXIT, SEAT_CAN_SWITCH = 0x02000000, 0x04000000
CHEST_BACK_SEAT, CHEST_SEAT, MOUNT_RIDER_SEAT = 10, 1, 21
PIGGYBACK_OFFSET, PIGGYBACK_PITCH = (0.05, 0.0, -0.35), -0.35
RIDER_BEHIND_OFFSET = (-0.28, 0.0, 0.0)
WOLF_BACK_OFFSET, CAT_BACK_OFFSET, CAT_BACK_PITCH = (-0.4, 0.0, 0.0), (-0.53, 0.0, 0.81), 0.55
SHOULDER_RIDE_OFFSET, SHOULDER_RIDE_PITCH = (0.02, 0.01, 0.26), -0.8
FIRST_POSE_SEAT, MOUNT_POSE, SIT_CHAIR_LOW, NO_ANIMATION = 9534, 91, 102, 0xFFFFFFFF
CHAIR_FROM_RECLINED = (0.2, 0.0, -0.31)
CAR_SEATS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "car_seats.tsv")
SPELL_AURAS, SPELL_VISUALS, VISUAL_STATE_KIT, KIT_ANIMATION, MOUNTED_AURA = 95, 131, 4, 2, 78
SEAT_COLUMNS = ("ID Flags AttachmentID AttachmentOffsetX AttachmentOffsetY AttachmentOffsetZ EnterPreDelay EnterSpeed "
    "EnterGravity EnterMinDuration EnterMaxDuration EnterMinArcHeight EnterMaxArcHeight EnterAnimStart EnterAnimLoop "
    "RideAnimStart RideAnimLoop RideUpperAnimStart RideUpperAnimLoop ExitPreDelay ExitSpeed ExitGravity ExitMinDuration "
    "ExitMaxDuration ExitMinArcHeight ExitMaxArcHeight ExitAnimStart ExitAnimLoop ExitAnimEnd PassengerYaw "
    "PassengerPitch PassengerRoll PassengerAttachmentID VehicleEnterAnim VehicleExitAnim VehicleRideAnimLoop "
    "VehicleEnterAnimBone VehicleExitAnimBone VehicleRideAnimLoopBone VehicleEnterAnimDelay VehicleExitAnimDelay "
    "VehicleAbilityDisplay EnterUISoundID ExitUISoundID UiSkin FlagsB CameraEnteringDelay CameraEnteringDuration "
    "CameraExitingDelay CameraExitingDuration CameraOffsetX CameraOffsetY CameraOffsetZ CameraPosChaseRate "
    "CameraFacingChaseRate CameraEnteringZoom CameraSeatZoomMin CameraSeatZoomMax").split()
SEAT_FORMAT = "niiffffffffffiiiiiifffffffiiifffiiiiiiiffiiiii" + "f" * 12
VEHICLE_COLUMNS = ("ID Flags TurnSpeed PitchSpeed PitchMin PitchMax SeatID_1 SeatID_2 SeatID_3 SeatID_4 SeatID_5 "
    "SeatID_6 SeatID_7 SeatID_8 MouseLookOffsetPitch CameraFadeDistScalarMin CameraFadeDistScalarMax "
    "CameraPitchOffset FacingLimitRight FacingLimitLeft MsslTrgtTurnLingering MsslTrgtPitchLingering "
    "MsslTrgtMouseLingering MsslTrgtEndOpacity MsslTrgtArcSpeed MsslTrgtArcRepeat MsslTrgtArcWidth "
    "MsslTrgtImpactRadius_1 MsslTrgtImpactRadius_2 MsslTrgtArcTexture MsslTrgtImpactTexture MsslTrgtImpactModel_1 "
    "MsslTrgtImpactModel_2 CameraYawOffset UilocomotionType MsslTrgtImpactTexRadius VehicleUIIndicatorID "
    "PowerDisplayID_1 PowerDisplayID_2 PowerDisplayID_3").split()
VEHICLE_FORMAT = "niffffiiiiiiiifffffffffffffffssssfifiiii"


def load(path):
    data = open(path, "rb").read()
    _, count, fields, size, strings = struct.unpack_from("<4s4I", data)
    rows = [bytearray(data[20 + i * size:20 + (i + 1) * size]) for i in range(count)]
    return data, fields, size, rows, data[20 + count * size:]


def save(path, fields, size, rows, strings):
    with open(path, "wb") as out:
        out.write(struct.pack("<4s4I", b"WDBC", len(rows), fields, size, len(strings)))
        for row in rows:
            out.write(row)
        out.write(strings)


def find(rows, row_id):
    return next(row for row in rows if struct.unpack_from("<I", row)[0] == row_id)


def column(name, columns):
    return columns.index(name) * 4


def seat(rows, seat_id, attachment, offset, pitch=0.0, ride=MOUNT_POSE):
    row = bytearray(find(rows, TEMPLATE_SEAT))
    struct.pack_into("<i", row, column("RideAnimLoop", SEAT_COLUMNS), ride)
    struct.pack_into("<f", row, column("PassengerPitch", SEAT_COLUMNS), pitch)
    struct.pack_into("<I", row, 0, seat_id)
    struct.pack_into("<I", row, column("AttachmentID", SEAT_COLUMNS), attachment)
    struct.pack_into("<3f", row, column("AttachmentOffsetX", SEAT_COLUMNS), *offset)
    flags = struct.unpack_from("<I", row, column("Flags", SEAT_COLUMNS))[0]
    struct.pack_into("<I", row, column("Flags", SEAT_COLUMNS), flags & ~(SEAT_CAN_ENTER_OR_EXIT | SEAT_CAN_SWITCH))
    return row


def vehicle(rows, vehicle_id, seat_id):
    row = bytearray(find(rows, TEMPLATE_VEHICLE))
    struct.pack_into("<I", row, 0, vehicle_id)
    struct.pack_into("<8I", row, column("SeatID_1", VEHICLE_COLUMNS), 0, seat_id, 0, 0, 0, 0, 0, 0)
    struct.pack_into("<I", row, column("VehicleUIIndicatorID", VEHICLE_COLUMNS), 0)
    return row


def sql(table, columns, fmt, rows, strings):
    lines = []
    for row in rows:
        values = []
        for index, kind in enumerate(fmt[:len(columns)]):
            if kind == "f":
                values.append(repr(round(struct.unpack_from("<f", row, index * 4)[0], 6)))
            elif kind == "s":
                offset = struct.unpack_from("<I", row, index * 4)[0]
                text = strings[offset:strings.index(b"\0", offset)].decode()
                values.append("'" + text.replace("'", "''") + "'")
            else:
                values.append(str(struct.unpack_from("<i", row, index * 4)[0]))
        lines.append("(" + ", ".join(values) + ")")
    ids = ", ".join(str(struct.unpack_from("<I", row)[0]) for row in rows)
    cols = ", ".join("`%s`" % name for name in columns)
    return (f"DELETE FROM `{table}` WHERE `ID` IN ({ids});\nINSERT INTO `{table}` ({cols}) VALUES\n" +
            ",\n".join(lines) + ";\n")


def table(name):
    _, fields, _, rows, _ = load(f"{SRC}/{name}.dbc")
    return {values[0]: values for values in (struct.unpack(f"<{fields}I", row) for row in rows)}


def rider_poses():
    visuals, kits = table("SpellVisual"), table("SpellVisualKit")
    poses = {}
    for spell_id, spell in table("Spell").items():
        if MOUNTED_AURA not in spell[SPELL_AURAS:SPELL_AURAS + 3]:
            continue
        for visual_id in spell[SPELL_VISUALS:SPELL_VISUALS + 2]:
            kit = kits.get(visuals.get(visual_id, (0,) * 8)[VISUAL_STATE_KIT])
            if kit and kit[KIT_ANIMATION] != NO_ANIMATION:
                if kit[KIT_ANIMATION] != MOUNT_POSE:
                    poses[spell_id] = kit[KIT_ANIMATION]
                break
    return poses


def pose_sql(seat_of):
    rows = ",\n".join(f"({spell}, {vehicle_id})" for spell, vehicle_id in sorted(seat_of.items()))
    return ("CREATE TABLE IF NOT EXISTS `mod_subclass_rider_pose` (\n"
            "    `SpellId` INT UNSIGNED NOT NULL,\n"
            "    `VehicleId` INT UNSIGNED NOT NULL,\n"
            "    PRIMARY KEY (`SpellId`)\n"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;\n\n"
            "DELETE FROM `mod_subclass_rider_pose`;\n"
            f"INSERT INTO `mod_subclass_rider_pose` (`SpellId`, `VehicleId`) VALUES\n{rows};\n")


def car_seats():
    seats = {}
    for line in open(CAR_SEATS, encoding="utf-8"):
        if line.strip() and not line.startswith("#"):
            spell, x, y, z = line.split("\t")[:4]
            seats[int(spell)] = (float(x), float(y), float(z))
    return seats


poses = rider_poses()
pose_ids = {anim: FIRST_POSE_SEAT + index for index, anim in enumerate(sorted(set(poses.values())))}
cars = car_seats()
car_ids = {offset: FIRST_POSE_SEAT + len(pose_ids) + index
           for index, offset in enumerate(sorted(set(cars.values())))}
_, seat_fields, seat_size, seats, seat_strings = load(f"{SRC}/VehicleSeat.dbc")
_, vehicle_fields, vehicle_size, vehicles, vehicle_strings = load(f"{SRC}/Vehicle.dbc")
new_seats = [seat(seats, PIGGYBACK_SEAT, CHEST_BACK_SEAT, PIGGYBACK_OFFSET, PIGGYBACK_PITCH),
             seat(seats, SADDLE_SEAT, MOUNT_RIDER_SEAT, RIDER_BEHIND_OFFSET),
             seat(seats, WOLF_BACK_SEAT, MOUNT_RIDER_SEAT, WOLF_BACK_OFFSET),
             seat(seats, CAT_BACK_SEAT, CHEST_SEAT, CAT_BACK_OFFSET, CAT_BACK_PITCH),
             seat(seats, SHOULDER_RIDE_SEAT, CHEST_BACK_SEAT, SHOULDER_RIDE_OFFSET, SHOULDER_RIDE_PITCH)]
new_vehicles = [vehicle(vehicles, PIGGYBACK, PIGGYBACK_SEAT), vehicle(vehicles, SADDLE, SADDLE_SEAT),
                vehicle(vehicles, WOLF_BACK, WOLF_BACK_SEAT), vehicle(vehicles, CAT_BACK, CAT_BACK_SEAT),
                vehicle(vehicles, SHOULDER_RIDE, SHOULDER_RIDE_SEAT)]
for anim, pose_id in pose_ids.items():
    new_seats.append(seat(seats, pose_id, MOUNT_RIDER_SEAT, RIDER_BEHIND_OFFSET, ride=anim))
    new_vehicles.append(vehicle(vehicles, pose_id, pose_id))
for offset, car_id in car_ids.items():
    chair = tuple(round(value + shift, 3) for value, shift in zip(offset, CHAIR_FROM_RECLINED))
    new_seats.append(seat(seats, car_id, MOUNT_RIDER_SEAT, chair, ride=SIT_CHAIR_LOW))
    new_vehicles.append(vehicle(vehicles, car_id, car_id))
seat_ids = {struct.unpack_from("<I", row)[0] for row in new_seats}
vehicle_ids = {struct.unpack_from("<I", row)[0] for row in new_vehicles}
seats = [row for row in seats if struct.unpack_from("<I", row)[0] not in seat_ids] + new_seats
vehicles = [row for row in vehicles if struct.unpack_from("<I", row)[0] not in vehicle_ids] + new_vehicles
save(f"{OUT}/VehicleSeat.dbc", seat_fields, seat_size, seats, seat_strings)
save(f"{OUT}/Vehicle.dbc", vehicle_fields, vehicle_size, vehicles, vehicle_strings)
print(sql("vehicleseat_dbc", SEAT_COLUMNS, SEAT_FORMAT, new_seats, seat_strings))
print(sql("vehicle_dbc", VEHICLE_COLUMNS, VEHICLE_FORMAT, new_vehicles, vehicle_strings))
seat_of = {spell: pose_ids[anim] for spell, anim in poses.items()}
seat_of.update({spell: car_ids[offset] for spell, offset in cars.items()})
print(pose_sql(seat_of))
