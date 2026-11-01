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


RACES = {
    "twoheadedogre": (OGRE_BOUNCE, Bounce, OGRE_BODY),
    "thinhumanmale": (THIN_TWINS, h.Handspring, THIN_HUMAN_BODY),
}


def vault_of(name):
    found = RACES.get(name.lower())
    if not found:
        raise ValueError(f"no subclass race model called {name}")
    return found


def add_vault(model, skin, name):
    style, move, body = vault_of(name)
    return h.add_handsprings(model, style, skin, move, body)


def main():
    parser = argparse.ArgumentParser(description="Add the subclass races' Vault to their models")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("Use a separate output; do not overwrite the source model")
    skin = args.source.with_name(args.source.stem + "00.skin")
    result = add_vault(args.source.read_bytes(), skin.read_bytes() if skin.exists() else None, args.source.stem)
    with args.output.open("xb") as output:
        output.write(result)


if __name__ == "__main__":
    main()
