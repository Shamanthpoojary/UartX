"""
Generates every brand asset from the logo masters, plus the small UI glyphs.

    python scripts/make_icon.py

Reads resources/logo/uartx-512-{white,black}.png -- the same artwork in two
inks on a transparent background -- and writes:

    resources/uartx.ico              app icon, white logo on a dark plate
    resources/icons/uartx_<N>.png    the same icon at each size, for Qt and
                                     for the Linux icon theme
    resources/icons/uartx_logo.png   white on transparent, no plate: this one
                                     is a mask, tinted at run time to contrast
                                     with whatever it is drawn on
    docs/Images/wordmark-light.png   black ink, for the README on a light page
    docs/Images/wordmark-dark.png    white ink, for the README in dark mode
    resources/icons/chevron_down*.png, check.png    small UI glyphs

The app icon gets a plate because it is the one asset that cannot adapt: it
lands on a taskbar or launcher that may be light or dark, and a transparent
logo in a single ink disappears against one of them.

Requires Pillow:  pip install pillow
"""

import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
# The script lives in scripts/; every path is resolved from the repo root.
REPO_ROOT = os.path.dirname(HERE)
RESOURCES = os.path.join(REPO_ROOT, "resources")
ICONS = os.path.join(RESOURCES, "icons")
LOGO_DIR = os.path.join(RESOURCES, "logo")
DOCS_IMAGES = os.path.join(REPO_ROOT, "docs", "Images")

MASTER_WHITE = os.path.join(LOGO_DIR, "uartx-512-white.png")
MASTER_BLACK = os.path.join(LOGO_DIR, "uartx-512-black.png")

TRANSPARENT = (0, 0, 0, 0)
# The plate is the application's own background colour, so the icon and the
# window it opens are visibly the same product.
PLATE = (20, 20, 20, 255)          # #141414
PLATE_EDGE = (58, 58, 58, 255)     # a hairline so the plate still reads on black
PLATE_RADIUS = 0.22                # of the icon's width
LOGO_INSET = 0.14                  # padding between plate edge and artwork


def _master(path):
    if not os.path.exists(path):
        raise SystemExit("missing logo master: %s" % path)
    return Image.open(path).convert("RGBA")


def _trim(img):
    """Crops to the artwork, so padding is ours to control rather than the
    exporter's."""
    box = img.getbbox()
    return img.crop(box) if box else img


def _fit(img, box_w, box_h):
    """Scales to fit inside the box, preserving aspect ratio."""
    w, h = img.size
    scale = min(box_w / w, box_h / h)
    return img.resize((max(1, round(w * scale)), max(1, round(h * scale))),
                      Image.LANCZOS)


def make_app_icon(master, size):
    """White artwork centred on a dark rounded plate."""
    canvas = Image.new("RGBA", (size, size), TRANSPARENT)
    radius = max(2, round(size * PLATE_RADIUS))

    plate = Image.new("RGBA", (size, size), TRANSPARENT)
    d = ImageDraw.Draw(plate)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=PLATE,
                        outline=PLATE_EDGE, width=1 if size >= 32 else 0)
    canvas.alpha_composite(plate)

    inset = round(size * LOGO_INSET)
    art = _fit(_trim(master), size - 2 * inset, size - 2 * inset)
    canvas.alpha_composite(art, ((size - art.size[0]) // 2,
                                 (size - art.size[1]) // 2))
    return canvas


def make_flat(master, size):
    """The artwork alone on transparency, trimmed and squared.

    Used where the surface is already known, or where the image is a mask the
    application re-inks at run time.
    """
    canvas = Image.new("RGBA", (size, size), TRANSPARENT)
    art = _fit(_trim(master), size, size)
    canvas.alpha_composite(art, ((size - art.size[0]) // 2,
                                 (size - art.size[1]) // 2))
    return canvas


# ---------------------------------------------------------------------------
# Small UI glyphs
#
# Qt draws no arrow of its own once QComboBox::drop-down is styled, so the
# chevron has to be supplied as an image or the control stops looking like a
# dropdown at all. Same story for the check mark on a styled checkbox.
# ---------------------------------------------------------------------------

def make_chevron(size=32, color=(200, 206, 214), width=3):
    img = Image.new("RGBA", (size, size), TRANSPARENT)
    d = ImageDraw.Draw(img)
    s = size / 32.0
    d.line([(9 * s, 13 * s), (16 * s, 20 * s), (23 * s, 13 * s)],
           fill=color, width=max(1, int(width * s)), joint="curve")
    return img


def make_check(size=32, color=(245, 245, 245), width=4):
    img = Image.new("RGBA", (size, size), TRANSPARENT)
    d = ImageDraw.Draw(img)
    s = size / 32.0
    d.line([(8 * s, 17 * s), (14 * s, 23 * s), (24 * s, 10 * s)],
           fill=color, width=max(1, int(width * s)), joint="curve")
    return img


def main():
    white = _master(MASTER_WHITE)
    black = _master(MASTER_BLACK)
    os.makedirs(DOCS_IMAGES, exist_ok=True)

    # Each frame is composed at its own size rather than downscaled from one
    # master, so the plate's corner radius and hairline stay crisp when small.
    sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = [make_app_icon(white, s) for s in sizes]

    ico_path = os.path.join(RESOURCES, "uartx.ico")
    frames[-1].save(ico_path, format="ICO", sizes=[(s, s) for s in sizes],
                    append_images=frames[:-1])
    print("wrote", ico_path)

    # PNG copies of every frame. Qt loads these without needing the ICO image
    # plugin, which is not always deployed, and the Linux icon theme wants PNGs.
    for s, img in zip(sizes, frames):
        path = os.path.join(ICONS, "uartx_%d.png" % s)
        img.save(path)
        print("wrote", path)

    # The About-box logo is a mask: Theme::wordmark() tints it to whatever
    # contrasts with the dialog, so it must be plain white with no plate.
    logo_path = os.path.join(ICONS, "uartx_logo.png")
    make_flat(white, 512).save(logo_path)
    print("wrote", logo_path)

    # The README cannot tint an image, so both inks ship and a <picture>
    # element picks one from the reader's theme.
    for master, name in ((white, "wordmark-dark.png"), (black, "wordmark-light.png")):
        path = os.path.join(DOCS_IMAGES, name)
        make_flat(master, 512).save(path)
        print("wrote", path)

    for name, img in (
        ("chevron_down.png",     make_chevron(color=(200, 206, 214))),
        ("chevron_down_dim.png", make_chevron(color=(110, 110, 110))),
        ("check.png",            make_check()),
    ):
        path = os.path.join(ICONS, name)
        img.save(path)
        print("wrote", path)


if __name__ == "__main__":
    main()
