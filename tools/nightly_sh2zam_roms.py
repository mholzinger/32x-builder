#!/usr/bin/env python3
"""Generate layout-identical real-mesh, view, clip, sharing and reuse test ROMs."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('--output-dir',type=Path,required=True)
p.add_argument('--pages',nargs='+',type=int,choices=range(1,31),default=list(range(9,31)));a=p.parse_args()
raw=a.rom.read_bytes()
if raw.count(b'DIAGCFG1')!=1:p.error('expected one DIAGCFG1 block')
off=raw.index(b'DIAGCFG1')
if off+32>len(raw) or raw[off+31]:p.error('source must have a complete, disabled math diagnostic')
if len(set(a.pages))!=len(a.pages):p.error('duplicate pages')
paths=[a.output_dir/f'nightly-page-{n}.32x' for n in a.pages]
manifest_path=a.output_dir/'manifest.json'
if any(path.exists() for path in [*paths,*(v.with_suffix('.mgl') for v in paths),manifest_path]):p.error('refusing to overwrite outputs')
a.output_dir.mkdir(parents=True,exist_ok=True);manifest=dict(source_sha256=hashlib.sha256(raw).hexdigest(),selector_offset=off+31,roms=[])
for page,path in zip(a.pages,paths):
 data=bytearray(raw);data[off+31]=page;path.write_bytes(data)
 path.with_suffix('.mgl').write_text(f'<mistergamedescription>\n<rbf>_Console/S32X</rbf>\n<file delay="2" type="f" index="0" path="../../../usb1/Games/S32X/{path.name}"/>\n</mistergamedescription>\n')
 manifest['roms'].append(dict(page=page,file=path.name,sha256=hashlib.sha256(data).hexdigest()))
manifest_path.write_text(json.dumps(manifest,indent=2)+'\n');print(manifest_path)
