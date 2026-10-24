#!/usr/bin/env python3
"""Regenerate the original CHOS Horizon desktop wallpaper (requires Pillow)."""
from __future__ import annotations
import math
import random
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets/wallpaper/custom.png"
random.seed(0xC405)
W, H = 1024, 768

im = Image.new("RGB", (W, H))
pix = im.load()
for y in range(H):
    t = y / (H - 1)
    base = (int(45 + 155 * t**1.7), int(138 + 93 * t**1.25), int(214 + 24 * t))
    for x in range(W):
        glow = max(0.0, 1.0 - math.sqrt(((x - 765) / 850) ** 2 + ((y - 230) / 620) ** 2))
        pix[x, y] = (min(255, int(base[0] + 17 * glow)),
                     min(255, int(base[1] + 8 * glow)),
                     min(255, int(base[2] + 2 * glow)))

# Cotton-like high clouds with soft but defined edges.
cloud = Image.new("RGBA", (W, H), (0, 0, 0, 0))
draw = ImageDraw.Draw(cloud)
def cloud_shape(cx: int, cy: int, scale: float, alpha: int) -> None:
    lobes = [(-65, 9, 43, 23), (-36, -7, 42, 31), (0, -16, 48, 39),
             (40, -5, 44, 31), (72, 11, 31, 21), (-6, 13, 81, 25)]
    for ox, oy, rx, ry in lobes:
        x, y = cx + ox * scale, cy + oy * scale
        draw.ellipse((x-rx*scale, y-ry*scale, x+rx*scale, y+ry*scale),
                     fill=(255, 255, 255, alpha))
for spec in [(145, 139, .82, 218), (440, 83, .48, 188),
             (795, 155, .92, 220), (1004, 102, .48, 180)]:
    cloud_shape(*spec)
cloud = cloud.filter(ImageFilter.GaussianBlur(2.6))
im = Image.alpha_composite(im.convert("RGBA"), cloud).convert("RGB")

def hill(base_y: float, amp1: float, amp2: float, phase: float,
         top: tuple[int, int, int], bottom: tuple[int, int, int], texture: float = 0.0) -> list[float]:
    heights = []
    for x in range(W):
        n = x / (W - 1)
        y = (base_y + amp1 * math.sin(n * math.tau + phase)
             + amp2 * math.sin(n * math.tau * 2.35 + phase * .61)
             + 1.2 * math.sin(n * 91 + phase) * math.sin(n * 27 + .4))
        heights.append(y)
    mask = Image.new("L", (W, H), 0)
    md = ImageDraw.Draw(mask)
    md.polygon([(x, int(y)) for x, y in enumerate(heights)] + [(W-1, H), (0, H)], fill=255)
    layer = Image.new("RGB", (W, H))
    lp = layer.load()
    for y in range(H):
        v = max(0.0, min(1.0, (y - (base_y - amp1 - amp2)) / (H - (base_y - amp1 - amp2))))
        shade = v * .72
        for x in range(W):
            broad = math.sin(x / 190 + phase) * .018 + math.sin(x / 57 + phase) * .006
            grain = (math.sin(x * .13 + y * .21) + math.sin(x * .037 - y * .11)) * texture * .006
            lp[x, y] = tuple(max(0, min(255, int(top[c] * (1-shade) + bottom[c] * shade + (broad+grain)*255))) for c in range(3))
    im.paste(layer, (0, 0), mask)
    return heights

hill(425, 32, 23, .5, (142, 184, 132), (90, 147, 80), .2)
hill(500, 29, 30, 2.2, (88, 168, 80), (42, 128, 58), .45)
front = hill(578, 31, 23, 4.1, (73, 171, 67), (20, 112, 48), .55)

# Sparse, directional grass catches the light without turning the hill noisy.
d = ImageDraw.Draw(im)
for _ in range(2600):
    x = random.randrange(W)
    top_y = front[x]
    y = random.randrange(max(0, int(top_y) + 5), H)
    depth = (y - top_y) / max(1, H - top_y)
    if random.random() > .20 + .17 * depth:
        continue
    length = random.randrange(2, 7)
    palette = [(117, 184, 76), (88, 163, 60), (49, 139, 52), (142, 190, 79), (35, 122, 51)]
    color = random.choice(palette)
    color = tuple(int(c * (1 - .27 * depth)) for c in color)
    d.line((x, y, x + length, y - random.randrange(0, 3)), fill=color, width=1)

# A subtle warm atmospheric band visually separates the rolling ridges.
haze = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(haze).rectangle((0, 360, W, 510), fill=(225, 241, 225, 17))
haze = haze.filter(ImageFilter.GaussianBlur(21))
im = Image.alpha_composite(im.convert("RGBA"), haze).convert("RGB")
im.save(OUT, format="PNG", optimize=True)
print(f"Wrote {OUT} ({W}x{H})")
