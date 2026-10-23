#!/usr/bin/env python3
"""Fill the en_XA pseudo-locale catalog after `lupdate`.

Every message without a finished translation gets its source text with vowels and a few consonants accented and the
whole string bracketed, so a run with GLANCELOT_LANGUAGE=en_XA shows untranslatable text (plain) and clipping (the
brackets and wider glyphs). Placeholders (%1, %L1, %n) are kept exactly. Existing finished translations are left alone.

Usage: python3 translations/pseudolocalize.py translations/glancelot_en_XA.ts
"""
import re
import sys
import xml.etree.ElementTree as ET

ACCENTS = str.maketrans("aeiouncyAEIOUNCY", "àéîõûñçýÀÉÎÕÛÑÇÝ")
PLACEHOLDER = re.compile(r"%L?\d+|%n")


def pseudo(text: str) -> str:
    parts, last = [], 0
    for match in PLACEHOLDER.finditer(text):
        parts.append(text[last:match.start()].translate(ACCENTS))
        parts.append(match.group(0))
        last = match.end()
    parts.append(text[last:].translate(ACCENTS))
    return "[" + "".join(parts) + "]"


def main(path: str) -> int:
    tree = ET.parse(path)
    filled = 0
    for message in tree.getroot().iter("message"):
        source = message.find("source")
        translation = message.find("translation")
        if source is None or translation is None or source.text is None:
            continue
        if translation.get("type") in ("unfinished", None) and (translation.get("type") == "unfinished" or not translation.text):
            translation.text = pseudo(source.text)
            translation.attrib.pop("type", None)
            filled += 1
    tree.write(path, encoding="utf-8", xml_declaration=True)
    with open(path, "r+", encoding="utf-8") as handle:  # keep lupdate's DOCTYPE line, which ElementTree drops
        body = handle.read().replace("<?xml version='1.0' encoding='utf-8'?>", '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>', 1)
        body = re.sub(r"(<(source|translation)[^>]*>)(.*?)(</\2>)", lambda m: m.group(1) + m.group(3).replace("'", "&apos;").replace('"', "&quot;") + m.group(4), body, flags=re.S)
        handle.seek(0); handle.write(body + ("" if body.endswith("\n") else "\n")); handle.truncate()
    print(f"{path}: {filled} message(s) pseudo-localized")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "translations/glancelot_en_XA.ts"))
