#!/usr/bin/env python3
"""Assemble the GitHub Pages site.

Usage: tools/build_site.py <web build dir> <output dir>

Needs the `markdown` package. Layout of the result:
  /            landing page (web/site)
  /console/    the full console with editors
  /play/       the player: ?cart=name, ?url=..., #c=<shared cart>
  /docs/       the markdown docs rendered to HTML
  /assets/     images from docs/assets
  /embed.js    the <glimmer-player> web component
"""

import html
import re
import shutil
import sys
from pathlib import Path

import markdown

ROOT = Path(__file__).resolve().parent.parent

DOCS = [
    ("api", "API reference", ROOT / "docs" / "api.md"),
    ("editors", "Editor guide", ROOT / "docs" / "editors.md"),
    ("spec", "Console spec", ROOT / "SPEC.md"),
    ("architecture", "Architecture", ROOT / "ARCHITECTURE.md"),
    ("contributing", "Contributing", ROOT / "CONTRIBUTING.md"),
    ("changelog", "Changelog", ROOT / "CHANGELOG.md"),
]
PAGE_FOR = {path.name.lower(): slug for slug, _, path in DOCS}


def fix_links(text):
    # Links between markdown files become links between the rendered pages.
    def repl(m):
        target, anchor = m.group(1), m.group(2) or ""
        name = Path(target).name.lower()
        if name in PAGE_FOR:
            return f"]({PAGE_FOR[name]}.html{anchor})"
        if not target.startswith("http"):
            return f"](https://github.com/aubergine-ux/glimmer/blob/main/{target.lstrip('./')}{anchor})"
        return m.group(0)

    return re.sub(r"\]\(([^)#\s]+\.md|LICENSE|\.github/[^)#\s]+)(#[^)\s]*)?\)", repl, text)


def render_docs(out):
    out.mkdir(parents=True, exist_ok=True)
    nav = "".join(f'<a href="{slug}.html">{html.escape(title)}</a>' for slug, title, _ in DOCS)
    for slug, title, path in DOCS:
        body = markdown.markdown(fix_links(path.read_text()), extensions=["tables", "fenced_code", "toc"])
        page = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)} - Glimmer</title><link rel="stylesheet" href="../site.css">
<link rel="icon" href="../assets/favicon.png"></head>
<body><div class="doc"><nav><a class="home" href="../">✦ glimmer</a>{nav}<a href="../console/">console</a></nav>
{body}</div></body></html>
"""
        (out / f"{slug}.html").write_text(page)


def make_favicon(out):
    # The sprout from the demo cart, scaled up: sprite 1 of carts/sprout.glim.
    from PIL import Image

    text = (ROOT / "carts" / "sprout.glim").read_text().splitlines()
    rows = text[text.index("[gfx]") + 1:][:8]
    palette = [0x0b0a12, 0x1f1a33, 0x3a2f5b, 0x6b4f9e, 0xa77bd8, 0xf2e9ff, 0x8a8fa8, 0x4a4e69,
               0x2b2d42, 0x5c1f2e, 0xa32d3c, 0xe8534a, 0xff8c69, 0xffc2a1, 0x7a3e1d, 0xc06a2b,
               0xf39c34, 0xffd166, 0xfff4a3, 0x1e4d2b, 0x2f8a3e, 0x6cc551, 0xc2f07a, 0x133b5c,
               0x1f6fb2, 0x3fa7f0, 0x8fdcff, 0x1d6b6b, 0x36c2a6, 0xb03f86, 0xff6fb5, 0xffffff]
    img = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        for x in range(8):
            c = int(row[8 + x], 32)
            if c:
                rgb = palette[c]
                img.putpixel((x, y), (rgb >> 16, (rgb >> 8) & 255, rgb & 255, 255))
    img.resize((32, 32), Image.NEAREST).save(out / "favicon.png")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    web, out = Path(sys.argv[1]), Path(sys.argv[2])
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(ROOT / "web" / "site", out)
    shutil.copytree(ROOT / "docs" / "assets", out / "assets", ignore=shutil.ignore_patterns("src"))
    make_favicon(out / "assets")

    console = out / "console"
    console.mkdir()
    shutil.copy(web / "glimmer.html", console / "index.html")
    for f in ("glimmer.js", "glimmer.wasm", "glimmer.data"):
        shutil.copy(web / f, console / f)

    play = out / "play"
    play.mkdir()
    template = (ROOT / "web" / "player.html").read_text()
    page = (template.replace("{{TITLE}}", "Glimmer player").replace("{{CART}}", "")
            .replace("{{RUNTIME}}", '<script>window.GLIMMER_RUNTIME_BASE = "../console/";</script>\n'
                       '  <script src="../console/glimmer.js"></script>'))
    (play / "index.html").write_text(page)
    shutil.copy(web / "glimmer_player.js", play / "glimmer_player.js")

    render_docs(out / "docs")
    print(f"site written to {out}")


if __name__ == "__main__":
    main()
