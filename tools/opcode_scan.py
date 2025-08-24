import re
from pathlib import Path

cpu_path = Path('emulator/src/cpu.c')
if not cpu_path.exists():
    print('cpu.c not found at', cpu_path)
    raise SystemExit(1)

text = cpu_path.read_text()
lines = text.splitlines()

handled = set()

# regexes
range_re = re.compile(r'opcode\s*>=\s*0x([0-9A-Fa-f]{1,2})\s*&&\s*opcode\s*<=\s*0x([0-9A-Fa-f]{1,2})')
or_list_re = re.compile(r'opcode\s*==\s*0x([0-9A-Fa-f]{1,2})(?:\s*\|\|\s*opcode\s*==\s*0x([0-9A-Fa-f]{1,2}))+')
# simpler: find all 0xNN in lines containing 'opcode'
hex_re = re.compile(r'0x([0-9A-Fa-f]{1,2})')

for i, line in enumerate(lines):
    if 'opcode' not in line:
        continue
    # ranges first
    for m in range_re.finditer(line):
        a = int(m.group(1), 16)
        b = int(m.group(2), 16)
        for v in range(a, b+1):
            handled.add(v)
    # find all single hex tokens on the line
    for m in hex_re.finditer(line):
        v = int(m.group(1), 16)
        handled.add(v)

# filter out unlikely opcode values: many flag masks use 0x400, 0x8000 etc. We only want 0x00..0xFF
handled = {v for v in handled if 0x00 <= v <= 0xFF}

all_ops = set(range(0x00, 0x100))
missing = sorted(all_ops - handled)
handled_sorted = sorted(handled)

# compact ranges helper
def compact(hex_list):
    if not hex_list:
        return []
    ranges = []
    start = prev = hex_list[0]
    for v in hex_list[1:]:
        if v == prev + 1:
            prev = v
            continue
        ranges.append((start, prev))
        start = prev = v
    ranges.append((start, prev))
    return ranges

handled_ranges = compact(handled_sorted)
missing_ranges = compact(missing)

print('Implemented opcode count:', len(handled_sorted))
print('Missing opcode count:', len(missing))
print()
print('Implemented ranges:')
for a,b in handled_ranges:
    if a==b:
        print(f'  0x{a:02X}')
    else:
        print(f'  0x{a:02X}-0x{b:02X}')

print('\nMissing ranges:')
for a,b in missing_ranges:
    if a==b:
        print(f'  0x{a:02X}')
    else:
        print(f'  0x{a:02X}-0x{b:02X}')

# Show a few examples of likely important missing opcodes
candidates = [0x00,0x06,0x09,0x0F,0x10,0x18,0x1A,0x20,0x24,0x2C,0x31,0x34,0x38,0x80,0xC6,0xC7,0xCD]
print('\nSample important opcodes and whether implemented:')
for c in candidates:
    print(f'  0x{c:02X}:', 'YES' if c in handled else 'NO')
