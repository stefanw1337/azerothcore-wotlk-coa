import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import patch_handspring_client as h  # noqa: E402

OGRE_BOUNCE = h.Style(900, (False,))
THIN_TWINS = h.HANDSPRING_STYLE
OGRE_BODY = (0,)
THIN_HUMAN_BODY = (10000, 401, 501)
LEAP_START, LEAP_END = 0.08, 0.4
BOUNCE_START, BOUNCE_END = 0.46, 0.8
SIT_DOWN, STAND_UP_START, STAND_UP_END = 0.36, 0.48, 0.7
LEAP_HEIGHT, BOUNCE_HEIGHT = 0.55, 0.4
SEAT_LEAN = -0.35
TWIN_SPRINGBOARD, TWIN_BACK_SPRINGBOARD, TWIN_BRACE = 1796, 1797, 1798
TWIN_ANIMATIONS = {TWIN_SPRINGBOARD: ("TwinSpringboard", h.JUMP, h.JUMP_START),
                   TWIN_BACK_SPRINGBOARD: ("TwinBackSpringboard", h.JUMP, h.JUMP_START),
                   TWIN_BRACE: ("TwinBrace", h.STAND, h.STAND)}
TWIN_KITS = {3455902: TWIN_SPRINGBOARD, 3455903: TWIN_BACK_SPRINGBOARD, 3455904: TWIN_BRACE}
BRACE_MS, BACK_SHARE = 800, 1.08
BEND_START, BENT, STRAIGHTEN_START, STRAIGHT = 0.0, 0.1, 0.55, 0.85
WAIST_BEND, HEAD_UP = 1.45, -0.6
HANDS_ON_KNEES = (-0.6, 0.0, -1.0)


def bump(share, start, end):
    inside = (share - start) / (end - start)
    return math.sin(math.pi * inside) if 0.0 < inside < 1.0 else 0.0


class Bounce(h.Handspring):
    def __init__(self, model, animation, backward, style):
        super().__init__(model, animation, backward, style)
        self.seated = model.sequences.get(h.SIT_GROUND)
        if self.seated is None:
            raise ValueError("model needs a SitGround sequence to land on")
        self.hips = model.pivots[self.root][2]

    def sitting(self, share):
        return min(h.window(share, LEAP_END - 0.07, SIT_DOWN + 0.07), 1.0 - h.window(share, STAND_UP_START,
                                                                                         STAND_UP_END))

    def progress(self, share):
        return 0.0

    def pitch(self, share):
        return h.about(h.FLIP_AXIS, SEAT_LEAN * self.sitting(share))

    def posed(self, bone, track, share):
        value = super().posed(bone, track, share)
        sitting = self.sitting(share)
        if track != h.ROTATION or bone in self.arm_bones or sitting <= 0.0:
            return value
        seated = self.model.value(bone, track, self.seated, 0)
        return h.slerp(value, seated, sitting) if seated is not None else value

    def hop(self, share):
        leap = LEAP_HEIGHT * bump(share, LEAP_START, LEAP_END)
        bounce = BOUNCE_HEIGHT * bump(share, BOUNCE_START, BOUNCE_END)
        return self.hips * (leap + bounce)


class Springboard(h.Handspring):
    def __init__(self, model, animation, backward, style):
        super().__init__(model, animation, backward, style)
        self.back = BACK_SHARE * model.pivots[self.root][2]

    def over(self, share):
        _, phase = self.phase(share)
        return min(h.window(phase, h.PLANT_RISE, h.PLANT_FULL), 1.0 - h.window(phase, h.PLANT_GONE, 1.0))

    def lift(self, share):
        return super().lift(share) + self.back * self.over(share)


