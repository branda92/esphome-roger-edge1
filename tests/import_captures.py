"""Regenerate the portable fixtures from the existing decoded analysis folder.

Usage: python tests/import_captures.py /path/to/analisi-uart-2026-09-18
Only FC06 control writes, P38/P80 reads and state words are included.
Service/configuration broadcasts are deliberately excluded.
"""
import json
from pathlib import Path
import sys

analysis = Path(sys.argv[1])
out = Path(__file__).with_name("captured_fixtures.h")


def array(hex_string):
    return "{" + ", ".join(f"0x{b:02X}" for b in bytes.fromhex(hex_string)) + "}"


lines = [
    "// Generated from the user's nine .sr acquisitions; see import_captures.py.",
    "#pragma once", "#include <array>", "#include <cstdint>",
    "struct CapturedWrite { uint16_t reg, value; std::array<uint8_t, 8> request, reply; };",
    "inline const CapturedWrite CAPTURED_WRITES[] = {",
]
states = []
reads = {}
for path in sorted((analysis / "decoded").glob("*-transactions.json")):
    data = json.loads(path.read_text())
    file_id = int(data["file"].split("-")[0])
    for t in data["transactions"]:
        if t["function"] == 6 and t["reg"] in (0x1965, 0x1603, 0x1622):
            lines.append(f"  {{0x{t['reg']:04X}, 0x{t['value_or_count']:04X}, "
                         f"{array(t['request_hex'])}, {array(t['response_hex'])}}},")
        if t["function"] != 3:
            continue
        if t["reg"] == 0x157C and t["value_or_count"] == 6:
            states.append((file_id, round(t["reply_time"] * 1000), t["words"][4]))
        if t["reg"] in (0x15FE, 0x161C) and t["value_or_count"] == 10:
            parameter, offset = (38, 5) if t["reg"] == 0x15FE else (80, 6)
            value = t["words"][offset]
            reads.setdefault((parameter, value), t)
lines += ["};", "struct CapturedRead { uint8_t parameter, value; std::array<uint8_t, 8> request; "
          "std::array<uint8_t, 25> reply; };", "inline const CapturedRead CAPTURED_READS[] = {"]
for (parameter, value), t in sorted(reads.items()):
    lines.append(f"  {{{parameter}, {value}, {array(t['request_hex'])}, {array(t['response_hex'])}}},")
lines += ["};", "struct CapturedState { uint8_t file; uint32_t ms; uint16_t word; };",
          "inline const CapturedState CAPTURED_STATES[] = {"]
lines += [f"  {{{file_id}, {ms}, 0x{word:04X}}}," for file_id, ms, word in states]
lines += ["};", ""]
assert len(states) == 977 and len(reads) == 4
out.write_text("\n".join(lines))
print(f"Generated {out.name}: 977 states and 4 parameter read variants")
