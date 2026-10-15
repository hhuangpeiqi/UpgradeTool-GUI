#!/usr/bin/env python3
"""Regenerate the locale-independent Unicode casing tables used by platform/Text."""
import unicodedata
from pathlib import Path

output = [f"// Generated from Python Unicode {unicodedata.unidata_version}. Locale-independent UTF-8 casing.\n"]
for name, transform in (("upper", str.upper), ("folded", str.casefold)):
    output.append(f"constexpr Mapping {name}[] = {{\n")
    for code in range(0x110000):
        char = chr(code)
        mapped = transform(char)
        if mapped != char:
            escaped = "".join(f"\\x{byte:02x}" for byte in mapped.encode("utf-8"))
            output.append(f'    {{0x{code:x}, "{escaped}"}},\n')
    output.append("};\n")
Path(__file__).resolve().parents[1].joinpath("platform/UnicodeData.inc").write_text("".join(output), encoding="utf-8")