class Brace(h.Handspring):
    def __init__(self, model, animation):
        super().__init__(model, animation, False, h.Style(BRACE_MS, (False,)))
        self.head = self.spine_bones[0] if self.spine_bones else -1
        self.hips = {bone for bone in self.leg_bones if self.model.parents[bone] == self.root}

    def bending(self, share):
        return min(h.window(share, BEND_START, BENT), 1.0 - h.window(share, STRAIGHTEN_START, STRAIGHT))

    def source(self, share):
        return self.stand, 0

    def progress(self, share):
        return 0.0

    def hop(self, share):
        return 0.0

    def bend(self, share):
        return h.about(h.FLIP_AXIS, WAIST_BEND * self.bending(share))

    def root_rotation(self, share):
        return h.product(self.bend(share), self.posed(self.root, h.ROTATION, share))

    def posed(self, bone, track, share):
        value = super().posed(bone, track, share)
        if track != h.ROTATION:
            return value
        if bone == self.head:
            return h.product(h.about(h.FLIP_AXIS, HEAD_UP * self.bending(share)), value)
        if bone in self.hips:
            hips = super().posed(self.root, h.ROTATION, share)
            upright = h.product(h.conjugate(hips), h.product(h.conjugate(self.bend(share)), hips))
            return h.product(upright, value)
        return value

    def arm_rotations(self, share):
        weight = self.bending(share)
        overrides = {}
        for chain in self.arms:
            _, bind_direction = self.arm_reach(chain)
            above = self.world_rotation(self.model.parents[chain[0]], share, {})
            standing = h.product(above, self.posed(chain[0], h.ROTATION, share))
            held = h.rotate(standing, bind_direction)
            direction = h.unit(tuple(a + (b - a) * weight for a, b in zip(held, h.unit(HANDS_ON_KNEES))))
            overrides[chain[0]] = h.product(h.conjugate(above), h.product(h.turning(held, direction), standing))
        return overrides


RACES = {
    "twoheadedogre": (OGRE_BOUNCE, Bounce, OGRE_BODY),
    "thinhumanmale": (THIN_TWINS, h.Handspring, THIN_HUMAN_BODY),
}


def vault_of(name):
    found = RACES.get(name.lower())
    if not found:
        raise ValueError(f"no subclass race model called {name}")
    return found


def add_vault(data, skin, name):
    style, move, body = vault_of(name)
    if move is not h.Handspring:
        return h.add_handsprings(data, style, skin, move, body)
    model = h.Model(data, h.body_vertices(skin, body) if skin else None)
    return h.add_moves(model, [h.Handspring(model, h.HANDSPRING, False, style),
                               h.Handspring(model, h.BACK_HANDSPRING, True, style),
                               Springboard(model, TWIN_SPRINGBOARD, False, style),
                               Springboard(model, TWIN_BACK_SPRINGBOARD, True, style),
                               Brace(model, TWIN_BRACE)])


def add_twin_animation_rows(dbc):
    def made(by_id, texts):
        added = []
        for animation, (name, template, fallback) in sorted(TWIN_ANIMATIONS.items()):
            row = list(by_id[template])
            row[0], row[1] = animation, len(texts)
            texts += name.encode() + b"\0"
            row[h.ANIMATION_WEAPON_FLAGS] = h.WEAPONS_SHEATHED
            row[5], row[6] = fallback, animation
            added.append(row)
        return added

    return h.appended_rows(dbc, h.ANIMATION_FIELDS, made)


def add_twin_kits(dbc):
    def made(by_id, _):
        added = []
        for kit, animation in TWIN_KITS.items():
            row = list(by_id[h.VAULT_DUST_KIT])
            row[0], row[h.KIT_ANIMATION] = kit, animation
            added.append(row)
        return added

    return h.appended_rows(dbc, h.KIT_FIELDS, made)


TABLES = {"animationdata": add_twin_animation_rows, "spellvisualkit": add_twin_kits}


def main():
    parser = argparse.ArgumentParser(
        description="Add the subclass races' Vault to their models, AnimationData.dbc and SpellVisualKit.dbc")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("Use a separate output; do not overwrite the source model")
    table = TABLES.get(args.source.stem.lower())
    if table:
        result = table(args.source.read_bytes())
    else:
        skin = args.source.with_name(args.source.stem + "00.skin")
        result = add_vault(args.source.read_bytes(), skin.read_bytes() if skin.exists() else None, args.source.stem)
    with args.output.open("xb") as output:
        output.write(result)


if __name__ == "__main__":
    main()
