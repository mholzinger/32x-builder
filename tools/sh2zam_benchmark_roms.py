#!/usr/bin/env python3
"""Generate layout-identical SH2ZAM math benchmark ROMs; diag[31] only."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('rom',type=Path);p.add_argument('--output-dir',required=True,type=Path)
a=p.parse_args();raw=a.rom.read_bytes()
if raw.count(b'DIAGCFG1')!=1:p.error('expected one DIAGCFG1 block')
off=raw.index(b'DIAGCFG1')
if off+32>len(raw):p.error('truncated diagnostics')
if raw[off+31]!=0:p.error('source must have the math diagnostic disabled')
paths=[a.output_dir/f'sh2zam-math-{i}.32x' for i in range(1,9)]
if any(f.exists() for f in paths):p.error('refusing to overwrite an existing benchmark ROM')
a.output_dir.mkdir(parents=True,exist_ok=True);manifest=[]
for i,path in enumerate(paths,1):
 b=bytearray(raw);b[off+31]=i;path.write_bytes(b)
 path.with_suffix('.mgl').write_text(f'<mistergamedescription>\n<rbf>_Console/S32X</rbf>\n<file delay="2" type="f" index="0" path="../../../usb1/Games/S32X/{path.name}"/>\n</mistergamedescription>\n')
 manifest.append(dict(page=i,sha256=hashlib.sha256(b).hexdigest()))
(a.output_dir/'rom-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.output_dir/'rom-manifest.json')
