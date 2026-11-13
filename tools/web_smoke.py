#!/usr/bin/env python3
"""Play each demo cart in a headless browser and check it runs.

Usage: tools/web_smoke.py <web build dir> [browser]

Serves the build, opens glimmer.html?cart=<name> for every cart in carts/,
and fails if the canvas stays blank or shows the runtime error screen.
Needs Pillow and a Chromium-based browser.
"""

import functools
import http.server
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ERROR_RED = (163, 45, 60)  # the banner across the top of the runtime error screen


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def main():
    build = Path(sys.argv[1]).resolve()
    browser = sys.argv[2] if len(sys.argv) > 2 else next(
        (b for b in ("chromium", "google-chrome", "chromium-browser") if shutil.which(b)), None)
    if not browser:
        sys.exit("no chromium-based browser found")

    handler = functools.partial(QuietHandler, directory=str(build))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]

    failed = []
    with tempfile.TemporaryDirectory() as tmp:
        for cart in sorted((ROOT / "carts").glob("*.glim")):
            shot = Path(tmp) / f"{cart.stem}.png"
            subprocess.run([browser, "--headless=new", "--no-sandbox", "--enable-unsafe-swiftshader",
                            "--use-angle=swiftshader", "--window-size=768,480", "--virtual-time-budget=8000",
                            f"--screenshot={shot}", f"http://127.0.0.1:{port}/glimmer.html?cart={cart.stem}"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
            if not shot.exists():
                failed.append(f"{cart.stem}: no screenshot")
                continue
            # Look only at the canvas area, above the page footer.
            img = Image.open(shot).convert("RGB").crop((0, 0, 768, 440))
            colors = img.getcolors(maxcolors=1 << 16) or []
            counts = {c: n for n, c in colors}
            area = img.width * img.height
            if len(colors) and len(colors) <= 2:
                problem = "nothing drawn"
            elif counts.get(ERROR_RED, 0) > area * 0.03:
                problem = "runtime error"
            else:
                problem = None
            print(f"{cart.stem}: {problem or 'ok'}")
            if problem:
                failed.append(f"{cart.stem}: {problem}")
    server.shutdown()
    if failed:
        sys.exit("web smoke test failed:\n  " + "\n  ".join(failed))


if __name__ == "__main__":
    main()
