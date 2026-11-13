#!/usr/bin/env python3
"""Regenerate the images used by README.md from the console itself.

Usage: tools/readme_assets.py [path/to/glimmer]

Needs Pillow. Everything is rendered with `glimmer --headless`, so the
pictures always match the current build.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "docs" / "assets"
GLIMMER = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "glimmer"

# Demo carts shown in the README grid: (cart, frames to skip, frames to record, extra args).
CARTS = [
    ("orbit", 30, 150, []),
    ("hello", 0, 150, ["--press", "right@0-40", "--press", "down@30-70", "--press", "left@60-110"]),
]


def headless(cart, *args):
    subprocess.run([str(GLIMMER), "--headless", str(cart), *args], check=True, stdout=subprocess.DEVNULL)


def rounded(img, radius):
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, img.width - 1, img.height - 1], radius, fill=255)
    out = img.convert("RGBA")
    out.putalpha(mask)
    return out


def banner(tmp):
    src = ASSETS / "src" / "banner.glim"
    for theme, extra in (("dark", []), ("light", ["--press", "x@0-100"])):
        shot = tmp / f"banner-{theme}.png"
        headless(src, "--frames", "4", "--screenshot", str(shot), *extra)
        img = Image.open(shot).crop((0, 0, 256, 72)).resize((1024, 288), Image.NEAREST)
        rounded(img, 24).save(ASSETS / f"banner-{theme}.png", optimize=True)


def button(tmp):
    shot = tmp / "button.png"
    headless(ASSETS / "src" / "button.glim", "--frames", "4", "--screenshot", str(shot))
    img = Image.open(shot).convert("RGB").crop((0, 0, 112, 24)).resize((336, 72), Image.NEAREST)
    # Anything matching the corner pixel is background and becomes transparent.
    background = Image.new("RGB", img.size, img.getpixel((0, 0)))
    alpha = ImageChops.difference(img, background).convert("L").point(lambda v: 255 if v else 0)
    img.putalpha(alpha)
    img.save(ASSETS / "button.png", optimize=True)


def hero():
    headless(ROOT / "carts" / "orbit.glim", "--frames", "420", "--record-from", "60",
             "--record", str(ASSETS / "hero.gif"))


def carts(tmp):
    out = ASSETS / "carts"
    out.mkdir(exist_ok=True)
    for name, skip, length, extra in CARTS:
        headless(ROOT / "carts" / f"{name}.glim", "--frames", str(skip + length), "--record-from", str(skip),
                 "--record", str(out / f"{name}.gif"), *extra)


def main():
    if not GLIMMER.exists():
        sys.exit(f"build glimmer first (looked for {GLIMMER})")
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        banner(tmp)
        button(tmp)
        hero()
        carts(tmp)
    for f in sorted(ASSETS.rglob("*")):
        if f.is_file() and f.suffix in (".png", ".gif"):
            print(f"{f.relative_to(ROOT)}  {f.stat().st_size // 1024} KiB")


if __name__ == "__main__":
    main()
