#!/usr/bin/env python3
"""Generate Chimer's bell icons: a 25x25 white menu icon (transparent) for the
launcher, and a 144x144 marketing icon (white bell on the app's black theme)."""
import os
from PIL import Image, ImageDraw


def draw_bell(size, fg, bg=None, bg_radius=0):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if bg is not None:
        d.rounded_rectangle([0, 0, size - 1, size - 1], radius=bg_radius, fill=bg)

    s = size / 25.0
    def S(seq):  # scale a list of (x, y) or a bbox
        return [v * s for v in seq]

    # top knob
    d.ellipse(S([11, 1.5, 14, 5]), fill=fg)
    # dome (top half of an ellipse)
    d.pieslice(S([5, 4, 20, 20]), 180, 360, fill=fg)
    # flared body
    d.polygon(S([5, 12, 3.5, 18, 21.5, 18, 20, 12]), fill=fg)
    # bottom rim
    d.rounded_rectangle(S([3, 16.5, 22, 18.5]), radius=1 * s, fill=fg)
    # clapper
    d.ellipse(S([10.5, 19.5, 14.5, 23.5]), fill=fg)
    return img


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    img_dir = os.path.join(here, "resources", "images")
    os.makedirs(img_dir, exist_ok=True)

    # Launcher menu icon: white bell, transparent background, small.
    menu = draw_bell(25, fg=(255, 255, 255, 255))
    menu.save(os.path.join(img_dir, "menu_icon.png"))

    # Marketing icon: white bell on the app's black rounded square.
    marketing = draw_bell(144, fg=(255, 255, 255, 255), bg=(0, 0, 0, 255), bg_radius=28)
    marketing.save(os.path.join(here, "icon_144.png"))

    print("wrote", os.path.join(img_dir, "menu_icon.png"))
    print("wrote", os.path.join(here, "icon_144.png"))


if __name__ == "__main__":
    main()
