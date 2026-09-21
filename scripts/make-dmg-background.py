#!/usr/bin/env python3
"""Draw the disk image background: an arrow from the app to Applications.

Run by hand when the design changes, not at build time -- the result is checked
in as res/macos/dmg-background.tiff so a release needs nothing but CPack:

    python3 scripts/make-dmg-background.py

Writes one TIFF holding both a 1x and a 2x representation, which is how a
picture stays sharp on a Retina display and honest on anything else. The
coordinates here and the ones in res/macos/dmg-setup.applescript describe the
same window and have to be changed together.

Needs Pillow (pip install pillow) and tiffutil, which ships with macOS.
"""

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# The Finder window, in points. See dmg-setup.applescript.
WIDTH, HEIGHT = 660, 400

# Icon centres, matching the positions the AppleScript sets.
APP_X, ICON_Y = 165, 185
APPLICATIONS_X = 495

BACKGROUND = (244, 244, 242)
ARROW = (65, 168, 63)      # the green of the app icon
CAPTION = (118, 118, 115)

CAPTION_TEXT = "Drag CedarLogic into your Applications folder"
CAPTION_Y = 320
CAPTION_SIZE = 15

# Helvetica Neue rather than the system font: Pillow sets the spaces in SF Pro
# too tight to read comfortably at this size, running "into your" together.
FONT = "/System/Library/Fonts/HelveticaNeue.ttc"

# Everything is drawn this many times larger and then scaled down, which is
# cheaper than antialiasing the arrow by hand.
OVERSAMPLE = 4


def draw_arrow(d, scale):
    """A shaft and a head, pointing from the app towards Applications."""
    x0 = (APP_X + 105) * scale
    x1 = (APPLICATIONS_X - 105) * scale
    y = ICON_Y * scale
    head = 19 * scale
    shaft = 5.5 * scale

    d.rounded_rectangle(
        [x0, y - shaft / 2, x1 - head * 0.8, y + shaft / 2],
        radius=shaft / 2, fill=ARROW)
    d.polygon(
        [(x1, y), (x1 - head, y - head * 0.62), (x1 - head, y + head * 0.62)],
        fill=ARROW)


def render(scale):
    big = OVERSAMPLE * scale
    im = Image.new("RGB", (WIDTH * big, HEIGHT * big), BACKGROUND)
    draw_arrow(ImageDraw.Draw(im), big)
    im = im.resize((WIDTH * scale, HEIGHT * scale), Image.LANCZOS)

    # Text goes on afterwards, at its real size: letterforms hinted for the
    # size they are drawn at survive better than ones shrunk into place.
    d = ImageDraw.Draw(im)
    font = ImageFont.truetype(FONT, CAPTION_SIZE * scale)
    d.text((WIDTH * scale / 2, CAPTION_Y * scale), CAPTION_TEXT,
           font=font, fill=CAPTION, anchor="mm")
    return im


def main():
    out = Path(__file__).resolve().parent.parent / "res" / "macos"
    one, two = out / "dmg-1x.png", out / "dmg-2x.png"
    render(1).save(one)
    render(2).save(two)

    target = out / "dmg-background.tiff"
    subprocess.run(
        ["tiffutil", "-cathidpicheck", str(one), str(two), "-out", str(target)],
        check=True, stdout=subprocess.DEVNULL)
    one.unlink()
    two.unlink()
    print(f"wrote {target}")


if __name__ == "__main__":
    sys.exit(main())
