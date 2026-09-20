"""Import only the 33 input request/reply pairs from the September 20 analysis.

Usage: python tests/import_input_captures.py /path/to/analisi-uart-2026-09-20
No console, identifiers, parameter tables or service writes are included.
"""
from pathlib import Path
import json
import sys

analysis = Path(sys.argv[1])
lines = [
    '// Actual input reads from the ten user-provided September 20 acquisitions.',
    '#pragma once', '#include <array>', '#include <cstdint>',
    'struct CapturedInput { const char *file; uint32_t ms; uint16_t raw, auxiliary; '
    'std::array<uint8_t, 8> request; std::array<uint8_t, 9> reply; };',
    'inline const CapturedInput CAPTURED_INPUTS[] = {',
]


def array(value):
    return '{' + ', '.join(f'0x{b:02X}' for b in bytes.fromhex(value)) + '}'


count = 0
for path in sorted((analysis / 'decoded').glob('*-transactions.json')):
    data = json.loads(path.read_text())
    for t in data['transactions']:
        if t['function'] != 3 or t['reg'] != 0x1711:
            continue
        assert t['value_or_count'] == 2
        raw, auxiliary = t['words']
        lines.append(f'  {{"{data["file"]}", {round(t["reply_time"] * 1000)}, 0x{raw:04X}, '
                     f'0x{auxiliary:04X}, {array(t["request_hex"])}, {array(t["response_hex"])}}},')
        count += 1
assert count == 33
lines += ['};', '']
Path(__file__).with_name('captured_inputs.h').write_text('\n'.join(lines))
print(f'Imported {count} captured input reads')
