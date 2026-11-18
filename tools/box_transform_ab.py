#!/usr/bin/env python3
"""Create layout-identical original/prepared box-transform diagnostic ROMs."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('rom', type=Path)
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--pose', type=int, choices=range(120), metavar='0..119',
                    help='repeat one baked pose indefinitely for hardware timing')
parser.add_argument('--include-native', action='store_true',
                    help='also emit experimental MAC and DMULS variants')
args = parser.parse_args()
arms = [(0, 'original'), (1, 'prepared')]
if args.include_native:
    arms += [(2, 'mac'), (3, 'dmuls')]
data = args.rom.read_bytes()
magic = b'DIAGCFG1'
if data.count(magic) != 1:
    parser.error('expected exactly one DIAGCFG1 block')
selector = data.index(magic) + 25
if selector + 7 > len(data):
    parser.error('truncated diagnostic block')
args.output_dir.mkdir(parents=True, exist_ok=True)
for mode, label in arms:
    path = args.output_dir / f'box-transform-{label}.32x'
    if path.resolve() == args.rom.resolve() or path.exists():
        parser.error(f'refusing to overwrite {path}')
# Validate both destinations before writing either one.
for mode, label in arms:
    patched = bytearray(data)
    patched[data.index(magic) + 10] = 1  # metrics visible from boot in both arms
    patched[selector] = mode
    if args.pose is not None:
        patched[data.index(magic) + 26] = args.pose + 1
    path = args.output_dir / f'box-transform-{label}.32x'
    path.write_bytes(patched)
    print(path)
