"""
Generates the UartX brand assets and the small UI glyphs.

    python make_icon.py

Writes into resources/:
    uartx.ico            square app icon  - white "UartX", transparent bg

and into resources/icons/:
    uartx_<N>.png        the same icon at each size, for Qt
    uartx_logo.png       wide wordmark    - white "UartX", transparent bg,
                         tinted at run time to suit the background
    chevron_down.png     combo-box arrow
    chevron_down_dim.png combo-box arrow, disabled
    check.png            checkbox tick

Requires Pillow:  pip install pillow
"""

import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
# The script lives in scripts/; the assets it generates belong with the rest
# of the resources, so every path is resolved from the repository root.
REPO_ROOT = os.path.dirname(HERE)
RESOURCES = os.path.join(REPO_ROOT, "resources")
ICONS = os.path.join(RESOURCES, "icons")

TRANSPARENT = (0, 0, 0, 0)
WHITE = (255, 255, 255, 255)
# A soft dark rim keeps the white lettering readable on a light taskbar or
# file manager without painting a solid box behind it.
RIM = (0, 0, 0, 150)
WORDMARK = "UartX"


def _load_font(px):
    """A bold sans face, falling back through the usual suspects."""
    for name in ("segoeuib.ttf", "arialbd.ttf", "calibrib.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, px)
        except OSError:
            continue
    return ImageFont.load_default()


def _fit_font(text, target_w, target_h, start_px):
    """Largest font size whose rendered text fits the target box."""
    probe = ImageDraw.Draw(Image.new("RGB", (1, 1)))
    size = start_px
    while size > 6:
        font = _load_font(size)
        box = probe.textbbox((0, 0), text, font=font)
        if (box[2] - box[0]) <= target_w and (box[3] - box[1]) <= target_h:
            return font, box
        size -= 1
    font = _load_font(6)
    return font, probe.textbbox((0, 0), text, font=font)


def _draw_wordmark(img, fill_ratio=0.86, rim=0):
    """Centres the wordmark on a transparent canvas."""
    w, h = img.size
    font, box = _fit_font(WORDMARK, int(w * fill_ratio), int(h * 0.72), int(h))
    tw, th = box[2] - box[0], box[3] - box[1]
    d = ImageDraw.Draw(img)
    d.text(((w - tw) // 2 - box[0], (h - th) // 2 - box[1]), WORDMARK, font=font,
           fill=WHITE, stroke_width=rim, stroke_fill=RIM if rim else None)
    return img


def make_app_icon(size=512):
    """Square icon: white UartX on a transparent background.

    The lettering carries a faint dark rim so it stays legible against light
    backgrounds as well as dark ones.
    """
    rim = max(1, round(size / 38))
    return _draw_wordmark(Image.new("RGBA", (size, size), TRANSPARENT),
                          fill_ratio=0.86, rim=rim)


def make_logo(width=640, height=200):
    """Wide wordmark for the About box.

    Plain white on transparent, with no rim: the application tints this at
    run time to whatever contrasts with the surface it is drawn on, so it
    doubles as a mask (see Theme::wordmark).
    """
    return _draw_wordmark(Image.new("RGBA", (width, height), TRANSPARENT),
                          fill_ratio=0.80)


# ---------------------------------------------------------------------------
# Small UI glyphs
#
# Qt draws no arrow of its own once QComboBox::drop-down is styled, so the
# chevron has to be supplied as an image or the control stops looking like a
# dropdown at all. Same story for the check mark on a styled checkbox.
# ---------------------------------------------------------------------------

def make_chevron(size=32, color=(200, 206, 214), width=3):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = size / 32.0
    d.line([(9 * s, 13 * s), (16 * s, 20 * s), (23 * s, 13 * s)],
           fill=color, width=max(1, int(width * s)), joint="curve")
    return img


def make_check(size=32, color=(245, 245, 245), width=4):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = size / 32.0
    d.line([(8 * s, 17 * s), (14 * s, 23 * s), (24 * s, 10 * s)],
           fill=color, width=max(1, int(width * s)), joint="curve")
    return img


def main():
    # The .ico carries several frames. Each is rendered at its own size rather
    # than downscaled from one master, so the lettering stays as crisp as the
    # pixel budget allows at small sizes.
    sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = [make_app_icon(s) for s in sizes]
    ico_path = os.path.join(RESOURCES, "uartx.ico")
    frames[-1].save(ico_path, format="ICO",
                    sizes=[(s, s) for s in sizes],
                    append_images=frames[:-1])
    print("wrote", ico_path)

    # PNG copies of every frame. Qt loads these without needing the ICO image
    # plugin, which is not always deployed.
    for s, img in zip(sizes, frames):
        path = os.path.join(ICONS, "uartx_%d.png" % s)
        img.save(path)
        print("wrote", path)

    for name, img in (
        ("uartx_logo.png",       make_logo()),
        ("chevron_down.png",     make_chevron(color=(200, 206, 214))),
        ("chevron_down_dim.png", make_chevron(color=(110, 110, 110))),
        ("check.png",            make_check()),
    ):
        path = os.path.join(ICONS, name)
        img.save(path)
        print("wrote", path)


if __name__ == "__main__":
    main()
