#!/usr/bin/env python3
"""List the console's translatable strings, for writing a language file.

Usage: tools/extract_strings.py [lang/xx.txt]

With a language file, prints only the strings it doesn't translate yet,
ready to paste in and fill. Without one, prints every string.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STRING = r'"((?:[^"\\]|\\.)*)"'


def strings():
    found = []
    paths = sorted((ROOT / "src").rglob("*.cpp")) + sorted((ROOT / "src").rglob("*.hpp"))
    for path in paths:
        text = path.read_text()
        # tr("...") calls, and tr(cond ? "a" : "b").
        found += re.findall(r"\btr\(\s*" + STRING, text)
        for m in re.finditer(r"\btr\([^\"();]*\?\s*" + STRING + r"\s*:\s*" + STRING, text):
            found += [m.group(1), m.group(2)]
        # Tables whose text is passed to tr() where it's used.
        for table in re.finditer(r"(kTools|kFlags|kHelp|kWaveNames|kEffectNames|kToolNames)\[\]?[^=]*=\s*\{(.*?)\};", text, re.S):
            literals = re.findall(STRING, table.group(2))
            if table.group(1) == "kHelp":
                literals = literals[1::2]  # command, description pairs
            if table.group(1) == "kFlags":
                literals = literals[1::2]  # label, tooltip pairs
            found += [s for s in literals if " " in s or table.group(1) in ("kWaveNames", "kEffectNames", "kToolNames")]
        # Editor names and API reference descriptions.
        found += re.findall(r'const char\* name\(\) const override \{ return ' + STRING, text)
        for doc in re.finditer(r"ApiDoc\{" + STRING + r", " + STRING + r", " + STRING + r", " + STRING + r"\}", text):
            found.append(doc.group(4))
        if path.name == "shell.cpp":
            found.append("type help to see commands, or browse for carts")
    seen, out = set(), []
    for s in found:
        s = s.replace('\\"', '"')
        if s and s not in seen:
            seen.add(s)
            out.append(s)
    return out


def main():
    done = set()
    if len(sys.argv) > 1:
        for line in Path(sys.argv[1]).read_text().splitlines():
            if " = " in line and not line.startswith("#"):
                done.add(line.split(" = ", 1)[0].strip())
    for s in strings():
        if s not in done:
            print(f"{s} = ")


if __name__ == "__main__":
    main()
