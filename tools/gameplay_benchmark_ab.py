#!/usr/bin/env python3
"""Generate fixed-scene, layout-identical renderer benchmark ROMs."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('rom', type=Path)
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--experiment', choices=['ceiling', 'quiet-audio', 'balanced-walking', 'furniture-divu', 'furniture-divu-blocking', 'furniture-mac'], default='ceiling')
parser.add_argument('--resolution', choices=['full', 'half', 'balanced'], default='full')
parser.add_argument('--scene', type=int, choices=range(1,10), action='append')
parser.add_argument('--motion', action='store_true', help='exercise walking render paths at the fixed pose')
args = parser.parse_args()
base = args.rom.read_bytes()
if base.count(b'DIAGCFG1') != 1:
    parser.error('expected exactly one DIAGCFG1 block')
if args.experiment == 'furniture-mac' and b'FURNMAC1' not in base:
    parser.error('requires a ROM built with make FURNITURE_MAC_EXPERIMENT=1')
offset = base.index(b'DIAGCFG1')
if offset + 32 > len(base):
    parser.error('truncated diagnostic block')
scenes = [(1, 'corridor'), (2, 'room'), (3, 'crawl'), (4, 'partitions'), (5, 'testbed'), (6, 'chair'), (7, 'desk'), (8, 'pvm'), (9, 'deskset')]
scenes = [s for s in scenes if s[0] in (args.scene or [1, 2, 3, 4])]
outputs = [(scene, name, arm, args.output_dir / f'{name}-{arm}.32x')
           for scene, name in scenes for arm in (0, 1)]
manifest_path = args.output_dir / 'manifest.json'
for path in [*(entry[3] for entry in outputs), manifest_path]:
    if path.exists() or path.resolve() == args.rom.resolve():
        parser.error(f'refusing to overwrite {path}')
args.output_dir.mkdir(parents=True, exist_ok=True)
manifest = {'source_sha256': hashlib.sha256(base).hexdigest(), 'experiment': args.experiment, 'resolution': 'FULL/BALANCED' if args.experiment == 'balanced-walking' else args.resolution, 'roms': []}
for scene, name, arm, path in outputs:
    data = bytearray(base)
    # Explicitly initialize every diagnostic byte, including other experiments.
    data[offset + 8:offset + 32] = bytes(24)
    flags = {9: 1, 10: 1, 13: (1 if arm == 0 else 8) if args.experiment == 'balanced-walking' else {'full': 1, 'half': 2, 'balanced': 8}[args.resolution], 14: 2 if args.motion or args.experiment == 'balanced-walking' else 0, 18: 1, 27: scene,
             28: arm if args.experiment == 'ceiling' else 1,
             29: arm * 3 if args.experiment == 'furniture-mac' else (arm * (2 if args.experiment == 'furniture-divu' else 1) if args.experiment.startswith('furniture-divu') else 0),
             30: arm if args.experiment == 'quiet-audio' else (1 if (args.experiment == 'balanced-walking' or args.experiment.startswith('furniture-divu') or args.experiment == 'furniture-mac') else 0)}
    for index, value in flags.items():
        data[offset + index] = value
    path.write_bytes(data)
    manifest['roms'].append({'file': path.name, 'scene': scene,
                             'hardware_divide': bool(flags[28]), 'quiet_audio_fill': bool(flags[30]), 'flags': flags,
                             'sha256': hashlib.sha256(data).hexdigest()})
    print(path)
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
