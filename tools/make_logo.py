#!/usr/bin/env python3
"""Builds content/icons from Groot's tree icon (MIT, BehaviorTree/Groot groot_icon.png): a "!" in front of the tree.

    python3 tools/make_logo.py path/to/groot_icon.png
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

source = Image.open(sys.argv[1]).convert("LA")
tree = Image.new("RGBA", source.size)
tree.putalpha(source.getchannel("A"))  # black strokes, the icon's own alpha
bbox = tree.getbbox()
tree = tree.crop(bbox)

# The "!": a stroke as thick as the tree's lines, as tall as the tree, with a round dot under it.
h = tree.height
stroke = 15
gap = 26
mark = Image.new("RGBA", (stroke, h))
draw = ImageDraw.Draw(mark)
dot = stroke
draw.rounded_rectangle((0, 8, stroke - 1, h - dot - 22), radius=stroke // 2, fill=(0, 0, 0, 255))
draw.ellipse((0, h - dot, stroke - 1, h - 1), fill=(0, 0, 0, 255))

logo = Image.new("RGBA", (stroke + gap + tree.width, h))
logo.alpha_composite(mark, (0, 0))
logo.alpha_composite(tree, (stroke + gap, 0))

out = Path(__file__).resolve().parent.parent / "content" / "icons"
out.mkdir(parents=True, exist_ok=True)


def square(image, size, pad, tile):
    canvas = Image.new("RGBA", (size, size))
    if tile:
        ImageDraw.Draw(canvas).rounded_rectangle((0, 0, size - 1, size - 1), radius=size // 6, fill=tile)
    inner = size - 2 * pad
    scale = inner / max(image.width, image.height)
    fitted = image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))), Image.LANCZOS)
    canvas.alpha_composite(fitted, ((size - fitted.width) // 2, (size - fitted.height) // 2))
    return canvas


# The mark itself (black on clear, tinted by the app), and the window icon on a light tile so it reads on dark docks.
logo.save(out / "notgroot_mark.png")
for size in (16, 32, 48, 64, 128, 256):
    square(logo, size, max(1, size // 12), (243, 245, 247, 255)).save(out / f"notgroot_{size}.png")
